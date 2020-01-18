#include "topography_generator/marching_squares/contours_builder.hpp"

namespace topography_generator
{
double constexpr kEps = 1e-7;

ContoursBuilder::ContoursBuilder(size_t levelsCount)
  : m_levelsCount(levelsCount)
{
  m_finalizedContours.resize(m_levelsCount);
  m_activeContours.resize(m_levelsCount);
}

void ContoursBuilder::GetContours(std::vector<std::vector<ms::LatLon>> & contours)
{
  contours.clear();
  contours.resize(m_finalizedContours.size());
  for (size_t i = 0; i < m_finalizedContours.size(); ++i)
  {
    auto const & contoursList = m_finalizedContours[i];
    for (auto const & contour : contoursList)
    {
      contours[i].emplace_back(contour.begin(), contour.end());
    }
  }
}

void ContoursBuilder::AddSegment(size_t levelInd, ms::LatLon const & beginPos, ms::LatLon const & endPos)
{
  if (beginPos.EqualDxDy(endPos, kEps))
    return;

  CHECK_LESS(levelInd, m_levelsCount, ());

  auto contourItBefore = FindContourWithEndPoint(levelInd, beginPos);
  auto contourItAfter = FindContourWithStartPoint(levelInd, endPos);
  auto const connectStart = contourItBefore != m_activeContours[levelInd].end();
  auto const connectEnd = contourItAfter != m_activeContours[levelInd].end();

  if (connectStart && connectEnd && contourItBefore != contourItAfter)
  {
    contourItBefore->m_countour.insert(contourItBefore->m_countour.end(),
      contourItAfter->m_countour.begin(), contourItAfter->m_countour.end());
    contourItBefore->m_active = true;
    m_activeContours[levelInd].erase(contourItAfter);
  }
  else if (connectStart)
  {
    contourItBefore->m_countour.push_back(endPos);
    contourItBefore->m_active = true;
  }
  else if (connectEnd)
  {
    contourItAfter->m_countour.push_front(beginPos);
    contourItBefore->m_active = true;
  }
  else
  {
    m_activeContours[levelInd].emplace_back(beginPos, endPos);
  }
}

void ContoursBuilder::BeginLine()
{
  for (auto & contoursList : m_activeContours)
  {
    for (auto & activeContour : contoursList)
      activeContour.m_active = false;
  }
}

void ContoursBuilder::EndLine(bool finalLine)
{
  for (size_t levelInd = 0; levelInd < m_activeContours.size(); ++levelInd)
  {
    auto & contours = m_activeContours[levelInd];
    auto it = contours.begin();
    while (it != contours.end())
    {
      if (!it->m_active || finalLine)
      {
        m_finalizedContours[levelInd].push_back(std::move(it->m_countour));
        it = contours.erase(it);
      }
      else
      {
        ++it;
      }
    }
  }
}

ContoursBuilder::ActiveContourIter ContoursBuilder::FindContourWithStartPoint(size_t levelInd, ms::LatLon const & pos)
{
  auto & contours = m_activeContours[levelInd];
  for (auto it = contours.begin(); it != contours.end(); ++it)
  {
    if (it->m_countour.front().EqualDxDy(pos, kEps))
      return it;
  }
  return contours.end();
}

ContoursBuilder::ActiveContourIter ContoursBuilder::FindContourWithEndPoint(size_t levelInd, ms::LatLon const & pos)
{
  auto & contours = m_activeContours[levelInd];
  for (auto it = contours.begin(); it != contours.end(); ++it)
  {
    if (it->m_countour.back().EqualDxDy(pos, kEps))
      return it;
  }
  return contours.end();
}
}  // namespace topography_generator
