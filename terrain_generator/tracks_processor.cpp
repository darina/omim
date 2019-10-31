#include "tracks_processor.hpp"

#include "platform/platform.hpp"

#include "geometry/distance_on_sphere.hpp"
#include "geometry/mercator.hpp"
#include "geometry/point3d.hpp"

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
  // Order in string is: lon, lat, z.
  strings::SimpleTokenizer iter(s, ',');
  if (!iter)
    return false;

  if (strings::to_double(*iter, lat) && MercatorBounds::ValidLat(lat) && ++iter)
  {
    if (strings::to_double(*iter, lon) && MercatorBounds::ValidLon(lon) && ++iter)
    {
      if (strings::to_double(*iter, height))
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
  ms::LatLon leftBottom = MercatorBounds::ToLatLon(limitRect.LeftBottom());
  ms::LatLon rightTop = MercatorBounds::ToLatLon(limitRect.RightTop());

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
          auto pt = MercatorBounds::FromLatLon(lat, lon);
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

  ms::LatLon const leftBottom = MercatorBounds::ToLatLon(limitRect.LeftBottom());

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
      auto const mPos = MercatorBounds::FromLatLon(lat, lon);
      if (!limitRect.IsPointInside(mPos))
        continue;
      auto const x = ms::DistanceOnEarth(leftBottom.m_lat, leftBottom.m_lon, leftBottom.m_lat, lon);
      auto const y = ms::DistanceOnEarth(leftBottom.m_lat, leftBottom.m_lon, lat, leftBottom.m_lon);
      auto pt = m3::PointD(x * factor, y * factor, height * factor);
      if (!points.empty() && ((points.back() - pt).Length() < 200 * factor))
        continue;
      auto const heightSRTM = m_srtmManager->GetHeight(ms::LatLon(lat, lon));
      if (heightSRTM != feature::kInvalidAltitude && heightSRTM > height)
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
