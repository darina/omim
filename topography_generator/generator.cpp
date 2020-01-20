#include "topography_generator/generator.hpp"
#include "topography_generator/isolines_utils.hpp"
#include "topography_generator/marching_squares/marching_squares.hpp"
#include "topography_generator/utils/contours_serdes.hpp"

#include "platform/platform.hpp"

#include "generator/srtm_parser.hpp"

#include "geometry/mercator.hpp"

#include <vector>

namespace topography_generator
{
size_t constexpr kArcSecondsInDegree = 60 * 60;

class SrtmProvider : public ValuesProvider<geometry::Altitude>
{
public:
  explicit SrtmProvider(std::string const & srtmDir):
    m_srtmManager(srtmDir)
  {}

  geometry::Altitude GetValue(ms::LatLon const & pos) override
  {
    return m_srtmManager.GetHeight(pos);
  }

  geometry::Altitude GetInvalidValue() const override
  {
    return geometry::kInvalidAltitude;
  }

private:
  generator::SrtmTileManager m_srtmManager;
};

class RawSrtmTile : public ValuesProvider<geometry::Altitude>
{
public:
  RawSrtmTile(std::vector<geometry::Altitude> const & values,
              int leftLon, int bottomLat)
    : m_values(values)
    , m_leftLon(leftLon)
    , m_bottomLat(bottomLat)
  {}

  geometry::Altitude GetValue(ms::LatLon const & pos) override
  {
    // TODO: assert
    CHECK_EQUAL(floor(pos.m_lat), m_bottomLat, ());
    CHECK_EQUAL(floor(pos.m_lon), m_leftLon, ());

    double ln = pos.m_lon - m_leftLon;
    double lt = pos.m_lat - m_bottomLat;
    lt = 1 - lt;  // from North to South

    size_t const row = kArcSecondsInDegree * lt + 0.5;
    size_t const col = kArcSecondsInDegree * ln + 0.5;

    size_t const ix = row * (kArcSecondsInDegree + 1) + col;
    return ix < m_values.size() ? m_values[ix] : geometry::kInvalidAltitude;
  }

  geometry::Altitude GetInvalidValue() const override
  {
    return geometry::kInvalidAltitude;
  }

private:
  std::vector<geometry::Altitude> const & m_values;
  int m_leftLon;
  int m_bottomLat;
};

class IsolinesTask : public threads::IRoutine
{
public:
  IsolinesTask(int leftLon, int bottomLat, int rightLon, int topLat,
               std::string const & srtmDir, IsolinesParams const & params)
    : m_leftLon(leftLon)
    , m_bottomLat(bottomLat)
    , m_rightLon(rightLon)
    , m_topLat(topLat)
    , m_srtmProvider(srtmDir)
    , m_params(params)
  {}

  void Do() override
  {
    for (int lat = m_bottomLat; lat < m_topLat; ++lat)
    {
      for (int lon = m_leftLon; lon < m_rightLon; ++lon)
      {
        ProcessTile(lat, lon);
      }
    }
  }

private:
  void ProcessTile(int lat, int lon)
  {
    ms::LatLon const leftBottom = ms::LatLon(lat, lon);
    ms::LatLon const rightTop = ms::LatLon(lat + 1, lon + 1);
    double const squaresStep = 1.0 / (kArcSecondsInDegree) * m_params.m_latLonStepFactor;
    Contours<geometry::Altitude> contours;
    if (lat >= 60)
    {
      std::vector<geometry::Altitude> filteredValues = FilterTile(
        m_params.m_filters, leftBottom, kArcSecondsInDegree,
        kArcSecondsInDegree + 1, m_srtmProvider);
      RawSrtmTile filteredProvider(filteredValues, lon, lat);

      MarchingSquares<geometry::Altitude> squares(leftBottom, rightTop,
                                                  squaresStep, m_params.m_alitudesStep,
                                                  filteredProvider);
      squares.GenerateContours(contours);
    }
    else
    {
      MarchingSquares<geometry::Altitude> squares(leftBottom, rightTop,
                                                  squaresStep, m_params.m_alitudesStep,
                                                  m_srtmProvider);
      squares.GenerateContours(contours);
    }

    SaveContrours(GetIsolinesFilePath(lat, lon, m_params.m_outputDir), std::move(contours));
  }

  int m_leftLon;
  int m_bottomLat;
  int m_rightLon;
  int m_topLat;
  SrtmProvider m_srtmProvider;
  IsolinesParams const & m_params;
};

Generator::Generator(std::string const & srtmPath, size_t threadsCount, size_t maxCachedTilesPerThread)
  : m_maxCachedTilesPerThread(maxCachedTilesPerThread)
  , m_srtmPath(srtmPath)
{
  m_infoGetter = storage::CountryInfoReader::CreateCountryInfoReader(GetPlatform());
  CHECK(m_infoGetter, ());
  m_infoReader = dynamic_cast<storage::CountryInfoReader *>(m_infoGetter.get());

  m_threadsPool = std::make_unique<base::thread_pool::routine::ThreadPool>(
    threadsCount, std::bind(&Generator::OnTaskFinished, this, std::placeholders::_1));
}

Generator::~Generator()
{
  m_threadsPool->Stop();
}

void Generator::GenerateIsolines(IsolinesParams const & params)
{
  std::vector<std::unique_ptr<IsolinesTask>> tasks;

  CHECK_GREATER(params.m_rightLon, params.m_leftLon, ());
  CHECK_GREATER(params.m_topLat, params.m_bottomLat, ());

  //int const tilesPerRow0 = static_cast<int>(sqrt(m_maxCachedTilesPerThread) + 0.5);
  //int const tilesPerCol0 = static_cast<int>(m_maxCachedTilesPerThread / tilesPerRow0);

  int tilesPerRow = params.m_topLat - params.m_bottomLat;
  int tilesPerCol = params.m_rightLon - params.m_leftLon;
  while (tilesPerRow * tilesPerCol > m_maxCachedTilesPerThread)
  {
    if (tilesPerRow > tilesPerCol)
      tilesPerRow = (tilesPerRow + 1) / 2;
    else
      tilesPerCol = (tilesPerCol + 1) / 2;
  }

  for (int lat = params.m_bottomLat; lat < params.m_topLat; lat += tilesPerRow)
  {
    int const topLat = std::min(lat + tilesPerRow, params.m_topLat);
    for (int lon = params.m_leftLon; lon < params.m_rightLon; lon += tilesPerCol)
    {
      int const rightLon = std::min(lon + tilesPerCol, params.m_rightLon);
      tasks.emplace_back(new IsolinesTask(lon, lat, rightLon, topLat, m_srtmPath, params));
    }
  }

  {
    std::lock_guard<std::mutex> lock(m_tasksMutex);
    CHECK(m_activeTasksCount == 0, ());
    m_activeTasksCount = tasks.size();
  }

  for (auto & task : tasks)
    m_threadsPool->PushBack(task.get());

  std::unique_lock<std::mutex> lock(m_tasksMutex);
  m_tasksReadyCondition.wait(lock, [this] { return m_activeTasksCount == 0; });
}

void Generator::OnTaskFinished(threads::IRoutine * task)
{
  {
    std::lock_guard<std::mutex> lock(m_tasksMutex);
    CHECK(m_activeTasksCount > 0, ());
    --m_activeTasksCount;
    if (m_activeTasksCount == 0)
      m_tasksReadyCondition.notify_one();
  }
}

void Generator::GetCountryRegions(std::string const & countryId, m2::RectD & countryRect,
                                  std::vector<m2::RegionD> & countryRegions)
{
  countryRect = m_infoReader->GetLimitRectForLeaf(countryId);

  size_t id;
  for (id = 0; id < m_infoReader->GetCountries().size(); ++id)
  {
    if (m_infoReader->GetCountries().at(id).m_countryId == countryId)
      break;
  }
  CHECK_LESS(id, m_infoReader->GetCountries().size(), ());

  m_infoReader->LoadRegionsFromDisk(id, countryRegions);
}

void Generator::PackIsolinesForCountry(std::string const & countryId,
                                       std::string const & isolinesPath,
                                       std::string const & outDir)
{
  m2::RectD countryRect;
  std::vector<m2::RegionD> countryRegions;
  GetCountryRegions(countryId, countryRect, countryRegions);

  auto const leftBottom = mercator::ToLatLon(countryRect.LeftBottom());
  auto const rightTop = mercator::ToLatLon(countryRect.RightTop());

  auto const left = static_cast<int>(floor(leftBottom.m_lon));
  auto const bottom = static_cast<int>(floor(leftBottom.m_lat));
  auto const right = static_cast<int>(floor(rightTop.m_lon));
  auto const top = static_cast<int>(floor(rightTop.m_lat));

  int const kBaseSimplificationZoom = 17;
  size_t const kMaxIsolineLength = 1000;

  Contours<geometry::Altitude> countryIsolines;
  countryIsolines.m_minValue = std::numeric_limits<geometry::Altitude>::max();
  countryIsolines.m_maxValue = std::numeric_limits<geometry::Altitude>::min();

  for (int lat = bottom; lat <= top; ++lat)
  {
    for (int lon = left; lon <= right; ++lon)
    {
      Contours<geometry::Altitude> isolines;
      if (!LoadContours(GetIsolinesFilePath(lat, lon, isolinesPath), isolines))
        continue;

      SimplifyContours(kBaseSimplificationZoom, isolines);
      CropContours(countryRect, countryRegions, kMaxIsolineLength, isolines);

      countryIsolines.m_minValue = std::min(isolines.m_minValue, countryIsolines.m_minValue);
      countryIsolines.m_maxValue = std::max(isolines.m_maxValue, countryIsolines.m_maxValue);
      countryIsolines.m_valueStep = isolines.m_valueStep;
      for (auto & levelIsolines : isolines.m_contours)
      {
        auto & dst = countryIsolines.m_contours[levelIsolines.first];
        dst.insert(dst.end(), levelIsolines.second.begin(), levelIsolines.second.end());
      }
    }
  }

  SaveContrours(GetIsolinesFilePath(countryId, outDir), std::move(countryIsolines));
}
}  // namespace topography_generator
