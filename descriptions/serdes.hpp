#pragma once

#include "descriptions/header.hpp"

#include "indexer/feature_decl.hpp"

#include "coding/dd_vector.hpp"
#include "coding/multilang_utf8_string.hpp"
#include "coding/text_storage.hpp"

#include "base/assert.hpp"
#include "base/stl_helpers.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <utility>

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
/// * version
/// * header
/// * sorted feature ids vector
/// * vector of unordered maps with language codes and string indices of corresponding translations of a description
/// * vector of maps offsets for each feature id (and one additional dummy offset in the end)
/// * BWT-compressed strings grouped by language.
class Serializer
{
public:
  /// \param descriptions A non-empty unsorted collection of feature descriptions.
  ///                     FeatureDescription::m_description must contain non-empty translations.
  explicit Serializer(DescriptionsCollection && descriptions);

  template <typename Sink>
  void Serialize(Sink & sink)
  {
    WriteToSink(sink, static_cast<uint8_t>(Version::Latest));

    auto const startPos = sink.Pos();

    HeaderV0 header;
    header.Serialize(sink);

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
    CHECK(std::is_sorted(m_descriptions.begin(), m_descriptions.end(),
                         base::LessBy(&FeatureDescription::m_featureIndex)), ());

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
    offsets.push_back(static_cast<LangMetaOffset>(sink.Pos() - startPos));
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
    for (auto const & langIndices : m_groupedByLang)
    {
      for (auto const & descIndex : langIndices.second)
      {
        auto const found = m_descriptions[descIndex].m_description.GetString(langIndices.first, str);
        CHECK(found, ());
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

    LangMetaOffset startOffset = 0;
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
      CHECK_LESS(d, ofs.size(), ());
      CHECK_LESS(d + 1, ofs.size(), ());

      startOffset = ofs[d];
      endOffset = ofs[d + 1];
    }

    LangMeta langMeta;
    {
      auto langMetaSubReader = CreateLangMetaSubReader(reader, startOffset, endOffset);
      NonOwningReaderSource source(*langMetaSubReader);

      while (source.Size() > 0)
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
    CHECK(m_initialized, ());

    auto const pos = m_header.m_featuresOffset;
    CHECK_GREATER_OR_EQUAL(m_header.m_langMetaOffset, pos, ());
    auto const size = m_header.m_langMetaOffset - pos;
    return reader.CreateSubReader(pos, size);
  }

  template <typename Reader>
  std::unique_ptr<Reader> CreateLangMetaOffsetsSubReader(Reader & reader)
  {
    CHECK(m_initialized, ());

    auto const pos = m_header.m_indexOffset;
    CHECK_GREATER_OR_EQUAL(m_header.m_stringsOffset, pos, ());
    auto const size = m_header.m_stringsOffset - pos;
    return reader.CreateSubReader(pos, size);
  }

  template <typename Reader>
  std::unique_ptr<Reader> CreateLangMetaSubReader(Reader & reader, LangMetaOffset startOffset, LangMetaOffset endOffset)
  {
    CHECK(m_initialized, ());

    auto const pos = m_header.m_langMetaOffset + startOffset;
    CHECK_GREATER_OR_EQUAL(m_header.m_indexOffset, pos, ());
    auto const size = endOffset - startOffset;
    CHECK_GREATER_OR_EQUAL(m_header.m_indexOffset, pos + size, ());
    return reader.CreateSubReader(pos, size);
  }

  template <typename Reader>
  std::unique_ptr<Reader> CreateStringsSubReader(Reader & reader)
  {
    CHECK(m_initialized, ());

    auto const pos = m_header.m_stringsOffset;
    CHECK_GREATER_OR_EQUAL(m_header.m_eosOffset, pos, ());
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
