#include "marching_squares.hpp"

#include "geometry/rect2d.hpp"

double constexpr kEps = 1e-7;

IsolinesWriter::IsolinesWriter(size_t isolineLevelsCount)
  : m_isolineLevelsCount(isolineLevelsCount)
{
  m_finalizedIsolines.resize(m_isolineLevelsCount);
  m_activeIsolines.resize(m_isolineLevelsCount);
}

void IsolinesWriter::AddSegment(size_t levelInd, ms::LatLon const & beginPos, ms::LatLon const & endPos)
{
  if (beginPos.EqualDxDy(endPos, kEps))
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

void IsolinesWriter::BeginLine()
{
  for (auto & isolinesList : m_activeIsolines)
  {
    for (auto & activeIsoline : isolinesList)
      activeIsoline.m_active = false;
  }
}

void IsolinesWriter::EndLine(bool finalLine)
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

void IsolinesWriter::GetIsolines(std::vector<IsolinesList> & isolines)
{
  m_finalizedIsolines.swap(isolines);
}

ActiveIsolineIter IsolinesWriter::findLineStartsWith(size_t levelInd, ms::LatLon const & pos)
{
  auto & isolines = m_activeIsolines[levelInd];
  for (auto it = isolines.begin(); it != isolines.end(); ++it)
  {
    if (it->m_isoline.front().EqualDxDy(pos, kEps))
      return it;
  }
  return isolines.end();
}

ActiveIsolineIter IsolinesWriter::findLineEndsWith(size_t levelInd, ms::LatLon const & pos)
{
  auto & isolines = m_activeIsolines[levelInd];
  for (auto it = isolines.begin(); it != isolines.end(); ++it)
  {
    if (it->m_isoline.back().EqualDxDy(pos, kEps))
      return it;
  }
  return isolines.end();
}

Square::Square(ms::LatLon const & leftBottom, double size,
  geometry::Altitude firstAltitude, uint16_t altitudeStep,
  generator::AltitudeExtractor & altExtractor)
  : m_firstAltitude(firstAltitude)
  , m_altitudeStep(altitudeStep)
  , m_left(leftBottom.m_lon)
  , m_right(leftBottom.m_lon + size)
  , m_bottom(leftBottom.m_lat)
  , m_top(leftBottom.m_lat + size)
{
  m_altLB = CorrectAltitude(altExtractor.GetAltitude(leftBottom));
  m_altLT = CorrectAltitude(altExtractor.GetAltitude(ms::LatLon(m_top, m_left)));
  m_altRT = CorrectAltitude(altExtractor.GetAltitude(ms::LatLon(m_top, m_right)));
  m_altRB = CorrectAltitude(altExtractor.GetAltitude(ms::LatLon(m_bottom, m_right)));
}

geometry::Altitude Square::CorrectAltitude(geometry::Altitude alt) const
{
  if (alt != geometry::kInvalidAltitude && (alt % m_altitudeStep == 0))
    return alt + 1;
  return alt;
}

void Square::GenerateSegments(IsolinesWriter & writer)
{
  geometry::Altitude minAlt = std::min(m_altLB, std::min(m_altLT, std::min(m_altRT, m_altRB)));
  geometry::Altitude maxAlt = std::max(m_altLB, std::max(m_altLT, std::max(m_altRT, m_altRB)));

  if (minAlt > 0)
    minAlt = m_altitudeStep * ((minAlt + m_altitudeStep - 1) / m_altitudeStep);
  else
    minAlt = m_altitudeStep * (minAlt / m_altitudeStep);
  if (maxAlt > 0)
    maxAlt = m_altitudeStep * ((maxAlt + m_altitudeStep) / m_altitudeStep);
  else
    maxAlt = m_altitudeStep * (maxAlt / m_altitudeStep);

  CHECK_GREATER_OR_EQUAL(minAlt, m_firstAltitude, ());

  for (auto alt = minAlt; alt < maxAlt; alt += m_altitudeStep)
    WriteSegments(alt, (alt - m_firstAltitude) / m_altitudeStep, writer);
}

void Square::WriteSegments(geometry::Altitude alt, uint16_t ind, IsolinesWriter & writer)
{
  // Segment is a vector directed so that higher altitudes is on the right.
  std::pair<Rib, Rib> intersectedRibs[] =
    {
      {Rib::None, Rib::None},       // 0000
      {Rib::Left, Rib::Bottom},     // 0001
      {Rib::Top, Rib::Left},        // 0010
      {Rib::Top, Rib::Bottom},      // 0011
      {Rib::Right, Rib::Top},       // 0100
      {Rib::Unclear, Rib::Unclear}, // 0101
      {Rib::Right, Rib::Left},      // 0110
      {Rib::Right, Rib::Bottom},    // 0111
      {Rib::Bottom, Rib::Right},    // 1000
      {Rib::Left, Rib::Right},      // 1001
      {Rib::Unclear, Rib::Unclear}, // 1010
      {Rib::Top, Rib::Right},       // 1011
      {Rib::Bottom, Rib::Top},      // 1100
      {Rib::Left, Rib::Top},        // 1101
      {Rib::Bottom, Rib::Left},     // 1110
      {Rib::None, Rib::None},       // 1111
    };

  uint8_t const pattern = (m_altLB > alt ? 1u : 0u) | ((m_altLT > alt ? 1u : 0u) << 1) |
    ((m_altRT > alt ? 1u : 0u) << 2) | ((m_altRB > alt ? 1u : 0u) << 3);

  auto ribs = intersectedRibs[pattern];

  if (ribs.first == Rib::None)
    return;

  if (ribs.first != Rib::Unclear)
  {
    writer.AddSegment(ind, InterpolatePoint(ribs.first, alt), InterpolatePoint(ribs.second, alt));
  }
  else
  {
    auto const leftPos = InterpolatePoint(Rib::Left, alt);
    auto const rightPos = InterpolatePoint(Rib::Right, alt);
    auto const bottomPos = InterpolatePoint(Rib::Bottom, alt);
    auto const topPos = InterpolatePoint(Rib::Top, alt);

    uint16_t middleAlt = (m_altLB + m_altLT + m_altRT + m_altRB) / 4;
    if (middleAlt > alt)
    {
      if (m_altLB > alt)
      {
        writer.AddSegment(ind, leftPos, topPos);
        writer.AddSegment(ind, rightPos, bottomPos);
      }
      else
      {
        writer.AddSegment(ind, bottomPos, leftPos);
        writer.AddSegment(ind, topPos, rightPos);
      }
    }
    else
    {
      if (m_altLB > alt)
      {
        writer.AddSegment(ind, leftPos, bottomPos);
        writer.AddSegment(ind, rightPos, topPos);
      }
      else
      {
        writer.AddSegment(ind, topPos, leftPos);
        writer.AddSegment(ind, bottomPos, rightPos);
      }
    }
  }
}

ms::LatLon Square::InterpolatePoint(Square::Rib rib, geometry::Altitude alt)
{
  double alt1;
  double alt2;
  double lat;
  double lon;

  switch (rib)
  {
  case Rib::Left:
    alt1 = static_cast<double>(m_altLB);
    alt2 = static_cast<double>(m_altLT);
    lon = m_left;
    break;
  case Rib::Right:
    alt1 = static_cast<double>(m_altRB);
    alt2 = static_cast<double>(m_altRT);
    lon = m_right;
    break;
  case Rib::Top:
    alt1 = static_cast<double>(m_altLT);
    alt2 = static_cast<double>(m_altRT);
    lat = m_top;
    break;
  case Rib::Bottom:
    alt1 = static_cast<double>(m_altLB);
    alt2 = static_cast<double>(m_altRB);
    lat = m_bottom;
    break;
  default:
    UNREACHABLE();
  }

  CHECK_NOT_EQUAL(alt, alt2, ());
  double const coeff = (alt1 - alt) / (alt - alt2);

  switch (rib)
  {
  case Rib::Left:
  case Rib::Right:
    lat = (m_bottom + m_top * coeff) / (1 + coeff);
    break;
  case Rib::Bottom:
  case Rib::Top:
    lon = (m_left + m_right * coeff) / (1 + coeff);
    break;
  default:
    UNREACHABLE();
  }

  return {lat, lon};
}

MarchingSquares::MarchingSquares(ms::LatLon const & leftBottom, ms::LatLon const & rightTop,
  double step, uint16_t heightStep, generator::AltitudeExtractor & altExtractor)
  : m_leftBottom(leftBottom)
  , m_rightTop(rightTop)
  , m_step(step)
  , m_altitudeStep(heightStep)
  , m_altExtractor(altExtractor)
{
  CHECK_GREATER(m_rightTop.m_lon, m_leftBottom.m_lon, ());
  CHECK_GREATER(m_rightTop.m_lat, m_leftBottom.m_lat, ());

  m_stepsCountLon = static_cast<size_t>((m_rightTop.m_lon - m_leftBottom.m_lon) / step);
  m_stepsCountLat = static_cast<size_t>((m_rightTop.m_lat - m_leftBottom.m_lat) / step);

  CHECK_GREATER(m_stepsCountLon, 0, ());
  CHECK_GREATER(m_stepsCountLat, 0, ());
}

void MarchingSquares::GenerateIsolines(
  std::vector<IsolinesList> & isolines,
  geometry::Altitude & minAltitude)
{
  geometry::Altitude maxAltitude;
  minAltitude = maxAltitude = m_altExtractor.GetAltitude(m_leftBottom);
  for (uint32_t i = 0; i < m_stepsCountLat; ++i)
  {
    for (uint32_t j = 0; j < m_stepsCountLon; ++j)
    {
      auto const pos = ms::LatLon(m_leftBottom.m_lat + m_step * i,
                                  m_leftBottom.m_lon + m_step * j);
      auto const altitude = m_altExtractor.GetAltitude(pos);
      if (i >= 649 && i <= 652 && j >= 398 && j <=401)
      {
       LOG(LWARNING, (i, j, pos, altitude));
      }
      if (altitude < minAltitude)
        minAltitude = altitude;
      if (altitude > maxAltitude)
        maxAltitude = altitude;
    }
  }

  if (minAltitude > 0)
    minAltitude = m_altitudeStep * ((minAltitude + m_altitudeStep - 1) / m_altitudeStep);
  else
    minAltitude = m_altitudeStep * (minAltitude / m_altitudeStep);

  if (maxAltitude > 0)
    maxAltitude = m_altitudeStep * ((maxAltitude + m_altitudeStep) / m_altitudeStep);
  else
    maxAltitude = m_altitudeStep * (maxAltitude / m_altitudeStep);

  ASSERT(maxAltitude >= minAltitude, ());
  uint16_t const isolinesCount = static_cast<uint16_t>(maxAltitude - minAltitude) / m_altitudeStep;
  IsolinesWriter writer(isolinesCount);

  for (size_t i = 0; i < m_stepsCountLat - 1; ++i)
  {
    writer.BeginLine();
    for (size_t j = 0; j < m_stepsCountLon - 1; ++j)
    {
      auto const pos = ms::LatLon(m_leftBottom.m_lat + m_step * i, m_leftBottom.m_lon + m_step * j);
      Square square(pos, m_step, minAltitude, m_altitudeStep, m_altExtractor);
      square.GenerateSegments(writer);

      /*
      static m2::RectD limitRect(m2::PointD(59.92, 56.92), m2::PointD(59.96, 56.96));
      if (limitRect.IsPointInside(m2::PointD(pos.m_lat, pos.m_lon)) && (i & 1) && (j & 1))
      {
        writer.addSegment(0, ms::LatLon(square.m_bottom, square.m_left), ms::LatLon(square.m_top, square.m_left));
        writer.addSegment(0, ms::LatLon(square.m_top, square.m_left), ms::LatLon(square.m_top, square.m_right));
        writer.addSegment(0, ms::LatLon(square.m_top, square.m_right), ms::LatLon(square.m_bottom, square.m_right));
        writer.AddSegment(0, ms::LatLon(square.m_bottom, square.m_right), ms::LatLon(square.m_bottom, square.m_left));
      }
      */
    }
    auto const isLastLine = i == m_stepsCountLat - 2;
    writer.EndLine(isLastLine);
  }

  writer.GetIsolines(isolines);
}
