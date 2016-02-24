#pragma once

#include "indexer/feature_decl.hpp"

#include "base/buffer_vector.hpp"
#include "base/mutex.hpp"

#include "std/set.hpp"
#include "std/utility.hpp"
#include "std/vector.hpp"
#include "std/noncopyable.hpp"

namespace df
{

using TFeaturesInfo = map<FeatureID, bool>;

class MemoryFeatureIndex : private noncopyable
{
  static int const kHashSize = 10;
public:
  MemoryFeatureIndex()
  {
    for (size_t i = 0; i < kHashSize; ++i)
      m_features[i].reset(new set<FeatureID>);
  }

  class Lock
  {
    std::vector<threads::MutexGuard> lock;
    MemoryFeatureIndex & m_index;
    int m_hashInd;
  public:
    Lock(MemoryFeatureIndex & index, int hashInd)
      : m_index(index)
      , m_hashInd(hashInd)
    {
      if (hashInd >= 0)
      {
        lock.emplace_back(m_index.m_mutex[hashInd]);
        m_index.m_isLocked[hashInd] = true;
      }
      else
      {
        lock.reserve(kHashSize);
        for (size_t i = 0; i < kHashSize; ++i)
        {
          lock.emplace_back(m_index.m_mutex[i]);
          m_index.m_isLocked[i] = true;
        }
      }
    }

    ~Lock()
    {
      if (m_hashInd < 0)
      {
        for (size_t i = 0; i < kHashSize; ++i)
          m_index.m_isLocked[i] = false;
      }
      else
      {
        m_index.m_isLocked[m_hashInd] = false;
      }
    }
  };

  void ReadFeaturesRequest(const TFeaturesInfo & features, vector<FeatureID> & featuresToRead);
  bool SetFeatureOwner(const FeatureID & feature);
  void RemoveFeatures(TFeaturesInfo & features);

  static int Hash(const FeatureID & feature)
  {
    return feature.m_index % kHashSize;
  }

private:
  bool m_isLocked[kHashSize] = { false };
  threads::Mutex m_mutex[kHashSize];
  shared_ptr<set<FeatureID>> m_features[kHashSize];
};

} // namespace df
