#pragma once

#include "generator/srtm_parser.hpp"

#include "storage/country_info_getter.hpp"

#include "geometry/polyline2d.hpp"
#include "geometry/region2d.hpp"

class TracksProcessor
{
public:
  TracksProcessor(storage::CountryInfoReader * infoReader,
                  generator::SrtmTileManager * srtmManager)
    : m_infoReader(infoReader)
    , m_srtmManager(srtmManager)
  {
    CHECK(m_infoReader != nullptr, ());
    CHECK(m_srtmManager != nullptr, ());
  }

  static double CalculateCoordinatesFactor(m2::RectD const & limitRect);
  static void GetCountryRegions(std::string const & countryId,
                                storage::CountryInfoReader * m_infoReader,
                                m2::RectD & limitRect,
                                std::vector<m2::RegionD> & regions);

  void ParseContours(std::string const & countryId, std::vector<std::string> const & csvFilePaths,
                     std::string const & outputDir);
  void ParseTracks(std::string const & csvFilePath, std::string const & outputDir);
  void GenerateTracksMesh(std::string const & countryId, std::string const & dataDir,
                          std::string const & outDir);

private:
  storage::CountryInfoReader * m_infoReader = nullptr;
  generator::SrtmTileManager * m_srtmManager = nullptr;
};
