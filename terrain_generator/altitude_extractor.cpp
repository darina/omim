#include "altitude_extractor.hpp"

#include "base/file_name_utils.hpp"

namespace generator
{
void FilteredTile::Init(std::string const & dir, ms::LatLon const & coord)
{
  CHECK(!m_valid, ());
  std::string const base = GetBase(coord);
  std::string const path = base::JoinPath(dir, base + ".SRTMGL1.blured.hgt");

  ms::LatLon orig(static_cast<int>(coord.m_lat), static_cast<int>(coord.m_lon));
  orig.m_lat += 1.0; // from North to South

  size_t stepsCount = (m_stepsInDegree + 1) + m_kernelSize - 1;
  double const step = 1.0 / m_stepsInDegree;

  std::vector<geometry::Altitude> originalAltitudes;
  originalAltitudes.resize(stepsCount * stepsCount);

  m_altitudes.resize((m_stepsInDegree + 1) * (m_stepsInDegree + 1), geometry::kInvalidAltitude);

  double const offset = step * (m_kernelSize - 1) / 2;

  ms::LatLon shiftedCoord = ms::LatLon(orig.m_lat + offset, orig.m_lon - offset);
  for (size_t i = 0; i < stepsCount; ++i)
  {
    for (size_t j = 0; j < stepsCount; ++j)
    {
      auto const pos = ms::LatLon(shiftedCoord.m_lat - i * step,
                                  shiftedCoord.m_lon + j * step);
      originalAltitudes[i * stepsCount + j] = m_altitudeExtractor.GetAltitude(pos);
    }
  }

  for (size_t i = 0; i < m_stepsInDegree + 1; ++i)
  {
    for (size_t j = 0; j < m_stepsInDegree + 1; ++j)
    {
      size_t const orig_index = i * stepsCount + j;
      size_t const index = i * (m_stepsInDegree + 1) + j;
      m_altitudes[index] = 0;
      for (size_t ki = 0; ki < m_kernelSize; ++ki)
      {
        for (size_t kj = 0; kj < m_kernelSize; ++kj)
          m_altitudes[index] += m_kernel[ki * m_kernelSize + kj] * originalAltitudes[orig_index + ki * stepsCount + kj];
      }
    }
  }
  m_valid = true;
}

geometry::Altitude FilteredTile::GetHeight(ms::LatLon const & coord)
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

  size_t const row = m_stepsInDegree * lt;
  size_t const col = m_stepsInDegree * ln;

  size_t const ix = row * (m_stepsInDegree + 1) + col;

  if (ix >= m_altitudes.size())
    return geometry::kInvalidAltitude;
  return m_altitudes[ix];
}

std::string FilteredTile::GetBase(ms::LatLon const & coord)
{
  return SrtmTile::GetBase(coord);
}

geometry::Altitude BluredAltitudeExtractor::GetAltitude(ms::LatLon const & pos)
{
  static std::vector<double> kernel = {
    0.000789, 0.006581, 0.013347, 0.006581, 0.000789,
    0.006581, 0.054901, 0.111345, 0.054901, 0.006581,
    0.013347, 0.111345, 0.225821, 0.111345, 0.013347,
    0.006581, 0.054901, 0.111345, 0.054901, 0.006581,
    0.000789, 0.006581, 0.013347, 0.006581, 0.000789 };

  std::string const base = FilteredTile::GetBase(pos);
  auto it = m_tiles.find(base);
  if (it == m_tiles.end())
  {
    FilteredTile tile(m_altitudeExtractor, m_stepsInDegree, kernel);
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
}  // namespace generator
