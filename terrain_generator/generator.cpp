#include "generator.hpp"
#include "marching_squares.hpp"
#include "tracks_processor.hpp"

#include "platform/platform.hpp"

#include "indexer/feature_altitude.hpp"

#include "geometry/distance_on_sphere.hpp"
#include "geometry/mercator.hpp"

#include "base/file_name_utils.hpp"
#include "base/thread.hpp"

#include <functional>
#include <fstream>
#include <vector>

using namespace std;
using namespace std::placeholders;

size_t constexpr kThreadsCount = 6;
size_t constexpr kArcSecondsInDegree = 60 * 60;
int constexpr stepMult = 2;

class ReadTask : public threads::IRoutine
{
public:
  explicit ReadTask(generator::SrtmTileManager & srtmManager,
                    std::vector<m2::RegionD> const & regions)
    : m_srtmManager(srtmManager)
    , m_regions(regions)
  {
  }

  void Do() override
  {
    size_t lonStepsCount = static_cast<size_t>(fabs(m_rightTop.m_lon - m_leftBottom.m_lon) * (kArcSecondsInDegree / stepMult));
    size_t latStepsCount = static_cast<size_t>(fabs(m_rightTop.m_lat - m_leftBottom.m_lat) * (kArcSecondsInDegree / stepMult));

    LOG(LWARNING, ("Task ", m_id, ": left lon", m_leftBottom.m_lon, "right lon", m_rightTop.m_lon,
      "dist", fabs(m_leftBottom.m_lon - m_rightTop.m_lon), "lonStepsCount", lonStepsCount));
    LOG(LWARNING, ("Task ", m_id, ": bottom lat", m_leftBottom.m_lat, "top lat", m_rightTop.m_lat,
      "dist", fabs(m_leftBottom.m_lat - m_rightTop.m_lat), "latStepsCount", latStepsCount));

    double const step = stepMult * 1.0 / kArcSecondsInDegree;
    int const stepInMeters = stepMult * 30;

    m_data.reserve(latStepsCount * lonStepsCount * 3);

    ms::LatLon pos = m_leftBottom;
    for (size_t i = 0; i < latStepsCount; ++i)
    {
      pos.m_lon = m_leftBottom.m_lon;
      for (size_t j = 0; j < lonStepsCount; ++j)
      {
        auto const mPos = mercator::FromLatLon(pos);
        pos.m_lon += step;

        if (!RegionsContain(m_regions, mPos))
          continue;

        auto const height = m_srtmManager.GetHeight(pos);
        if (height == geometry::kInvalidAltitude)
          continue;

        m_minHeight = min(m_minHeight, height);
        m_maxHeight = max(m_maxHeight, height);

        double const x = ms::DistanceOnEarth(pos.m_lat, m_leftBottom.m_lon, pos.m_lat, pos.m_lon);
        double const y = ms::DistanceOnEarth(m_leftBottom.m_lat, pos.m_lon, pos.m_lat, pos.m_lon);

        m_data.push_back(static_cast<int>(m_leftOffset + x));
        m_data.push_back(static_cast<int>(m_bottomOffset + y));
        m_data.push_back(static_cast<int>(height));
      }
      pos.m_lat += step;
    }
  }

  void Init(ms::LatLon const & leftBottom, ms::LatLon const & rightTop,
            double leftOffsetInMeters, double bottomOffsetInMeters, int id)
  {
    m_id = id;
    m_leftBottom = leftBottom;
    m_rightTop = rightTop;
    m_maxHeight = numeric_limits<int16_t>::min();
    m_minHeight = numeric_limits<int16_t>::max();
    m_leftOffset = leftOffsetInMeters;
    m_bottomOffset = bottomOffsetInMeters;
    m_data.clear();
  }

  int const & GetId() const { return m_id; }
  int16_t GetMaxHeight() const { return m_maxHeight; }
  int16_t GetMinHeight() const { return m_minHeight; }
  vector<int> const & GetData() const { return m_data; }

private:
  generator::SrtmTileManager & m_srtmManager;
  std::vector<m2::RegionD> const & m_regions;
  ms::LatLon m_leftBottom;
  ms::LatLon m_rightTop;
  vector<int> m_data;
  int16_t m_maxHeight;
  int16_t m_minHeight;
  double m_leftOffset;
  double m_bottomOffset;
  int m_id;
};

TerrainGenerator::TerrainGenerator(std::string const & srtmDir, std::string const & outDir)
  : m_outDir(outDir)
  , m_srtmManager(srtmDir)
{
  m_infoGetter = storage::CountryInfoReader::CreateCountryInfoReader(GetPlatform());
  CHECK(m_infoGetter, ());
  m_infoReader = dynamic_cast<storage::CountryInfoReader *>(m_infoGetter.get());

  m_threadsPool = make_unique<base::thread_pool::routine::ThreadPool>(
    kThreadsCount, std::bind(&TerrainGenerator::OnTaskFinished, this, _1));
}

void TerrainGenerator::ParseTracks(std::string const & csvPath, std::string const & outDir)
{
  TracksProcessor processor(m_infoReader, &m_srtmManager);
  processor.ParseTracks(csvPath, outDir);
}

void TerrainGenerator::GenerateContours(std::vector<std::string> const & csvPaths, std::string const & countryId,
                                        std::string const & outDir)
{
  TracksProcessor processor(m_infoReader, &m_srtmManager);
  processor.ParseContours(countryId, csvPaths, outDir);
}

void TerrainGenerator::GenerateIsolines(std::string const & countryId,
                                        std::string const & outputDir)
{
  auto const leftBottom = ms::LatLon(45.7656, 6.7236);
  auto const rightTop = ms::LatLon(46.0199, 7.0444);
  auto const step = 1.0 / kArcSecondsInDegree;
  uint16_t const altitudeStep = 10;

  SRTMAltExtractor altExtractor(m_srtmManager);
  MarchingSquares marchingSquares(leftBottom, rightTop, step, altitudeStep, altExtractor);

  std::vector<IsolinesList> isolines;
  geometry::Altitude minAltitude;
  marchingSquares.GenerateIsolines(isolines, minAltitude);

  m2::RectD limitRect = m2::RectD(mercator::FromLatLon(leftBottom),
                                  mercator::FromLatLon(rightTop));
  m2::RectD countryRect;
  std::vector<m2::RegionD> regions;
  TracksProcessor::GetCountryRegions(countryId, m_infoReader,
                                     countryRect, regions);

  std::ofstream dstFile(base::JoinPath(outputDir, countryId + "_isolines2.txt"));

  auto saveIsoline = [&](geometry::Altitude altitude, std::vector<m2::PointD> & points)
  {
    dstFile << altitude << " " << points.size() << " ";

    for (size_t ptInd = 0; ptInd < points.size(); ++ptInd)
    {
      dstFile << points[ptInd].x << " " << points[ptInd].y
              << ((ptInd + 1 == points.size()) ? "" : " ");
    }

    dstFile << std::endl;
  };

  geometry::Altitude currentAltitude = minAltitude;
  for (auto const & isolineList : isolines)
  {
    for (auto const & isoline : isolineList)
    {
      std::vector<m2::PointD> points;
      points.reserve(isoline.size());

      for (auto const & ptLatLon : isoline)
      {
        auto const pt = mercator::FromLatLon(ptLatLon);
        if (limitRect.IsPointInside(pt) && RegionsContain(regions, pt))
        {
          points.push_back(pt);
        }
        else
        {
          if (points.size() >= 2)
            saveIsoline(static_cast<int>(currentAltitude), points);
          points.clear();
        }
      }
      if (points.size() >= 2)
        saveIsoline(static_cast<int>(currentAltitude), points);
    }
    currentAltitude += altitudeStep;
  }
}

void TerrainGenerator::OnTaskFinished(threads::IRoutine * task)
{
  ASSERT(dynamic_cast<ReadTask *>(task) != NULL, ());
  auto t = static_cast<ReadTask *>(task);

  LOG(LINFO, ("On task finished", t->GetId(),
    "maxHeight", t->GetMaxHeight(), "minHeight", t->GetMinHeight(),
    "vert count", t->GetData().size() / 3));

  // finish tiles
  {
    std::lock_guard<std::mutex> lock(m_tasksMutex);
    CHECK(m_activeTasksCount > 0, ());
    --m_activeTasksCount;
    if (m_activeTasksCount == 0)
      m_tasksReadyCondition.notify_one();
  }
}

void TerrainGenerator::Generate(string const & countryId)
{
  m2::RectD const limitRect = m_infoGetter->GetLimitRectForLeaf(countryId);
  LOG(LINFO, ("Limit rect", limitRect));

  if (limitRect.IsEmptyInterior())
  {
    LOG(LWARNING, ("Invalid mwm rect for", countryId));
    return;
  }

  size_t id;
  for (id = 0; id < m_infoReader->GetCountries().size(); ++id)
  {
    if (m_infoReader->GetCountries().at(id).m_countryId == countryId)
      break;
  }

  std::vector<m2::RegionD> regions;
  m_infoReader->LoadRegionsFromDisk(id, regions);

  ms::LatLon leftBottom = mercator::ToLatLon(limitRect.LeftBottom());
  ms::LatLon rightTop = mercator::ToLatLon(limitRect.RightTop());

  auto const factor = TracksProcessor::CalculateCoordinatesFactor(limitRect);

  auto const heightInMeters = ms::DistanceOnEarth(leftBottom.m_lat, leftBottom.m_lon,
                                                  rightTop.m_lat, leftBottom.m_lon);
  auto const widthInMetersBottom = ms::DistanceOnEarth(leftBottom.m_lat, leftBottom.m_lon,
                                                       leftBottom.m_lat, rightTop.m_lon);
  auto const widthInMetersTop = ms::DistanceOnEarth(rightTop.m_lat, leftBottom.m_lon,
                                                    rightTop.m_lat, rightTop.m_lon);

  int16_t const stepInMeters = 30 * stepMult;
  LOG(LINFO, ("heightInMeters", heightInMeters, (int)(heightInMeters / stepInMeters),
    "widthInMetersBottom", widthInMetersBottom, (int)(widthInMetersBottom / stepInMeters),
    "widthInMetersTop", widthInMetersTop, (int)(widthInMetersTop / stepInMeters)));

  LOG(LINFO, ("left lon", leftBottom.m_lon, "right lon", rightTop.m_lon, "dist", fabs(leftBottom.m_lon - rightTop.m_lon)));
  LOG(LINFO, ("bottom lat", leftBottom.m_lat, "top lat", rightTop.m_lat, "dist", fabs(leftBottom.m_lat - rightTop.m_lat)));

  double latStep = (rightTop.m_lat - leftBottom.m_lat) / kThreadsCount;

  ms::LatLon currentLeftBottom = leftBottom;
  ms::LatLon currentRightTop = ms::LatLon(leftBottom.m_lat + latStep, rightTop.m_lon);
  double bottomOffset = 0.0;
  vector<unique_ptr<ReadTask>> tasks;
  for (int i = 0; i < kThreadsCount; ++i)
  {
    if (i == kThreadsCount - 1)
      currentRightTop.m_lat = rightTop.m_lat;

    tasks.emplace_back(new ReadTask(m_srtmManager, regions));
    tasks.back()->Init(currentLeftBottom, currentRightTop, 0.0 /* leftOffset */, bottomOffset, i);

    currentLeftBottom.m_lat += latStep;
    currentRightTop.m_lat += latStep;
    bottomOffset = ms::DistanceOnEarth(leftBottom, currentLeftBottom);
    LOG(LINFO, ("Task", i, "bottomOffset", bottomOffset));

    // Load srtm tiles before multithreading reading.
    m_srtmManager.GetHeight(currentLeftBottom);
    m_srtmManager.GetHeight(currentRightTop);
  }

  {
    std::lock_guard<std::mutex> lock(m_tasksMutex);
    CHECK(m_activeTasksCount == 0, ());
    m_activeTasksCount = tasks.size();
  }

  for (int i = 0; i < tasks.size(); ++i)
    m_threadsPool->PushBack(tasks[i].get());

  std::unique_lock<std::mutex> lock(m_tasksMutex);
  m_tasksReadyCondition.wait(lock, [this] { return m_activeTasksCount == 0; });

  size_t verticesCount = 0;
  int16_t maxHeight = numeric_limits<int16_t>::min();
  int16_t minHeight = numeric_limits<int16_t>::max();

  for (size_t i = 0; i < tasks.size(); ++i)
  {
    verticesCount += tasks[i]->GetData().size() / 3;
    maxHeight = max(maxHeight, tasks[i]->GetMaxHeight());
    minHeight = min(minHeight, tasks[i]->GetMinHeight());
  }

  auto width = static_cast<size_t>(fabs(rightTop.m_lon - leftBottom.m_lon) * (kArcSecondsInDegree / stepMult));
  auto height = static_cast<size_t>(fabs(rightTop.m_lat - leftBottom.m_lat) * (kArcSecondsInDegree / stepMult));
  {
    ofstream fout(base::JoinPath(m_outDir, countryId + "_terrain_info.txt"), ofstream::out);
    fout << "Lat bottom " << leftBottom.m_lat << " top " << rightTop.m_lat << endl;
    fout << "Lon left " << leftBottom.m_lon << " right " << rightTop.m_lon << endl;
    fout << "Width " << width << " vertices, " << width * stepInMeters << " m" << endl;
    fout << "Height " << height << " vertices, " << height * stepInMeters << " m" << endl;
    fout << "Min height " << minHeight << " m" << endl;
    fout << "Max height " << maxHeight << " m" << endl;
    fout << "Stored vertices count " << verticesCount << " (width * height = " << width * height << ")" << endl;
  }

  {
    ofstream fout(base::JoinPath(m_outDir, countryId + "_terrain.txt"), ofstream::out);
    fout << fixed << setprecision(5);
    for (size_t i = 0; i < tasks.size(); ++i)
    {
      auto const & data = tasks[i]->GetData();
      for (size_t j = 0; j + 2 < data.size(); j += 3)
      {
        fout << data[j] * factor << " "
             << data[j + 1] * factor << " " << data[j + 2] * factor << endl;
      }
    }
  }

  {
    ofstream fout(base::JoinPath(m_outDir, countryId + "_border.txt"), ofstream::out);
    fout << fixed << setprecision(5);
    for (size_t i = 0; i < regions.size(); ++i)
    {
      vector<m2::PointD> const & points = regions[i].Data();
      for (size_t j = 0; j < points.size(); ++j)
      {
        double const x = mercator::DistanceOnEarth(points[j], m2::PointD(limitRect.LeftBottom().x, points[j].y));
        double const y = mercator::DistanceOnEarth(points[j], m2::PointD(points[j].x, limitRect.LeftBottom().y));
        fout << x * factor << " " << y * factor << endl;
      }

      if (regions.size() > 1)
      {
        LOG(LWARNING, ("Several borders are not supported! Current count", regions.size()));
        break;
      }
    }
  }
}

void TerrainGenerator::GenerateTracksMesh(std::string const & dataDir, std::string const & countryId)
{
  TracksProcessor processor(m_infoReader, &m_srtmManager);
  processor.GenerateTracksMesh(countryId, dataDir, m_outDir);
}
