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

struct TransitRenderData
{
  dp::GLState m_state;
  uint32_t m_recacheId;
  MwmSet::MwmId m_mwmId;
  m2::PointD m_pivot;
  std::vector<drape_ptr<dp::RenderBucket>> m_buckets;

  TransitRenderData(dp::GLState const & state, uint32_t recacheId, MwmSet::MwmId const & mwmId, m2::PointD const pivot)
    : m_state(state)
    , m_recacheId(recacheId)
    , m_mwmId(mwmId)
    , m_pivot(pivot)
  {}
  TransitRenderData(TransitRenderData &&) = default;
  TransitRenderData & operator=(TransitRenderData &&) = default;
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
};

struct ShapeInfo
{
  m2::PointD m_direction;
  size_t m_linesCount;
};

struct StopInfo
{
  StopInfo() = default;
  StopInfo(std::string const & name, FeatureID const & featureId)
    : m_name(name)
    , m_featureId(featureId)
  {}

  std::string m_name;
  FeatureID m_featureId;
};

struct StopNodeParams
{
  bool m_isTransfer = false;
  m2::PointD m_pivot;
  std::map<routing::transit::ShapeId, ShapeInfo> m_shapesInfo;
  std::map<routing::transit::LineId, StopInfo> m_stopsInfo;
};

class TransitSchemeBuilder
{
public:
  using TFlushRenderDataFn = function<void (TransitRenderData && renderData)>;

  TransitSchemeBuilder(TFlushRenderDataFn const & flushFn,
                       TFlushRenderDataFn const & flushMarkersFn,
                       TFlushRenderDataFn const & flushTextFn)
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
    std::map<routing::transit::StopId, StopNodeParams> m_stops;
    std::map<routing::transit::TransferId, StopNodeParams> m_transfers;
  };

  void CollectStops(TransitDisplayInfo const & transitDisplayInfo,
                    MwmSet::MwmId const & mwmId, MwmSchemeData & scheme);
  void CollectLines(TransitDisplayInfo const & transitDisplayInfo, MwmSchemeData & scheme);
  void CollectShapes(TransitDisplayInfo const & transitDisplayInfo, MwmSchemeData & scheme);
  void PrepareScheme(MwmSchemeData & scheme);

  void GenerateShapes(MwmSet::MwmId const & mwmId);
  void GenerateStops(MwmSet::MwmId const & mwmId, ref_ptr<dp::TextureManager> textures);

  void GenerateLine(std::vector<m2::PointD> const & path, m2::PointD const & pivot, dp::Color const & colorConst,
                    float lineOffset, float lineHalfWidth, float depth, dp::Batcher & batcher);

  void GenerateMarker(m2::PointD const & pt, m2::PointD widthDir, float linesCountWidth, float linesCountHeight,
                      float scaleWidth, float scaleHeight, float depth, dp::Color const & color, dp::Batcher & batcher);

  void GenerateTransfer(StopNodeParams const & params, m2::PointD const & pivot, dp::Batcher & batcher);

  void GenerateStop(StopNodeParams const & params, m2::PointD const & pivot,
                    std::map<routing::transit::LineId, LineParams> const & lines, dp::Batcher & batcher);

  void GenerateTitles(StopNodeParams const & params, m2::PointD const & pivot, vector<m2::PointF> const & markerSizes,
                      ref_ptr<dp::TextureManager> textures, dp::Batcher & batcher);

  void AddShape(TransitDisplayInfo const & transitDisplayInfo, routing::transit::StopId stop1Id,
                routing::transit::StopId stop2Id, routing::transit::LineId lineId, MwmSchemeData & scheme);

  using TransitSchemes = std::map<MwmSet::MwmId, MwmSchemeData>;
  TransitSchemes m_schemes;
  std::vector<MwmSet::MwmId> m_visibleMwms;
  TFlushRenderDataFn m_flushRenderDataFn;
  TFlushRenderDataFn m_flushMarkersRenderDataFn;
  TFlushRenderDataFn m_flushTextRenderDataFn;

  uint32_t m_recacheId = 0;
};
}  // namespace df
