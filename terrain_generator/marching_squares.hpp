#pragma once

#include "altitude_extractor.hpp"

#include <deque>
#include <vector>

using Isoline = std::list<ms::LatLon>;
using IsolinesList = std::list<Isoline>;

struct ActiveIsoline
{
  ActiveIsoline(Isoline && isoline) : m_isoline(std::move(isoline)) {}

  Isoline m_isoline;
  bool m_active = true;
};
using ActiveIsolinesList = std::list<ActiveIsoline>;
using ActiveIsolineIter = ActiveIsolinesList::iterator;

class IsolinesWriter
{
public:
  double static constexpr EPS = 1e-7;

  explicit IsolinesWriter(size_t isolineLevelsCount);

  void addSegment(size_t levelInd, ms::LatLon const & beginPos, ms::LatLon const & endPos);
  void beginLine();
  void endLine(bool finalLine);

  void getIsolines(std::vector<IsolinesList> & isolines);

private:
  ActiveIsolineIter findLineStartsWith(size_t levelInd, ms::LatLon const & pos);
  ActiveIsolineIter findLineEndsWith(size_t levelInd, ms::LatLon const & pos);

  size_t const m_isolineLevelsCount;

  std::vector<IsolinesList> m_finalizedIsolines;
  std::vector<ActiveIsolinesList> m_activeIsolines;
};

class Square
{
public:
  Square(ms::LatLon const & leftBottom, double size, generator::AltitudeExtractor & altExtractor);

  void generateSegments(geometry::Altitude firstAltitude, uint16_t altStep, IsolinesWriter & writer);

//private:
public:

  enum Rib
  {
    NONE,
    LEFT,
    TOP,
    RIGHT,
    BOTTOM,
    UNCLEAR,
  };

  void writeSegments(geometry::Altitude alt, uint16_t ind, IsolinesWriter & writer);
  ms::LatLon interpolatePoint(Rib rib, geometry::Altitude alt);

  double m_left;
  double m_right;
  double m_bottom;
  double m_top;

  geometry::Altitude m_hLB;
  geometry::Altitude m_hLT;
  geometry::Altitude m_hRT;
  geometry::Altitude m_hRB;
};

class MarchingSquares
{
public:
  MarchingSquares(ms::LatLon const & leftBottom, ms::LatLon const & rightTop,
         double step, uint16_t heightStep, generator::AltitudeExtractor & altExtractor);

  void GenerateIsolines(std::vector<IsolinesList> & isolines,
                        geometry::Altitude & minAltitude);

private:
  ms::LatLon const m_leftBottom;
  ms::LatLon const m_rightTop;
  double const m_step;
  uint16_t const m_heightStep;
  generator::AltitudeExtractor & m_altExtractor;

  size_t m_stepsCountLon;
  size_t m_stepsCountLat;
};
