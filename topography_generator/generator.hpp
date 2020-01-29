#pragma once

#include "topography_generator/isolines_utils.hpp"
#include "topography_generator/tile_filter.hpp"

#include "geometry/rect2d.hpp"
#include "geometry/region2d.hpp"

#include <memory>
#include <string>

namespace topography_generator
{
struct TileIsolinesParams
{
  Altitude m_alitudesStep = 10;
  size_t m_latLonStepFactor = 1;
  int m_simplificationZoom = 17; // Value == 0 disables simplification.
  FiltersSequence<Altitude> m_filters;
  std::string m_outputDir;
};

struct CountryIsolinesParams
{
  size_t m_maxIsolineLength = 1000;
  int m_simplificationZoom = 17; // Value == 0 disables simplification.
  size_t m_alitudesStepFactor = 1;
  std::string m_isolinesTilesPath;
};

class Generator
{
public:
  Generator(std::string const & srtmPath, size_t threadsCount, size_t maxCachedTilesPerThread);

  void GenerateIsolines(int left, int bottom, int right, int top,
                        TileIsolinesParams const & params);

  void SetDataDir(std::string const & dataDir);

  void PackIsolinesForPlanet(CountryIsolinesParams const & params,
                             std::string const & outDir);

  void PackIsolinesForCountry(storage::CountryId const & countryId,
                              CountryIsolinesParams const & params,
                              std::string const & outDir);

private:
  void GetCountryRegions(storage::CountryId const & countryId, m2::RectD & countryRect,
                         std::vector<m2::RegionD> & countryRegions) const;

  size_t m_threadsCount;
  size_t m_maxCachedTilesPerThread;
  std::string m_srtmPath;
};
}  // namespace topography_generator
