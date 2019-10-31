#pragma once

#include "generator/srtm_parser.hpp"

#include "storage/country_info_getter.hpp"

#include "base/thread_pool.hpp"

#include <condition_variable>
#include <string>
#include <memory>
#include <mutex>

class TerrainGenerator
{
public:
  TerrainGenerator(std::string const & srtmDir, std::string const & outDir);
  void Generate(std::string const & countryId);
  void OnTaskFinished(threads::IRoutine * task);

  void ParseTracks(std::string const & csvPath, std::string const & outDir);
  void GenerateTracksMesh(std::string const & dataDir, std::string const & countryId);

private:
  std::string m_outDir;
  generator::SrtmTileManager m_srtmManager;
  std::unique_ptr<storage::CountryInfoGetter> m_infoGetter;
  storage::CountryInfoReader * m_infoReader = nullptr;
  std::unique_ptr<base::thread_pool::routine::ThreadPool> m_threadsPool;
  std::mutex m_tasksMutex;
  std::condition_variable m_tasksReadyCondition;
  size_t m_activeTasksCount = 0;
};
