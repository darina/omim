#include "tracks_processor.hpp"

#include "platform/platform.hpp"

#include "geometry/distance_on_sphere.hpp"
#include "geometry/mercator.hpp"

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

void TracksProcessor::GenerateTracksMesh(std::string const & countryId, std::string const & dataDir)
{
  //auto const tracksDir = base::JoinPath(outputDir, "tracks");
  auto const countryFileName = base::JoinPath(dataDir, "countries", countryId);
  string trackId;
  ifstream fin(countryFileName);
  if (!fin.good())
  {
    LOG(LWARNING, ("Couldn't load tracks from", countryFileName));
    return;
  }

  vector<string> tracks;
  while (getline(fin, trackId))
  {
    tracks.push_back(trackId);
  }

  for (auto const & track : tracks)
  {

  }
}
