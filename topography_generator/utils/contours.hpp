#pragma once

#include "geometry/latlon.hpp"

#include "base/visitor.hpp"

#include <vector>

namespace topography_generator
{
using Contour = std::vector<ms::LatLon>;

template <typename ValueType>
struct Contours
{
  std::vector<std::vector<Contour>> m_contours;
  ValueType m_minValue;
  ValueType m_maxValue;
  ValueType m_valueStep;
  size_t m_invalidValuesCount = 0; // for debug purpose only.
};
}  // namespace topography_generator

