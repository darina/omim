#include "tracks_processor.hpp"

#include "platform/platform.hpp"

#include "generator/feature_helpers.hpp"

#include "geometry/distance_on_sphere.hpp"
#include "geometry/mercator.hpp"
#include "geometry/point3d.hpp"

#include "coding/geometry_coding.hpp"

#include "base/logging.hpp"
#include "base/file_name_utils.hpp"
#include "base/string_utils.hpp"

#include <fstream>
#include <istream>

using namespace std;

string GetTrackFileName(size_t trackId)
{
  stringstream sstr;
  sstr << setfill('0') << setw(7) << trackId;
  return sstr.str();
}

bool PrepareDir(std::string const & dirPath)
{
  if (!Platform::IsDirectory(dirPath) && !Platform::MkDirChecked(dirPath))
  {
    LOG(LWARNING, ("Directory creation failed:", dirPath));
    return false;
  }
  return true;
}

bool ParsePoint(std::string const & s, double & lat, double & lon, double & height)
{
  // Order in string is: lat, lon, z.
  strings::SimpleTokenizer iter(s, ',');
  if (!iter)
    return false;

  if (strings::to_double(*iter, lat) && mercator::ValidLat(lat) && ++iter)
  {
    if (strings::to_double(*iter, lon) && mercator::ValidLon(lon) && ++iter)
    {
      if (strings::to_double(*iter, height))
        return true;
    }
  }
  return false;
}

bool ParsePointFromContour(std::string const & s, m2::PointD & pt)
{
  // Order in string is: lon, lat
  strings::SimpleTokenizer iter(s, ' ');
  if (!iter)
    return false;

  double lat;
  double lon;
  if (strings::to_double(*iter, lon) && mercator::ValidLon(lon) && ++iter)
  {
    if (strings::to_double(*iter, lat) && mercator::ValidLat(lat))
    {
      pt = mercator::FromLatLon(lat, lon);
      return true;
    }
  }
  return false;
}

void DumpCountries(string const & countriesDir, map<string, vector<size_t>> const & mwmTracks)
{
  std::multimap<size_t, string> countrySum;

  for (auto const & t : mwmTracks)
  {
    countrySum.insert(make_pair(t.second.size(), t.first));
    LOG(LINFO, ("country", t.first, "tracks count", t.second.size()));
    ofstream fout(base::JoinPath(countriesDir, t.first + ".txt"));
    for (auto const trackId : t.second)
      fout << GetTrackFileName(trackId) << endl;
    fout.close();
  }

  {
    ofstream fout(base::JoinPath(countriesDir, "#summary.txt"));
    for (auto const & c : countrySum)
      fout << setw(5) << c.first << " " << c.second << endl;
    fout.close();
  }
}

double TracksProcessor::CalculateCoordinatesFactor(m2::RectD const & limitRect)
{
  ms::LatLon leftBottom = mercator::ToLatLon(limitRect.LeftBottom());
  ms::LatLon rightTop = mercator::ToLatLon(limitRect.RightTop());

  auto const heightInMeters = ms::DistanceOnEarth(leftBottom.m_lat, leftBottom.m_lon,
                                                  rightTop.m_lat, leftBottom.m_lon);
  auto const widthInMetersBottom = ms::DistanceOnEarth(leftBottom.m_lat, leftBottom.m_lon,
                                                       leftBottom.m_lat, rightTop.m_lon);
  auto const widthInMetersTop = ms::DistanceOnEarth(rightTop.m_lat, leftBottom.m_lon,
                                                    rightTop.m_lat, rightTop.m_lon);

  auto const maxAbs = max(heightInMeters, max(widthInMetersTop, widthInMetersBottom));
  auto const factor = 1000.0 / maxAbs;
  return factor;
}

void TracksProcessor::GetCountryRegions(std::string const & countryId,
                                        storage::CountryInfoReader * infoReader,
                                        m2::RectD & limitRect,
                                        std::vector<m2::RegionD> & regions)
{
  limitRect = infoReader->GetLimitRectForLeaf(countryId);
  LOG(LINFO, ("Limit rect", limitRect));

  if (limitRect.IsEmptyInterior())
  {
    LOG(LWARNING, ("Invalid mwm rect for", countryId));
    return;
  }

  size_t id;
  for (id = 0; id < infoReader->GetCountries().size(); ++id)
  {
    if (infoReader->GetCountries().at(id).m_countryId == countryId)
      break;
  }
  CHECK_LESS(id, infoReader->GetCountries().size(), ());

  infoReader->LoadRegionsFromDisk(id, regions);
}

void TracksProcessor::ParseContours(std::string const & countryId,
                                    std::vector<std::string> const & csvFilePaths,
                                    std::string const & outputDir)
{
  std::ofstream dstFile(base::JoinPath(outputDir, countryId + "_isolines.txt"));

  m2::RectD limitRect = m2::RectD(mercator::FromLatLon(45.7656, 6.7236),
                                  mercator::FromLatLon(46.0199, 7.0444));

  m2::RectD countryRect;
  std::vector<m2::RegionD> regions;
  GetCountryRegions(countryId, m_infoReader, countryRect, regions);


  auto saveContour = [&](int height, std::vector<m2::PointD> & points)
  {
    int kBaseSimplificationZoom = 17;

    std::vector<m2::PointD> & pointsSimple = points;
    /*feature::SimplifyPoints(m2::SquaredDistanceFromSegmentToPoint<m2::PointD>(),
                            kBaseSimplificationZoom, points, pointsSimple);
*/
    if (pointsSimple.size() < 2)
    {
      LOG(LWARNING, ("Empty simplified contour!"));
      return;
    }

    dstFile << height << " " << pointsSimple.size() << " ";

    for (size_t ptInd = 0; ptInd < pointsSimple.size(); ++ptInd)
    {
      dstFile << pointsSimple[ptInd].x << " " << pointsSimple[ptInd].y
        << ((ptInd + 1 == pointsSimple.size()) ? "" : " ");
    }

    dstFile << std::endl;
  };

  for (auto csvFilePath : csvFilePaths)
  {
    ifstream fin(csvFilePath);
    string line;
    // skip field names line
    getline(fin, line);

    while (getline(fin, line))
    {
      auto const startPos = line.find('(');
      auto const endPos = line.find(')');
      auto const coordinates = line.substr(startPos + 1, endPos - startPos - 1);

      double height;
      CHECK(strings::to_double(line.substr(line.rfind(',') + 1), height), ());

      strings::SimpleTokenizer iter(coordinates, ',');
      m2::PointD pt;
      std::vector<m2::PointD> points;
      while (iter)
      {
        if (ParsePointFromContour(*iter, pt))
        {
          if (limitRect.IsPointInside(pt) && RegionsContain(regions, pt))
          {
            points.push_back(pt);
          }
          else
          {
            if (points.size() > 2)
              saveContour(static_cast<int>(height), points);
            points.clear();
          }
        }
        ++iter;
      }
      if (points.size() > 2)
        saveContour(static_cast<int>(height), points);
    }
  }
}
/*
void TracksProcessor::ParseContours(std::string const & countryId,
                                    std::vector<std::string> const & csvFilePaths,
                                    std::string const & outputDir)
{
  std::vector<int> zoomLevels = {10, 12, 14, 17};
  std::vector<unique_ptr<FileWriter>> writers;
  std::vector<std::ofstream> files;
  std::vector<size_t> totalPointsCount(zoomLevels.size(), 0);
  std::vector<size_t> totalRejectedPointsCount(zoomLevels.size(), 0);

  std::vector<size_t> testPointsCount(zoomLevels.size(), 0);

  m2::RectD testWindow = m2::RectD(mercator::FromLatLon(45.9122269, 6.9125985),
    mercator::FromLatLon(45.9593981, 6.9966812));
  double testArea = mercator::AreaOnEarth(testWindow);
  double testAreaMerc = testWindow.SizeX() * testWindow.SizeY();

  for (size_t i = 0; i < zoomLevels.size(); ++i)
  {
    auto const postfix = strings::to_string(zoomLevels[i]);
    auto const basePath = base::JoinPath(outputDir, countryId + "_contour_" + postfix);
    writers.emplace_back(make_unique<FileWriter>(basePath + ".dat"));
    files.emplace_back(basePath + ".txt");
    files[i] << fixed << setprecision(6);
  }

  m2::RectD limitRect;
  std::vector<m2::RegionD> regions;
  GetCountryRegions(countryId, m_infoReader, limitRect, regions);

  serial::GeometryCodingParams codingParams(kFeatureSorterPointCoordBits, 0);

  auto saveContour = [&](int height, std::vector<m2::PointD> & points)
  {
    int zoomInd = 0;
    if (height % 200 == 0)
      zoomInd = 0;
    else if (height % 100 == 0)
      zoomInd = 1;
    else if (height % 50 == 0)
      zoomInd = 2;
    else if (height % 10 == 0)
      zoomInd = 3;
    else
    {
      LOG(LWARNING, ("Unknown height", height));
      return;
    }

    for (size_t i = zoomInd; i < zoomLevels.size(); ++i)
    {
      std::vector<m2::PointD> pointsSimple;
      feature::SimplifyPoints(m2::SquaredDistanceFromSegmentToPoint<m2::PointD>(),
                              zoomLevels[i], points, pointsSimple);

      if (pointsSimple.size() < 2)
      {
        LOG(LWARNING, ("Empty simplified contour!"));
        continue;
      }

      totalPointsCount[i] += pointsSimple.size();
      totalRejectedPointsCount[i] += points.size() - pointsSimple.size();

      codingParams.SetBasePoint(pointsSimple[0]);
      std::vector<m2::PointD> toSave(pointsSimple.begin() + 1, pointsSimple.end());
      serial::SaveInnerPath(toSave, codingParams, *writers[i].get());

      for (size_t ptInd = 0; ptInd < pointsSimple.size(); ++ptInd)
      {
        if (testWindow.IsPointInside(pointsSimple[ptInd]))
          ++testPointsCount[i];

        ms::LatLon latPt = mercator::ToLatLon(pointsSimple[ptInd]);
        files[i] << latPt.m_lat << " " << latPt.m_lon <<
                 ((ptInd + 1 == pointsSimple.size()) ? "" : ", ");
      }
      files[i] << std::endl;

      LOG(LINFO, ("---", zoomLevels[i], "Current points count", totalPointsCount[i],
        "rejected points count", totalRejectedPointsCount[i]));
    }
  };

  for (auto csvFilePath : csvFilePaths)
  {
    ifstream fin(csvFilePath);
    string line;
    // skip field names line
    getline(fin, line);

    while (getline(fin, line))
    {
      auto const startPos = line.find('(');
      auto const endPos = line.find(')');
      auto const coordinates = line.substr(startPos + 1, endPos - startPos - 1);

      double height;
      CHECK(strings::to_double(line.substr(line.rfind(',') + 1), height), ());

      strings::SimpleTokenizer iter(coordinates, ',');
      m2::PointD pt;
      std::vector<m2::PointD> points;
      while (iter)
      {
        if (ParsePointFromContour(*iter, pt))
        {
          if (limitRect.IsPointInside(pt) && RegionsContain(regions, pt))
          {
            points.push_back(pt);
          }
          else
          {
            if (points.size() > 2)
              saveContour(static_cast<int>(height), points);
            points.clear();
          }
        }
        ++iter;
      }
      if (points.size() > 2)
        saveContour(static_cast<int>(height), points);
    }
  }

  LOG(LWARNING, ("=== Test window area in m2", testArea, ", in merc", testAreaMerc,
    ", test window", testWindow));
  for (size_t i = 0; i < zoomLevels.size(); ++i)
  {
    LOG(LWARNING, ("---", zoomLevels[i], "Total points count", totalPointsCount[i],
                         "rejected points count", totalRejectedPointsCount[i]));
    LOG(LWARNING, ("---", zoomLevels[i], "Points factor", testPointsCount[i] / testArea,
      "points factor merc", testPointsCount[i] / testAreaMerc,
      "test points count", testPointsCount[i]));
  }
}
*/
void TracksProcessor::ParseTracks(string const & csvFilePath, string const & outputDir)
{
  auto const tracksDir = base::JoinPath(outputDir, "tracks");
  auto const countriesDir = base::JoinPath(outputDir, "countries");

  if (!PrepareDir(outputDir) || !PrepareDir(tracksDir) || !PrepareDir(countriesDir))
    return;

  size_t trackId = 0;
  map<string, vector<size_t>> mwmTracks;

  ifstream fin(csvFilePath);
  string line;
  while (getline(fin, line))
  {
    if (trackId % 1500 == 0)
      DumpCountries(countriesDir, mwmTracks);

    strings::SimpleTokenizer iter(line, ' ');
    if (!iter)
      continue;

    double lat, lon, height;
    storage::CountryId prevCountryId;
    ms::LatLon prevPt = ms::LatLon::Zero();
    size_t countriesCount = 0;

    ofstream fout(base::JoinPath(tracksDir, GetTrackFileName(trackId) + ".txt"));
    size_t pointsCount = 0;
    while (iter)
    {
      if (ParsePoint(*iter, lat, lon, height))
      {
        fout << lat << " " << lon << " " << height << endl;
        if (ms::DistanceOnEarth(prevPt.m_lat, prevPt.m_lon, lat, lon) > 200)
        {
          auto pt = mercator::FromLatLon(lat, lon);
          auto countryId = m_infoReader->GetRegionCountryId(pt);
          if (countryId.empty())
            countryId = "#unknown";

          if (prevCountryId != countryId)
          {
            if (mwmTracks[countryId].empty() || mwmTracks[countryId].back() != trackId)
            {
              ++countriesCount;
              //LOG(LINFO, ("track", trackId, "belongs to", countryId, "countries count", countriesCount));
              mwmTracks[countryId].push_back(trackId);
            }
            prevCountryId = countryId;
            prevPt.m_lat = lat;
            prevPt.m_lon = lon;
          }
        }
        ++pointsCount;
      }
      ++iter;
    }
    fout.close();

    if (pointsCount < 2)
      LOG(LWARNING, ("track", trackId, "has invalid points count:", pointsCount));

    ++trackId;
  }

  DumpCountries(countriesDir, mwmTracks);
}

void TracksProcessor::GenerateTracksMesh(std::string const & countryId, std::string const & dataDir,
                                         std::string const & outDir)
{
  vector<string> tracks;
  //auto const tracksDir = base::JoinPath(outputDir, "tracks");
  {
    auto const countryFileName = base::JoinPath(dataDir, "countries", countryId + ".txt");
    string trackId;
    ifstream fin(countryFileName);
    if (!fin.good())
    {
      LOG(LWARNING, ("Couldn't load tracks from", countryFileName));
      return;
    }

    while (getline(fin, trackId))
    {
      if (!trackId.empty())
        tracks.push_back(trackId);
    }
  }

  m2::RectD const limitRect = m_infoReader->GetLimitRectForLeaf(countryId);
  auto const factor = CalculateCoordinatesFactor(limitRect);
  auto const trackHWidthInMeters = 15 * factor;

  ms::LatLon const leftBottom = mercator::ToLatLon(limitRect.LeftBottom());

  vector<m3::PointD> vertices;
  vector<size_t> indices;

  for (auto const & track : tracks)
  {
    auto const trackFileName = base::JoinPath(dataDir, "tracks", track + ".txt");
    ifstream fin(trackFileName);
    if (!fin.good())
    {
      LOG(LWARNING, ("Can't open track file", trackFileName));
      continue;
    }

    vector<m3::PointD> points;
    double lat, lon, height;
    while (fin >> lat >> lon >> height)
    {
      auto const mPos = mercator::FromLatLon(lat, lon);
      if (!limitRect.IsPointInside(mPos))
        continue;
      auto const x = ms::DistanceOnEarth(leftBottom.m_lat, leftBottom.m_lon, leftBottom.m_lat, lon);
      auto const y = ms::DistanceOnEarth(leftBottom.m_lat, leftBottom.m_lon, lat, leftBottom.m_lon);
      auto pt = m3::PointD(x * factor, y * factor, height * factor);
      if (!points.empty() && ((points.back() - pt).Length() < 200 * factor))
        continue;
      auto const heightSRTM = m_srtmManager->GetHeight(ms::LatLon(lat, lon));
      if (heightSRTM != geometry::kInvalidAltitude && heightSRTM > height)
        height = heightSRTM;
      height += 50;
      points.emplace_back(x * factor, y * factor, height * factor);
    }

    CHECK_GREATER_OR_EQUAL(points.size(), 2, ());

    vertices.reserve(vertices.size() + (points.size() - 1) * 4);
    indices.reserve(indices.size() + (points.size() - 1) * 6);
    m3::PointD prevN;
    size_t prevInd2;
    size_t prevInd3;
    for (size_t i = 0; i + 1 < points.size(); ++i)
    {
      auto const & p1 = points[i];
      auto const & p2 = points[i + 1];
      auto const v = p2 - p1;
      auto const n = m3::PointD(-v.y, v.x, 0.0).Normalize();


      //if (v.z > 0)
      //  n = m3::CrossProduct(v, p).Normalize();
      //else
      //  n = m3::CrossProduct(p, v).Normalize();

      auto const ind = vertices.size();
      indices.push_back(ind);
      indices.push_back(ind + 1);
      indices.push_back(ind + 2);
      indices.push_back(ind);
      indices.push_back(ind + 2);
      indices.push_back(ind + 3);


      if (i > 0)
      {
        if (m2::CrossProduct(m2::PointD(n.x, n.y), m2::PointD(prevN.x, prevN.y)) < 0)
        {
          indices.push_back(prevInd3);
          indices.push_back(ind + 4);
          indices.push_back(ind);
        }
        else
        {
          indices.push_back(ind + 4);
          indices.push_back(prevInd2);
          indices.push_back(ind + 1);
        }
      }

      vertices.push_back(p1 - (n * trackHWidthInMeters));
      vertices.push_back(p1 + (n * trackHWidthInMeters));
      vertices.push_back(p2 + (n * trackHWidthInMeters));
      vertices.push_back(p2 - (n * trackHWidthInMeters));
      vertices.push_back(p1);

      prevInd2 = ind + 2;
      prevInd3 = ind + 3;
      prevN = n;
    }
  }

  {
    ofstream fout(base::JoinPath(outDir, countryId + "_tracks_vertices.txt"));
    fout << fixed << setprecision(5);
    for (auto const & vert : vertices)
      fout << vert.x << " " << vert.y << " " << vert.z << endl;
  }

  {
    ofstream fout(base::JoinPath(outDir, countryId + "_tracks_indices.txt"));
    for (auto ind : indices)
      fout << ind << " ";
  }
}
