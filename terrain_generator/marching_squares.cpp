#include "marching_squares.hpp"

#include "geometry/rect2d.hpp"

IsolinesWriter::IsolinesWriter(size_t isolineLevelsCount)
  : m_isolineLevelsCount(isolineLevelsCount)
{
  m_finalizedIsolines.resize(m_isolineLevelsCount);
  m_activeIsolines.resize(m_isolineLevelsCount);
}

void IsolinesWriter::addSegment(size_t levelInd, ms::LatLon const & beginPos, ms::LatLon const & endPos)
{
  if (beginPos.EqualDxDy(endPos, EPS))
    return;

  CHECK_LESS(levelInd, m_isolineLevelsCount, ());

  auto lineIterBefore = findLineEndsWith(levelInd, beginPos);
  auto lineIterAfter = findLineStartsWith(levelInd, endPos);
  bool connectStart = lineIterBefore != m_activeIsolines[levelInd].end();
  bool connectEnd = lineIterAfter != m_activeIsolines[levelInd].end();

  if (connectStart && connectEnd && lineIterBefore != lineIterAfter)
  {
    lineIterBefore->m_isoline.splice(lineIterBefore->m_isoline.end(), lineIterAfter->m_isoline);
    lineIterBefore->m_active = true;
    m_activeIsolines[levelInd].erase(lineIterAfter);
  }
  else if (connectStart)
  {
    lineIterBefore->m_isoline.push_back(endPos);
    lineIterBefore->m_active = true;
  }
  else if (connectEnd)
  {
    lineIterAfter->m_isoline.push_front(beginPos);
    lineIterBefore->m_active = true;
  }
  else
  {
    Isoline line = { beginPos, endPos };
    m_activeIsolines[levelInd].emplace_back(std::move(line));
  }
}

void IsolinesWriter::beginLine()
{
  for (auto & isolinesList : m_activeIsolines)
  {
    for (auto & activeIsoline : isolinesList)
      activeIsoline.m_active = false;
  }
}

void IsolinesWriter::endLine(bool finalLine)
{
  for (size_t levelInd = 0; levelInd < m_activeIsolines.size(); ++levelInd)
  {
    auto & isolines = m_activeIsolines[levelInd];
    auto it = isolines.begin();
    while (it != isolines.end())
    {
      if (!it->m_active || finalLine)
      {
        m_finalizedIsolines[levelInd].push_back(std::move(it->m_isoline));
        it = isolines.erase(it);
      }
      else
      {
        ++it;
      }
    }
  }
}

void IsolinesWriter::getIsolines(std::vector<IsolinesList> & isolines)
{
  m_finalizedIsolines.swap(isolines);
}

ActiveIsolineIter IsolinesWriter::findLineStartsWith(size_t levelInd, ms::LatLon const & pos)
{
  auto & isolines = m_activeIsolines[levelInd];
  for (auto it = isolines.begin(); it != isolines.end(); ++it)
  {
    if (it->m_isoline.front().EqualDxDy(pos, EPS))
      return it;
  }
  return isolines.end();
}

ActiveIsolineIter IsolinesWriter::findLineEndsWith(size_t levelInd, ms::LatLon const & pos)
{
  auto & isolines = m_activeIsolines[levelInd];
  for (auto it = isolines.begin(); it != isolines.end(); ++it)
  {
    if (it->m_isoline.back().EqualDxDy(pos, EPS))
      return it;
  }
  return isolines.end();
}

Square::Square(ms::LatLon const & leftBottom,
               double size, generator::AltitudeExtractor & altExtractor)
  : m_left(leftBottom.m_lon)
  , m_right(leftBottom.m_lon + size)
  , m_bottom(leftBottom.m_lat)
  , m_top(leftBottom.m_lat + size)
{
  if (leftBottom.EqualDxDy(ms::LatLon(59.9511041, 56.9386135), 1e-4))
  {
    LOG(LWARNING, ("Our magic rect!"));
    int i = 0;
    i++;
    LOG(LWARNING, ("I:", i));
  }

  //45.863077, 6.8863286
  //45.8630783, 6.8864105
  //45.8632623, 6.8855211
  //45.8612715, 6.8880459
  m_hLB = altExtractor.GetAltitude(leftBottom);
  m_hLT = altExtractor.GetAltitude(ms::LatLon(m_top, m_left));
  m_hRT = altExtractor.GetAltitude(ms::LatLon(m_top, m_right));
  m_hRB = altExtractor.GetAltitude(ms::LatLon(m_bottom, m_right));
}

void Square::generateSegments(geometry::Altitude firstAltitude, uint16_t altStep, IsolinesWriter & writer)
{
  if (m_hLB % altStep == 0)
    m_hLB += 1;
  if (m_hLT % altStep == 0)
    m_hLT += 1;
  if (m_hRT % altStep == 0)
    m_hRT += 1;
  if (m_hRB % altStep == 0)
    m_hRB += 1;

  geometry::Altitude minAlt = std::min(m_hLB, std::min(m_hLT, std::min(m_hRT, m_hRB)));
  geometry::Altitude maxAlt = std::max(m_hLB, std::max(m_hLT, std::max(m_hRT, m_hRB)));

  if (minAlt > 0)
    minAlt = altStep * ((minAlt + altStep - 1) / altStep);
  else
    minAlt = altStep * (minAlt / altStep);
  if (maxAlt > 0)
    maxAlt = altStep * ((maxAlt + altStep) / altStep);
  else
    maxAlt = altStep * (maxAlt / altStep);

  CHECK_GREATER_OR_EQUAL(minAlt, firstAltitude, ());

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
  } pattern, pattern2;
  pattern.value = 0;
  pattern.pLB = m_hLB > alt;
  pattern.pLT = m_hLT > alt;
  pattern.pRT = m_hRT > alt;
  pattern.pRB = m_hRB > alt;

  pattern2.value = 0;
  pattern2.pLB = m_hLB < alt;
  pattern2.pLT = m_hLT < alt;
  pattern2.pRT = m_hRT < alt;
  pattern2.pRB = m_hRB < alt;

  //uint8_t pt = (m_hLB > alt ? 1u : 0u) | ((m_hLT > alt ? 1u : 0u) << 1)
  //  | ((m_hRT > alt ? 1u : 0u) << 2) | ((m_hRB > alt ? 1u : 0u) << 3);

  auto ribs = intersectedRibs[pattern.value];
  auto ribs2 = intersectedRibs[pattern2.value];

  if (ribs.first == NONE || (ribs.first == UNCLEAR && ribs2.first == NONE))
    return;

  if (ribs.first == UNCLEAR && ribs2.first != UNCLEAR)
    ribs = intersectedRibs[~pattern2.value & 0xF];

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
  double step, uint16_t heightStep, generator::AltitudeExtractor & altExtractor)
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
    maxHeight = m_heightStep * ((maxHeight + m_heightStep) / m_heightStep);
  else
    maxHeight = m_heightStep * (maxHeight / m_heightStep);

  ASSERT(maxHeight >= minHeight, ());
  uint16_t const isolinesCount = static_cast<uint16_t>(maxHeight - minHeight) / m_heightStep;
  minAltitude = minHeight;
  IsolinesWriter writer(isolinesCount);

  auto const startPos = ms::LatLon(m_leftBottom.m_lat, m_leftBottom.m_lon);
  for (size_t i = 0; i < m_stepsCountLat - 1; ++i)
  {
    writer.beginLine();
    for (size_t j = 0; j < m_stepsCountLon - 1; ++j)
    {
      auto const pos = ms::LatLon(startPos.m_lat + m_step * i, startPos.m_lon + m_step * j);
      Square square(pos, m_step, m_altExtractor);
      square.generateSegments(minHeight, m_heightStep, writer);

      //59.9479036, 56.9451127
      static m2::RectD limitRect(m2::PointD(59.92, 56.92), m2::PointD(59.96, 56.96));
      if (false)//limitRect.IsPointInside(m2::PointD(pos.m_lat, pos.m_lon)) && (i & 1) && (j & 1))
      {
        //LOG(LWARNING, ("!!!", i, j));
        writer.addSegment(0, ms::LatLon(square.m_bottom, square.m_left), ms::LatLon(square.m_top, square.m_left));
        writer.addSegment(0, ms::LatLon(square.m_top, square.m_left), ms::LatLon(square.m_top, square.m_right));
        writer.addSegment(0, ms::LatLon(square.m_top, square.m_right), ms::LatLon(square.m_bottom, square.m_right));
        writer.addSegment(0, ms::LatLon(square.m_bottom, square.m_right), ms::LatLon(square.m_bottom, square.m_left));
      }
    }
    auto const isLastLine = i == m_stepsCountLat - 2;
    writer.endLine(isLastLine);
  }

  writer.getIsolines(isolines);
}
