#include "descriptions/serdes.hpp"

namespace descriptions
{
Serializer::Serializer(DescriptionsCollection && descriptions)
{
  m_featureIndices.reserve(descriptions.size());
  m_index.reserve(descriptions.size());

  size_t stringsCount = 0;
  std::map<LangCode, std::vector<std::string>> groupedByLang;
  for (auto & pair : descriptions)
  {
    stringsCount += pair.second.size();
    m_featureIndices.push_back(pair.first);

    LocalizableStringSubIndex subIndex;
    for (auto & translate : pair.second)
    {
      auto & group = groupedByLang[translate.first];
      subIndex.insert(std::make_pair(translate.first, static_cast<uint32_t>(group.size())));
      group.push_back(std::move(translate.second));
    }
    m_index.push_back(subIndex);
  }

  m_stringsCollection.reserve(stringsCount);

  std::map<LangCode, uint32_t> indicesOffsets;
  uint32_t currentOffset = 0;
  for (auto & pair : groupedByLang)
  {
    indicesOffsets.insert(std::make_pair(pair.first, currentOffset));
    currentOffset += pair.second.size();
    std::move(pair.second.begin(), pair.second.end(), std::back_inserter(m_stringsCollection));
  }

  for (auto & subIndex : m_index)
  {
    for (auto & translate : subIndex)
      translate.second += indicesOffsets[translate.first];
  }
}
}  // descriptions

