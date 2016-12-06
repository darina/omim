#include "drape_frontend/traffic_generator.hpp"

#include "drape_frontend/line_shape_helper.hpp"
#include "drape_frontend/map_shape.hpp"
#include "drape_frontend/shape_view_params.hpp"
#include "drape_frontend/tile_utils.hpp"
#include "drape_frontend/traffic_renderer.hpp"
#include "drape_frontend/visual_params.hpp"

#include "drape/attribute_provider.hpp"
#include "drape/glsl_func.hpp"
#include "drape/shader_def.hpp"
#include "drape/texture_manager.hpp"
#include "drape/support_manager.hpp"

#include "indexer/map_style_reader.hpp"

#include "base/logging.hpp"

#include "std/algorithm.hpp"
#include "std/atomic.hpp"

namespace df
{

namespace
{

uint32_t const kDynamicStreamID = 0x7F;

dp::BindingInfo const & GetTrafficStaticBindingInfo()
{
  static unique_ptr<dp::BindingInfo> s_info;
  if (s_info == nullptr)
  {
    dp::BindingFiller<TrafficStaticVertex> filler(2);
    filler.FillDecl<TrafficStaticVertex::TPosition>("a_position");
    filler.FillDecl<TrafficStaticVertex::TNormal>("a_normal");
    s_info.reset(new dp::BindingInfo(filler.m_info));
  }
  return *s_info;
}

dp::BindingInfo const & GetTrafficLineStaticBindingInfo()
{
  static unique_ptr<dp::BindingInfo> s_info;
  if (s_info == nullptr)
  {
    dp::BindingFiller<TrafficLineStaticVertex> filler(1);
    filler.FillDecl<TrafficLineStaticVertex::TPosition>("a_position");
    s_info.reset(new dp::BindingInfo(filler.m_info));
  }
  return *s_info;
}

dp::BindingInfo const & GetTrafficDynamicBindingInfo()
{
  static unique_ptr<dp::BindingInfo> s_info;
  if (s_info == nullptr)
  {
    dp::BindingFiller<TrafficDynamicVertex> filler(1, kDynamicStreamID);
    filler.FillDecl<TrafficDynamicVertex::TTexCoord>("a_colorTexCoord");
    s_info.reset(new dp::BindingInfo(filler.m_info));
  }
  return *s_info;
}

void SubmitStaticVertex(glsl::vec3 const & pivot, glsl::vec2 const & normal, float side, float offsetFromStart,
                        vector<TrafficStaticVertex> & staticGeom)
{
  staticGeom.emplace_back(pivot, TrafficStaticVertex::TNormal(normal, side, offsetFromStart));
}

void SubmitDynamicVertex(glsl::vec2 const & texCoord, vector<TrafficDynamicVertex> & dynamicGeom)
{
  dynamicGeom.emplace_back(texCoord);
}

void GenerateCapTriangles(glsl::vec3 const & pivot, vector<glsl::vec2> const & normals,
                          dp::TextureManager::ColorRegion const & colorRegion,
                          vector<TrafficStaticVertex> & staticGeometry,
                          vector<TrafficDynamicVertex> & dynamicGeometry)
{
  float const kEps = 1e-5;
  glsl::vec2 const uv = glsl::ToVec2(colorRegion.GetTexRect().Center());
  size_t const trianglesCount = normals.size() / 3;
  for (int j = 0; j < trianglesCount; j++)
  {
    SubmitStaticVertex(pivot, normals[3 * j],
                       glsl::length(normals[3 * j]) < kEps ? 0.0f : 1.0f, 0.0f, staticGeometry);
    SubmitStaticVertex(pivot, normals[3 * j + 1],
                       glsl::length(normals[3 * j + 1]) < kEps ? 0.0f : 1.0f, 0.0f, staticGeometry);
    SubmitStaticVertex(pivot, normals[3 * j + 2],
                       glsl::length(normals[3 * j + 2]) < kEps ? 0.0f : 1.0f, 0.0f, staticGeometry);

    for (int k = 0; k < 3; k++)
      SubmitDynamicVertex(uv, dynamicGeometry);
  }
}

} // namespace

TrafficHandle::TrafficHandle(traffic::TrafficInfo::RoadSegmentId const & segmentId, RoadClass const & roadClass,
                             m2::RectD const & boundingBox, glsl::vec2 const & texCoord, size_t verticesCount)
  : OverlayHandle(FeatureID(), dp::Anchor::Center, 0, false)
  , m_segmentId(segmentId)
  , m_roadClass(roadClass)
  , m_boundingBox(boundingBox)
  , m_needUpdate(false)
{
  ASSERT_NOT_EQUAL(verticesCount, 0, ());
  m_buffer.resize(verticesCount);
  for (size_t i = 0; i < m_buffer.size(); i++)
    m_buffer[i] = texCoord;
}

void TrafficHandle::GetAttributeMutation(ref_ptr<dp::AttributeBufferMutator> mutator) const
{
  if (!m_needUpdate)
    return;

  TOffsetNode const & node = GetOffsetNode(kDynamicStreamID);
  ASSERT(node.first.GetElementSize() == sizeof(TrafficDynamicVertex), ());
  ASSERT(node.second.m_count == m_buffer.size(), ());

  uint32_t const byteCount = m_buffer.size() * sizeof(TrafficDynamicVertex);
  void * buffer = mutator->AllocateMutationBuffer(byteCount);
  memcpy(buffer, m_buffer.data(), byteCount);

  dp::MutateNode mutateNode;
  mutateNode.m_region = node.second;
  mutateNode.m_data = make_ref(buffer);
  mutator->AddMutation(node.first, mutateNode);

  m_needUpdate = false;
}

bool TrafficHandle::Update(ScreenBase const & screen)
{
  UNUSED_VALUE(screen);
  return true;
}

bool TrafficHandle::IndexesRequired() const
{
  return false;
}

m2::RectD TrafficHandle::GetPixelRect(ScreenBase const & screen, bool perspective) const
{
  UNUSED_VALUE(screen);
  UNUSED_VALUE(perspective);
  return m2::RectD();
}

void TrafficHandle::GetPixelShape(ScreenBase const & screen, bool perspective, Rects & rects) const
{
  UNUSED_VALUE(screen);
  UNUSED_VALUE(perspective);
}

void TrafficHandle::SetTexCoord(glsl::vec2 const & texCoord)
{
  for (size_t i = 0; i < m_buffer.size(); i++)
    m_buffer[i] = texCoord;
  m_needUpdate = true;
}

traffic::TrafficInfo::RoadSegmentId const & TrafficHandle::GetSegmentId() const
{
  return m_segmentId;
}

RoadClass const & TrafficHandle::GetRoadClass() const
{
  return m_roadClass;
}

m2::RectD const & TrafficHandle::GetBoundingBox() const
{
  return m_boundingBox;
}

void TrafficKnownFeatures::Update(TrafficSegmentsColoring & coloring)
{
  auto tmp = make_shared<set<FeatureID>>();
  for (auto const & mwmPair : coloring)
    for (auto const & segmentPair : mwmPair.second)
      tmp->insert(FeatureID(mwmPair.first, segmentPair.first.m_fid));

  atomic_exchange(&m_knownTrafficFeatures, tmp);
}

bool TrafficKnownFeatures::IsFeatureKnown(FeatureID const & featureId) const
{
  auto tmp = atomic_load(&m_knownTrafficFeatures);
  return tmp.get() == nullptr || tmp->find(featureId) != tmp->end();
}

void TrafficGenerator::Init()
{
  int constexpr kBatchersCount = 3;
  int constexpr kBatchSize = 65000;
  m_batchersPool = make_unique_dp<BatchersPool<TrafficBatcherKey, TrafficBatcherKeyComparator>>(
                                  kBatchersCount, bind(&TrafficGenerator::FlushGeometry, this, _1, _2, _3),
                                  kBatchSize, kBatchSize);
}

void TrafficGenerator::ClearGLDependentResources()
{
  ClearCache();
  m_batchersPool.reset();
}

void TrafficGenerator::FlushSegmentsGeometry(TileKey const & tileKey, TrafficSegmentsGeometry const & geom,
                                             ref_ptr<dp::TextureManager> textures)
{
  FillColorsCache(textures);
  ASSERT(m_colorsCacheValid, ());
  auto const texture = m_colorsCache[static_cast<size_t>(traffic::SpeedGroup::G0)].GetTexture();

  dp::GLState state(gpu::TRAFFIC_PROGRAM, dp::GLState::GeometryLayer);
  state.SetColorTexture(texture);
  state.SetMaskTexture(textures->GetTrafficArrowTexture());

  dp::GLState lineState(gpu::TRAFFIC_LINE_PROGRAM, dp::GLState::GeometryLayer);
  lineState.SetColorTexture(texture);
  lineState.SetDrawAsLine(true);

  static vector<RoadClass> const kRoadClasses = {RoadClass::Class0, RoadClass::Class1, RoadClass::Class2};
  static float const kDepths[] = {2.0f, 1.0f, 0.0f};
  static vector<int> const kGenerateCapsZoomLevel = {14, 14, 16};
  static vector<int> const kLineDrawerRoadClass1 = {12, 13, 14};
  static vector<int> const kLineDrawerRoadClass2 = {15, 16};

  for (auto geomIt = geom.begin(); geomIt != geom.end(); ++geomIt)
  {
    auto coloringIt = m_coloring.find(geomIt->first);
    if (coloringIt != m_coloring.end())
    {
      for (auto const & roadClass : kRoadClasses)
        m_batchersPool->ReserveBatcher(TrafficBatcherKey(geomIt->first, tileKey, roadClass));

      auto & coloring = coloringIt->second;
      for (size_t i = 0; i < geomIt->second.size(); i++)
      {
        traffic::TrafficInfo::RoadSegmentId const & sid = geomIt->second[i].first;
        auto segmentColoringIt = coloring.find(sid);
        if (segmentColoringIt != coloring.end())
        {
          // We do not generate geometry for unknown segments.
          if (segmentColoringIt->second == traffic::SpeedGroup::Unknown)
            continue;

          TrafficSegmentGeometry const & g = geomIt->second[i].second;
          ref_ptr<dp::Batcher> batcher = m_batchersPool->GetBatcher(TrafficBatcherKey(geomIt->first, tileKey, g.m_roadClass));

          float const depth = kDepths[static_cast<size_t>(g.m_roadClass)];

          ASSERT(m_colorsCacheValid, ());
          dp::TextureManager::ColorRegion const & colorRegion = m_colorsCache[static_cast<size_t>(segmentColoringIt->second)];
          glsl::vec2 const uv = glsl::ToVec2(colorRegion.GetTexRect().Center());

          bool generatedAsLine = false;
          vector<int> const * lineDrawer = nullptr;
          if (g.m_roadClass == RoadClass::Class1)
            lineDrawer = &kLineDrawerRoadClass1;
          else if (g.m_roadClass == RoadClass::Class2)
            lineDrawer = &kLineDrawerRoadClass2;

          if (lineDrawer != nullptr)
          {
            auto lineDrawerIt = find(lineDrawer->begin(), lineDrawer->end(), tileKey.m_zoomLevel);
            int const w = static_cast<int>(TrafficRenderer::GetPixelWidth(g.m_roadClass, tileKey.m_zoomLevel));
            if (lineDrawerIt != lineDrawer->end() && w > 0 && w <= dp::SupportManager::Instance().GetMaxLineWidth())
            {
              vector<TrafficLineStaticVertex> staticGeometry;
              vector<TrafficDynamicVertex> dynamicGeometry;
              GenerateLineSegment(colorRegion, g.m_polyline, tileKey.GetGlobalRect().Center(), depth,
                                  staticGeometry, dynamicGeometry);
              ASSERT_EQUAL(staticGeometry.size(), dynamicGeometry.size(), ());

              if ((staticGeometry.size() + dynamicGeometry.size()) == 0)
                continue;

              drape_ptr<dp::OverlayHandle> handle = make_unique_dp<TrafficHandle>(sid, g.m_roadClass, g.m_polyline.GetLimitRect(), uv,
                                                                                  staticGeometry.size());
              dp::AttributeProvider provider(2 /* stream count */, staticGeometry.size());
              provider.InitStream(0 /* stream index */, GetTrafficLineStaticBindingInfo(), make_ref(staticGeometry.data()));
              provider.InitStream(1 /* stream index */, GetTrafficDynamicBindingInfo(), make_ref(dynamicGeometry.data()));

              dp::GLState curLineState = lineState;
              curLineState.SetLineWidth(w * df::VisualParams::Instance().GetVisualScale());
              batcher->InsertLineStrip(curLineState, make_ref(&provider), move(handle));
              generatedAsLine = true;
            }
          }

          if (!generatedAsLine)
          {
            vector<TrafficStaticVertex> staticGeometry;
            vector<TrafficDynamicVertex> dynamicGeometry;
            bool const generateCaps = (tileKey.m_zoomLevel > kGenerateCapsZoomLevel[static_cast<uint32_t>(g.m_roadClass)]);
            GenerateSegment(colorRegion, g.m_polyline, tileKey.GetGlobalRect().Center(), generateCaps, depth,
                            staticGeometry, dynamicGeometry);
            ASSERT_EQUAL(staticGeometry.size(), dynamicGeometry.size(), ());

            if ((staticGeometry.size() + dynamicGeometry.size()) == 0)
              continue;

            drape_ptr<dp::OverlayHandle> handle = make_unique_dp<TrafficHandle>(sid, g.m_roadClass, g.m_polyline.GetLimitRect(), uv,
                                                                                staticGeometry.size());
            dp::AttributeProvider provider(2 /* stream count */, staticGeometry.size());
            provider.InitStream(0 /* stream index */, GetTrafficStaticBindingInfo(), make_ref(staticGeometry.data()));
            provider.InitStream(1 /* stream index */, GetTrafficDynamicBindingInfo(), make_ref(dynamicGeometry.data()));
            batcher->InsertTriangleList(state, make_ref(&provider), move(handle));
          }
        }
      }

      for (auto const & roadClass : kRoadClasses)
        m_batchersPool->ReleaseBatcher(TrafficBatcherKey(geomIt->first, tileKey, roadClass));
    }
  }

  GLFunctions::glFlush();
}

bool TrafficGenerator::UpdateColoring(TrafficSegmentsColoring const & coloring)
{
  bool hasNew = false;
  for (auto it = coloring.begin(); it != coloring.end(); ++it)
  {
    if (!hasNew && m_coloring.find(it->first) == m_coloring.end())
      hasNew = true;
    m_coloring[it->first] = it->second;
  }
  return hasNew;
}

void TrafficGenerator::ClearCache()
{
  InvalidateTexturesCache();
  m_coloring.clear();
}

void TrafficGenerator::ClearCache(MwmSet::MwmId const & mwmId)
{
  m_coloring.erase(mwmId);
}

void TrafficGenerator::InvalidateTexturesCache()
{
  m_colorsCacheValid = false;
}

void TrafficGenerator::FlushGeometry(TrafficBatcherKey const & key, dp::GLState const & state,
                                     drape_ptr<dp::RenderBucket> && buffer)
{
  TrafficRenderData renderData(state);
  renderData.m_bucket = move(buffer);
  renderData.m_mwmId = key.m_mwmId;
  renderData.m_tileKey = key.m_tileKey;
  m_flushRenderDataFn(move(renderData));
}

void TrafficGenerator::GenerateSegment(dp::TextureManager::ColorRegion const & colorRegion,
                                       m2::PolylineD const & polyline, m2::PointD const & tileCenter,
                                       bool generateCaps, float depth, vector<TrafficStaticVertex> & staticGeometry,
                                       vector<TrafficDynamicVertex> & dynamicGeometry)
{
  vector<m2::PointD> const & path = polyline.GetPoints();
  ASSERT_GREATER(path.size(), 1, ());

  size_t const kAverageSize = path.size() * 4;
  size_t const kAverageCapSize = 24;
  staticGeometry.reserve(staticGeometry.size() + kAverageSize + kAverageCapSize * 2);
  dynamicGeometry.reserve(dynamicGeometry.size() + kAverageSize + kAverageCapSize * 2);

  // Build geometry.
  glsl::vec2 firstPoint, firstTangent, firstLeftNormal, firstRightNormal;
  glsl::vec2 lastPoint, lastTangent, lastLeftNormal, lastRightNormal;
  bool firstFilled = false;

  glsl::vec2 const uv = glsl::ToVec2(colorRegion.GetTexRect().Center());
  for (size_t i = 1; i < path.size(); ++i)
  {
    if (path[i].EqualDxDy(path[i - 1], 1.0E-5))
      continue;

    glsl::vec2 const p1 = glsl::ToVec2(MapShape::ConvertToLocal(path[i - 1], tileCenter, kShapeCoordScalar));
    glsl::vec2 const p2 = glsl::ToVec2(MapShape::ConvertToLocal(path[i], tileCenter, kShapeCoordScalar));
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
    float const maskSize = (path[i] - path[i - 1]).Length();

    glsl::vec3 const startPivot = glsl::vec3(p1, depth);
    glsl::vec3 const endPivot = glsl::vec3(p2, depth);
    SubmitStaticVertex(startPivot, rightNormal, -1.0f, 0.0f, staticGeometry);
    SubmitStaticVertex(startPivot, leftNormal, 1.0f, 0.0f, staticGeometry);
    SubmitStaticVertex(endPivot, rightNormal, -1.0f, maskSize, staticGeometry);
    SubmitStaticVertex(endPivot, rightNormal, -1.0f, maskSize, staticGeometry);
    SubmitStaticVertex(startPivot, leftNormal, 1.0f, 0.0f, staticGeometry);
    SubmitStaticVertex(endPivot, leftNormal, 1.0f, maskSize, staticGeometry);
    for (int j = 0; j < 6; j++)
      SubmitDynamicVertex(uv, dynamicGeometry);
  }

  // Generate caps.
  if (generateCaps && firstFilled)
  {
    int const kSegmentsCount = 4;
    vector<glsl::vec2> normals;
    normals.reserve(kAverageCapSize);
    GenerateCapNormals(dp::RoundCap, firstLeftNormal, firstRightNormal, -firstTangent,
                       1.0f, true /* isStart */, normals, kSegmentsCount);
    GenerateCapTriangles(glsl::vec3(firstPoint, depth), normals, colorRegion,
                         staticGeometry, dynamicGeometry);

    normals.clear();
    GenerateCapNormals(dp::RoundCap, lastLeftNormal, lastRightNormal, lastTangent,
                       1.0f, false /* isStart */, normals, kSegmentsCount);
    GenerateCapTriangles(glsl::vec3(lastPoint, depth), normals, colorRegion,
                         staticGeometry, dynamicGeometry);
  }
}

void TrafficGenerator::GenerateLineSegment(dp::TextureManager::ColorRegion const & colorRegion,
                                           m2::PolylineD const & polyline, m2::PointD const & tileCenter,
                                           float depth, vector<TrafficLineStaticVertex> & staticGeometry,
                                           vector<TrafficDynamicVertex> & dynamicGeometry)
{
  vector<m2::PointD> const & path = polyline.GetPoints();
  ASSERT_GREATER(path.size(), 1, ());

  size_t const kAverageSize = path.size();
  staticGeometry.reserve(staticGeometry.size() + kAverageSize);
  dynamicGeometry.reserve(dynamicGeometry.size() + kAverageSize);

  // Build geometry.
  glsl::vec2 const uv = glsl::ToVec2(colorRegion.GetTexRect().Center());
  for (size_t i = 0; i < path.size(); ++i)
  {
    glsl::vec2 const p = glsl::ToVec2(MapShape::ConvertToLocal(path[i], tileCenter, kShapeCoordScalar));
    staticGeometry.emplace_back(glsl::vec3(p, depth));
    SubmitDynamicVertex(uv, dynamicGeometry);
  }
}

// static
df::ColorConstant TrafficGenerator::GetColorBySpeedGroup(traffic::SpeedGroup const & speedGroup)
{
  size_t constexpr kSpeedGroupsCount = static_cast<size_t>(traffic::SpeedGroup::Count);
  static array<df::ColorConstant, kSpeedGroupsCount> const colorMap
  {{
    df::TrafficG0,
    df::TrafficG1,
    df::TrafficG2,
    df::TrafficG3,
    df::TrafficG4,
    df::TrafficG5,
    df::TrafficTempBlock,
    df::TrafficUnknown,
  }};

  size_t const index = static_cast<size_t>(speedGroup);
  ASSERT_LESS(index, kSpeedGroupsCount, ());
  return colorMap[index];
}

void TrafficGenerator::FillColorsCache(ref_ptr<dp::TextureManager> textures)
{
  size_t constexpr kSpeedGroupsCount = static_cast<size_t>(traffic::SpeedGroup::Count);
  if (!m_colorsCacheValid)
  {
    auto const & style = GetStyleReader().GetCurrentStyle();
    for (size_t i = 0; i < kSpeedGroupsCount; i++)
    {
      dp::TextureManager::ColorRegion colorRegion;
      auto const colorConstant = GetColorBySpeedGroup(static_cast<traffic::SpeedGroup>(i));
      textures->GetColorRegion(df::GetColorConstant(style, colorConstant), colorRegion);
      m_colorsCache[i] = colorRegion;
    }
    m_colorsCacheValid = true;
    m_colorsCacheRefreshed = true;
  }
}

TrafficTexCoords TrafficGenerator::ProcessCacheRefreshing()
{
  TrafficTexCoords result;
  for (size_t i = 0; i < m_colorsCache.size(); i++)
    result[i] = glsl::ToVec2(m_colorsCache[i].GetTexRect().Center());
  m_colorsCacheRefreshed = false;
  return result;
}

} // namespace df

