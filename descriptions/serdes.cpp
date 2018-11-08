#include "descriptions/serdes.hpp"

#include <utility>

namespace descriptions
{
Serializer::Serializer(DescriptionsCollection && descriptions)
  : m_descriptions(std::move(descriptions))
{
  std::sort(m_descriptions.begin(), m_descriptions.end(),
            [](FeatureDescription const & lhs, FeatureDescription const & rhs) {
    return lhs.m_featureIndex < rhs.m_featureIndex;
  });

  m_index.reserve(m_descriptions.size());

  size_t stringsCount = 0;

  for (size_t i = 0; i < m_descriptions.size(); ++i)
  {
    auto & index = m_descriptions[i];

    coding::LocalizableStringSubIndex subIndex;
    index.m_description.ForEach([this, &stringsCount, &subIndex, i](LangCode lang, std::string const & str)
                                {
                                  ++stringsCount;
                                  auto & group = m_groupedByLang[lang];
                                  subIndex.insert(std::make_pair(lang, static_cast<uint32_t>(group.size())));
                                  group.push_back(i);
                                });
    m_index.push_back(subIndex);
  }

  std::map<LangCode, uint32_t> indicesOffsets;
  uint32_t currentOffset = 0;
  for (auto & pair : m_groupedByLang)
  {
    indicesOffsets.insert(std::make_pair(pair.first, currentOffset));
    currentOffset += pair.second.size();
  }

  for (auto & subIndex : m_index)
  {
    for (auto & translate : subIndex)
      translate.second += indicesOffsets[translate.first];
  }
}
}  // namespace descriptions
