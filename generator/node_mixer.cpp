#include "generator/node_mixer.hpp"

#include "base/logging.hpp"
#include "base/string_utils.hpp"

#include "coding/point_coding.hpp"

using namespace std;

namespace generator
{
void MixFakeNodes(istream & stream, function<void(OsmElement &)> processor)
{
  if (stream.fail())
    return;

  // Max node id on 12.02.2018 times hundred — good enough until ~2030.
  uint64_t constexpr baseNodeId = 5396734321 * 100;
  uint8_t constexpr kCFLat = 1;
  uint8_t constexpr kCFLon = 2;
  uint8_t constexpr kCFTags = 4;
  uint8_t constexpr kCFAll = kCFLat | kCFLon | kCFTags;
  uint64_t count = 0;
  uint8_t completionFlag = 0;
  OsmElement p;
  p.m_id = baseNodeId;
  p.m_type = OsmElement::EntityType::Node;

  string line;
  while (getline(stream, line))
  {
    if (line.empty())
    {
      if (completionFlag == kCFAll)
      {
        processor(p);
        count++;
        p.Clear();
        p.m_id = baseNodeId + count;
        p.m_type = OsmElement::EntityType::Node;
        completionFlag = 0;
      }
      continue;
    }

    auto const eqPos = line.find('=');
    if (eqPos != string::npos)
    {
      string key = line.substr(0, eqPos);
      string value = line.substr(eqPos + 1);
      strings::Trim(key);
      strings::Trim(value);

      if (key == "lat")
      {
        if (strings::to_double(value, p.m_lat))
          completionFlag |= kCFLat;
      }
      else if (key == "lon")
      {
        if (strings::to_double(value, p.m_lon))
          completionFlag |= kCFLon;
      }
      else
      {
        p.AddTag(key, value);
        completionFlag |= kCFTags;
      }
    }
  }

  if (completionFlag == kCFAll)
  {
    processor(p);
    count++;
  }

  LOG(LINFO, ("Added", count, "fake nodes."));
}

void MixFakeLines(std::istream & stream, std::function<void(OsmElement &, std::vector<m2::PointD> const &)> processor)
{
  if (stream.fail())
    return;

  // Max node id on 12.02.2018 times hundred — good enough until ~2030.
  uint64_t constexpr baseNodeId = 5396734321 * 100;

  uint64_t count = 0;
  OsmElement p;
  p.m_id = baseNodeId;
  p.m_type = OsmElement::EntityType::Way;

  string line;
  while (getline(stream, line))
  {
    std::istringstream ss(line);
    int height;
    size_t pointsCount;
    ss >> height >> pointsCount;
    int kPointCoordBits = 30;
    std::vector<m2::PointD> points;
    points.reserve(pointsCount);
    for (size_t i = 0; i < pointsCount; ++i)
    {
      double x;
      double y;
      ss >> x >> y;
      points.emplace_back(x, y);
      p.AddNd(PointToInt64Obsolete(x, y, kPointCoordBits));
    }
    string isolineClass;
    if (height % 200 == 0)
      isolineClass = "1";
    else if (height % 100 == 0)
      isolineClass = "2";
    else if (height % 50 == 0)
      isolineClass = "3";
    else if (height % 10 == 0)
      isolineClass = "4";
    else
      LOG(LWARNING, ("Invalid height", height));

    p.AddTag("isoline", isolineClass);
    p.AddTag("name", strings::to_string(height));

    processor(p, points);
    count++;
    p.Clear();
    p.m_id = baseNodeId + count;
    p.m_type = OsmElement::EntityType::Way;
  }

  LOG(LINFO, ("Added", count, "fake lines."));
}
}  // namespace generator
