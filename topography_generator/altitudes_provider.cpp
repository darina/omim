#include "altitudes_provider.hpp"

#include "base/file_name_utils.hpp"

namespace topography_generator
{
void FilteredSRTMTile::Init(std::string const & dir, ms::LatLon const & coord)
{
  CHECK(!m_valid, ());
  std::string const base = GetBase(coord);
  std::string const path = base::JoinPath(dir, base + ".SRTMGL1.blured.hgt");

  ms::LatLon orig(static_cast<int>(coord.m_lat), static_cast<int>(coord.m_lon));
  orig.m_lat += 1.0; // from North to South

  size_t const stepsOffset = std::max((m_kernelSize - 1) / 2, m_medianKernelRadius);
  size_t const stepsCount = (m_stepsInDegree + 1) + 2 * stepsOffset;
  double const step = 1.0 / m_stepsInDegree;

  std::vector<geometry::Altitude> originalAltitudes;
  originalAltitudes.resize(stepsCount * stepsCount);

  double const offset = step * stepsOffset;

  ms::LatLon shiftedCoord = ms::LatLon(orig.m_lat + offset, orig.m_lon - offset);
  for (size_t i = 0; i < stepsCount; ++i)
  {
    for (size_t j = 0; j < stepsCount; ++j)
    {
      auto const pos = ms::LatLon(shiftedCoord.m_lat - i * step,
                                  shiftedCoord.m_lon + j * step);
      originalAltitudes[i * stepsCount + j] = m_altitudeProvider.GetAltitude(pos);
    }
  }


  if (m_medianKernelRadius > 0)
  {
    std::vector<geometry::Altitude> tempAltitudes = originalAltitudes;
    ProcessMedian(stepsCount, tempAltitudes, originalAltitudes);
  }
  if (m_linearKernel)
  {
    std::vector<geometry::Altitude> tempAltitudes = originalAltitudes;
    ProcessWithLinearKernel(stepsCount, tempAltitudes, originalAltitudes);
  }
  else
  {
    std::vector<geometry::Altitude> tempAltitudes = originalAltitudes;
    ProcessWithSquareKernel(stepsCount, tempAltitudes, originalAltitudes);
  }
  Finalize(stepsCount, originalAltitudes);

  m_valid = true;
}

void FilteredSRTMTile::ProcessMedian(size_t stepsCount, std::vector<geometry::Altitude> const & origAltitudes,
                                     std::vector<geometry::Altitude> & dstAltitudes)
{
  size_t const offset = (stepsCount - m_stepsInDegree - 1) /  2;
  CHECK_LESS_OR_EQUAL(m_medianKernelRadius, offset, ());

  size_t kernelSize = m_medianKernelRadius * 2 + 1;
  std::vector<geometry::Altitude> kernel(kernelSize * kernelSize);
  for (size_t i = 0; i < m_stepsInDegree + 1; ++i)
  {
    for (size_t j = 0; j < m_stepsInDegree + 1; ++j)
    {
      for (size_t ki = 0; ki < kernelSize; ++ki)
      {
        for (size_t kj = 0; kj < kernelSize; ++kj)
        {
          size_t const origIndex = (i + offset - m_medianKernelRadius + ki) * stepsCount + j + offset - m_medianKernelRadius + kj;
          kernel[ki * kernelSize + kj] = origAltitudes[origIndex];
        }
      }
      std::sort(kernel.begin(), kernel.end());
      size_t const dstIndex = (i + offset) * stepsCount + j + offset;
      dstAltitudes[dstIndex] = kernel[kernel.size() / 2];
    }
  }
}

void FilteredSRTMTile::ProcessWithLinearKernel(size_t stepsCount, std::vector<geometry::Altitude> const & originalAltitudes,
                                               std::vector<geometry::Altitude> & dstAltitudes)
{
  size_t const offset = (stepsCount - m_stepsInDegree - 1) /  2;
  CHECK_LESS_OR_EQUAL(m_kernelSize / 2, offset, ());

  for (size_t i = 0; i < m_stepsInDegree + 1; ++i)
  {
    std::vector<geometry::Altitude> tempAltitudes(m_stepsInDegree + 1, 0);
    for (size_t j = 0; j < m_stepsInDegree + 1; ++j)
    {
      for (size_t ki = 0; ki < m_kernelSize; ++ki)
      {
        size_t const orig_index = (i + offset) * stepsCount + j + offset - m_kernelSize / 2 + ki;
        tempAltitudes[j] += m_kernel[ki] * originalAltitudes[orig_index];
      }
    }
    for (size_t j = 0; j < m_stepsInDegree + 1; ++j)
    {
      size_t const orig_index = (i + offset) * stepsCount + j + offset;
      dstAltitudes[orig_index] = tempAltitudes[j];
    }
  }

  for (size_t j = 0; j < m_stepsInDegree + 1; ++j)
  {
    std::vector<geometry::Altitude> tempAltitudes(m_stepsInDegree + 1, 0);
    for (size_t i = 0; i < m_stepsInDegree + 1; ++i)
    {
      for (size_t ki = 0; ki < m_kernelSize; ++ki)
      {
        size_t const orig_index = (i + offset - m_kernelSize / 2 + ki) * stepsCount + j + offset;
        tempAltitudes[i] += m_kernel[ki] * dstAltitudes[orig_index];
      }
    }
    for (size_t i = 0; i < m_stepsInDegree + 1; ++i)
    {
      size_t const dstIndex = (i + offset) * stepsCount + j + offset;
      dstAltitudes[dstIndex] = tempAltitudes[i];
    }
  }
}

void FilteredSRTMTile::ProcessWithSquareKernel(size_t stepsCount, std::vector<geometry::Altitude> const & originalAltitudes,
                                               std::vector<geometry::Altitude> & dstAltitudes)
{
  size_t const offset = (stepsCount - m_stepsInDegree - 1) /  2;
  CHECK_LESS_OR_EQUAL(m_kernelSize / 2, offset, ());

  for (size_t i = 0; i < m_stepsInDegree + 1; ++i)
  {
    for (size_t j = 0; j < m_stepsInDegree + 1; ++j)
    {
      size_t const dstIndex = (i + offset) * (m_stepsInDegree + 1) + j + offset;
      dstAltitudes[dstIndex] = 0;
      for (size_t ki = 0; ki < m_kernelSize; ++ki)
      {
        for (size_t kj = 0; kj < m_kernelSize; ++kj)
        {
          size_t const orig_index = (i + offset - m_kernelSize / 2 + ki) * stepsCount + j + offset - m_kernelSize / 2 + kj;
          dstAltitudes[dstIndex] += m_kernel[ki * m_kernelSize + kj] * originalAltitudes[orig_index];
        }
      }
    }
  }
}

void FilteredSRTMTile::Finalize(size_t stepsCount, std::vector<geometry::Altitude> & originalAltitudes)
{
  size_t const offset = (stepsCount - m_stepsInDegree - 1) /  2;
  CHECK_LESS_OR_EQUAL(m_kernelSize / 2, offset, ());

  m_altitudes.resize((m_stepsInDegree + 1) * (m_stepsInDegree + 1), 0);
  for (size_t i = 0; i < m_stepsInDegree + 1; ++i)
  {
    for (size_t j = 0; j < m_stepsInDegree + 1; ++j)
    {
      size_t const index = i * (m_stepsInDegree + 1) + j;
      size_t const orig_index = (i + offset) * stepsCount + j + offset;
      m_altitudes[index] = originalAltitudes[orig_index];
    }
  }
}

geometry::Altitude FilteredSRTMTile::GetHeight(ms::LatLon const & coord)
{
  if (!IsValid())
    return geometry::kInvalidAltitude;

  double ln = coord.m_lon - static_cast<int>(coord.m_lon);
  if (ln < 0)
    ln += 1;
  double lt = coord.m_lat - static_cast<int>(coord.m_lat);
  if (lt < 0)
    lt += 1;
  lt = 1 - lt;  // from North to South

  size_t const row = m_stepsInDegree * lt + 0.5;
  size_t const col = m_stepsInDegree * ln + 0.5;

  size_t const ix = row * (m_stepsInDegree + 1) + col;

  if (ix >= m_altitudes.size())
    return geometry::kInvalidAltitude;
  return m_altitudes[ix];
}

std::string FilteredSRTMTile::GetBase(ms::LatLon const & coord)
{
  return SrtmTile::GetBase(coord);
}

geometry::Altitude FilteredSRTMTileManager::GetValue(ms::LatLon const & pos)
{
  if (pos.m_lat < 59.9)
    return m_srtmTileManager.GetHeight(pos);

  std::string const base = FilteredSRTMTile::GetBase(pos);
  auto it = m_tiles.find(base);
  if (it == m_tiles.end())
  {
    static std::vector<double> kernel = {
      0.000789, 0.006581, 0.013347, 0.006581, 0.000789,
      0.006581, 0.054901, 0.111345, 0.054901, 0.006581,
      0.013347, 0.111345, 0.225821, 0.111345, 0.013347,
      0.006581, 0.054901, 0.111345, 0.054901, 0.006581,
      0.000789, 0.006581, 0.013347, 0.006581, 0.000789 };

    double const gaussianStD = 2.0;
    double const radiusFactor = 1.0;
    size_t const medianKernelRadius = 1;
    auto const kernelRadius = static_cast<size_t>(ceil(radiusFactor * gaussianStD));
    auto const kernelSize = 2 * kernelRadius + 1;
    std::vector<double> linearKernel(kernelSize, 0);

    double sum = 1.0;
    linearKernel[kernelRadius] = 1.0;
    for (int i = 1; i <= kernelRadius; ++i)
    {
      double const val = exp(-i * i / (2 * gaussianStD * gaussianStD));
      linearKernel[kernelRadius - i] = linearKernel[kernelRadius + i] = val;
      sum += 2.0 * val;
    }
    for (auto & val : linearKernel)
      val /= sum;

    FilteredSRTMTile tile(m_altitudeProvider, m_stepsInDegree, medianKernelRadius, linearKernel, true);

    try
    {
      tile.Init("", pos);
    }
    catch (RootException const & e)
    {
      LOG(LINFO, ("Can't init filtered tile:", base, "reason:", e.Msg()));
    }
    // It's OK to store even invalid tiles and return invalid height
    // for them later.
    it = m_tiles.emplace(base, std::move(tile)).first;
  }
  return it->second.GetHeight(pos);
}
}  // namespace topography_generator
