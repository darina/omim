#include "transit_scheme_builder.hpp"

#include "drape_frontend/color_constants.hpp"
#include "drape_frontend/line_shape_helper.hpp"
#include "drape_frontend/map_shape.hpp"
#include "drape_frontend/render_state.hpp"
#include "drape_frontend/shader_def.hpp"
#include "drape_frontend/shape_view_params.hpp"

#include "drape/batcher.hpp"
#include "drape/glsl_func.hpp"
#include "drape/glsl_types.hpp"
#include "drape/render_bucket.hpp"
#include "drape/utils/vertex_decl.hpp"

using namespace std;

namespace df
{

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

void GenerateJoinsTriangles(glsl::vec3 const & pivot, std::vector<glsl::vec2> const & normals,
                            glsl::vec4 const & color, TGeometryBuffer & joinsGeometry)
{
  float const kEps = 1e-5;
  size_t const trianglesCount = normals.size() / 3;
  for (size_t j = 0; j < trianglesCount; j++)
  {
    SubmitStaticVertex(pivot, normals[3 * j], glsl::length(normals[3 * j]) < kEps ? 0.0f : 1.0f, color,
                       joinsGeometry);
    SubmitStaticVertex(pivot, normals[3 * j + 1], glsl::length(normals[3 * j + 1]) < kEps ? 0.0f : 1.0f, color,
                       joinsGeometry);
    SubmitStaticVertex(pivot, normals[3 * j + 2], glsl::length(normals[3 * j + 2]) < kEps ? 0.0f : 1.0f, color,
                       joinsGeometry);
  }
}

void TransitSchemeBuilder::SetVisibleMwms(std::vector<MwmSet::MwmId> const & visibleMwms)
{
  m_visibleMwms = visibleMwms;
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

    float depth = 0.0f;
    float const kDepthPerLine = 1.0f;
    for (auto const &line : transitDisplayInfo.m_lines)
    {
      auto const lineId = line.second.GetId();
      scheme.m_lines[lineId] = LineParams(line.second.GetColor(), depth);
      depth += kDepthPerLine;
      auto const &stopsRanges = line.second.GetStopIds();
      for (auto const &stopsRange : stopsRanges)
      {
        for (size_t i = 0; i < stopsRange.size(); ++i)
        {
          auto const stopIt = transitDisplayInfo.m_stops.find(stopsRange[i]);
          ASSERT(stopIt != transitDisplayInfo.m_stops.end(), ());

          auto const fid = stopIt->second.GetFeatureId();
          string title;
          if (fid != routing::transit::kInvalidFeatureId)
            title = transitDisplayInfo.m_features.at(FeatureID(mwmId, fid)).m_title;

          auto const transferId = stopIt->second.GetTransferId();
          if (transferId != routing::transit::kInvalidTransferId)
          {
            auto & transfer = scheme.m_transfers[transferId];
            transfer.m_pivot = transitDisplayInfo.m_transfers.at(transferId).GetPoint();
            transfer.m_lines.insert(lineId);
            transfer.m_names.insert(title);
          }
          else
          {
            auto & stop = scheme.m_stops[stopIt->second.GetId()];
            stop.m_pivot = stopIt->second.GetPoint();
            stop.m_lines.insert(lineId);
            stop.m_names.insert(title);
          }

          if (i + 1 < stopsRange.size())
          {
            auto const stop2It = transitDisplayInfo.m_stops.find(stopsRange[i + 1]);
            ASSERT(stop2It != transitDisplayInfo.m_stops.end(), ());

            auto const transfer2Id = stop2It->second.GetTransferId();

            auto shapeId = routing::transit::ShapeId(transferId != routing::transit::kInvalidTransferId ? transferId
                                                                                                        : stopsRange[i],
                                                     transfer2Id != routing::transit::kInvalidTransferId ? transfer2Id
                                                                                                         : stopsRange[i + 1]);
            auto it = transitDisplayInfo.m_shapes.find(shapeId);
            if (it == transitDisplayInfo.m_shapes.end())
            {
              shapeId = routing::transit::ShapeId(shapeId.GetStop2Id(), shapeId.GetStop1Id());
              it = transitDisplayInfo.m_shapes.find(shapeId);
            }
            ASSERT(it != transitDisplayInfo.m_shapes.end(), ());

            if (shapeId.GetStop1Id() > shapeId.GetStop2Id())
              shapeId = routing::transit::ShapeId(shapeId.GetStop2Id(), shapeId.GetStop1Id());

            scheme.m_shapes[shapeId].m_lines.insert(lineId);
            scheme.m_shapes[shapeId].m_polyline = transitDisplayInfo.m_shapes.at(it->first).GetPolyline();
          }
        }
      }
    }
  }
}

void TransitSchemeBuilder::BuildScheme()
{
  auto state = CreateGLState(gpu::TRANSIT_PROGRAM, RenderState::TransitSchemeLayer);
  auto stateMarkers = CreateGLState(gpu::TRANSIT_MARKER_PROGRAM, RenderState::TransitSchemeLayer);

  for (auto const & mwmId : m_visibleMwms)
  {
    MwmSchemeData &scheme = m_schemes[mwmId];
    m2::PointD pivot;

    for (auto const &shape : scheme.m_shapes)
    {
      // TODO(darina): Sort lines by depth.
      auto const colorName = df::GetTransitColorName(scheme.m_lines[*shape.second.m_lines.begin()].m_color);
      dp::Color const colorConst = GetColorConstant(colorName);
      auto const color = glsl::vec4(colorConst.GetRedF(),
                                    colorConst.GetGreenF(),
                                    colorConst.GetBlueF(),
                                    1.0f /* alpha */);

      auto const depth = scheme.m_lines[*shape.second.m_lines.begin()].m_depth;

      TGeometryBuffer geometry;
      TGeometryBuffer joinsGeometry;

      auto const & path = shape.second.m_polyline;

      size_t const kAverageSize = path.size() * 4;
      size_t const kAverageCapSize = 12;

      geometry.reserve(kAverageSize + kAverageCapSize * 2);

      // Build geometry.
      glsl::vec2 firstPoint, firstTangent, firstLeftNormal, firstRightNormal;
      glsl::vec2 lastPoint, lastTangent, lastLeftNormal, lastRightNormal;
      bool firstFilled = false;

      m2::RectD rect;
      for (auto const & pt : path)
        rect.Add(pt);
      pivot = rect.Center();

      for (size_t i = 1; i < path.size(); ++i)
      {
        if (path[i].EqualDxDy(path[i - 1], 1.0E-5))
          continue;

        glsl::vec2 const p1 = glsl::ToVec2(MapShape::ConvertToLocal(path[i - 1], pivot, kShapeCoordScalar));
        glsl::vec2 const p2 = glsl::ToVec2(MapShape::ConvertToLocal(path[i], pivot, kShapeCoordScalar));
        glsl::vec2 tangent, leftNormal, rightNormal;
        CalculateTangentAndNormals(p1, p2, tangent, leftNormal, rightNormal);

        // Fill first and last point, tangent and normals.
        if (!firstFilled)
        {
          firstPoint = p1;
          firstTangent = tangent;
          firstLeftNormal = leftNormal;
          firstRightNormal = rightNormal;
          firstFilled = true;
        }
        lastTangent = tangent;
        lastLeftNormal = leftNormal;
        lastRightNormal = rightNormal;
        lastPoint = p2;

        glsl::vec3 const startPivot = glsl::vec3(p1, depth);
        glsl::vec3 const endPivot = glsl::vec3(p2, depth);

        SubmitStaticVertex(startPivot, rightNormal, -1.0f, color, geometry);
        SubmitStaticVertex(startPivot, leftNormal, 1.0f, color, geometry);
        SubmitStaticVertex(endPivot, rightNormal, -1.0f, color, geometry);
        SubmitStaticVertex(endPivot, rightNormal, -1.0f, color, geometry);
        SubmitStaticVertex(startPivot, leftNormal, 1.0f, color, geometry);
        SubmitStaticVertex(endPivot, leftNormal, 1.0f, color, geometry);
      }

      // Generate caps.
      if (firstFilled)
      {
        int const kSegmentsCount = 4;
        vector<glsl::vec2> normals;
        normals.reserve(kAverageCapSize);
        GenerateCapNormals(dp::RoundCap, firstLeftNormal, firstRightNormal, -firstTangent,
                           1.0f, true /* isStart */, normals, kSegmentsCount);
        GenerateJoinsTriangles(glsl::vec3(firstPoint, depth), normals, color, joinsGeometry);

        normals.clear();
        GenerateCapNormals(dp::RoundCap, lastLeftNormal, lastRightNormal, lastTangent,
                           1.0f, false /* isStart */, normals, kSegmentsCount);
        GenerateJoinsTriangles(glsl::vec3(lastPoint, depth), normals, color, joinsGeometry);
      }

      auto const geomSize = static_cast<uint32_t>(geometry.size());
      auto const joinsGeomSize = static_cast<uint32_t>(joinsGeometry.size());

      uint32_t const kBatchSize = 5000;
      dp::Batcher batcher(kBatchSize, kBatchSize);

      TransitRenderData renderData(state);
      renderData.m_mwmId = mwmId;
      renderData.m_shapeId = shape.first;
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
          joinsProvider.InitStream(0 /* stream index */, GetTransitStaticBindingInfo(), make_ref(joinsGeometry.data()));
          batcher.InsertTriangleList(state, make_ref(&joinsProvider));
        }
      }
      m_flushRenderDataFn(std::move(renderData));
    }

    float const kBaseMarkerDepth = 300.0f;
    float const kStopScale = 2.3f;
    float const kTransferScale = 3.0f;
    float const kSqrt3 = sqrt(3.0f);
    float const kInnerRadius = 0.8f;
    float const kOuterRadius = 1.0f;
    TGeometryBuffer geometry;
    for (auto const &stop : scheme.m_stops)
    {
      float const depth = kBaseMarkerDepth + 0.5f;

      float const outerRadius = kOuterRadius * kStopScale;

      m2::PointD const pt = MapShape::ConvertToLocal(stop.second.m_pivot, pivot, kShapeCoordScalar);
      glsl::vec3 outerPos(pt.x, pt.y, depth);

      auto const colorName = df::GetTransitColorName(scheme.m_lines[*stop.second.m_lines.begin()].m_color);
      dp::Color const colorConst = GetColorConstant(colorName);
      auto const color = glsl::vec4(colorConst.GetRedF(),
                                    colorConst.GetGreenF(),
                                    colorConst.GetBlueF(),
                                    1.0f /* alpha */ );

      // Here we use an equilateral triangle to render circle (incircle of a triangle).
      geometry.emplace_back(outerPos, TransitLineStaticVertex::TNormal(-kSqrt3, -1.0f, outerRadius), color);
      geometry.emplace_back(outerPos, TransitLineStaticVertex::TNormal(kSqrt3, -1.0f, outerRadius), color);
      geometry.emplace_back(outerPos, TransitLineStaticVertex::TNormal(0.0f, 2.0f, outerRadius), color);
    }
    for (auto const &transfer : scheme.m_transfers)
    {
      float const depth = kBaseMarkerDepth + 0.5f;
      float const innerDepth = kBaseMarkerDepth + 1.0f;

      float const innerRadius = kInnerRadius * kTransferScale;
      float const outerRadius = kOuterRadius * kTransferScale;

      m2::PointD const pt = MapShape::ConvertToLocal(transfer.second.m_pivot, pivot, kShapeCoordScalar);
      glsl::vec3 outerPos(pt.x, pt.y, depth);
      glsl::vec3 innerPos(pt.x, pt.y, innerDepth);

      dp::Color const colorConst = dp::Color::Black();
      auto const color = glsl::vec4(colorConst.GetRedF(),
                                    colorConst.GetGreenF(),
                                    colorConst.GetBlueF(),
                                    1.0f /* alpha */ );
      // Here we use an equilateral triangle to render circle (incircle of a triangle).
      geometry.emplace_back(outerPos, TransitLineStaticVertex::TNormal(-kSqrt3, -1.0f, outerRadius), color);
      geometry.emplace_back(outerPos, TransitLineStaticVertex::TNormal(kSqrt3, -1.0f, outerRadius), color);
      geometry.emplace_back(outerPos, TransitLineStaticVertex::TNormal(0.0f, 2.0f, outerRadius), color);

      dp::Color const innerColorConst = dp::Color::White();
      auto const innerColor = glsl::vec4(innerColorConst.GetRedF(),
                                    innerColorConst.GetGreenF(),
                                    innerColorConst.GetBlueF(),
                                    1.0f /* alpha */ );
      geometry.emplace_back(innerPos, TransitLineStaticVertex::TNormal(-kSqrt3, -1.0f, innerRadius), innerColor);
      geometry.emplace_back(innerPos, TransitLineStaticVertex::TNormal(kSqrt3, -1.0f, innerRadius), innerColor);
      geometry.emplace_back(innerPos, TransitLineStaticVertex::TNormal(0.0f, 2.0f, innerRadius), innerColor);
    }

    auto const geomSize = static_cast<uint32_t>(geometry.size());
    uint32_t const kBatchSize = 5000;
    dp::Batcher batcher(kBatchSize, kBatchSize);

    TransitMarkersRenderData markersRenderData(stateMarkers);
    markersRenderData.m_mwmId = mwmId;
    markersRenderData.m_pivot = pivot;

    {
      dp::SessionGuard guard(batcher, [&markersRenderData](dp::GLState const & state, drape_ptr<dp::RenderBucket> && b)
      {
        markersRenderData.m_buckets.push_back(std::move(b));
      });

      if (geomSize != 0)
      {
        dp::AttributeProvider provider(1 /* stream count */, geomSize);
        provider.InitStream(0 /* stream index */, GetTransitStaticBindingInfo(), make_ref(geometry.data()));
        batcher.InsertTriangleList(state, make_ref(&provider));
      }

    }
    m_flushMarkersRenderDataFn(std::move(markersRenderData));
  }
}
}  // namespace df
