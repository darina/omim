#pragma once

#include "base/thread.hpp"

#include "drape_frontend/gui/layer_render.hpp"

#include "drape_frontend/backend_renderer.hpp"
#include "drape_frontend/base_renderer.hpp"
#include "drape_frontend/drape_api_renderer.hpp"
#include "drape_frontend/frame_values.hpp"
#include "drape_frontend/gps_track_renderer.hpp"
#include "drape_frontend/my_position_controller.hpp"
#include "drape_frontend/navigator.hpp"
#include "drape_frontend/overlays_tracker.hpp"
#include "drape_frontend/render_group.hpp"
#include "drape_frontend/render_state_extension.hpp"
#include "drape_frontend/requested_tiles.hpp"
#include "drape_frontend/route_renderer.hpp"
#include "drape_frontend/postprocess_renderer.hpp"
#include "drape_frontend/threads_commutator.hpp"
#include "drape_frontend/tile_info.hpp"
#include "drape_frontend/traffic_renderer.hpp"
#include "drape_frontend/transit_scheme_renderer.hpp"
#include "drape_frontend/user_event_stream.hpp"

#include "shaders/program_manager.hpp"

#include "drape/overlay_tree.hpp"
#include "drape/pointers.hpp"
#include "drape/vertex_array_buffer.hpp"

#include "platform/location.hpp"

#include "geometry/screenbase.hpp"
#include "geometry/triangle2d.hpp"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

namespace dp
{
class Framebuffer;
class OverlayTree;
class RenderBucket;
}  // namespace dp

namespace df
{
class DebugRectRenderer;
class DrapeNotifier;
class ScenarioManager;
class ScreenQuadRenderer;
class SelectionShape;
class SelectObjectMessage;

struct TapInfo
{
  m2::PointD const m_mercator;
  bool const m_isLong;
  bool const m_isMyPositionTapped;
  FeatureID const m_featureTapped;

  static m2::AnyRectD GetDefaultSearchRect(m2::PointD const & mercator, ScreenBase const & screen);
  static m2::AnyRectD GetBookmarkSearchRect(m2::PointD const & mercator, ScreenBase const & screen);
  static m2::AnyRectD GetRoutingPointSearchRect(m2::PointD const & mercator, ScreenBase const & screen);
  static m2::AnyRectD GetPreciseSearchRect(m2::PointD const & mercator, double const eps);
};

class FrontendRenderer : public BaseRenderer,
                         public MyPositionController::Listener,
                         public UserEventStream::Listener
{
public:
  using ModelViewChangedHandler = std::function<void(ScreenBase const & screen)>;
  using GraphicsReadyHandler = std::function<void()>;
  using TapEventInfoHandler = std::function<void(TapInfo const &)>;
  using UserPositionChangedHandler = std::function<void(m2::PointD const & pt, bool hasPosition)>;
  using UserPositionPendingTimeoutHandler = std::function<void()>;

  struct Params : BaseRenderer::Params
  {
    Params(dp::ApiVersion apiVersion, ref_ptr<ThreadsCommutator> commutator,
           ref_ptr<dp::GraphicsContextFactory> factory, ref_ptr<dp::TextureManager> texMng,
           MyPositionController::Params && myPositionParams, dp::Viewport viewport,
           ModelViewChangedHandler && modelViewChangedHandler, TapEventInfoHandler && tapEventHandler,
           UserPositionChangedHandler && positionChangedHandler,
           UserPositionPendingTimeoutHandler && userPositionPendingTimeoutHandler,
           ref_ptr<RequestedTiles> requestedTiles,
           OverlaysShowStatsCallback && overlaysShowStatsCallback,
           bool allow3dBuildings, bool trafficEnabled, bool guidesEnabled, bool blockTapEvents,
           std::vector<PostprocessRenderer::Effect> && enabledEffects,
           OnGraphicsContextInitialized const & onGraphicsContextInitialized)
      : BaseRenderer::Params(apiVersion, commutator, factory, texMng, onGraphicsContextInitialized)
      , m_myPositionParams(std::move(myPositionParams))
      , m_viewport(viewport)
      , m_modelViewChangedHandler(std::move(modelViewChangedHandler))
      , m_tapEventHandler(std::move(tapEventHandler))
      , m_positionChangedHandler(std::move(positionChangedHandler))
      , m_userPositionPendingTimeoutHandler(std::move(userPositionPendingTimeoutHandler))
      , m_requestedTiles(requestedTiles)
      , m_overlaysShowStatsCallback(std::move(overlaysShowStatsCallback))
      , m_allow3dBuildings(allow3dBuildings)
      , m_trafficEnabled(trafficEnabled)
      , m_guidesEnabled(guidesEnabled)
      , m_blockTapEvents(blockTapEvents)
      , m_enabledEffects(std::move(enabledEffects))
    {}

    MyPositionController::Params m_myPositionParams;
    dp::Viewport m_viewport;
    ModelViewChangedHandler m_modelViewChangedHandler;
    TapEventInfoHandler m_tapEventHandler;
    UserPositionChangedHandler m_positionChangedHandler;
    UserPositionPendingTimeoutHandler m_userPositionPendingTimeoutHandler;
    ref_ptr<RequestedTiles> m_requestedTiles;
    OverlaysShowStatsCallback m_overlaysShowStatsCallback;
    bool m_allow3dBuildings;
    bool m_trafficEnabled;
    bool m_guidesEnabled;
    bool m_blockTapEvents;
    std::vector<PostprocessRenderer::Effect> m_enabledEffects;
  };

  explicit FrontendRenderer(Params && params);
  ~FrontendRenderer() override;

  void Teardown();

  void AddUserEvent(drape_ptr<UserEvent> && event);

  // MyPositionController::Listener
  void PositionChanged(m2::PointD const & position, bool hasPosition) override;
  void PositionPendingTimeout() override;
  void ChangeModelView(m2::PointD const & center, int zoomLevel,
                       TAnimationCreator const & parallelAnimCreator) override;
  void ChangeModelView(double azimuth, TAnimationCreator const & parallelAnimCreator) override;
  void ChangeModelView(m2::RectD const & rect,
                       TAnimationCreator const & parallelAnimCreator) override;
  void ChangeModelView(m2::PointD const & userPos, double azimuth, m2::PointD const & pxZero,
                       int preferredZoomLevel, Animation::TAction const & onFinishAction,
                       TAnimationCreator const & parallelAnimCreator) override;
  void ChangeModelView(double autoScale, m2::PointD const & userPos, double azimuth,
                       m2::PointD const & pxZero, TAnimationCreator const & parallelAnimCreator) override;

  drape_ptr<ScenarioManager> const & GetScenarioManager() const;

protected:
  void AcceptMessage(ref_ptr<Message> message) override;
  std::unique_ptr<threads::IRoutine> CreateRoutine() override;
  
  void RenderFrame() override;
  
  void OnContextCreate() override;
  void OnContextDestroy() override;

  void OnRenderingEnabled() override;
  void OnRenderingDisabled() override;

private:
  void OnResize(ScreenBase const & screen);
  void RenderScene(ScreenBase const & modelView, bool activeFrame);
  void PrepareBucket(dp::RenderState const & state, drape_ptr<dp::RenderBucket> & bucket);
  void RenderSingleGroup(ref_ptr<dp::GraphicsContext> context, ScreenBase const & modelView,
                         ref_ptr<BaseRenderGroup> group);
  void RefreshProjection(ScreenBase const & screen);
  void RefreshZScale(ScreenBase const & screen);
  void RefreshPivotTransform(ScreenBase const & screen);
  void RefreshBgColor();

  struct RenderLayer
  {
    std::vector<drape_ptr<RenderGroup>> m_renderGroups;
    bool m_isDirty = false;

    void Sort(ref_ptr<dp::OverlayTree> overlayTree);
  };
  // Render part of scene
  void Render2dLayer(ScreenBase const & modelView);
  void PreRender3dLayer(ScreenBase const & modelView);
  void Render3dLayer(ScreenBase const & modelView);
  void RenderOverlayLayer(ScreenBase const & modelView);
  void RenderNavigationOverlayLayer(ScreenBase const & modelView);
  void RenderUserMarksLayer(ScreenBase const & modelView, DepthLayer layerId);
  void RenderNonDisplacedUserMarksLayer(ScreenBase const & modelView, DepthLayer layerId);
  void RenderTransitSchemeLayer(ScreenBase const & modelView);
  void RenderTrafficLayer(ScreenBase const & modelView);
  void RenderRouteLayer(ScreenBase const & modelView);
  void RenderGuidesLayer(ScreenBase const & modelView);
  void RenderLayerBackground();
  void RenderEmptyFrame();

  bool HasTransitRouteData() const;
  bool HasRouteData() const;

  ScreenBase const & ProcessEvents(bool & modelViewChanged, bool & viewportChanged);
  void PrepareScene(ScreenBase const & modelView);
  void UpdateScene(ScreenBase const & modelView);
  void BuildOverlayTree(ScreenBase const & modelView);

  void EmitModelViewChanged(ScreenBase const & modelView) const;

#if defined(OMIM_OS_MAC) || defined(OMIM_OS_LINUX)
  void EmitGraphicsReady();
#endif

  TTilesCollection ResolveTileKeys(ScreenBase const & screen);
  void ResolveZoomLevel(ScreenBase const & screen);
  void UpdateDisplacementEnabled();
  void CheckIsometryMinScale(ScreenBase const & screen);

  void DisablePerspective();

  void OnTap(m2::PointD const & pt, bool isLong) override;
  void OnForceTap(m2::PointD const & pt) override;
  void OnDoubleTap(m2::PointD const & pt) override;
  void OnTwoFingersTap() override;
  bool OnSingleTouchFiltrate(m2::PointD const & pt, TouchEvent::ETouchType type) override;
  void OnDragStarted() override;
  void OnDragEnded(m2::PointD const & distance) override;

  void OnScaleStarted() override;
  void OnRotated() override;
  void CorrectScalePoint(m2::PointD & pt) const override;
  void CorrectScalePoint(m2::PointD & pt1, m2::PointD & pt2) const override;
  void CorrectGlobalScalePoint(m2::PointD & pt) const override;
  void OnScaleEnded() override;
  void OnAnimatedScaleEnded() override;
  void OnTouchMapAction(TouchEvent::ETouchType touchType, bool isMapTouch) override;
  bool OnNewVisibleViewport(m2::RectD const & oldViewport, m2::RectD const & newViewport,
                            bool needOffset, m2::PointD & gOffset) override;

  class Routine : public threads::IRoutine
  {
  public:
    Routine(FrontendRenderer & renderer);
    void Do() override;

  private:
    FrontendRenderer & m_renderer;
  };

  void ReleaseResources();
  void UpdateContextDependentResources();

  void BeginUpdateOverlayTree(ScreenBase const & modelView);
  void UpdateOverlayTree(ScreenBase const & modelView, drape_ptr<RenderGroup> & renderGroup);
  void EndUpdateOverlayTree();

  template<typename TRenderGroup>
  void AddToRenderGroup(dp::RenderState const & state, drape_ptr<dp::RenderBucket> && renderBucket,
                        TileKey const & newTile);

  using TRenderGroupRemovePredicate = std::function<bool(drape_ptr<RenderGroup> const &)>;
  void RemoveRenderGroupsLater(TRenderGroupRemovePredicate const & predicate);

  void FollowRoute(int preferredZoomLevel, int preferredZoomLevelIn3d, bool enableAutoZoom,
                   bool isArrowGlued);

  bool CheckRouteRecaching(ref_ptr<BaseSubrouteData> subrouteData);

  void InvalidateRect(m2::RectD const & gRect);
  bool CheckTileGenerations(TileKey const & tileKey);
  void UpdateCanBeDeletedStatus();

  void OnCompassTapped();

  FeatureID GetVisiblePOI(m2::PointD const & pixelPoint);
  FeatureID GetVisiblePOI(m2::RectD const & pixelRect);

  void PullToBoundArea(bool randomPlace, bool applyZoom);

  void ProcessSelection(ref_ptr<SelectObjectMessage> msg);

  void OnPrepareRouteArrows(dp::DrapeID subrouteIndex, std::vector<ArrowBorders> && borders);
  void OnCacheRouteArrows(dp::DrapeID subrouteIndex, std::vector<ArrowBorders> const & borders);

  void CollectShowOverlaysEvents();

  void CheckAndRunFirstLaunchAnimation();

  void ScheduleOverlayCollecting();

  drape_ptr<gpu::ProgramManager> m_gpuProgramManager;

  std::array<RenderLayer, static_cast<size_t>(DepthLayer::LayersCount)> m_layers;

  drape_ptr<gui::LayerRenderer> m_guiRenderer;
  gui::TWidgetsLayoutInfo m_lastWidgetsLayout;
  drape_ptr<MyPositionController> m_myPositionController;

  drape_ptr<SelectionShape> m_selectionShape;
  struct SelectionTrackInfo
  {
    SelectionTrackInfo() = default;

    SelectionTrackInfo(m2::AnyRectD const & startRect, m2::PointD const & startPos)
      : m_startRect(startRect)
      , m_startPos(startPos)
    {}

    m2::AnyRectD m_startRect;
    m2::PointD m_startPos;
    m2::PointI m_snapSides = m2::PointI::Zero();
  };
  std::optional<SelectionTrackInfo> m_selectionTrackInfo;

  drape_ptr<RouteRenderer> m_routeRenderer;
  drape_ptr<TrafficRenderer> m_trafficRenderer;
  drape_ptr<TransitSchemeRenderer> m_transitSchemeRenderer;
  drape_ptr<dp::Framebuffer> m_buildingsFramebuffer;
  drape_ptr<ScreenQuadRenderer> m_screenQuadRenderer;
  drape_ptr<GpsTrackRenderer> m_gpsTrackRenderer;
  drape_ptr<DrapeApiRenderer> m_drapeApiRenderer;

  drape_ptr<dp::OverlayTree> m_overlayTree;

  FrameValues m_frameValues;

  bool m_enablePerspectiveInNavigation;
  bool m_enable3dBuildings;
  bool m_isIsometry;

  bool m_blockTapEvents;

  bool m_choosePositionMode;
  bool m_screenshotMode;

  dp::Viewport m_viewport;
  UserEventStream m_userEventStream;
  ModelViewChangedHandler m_modelViewChangedHandler;
  TapEventInfoHandler m_tapEventInfoHandler;
  UserPositionChangedHandler m_userPositionChangedHandler;
  UserPositionPendingTimeoutHandler m_userPositionPendingTimeoutHandler;

  ScreenBase m_lastReadedModelView;
  TTilesCollection m_notFinishedTiles;

  int m_currentZoomLevel = -1;

  ref_ptr<RequestedTiles> m_requestedTiles;
  uint64_t m_maxGeneration;
  uint64_t m_maxUserMarksGeneration;

  int m_lastRecacheRouteId = 0;

  struct FollowRouteData
  {
    FollowRouteData(int preferredZoomLevel, int preferredZoomLevelIn3d, bool enableAutoZoom,
                    bool isArrowGlued)
      : m_preferredZoomLevel(preferredZoomLevel)
      , m_preferredZoomLevelIn3d(preferredZoomLevelIn3d)
      , m_enableAutoZoom(enableAutoZoom)
      , m_isArrowGlued(isArrowGlued)
    {}

    int m_preferredZoomLevel;
    int m_preferredZoomLevelIn3d;
    bool m_enableAutoZoom;
    bool m_isArrowGlued;
  };

  std::unique_ptr<FollowRouteData> m_pendingFollowRoute;

  std::vector<m2::TriangleD> m_dragBoundArea;

  drape_ptr<SelectObjectMessage> m_selectObjectMessage;

  bool m_needRestoreSize;

  bool m_trafficEnabled;
  bool m_guidesEnabled;
  bool m_transitSchemeEnabled = false;

  drape_ptr<OverlaysTracker> m_overlaysTracker;
  OverlaysShowStatsCallback m_overlaysShowStatsCallback;

  bool m_forceUpdateScene;
  bool m_forceUpdateUserMarks;

  bool m_isAntialiasingEnabled = false;
  drape_ptr<PostprocessRenderer> m_postprocessRenderer;
  std::vector<PostprocessRenderer::Effect> m_enabledOnStartEffects;

  bool m_isDebugRectRenderingEnabled = false;
  drape_ptr<DebugRectRenderer> m_debugRectRenderer;

  drape_ptr<ScenarioManager> m_scenarioManager;

  bool m_firstTilesReady = false;
  bool m_firstLaunchAnimationTriggered = false;
  bool m_firstLaunchAnimationInterrupted = false;

#if defined(OMIM_OS_MAC) || defined(OMIM_OS_LINUX)
  GraphicsReadyHandler m_graphicsReadyFn;

  enum class GraphicsStage
  {
    Unknown,
    WaitReady,
    WaitRendering,
    Rendered
  };
  GraphicsStage m_graphicsStage = GraphicsStage::Unknown;
#endif

  bool m_finishTexturesInitialization = false;
  drape_ptr<ScreenQuadRenderer> m_layerBackground;

  drape_ptr<DrapeNotifier> m_notifier;

  struct FrameData
  {
    base::Timer m_timer;
    double m_frameTime = 0.0;
    bool m_modelViewChanged = true;
    bool m_viewportChanged = true;
    uint32_t m_inactiveFramesCounter = 0;
    bool m_forceFullRedrawNextFrame = false;
#ifdef SHOW_FRAMES_STATS
    uint64_t m_framesOverall = 0;
    uint64_t m_framesFast = 0;
#endif
    static uint32_t constexpr kMaxInactiveFrames = 2;
  };
  FrameData m_frameData;

#ifdef DEBUG
  bool m_isTeardowned;
#endif
};
}  // namespace df
