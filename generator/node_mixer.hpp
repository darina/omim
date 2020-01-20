#pragma once

#include "geometry/point2d.hpp"

#include "generator/osm_element.hpp"

#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace generator
{
void MixFakeNodes(std::istream & stream, std::function<void(OsmElement &)> processor);
void MixFakeLines(std::string const filePath, std::function<void(OsmElement &,
  std::vector<m2::PointD> const &)> processor);

inline void MixFakeNodes(std::string const filePath, std::function<void(OsmElement &)> processor)
{
  std::ifstream stream(filePath);
  MixFakeNodes(stream, processor);
}
}  // namespace generator
