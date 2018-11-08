#pragma once

#include "descriptions/header.hpp"

#include "indexer/feature_decl.hpp"

#include "coding/dd_vector.hpp"
#include "coding/localizable_string_index.hpp"
#include "coding/multilang_utf8_string.hpp"
#include "coding/text_storage.hpp"

#include "base/assert.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace descriptions
{
using FeatureIndex = uint32_t;
using StringIndex = uint32_t;
using LangCode = int8_t;

enum class Version : uint8_t
{
  V0 = 0,
  Latest = V0
};

struct FeatureDescription
{
  FeatureDescription() = default;
  FeatureDescription(FeatureIndex index, StringUtf8Multilang && description)
    : m_featureIndex(index)
    , m_description(std::move(description))
  {}

  FeatureIndex m_featureIndex = 0;
  StringUtf8Multilang m_description;
};
using DescriptionsCollection = std::vector<FeatureDescription>;

class Serializer
{
public:
  explicit Serializer(DescriptionsCollection && descriptions);

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
    for (auto const & index : m_descriptions)
      WriteToSink(sink, index.m_featureIndex);
  }

  template <typename Sink>
  void SerializeStringIndex(Sink & sink)
  {
    coding::WriteLocalizableStringIndex(sink, m_index);
  }

  // Serializes strings in a compressed storage with block access.
  template <typename Sink>
  void SerializeStrings(Sink & sink)
  {
    coding::BlockedTextStorageWriter<Sink> writer(sink, 200000 /* blockSize */);
    std::string str;
    for (auto const & pair : m_groupedByLang)
    {
      for (auto const & descIndex : pair.second)
      {
        auto const found = m_descriptions[descIndex].m_description.GetString(pair.first, str);
        ASSERT(found, ());
        writer.Append(str);
      }
    }
  }

private:
  DescriptionsCollection m_descriptions;
  coding::LocalizableStringIndex m_index;
  std::map<LangCode, std::vector<size_t>> m_groupedByLang;
};

class Deserializer
{
public:
  template <typename Reader>
  bool Deserialize(Reader & reader, FeatureIndex featureIndex, std::vector<LangCode> const & langPriority,
                   std::string & description)
  {
    NonOwningReaderSource source(reader);
    auto const version = static_cast<Version>(ReadPrimitiveFromSource<uint8_t>(source));

    auto subReader = reader.CreateSubReader(source.Pos(), source.Size());
    CHECK(subReader, ());

    switch (version)
    {
    case Version::V0: return DeserializeV0(*subReader, featureIndex, langPriority, description);
    }
    CHECK_SWITCH();

    return false;
  }

  template <typename Reader>
  bool DeserializeV0(Reader & reader, FeatureIndex featureIndex, std::vector<LangCode> const & langPriority,
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

      d = static_cast<uint32_t>(std::distance(ids.begin(), it));
    }

    coding::LocalizableStringSubIndex subIndex;
    {
      coding::LocalizableStringIndex index;
      auto indexSubReader = CreateStringsIndexSubReader(reader);
      NonOwningReaderSource source(*indexSubReader);
      coding::ReadLocalizableStringIndex(source, index);
      CHECK(d < index.size(), ());
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

  template <typename Reader>
  std::unique_ptr<Reader> CreateFeatureIndicesSubReader(Reader & reader)
  {
    ASSERT(m_initialized, ());

    auto const pos = m_header.m_featuresOffset;
    ASSERT_GREATER_OR_EQUAL(m_header.m_stringsIndexOffset, pos, ());
    auto const size = m_header.m_stringsIndexOffset - pos;
    return reader.CreateSubReader(pos, size);
  }

  template <typename Reader>
  std::unique_ptr<Reader> CreateStringsIndexSubReader(Reader & reader)
  {
    ASSERT(m_initialized, ());

    auto const pos = m_header.m_stringsIndexOffset;
    ASSERT_GREATER_OR_EQUAL(m_header.m_stringsOffset, pos, ());
    auto const size = m_header.m_stringsOffset - pos;
    return reader.CreateSubReader(pos, size);
  }

  template <typename Reader>
  std::unique_ptr<Reader> CreateStringsSubReader(Reader & reader)
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
