#pragma once

#include "descriptions/header.hpp"

#include "indexer/feature_decl.hpp"

#include "coding/dd_vector.hpp"
#include "coding/localizable_string.hpp"
#include "coding/text_storage.hpp"

#include <map>
#include <set>
#include <string>

namespace descriptions
{
using FeatureIndex = uint32_t;
using StringIndex = uint32_t;
using LangCode = uint8_t;

enum class Version : uint8_t
{
  V0 = 0,
  Latest = V0
};

using DescriptionsCollection = std::map<FeatureIndex, LocalizableString>;

class Serializer
{
public:
  Serializer(DescriptionsCollection && descriptions);

  template <typename Sink>
  void Serialize(Sink & sink)
  {
    WriteToSink(sink, static_cast<uint8_t>(Version::Latest));

    auto const startPos = sink.Pos();

    HeaderV0 header;
    WriteZeroesToSink(sink, header.Size());

    header.m_featuresOffset = sink.Pos() - startPos;
    SerializeFeaturesIndices(sink);

    header.m_stringsIndexOffset = sink.Pos() - startPos;
    SerializeStringIndex(sink);

    header.m_stringsOffset = sink.Pos() - startPos;
    SerializeStrings(sink);

    header.m_eosOffset = sink.Pos() - startPos;
    sink.Seek(startPos);
    header.Serialize(sink);
    sink.Seek(startPos + header.m_eosOffset);
  }

  // Serializes a vector of 32-bit sorted feature ids.
  template <typename Sink>
  void SerializeFeaturesIndices(Sink & sink)
  {
    for (auto const & index : m_featureIndices)
      WriteToSink(sink, index);
  }

  template <typename Sink>
  void SerializeStringIndex(Sink & sink)
  {
    WriteLocalizableStringIndex(sink, m_index);
  }

  // Serializes strings in a compressed storage with block access.
  template <typename Sink>
  void SerializeStrings(Sink & sink)
  {
    coding::BlockedTextStorageWriter<Sink> writer(sink, 200000 /* blockSize */);
    for (auto const & str : m_stringsCollection)
      writer.Append(str);
  }

private:
  std::vector<FeatureIndex> m_featureIndices;
  LocalizableStringIndex m_index;
  std::vector<std::string> m_stringsCollection;
};

class Deserializer
{
public:
  template <typename R>
  bool Deserialize(R & reader, FeatureIndex featureIndex, std::vector<LangCode> const & langPriority,
                   std::string & description)
  {
    NonOwningReaderSource source(reader);
    auto const version = static_cast<Version>(ReadPrimitiveFromSource<uint8_t>(source));

    auto subReader = reader.CreateSubReader(source.Pos(), source.Size());

    switch (version)
    {
    case Version::V0: return DeserializeV0(*subReader, featureIndex, langPriority, description);
    default: ASSERT(false, ("Cannot deserialize descriptions for version", static_cast<uint8_t>(version)));
    }

    return false;
  }

  template <typename R>
  bool DeserializeV0(R & reader, FeatureIndex featureIndex, std::vector<LangCode> const & langPriority,
                     std::string & description)
  {
    InitializeIfNeeded(reader);

    uint32_t d = 0;
    {
      ReaderPtr<Reader> idsSubReader(CreateFeatureIndicesSubReader(reader));
      DDVector<FeatureIndex, ReaderPtr<Reader>> ids(idsSubReader);
      auto const it = std::lower_bound(ids.begin(), ids.end(), featureIndex);
      if (it == ids.end() || *it != featureIndex)
        return false;

      d = static_cast<uint32_t>(distance(ids.begin(), it));
    }

    LocalizableStringSubIndex subIndex;
    {
      LocalizableStringIndex index;
      auto indexSubReader = CreateStringsIndexSubReader(reader);
      NonOwningReaderSource source(*indexSubReader);
      ReadLocalizableStringIndex(source, index);
      subIndex = index[d];
    }

    auto stringsSubReader = CreateStringsSubReader(reader);
    for (auto const lang : langPriority)
    {
      auto const it = subIndex.find(lang);
      if (it != subIndex.end())
      {
        description = m_stringsReader.ExtractString(*stringsSubReader, it->second);
        return true;
      }
    }

    return false;
  }

  uint64_t GetNumDescriptions()
  {
    ASSERT(m_initialized, ());

    ASSERT_GREATER_OR_EQUAL(m_header.m_stringsIndexOffset, m_header.m_featuresOffset, ());
    auto const totalSize = m_header.m_stringsIndexOffset - m_header.m_featuresOffset;

    size_t constexpr kIndexOffset = sizeof(FeatureIndex);
    ASSERT(totalSize % kIndexOffset == 0, (totalSize));

    return totalSize / kIndexOffset;
  }

  template <typename R>
  std::unique_ptr<Reader> CreateFeatureIndicesSubReader(R & reader)
  {
    ASSERT(m_initialized, ());

    auto const pos = m_header.m_featuresOffset;
    auto const n = GetNumDescriptions();
    return reader.CreateSubReader(pos, n * sizeof(FeatureIndex));
  }

  template <typename R>
  std::unique_ptr<Reader> CreateStringsIndexSubReader(R & reader)
  {
    ASSERT(m_initialized, ());

    auto const pos = m_header.m_stringsIndexOffset;
    ASSERT_GREATER_OR_EQUAL(m_header.m_stringsOffset, pos, ());
    auto const size = m_header.m_stringsOffset - pos;
    return reader.CreateSubReader(pos, size);
  }

  template <typename R>
  std::unique_ptr<Reader> CreateStringsSubReader(R & reader)
  {
    ASSERT(m_initialized, ());

    auto const pos = m_header.m_stringsOffset;
    ASSERT_GREATER_OR_EQUAL(m_header.m_eosOffset, pos, ());
    auto const size = m_header.m_eosOffset - pos;
    return reader.CreateSubReader(pos, size);
  }

private:
  template <typename Reader>
  void InitializeIfNeeded(Reader & reader)
  {
    if (m_initialized)
      return;

    {
      NonOwningReaderSource source(reader);
      m_header.Deserialize(source);
    }

    m_initialized = true;
  }

  bool m_initialized = false;
  HeaderV0 m_header;
  coding::BlockedTextStorageReader m_stringsReader;
};
}  // namespace descriptions
