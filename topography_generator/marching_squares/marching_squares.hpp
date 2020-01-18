#pragma once

#include "topography_generator/marching_squares/contours_builder.hpp"
#include "topography_generator/marching_squares/square.hpp"

//#include "geometry/rect2d.hpp"

#include <vector>

namespace topography_generator
{
template <typename ValueType>
struct ContoursResult
{
  std::vector<std::vector<ms::LatLon>> m_contours;
  ValueType m_minValue;
  ValueType m_maxValue;
  size_t m_invalidValuesCount = 0;
};

template <typename ValueType>
class MarchingSquares
{
public:
  MarchingSquares(ms::LatLon const & leftBottom, ms::LatLon const & rightTop,
                  double step, ValueType valueStep, ValuesProvider<ValueType> & valuesProvider)
    : m_leftBottom(leftBottom),
      m_rightTop(rightTop),
      m_step(step),
      m_valueStep(valueStep),
      m_valuesProvider(valuesProvider)
  {
    CHECK_GREATER(m_rightTop.m_lon, m_leftBottom.m_lon, ());
    CHECK_GREATER(m_rightTop.m_lat, m_leftBottom.m_lat, ());

    m_stepsCountLon = static_cast<size_t>((m_rightTop.m_lon - m_leftBottom.m_lon) / step);
    m_stepsCountLat = static_cast<size_t>((m_rightTop.m_lat - m_leftBottom.m_lat) / step);

    CHECK_GREATER(m_stepsCountLon, 0, ());
    CHECK_GREATER(m_stepsCountLat, 0, ());
  }

  void GenerateContours(ContoursResult<ValueType> & result)
  {
    ScanValuesInRect(result.m_minValue, result.m_maxValue, result.m_invalidValuesCount);

    auto const levelsCount = static_cast<size_t>(result.maxValue - result.minValue) / m_valueStep;
    if (levelsCount == 0)
    {
      LOG(LINFO, ("Contours can't be generated: min and max values are equal."));
      return;
    }

    ContoursBuilder contoursBuilder(levelsCount);

    for (size_t i = 0; i < m_stepsCountLat - 1; ++i)
    {
      contoursBuilder.BeginLine();
      for (size_t j = 0; j < m_stepsCountLon - 1; ++j)
      {
        auto const pos = ms::LatLon(m_leftBottom.m_lat + m_step * i, m_leftBottom.m_lon + m_step * j);
        Square<ValueType> square(pos, m_step, result.m_minValue, m_valueStep, m_valuesProvider);
        square.GenerateSegments(contoursBuilder);

        /*
        static m2::RectD limitRect(m2::PointD(59.92, 56.92), m2::PointD(59.96, 56.96));
        if (limitRect.IsPointInside(m2::PointD(pos.m_lat, pos.m_lon)) && (i & 1) && (j & 1))
        {
          contoursBuilder.addSegment(0, ms::LatLon(square.m_bottom, square.m_left), ms::LatLon(square.m_top, square.m_left));
          contoursBuilder.addSegment(0, ms::LatLon(square.m_top, square.m_left), ms::LatLon(square.m_top, square.m_right));
          contoursBuilder.addSegment(0, ms::LatLon(square.m_top, square.m_right), ms::LatLon(square.m_bottom, square.m_right));
          contoursBuilder.AddSegment(0, ms::LatLon(square.m_bottom, square.m_right), ms::LatLon(square.m_bottom, square.m_left));
        }
        */
      }
      auto const isLastLine = i == m_stepsCountLat - 2;
      contoursBuilder.EndLine(isLastLine);
    }

    contoursBuilder.GetContours(result.m_contours);
  }

private:
  void ScanValuesInRect(ValueType & minValue, ValueType & maxValue, size_t & invalidValuesCount) const
  {
    minValue = maxValue = m_valuesProvider.GetValue(m_leftBottom);
    invalidValuesCount = 0;

    for (size_t i = 0; i < m_stepsCountLat; ++i)
    {
      for (size_t j = 0; j < m_stepsCountLon; ++j)
      {
        auto const pos = ms::LatLon(m_leftBottom.m_lat + m_step * i,
                                    m_leftBottom.m_lon + m_step * j);
        auto const altitude = m_valuesProvider.GetAltitude(pos);
        if (altitude == geometry::kInvalidAltitude)
        {
          ++invalidValuesCount;
          continue;
        }
        if (altitude < minValue)
          minValue = altitude;
        if (altitude > minValue)
          maxValue = altitude;
      }
    }

    if (minValue > 0)
      minValue = m_valueStep * ((minValue + m_valueStep - 1) / m_valueStep);
    else
      minValue = m_valueStep * (minValue / m_valueStep);

    if (maxValue > 0)
      maxValue = m_valueStep * ((maxValue + m_valueStep) / m_valueStep);
    else
      maxValue = m_valueStep * (maxValue / m_valueStep);

    CHECK_GREATER_OR_EQUAL(maxValue, minValue, ());
  }

  ms::LatLon const m_leftBottom;
  ms::LatLon const m_rightTop;
  double const m_step;
  ValueType const m_valueStep;
  ValuesProvider<ValueType> & m_valuesProvider;

  size_t m_stepsCountLon;
  size_t m_stepsCountLat;
};
}  // namespace topography_generator
