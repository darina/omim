#pragma once

#include "coding/reader.hpp"
#include "coding/varint.hpp"
#include "coding/write_to_sink.hpp"

#include <cstdint>

using LocalizableString = std::unordered_map<uint8_t, std::string>;
using LocalizableStringSubIndex = std::map<uint8_t, uint32_t>;
using LocalizableStringIndex = std::vector<LocalizableStringSubIndex>;

template<typename Sink>
inline void WriteLocalizableStringIndex(Sink & sink, LocalizableStringIndex const & index)
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
inline void ReadLocalizableStringIndex(Source & source, LocalizableStringIndex & index)
{
  auto const indexSize = ReadVarUint<uint32_t, Source>(source);
  index.reserve(indexSize);
  for (uint32_t i = 0; i < indexSize; ++i)
  {
    index.emplace_back(LocalizableStringSubIndex());
    auto const subIndexSize = ReadVarUint<uint32_t, Source>(source);
    for (uint32_t j = 0; j < subIndexSize; ++j)
    {
      auto const lang = ReadPrimitiveFromSource<int8_t>(source);
      auto const strIndex = ReadVarUint<uint32_t, Source>(source);
      index.back()[lang] = strIndex;
    }
  }
}
