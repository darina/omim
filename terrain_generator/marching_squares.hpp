#pragma once

#include "altitude_extractor.hpp"

#include <deque>
#include <vector>

using Isoline = std::list<ms::LatLon>;
using IsolinesList = std::list<Isoline>;
using IsolineIter = IsolinesList::iterator;

class IsolinesWriter
{
public:
  double static constexpr EPS = 1e-5;

  explicit IsolinesWriter(std::vector<IsolinesList> & isolines);

  void addSegment(size_t ind, ms::LatLon const & beginPos, ms::LatLon const & endPos);
  /*void addUnsolvedSegments(size_t ind,
                           ms::LatLon const & start1,
                           ms::LatLon const & end1,
                           ms::LatLon const & start2,
                           ms::LatLon const & end2);*/

  std::vector<IsolinesList> & getIsolines() const { return m_isolines; }

private:
  using UnsolvedSegment = ms::LatLon[4];
  using UnsolvedList = std::list<UnsolvedSegment>;

  IsolineIter findLineStartsWith(size_t ind, ms::LatLon const & pos);
  IsolineIter findLineEndsWith(size_t ind, ms::LatLon const & pos);

  std::vector<IsolinesList> & m_isolines;

  /*void resolveSegments(size_t ind, Isoline & line, bool start, bool end);
  uint8_t checkUnsolvedSegment(UnsolvedSegment const & segment, ms::LatLon const & pos);
  std::vector<UnsolvedList> m_unsolvedSegments;*/
};

class Square
{
public:
  Square(ms::LatLon const & leftBottom, double size, generator::AltitudeExtractor & altExtractor);

  void generateSegments(geometry::Altitude firstAltitude, uint16_t altStep, IsolinesWriter & writer);

private:

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
