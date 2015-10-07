#include "drape_frontend/animation/interpolation_holder.hpp"
#include "drape_frontend/gui/drape_gui.hpp"
#include "drape_frontend/frontend_renderer.hpp"
#include "drape_frontend/message_subclasses.hpp"
#include "drape_frontend/visual_params.hpp"
#include "drape_frontend/user_mark_shapes.hpp"

/////////////////////////////////////
#include <OpenGL/glext.h>
#include <OpenGL/gl.h>
#include "3party/stb_image/stb_image.h"
#include "math.h"
/////////////////////////////////////

#include "drape/utils/glyph_usage_tracker.hpp"
#include "drape/utils/gpu_mem_tracker.hpp"
#include "drape/utils/projection.hpp"

#include "indexer/scales.hpp"
#include "indexer/drawing_rules.hpp"

#include "geometry/any_rect2d.hpp"

#include "base/timer.hpp"
#include "base/assert.hpp"
#include "base/logging.hpp"
#include "base/stl_add.hpp"

#include "std/algorithm.hpp"
#include "std/bind.hpp"
#include "std/cmath.hpp"

namespace df
{

namespace
{

const double VSyncInterval = 0.030;
//#ifdef DEBUG
//const double VSyncInterval = 0.030;
//#else
//const double VSyncInterval = 0.014;
//#endif

} // namespace

FrontendRenderer::FrontendRenderer(Params const & params)
  : BaseRenderer(ThreadsCommutator::RenderThread, params)
  , m_gpuProgramManager(new dp::GpuProgramManager())
  , m_routeRenderer(new RouteRenderer())
  , m_overlayTree(new dp::OverlayTree())
  , m_viewport(params.m_viewport)
  , m_userEventStream(params.m_isCountryLoadedFn)
  , m_modelViewChangedFn(params.m_modelViewChangedFn)
  , m_tapEventInfoFn(params.m_tapEventFn)
  , m_userPositionChangedFn(params.m_positionChangedFn)
  , m_tileTree(new TileTree())
{
#ifdef DRAW_INFO
  m_tpf = 0,0;
  m_fps = 0.0;
#endif

  ASSERT(m_tapEventInfoFn, ());
  ASSERT(m_userPositionChangedFn, ());

  m_myPositionController.reset(new MyPositionController(params.m_initMyPositionMode));
  m_myPositionController->SetModeListener(params.m_myPositionModeCallback);

  StartThread();
}

FrontendRenderer::~FrontendRenderer()
{
  StopThread();
}

#ifdef DRAW_INFO
void FrontendRenderer::BeforeDrawFrame()
{
  m_frameStartTime = m_timer.ElapsedSeconds();
}

void FrontendRenderer::AfterDrawFrame()
{
  m_drawedFrames++;

  double elapsed = m_timer.ElapsedSeconds();
  m_tpfs.push_back(elapsed - m_frameStartTime);

  if (elapsed > 1.0)
  {
    m_timer.Reset();
    m_fps = m_drawedFrames / elapsed;
    m_drawedFrames = 0;

    m_tpf = accumulate(m_tpfs.begin(), m_tpfs.end(), 0.0) / m_tpfs.size();

    LOG(LINFO, ("Average Fps : ", m_fps));
    LOG(LINFO, ("Average Tpf : ", m_tpf));

#if defined(TRACK_GPU_MEM)
    string report = dp::GPUMemTracker::Inst().Report();
    LOG(LINFO, (report));
#endif
#if defined(TRACK_GLYPH_USAGE)
    string glyphReport = dp::GlyphUsageTracker::Instance().Report();
    LOG(LINFO, (glyphReport));
#endif
  }
}

#endif

void FrontendRenderer::AcceptMessage(ref_ptr<Message> message)
{
  switch (message->GetType())
  {
  case Message::FlushTile:
    {
      ref_ptr<FlushRenderBucketMessage> msg = message;
      dp::GLState const & state = msg->GetState();
      TileKey const & key = msg->GetKey();
      drape_ptr<dp::RenderBucket> bucket = msg->AcceptBuffer();
      ref_ptr<dp::GpuProgram> program = m_gpuProgramManager->GetProgram(state.GetProgramIndex());
      program->Bind();
      bucket->GetBuffer()->Build(program);
      if (!IsUserMarkLayer(key))
        m_tileTree->ProcessTile(key, GetCurrentZoomLevel(), state, move(bucket));
      else
        m_userMarkRenderGroups.emplace_back(make_unique_dp<UserMarkRenderGroup>(state, key, move(bucket)));
      break;
    }

  case Message::FinishReading:
    {
      ref_ptr<FinishReadingMessage> msg = message;
      m_tileTree->FinishTiles(msg->GetTiles(), GetCurrentZoomLevel());
      break;
    }

  case Message::InvalidateRect:
    {
      ref_ptr<InvalidateRectMessage> m = message;
      TTilesCollection tiles;
      ScreenBase screen = m_userEventStream.GetCurrentScreen();
      m2::RectD rect = m->GetRect();
      if (rect.Intersect(screen.ClipRect()))
      {
        m_tileTree->Invalidate();
        ResolveTileKeys(rect, tiles);

        auto eraseFunction = [&tiles](vector<drape_ptr<RenderGroup>> & groups)
        {
          vector<drape_ptr<RenderGroup> > newGroups;
          for (drape_ptr<RenderGroup> & group : groups)
          {
            if (tiles.find(group->GetTileKey()) == tiles.end())
              newGroups.push_back(move(group));
          }

          swap(groups, newGroups);
        };

        eraseFunction(m_renderGroups);
        eraseFunction(m_deferredRenderGroups);

        m_commutator->PostMessage(ThreadsCommutator::ResourceUploadThread,
                                  make_unique_dp<InvalidateReadManagerRectMessage>(tiles),
                                  MessagePriority::Normal);

        m_commutator->PostMessage(ThreadsCommutator::ResourceUploadThread,
                                  make_unique_dp<UpdateReadManagerMessage>(screen, move(tiles)),
                                  MessagePriority::Normal);
      }
      break;
    }

  case Message::ClearUserMarkLayer:
    {
      TileKey const & tileKey = ref_ptr<ClearUserMarkLayerMessage>(message)->GetKey();
      auto const functor = [&tileKey](drape_ptr<UserMarkRenderGroup> const & g)
      {
        return g->GetTileKey() == tileKey;
      };

      auto const iter = remove_if(m_userMarkRenderGroups.begin(),
                                  m_userMarkRenderGroups.end(),
                                  functor);

      m_userMarkRenderGroups.erase(iter, m_userMarkRenderGroups.end());
      break;
    }

  case Message::ChangeUserMarkLayerVisibility:
    {
      ref_ptr<ChangeUserMarkLayerVisibilityMessage> m = message;
      TileKey const & key = m->GetKey();
      if (m->IsVisible())
        m_userMarkVisibility.insert(key);
      else
        m_userMarkVisibility.erase(key);
      break;
    }

  case Message::GuiLayerRecached:
    {
      ref_ptr<GuiLayerRecachedMessage> msg = message;
      drape_ptr<gui::LayerRenderer> renderer = move(msg->AcceptRenderer());
      renderer->Build(make_ref(m_gpuProgramManager));
      if (m_guiRenderer == nullptr)
        m_guiRenderer = move(renderer);
      else
        m_guiRenderer->Merge(make_ref(renderer));
      break;
    }

  case Message::GuiLayerLayout:
    {
      ASSERT(m_guiRenderer != nullptr, ());
      m_guiRenderer->SetLayout(ref_ptr<GuiLayerLayoutMessage>(message)->GetLayoutInfo());
      break;
    }

  case Message::StopRendering:
    {
      ProcessStopRenderingMessage();
      break;
    }

  case Message::MyPositionShape:
    {
      ref_ptr<MyPositionShapeMessage> msg = message;
      m_myPositionController->SetRenderShape(msg->AcceptShape());
      m_selectionShape = msg->AcceptSelection();
    }
    break;

  case Message::ChangeMyPostitionMode:
    {
      ref_ptr<ChangeMyPositionModeMessage> msg = message;
      switch (msg->GetChangeType())
      {
      case ChangeMyPositionModeMessage::TYPE_NEXT:
        m_myPositionController->NextMode();
        break;
      case ChangeMyPositionModeMessage::TYPE_STOP_FOLLOW:
        m_myPositionController->StopLocationFollow();
        break;
      case ChangeMyPositionModeMessage::TYPE_INVALIDATE:
        m_myPositionController->Invalidate();
        break;
      case ChangeMyPositionModeMessage::TYPE_CANCEL:
        m_myPositionController->TurnOff();
        break;
      default:
        ASSERT(false, ("Unknown change type:", static_cast<int>(msg->GetChangeType())));
        break;
      }
      break;
    }

  case Message::CompassInfo:
    {
      ref_ptr<CompassInfoMessage> msg = message;
      m_myPositionController->OnCompassUpdate(msg->GetInfo(), m_userEventStream.GetCurrentScreen());
      break;
    }

  case Message::GpsInfo:
    {
      ref_ptr<GpsInfoMessage> msg = message;
      m_myPositionController->OnLocationUpdate(msg->GetInfo(), msg->IsNavigable(),
                                               m_userEventStream.GetCurrentScreen());

      location::RouteMatchingInfo const & info = msg->GetRouteInfo();
      if (info.HasDistanceFromBegin())
        m_routeRenderer->UpdateDistanceFromBegin(info.GetDistanceFromBegin());

      break;
    }

  case Message::FindVisiblePOI:
    {
      ref_ptr<FindVisiblePOIMessage> msg = message;
      msg->SetFeatureID(GetVisiblePOI(m_userEventStream.GetCurrentScreen().GtoP(msg->GetPoint())));
      break;
    }

  case Message::SelectObject:
    {
      ref_ptr<SelectObjectMessage> msg = message;

      if (msg->IsDismiss())
      {
        // m_selectionShape can be null in case of deselection
        if (m_selectionShape != nullptr)
          m_selectionShape->Hide();
      }
      else
      {
        ASSERT(m_selectionShape != nullptr, ());
        m_selectionShape->Show(msg->GetSelectedObject(), msg->GetPosition(), msg->IsAnim());
      }
      break;
    }

  case Message::GetSelectedObject:
    {
      ref_ptr<GetSelectedObjectMessage> msg = message;
      if (m_selectionShape != nullptr)
        msg->SetSelectedObject(m_selectionShape->GetSelectedObject());
      else
        msg->SetSelectedObject(SelectionShape::OBJECT_EMPTY);
      break;
    }

  case Message::GetMyPosition:
    {
      ref_ptr<GetMyPositionMessage> msg = message;
      msg->SetMyPosition(m_myPositionController->IsModeHasPosition(), m_myPositionController->Position());
      break;
    }

  case Message::FlushRoute:
    {
      ref_ptr<FlushRouteMessage> msg = message;
      drape_ptr<RouteData> routeData = msg->AcceptRouteData();
      m_routeRenderer->SetRouteData(move(routeData), make_ref(m_gpuProgramManager));

      m_myPositionController->ActivateRouting();
      break;
    }

  case Message::RemoveRoute:
    {
      ref_ptr<RemoveRouteMessage> msg = message;
      m_routeRenderer->Clear();
      if (msg->NeedDeactivateFollowing())
        m_myPositionController->DeactivateRouting();
      break;
    }

  case Message::UpdateMapStyle:
    {
      m_tileTree->Invalidate();

      TTilesCollection tiles;
      ScreenBase screen = m_userEventStream.GetCurrentScreen();
      ResolveTileKeys(screen.ClipRect(), tiles);

      m_renderGroups.clear();
      m_deferredRenderGroups.clear();

      m_commutator->PostMessage(ThreadsCommutator::ResourceUploadThread,
                                make_unique_dp<InvalidateReadManagerRectMessage>(tiles),
                                MessagePriority::Normal);

      BaseBlockingMessage::Blocker blocker;
      m_commutator->PostMessage(ThreadsCommutator::ResourceUploadThread,
                                make_unique_dp<InvalidateTexturesMessage>(blocker),
                                MessagePriority::Normal);
      blocker.Wait();

      m_commutator->PostMessage(ThreadsCommutator::ResourceUploadThread,
                                make_unique_dp<UpdateReadManagerMessage>(screen, move(tiles)),
                                MessagePriority::Normal);

      RefreshBgColor();

      break;
    }

  default:
    ASSERT(false, ());
  }
}

unique_ptr<threads::IRoutine> FrontendRenderer::CreateRoutine()
{
  return make_unique<Routine>(*this);
}

void FrontendRenderer::OnResize(ScreenBase const & screen)
{
  m_viewport.SetViewport(0, 0, screen.GetWidth(), screen.GetHeight());
  m_myPositionController->SetPixelRect(screen.PixelRect());
  m_contextFactory->getDrawContext()->resize(m_viewport.GetWidth(), m_viewport.GetHeight());
  RefreshProjection();
}

void FrontendRenderer::AddToRenderGroup(vector<drape_ptr<RenderGroup>> & groups,
                                        dp::GLState const & state,
                                        drape_ptr<dp::RenderBucket> && renderBucket,
                                        TileKey const & newTile)
{
  drape_ptr<RenderGroup> group = make_unique_dp<RenderGroup>(state, newTile);
  group->AddBucket(move(renderBucket));
  groups.push_back(move(group));
}

void FrontendRenderer::OnAddRenderGroup(TileKey const & tileKey, dp::GLState const & state,
                                        drape_ptr<dp::RenderBucket> && renderBucket)
{
  AddToRenderGroup(m_renderGroups, state, move(renderBucket), tileKey);
}

void FrontendRenderer::OnDeferRenderGroup(TileKey const & tileKey, dp::GLState const & state,
                                          drape_ptr<dp::RenderBucket> && renderBucket)
{
  AddToRenderGroup(m_deferredRenderGroups, state, move(renderBucket), tileKey);
}

void FrontendRenderer::OnActivateTile(TileKey const & tileKey)
{
  for(auto it = m_deferredRenderGroups.begin(); it != m_deferredRenderGroups.end();)
  {
    if ((*it)->GetTileKey() == tileKey)
    {
      m_renderGroups.push_back(move(*it));
      it = m_deferredRenderGroups.erase(it);
    }
    else
    {
      ++it;
    }
  }
}

void FrontendRenderer::OnRemoveTile(TileKey const & tileKey)
{
  m_overlayTree->ForceUpdate();
  for(auto const & group : m_renderGroups)
  {
    if (group->GetTileKey() == tileKey)
    {
      group->DeleteLater();
      group->Disappear();
    }
  }

  auto removePredicate = [&tileKey](drape_ptr<RenderGroup> const & group)
  {
    return group->GetTileKey() == tileKey;
  };
  m_deferredRenderGroups.erase(remove_if(m_deferredRenderGroups.begin(),
                                         m_deferredRenderGroups.end(),
                                         removePredicate),
                               m_deferredRenderGroups.end());
}

void FrontendRenderer::OnCompassTapped()
{
  m_myPositionController->StopCompassFollow();
  m_userEventStream.AddEvent(RotateEvent(0.0));
}

FeatureID FrontendRenderer::GetVisiblePOI(m2::PointD const & pixelPoint) const
{
  double halfSize = VisualParams::Instance().GetTouchRectRadius();
  m2::PointD sizePoint(halfSize, halfSize);
  m2::RectD selectRect(pixelPoint - sizePoint, pixelPoint + sizePoint);
  return GetVisiblePOI(selectRect);
}

FeatureID FrontendRenderer::GetVisiblePOI(m2::RectD const & pixelRect) const
{
  m2::PointD pt = pixelRect.Center();
  dp::OverlayTree::TSelectResult selectResult;
  m_overlayTree->Select(pixelRect, selectResult);

  double dist = numeric_limits<double>::max();
  FeatureID featureID;

  ScreenBase const & screen = m_userEventStream.GetCurrentScreen();
  for (ref_ptr<dp::OverlayHandle> handle : selectResult)
  {
    double const  curDist = pt.SquareLength(handle->GetPivot(screen));
    if (curDist < dist)
    {
      dist = curDist;
      featureID = handle->GetFeatureID();
    }
  }

  return featureID;
}

void FrontendRenderer::BeginUpdateOverlayTree(ScreenBase const & modelView)
{
  m_overlayTree->Frame();

  if (m_overlayTree->IsNeedUpdate())
    m_overlayTree->StartOverlayPlacing(modelView);
}

void FrontendRenderer::UpdateOverlayTree(ScreenBase const & modelView, drape_ptr<RenderGroup> & renderGroup)
{
  if (m_overlayTree->IsNeedUpdate())
    renderGroup->CollectOverlay(make_ref(m_overlayTree));
  else
    renderGroup->Update(modelView);
}

void FrontendRenderer::EndUpdateOverlayTree()
{
  if (m_overlayTree->IsNeedUpdate())
    m_overlayTree->EndOverlayPlacing();
}

////////////////////////////////
static GLuint g_dstTex;
static GLuint g_depthTex;
static GLuint g_program;
static GLuint g_pos_attrib;
static GLuint g_tcoord_attrib;
static GLuint g_fbo;
static GLuint g_vbo;
static GLuint g_vao;
static unsigned g_width;
static unsigned g_height;
static unsigned char * g_texData;
static int g_frameIndex;


void setProjection(array<float, 16> & result)
{
  result.fill(0.0f);

  float fovy = M_PI / 3.0f;
  float ctg_fovy = 1.0/tanf(fovy/2.0f);
  float aspect = (float)g_width / g_height;
  float near = 0.1f;
  float far = 100.0f;
  float width = g_width;
  float height = g_height;

  result[0] = ctg_fovy / aspect;
  result[5] = ctg_fovy;
  result[10] = (far + near) / (far - near);
  result[11] = 1.0f;
  result[14] = -2 * far * near / (far - near);
}

void setRotate(array<float, 16> & result)
{
  result.fill(0.0f);

  float angle = -M_PI_4;
  result[0] = 1.0f;
  result[5] = cos(angle);
  result[6] = -sin(angle);
  result[9] = sin(angle);
  result[10] = cos(angle);
  result[15] = 1.0f;
}

void setTranslate(array<float, 16> & result)
{
  result.fill(0.0f);

  float dx = 0.0f;
  float dy = 0.0f;
  float dz = 0.5f;
  result[0] = 1.0f;
  result[5] = 1.0f;
  result[10] = 1.0f;
  result[12] = dx;
  result[13] = dy;
  result[14] = dz;
  result[15] = 1.0f;
}

void initTwoPassRenderer(unsigned w, unsigned h)
{
  if (g_width == w && g_height == h)
    return;

  if (g_texData)
    delete[] g_texData;

  g_texData = new unsigned char[g_width * g_height * 4];

  if (g_dstTex)
    glDeleteTextures(1, &g_dstTex);

  if (!g_fbo)
    glGenFramebuffers(1, &g_fbo);

  glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);

  glGenTextures(1, &g_dstTex);
  GLFunctions::glBindTexture(g_dstTex);
  GLFunctions::glTexImage2D(w, h, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  GLFunctions::glBindTexture(0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_dstTex, 0);

  glGenTextures(1, &g_depthTex);
  GLFunctions::glBindTexture(g_depthTex);
  GLFunctions::glTexImage2D(w, h, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, g_depthTex, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);

  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER) ;
  if (status != GL_FRAMEBUFFER_COMPLETE)
  {
    LOG(LWARNING, ("INCOMPLETE FRAMEBUFFER: ", strings::to_string(status)));
    return;
  }
  g_width = w;
  g_height = h;

  if (g_program)
    return;

  string vs =
        "\n attribute vec2 a_pos;"
        "\n attribute vec2 a_tcoord;"
        "\n"
        "\n uniform mat4 rotate;"
        "\n uniform mat4 translate;"
        "\n uniform mat4 projection;"
        "\n"
        "\n varying vec2 v_tcoord;"
        "\n"
        "\n	void main() {"
        "\n	"
        "\n		v_tcoord = a_tcoord;"
        "\n		gl_Position.xy = a_pos;"
        "\n   gl_Position.zw = vec2(0.0, 1.0);"
        "\n   gl_Position = projection * translate * rotate * gl_Position;"
        "\n	}"
        "\n	";

  string fs =
        "\n	uniform sampler2D tex;"
        "\n varying vec2 v_tcoord;"
        "\n	"
        "\n	void main() {"
        "\n		gl_FragColor = texture2D(tex, v_tcoord);"
//        "\n   gl_FragColor = vec4(v_tcoord.x, v_tcoord.y, 0.0, 1.0);"
        "\n	}";

  GLuint v = GLFunctions::glCreateShader(GL_VERTEX_SHADER);
  GLuint f = GLFunctions::glCreateShader(GL_FRAGMENT_SHADER);

  GLFunctions::glShaderSource(v, vs);
  GLFunctions::glShaderSource(f, fs);

  string errlog;
  if (!GLFunctions::glCompileShader(v, errlog))
  {
    LOG(LWARNING, ("VS Error log: ", errlog));
    return;
  }
  if (!GLFunctions::glCompileShader(f, errlog))
  {
    LOG(LWARNING, ("FS Error log: ", errlog));
    return;
  }

  g_program = GLFunctions::glCreateProgram();

  GLFunctions::glAttachShader(g_program, v);
  GLFunctions::glAttachShader(g_program, f);

  GLFunctions::glBindAttribLocation(g_program, 0, "a_pos");
  GLFunctions::glBindAttribLocation(g_program, 1, "a_tcoord");

  if (!GLFunctions::glLinkProgram(g_program, errlog))
  {
    LOG(LWARNING, ("Link Error log: ", errlog));
    return;
  }
  GLFunctions::glUseProgram(g_program);

  GLint loc = GLFunctions::glGetUniformLocation(g_program, "tex");
  glUniform1i(loc, 0);

  array<float, 16> rotateMatrix;
  array<float, 16> translateMatrix;
  array<float, 16> projectionMatrix;

  setRotate(rotateMatrix);
  setTranslate(translateMatrix);
  setProjection(projectionMatrix);

  int8_t locationRotate = GLFunctions::glGetUniformLocation(g_program, "rotate");
  int8_t locationTranslate = GLFunctions::glGetUniformLocation(g_program, "translate");
  int8_t locationProjection = GLFunctions::glGetUniformLocation(g_program, "projection");

  GLFunctions::glUniformMatrix4x4Value(locationRotate, rotateMatrix.data());
  GLFunctions::glUniformMatrix4x4Value(locationTranslate, translateMatrix.data());
  GLFunctions::glUniformMatrix4x4Value(locationProjection, projectionMatrix.data());

  g_pos_attrib = GLFunctions::glGetAttribLocation(g_program, "a_pos");
  g_tcoord_attrib = GLFunctions::glGetAttribLocation(g_program, "a_tcoord");

  const float vertices[] =
  {
    -1.0f,  1.0f, 0.0f, 1.0f,

     1.0f,  1.0f, 1.0f, 1.0f,
    -1.0f, -1.0f, 0.0f, 0.0f,
     1.0f, -1.0f, 1.0f, 0.0f
  };

  g_vbo = GLFunctions::glGenBuffer();
  GLFunctions::glBindBuffer(g_vbo, GL_ARRAY_BUFFER);
  GLFunctions::glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

  g_vao = GLFunctions::glGenVertexArray();
  GLFunctions::glBindVertexArray(g_vao);
  GLFunctions::glVertexAttributePointer(g_pos_attrib, 2, GL_FLOAT, false, sizeof(float)*4, 0);
  GLFunctions::glEnableVertexAttribute(g_pos_attrib);
  GLFunctions::glVertexAttributePointer(g_tcoord_attrib, 2, GL_FLOAT, false, sizeof(float)*4, sizeof(float)*2);
  GLFunctions::glEnableVertexAttribute(g_tcoord_attrib);

  GLFunctions::glBindBuffer(0, GL_ARRAY_BUFFER);
  GLFunctions::glUseProgram(0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  LOG(LINFO, ("initTwoPassRenderer completed: tex ", strings::to_string(g_dstTex),
              " size ", strings::to_string(g_width), "x", strings::to_string(g_height),
              ", fbo ", strings::to_string(g_fbo),
              ", program ", strings::to_string(g_program),
              ", pos_attrib ", strings::to_string(g_pos_attrib),
              ", tcoord_attrib ", strings::to_string(g_tcoord_attrib)));
}

////////////////////////////////

void FrontendRenderer::RenderScene(ScreenBase const & modelView)
{
#ifdef DRAW_INFO
  BeforeDrawFrame();
#endif

  ////////////////////////////////////////////////////
  initTwoPassRenderer(m_viewport.GetWidth(), m_viewport.GetHeight());

  glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
  ////////////////////////////////////////////////////

  RenderGroupComparator comparator;
  sort(m_renderGroups.begin(), m_renderGroups.end(), bind(&RenderGroupComparator::operator (), &comparator, _1, _2));

  BeginUpdateOverlayTree(modelView);
  size_t eraseCount = 0;
  for (size_t i = 0; i < m_renderGroups.size(); ++i)
  {
    drape_ptr<RenderGroup> & group = m_renderGroups[i];
    if (group->IsEmpty())
      continue;

    if (group->IsPendingOnDelete())
    {
      group.reset();
      ++eraseCount;
      continue;
    }

    switch (group->GetState().GetDepthLayer())
    {
    case dp::GLState::OverlayLayer:
      UpdateOverlayTree(modelView, group);
      break;
    case dp::GLState::DynamicGeometry:
      group->Update(modelView);
      break;
    default:
      break;
    }
  }
  EndUpdateOverlayTree();
  m_renderGroups.resize(m_renderGroups.size() - eraseCount);

  m_viewport.Apply();
  GLFunctions::glClear();

  dp::GLState::DepthLayer prevLayer = dp::GLState::GeometryLayer;
  size_t currentRenderGroup = 0;
  for (; currentRenderGroup < m_renderGroups.size(); ++currentRenderGroup)
  {
    drape_ptr<RenderGroup> const & group = m_renderGroups[currentRenderGroup];

    dp::GLState const & state = group->GetState();
    dp::GLState::DepthLayer layer = state.GetDepthLayer();
    if (prevLayer != layer && layer == dp::GLState::OverlayLayer)
      break;

    prevLayer = layer;
    RenderSingleGroup(modelView, make_ref(group));
  }

  GLFunctions::glClearDepth();
  if (m_selectionShape != nullptr)
  {
    SelectionShape::ESelectedObject selectedObject = m_selectionShape->GetSelectedObject();
    if (selectedObject == SelectionShape::OBJECT_MY_POSITION)
    {
      ASSERT(m_myPositionController->IsModeHasPosition(), ());
      m_selectionShape->SetPosition(m_myPositionController->Position());
      m_selectionShape->Render(modelView, make_ref(m_gpuProgramManager), m_generalUniforms);
    }
    else if (selectedObject == SelectionShape::OBJECT_POI)
      m_selectionShape->Render(modelView, make_ref(m_gpuProgramManager), m_generalUniforms);
  }

  m_myPositionController->Render(MyPositionController::RenderAccuracy,
                                 modelView, make_ref(m_gpuProgramManager), m_generalUniforms);

  for (; currentRenderGroup < m_renderGroups.size(); ++currentRenderGroup)
  {
    drape_ptr<RenderGroup> const & group = m_renderGroups[currentRenderGroup];
    RenderSingleGroup(modelView, make_ref(group));
  }

  GLFunctions::glClearDepth();
  if (m_selectionShape != nullptr && m_selectionShape->GetSelectedObject() == SelectionShape::OBJECT_USER_MARK)
    m_selectionShape->Render(modelView, make_ref(m_gpuProgramManager), m_generalUniforms);

  GLFunctions::glDisable(gl_const::GLDepthTest);

  for (drape_ptr<UserMarkRenderGroup> const & group : m_userMarkRenderGroups)
  {
    ASSERT(group.get() != nullptr, ());
    group->UpdateAnimation();
    if (m_userMarkVisibility.find(group->GetTileKey()) != m_userMarkVisibility.end())
      RenderSingleGroup(modelView, make_ref(group));
  }

  m_routeRenderer->Render(modelView, make_ref(m_gpuProgramManager), m_generalUniforms);
  m_myPositionController->Render(MyPositionController::RenderMyPosition,
                                 modelView, make_ref(m_gpuProgramManager), m_generalUniforms);

  if (m_guiRenderer != nullptr)
    m_guiRenderer->Render(make_ref(m_gpuProgramManager), modelView);

////////////////////////////////////////////////////
  /*
  if (!(++g_frameIndex % 50))
  {
    unsigned char* buf = new unsigned char[g_width * g_height * 4];
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, g_width, g_height, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    int retCode = stbi_write_bmp("/Users/daravolvenkova/dstTex.bmp", g_width, g_height, 4, buf);
    delete[] buf;
  }
  */
  GLFunctions::glDisable(gl_const::GLDepthTest);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  GLFunctions::glActiveTexture(GL_TEXTURE0);
  GLFunctions::glBindTexture(g_dstTex);
  GLFunctions::glUseProgram(g_program);
  GLFunctions::glBindBuffer(g_vbo, GL_ARRAY_BUFFER);
  GLFunctions::glBindVertexArray(g_vao);


  m_viewport.Apply();
  GLFunctions::glClear();
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  GLFunctions::glUseProgram(0);
  GLFunctions::glBindTexture(0);
////////////////////////////////////////////////////

  GLFunctions::glEnable(gl_const::GLDepthTest);

#ifdef DRAW_INFO
  AfterDrawFrame();
#endif
}

void FrontendRenderer::RenderSingleGroup(ScreenBase const & modelView, ref_ptr<BaseRenderGroup> group)
{
  group->UpdateAnimation();
  dp::GLState const & state = group->GetState();

  ref_ptr<dp::GpuProgram> program = m_gpuProgramManager->GetProgram(state.GetProgramIndex());
  program->Bind();
  ApplyUniforms(m_generalUniforms, program);
  ApplyUniforms(group->GetUniforms(), program);
  ApplyState(state, program);

  group->Render(modelView);
}

void FrontendRenderer::RefreshProjection()
{
  array<float, 16> m;

  dp::MakeProjection(m, 0.0f, m_viewport.GetWidth(), m_viewport.GetHeight(), 0.0f);
  m_generalUniforms.SetMatrix4x4Value("projection", m.data());
}

void FrontendRenderer::RefreshModelView(ScreenBase const & screen)
{
  ScreenBase::MatrixT const & m = screen.GtoPMatrix();
  math::Matrix<float, 4, 4> mv;

  /// preparing ModelView matrix

  mv(0, 0) = m(0, 0); mv(0, 1) = m(1, 0); mv(0, 2) = 0; mv(0, 3) = m(2, 0);
  mv(1, 0) = m(0, 1); mv(1, 1) = m(1, 1); mv(1, 2) = 0; mv(1, 3) = m(2, 1);
  mv(2, 0) = 0;       mv(2, 1) = 0;       mv(2, 2) = 1; mv(2, 3) = 0;
  mv(3, 0) = m(0, 2); mv(3, 1) = m(1, 2); mv(3, 2) = 0; mv(3, 3) = m(2, 2);

  m_generalUniforms.SetMatrix4x4Value("modelView", mv.m_data);
}

void FrontendRenderer::RefreshBgColor()
{
  uint32_t color = drule::rules().GetBgColor(df::GetDrawTileScale(m_userEventStream.GetCurrentScreen()));
  dp::Color c = dp::Extract(color, 255 - (color >> 24));
  GLFunctions::glClearColor(c.GetRedF(), c.GetGreenF(), c.GetBlueF(), 1.0f);
}

int FrontendRenderer::GetCurrentZoomLevel() const
{
  return m_currentZoomLevel;
}

void FrontendRenderer::ResolveZoomLevel(ScreenBase const & screen)
{
  m_currentZoomLevel = GetDrawTileScale(screen);
}

void FrontendRenderer::OnTap(m2::PointD const & pt, bool isLongTap)
{
  double halfSize = VisualParams::Instance().GetTouchRectRadius();
  m2::PointD sizePoint(halfSize, halfSize);
  m2::RectD selectRect(pt - sizePoint, pt + sizePoint);

  ScreenBase const & screen = m_userEventStream.GetCurrentScreen();
  bool isMyPosition = false;
  if (m_myPositionController->IsModeHasPosition())
    isMyPosition = selectRect.IsPointInside(screen.GtoP(m_myPositionController->Position()));

  m_tapEventInfoFn(pt, isLongTap, isMyPosition, GetVisiblePOI(selectRect));
}

void FrontendRenderer::OnDoubleTap(m2::PointD const & pt)
{
  m_userEventStream.AddEvent(ScaleEvent(2.0 /*scale factor*/, pt, true /*animated*/));
}

bool FrontendRenderer::OnSingleTouchFiltrate(m2::PointD const & pt, TouchEvent::ETouchType type)
{
  float const rectHalfSize = df::VisualParams::Instance().GetTouchRectRadius();
  m2::RectD r(-rectHalfSize, -rectHalfSize, rectHalfSize, rectHalfSize);
  r.SetCenter(pt);

  switch(type)
  {
  case TouchEvent::ETouchType::TOUCH_DOWN:
    return m_guiRenderer->OnTouchDown(r);
  case TouchEvent::ETouchType::TOUCH_UP:
    m_guiRenderer->OnTouchUp(r);
    return false;
  case TouchEvent::ETouchType::TOUCH_CANCEL:
    m_guiRenderer->OnTouchCancel(r);
    return false;
  case TouchEvent::ETouchType::TOUCH_MOVE:
    return false;
  }

  return false;
}

void FrontendRenderer::OnDragStarted()
{
  m_myPositionController->DragStarted();
}

void FrontendRenderer::OnDragEnded(m2::PointD const & distance)
{
  m_myPositionController->DragEnded(distance);
}

void FrontendRenderer::OnScaleStarted()
{
  m_myPositionController->ScaleStarted();
}

void FrontendRenderer::OnRotated()
{
  m_myPositionController->Rotated();
}

void FrontendRenderer::CorrectScalePoint(m2::PointD & pt) const
{
  m_myPositionController->CorrectScalePoint(pt);
}

void FrontendRenderer::CorrectScalePoint(m2::PointD & pt1, m2::PointD & pt2) const
{
  m_myPositionController->CorrectScalePoint(pt1, pt2);
}

void FrontendRenderer::OnScaleEnded()
{
  m_myPositionController->ScaleEnded();
}

void FrontendRenderer::ResolveTileKeys(ScreenBase const & screen, TTilesCollection & tiles)
{
  m2::RectD const & clipRect = screen.ClipRect();
  ResolveTileKeys(clipRect, tiles);
}

void FrontendRenderer::ResolveTileKeys(m2::RectD const & rect, TTilesCollection & tiles)
{
  // equal for x and y
  int const tileScale = GetCurrentZoomLevel();
  double const range = MercatorBounds::maxX - MercatorBounds::minX;
  double const rectSize = range / (1 << tileScale);

  int const minTileX = static_cast<int>(floor(rect.minX() / rectSize));
  int const maxTileX = static_cast<int>(ceil(rect.maxX() / rectSize));
  int const minTileY = static_cast<int>(floor(rect.minY() / rectSize));
  int const maxTileY = static_cast<int>(ceil(rect.maxY() / rectSize));

  // request new tiles
  m_tileTree->BeginRequesting(tileScale, rect);
  for (int tileY = minTileY; tileY < maxTileY; ++tileY)
  {
    for (int tileX = minTileX; tileX < maxTileX; ++tileX)
    {
      TileKey key(tileX, tileY, tileScale);
      if (rect.IsIntersect(key.GetGlobalRect()))
      {
        tiles.insert(key);
        m_tileTree->RequestTile(key);
      }
    }
  }
  m_tileTree->EndRequesting();
}

FrontendRenderer::Routine::Routine(FrontendRenderer & renderer) : m_renderer(renderer) {}

void FrontendRenderer::Routine::Do()
{
  gui::DrapeGui::Instance().ConnectOnCompassTappedHandler(bind(&FrontendRenderer::OnCompassTapped, &m_renderer));
  m_renderer.m_myPositionController->SetListener(ref_ptr<MyPositionController::Listener>(&m_renderer));
  m_renderer.m_userEventStream.SetListener(ref_ptr<UserEventStream::Listener>(&m_renderer));

  m_renderer.m_tileTree->SetHandlers(bind(&FrontendRenderer::OnAddRenderGroup, &m_renderer, _1, _2, _3),
                                     bind(&FrontendRenderer::OnDeferRenderGroup, &m_renderer, _1, _2, _3),
                                     bind(&FrontendRenderer::OnActivateTile, &m_renderer, _1),
                                     bind(&FrontendRenderer::OnRemoveTile, &m_renderer, _1));

  dp::OGLContext * context = m_renderer.m_contextFactory->getDrawContext();
  context->makeCurrent();
  GLFunctions::Init();
  GLFunctions::AttachCache(this_thread::get_id());

  GLFunctions::glPixelStore(gl_const::GLUnpackAlignment, 1);
  GLFunctions::glEnable(gl_const::GLDepthTest);

  m_renderer.RefreshBgColor();

  GLFunctions::glClearDepthValue(1.0);
  GLFunctions::glDepthFunc(gl_const::GLLessOrEqual);
  GLFunctions::glDepthMask(true);

  GLFunctions::glFrontFace(gl_const::GLClockwise);
  GLFunctions::glCullFace(gl_const::GLBack);
  GLFunctions::glEnable(gl_const::GLCullFace);

  dp::BlendingParams blendingParams;
  blendingParams.Apply();

  my::HighResTimer timer;
  //double processingTime = InitAvarageTimePerMessage; // By init we think that one message processed by 1ms

  timer.Reset();
  double frameTime = 0.0;
  int inactiveFrameCount = 0;
  bool viewChanged = true;
  ScreenBase modelView = m_renderer.UpdateScene(viewChanged);
  while (!IsCancelled())
  {
    context->setDefaultFramebuffer();
    bool const hasAsyncRoutines = m_renderer.m_texMng->UpdateDynamicTextures();
    m_renderer.RenderScene(modelView);
    bool const animActive = InterpolationHolder::Instance().Advance(frameTime);
    modelView = m_renderer.UpdateScene(viewChanged);

    if (!viewChanged && m_renderer.IsQueueEmpty() && !animActive && !hasAsyncRoutines)
      ++inactiveFrameCount;
    else
      inactiveFrameCount = 0;

    if (inactiveFrameCount > 60)
    {
      // process a message or wait for a message
      m_renderer.ProcessSingleMessage();
      inactiveFrameCount = 0;
    }
    else
    {
      double availableTime = VSyncInterval - (timer.ElapsedSeconds() /*+ avarageMessageTime*/);

      if (availableTime < 0.0)
        availableTime = 0.01;

      while (availableTime > 0)
      {
        m_renderer.ProcessSingleMessage(availableTime * 1000.0);
        availableTime = VSyncInterval - (timer.ElapsedSeconds() /*+ avarageMessageTime*/);
        //messageCount++;
      }

      //processingTime = (timer.ElapsedSeconds() - processingTime) / messageCount;
    }

    context->present();
    frameTime = timer.ElapsedSeconds();
    timer.Reset();

    m_renderer.CheckRenderingEnabled();
  }

  m_renderer.ReleaseResources();
}

void FrontendRenderer::ReleaseResources()
{
  m_tileTree.reset();
  m_renderGroups.clear();
  m_deferredRenderGroups.clear();
  m_userMarkRenderGroups.clear();
  m_guiRenderer.reset();
  m_myPositionController.reset();
  m_selectionShape.release();
  m_routeRenderer.reset();

  m_gpuProgramManager.reset();
  m_contextFactory->getDrawContext()->doneCurrent();
}

void FrontendRenderer::AddUserEvent(UserEvent const & event)
{
  m_userEventStream.AddEvent(event);
  if (IsInInfinityWaiting())
    CancelMessageWaiting();
}

void FrontendRenderer::PositionChanged(m2::PointD const & position)
{
  m_userPositionChangedFn(position);
}

void FrontendRenderer::ChangeModelView(m2::PointD const & center)
{
  AddUserEvent(SetCenterEvent(center, -1, true));
}

void FrontendRenderer::ChangeModelView(double azimuth)
{
  AddUserEvent(RotateEvent(azimuth));
}

void FrontendRenderer::ChangeModelView(m2::RectD const & rect)
{
  AddUserEvent(SetRectEvent(rect, true, scales::GetUpperComfortScale(), true));
}

void FrontendRenderer::ChangeModelView(m2::PointD const & userPos, double azimuth,
                                       m2::PointD const & pxZero)
{
  ScreenBase const & screen = m_userEventStream.GetCurrentScreen();
  m2::RectD const & pixelRect = screen.PixelRect();
  m2::AnyRectD targetRect = m_userEventStream.GetTargetRect();

  auto calculateOffset = [&pixelRect, &targetRect](m2::PointD const & pixelPos)
  {
    m2::PointD formingVector = pixelPos;
    formingVector.x /= pixelRect.SizeX();
    formingVector.y /= pixelRect.SizeY();
    formingVector.x *= targetRect.GetLocalRect().SizeX();
    formingVector.y *= targetRect.GetLocalRect().SizeY();

    return formingVector.Length();
  };

  double const newCenterOffset = calculateOffset(pixelRect.Center() - pxZero);
  double const oldCenterOffset = calculateOffset(pixelRect.Center() - screen.GtoP(userPos));

  m2::PointD viewVector = userPos.Move(1.0, -azimuth + math::pi2) - userPos;
  viewVector.Normalize();

  m2::AnyRectD const rect = m2::AnyRectD(viewVector * newCenterOffset + userPos,
                                         -azimuth, targetRect.GetLocalRect());

  AddUserEvent(FollowAndRotateEvent(rect, userPos, newCenterOffset, oldCenterOffset, -azimuth, true));
}

ScreenBase const & FrontendRenderer::UpdateScene(bool & modelViewChanged)
{
  bool viewportChanged;
  ScreenBase const & modelView = m_userEventStream.ProcessEvents(modelViewChanged, viewportChanged);
  gui::DrapeGui::Instance().SetInUserAction(m_userEventStream.IsInUserAction());
  if (viewportChanged)
    OnResize(modelView);

  if (modelViewChanged)
  {
    ResolveZoomLevel(modelView);
    TTilesCollection tiles;
    ResolveTileKeys(modelView, tiles);

    m_commutator->PostMessage(ThreadsCommutator::ResourceUploadThread,
                              make_unique_dp<UpdateReadManagerMessage>(modelView, move(tiles)),
                              MessagePriority::High);

    RefreshModelView(modelView);
    RefreshBgColor();
    EmitModelViewChanged(modelView);
  }

  return modelView;
}

void FrontendRenderer::EmitModelViewChanged(ScreenBase const & modelView) const
{
  m_modelViewChangedFn(modelView);
}

} // namespace df
