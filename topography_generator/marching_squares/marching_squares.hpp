#pragma once

#include "topography_generator/marching_squares/contours_builder.hpp"
#include "topography_generator/marching_squares/square.hpp"
#include "topography_generator/utils/contours.hpp"

#include "geometry/rect2d.hpp"

#include "base/logging.hpp"

namespace topography_generator
{
template <typename ValueType>
class MarchingSquares
{
public: //45.492268, 6.603504
  MarchingSquares(ms::LatLon const & leftBottom, ms::LatLon const & rightTop,
                  double step, ValueType valueStep, ValuesProvider<ValueType> & valuesProvider)
    : m_leftBottom(45.48, 6.5)//leftBottom)
    , m_rightTop(45.50, 6.7)//rightTop)
    , m_step(step)
    , m_valueStep(valueStep)
    , m_valuesProvider(valuesProvider)
  {
    CHECK_GREATER(m_rightTop.m_lon, m_leftBottom.m_lon, ());
    CHECK_GREATER(m_rightTop.m_lat, m_leftBottom.m_lat, ());

    m_stepsCountLon = static_cast<size_t>((m_rightTop.m_lon - m_leftBottom.m_lon) / step);
    m_stepsCountLat = static_cast<size_t>((m_rightTop.m_lat - m_leftBottom.m_lat) / step);

    CHECK_GREATER(m_stepsCountLon, 0, ());
    CHECK_GREATER(m_stepsCountLat, 0, ());
  }

  void GenerateContours(Contours<ValueType> & result)
  {
    ScanValuesInRect(result.m_minValue, result.m_maxValue, result.m_invalidValuesCount);
    result.m_valueStep = m_valueStep;

    auto const levelsCount = static_cast<size_t>(result.m_maxValue - result.m_minValue) / m_valueStep;
    if (levelsCount == 0)
    {
      LOG(LINFO, ("Contours can't be generated: min and max values are equal:", result.m_minValue));
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

        if (i == 2058 && j == 1963)
        {
          LOG(LINFO, (i, j, pos, square.m_top, square.m_right,
            square.m_valueLB, square.m_valueLT, square.m_valueRB, square.m_valueRT));
        }

        square.GenerateSegments(contoursBuilder);

        //45.492268, 6.603504
        static m2::RectD limitRect(m2::PointD(45.492, 6.603), m2::PointD(49.493, 6.604));
        if (limitRect.IsPointInside(m2::PointD(pos.m_lat, pos.m_lon)) && (i & 1) && (j & 1))
        {
          contoursBuilder.AddSegment(0, ms::LatLon(square.m_bottom, square.m_left), ms::LatLon(square.m_top, square.m_left));
          contoursBuilder.AddSegment(0, ms::LatLon(square.m_top, square.m_left), ms::LatLon(square.m_top, square.m_right));
          contoursBuilder.AddSegment(0, ms::LatLon(square.m_top, square.m_right), ms::LatLon(square.m_bottom, square.m_right));
          contoursBuilder.AddSegment(0, ms::LatLon(square.m_bottom, square.m_right), ms::LatLon(square.m_bottom, square.m_left));
        }

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
        auto const value = m_valuesProvider.GetValue(pos);
        if (i >= 2057 && i <= 2059 && j >= 1962 && j <= 1964)
        {
          LOG(LINFO, (i, j, pos, value));
        }
        if (value == m_valuesProvider.GetInvalidValue())
        {
          ++invalidValuesCount;
          continue;
        }
        if (value < minValue)
          minValue = value;
        if (value > maxValue)
          maxValue = value;
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
