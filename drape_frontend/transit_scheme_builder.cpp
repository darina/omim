#include "transit_scheme_builder.hpp"

#include "drape_frontend/color_constants.hpp"
#include "drape_frontend/colored_symbol_shape.hpp"
#include "drape_frontend/line_shape_helper.hpp"
#include "drape_frontend/map_shape.hpp"
#include "drape_frontend/render_state.hpp"
#include "drape_frontend/shader_def.hpp"
#include "drape_frontend/shape_view_params.hpp"
#include "drape_frontend/text_shape.hpp"
#include "drape_frontend/visual_params.hpp"

#include "drape/batcher.hpp"
#include "drape/glsl_func.hpp"
#include "drape/glsl_types.hpp"
#include "drape/render_bucket.hpp"
#include "drape/utils/vertex_decl.hpp"

using namespace std;

namespace df
{

std::vector<float> const kTransitLinesWidthInPixel =
{
  // 1   2     3     4     5     6     7     8     9    10
  1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
  //11  12    13    14    15    16    17    18    19     20
  1.5f, 1.8f, 2.2f, 2.8f, 3.2f, 3.8f, 4.8f, 5.2f, 5.8f, 5.8f
};

struct TransitLineStaticVertex
{
  using TPosition = glsl::vec3;
  using TNormal = glsl::vec3;
  using TColor = glsl::vec4;

  TransitLineStaticVertex() = default;
  TransitLineStaticVertex(TPosition const & position, TNormal const & normal,
                          TColor const & color)
  : m_position(position)
  , m_normal(normal)
  , m_color(color)
  {}

  TPosition m_position;
  TNormal m_normal;
  TColor m_color;
};

using TV = TransitLineStaticVertex;
using TGeometryBuffer = vector<TV>;

dp::BindingInfo const & GetTransitStaticBindingInfo()
{
  static unique_ptr<dp::BindingInfo> s_info;
  if (s_info == nullptr)
  {
    dp::BindingFiller<TransitLineStaticVertex> filler(3);
    filler.FillDecl<TransitLineStaticVertex::TPosition>("a_position");
    filler.FillDecl<TransitLineStaticVertex::TNormal>("a_normal");
    filler.FillDecl<TransitLineStaticVertex::TColor>("a_color");
    s_info.reset(new dp::BindingInfo(filler.m_info));
  }
  return *s_info;
}

void SubmitStaticVertex(glsl::vec3 const & pivot, glsl::vec2 const & normal, float side,
                        glsl::vec4 const & color, TGeometryBuffer & geometry)
{
  geometry.emplace_back(pivot, TransitLineStaticVertex::TNormal(normal, side), color);
}

void GenerateJoinsTriangles(glsl::vec3 const & pivot, std::vector<glsl::vec2> const & normals, glsl::vec2 const & offset,
                            glsl::vec4 const & color, TGeometryBuffer & joinsGeometry)
{
  float const kEps = 1e-5;
  size_t const trianglesCount = normals.size() / 3;
  for (size_t j = 0; j < trianglesCount; j++)
  {
    SubmitStaticVertex(pivot, normals[3 * j] + offset, 1.0f, color, joinsGeometry);
    SubmitStaticVertex(pivot, normals[3 * j + 1] + offset, 1.0f, color, joinsGeometry);
    SubmitStaticVertex(pivot, normals[3 * j + 2] + offset, 1.0f, color, joinsGeometry);
  }
}

void TransitSchemeBuilder::SetVisibleMwms(std::vector<MwmSet::MwmId> const & visibleMwms)
{
  m_visibleMwms = visibleMwms;
}

void TransitSchemeBuilder::AddShape(TransitDisplayInfo const & transitDisplayInfo,
                                    routing::transit::StopId stop1Id,
                                    routing::transit::StopId stop2Id,
                                    routing::transit::LineId lineId,
                                    MwmSchemeData & scheme)
{
  auto const stop1It = transitDisplayInfo.m_stops.find(stop1Id);
  ASSERT(stop1It != transitDisplayInfo.m_stops.end(), ());

  auto const stop2It = transitDisplayInfo.m_stops.find(stop2Id);
  ASSERT(stop2It != transitDisplayInfo.m_stops.end(), ());

  auto const transfer1Id = stop1It->second.GetTransferId();
  auto const transfer2Id = stop2It->second.GetTransferId();

  auto shapeId = routing::transit::ShapeId(transfer1Id != routing::transit::kInvalidTransferId ? transfer1Id
                                                                                               : stop1Id,
                                           transfer2Id != routing::transit::kInvalidTransferId ? transfer2Id
                                                                                               : stop2Id);
  auto it = transitDisplayInfo.m_shapes.find(shapeId);
  bool isForward = true;
  if (it == transitDisplayInfo.m_shapes.end())
  {
    isForward = false;
    shapeId = routing::transit::ShapeId(shapeId.GetStop2Id(), shapeId.GetStop1Id());
    it = transitDisplayInfo.m_shapes.find(shapeId);
  }

  // TODO: check
  if (it == transitDisplayInfo.m_shapes.end())
    return;

  ASSERT(it != transitDisplayInfo.m_shapes.end(), ());

  if (shapeId.GetStop1Id() > shapeId.GetStop2Id())
    shapeId = routing::transit::ShapeId(shapeId.GetStop2Id(), shapeId.GetStop1Id());

  auto itScheme = scheme.m_shapes.find(shapeId);
  if (itScheme == scheme.m_shapes.end())
  {
    auto const & polyline = transitDisplayInfo.m_shapes.at(it->first).GetPolyline();
    if (isForward)
      scheme.m_shapes[shapeId].m_forwardLines.push_back(lineId);
    else
      scheme.m_shapes[shapeId].m_backwardLines.push_back(lineId);
    scheme.m_shapes[shapeId].m_polyline = polyline;
  }
  else
  {
    for (auto id : itScheme->second.m_forwardLines)
      if (id >> 4 == lineId >> 4)
        return;
    for (auto id : itScheme->second.m_backwardLines)
      if (id >> 4 == lineId >> 4)
        return;

    if (isForward)
      itScheme->second.m_forwardLines.push_back(lineId);
    else
      itScheme->second.m_backwardLines.push_back(lineId);
  }
}

void TransitSchemeBuilder::UpdateScheme(TransitDisplayInfos const & transitDisplayInfos)
{
  for (auto const & mwmInfo : transitDisplayInfos)
  {
    auto const & mwmId = mwmInfo.first;
    if (!mwmInfo.second)
      continue;
    auto const & transitDisplayInfo = *mwmInfo.second.get();

    MwmSchemeData & scheme = m_schemes[mwmId];

    std::multimap<size_t, routing::transit::LineId> linesLengths;

    std::map<uint32_t, std::vector<routing::transit::LineId>> roads;

    for (auto const & line : transitDisplayInfo.m_lines)
    {
      auto const lineId = line.second.GetId();
      auto const roadId = static_cast<uint32_t>(lineId >> 4);
      roads[roadId].push_back(lineId);
    }

    for (auto const & line : transitDisplayInfo.m_lines)
    {
      auto const lineId = line.second.GetId();
      auto const roadId = static_cast<uint32_t>(lineId >> 4);

      scheme.m_lines[lineId] = LineParams(line.second.GetColor(), 0.0f /* depth */);

      std::map<routing::transit::StopId, std::string> stopTitles;

      auto const & stopsRanges = line.second.GetStopIds();
      for (auto const & stops : stopsRanges)
      {
        for (size_t i = 0; i < stops.size(); ++i)
        {
          auto const stopIt = transitDisplayInfo.m_stops.find(stops[i]);
          ASSERT(stopIt != transitDisplayInfo.m_stops.end(), ());

          FeatureID featureId;
          string title;

          auto const fid = stopIt->second.GetFeatureId();
          if (fid != routing::transit::kInvalidFeatureId)
          {
            featureId = FeatureID(mwmId, fid);
            title = transitDisplayInfo.m_features.at(featureId).m_title;
          }

          auto const transferId = stopIt->second.GetTransferId();
          if (transferId != routing::transit::kInvalidTransferId)
          {
            auto & transfer = scheme.m_transfers[transferId];
            transfer.m_pivot = transitDisplayInfo.m_transfers.at(transferId).GetPoint();
            transfer.m_lines.insert(lineId);
            transfer.m_names.insert(title);
            transfer.m_featureId = featureId;
          }
          else
          {
            auto & stop = scheme.m_stops[stopIt->second.GetId()];
            stop.m_pivot = stopIt->second.GetPoint();
            stop.m_lines.insert(lineId);
            stop.m_names.insert(title);
            stop.m_featureId = featureId;
          }
          stopTitles[stopIt->second.GetId()] = title;
        }
      }

      size_t stopsCount = 0;
      for (auto const & stops : stopsRanges)
      {
        for (size_t i = 0; i < stops.size(); ++i)
        {
          ++stopsCount;
          if (i + 1 < stops.size())
          {
            bool shapeAdded = false;

            auto const & sameLines = roads[roadId];
            for (auto const & sameLineId : sameLines)
            {
              if (sameLineId == lineId)
                continue;

              auto const & sameLine = transitDisplayInfo.m_lines.at(sameLineId);
              auto const & sameStopsRanges = sameLine.GetStopIds();
              for (auto const & sameStops : sameStopsRanges)
              {
                size_t stop1Ind = std::numeric_limits<size_t>::max();
                size_t stop2Ind = std::numeric_limits<size_t>::max();

                for (size_t stopInd = 0; stopInd < sameStops.size(); ++stopInd)
                {
                  if (sameStops[stopInd] == stops[i])
                    stop1Ind = stopInd;
                  else if (sameStops[stopInd] == stops[i + 1])
                    stop2Ind = stopInd;
                }

                if (stop1Ind < sameStops.size() || stop2Ind < sameStops.size())
                {
                  if (stop1Ind > stop2Ind)
                    swap(stop1Ind, stop2Ind);

                  if (stop1Ind < sameStops.size() && stop2Ind < sameStops.size() && stop2Ind - stop1Ind > 1)
                  {


                    for (size_t stopInd = stop1Ind; stopInd < stop2Ind; ++stopInd)
                    {
                      shapeAdded = true;
/*
                      LOG(LWARNING, ("!!! Use shape lineId",
                        lineId, line.second.GetTitle(),
                        "stop1", stops[i], stopTitles[stops[i]],
                        "stop2", stops[i + 1], stopTitles[stops[i + 1]],
                        "sameLineId", sameLineId, sameLine.GetTitle(),
                        "sameStop1", sameStops[stopInd], stopTitles[sameStops[stopInd]],
                        "sameStop2", sameStops[stopInd + 1], stopTitles[sameStops[stopInd + 1]]));
*/
                      AddShape(transitDisplayInfo, sameStops[stopInd], sameStops[stopInd + 1], lineId, scheme);
                    }
                  }
                  break;
                }
              }

              if (shapeAdded)
                break;
            }

            if (!shapeAdded)
            {
              AddShape(transitDisplayInfo, stops[i], stops[i + 1], lineId, scheme);
            }
            else
            {
              LOG(LWARNING, ("Skip shape for line", lineId, line.second.GetTitle(),
                "stop1", stops[i], stopTitles[stops[i]], "stop2", stops[i + 1], stopTitles[stops[i + 1]]));
            }
          }
        }
      }

      linesLengths.insert(std::make_pair(stopsCount, lineId));
    }

    m2::RectD boundingRect;
    for (auto const & shape : scheme.m_shapes)
    {
      for (auto const & pt : shape.second.m_polyline)
        boundingRect.Add(pt);
    }
    scheme.m_pivot = boundingRect.Center();

    float const kDepthPerLine = 1.0f;
    float depth = 0.0f;
    for (auto const & pair : linesLengths)
    {
      depth += kDepthPerLine;
      scheme.m_lines[pair.second].m_depth = depth;
    }
  }
}

vector<m2::PointF> GetTransitMarkerSizes(float markerScale, float maxRouteWidth)
{
  auto const vs = static_cast<float>(df::VisualParams::Instance().GetVisualScale());
  vector<m2::PointF> markerSizes;
  markerSizes.reserve(df::kTransitLinesWidthInPixel.size());
  for (auto const halfWidth : df::kTransitLinesWidthInPixel)
  {
    float const d = 2.0f * std::min(halfWidth * vs, maxRouteWidth * 0.5f) * markerScale;
    markerSizes.push_back(m2::PointF(d, d));
  }
  return markerSizes;
}

void GenerateMarker(m2::PointD const & pt, float depth, float innerDepth, float outerRadius, float innerRadius,
                     dp::Color const & outerColor, dp::Color const & innerColor, TGeometryBuffer & geometry)
{
  float const kSqrt3 = sqrt(3.0f);

  glsl::vec3 outerPos(pt.x, pt.y, depth);
  glsl::vec3 innerPos(pt.x, pt.y, innerDepth);

  dp::Color const colorConst = dp::Color::Black();
  auto const color1 = glsl::vec4(outerColor.GetRedF(), outerColor.GetGreenF(), outerColor.GetBlueF(), 1.0f /* alpha */ );
  // Here we use an equilateral triangle to render circle (incircle of a triangle).
  geometry.emplace_back(outerPos, TransitLineStaticVertex::TNormal(-kSqrt3, -1.0f, outerRadius), color1);
  geometry.emplace_back(outerPos, TransitLineStaticVertex::TNormal(kSqrt3, -1.0f, outerRadius), color1);
  geometry.emplace_back(outerPos, TransitLineStaticVertex::TNormal(0.0f, 2.0f, outerRadius), color1);

  auto const color2 = glsl::vec4(innerColor.GetRedF(), innerColor.GetGreenF(), innerColor.GetBlueF(), 1.0f /* alpha */ );
  geometry.emplace_back(innerPos, TransitLineStaticVertex::TNormal(-kSqrt3, -1.0f, innerRadius), color2);
  geometry.emplace_back(innerPos, TransitLineStaticVertex::TNormal(kSqrt3, -1.0f, innerRadius), color2);
  geometry.emplace_back(innerPos, TransitLineStaticVertex::TNormal(0.0f, 2.0f, innerRadius), color2);
}

void TransitSchemeBuilder::BuildScheme(ref_ptr<dp::TextureManager> textures)
{
  auto state = CreateGLState(gpu::TRANSIT_PROGRAM, RenderState::TransitSchemeLayer);
  auto stateMarkers = CreateGLState(gpu::TRANSIT_MARKER_PROGRAM, RenderState::TransitSchemeLayer);
  auto stateText = CreateGLState(gpu::TEXT_OUTLINED_PROGRAM, RenderState::TransitSchemeLayer);

  for (auto const & mwmId : m_visibleMwms)
  {
    MwmSchemeData & scheme = m_schemes[mwmId];
    m2::PointD pivot = scheme.m_pivot;

    TransitMarkersRenderData markersRenderData(stateMarkers);
    markersRenderData.m_mwmId = mwmId;
    markersRenderData.m_pivot = pivot;

    TransitTextRenderData textRenderData(stateText);
    textRenderData.m_mwmId = mwmId;
    textRenderData.m_pivot = pivot;

    TransitTextRenderData colorSymbolRenderData(stateText);
    colorSymbolRenderData.m_mwmId = mwmId;
    colorSymbolRenderData.m_pivot = pivot;

    for (auto const & shape : scheme.m_shapes)
    {
      auto const linesCount = shape.second.m_forwardLines.size() + shape.second.m_backwardLines.size();

      auto offset = -static_cast<float>(linesCount / 2) * 2.0f - 1.0f * (linesCount % 2) + 1.0f;

      std::vector<std::pair<std::string, routing::transit::LineId>> colorsNames;
      for (auto lineId : shape.second.m_forwardLines)
      {
        auto const colorName = df::GetTransitColorName(scheme.m_lines[lineId].m_color);
        colorsNames.push_back(std::make_pair(colorName, lineId));
      }
      for (auto it = shape.second.m_backwardLines.rbegin(); it != shape.second.m_backwardLines.rend(); ++it)
      {
        auto const colorName = df::GetTransitColorName(scheme.m_lines[*it].m_color);
        colorsNames.push_back(std::make_pair(colorName, *it));
      }

      for (auto const & colorName : colorsNames)
      {
        dp::Color const colorConst = GetColorConstant(colorName.first);
        auto const color = glsl::vec4(colorConst.GetRedF(), colorConst.GetGreenF(), colorConst.GetBlueF(),
                                      1.0f /* alpha */);

        auto const lineId = (!shape.second.m_forwardLines.empty()) ? shape.second.m_forwardLines.front()
                                                                   : shape.second.m_backwardLines.front();
        auto const depth = scheme.m_lines[lineId].m_depth;

        TGeometryBuffer geometry;
        TGeometryBuffer joinsGeometry;

        auto const & path = shape.second.m_polyline;

        size_t const kAverageSize = path.size() * 4;
        size_t const kAverageCapSize = 12;

        geometry.reserve(kAverageSize + kAverageCapSize * 2);

        struct SchemeSegment
        {
          glsl::vec2 m_p1;
          glsl::vec2 m_p2;
          glsl::vec2 m_tangent;
          glsl::vec2 m_leftNormal;
          glsl::vec2 m_rightNormal;
        };

        std::vector<SchemeSegment> segments;
        segments.reserve(path.size() - 1);

        float const halfWidth = 0.8f;

        for (size_t i = 1; i < path.size(); ++i)
        {
          if (path[i].EqualDxDy(path[i - 1], 1.0E-5))
            continue;

          SchemeSegment segment;
          segment.m_p1 = glsl::ToVec2(MapShape::ConvertToLocal(path[i - 1], pivot, kShapeCoordScalar));
          segment.m_p2 = glsl::ToVec2(MapShape::ConvertToLocal(path[i], pivot, kShapeCoordScalar));
          CalculateTangentAndNormals(segment.m_p1, segment.m_p2, segment.m_tangent,
                                     segment.m_leftNormal, segment.m_rightNormal);

          glsl::vec3 const startPivot = glsl::vec3(segment.m_p1, depth);
          glsl::vec3 const endPivot = glsl::vec3(segment.m_p2, depth);

          SubmitStaticVertex(startPivot, segment.m_rightNormal * halfWidth - offset * segment.m_rightNormal, -halfWidth, color, geometry);
          SubmitStaticVertex(startPivot, segment.m_leftNormal * halfWidth - offset * segment.m_rightNormal, halfWidth, color, geometry);
          SubmitStaticVertex(endPivot, segment.m_rightNormal * halfWidth - offset * segment.m_rightNormal, -halfWidth, color, geometry);
          SubmitStaticVertex(endPivot, segment.m_rightNormal * halfWidth - offset * segment.m_rightNormal, -halfWidth, color, geometry);
          SubmitStaticVertex(startPivot, segment.m_leftNormal * halfWidth - offset * segment.m_rightNormal, halfWidth, color, geometry);
          SubmitStaticVertex(endPivot, segment.m_leftNormal * halfWidth - offset * segment.m_rightNormal, halfWidth, color, geometry);

          segments.push_back(segment);
        }

        for (size_t i = 0; i < segments.size(); ++i)
        {
          int const kSegmentsCount = 4;
          vector<glsl::vec2> normals;
          normals.reserve(kAverageCapSize);
          GenerateCapNormals(dp::RoundCap, segments[i].m_leftNormal, segments[i].m_rightNormal, segments[i].m_tangent,
                             halfWidth, false /* isStart */, normals, kSegmentsCount);
          GenerateJoinsTriangles(glsl::vec3(segments[i].m_p2, depth), normals, -offset * segments[i].m_rightNormal,
                                 color, joinsGeometry);
        }

        auto const geomSize = static_cast<uint32_t>(geometry.size());
        auto const joinsGeomSize = static_cast<uint32_t>(joinsGeometry.size());

        uint32_t const kBatchSize = 5000;
        dp::Batcher batcher(kBatchSize, kBatchSize);

        TransitRenderData renderData(state);
        renderData.m_mwmId = mwmId;
        renderData.m_shapeId = shape.first;
        renderData.m_lineId = colorName.second;
        renderData.m_pivot = pivot;
        {
          dp::SessionGuard guard(batcher, [&renderData](dp::GLState const & state, drape_ptr<dp::RenderBucket> && b)
          {
            renderData.m_buckets.push_back(std::move(b));
          });

          if (geomSize != 0)
          {
            dp::AttributeProvider provider(1 /* stream count */, geomSize);
            provider.InitStream(0 /* stream index */, GetTransitStaticBindingInfo(), make_ref(geometry.data()));
            batcher.InsertTriangleList(state, make_ref(&provider));
          }

          if (joinsGeomSize != 0)
          {
            dp::AttributeProvider joinsProvider(1 /* stream count */, joinsGeomSize);
            joinsProvider.InitStream(0 /* stream index */,
                                     GetTransitStaticBindingInfo(),
                                     make_ref(joinsGeometry.data()));
            batcher.InsertTriangleList(state, make_ref(&joinsProvider));
          }
        }
        m_flushRenderDataFn(std::move(renderData));

        offset += 2.0f;
      }
    }

    float const kBaseMarkerDepth = 300.0f;
    float const kStopScale = 2.5f;
    float const kSharedStopScale = 2.8;
    float const kTransferScale = 3.0f;

    float const kInnerRadius = 0.8f;
    float const kOuterRadius = 1.0f;

    std::vector<m2::PointF> const transferMarkerSizes = GetTransitMarkerSizes(kTransferScale, 1000);
    std::vector<m2::PointF> const stopMarkerSizes = GetTransitMarkerSizes(kStopScale, 1000);

    TGeometryBuffer geometry;

    uint32_t const kBatchSize = 5000;
    dp::Batcher batcher(kBatchSize, kBatchSize);

    {
      dp::SessionGuard guard(batcher,
                             [&markersRenderData, &textRenderData, &colorSymbolRenderData](dp::GLState const & state,
                                                                                           drape_ptr<dp::RenderBucket> && b)
      {
        if (state.GetProgramIndex() == gpu::TRANSIT_MARKER_PROGRAM)
        {
          markersRenderData.m_state = state;
          markersRenderData.m_buckets.push_back(std::move(b));
        }
        else if (state.GetProgramIndex() == gpu::TEXT_OUTLINED_PROGRAM)
        {
          textRenderData.m_state = state;
          textRenderData.m_buckets.push_back(std::move(b));
        }
        else
        {
          colorSymbolRenderData.m_state = state;
          colorSymbolRenderData.m_buckets.push_back(std::move(b));
        }
      });

      float const depth = kBaseMarkerDepth + 0.5f;
      float const innerDepth = kBaseMarkerDepth + 1.0f;
      for (auto const & stop : scheme.m_stops)
      {
        bool severalRoads = false;
        auto const roadId = *stop.second.m_lines.begin() >> 4;
        for (auto const & lineId : stop.second.m_lines)
        {
          severalRoads |= lineId >> 4 != roadId;
        }

        float outerRadius = kOuterRadius * kSharedStopScale;
        float innerRadius = kInnerRadius * kSharedStopScale;;
        dp::Color outerColor = dp::Color::Black();
        dp::Color innerColor = dp::Color::White();
        if (!severalRoads)
        {
          outerRadius = kOuterRadius * kStopScale;
          innerRadius = outerRadius * 0.4f;
          auto const colorName = df::GetTransitColorName(scheme.m_lines[*stop.second.m_lines.begin()].m_color);
          outerColor = GetColorConstant(colorName);
        }

        m2::PointD const pt = MapShape::ConvertToLocal(stop.second.m_pivot, pivot, kShapeCoordScalar);
        GenerateMarker(pt, depth, innerDepth, outerRadius, innerRadius, outerColor, innerColor, geometry);
        GenerateTitles(stop.second, pivot, stopMarkerSizes, textures, batcher);
      }
      for (auto const & transfer : scheme.m_transfers)
      {
        float const innerRadius = kInnerRadius * kTransferScale;
        float const outerRadius = kOuterRadius * kTransferScale;
        dp::Color const outerColor = dp::Color::Black();
        dp::Color const innerColor = dp::Color::White();

        m2::PointD const pt = MapShape::ConvertToLocal(transfer.second.m_pivot, pivot, kShapeCoordScalar);
        GenerateMarker(pt, depth, innerDepth, outerRadius, innerRadius, outerColor, innerColor, geometry);
        GenerateTitles(transfer.second, pivot, transferMarkerSizes, textures, batcher);
      }

      auto const geomSize = static_cast<uint32_t>(geometry.size());
      if (geomSize != 0)
      {
        dp::AttributeProvider provider(1 /* stream count */, geomSize);
        provider.InitStream(0 /* stream index */, GetTransitStaticBindingInfo(), make_ref(geometry.data()));
        batcher.InsertTriangleList(stateMarkers, make_ref(&provider));
      }
    }

    m_flushMarkersRenderDataFn(std::move(markersRenderData));
    m_flushTextRenderDataFn(std::move(colorSymbolRenderData));
    m_flushTextRenderDataFn(std::move(textRenderData));
  }
}

void TransitSchemeBuilder::GenerateTitles(StopParams const & stopParams, m2::PointD const & pivot,
                                          vector<m2::PointF> const & markerSizes,
                                          ref_ptr<dp::TextureManager> textures, dp::Batcher & batcher)
{
  if (stopParams.m_names.empty() || stopParams.m_names.begin()->empty())
    return;

  auto const vs = df::VisualParams::Instance().GetVisualScale();

  float const kBaseTitleDepth = 400.0f;

  // TODO(@darina) Use separate colors.
  std::string const kTransitMarkText = "RouteMarkPrimaryText";
  std::string const kTransitMarkTextOutline = "RouteMarkPrimaryTextOutline";

  float const kRouteMarkPrimaryTextSize = 11.0f;
  float const kRouteMarkSecondaryTextSize = 10.0f;
  float const kRouteMarkSecondaryOffsetY = 2.0f;
  float const kTransitMarkTextSize = 12.0f;

  dp::TitleDecl titleDecl;
  titleDecl.m_primaryText = *stopParams.m_names.begin();
  titleDecl.m_primaryTextFont.m_color = df::GetColorConstant(kTransitMarkText);
  titleDecl.m_primaryTextFont.m_outlineColor = df::GetColorConstant(kTransitMarkTextOutline);
  titleDecl.m_primaryTextFont.m_size = kTransitMarkTextSize;
  titleDecl.m_secondaryTextFont.m_color = df::GetColorConstant(kTransitMarkText);
  titleDecl.m_secondaryTextFont.m_outlineColor = df::GetColorConstant(kTransitMarkTextOutline);
  titleDecl.m_secondaryTextFont.m_size = kTransitMarkTextSize;
  titleDecl.m_anchor = dp::Left;
  titleDecl.m_primaryOffset = m2::PointF(1.0f * vs, 0.0f);

  TextViewParams params;
  params.m_featureID = stopParams.m_featureId;
  params.m_tileCenter = pivot;
  params.m_titleDecl = titleDecl;

  // Here we use visual scale to adapt texts sizes and offsets
  // to different screen resolutions and DPI.
  params.m_titleDecl.m_primaryTextFont.m_size *= vs;
  params.m_titleDecl.m_secondaryTextFont.m_size *= vs;
  params.m_titleDecl.m_primaryOffset *= vs;
  params.m_titleDecl.m_secondaryOffset *= vs;
  bool const isSdf = df::VisualParams::Instance().IsSdfPrefered();
  params.m_titleDecl.m_primaryTextFont.m_isSdf =
    params.m_titleDecl.m_primaryTextFont.m_outlineColor != dp::Color::Transparent() ? true : isSdf;
  params.m_titleDecl.m_secondaryTextFont.m_isSdf =
    params.m_titleDecl.m_secondaryTextFont.m_outlineColor != dp::Color::Transparent() ? true : isSdf;

  params.m_depth = kBaseTitleDepth;
  params.m_depthLayer = RenderState::TransitSchemeLayer;

  uint32_t const overlayIndex = kStartUserMarkOverlayIndex;

  params.m_specialDisplacement = SpecialDisplacement::UserMark;
  //params.m_specialPriority = ;
  params.m_startOverlayRank = dp::OverlayRank1;

  m2::PointF symbolOffset(0.0f, 0.0f);

  TileKey tileKey;
  TextShape(stopParams.m_pivot, params, tileKey, markerSizes, symbolOffset, dp::Center, overlayIndex)
    .Draw(&batcher, textures);

  df::ColoredSymbolViewParams colorParams;
  colorParams.m_radiusInPixels = markerSizes.front().x * 0.5f;

  colorParams.m_color = dp::Color::Transparent();
  colorParams.m_featureID = stopParams.m_featureId;
  colorParams.m_tileCenter = pivot;
  colorParams.m_depth = kBaseTitleDepth;
  colorParams.m_depthLayer = RenderState::TransitSchemeLayer;
  colorParams.m_specialDisplacement = SpecialDisplacement::UserMark;
  params.m_startOverlayRank = dp::OverlayRank0;
  //colorParams.m_specialPriority = ;

  ColoredSymbolShape(stopParams.m_pivot, colorParams, tileKey, overlayIndex, markerSizes)
    .Draw(&batcher, textures);
}

}  // namespace df
