#pragma once

#include "topography_generator/tile_filter.hpp"

#include "storage/country_info_getter.hpp"

#include "geometry/point_with_altitude.hpp"
#include "geometry/rect2d.hpp"
#include "geometry/region2d.hpp"

#include "base/thread_pool.hpp"

#include <condition_variable>
#include <string>
#include <memory>
#include <mutex>

namespace topography_generator
{
struct IsolinesParams
{
  int m_leftLon;
  int m_bottomLat;
  int m_rightLon;
  int m_topLat;
  geometry::Altitude m_alitudesStep = 10;
  size_t m_latLonStepFactor = 1;
  FiltersSequence<geometry::Altitude> m_filters;
  std::string m_outputDir;
};

class Generator
{
public:
  Generator(std::string const & srtmPath, size_t threadsCount, size_t maxCachedTilesPerThread);
  ~Generator();

  void GenerateIsolines(IsolinesParams const & params);
  void PackIsolinesForCountry(std::string const & countryId, std::string const & isolinesPath,
                              std::string const & outDir);

private:
  void OnTaskFinished(threads::IRoutine * task);
  void GetCountryRegions(std::string const & countryId, m2::RectD & countryRect,
                         std::vector<m2::RegionD> & countryRegions);

  std::unique_ptr<storage::CountryInfoGetter> m_infoGetter;
  storage::CountryInfoReader * m_infoReader = nullptr;

  std::unique_ptr<base::thread_pool::routine::ThreadPool> m_threadsPool;
  size_t m_maxCachedTilesPerThread;
  std::string m_srtmPath;
  std::mutex m_tasksMutex;
  std::condition_variable m_tasksReadyCondition;
  size_t m_activeTasksCount = 0;
};
}  // namespace topography_generator
