#pragma once

#include "topography_generator/utils/values_provider.hpp"
#include "generator/srtm_parser.hpp"

namespace topography_generator
{
class AltitudeProvider : public ValuesProvider<geometry::Altitude>
{
  geometry::Altitude GetInvalidValue() const override { return geometry::kInvalidAltitude; }
};

struct FilteringParams
{
  double m_gaussianStDev = 2.0;
  double m_gaussianRadiusFactor = 1.0;
  size_t m_medianKernelRadius = 0;
};

class FilteredSRTMTile
{
public:
  FilteredSRTMTile(generator::SrtmTileManager & srtmTileManager, size_t stepsInDegree, size_t medianKernelSize,
                   std::vector<double> const & kernel, bool linearKernel)
    : m_srtmTileManager(srtmTileManager)
    , m_stepsInDegree(stepsInDegree)
    , m_kernel(kernel)
    , m_linearKernel(linearKernel)
    , m_medianKernelRadius(medianKernelSize)
  {
    if (m_linearKernel)
    {
      m_kernelSize = m_kernel.size();
    }
    else
    {
      m_kernelSize = static_cast<size_t>(sqrt(m_kernel.size()));
      CHECK_EQUAL(m_kernelSize * m_kernelSize, m_kernel.size(), ());
    }
  }

  FilteredSRTMTile(FilteredSRTMTile && rhs) = default;

  void Init(std::string const & dir, ms::LatLon const & coord);

  inline bool IsValid() const { return m_valid; }
  // Returns height in meters at |coord| or kInvalidAltitude.
  geometry::Altitude GetHeight(ms::LatLon const & coord);

  static std::string GetBase(ms::LatLon const & coord);
private:
  void ProcessMedian(size_t stepsCount, std::vector<geometry::Altitude> const & origAltitudes,
                     std::vector<geometry::Altitude> & dstAltitudes);
  void ProcessWithLinearKernel(size_t stepsCount,
                               std::vector<geometry::Altitude> const & originalAltitudes,
                               std::vector<geometry::Altitude> & dstAltitudes);
  void ProcessWithSquareKernel(size_t stepsCount,
                               std::vector<geometry::Altitude> const & originalAltitudes,
                               std::vector<geometry::Altitude> & dstAltitudes);
  void Finalize(size_t stepsCount, std::vector<geometry::Altitude> & originalAltitudes);

  generator::SrtmTileManager & m_srtmTileManager;
  size_t m_stepsInDegree;
  std::vector<double> const & m_kernel;
  bool m_linearKernel;
  size_t m_kernelSize;
  size_t m_medianKernelRadius;
  std::vector<geometry::Altitude> m_altitudes;
  bool m_valid = false;

  DISALLOW_COPY(FilteredSRTMTile);
};

class FilteredSRTMTileManager : public AltitudeProvider
{
public:
  FilteredSRTMTileManager(generator::SrtmTileManager & srtmTileManager, size_t stepsInDegree)
    : m_srtmTileManager(srtmTileManager)
    , m_stepsInDegree(stepsInDegree)
  {}

  geometry::Altitude GetValue(ms::LatLon const & pos) override;

private:
  generator::SrtmTileManager & m_srtmTileManager;
  size_t m_stepsInDegree;
  std::unordered_map<std::string, FilteredSRTMTile> m_tiles;
};
}  // namespace topography_generator
