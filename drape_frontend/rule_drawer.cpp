#include "drape_frontend/rule_drawer.hpp"

#include "drape_frontend/apply_feature_functors.hpp"
#include "drape_frontend/engine_context.hpp"
#include "drape_frontend/stylist.hpp"
#include "drape_frontend/traffic_renderer.hpp"
#include "drape_frontend/visual_params.hpp"

#include "indexer/feature.hpp"
#include "indexer/feature_algo.hpp"
#include "indexer/feature_visibility.hpp"
#include "indexer/scales.hpp"
#include "indexer/ftypes_matcher.hpp"

#include "base/assert.hpp"
#include "base/logging.hpp"

#include "std/bind.hpp"

//#define DRAW_TILE_NET

#ifdef DRAW_TILE_NET
#include "drape_frontend/line_shape.hpp"
#include "drape_frontend/text_shape.hpp"

#include "base/string_utils.hpp"
#endif

namespace
{
int constexpr kOutlineMinZoomLevel = 16;

df::BaseApplyFeature::HotelData ExtractHotelData(FeatureType const & f)
{
  df::BaseApplyFeature::HotelData result;
  if (ftypes::IsBookingChecker::Instance()(f))
  {
    result.m_isHotel = true;
    result.m_rating = f.GetMetadata().Get(feature::Metadata::FMD_RATING);
    strings::to_int(f.GetMetadata().Get(feature::Metadata::FMD_STARS), result.m_stars);
    strings::to_int(f.GetMetadata().Get(feature::Metadata::FMD_PRICE_RATE), result.m_priceCategory);
  }
  return result;
}

void ExtractTrafficGeometry(FeatureType const & f, df::RoadClass const & roadClass,
                            m2::PolylineD const & polyline, bool oneWay, int zoomLevel,
                            double pixelToGlobalScale, df::TrafficSegmentsGeometry & geometry)
{
  if (polyline.GetSize() < 2)
    return;

  auto const & regionData = f.GetID().m_mwmId.GetInfo()->GetRegionData();
  bool const isLeftHandTraffic = regionData.Get(feature::RegionData::RD_DRIVING) == "l";

  // Calculate road offset for two-way roads. The offset is available since a zoom level in
  // kMinOffsetZoomLevels.
  double twoWayOffset = 0.0;
  double const kOffsetScalar = 0.5 * df::VisualParams::Instance().GetVisualScale();
  static vector<int> const kMinOffsetZoomLevels = { 13, 11, 10 };
  bool const needTwoWayOffset = !oneWay && zoomLevel > kMinOffsetZoomLevels[static_cast<int>(roadClass)];
  if (needTwoWayOffset)
    twoWayOffset = kOffsetScalar * pixelToGlobalScale * df::TrafficRenderer::GetPixelWidth(roadClass, zoomLevel);

  static vector<uint8_t> directions = {traffic::TrafficInfo::RoadSegmentId::kForwardDirection,
                                       traffic::TrafficInfo::RoadSegmentId::kReverseDirection};
  auto & segments = geometry[f.GetID().m_mwmId];
  segments.reserve(segments.size() + directions.size() * (polyline.GetSize() - 1));
  for (uint16_t segIndex = 0; segIndex + 1 < static_cast<uint16_t>(polyline.GetSize()); ++segIndex)
  {
    for (size_t dirIndex = 0; dirIndex < directions.size(); ++dirIndex)
    {
      if (oneWay && dirIndex > 0)
        break;

      auto const sid = traffic::TrafficInfo::RoadSegmentId(f.GetID().m_index, segIndex, directions[dirIndex]);
      bool isReversed = (directions[dirIndex] == traffic::TrafficInfo::RoadSegmentId::kReverseDirection);
      if (isLeftHandTraffic)
        isReversed = !isReversed;

      auto const segment = polyline.ExtractSegment(segIndex, isReversed);
      ASSERT_EQUAL(segment.size(), 2, ());

      // Skip zero-length segments.
      double const kEps = 1e-5;
      if (segment[0].EqualDxDy(segment[1], kEps))
        break;

      if (needTwoWayOffset)
      {
        m2::PointD const tangent = (segment[1] - segment[0]).Normalize();
        m2::PointD const normal = isLeftHandTraffic ? m2::PointD(-tangent.y, tangent.x) :
                                                      m2::PointD(tangent.y, -tangent.x);
        m2::PolylineD segmentPolyline(vector<m2::PointD>{segment[0] + normal * twoWayOffset,
                                                         segment[1] + normal * twoWayOffset});
        segments.emplace_back(sid, df::TrafficSegmentGeometry(move(segmentPolyline), roadClass));
      }
      else
      {
        m2::PolylineD segmentPolyline(vector<m2::PointD>{segment[0], segment[1]});
        segments.emplace_back(sid, df::TrafficSegmentGeometry(move(segmentPolyline), roadClass));
      }
    }
  }
}

} //  namespace

namespace df
{

RuleDrawer::RuleDrawer(TDrawerCallback const & fn,
                       TCheckCancelledCallback const & checkCancelled,
                       TIsCountryLoadedByNameFn const & isLoadedFn,
                       ref_ptr<EngineContext> context,
                       bool is3dBuildings, bool trafficEnabled)
  : m_callback(fn)
  , m_checkCancelled(checkCancelled)
  , m_isLoadedFn(isLoadedFn)
  , m_context(context)
  , m_is3dBuidings(is3dBuildings)
  , m_trafficEnabled(trafficEnabled)
  , m_wasCancelled(false)
{
  ASSERT(m_callback != nullptr, ());
  ASSERT(m_checkCancelled != nullptr, ());

  m_globalRect = m_context->GetTileKey().GetGlobalRect();

  int32_t tileSize = df::VisualParams::Instance().GetTileSize();
  m2::RectD const r = m_context->GetTileKey().GetGlobalRect(false /* clipByDataMaxZoom */);
  ScreenBase geometryConvertor;
  geometryConvertor.OnSize(0, 0, tileSize, tileSize);
  geometryConvertor.SetFromRect(m2::AnyRectD(r));
  m_currentScaleGtoP = 1.0f / geometryConvertor.GetScale();

  int const kAverageOverlaysCount = 200;
  m_mapShapes[df::OverlayType].reserve(kAverageOverlaysCount);
}

RuleDrawer::~RuleDrawer()
{
  if (m_wasCancelled)
    return;

  for (auto const & shape : m_mapShapes[df::OverlayType])
    shape->Prepare(m_context->GetTextureManager());

  if (!m_mapShapes[df::OverlayType].empty())
  {
    TMapShapes overlayShapes;
    overlayShapes.swap(m_mapShapes[df::OverlayType]);
    m_context->FlushOverlays(move(overlayShapes));
  }

  m_context->FlushTrafficGeometry(move(m_trafficGeometry));
}

bool RuleDrawer::CheckCancelled()
{
  m_wasCancelled = m_checkCancelled();
  return m_wasCancelled;
}

void RuleDrawer::operator()(FeatureType const & f)
{
  if (CheckCancelled())
    return;

  Stylist s;
  m_callback(f, s);

  if (s.IsEmpty())
    return;

  int const zoomLevel = m_context->GetTileKey().m_zoomLevel;

  if (s.IsCoastLine() &&
      zoomLevel > scales::GetUpperWorldScale() &&
      f.GetID().m_mwmId.GetInfo()->GetType() == MwmInfo::COASTS)
  {
    string name;
    if (f.GetName(StringUtf8Multilang::kDefaultCode, name))
    {
      ASSERT(!name.empty(), ());
      strings::SimpleTokenizer iter(name, ";");
      while (iter)
      {
        if (m_isLoadedFn(*iter))
          return;
        ++iter;
      }
    }
  }

  // FeatureType::GetLimitRect call invokes full geometry reading and decoding.
  // That's why this code follows after all lightweight return options.
  m2::RectD const limitRect = f.GetLimitRect(zoomLevel);
  if (!m_globalRect.IsIntersect(limitRect))
    return;

#ifdef DEBUG
  // Validate on feature styles
  if (s.AreaStyleExists() == false)
  {
    int checkFlag = s.PointStyleExists() ? 1 : 0;
    checkFlag += s.LineStyleExists() ? 1 : 0;
    ASSERT(checkFlag == 1, ());
  }
#endif

  int minVisibleScale = 0;
  auto insertShape = [this, &minVisibleScale](drape_ptr<MapShape> && shape)
  {
    int const index = static_cast<int>(shape->GetType());
    ASSERT_LESS(index, m_mapShapes.size(), ());

    shape->SetFeatureMinZoom(minVisibleScale);
    m_mapShapes[index].push_back(move(shape));
  };

  if (s.AreaStyleExists())
  {
    bool isBuilding = false;
    if (f.GetLayer() >= 0)
    {
      // Looks like nonsense, but there are some osm objects with types
      // highway-path-bridge and building (sic!) at the same time (pedestrian crossing).
      isBuilding = (ftypes::IsBuildingChecker::Instance()(f) ||
                    ftypes::IsBuildingPartChecker::Instance()(f)) &&
                    !ftypes::IsBridgeChecker::Instance()(f) &&
                    !ftypes::IsTunnelChecker::Instance()(f);
    }
    bool const is3dBuilding = m_is3dBuidings && isBuilding;

    m2::PointD featureCenter;

    float areaHeight = 0.0f;
    float areaMinHeight = 0.0f;
    if (is3dBuilding)
    {
      feature::Metadata const & md = f.GetMetadata();

      constexpr double kDefaultHeightInMeters = 3.0;
      constexpr double kMetersPerLevel = 3.0;
      double heightInMeters = kDefaultHeightInMeters;

      string value = md.Get(feature::Metadata::FMD_HEIGHT);
      if (!value.empty())
      {
        strings::to_double(value, heightInMeters);
      }
      else
      {
        value = md.Get(feature::Metadata::FMD_BUILDING_LEVELS);
        if (!value.empty())
        {
          if (strings::to_double(value, heightInMeters))
            heightInMeters *= kMetersPerLevel;
        }
      }

      value = md.Get(feature::Metadata::FMD_MIN_HEIGHT);
      double minHeigthInMeters = 0.0;
      if (!value.empty())
        strings::to_double(value, minHeigthInMeters);

      featureCenter = feature::GetCenter(f, zoomLevel);
      double const lon = MercatorBounds::XToLon(featureCenter.x);
      double const lat = MercatorBounds::YToLat(featureCenter.y);

      m2::RectD rectMercator = MercatorBounds::MetresToXY(lon, lat, heightInMeters);
      areaHeight = (rectMercator.SizeX() + rectMercator.SizeY()) / 2.0;

      rectMercator = MercatorBounds::MetresToXY(lon, lat, minHeigthInMeters);
      areaMinHeight = (rectMercator.SizeX() + rectMercator.SizeY()) / 2.0;
    }

    bool applyPointStyle = s.PointStyleExists();
    if (applyPointStyle)
    {
      if (!is3dBuilding)
        featureCenter = feature::GetCenter(f, zoomLevel);
      applyPointStyle = m_globalRect.IsPointInside(featureCenter);
    }

    if (applyPointStyle || is3dBuilding)
      minVisibleScale = feature::GetMinDrawableScale(f);

    bool const generateOutline = (zoomLevel >= kOutlineMinZoomLevel);
    ApplyAreaFeature apply(m_globalRect.Center(), insertShape, f.GetID(), m_globalRect,
                           isBuilding, areaMinHeight, areaHeight, minVisibleScale,
                           f.GetRank(), generateOutline, s.GetCaptionDescription());
    f.ForEachTriangle(apply, zoomLevel);
    apply.SetHotelData(ExtractHotelData(f));
    if (applyPointStyle)
      apply(featureCenter, true /* hasArea */);

    if (CheckCancelled())
      return;

    s.ForEachRule(bind(&ApplyAreaFeature::ProcessRule, &apply, _1));
    apply.Finish();
  }
  else if (s.LineStyleExists())
  {
    ApplyLineFeature apply(m_globalRect.Center(), m_currentScaleGtoP, insertShape, f.GetID(),
                           m_globalRect, minVisibleScale, f.GetRank(), s.GetCaptionDescription(),
                           zoomLevel, f.GetPointsCount());
    f.ForEachPoint(apply, zoomLevel);

    if (CheckCancelled())
      return;

    if (apply.HasGeometry())
      s.ForEachRule(bind(&ApplyLineFeature::ProcessRule, &apply, _1));
    apply.Finish();

    if (m_trafficEnabled && zoomLevel >= kRoadClass0ZoomLevel &&
        m_context->GetKnownTrafficFeatures().IsFeatureKnown(f.GetID()))
    {
      struct Checker
      {
        vector<ftypes::HighwayClass> m_highwayClasses;
        int m_zoomLevel;
        df::RoadClass m_roadClass;
      };
      static Checker const checkers[] = {
        {{ftypes::HighwayClass::Trunk, ftypes::HighwayClass::Primary},
         kRoadClass0ZoomLevel, df::RoadClass::Class0},
        {{ftypes::HighwayClass::Secondary, ftypes::HighwayClass::Tertiary},
         kRoadClass1ZoomLevel, df::RoadClass::Class1},
        {{ftypes::HighwayClass::LivingStreet, ftypes::HighwayClass::Service},
         kRoadClass2ZoomLevel, df::RoadClass::Class2}
      };

      bool const oneWay = ftypes::IsOneWayChecker::Instance()(f);
      auto const highwayClass = ftypes::GetHighwayClass(f);
      double const pixelToGlobalScale = 1.0 / m_currentScaleGtoP;
      for (size_t i = 0; i < ARRAY_SIZE(checkers); ++i)
      {
        auto const & classes = checkers[i].m_highwayClasses;
        if (find(classes.begin(), classes.end(), highwayClass) != classes.end() &&
            zoomLevel >= checkers[i].m_zoomLevel)
        {
          vector<m2::PointD> points;
          points.reserve(f.GetPointsCount());
          f.ResetGeometry();
          f.ForEachPoint([&points](m2::PointD const & p) { points.emplace_back(p); },
                         FeatureType::BEST_GEOMETRY);
          ExtractTrafficGeometry(f, checkers[i].m_roadClass, m2::PolylineD(points), oneWay,
                                 zoomLevel, pixelToGlobalScale, m_trafficGeometry);
          break;
        }
      }
    }
  }
  else
  {
    ASSERT(s.PointStyleExists(), ());

    minVisibleScale = feature::GetMinDrawableScale(f);
    ApplyPointFeature apply(m_globalRect.Center(), insertShape, f.GetID(), minVisibleScale, f.GetRank(),
                            s.GetCaptionDescription(), 0.0f /* posZ */);
    apply.SetHotelData(ExtractHotelData(f));
    f.ForEachPoint([&apply](m2::PointD const & pt) { apply(pt, false /* hasArea */); }, zoomLevel);

    if (CheckCancelled())
      return;

    s.ForEachRule(bind(&ApplyPointFeature::ProcessRule, &apply, _1));
    apply.Finish();
  }

#ifdef DRAW_TILE_NET
  TileKey key = m_context->GetTileKey();
  m2::RectD r = key.GetGlobalRect();
  vector<m2::PointD> path;
  path.push_back(r.LeftBottom());
  path.push_back(r.LeftTop());
  path.push_back(r.RightTop());
  path.push_back(r.RightBottom());
  path.push_back(r.LeftBottom());

  m2::SharedSpline spline(path);
  df::LineViewParams p;
  p.m_tileCenter = m_globalRect.Center();
  p.m_baseGtoPScale = 1.0;
  p.m_cap = dp::ButtCap;
  p.m_color = dp::Color::Red();
  p.m_depth = 20000;
  p.m_width = 5;
  p.m_join = dp::RoundJoin;

  insertShape(make_unique_dp<LineShape>(spline, p));

  df::TextViewParams tp;
  tp.m_tileCenter = m_globalRect.Center();
  tp.m_anchor = dp::Center;
  tp.m_depth = 20000;
  tp.m_primaryText = strings::to_string(key.m_x) + " " +
                     strings::to_string(key.m_y) + " " +
                     strings::to_string(key.m_zoomLevel);

  tp.m_primaryTextFont = dp::FontDecl(dp::Color::Red(), 30);

  drape_ptr<TextShape> textShape = make_unique_dp<TextShape>(r.Center(), tp, false, 0, false);
  textShape->DisableDisplacing();
  insertShape(move(textShape));
#endif

  if (CheckCancelled())
    return;

  for (auto const & shape : m_mapShapes[df::GeometryType])
    shape->Prepare(m_context->GetTextureManager());

  if (!m_mapShapes[df::GeometryType].empty())
  {
    TMapShapes geomShapes;
    geomShapes.swap(m_mapShapes[df::GeometryType]);
    m_context->Flush(move(geomShapes));
  }
}

} // namespace df
