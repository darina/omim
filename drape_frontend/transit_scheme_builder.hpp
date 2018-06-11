#pragma once

#include "drape/batcher.hpp"
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

extern std::vector<float> const kTransitLinesWidthInPixel;

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
  routing::transit::LineId m_lineId;
  explicit TransitRenderData(dp::GLState const & state) : BaseTransitRenderData(state) {}
};

struct TransitMarkersRenderData : public BaseTransitRenderData
{
  explicit TransitMarkersRenderData(dp::GLState const & state) : BaseTransitRenderData(state) {}
};

struct TransitTextRenderData : public BaseTransitRenderData
{
  explicit TransitTextRenderData(dp::GLState const & state) : BaseTransitRenderData(state) {}
};

using TTransitRenderData = std::vector<TransitRenderData>;

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
  std::vector<routing::transit::LineId> m_forwardLines;
  std::vector<routing::transit::LineId> m_backwardLines;
  std::vector<m2::PointD> m_polyline;
  bool m_isForward = true;
};

struct ShapeInfo
{
  m2::PointD m_direction;
  size_t m_linesCount;
};

struct StopParams
{
  m2::PointD m_pivot;
  std::map<routing::transit::ShapeId, ShapeInfo> m_shapes;
  std::set<routing::transit::LineId> m_lines;
  std::set<std::string> m_names;
  FeatureID m_featureId;
};

class TransitSchemeBuilder
{
public:
  using TFlushRenderDataFn = function<void (TransitRenderData && renderData)>;
  using TFlushMarkersRenderDataFn = function<void (TransitMarkersRenderData && renderData)>;
  using TFlushTextRenderDataFn = function<void (TransitTextRenderData && renderData)>;

  explicit TransitSchemeBuilder(TFlushRenderDataFn const & flushFn,
                                TFlushMarkersRenderDataFn const & flushMarkersFn,
                                TFlushTextRenderDataFn const & flushTextFn)
    : m_flushRenderDataFn(flushFn)
    , m_flushMarkersRenderDataFn(flushMarkersFn)
    , m_flushTextRenderDataFn(flushTextFn)
  {}

  void SetVisibleMwms(std::vector<MwmSet::MwmId> const & visibleMwms);
  void UpdateScheme(TransitDisplayInfos const & transitDisplayInfos);
  void BuildScheme(ref_ptr<dp::TextureManager> textures);

private:
  struct MwmSchemeData
  {
    m2::PointD m_pivot;

    std::map<routing::transit::LineId, LineParams> m_lines;
    std::map<routing::transit::ShapeId, ShapeParams> m_shapes;
    std::map<routing::transit::StopId, StopParams> m_stops;
    std::map<routing::transit::TransferId, StopParams> m_transfers;
  };

  //void GenerateMarker(m2::PointD const & pivot, float depth, float innerDepth, float outerRadius, float innerRadius,
  //                    dp::Color const & outerColor, dp::Color const & innerColor, TGeometryBuffer & geometry);
  void GenerateTitles(StopParams const & params, m2::PointD const & pivot, vector<m2::PointF> const & markerSizes,
                      ref_ptr<dp::TextureManager> textures, dp::Batcher & batcher);

  void AddShape(TransitDisplayInfo const & transitDisplayInfo, routing::transit::StopId stop1Id,
                routing::transit::StopId stop2Id, routing::transit::LineId lineId, MwmSchemeData & scheme);

  using TransitSchemes = std::map<MwmSet::MwmId, MwmSchemeData>;
  TransitSchemes m_schemes;
  std::vector<MwmSet::MwmId> m_visibleMwms;
  TFlushRenderDataFn m_flushRenderDataFn;
  TFlushMarkersRenderDataFn m_flushMarkersRenderDataFn;
  TFlushTextRenderDataFn m_flushTextRenderDataFn;
};
}  // namespace df
