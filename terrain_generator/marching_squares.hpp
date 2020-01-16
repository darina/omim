#pragma once

#include "altitude_extractor.hpp"

#include <list>
#include <vector>

using Isoline = std::list<ms::LatLon>;
using IsolinesList = std::list<Isoline>;

struct ActiveIsoline
{
  explicit ActiveIsoline(Isoline && isoline) : m_isoline(std::move(isoline)) {}

  Isoline m_isoline;
  bool m_active = true;
};
using ActiveIsolinesList = std::list<ActiveIsoline>;
using ActiveIsolineIter = ActiveIsolinesList::iterator;

class IsolinesWriter
{
public:
  explicit IsolinesWriter(size_t isolineLevelsCount);

  void AddSegment(size_t levelInd, ms::LatLon const & beginPos, ms::LatLon const & endPos);
  void BeginLine();
  void EndLine(bool finalLine);

  void GetIsolines(std::vector<IsolinesList> & isolines);

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
  Square(ms::LatLon const & leftBottom, double size,
    geometry::Altitude firstAltitude, uint16_t altitudeStep,
    generator::AltitudeExtractor & altExtractor);

  void GenerateSegments(IsolinesWriter & writer);

private:
  enum class Rib
  {
    None,
    Left,
    Top,
    Right,
    Bottom,
    Unclear,
  };

  geometry::Altitude CorrectAltitude(geometry::Altitude alt) const;

  void WriteSegments(geometry::Altitude alt, uint16_t ind, IsolinesWriter & writer);
  ms::LatLon InterpolatePoint(Rib rib, geometry::Altitude alt);

  geometry::Altitude m_firstAltitude;
  uint16_t m_altitudeStep;

  double m_left;
  double m_right;
  double m_bottom;
  double m_top;

  geometry::Altitude m_altLB;
  geometry::Altitude m_altLT;
  geometry::Altitude m_altRT;
  geometry::Altitude m_altRB;
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
  uint16_t const m_altitudeStep;
  generator::AltitudeExtractor & m_altExtractor;

  size_t m_stepsCountLon;
  size_t m_stepsCountLat;
};
