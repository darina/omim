#include "map/transit/transit_reader.hpp"

#include "transit/transit_graph_data.hpp"

#include "indexer/drawing_rules.hpp"
#include "indexer/drules_include.hpp"
#include "indexer/feature_algo.hpp"

#include "coding/reader.hpp"

#include "drape_frontend/drape_engine.hpp"
#include "drape_frontend/stylist.hpp"
#include "drape_frontend/visual_params.hpp"

#include "base/stl_add.hpp"

using namespace routing;
using namespace std;

namespace
{
int constexpr kMinSchemeZoomLevel = 10;
}  // namespace

// ReadTransitTask --------------------------------------------------------------------------------
void ReadTransitTask::Init(uint64_t id, MwmSet::MwmId const & mwmId,
                           unique_ptr<TransitDisplayInfo> transitInfo)
{
  m_id = id;
  m_mwmId = mwmId;
  if (transitInfo == nullptr)
  {
    m_loadSubset = false;
    m_transitInfo = my::make_unique<TransitDisplayInfo>();
  }
  else
  {
    m_loadSubset = true;
    m_transitInfo = move(transitInfo);
  }
  m_success = false;
}

void ReadTransitTask::Do()
{
  MwmSet::MwmHandle handle = m_index.GetMwmHandleById(m_mwmId);
  if (!handle.IsAlive())
  {
    // It's possible that mwm handle is not alive because mwm may be removed after
    // transit route is built but before this task is executed.
    LOG(LWARNING, ("Can't get mwm handle for", m_mwmId));
    m_success = false;
    return;
  }
  MwmValue const & mwmValue = *handle.GetValue<MwmValue>();
  if (!m_loadSubset && !mwmValue.m_cont.IsExist(TRANSIT_FILE_TAG))
  {
    m_success = true;
    return;
  }
  CHECK(mwmValue.m_cont.IsExist(TRANSIT_FILE_TAG),
        ("No transit section in mwm, but transit route was built with it. mwmId:", m_mwmId));

  auto reader = mwmValue.m_cont.GetReader(TRANSIT_FILE_TAG);
  CHECK(reader.GetPtr() != nullptr, ());

  transit::GraphData graphData;
  graphData.DeserializeForRendering(*reader.GetPtr());

  FillItemsByIdMap(graphData.GetStops(), m_transitInfo->m_stops);
  for (auto const & stop : m_transitInfo->m_stops)
  {
    if (stop.second.GetFeatureId() != transit::kInvalidFeatureId)
    {
      auto const featureId = FeatureID(m_mwmId, stop.second.GetFeatureId());
      m_transitInfo->m_features[featureId] = {};
    }

    if (m_loadSubset && (stop.second.GetTransferId() != transit::kInvalidTransferId))
      m_transitInfo->m_transfers[stop.second.GetTransferId()] = {};
  }
  FillItemsByIdMap(graphData.GetTransfers(), m_transitInfo->m_transfers);
  FillItemsByIdMap(graphData.GetLines(), m_transitInfo->m_lines);
  FillItemsByIdMap(graphData.GetShapes(), m_transitInfo->m_shapes);

  vector<FeatureID> features;
  for (auto & id : m_transitInfo->m_features)
    features.push_back(id.first);

  m_readFeaturesFn([this](FeatureType const & ft)
  {
    auto & featureInfo = m_transitInfo->m_features[ft.GetID()];
    ft.GetReadableName(featureInfo.m_title);
    if (featureInfo.m_isGate)
    {
      df::Stylist stylist;
      if (df::InitStylist(ft, 0, 19, false, stylist))
      {
        stylist.ForEachRule([&](df::Stylist::TRuleWrapper const & rule)
        {
          auto const * symRule = rule.first->GetSymbol();
          if (symRule != nullptr)
            featureInfo.m_gateSymbolName = symRule->name();
        });
      }
    }
    featureInfo.m_point = feature::GetCenter(ft);
  }, features);
  m_success = true;
}

void ReadTransitTask::Reset()
{
  m_transitInfo.reset();
  m_id = 0;
  IRoutine::Reset();
}

unique_ptr<TransitDisplayInfo> && ReadTransitTask::GetTransitInfo()
{
  return move(m_transitInfo);
}

TransitReadManager::TransitReadManager(Index & index, TReadFeaturesFn const & readFeaturesFn,
                                       GetMwmsByRectFn const & getMwmsByRectFn)
  : m_index(index)
  , m_readFeaturesFn(readFeaturesFn)
  , m_getMwmsByRectFn(getMwmsByRectFn)
{
  Start();
}

TransitReadManager::~TransitReadManager()
{
  Stop();
}

void TransitReadManager::Start()
{
  if (m_threadsPool != nullptr)
    return;

  using namespace placeholders;
  uint8_t constexpr kThreadsCount = 1;
  m_threadsPool = my::make_unique<threads::ThreadPool>(
      kThreadsCount, bind(&TransitReadManager::OnTaskCompleted, this, _1));
}

void TransitReadManager::Stop()
{
  if (m_threadsPool != nullptr)
    m_threadsPool->Stop();
  m_threadsPool.reset();
}

void TransitReadManager::SetDrapeEngine(ref_ptr<df::DrapeEngine> engine)
{
  m_drapeEngine.Set(engine);
}

void TransitReadManager::UpdateViewport(ScreenBase const & screen)
{
  m_currentModelView = {screen, true /* initialized */};

  if (!m_isSchemeMode)
    return;

  if (df::GetZoomLevel(screen.GetScale()) < kMinSchemeZoomLevel)
    return;

  auto mwms = m_getMwmsByRectFn(screen.ClipRect());
  if (m_lastVisibleMwms == mwms)
    return;

  m_lastVisibleMwms = mwms;

  TransitDisplayInfos displayInfos;
  for (auto const & mwmId : mwms)
  {
    if (mwmId.IsAlive() && m_transitDisplayCache.find(mwmId) == m_transitDisplayCache.end())
    {
      displayInfos[mwmId] = {};
      m_transitDisplayCache[mwmId] = nullptr;
    }
  }
  GetTransitDisplayInfo(displayInfos);
  if (!displayInfos.empty())
  {
    m_drapeEngine.SafeCall(&df::DrapeEngine::UpdateTransitScheme,
                           std::move(displayInfos), mwms);
  }
}

bool TransitReadManager::GetTransitDisplayInfo(TransitDisplayInfos & transitDisplayInfos)
{
  unique_lock<mutex> lock(m_mutex);
  auto const groupId = ++m_nextTasksGroupId;
  lock.unlock();

  map<MwmSet::MwmId, unique_ptr<ReadTransitTask>> transitTasks;
  for (auto & mwmTransitPair : transitDisplayInfos)
  {
    auto const & mwmId = mwmTransitPair.first;
    auto task = my::make_unique<ReadTransitTask>(m_index, m_readFeaturesFn);
    task->Init(groupId, mwmId, move(mwmTransitPair.second));
    transitTasks[mwmId] = move(task);
  }

  lock.lock();
  m_tasksGroups[groupId] = transitTasks.size();
  lock.unlock();

  for (auto const & task : transitTasks)
    m_threadsPool->PushBack(task.second.get());

  lock.lock();
  m_event.wait(lock, [&]() { return m_tasksGroups[groupId] == 0; });
  m_tasksGroups.erase(groupId);
  lock.unlock();

  for (auto const & transitTask : transitTasks)
  {
    if (!transitTask.second->GetSuccess())
      return false;
  }

  for (auto const & transitTask : transitTasks)
    transitDisplayInfos[transitTask.first] = transitTask.second->GetTransitInfo();

  return true;
}

void TransitReadManager::OnTaskCompleted(threads::IRoutine * task)
{
  ASSERT(dynamic_cast<ReadTransitTask *>(task) != nullptr, ());
  ReadTransitTask * t = static_cast<ReadTransitTask *>(task);

  lock_guard<mutex> lock(m_mutex);

  if (--m_tasksGroups[t->GetId()] == 0)
    m_event.notify_all();
}
