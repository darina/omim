#include "marching_squares.hpp"

IsolinesWriter::IsolinesWriter(std::vector<IsolinesList> & isolines)
: m_isolines(isolines)
//, m_unsolvedSegments(isolines.size())
{

}

void IsolinesWriter::addSegment(size_t ind, ms::LatLon const & beginPos, ms::LatLon const & endPos)
{
  if (beginPos.EqualDxDy(endPos, EPS))
    return;

  ASSERT(ind < m_isolines.size(), ());
  auto lineIterBefore = findLineEndsWith(ind, beginPos);
  auto lineIterAfter = findLineStartsWith(ind, endPos);
  bool connectStart = lineIterBefore != m_isolines[ind].end();
  bool connectEnd = lineIterAfter != m_isolines[ind].end();
  if (connectStart && connectEnd && lineIterBefore != lineIterAfter)
  {
    lineIterBefore->splice(lineIterBefore->end(), *lineIterAfter);
    m_isolines[ind].erase(lineIterAfter);
    LOG(LWARNING, ("Splice for height", ind * 10, "points", lineIterBefore->size()));
    // No new points to check unsolved segments with.
    //return;
  }
  else if (connectStart)
  {
    lineIterBefore->push_back(endPos);
    //resolveSegments(ind, *lineIterBefore, false, true);
  }
  else if (connectEnd)
  {
    lineIterAfter->push_front(beginPos);
    //resolveSegments(ind, *lineIterAfter, true, false);
  }
  else
  {
    Isoline line = { beginPos, endPos };
    m_isolines[ind].push_back(std::move(line));
    //resolveSegments(ind, m_isolines[ind].back(), true, true);
  }
}

IsolineIter IsolinesWriter::findLineStartsWith(size_t ind, ms::LatLon const & pos)
{
  auto & lines = m_isolines[ind];
  for (auto it = lines.begin(); it != lines.end(); ++it)
  {
    if (it->front().EqualDxDy(pos, EPS))
      return it;
  }
  return lines.end();
}

IsolineIter IsolinesWriter::findLineEndsWith(size_t ind, ms::LatLon const & pos)
{
  auto & lines = m_isolines[ind];
  for (auto it = lines.begin(); it != lines.end(); ++it)
  {
    if (it->back().EqualDxDy(pos, EPS))
      return it;
  }
  return lines.end();
}

/*void IsolinesWriter::addUnsolvedSegments(size_t ind,
                                         ms::LatLon const & start1,
                                         ms::LatLon const & end1,
                                         ms::LatLon const & start2,
                                         ms::LatLon const & end2)
{
  ASSERT(ind < m_unsolvedSegments.size(), ());
  UnsolvedSegment unsolved = {start1, end1, start2, end2};
  m_unsolvedSegments[ind].push_back(std::move(unsolved));
}*/

/*void IsolinesWriter::resolveSegments(size_t ind, Isoline & line, bool start, bool end)
{
  auto & list = m_unsolvedSegments[ind];
  bool found = false;
  do
  {
    for (auto it = list.begin(); it != list.end(); ++it)
    {
      if (start)
      {
        uint8_t ptInd = checkUnsolvedSegment(*it, line.front());
        if (ptInd != 4)
        {
         found = true;
          // TODO: resolve
         list.erase(it);
         break;
        }
      }
      if (end)
      {
        uint8_t ptInd = checkUnsolvedSegment(*it, line.back());
        if (ptInd != 4)
        {
          found = true;
          // TODO: resolve
          list.erase(it);
          break;
        }
      }
    }
  } while (found);
}

uint8_t IsolinesWriter::checkUnsolvedSegment(UnsolvedSegment const & segment,
                                          ms::LatLon const & pos)
{
  uint8_t pointInd;
  for (pointInd = 0; pointInd < 4; ++pointInd)
  {
    if (pos.EqualDxDy(segment[pointInd], EPS))
      break;
  }
  return pointInd;
}*/

Square::Square(ms::LatLon const & leftBottom,
               double size, AltitudeExtractor & altExtractor)
  : m_left(leftBottom.m_lon)
  , m_right(leftBottom.m_lon + size)
  , m_bottom(leftBottom.m_lat)
  , m_top(leftBottom.m_lat + size)
{
  m_hLB = altExtractor.GetAltitude(leftBottom);
  m_hLT = altExtractor.GetAltitude(ms::LatLon(m_top, m_left));
  m_hRT = altExtractor.GetAltitude(ms::LatLon(m_top, m_right));
  m_hRB = altExtractor.GetAltitude(ms::LatLon(m_bottom, m_right));
}

void Square::generateSegments(geometry::Altitude firstAltitude, uint16_t altStep, IsolinesWriter & writer)
{
  geometry::Altitude minAlt = std::min(m_hLB, std::min(m_hLT, std::min(m_hRT, m_hRB)));
  geometry::Altitude maxAlt = std::max(m_hLB, std::max(m_hLT, std::max(m_hRT, m_hRB)));

  if (minAlt > 0)
    minAlt = altStep * ((minAlt + altStep - 1) / altStep);
  else
    minAlt = altStep * (minAlt / altStep);
  if (maxAlt > 0)
    maxAlt = altStep * ((maxAlt + altStep - 1) / altStep);
  else
    maxAlt = altStep * (maxAlt / altStep);

  ASSERT(minAlt >= firstAltitude, ());

  for (auto alt = minAlt; alt < maxAlt; alt += altStep)
  {
    writeSegments(alt, (alt - firstAltitude) / altStep, writer);
  }
}

void Square::writeSegments(geometry::Altitude alt, uint16_t ind, IsolinesWriter & writer)
{
  // Segment is a vector directed so that higher altitude is on the right.
  std::pair<Rib, Rib> intersectedRibs[] =
    {
      {NONE, NONE},       // 0000
      {LEFT, BOTTOM},     // 0001
      {TOP, LEFT},        // 0010
      {TOP, BOTTOM},      // 0011
      {RIGHT, TOP},       // 0100
      {UNCLEAR, UNCLEAR}, // 0101
      {RIGHT, LEFT},      // 0110
      {RIGHT, BOTTOM},    // 0111
      {BOTTOM, RIGHT},    // 1000
      {LEFT, RIGHT},      // 1001
      {UNCLEAR, UNCLEAR}, // 1010
      {TOP, RIGHT},       // 1011
      {BOTTOM, TOP},      // 1100
      {LEFT, TOP},        // 1101
      {BOTTOM, LEFT},     // 1110
      {NONE, NONE},       // 1111
    };

  union Pattern
  {
    struct
    {
      uint8_t pLB : 1;
      uint8_t pLT : 1;
      uint8_t pRT : 1;
      uint8_t pRB : 1;
    };
    uint8_t value;
  } pattern;
  pattern.value = 0;
  pattern.pLB = m_hLB > alt;
  pattern.pLT = m_hLT > alt;
  pattern.pRT = m_hRT > alt;
  pattern.pRB = m_hRB > alt;

  //uint8_t pt = (m_hLB > alt ? 1u : 0u) | ((m_hLT > alt ? 1u : 0u) << 1)
  //  | ((m_hRT > alt ? 1u : 0u) << 2) | ((m_hRB > alt ? 1u : 0u) << 3);

  auto ribs = intersectedRibs[pattern.value];

  if (ribs.first == NONE)
    return;

  if (ribs.first != UNCLEAR)
  {
    writer.addSegment(ind, interpolatePoint(ribs.first, alt), interpolatePoint(ribs.second, alt));
  }
  else
  {
    auto const leftPos = interpolatePoint(LEFT, alt);
    auto const rightPos = interpolatePoint(RIGHT, alt);
    auto const bottomPos = interpolatePoint(BOTTOM, alt);
    auto const topPos = interpolatePoint(TOP, alt);

    uint16_t middleAlt = (m_hLB + m_hLT + m_hRT + m_hRB) / 4;
    if (middleAlt > alt)
    {
      if (m_hLB > alt)
      {
        writer.addSegment(ind, leftPos, topPos);
        writer.addSegment(ind, rightPos, bottomPos);
      }
      else
      {
        writer.addSegment(ind, bottomPos, leftPos);
        writer.addSegment(ind, topPos, rightPos);
      }
    }
    else
    {
      if (m_hLB > alt)
      {
        writer.addSegment(ind, leftPos, bottomPos);
        writer.addSegment(ind, rightPos, topPos);
      }
      else
      {
        writer.addSegment(ind, topPos, leftPos);
        writer.addSegment(ind, bottomPos, rightPos);
      }
    }
    /*if (m_hLB > alt)
      writer.addUnsolvedSegments(ind, leftPos, topPos, rightPos, bottomPos);
    else
      writer.addUnsolvedSegments(ind, topPos, leftPos, bottomPos, rightPos);*/
  }
}

ms::LatLon Square::interpolatePoint(Square::Rib rib, geometry::Altitude alt)
{
  double f1, f2;
  double lat = 0.0, lon = 0.0;
  switch (rib)
  {
  case LEFT:
    f1 = static_cast<double>(m_hLB);
    f2 = static_cast<double>(m_hLT);
    lon = m_left;
    break;
  case RIGHT:
    f1 = static_cast<double>(m_hRB);
    f2 = static_cast<double>(m_hRT);
    lon = m_right;
    break;
  case TOP:
    f1 = static_cast<double>(m_hLT);
    f2 = static_cast<double>(m_hRT);
    lat = m_top;
    break;
  case BOTTOM:
    f1 = static_cast<double>(m_hLB);
    f2 = static_cast<double>(m_hRB);
    lat = m_bottom;
    break;
  default:
    ASSERT(!"", ());
    return {};
  }

  double coeff = (f1 - alt) / (alt - f2);

  switch (rib)
  {
  case LEFT:
  case RIGHT:
    lat = alt != f2 ? (m_bottom + m_top * coeff) / (1 + coeff) : m_top;
    break;
  case BOTTOM:
  case TOP:
    lon = alt != f2 ? (m_left + m_right * coeff) / (1 + coeff) : m_right;
    break;
  default:
    return {};
  }

  if (std::isnan(lat) || std::isnan(lon))
  {
    LOG(LWARNING, ("nan value"));
  }

  return {lat, lon};
}

MarchingSquares::MarchingSquares(ms::LatLon const & leftBottom, ms::LatLon const & rightTop,
                    double step, uint16_t heightStep, AltitudeExtractor & altExtractor)
                    : m_leftBottom(leftBottom)
                    , m_rightTop(rightTop)
                    , m_step(step)
                    , m_heightStep(heightStep)
                    , m_altExtractor(altExtractor)
{
  ASSERT(m_rightTop.m_lon > m_leftBottom.m_lon, ());
  ASSERT(m_rightTop.m_lat > m_leftBottom.m_lat, ());
  m_stepsCountLon = static_cast<size_t>((m_rightTop.m_lon - m_leftBottom.m_lon) / step);
  m_stepsCountLat = static_cast<size_t>((m_rightTop.m_lat - m_leftBottom.m_lat) / step);
}

void MarchingSquares::GenerateIsolines(
  std::vector<IsolinesList> & isolines,
  geometry::Altitude & minAltitude)
{
  geometry::Altitude minHeight, maxHeight;
  minHeight = maxHeight = m_altExtractor.GetAltitude(m_leftBottom);
  for (uint32_t i = 0; i < m_stepsCountLat; ++i)
  {
    for (uint32_t j = 0; j < m_stepsCountLon; ++j)
    {
      auto const pos = ms::LatLon(m_leftBottom.m_lat + m_step * i,
                                  m_leftBottom.m_lon + m_step * j);
      auto const h = m_altExtractor.GetAltitude(pos);
      if (h < minHeight)
        minHeight = h;
      if (h > maxHeight)
        maxHeight = h;
    }
  }

  if (minHeight > 0)
    minHeight = m_heightStep * ((minHeight + m_heightStep - 1) / m_heightStep);
  else
    minHeight = m_heightStep * (minHeight / m_heightStep);
  if (maxHeight > 0)
    maxHeight = m_heightStep * ((maxHeight + m_heightStep - 1) / m_heightStep);
  else
    maxHeight = m_heightStep * (maxHeight / m_heightStep);

  ASSERT(maxHeight >= minHeight, ());
  uint16_t const isolinesCount = static_cast<uint16_t>(maxHeight - minHeight) / m_heightStep;
  minAltitude = minHeight;
  isolines.resize(isolinesCount);
  IsolinesWriter writer(isolines);

  auto const startPos = ms::LatLon(
    m_leftBottom.m_lat - m_step,
    m_leftBottom.m_lon - m_step);
  for (size_t i = 0; i < m_stepsCountLat + 1; ++i)
  {
    for (size_t j = 0; j < m_stepsCountLon + 1; ++j)
    {
      auto const pos = ms::LatLon(startPos.m_lat + m_step * i, startPos.m_lon + m_step * j);
      Square square(pos, m_step, m_altExtractor);
      square.generateSegments(minHeight, m_heightStep, writer);
    }
  }
}
