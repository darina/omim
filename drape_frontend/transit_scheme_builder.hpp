#pragma once

#include "drape/glstate.hpp"
#include "drape/render_bucket.hpp"
#include "drape/texture_manager.hpp"

#include "transit/transit_display_info.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace df
{
int constexpr kTransitSchemeMinZoomLevel = 10;

struct BaseTransitRenderData
{
  dp::GLState m_state;
  std::vector<drape_ptr<dp::RenderBucket>> m_buckets;
  MwmSet::MwmId m_mwmId;
  m2::PointD m_pivot;

  explicit BaseTransitRenderData(dp::GLState const & state) : m_state(state) {}
  BaseTransitRenderData(BaseTransitRenderData &&) = default;
  BaseTransitRenderData & operator=(BaseTransitRenderData &&) = default;
};

struct TransitRenderData : public BaseTransitRenderData
{
  routing::transit::ShapeId m_shapeId;

  explicit TransitRenderData(dp::GLState const & state) : BaseTransitRenderData(state) {}
};

struct TransitMarkersRenderData : public BaseTransitRenderData
{
  explicit TransitMarkersRenderData(dp::GLState const & state) : BaseTransitRenderData(state) {}
};

using TTransitRenderData = std::vector<TransitRenderData>;

class TransitSchemeBuilder
{
public:
  using TFlushRenderDataFn = function<void (TransitRenderData && renderData)>;
  using TFlushMarkersRenderDataFn = function<void (TransitMarkersRenderData && renderData)>;

  explicit TransitSchemeBuilder(TFlushRenderDataFn const & flushFn,
                                TFlushMarkersRenderDataFn const & flushMarkersFn)
    : m_flushRenderDataFn(flushFn)
    , m_flushMarkersRenderDataFn(flushMarkersFn)
  {}

  void SetVisibleMwms(std::vector<MwmSet::MwmId> const & visibleMwms);
  void UpdateScheme(TransitDisplayInfos const & transitDisplayInfos);
  void BuildScheme();

private:
  struct LineParams
  {
    LineParams() = default;
    LineParams(string const & color, float depth)
        : m_color(color), m_depth(depth)
    {}
    std::string m_color;
    float m_depth;
  };

  struct ShapeParams
  {
    std::set<routing::transit::LineId> m_lines;
    std::vector<m2::PointD> m_polyline;
  };

  struct StopParams
  {
    m2::PointD m_pivot;
    std::set<routing::transit::LineId> m_lines;
    std::set<std::string> m_names;
  };

  struct MwmSchemeData
  {
    std::map<routing::transit::LineId, LineParams> m_lines;
    std::map<routing::transit::ShapeId, ShapeParams> m_shapes;
    std::map<routing::transit::StopId, StopParams> m_stops;
    std::map<routing::transit::TransferId, StopParams> m_transfers;
  };
  using TransitSchemes = std::map<MwmSet::MwmId, MwmSchemeData>;
  TransitSchemes m_schemes;
  std::vector<MwmSet::MwmId> m_visibleMwms;
  TFlushRenderDataFn m_flushRenderDataFn;
  TFlushMarkersRenderDataFn m_flushMarkersRenderDataFn;
};
}  // namespace df
