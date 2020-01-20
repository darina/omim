#pragma once

#include "topography_generator/utils/contours.hpp"

#include "geometry/latlon.hpp"

#include <deque>
#include <list>
#include <vector>

namespace topography_generator
{
class ContoursBuilder
{
public:
  explicit ContoursBuilder(size_t levelsCount);

  void AddSegment(size_t levelInd, ms::LatLon const & beginPos, ms::LatLon const & endPos);
  void BeginLine();
  void EndLine(bool finalLine);

  void GetContours(std::vector<std::vector<Contour>> & contours);

private:
  using ContourRaw = std::deque<ms::LatLon>;
  using ContoursList = std::list<ContourRaw>;

  struct ActiveContour
  {
    explicit ActiveContour(ContourRaw && isoline)
      : m_countour(std::move(isoline))
    {}

    ContourRaw m_countour;
    bool m_active = true;
  };
  using ActiveContoursList = std::list<ActiveContour>;
  using ActiveContourIter = ActiveContoursList::iterator;

  ActiveContourIter FindContourWithStartPoint(size_t levelInd, ms::LatLon const & pos);
  ActiveContourIter FindContourWithEndPoint(size_t levelInd, ms::LatLon const & pos);

  size_t const m_levelsCount;

  std::vector<ContoursList> m_finalizedContours;
  std::vector<ActiveContoursList> m_activeContours;
};
}  // namespace topography_generator
