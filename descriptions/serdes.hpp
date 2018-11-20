#pragma once

#include "descriptions/header.hpp"

#include "indexer/feature_decl.hpp"

#include "coding/dd_vector.hpp"
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
using LangMeta = std::unordered_map<LangCode, StringIndex>;
using LangMetaOffset = uint32_t;

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

/// \brief
/// Section name: "descriptions".
/// Description: keeping text descriptions of features in different languages.
/// Section tables:
/// * header
/// * sorted feature ids vector
/// * vector of unordered maps with language codes and string indices of corresponding translations of a description
/// * vector of maps offsets for each feature id
/// * BWT-compressed strings grouped by language.
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

    std::vector<LangMetaOffset> offsets;
    header.m_langMetaOffset = sink.Pos() - startPos;
    SerializeLangMetaCollection(sink, offsets);

    header.m_indexOffset = sink.Pos() - startPos;
    SerializeLangMetaIndex(sink, offsets);

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
  void SerializeLangMetaCollection(Sink & sink, std::vector<LangMetaOffset> & offsets)
  {
    auto const startPos = sink.Pos();
    for (auto const & meta : m_langMetaCollection)
    {
      offsets.push_back(static_cast<LangMetaOffset>(sink.Pos() - startPos));
      for (auto const & pair : meta)
      {
        WriteToSink(sink, pair.first);
        WriteVarUint(sink, pair.second);
      }
    }
  }

  template <typename Sink>
  void SerializeLangMetaIndex(Sink & sink, std::vector<LangMetaOffset> const & offsets)
  {
    for (auto const & offset : offsets)
      WriteToSink(sink, offset);
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
  std::vector<LangMeta> m_langMetaCollection;
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

    LangMetaOffset offset = 0;
    LangMetaOffset endOffset = 0;
    {
      ReaderPtr<Reader> idsSubReader(CreateFeatureIndicesSubReader(reader));
      DDVector<FeatureIndex, ReaderPtr<Reader>> ids(idsSubReader);
      auto const it = std::lower_bound(ids.begin(), ids.end(), featureIndex);
      if (it == ids.end() || *it != featureIndex)
        return false;

      auto const d = static_cast<uint32_t>(std::distance(ids.begin(), it));

      ReaderPtr<Reader> ofsSubReader(CreateLangMetaOffsetsSubReader(reader));
      DDVector<LangMetaOffset, ReaderPtr<Reader>> ofs(ofsSubReader);
      offset = ofs[d];
      endOffset = d < ids.size() - 1 ? ofs[d + 1]
                                     : static_cast<LangMetaOffset>(m_header.m_indexOffset - m_header.m_langMetaOffset);
    }

    LangMeta langMeta;
    {
      auto langMetaSubReader = CreateLangMetaSubReader(reader);
      NonOwningReaderSource source(*langMetaSubReader);
      auto startPos = source.Pos();

      source.Skip(offset);
      while (source.Pos() - startPos < endOffset)
      {
        auto const lang = ReadPrimitiveFromSource<LangCode>(source);
        auto const stringIndex = ReadVarUint<StringIndex>(source);
        langMeta.insert(std::make_pair(lang, stringIndex));
      }
    }

    auto stringsSubReader = CreateStringsSubReader(reader);
    for (auto const lang : langPriority)
    {
      auto const it = langMeta.find(lang);
      if (it != langMeta.end())
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
    ASSERT_GREATER_OR_EQUAL(m_header.m_langMetaOffset, pos, ());
    auto const size = m_header.m_langMetaOffset - pos;
    return reader.CreateSubReader(pos, size);
  }

  template <typename Reader>
  std::unique_ptr<Reader> CreateLangMetaOffsetsSubReader(Reader & reader)
  {
    ASSERT(m_initialized, ());

    auto const pos = m_header.m_indexOffset;
    ASSERT_GREATER_OR_EQUAL(m_header.m_stringsOffset, pos, ());
    auto const size = m_header.m_stringsOffset - pos;
    return reader.CreateSubReader(pos, size);
  }

  template <typename Reader>
  std::unique_ptr<Reader> CreateLangMetaSubReader(Reader & reader)
  {
    ASSERT(m_initialized, ());

    auto const pos = m_header.m_langMetaOffset;
    ASSERT_GREATER_OR_EQUAL(m_header.m_indexOffset, pos, ());
    auto const size = m_header.m_indexOffset - pos;
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
