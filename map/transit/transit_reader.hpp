#pragma once

#include "transit/transit_display_info.hpp"

#include "drape_frontend/drape_engine_safe_ptr.hpp"

#include "geometry/screenbase.hpp"

#include "indexer/feature_decl.hpp"
#include "indexer/index.hpp"

#include "base/thread.hpp"
#include "base/thread_pool.hpp"

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using FeatureCallback = std::function<void (FeatureType const &)>;
using TReadFeaturesFn = std::function<void (FeatureCallback const & , std::vector<FeatureID> const &)>;

class ReadTransitTask: public threads::IRoutine
{
public:
  ReadTransitTask(Index & index,
                  TReadFeaturesFn const & readFeaturesFn)
    : m_index(index), m_readFeaturesFn(readFeaturesFn)
  {}

  void Init(uint64_t id, MwmSet::MwmId const & mwmId,
            std::unique_ptr<TransitDisplayInfo> transitInfo = nullptr);
  uint64_t GetId() const { return m_id; }
  bool GetSuccess() const { return m_success; }

  void Do() override;
  void Reset() override;

  std::unique_ptr<TransitDisplayInfo> && GetTransitInfo();

private:
  template <typename T, typename TID>
  void FillItemsByIdMap(std::vector<T> const & items, std::map<TID, T> & itemsById)
  {
    for (auto const & item : items)
    {
      if (!m_loadSubset)
      {
        itemsById.insert(make_pair(item.GetId(), item));
      }
      else
      {
        auto it = itemsById.find(item.GetId());
        if (it != itemsById.end())
          it->second = item;
      }
    }
  };

  Index & m_index;
  TReadFeaturesFn m_readFeaturesFn;

  uint64_t m_id = 0;
  MwmSet::MwmId m_mwmId;
  std::unique_ptr<TransitDisplayInfo> m_transitInfo;

  bool m_loadSubset = false;
  // Sets to true if Do() method was executed successfully.
  bool m_success = false;
};

class TransitReadManager
{
public:
  using GetMwmsByRectFn = function<vector<MwmSet::MwmId>(m2::RectD const &)>;


  TransitReadManager(Index & index, TReadFeaturesFn const & readFeaturesFn,
                     GetMwmsByRectFn const & getMwmsByRectFn);
  ~TransitReadManager();

  void Start();
  void Stop();

  void SetDrapeEngine(ref_ptr<df::DrapeEngine> engine);

  bool GetTransitDisplayInfo(TransitDisplayInfos & transitDisplayInfos);

  void UpdateViewport(ScreenBase const & screen);

  // TODO(@darina) Clear cache for deleted mwm.
  //void OnMwmDeregistered(MwmSet::MwmId const & mwmId);

private:
  void OnTaskCompleted(threads::IRoutine * task);

  std::unique_ptr<threads::ThreadPool> m_threadsPool;

  std::mutex m_mutex;
  std::condition_variable m_event;

  uint64_t m_nextTasksGroupId = 0;
  std::map<uint64_t, size_t> m_tasksGroups;

  Index & m_index;
  TReadFeaturesFn m_readFeaturesFn;
  // TODO(@darina) In case of reading the whole mwm transit section, save it in the cache for transit scheme rendering.
  TransitDisplayInfos m_transitDisplayCache;

  df::DrapeEngineSafePtr m_drapeEngine;

  GetMwmsByRectFn m_getMwmsByRectFn;
  vector<MwmSet::MwmId> m_lastVisibleMwms;
  bool m_isSchemeMode = true;
  pair<ScreenBase, bool> m_currentModelView = {ScreenBase(), false /* initialized */};
};
