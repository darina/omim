#pragma once

#include "generator/srtm_parser.hpp"

namespace generator
{
class AltitudeExtractor
{
public:
  virtual geometry::Altitude GetAltitude(ms::LatLon const & pos) = 0;
};

class FilteredTile
{
public:
  FilteredTile(AltitudeExtractor & altitudeExtractor, size_t stepsInDegree,
               std::vector<double> const & kernel)
    : m_altitudeExtractor(altitudeExtractor)
    , m_stepsInDegree(stepsInDegree)
    , m_kernel(kernel)
  {
    m_kernelSize = static_cast<size_t>(sqrt(m_kernel.size()));
    CHECK_EQUAL(m_kernelSize * m_kernelSize, m_kernel.size(), ());
  }
  FilteredTile(FilteredTile && rhs) = default;

  void Init(std::string const & dir, ms::LatLon const & coord);

  inline bool IsValid() const { return m_valid; }
  // Returns height in meters at |coord| or kInvalidAltitude.
  geometry::Altitude GetHeight(ms::LatLon const & coord);

  static std::string GetBase(ms::LatLon const & coord);
private:
  AltitudeExtractor & m_altitudeExtractor;
  size_t m_stepsInDegree;
  std::vector<double> const & m_kernel;
  size_t m_kernelSize;
  std::vector<geometry::Altitude> m_altitudes;
  bool m_valid = false;

  DISALLOW_COPY(FilteredTile);
};

class BluredAltitudeExtractor : public AltitudeExtractor
{
public:
  BluredAltitudeExtractor(AltitudeExtractor & altitudeExtractor, size_t stepsInDegree)
    : m_altitudeExtractor(altitudeExtractor)
    , m_stepsInDegree(stepsInDegree)
  {}

  geometry::Altitude GetAltitude(ms::LatLon const & pos) override;

private:
  AltitudeExtractor & m_altitudeExtractor;
  std::unordered_map<std::string, FilteredTile> m_tiles;
  size_t m_stepsInDegree;
};

class SRTMAltExtractor : public AltitudeExtractor
{
public:
  explicit SRTMAltExtractor(generator::SrtmTileManager & srtmManager)
    : m_srtmManager(srtmManager) {}

  geometry::Altitude GetAltitude(ms::LatLon const & pos) override
  {
    return m_srtmManager.GetHeight(pos);
  }

private:
  generator::SrtmTileManager & m_srtmManager;
};
}  // namespace generator
