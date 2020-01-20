#pragma once

#include "generator/feature_helpers.hpp"

#include "geometry/point2d.hpp"
#include "geometry/region2d.hpp"

#include <vector>
#include <unordered_map>

namespace topography_generator
{
using Contour = std::vector<m2::PointD>;

template <typename ValueType>
struct Contours
{
  std::unordered_map<ValueType, std::vector<Contour>> m_contours;
  ValueType m_minValue;
  ValueType m_maxValue;
  ValueType m_valueStep;
  size_t m_invalidValuesCount = 0; // for debug purpose only.
};

template <typename ValueType>
void CropContours(m2::RectD & rect, std::vector<m2::RegionD> & regions, size_t maxLength,
                  Contours<ValueType> & contours)
{
  contours.m_minValue = std::numeric_limits<geometry::Altitude>::max();
  contours.m_maxValue = std::numeric_limits<geometry::Altitude>::min();

  auto it = contours.m_contours.begin();
  while (it != contours.m_contours.end())
  {
    std::vector<Contour> levelCroppedContours;
    for (auto const & contour : it->second)
    {
      Contour cropped;
      cropped.reserve(contour.size());
      for (auto const & pt : contour)
      {
        cropped.push_back(pt);
        if (!rect.IsPointInside(pt) || !RegionsContain(regions, pt) || cropped.size() == maxLength)
        {
          if (cropped.size() > 1)
            levelCroppedContours.emplace_back(std::move(cropped));
          cropped = {};
        }
      }
      if (cropped.size() > 1)
        levelCroppedContours.emplace_back(std::move(cropped));
    }
    it->second.swap(levelCroppedContours);

    if (!it->second.empty())
    {
      contours.m_minValue = std::min(it->first, contours.m_minValue);
      contours.m_maxValue = std::max(it->first, contours.m_maxValue);
      ++it;
    }
    else
    {
      it = contours.m_contours.erase(it);
    }
  }
}

template <typename ValueType>
void SimplifyContours(int simplificationZoom, Contours<ValueType> & contours)
{
  for (auto & levelContours : contours.m_contours)
  {
    for (auto & contour : levelContours.second)
    {
      std::vector<m2::PointD> contourSimple;
      feature::SimplifyPoints(m2::SquaredDistanceFromSegmentToPoint<m2::PointD>(),
                              simplificationZoom, contour, contourSimple);
      contour.swap(contourSimple);
    }
  }
}
}  // namespace topography_generator

