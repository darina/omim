#pragma once

#include "coding/reader.hpp"
#include "coding/varint.hpp"
#include "coding/write_to_sink.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>

namespace coding
{
using LangCode = int8_t;
using StringIndex = uint32_t;
using LocalizableString = std::unordered_map<LangCode, std::string>;
using LocalizableStringSubIndex = std::map<LangCode, StringIndex>;
using LocalizableStringIndex = std::vector<LocalizableStringSubIndex>;

template<typename Sink>
void WriteLocalizableStringIndex(Sink & sink, LocalizableStringIndex const & index)
{
  WriteVarUint(sink, static_cast<uint32_t>(index.size()));
  for (auto const & subIndex : index)
  {
    WriteVarUint(sink, static_cast<uint32_t>(subIndex.size()));
    for (auto const & p : subIndex)
    {
      WriteToSink(sink, p.first);
      WriteVarUint(sink, p.second);
    }
  }
}

template<typename Source>
void ReadLocalizableStringIndex(Source & source, LocalizableStringIndex & index)
{
  auto const indexSize = ReadVarUint<uint32_t, Source>(source);
  index.reserve(indexSize);
  for (uint32_t i = 0; i < indexSize; ++i)
  {
    index.emplace_back(LocalizableStringSubIndex());
    auto & subIndex = index.back();
    auto const subIndexSize = ReadVarUint<uint32_t, Source>(source);
    for (uint32_t j = 0; j < subIndexSize; ++j)
    {
      auto const lang = ReadPrimitiveFromSource<int8_t>(source);
      auto const strIndex = ReadVarUint<uint32_t, Source>(source);
      subIndex[lang] = strIndex;
    }
  }
}
}  // namespace coding
