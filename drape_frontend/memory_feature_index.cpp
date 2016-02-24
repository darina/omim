#include "drape_frontend/memory_feature_index.hpp"

namespace df
{

void MemoryFeatureIndex::ReadFeaturesRequest(TFeaturesInfo const & features, vector<FeatureID> & featuresToRead)
{
#ifdef DEBUG
  for (int i = 0; i < kHashSize; ++i)
    ASSERT(m_isLocked[i], ());
#endif

  for (auto const & featureInfo : features)
  {
    if (m_features[Hash(featureInfo.first)]->find(featureInfo.first) == m_features[Hash(featureInfo.first)]->end())
      featuresToRead.push_back(featureInfo.first);
  }
}

bool MemoryFeatureIndex::SetFeatureOwner(FeatureID const & feature)
{
  ASSERT(m_isLocked[Hash(feature)], ());

  return m_features[Hash(feature)]->insert(feature).second;
}

void MemoryFeatureIndex::RemoveFeatures(TFeaturesInfo & features)
{
#ifdef DEBUG
  for (int i = 0; i < kHashSize; ++i)
    ASSERT(m_isLocked[i], ());
#endif

  for (auto & featureInfo : features)
  {
    if (featureInfo.second)
    {
      VERIFY(m_features[Hash(featureInfo.first)]->erase(featureInfo.first) == 1, ());
      featureInfo.second = false;
    }
  }
}

} // namespace df
