#pragma once

#include "storage/country_info_getter.hpp"

#include "geometry/polyline2d.hpp"

class TracksProcessor
{
public:
  TracksProcessor(storage::CountryInfoReader * infoReader)
    : m_infoReader(infoReader)
  {
    CHECK(m_infoReader != nullptr, ());
  }

  void ParseTracks(std::string const & csvFilePath, std::string const & outputDir);
  void GenerateTracksMesh(std::string const & countryId, std::string const & dataDir);

private:
  storage::CountryInfoReader * m_infoReader = nullptr;
};
