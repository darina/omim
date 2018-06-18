#include "drape_frontend/transit_scheme_renderer.hpp"

#include "drape_frontend/shape_view_params.hpp"
#include "drape_frontend/visual_params.hpp"

#include "drape/overlay_tree.hpp"
#include "drape/vertex_array_buffer.hpp"
#include "shader_def.hpp"

namespace df
{
namespace
{

float CalculateHalfWidth(ScreenBase const & screen)
{
  double zoom = 0.0;
  int index = 0;
  float lerpCoef = 0.0f;
  ExtractZoomFactors(screen, zoom, index, lerpCoef);

  return InterpolateByZoomLevels(index, lerpCoef, kTransitLinesWidthInPixel)
    * static_cast<float>(VisualParams::Instance().GetVisualScale());
}
}  // namespace

bool TransitSchemeRenderer::HasRenderData(int zoomLevel) const
{
  return !m_renderData.empty() && zoomLevel >= kTransitSchemeMinZoomLevel;
}

void TransitSchemeRenderer::ClearGLDependentResources()
{
  m_renderData.clear();
  m_markersRenderData.clear();
  m_textRenderData.clear();
  m_colorSymbolRenderData.clear();
}

void TransitSchemeRenderer::Clear(MwmSet::MwmId const & mwmId)
{
  ClearRenderData(mwmId, m_renderData);
  ClearRenderData(mwmId, m_markersRenderData);
  ClearRenderData(mwmId, m_textRenderData);
  ClearRenderData(mwmId, m_colorSymbolRenderData);
}

void TransitSchemeRenderer::ClearRenderData(MwmSet::MwmId const & mwmId, std::vector<TransitRenderData> & renderData)
{
  auto removePredicate = [&mwmId](TransitRenderData const & data) { return data.m_mwmId == mwmId; };

  renderData.erase(remove_if(renderData.begin(), renderData.end(), removePredicate),
                   renderData.end());
}

void TransitSchemeRenderer::PrepareRenderData(ref_ptr<dp::GpuProgramManager> mng,
                                              std::vector<TransitRenderData> & currentRenderData,
                                              TransitRenderData & newRenderData)
{
  // Remove obsolete render data.
  currentRenderData.erase(remove_if(currentRenderData.begin(), currentRenderData.end(),
                          [this, &newRenderData](TransitRenderData const & rd)
                          {
                            return rd.m_mwmId == newRenderData.m_mwmId && rd.m_recacheId < m_lastRecacheId;
                          }), currentRenderData.end());

  m_lastRecacheId = max(m_lastRecacheId, newRenderData.m_recacheId);

  // Add new render data.
  currentRenderData.emplace_back(std::move(newRenderData));
  TransitRenderData & rd = currentRenderData.back();

  ref_ptr<dp::GpuProgram> program = mng->GetProgram(rd.m_state.GetProgramIndex());
  program->Bind();
  for (auto const & bucket : rd.m_buckets)
    bucket->GetBuffer()->Build(program);
}

void TransitSchemeRenderer::AddRenderData(ref_ptr<dp::GpuProgramManager> mng,
                                          TransitRenderData && renderData)
{
  PrepareRenderData(mng, m_renderData, renderData);
}

void TransitSchemeRenderer::AddMarkersRenderData(ref_ptr<dp::GpuProgramManager> mng,
                                                 TransitRenderData && renderData)
{
  PrepareRenderData(mng, m_markersRenderData, renderData);
}

void TransitSchemeRenderer::AddTextRenderData(ref_ptr<dp::GpuProgramManager> mng,
                                              TransitRenderData && renderData)
{
  PrepareRenderData(mng, m_textRenderData, renderData);
}

void TransitSchemeRenderer::AddStubsRenderData(ref_ptr<dp::GpuProgramManager> mng,
                                               TransitRenderData && renderData)
{
  PrepareRenderData(mng, m_colorSymbolRenderData, renderData);
}

void TransitSchemeRenderer::RenderTransit(ScreenBase const & screen, int zoomLevel,
                                          ref_ptr<dp::GpuProgramManager> mng,
                                          dp::UniformValuesStorage const & commonUniforms)
{
  if (!HasRenderData(zoomLevel))
    return;

  GLFunctions::glDisable(gl_const::GLDepthTest);
  for (auto & renderData : m_renderData)
  {
    float const pixelHalfWidth = CalculateHalfWidth(screen);

    ref_ptr<dp::GpuProgram> program = mng->GetProgram(renderData.m_state.GetProgramIndex());
    program->Bind();
    dp::ApplyState(renderData.m_state, program);

    dp::UniformValuesStorage uniforms = commonUniforms;
    math::Matrix<float, 4, 4> mv = screen.GetModelView(renderData.m_pivot, kShapeCoordScalar);
    uniforms.SetMatrix4x4Value("modelView", mv.m_data);
    uniforms.SetFloatValue("u_transitParams", pixelHalfWidth);
    dp::ApplyUniforms(uniforms, program);

    for (auto const & bucket : renderData.m_buckets)
      bucket->Render(false /* draw as line */);
  }
  GLFunctions::glEnable(gl_const::GLDepthTest);
  for (auto & renderData : m_markersRenderData)
  {
    float const pixelHalfWidth = CalculateHalfWidth(screen);

    ref_ptr<dp::GpuProgram> program = mng->GetProgram(renderData.m_state.GetProgramIndex());
    program->Bind();
    dp::ApplyState(renderData.m_state, program);

    dp::UniformValuesStorage uniforms = commonUniforms;
    math::Matrix<float, 4, 4> mv = screen.GetModelView(renderData.m_pivot, kShapeCoordScalar);
    uniforms.SetMatrix4x4Value("modelView", mv.m_data);
    uniforms.SetFloatValue("u_transitParams", pixelHalfWidth);
    uniforms.SetFloatValue("u_angleCosSin",
                           static_cast<float>(cos(screen.GetAngle())),
                           static_cast<float>(sin(screen.GetAngle())));
    dp::ApplyUniforms(uniforms, program);

    for (auto const & bucket : renderData.m_buckets)
      bucket->Render(false /* draw as line */);
  }

  auto const & params = df::VisualParams::Instance().GetGlyphVisualParams();
  for (auto & renderData : m_textRenderData)
  {
    ref_ptr<dp::GpuProgram> program = mng->GetProgram(renderData.m_state.GetProgramIndex());
    program->Bind();
    dp::ApplyState(renderData.m_state, program);

    for (auto const & bucket : renderData.m_buckets)
    {
      bucket->Update(screen);
      bucket->GetBuffer()->Build(program);
    }

    dp::UniformValuesStorage uniforms = commonUniforms;
    math::Matrix<float, 4, 4> mv = screen.GetModelView(renderData.m_pivot, kShapeCoordScalar);
    uniforms.SetMatrix4x4Value("modelView", mv.m_data);
    uniforms.SetFloatValue("u_opacity", 1.0);

    uniforms.SetFloatValue("u_contrastGamma", params.m_outlineContrast, params.m_outlineGamma);
    uniforms.SetFloatValue("u_isOutlinePass", 1.0f);
    dp::ApplyUniforms(uniforms, program);

    for (auto const & bucket : renderData.m_buckets)
      bucket->Render(false /* draw as line */);

    uniforms.SetFloatValue("u_contrastGamma", params.m_contrast, params.m_gamma);
    uniforms.SetFloatValue("u_isOutlinePass", 0.0f);
    dp::ApplyUniforms(uniforms, program);

    for (auto const & bucket : renderData.m_buckets)
      bucket->Render(false /* draw as line */);
  }

  for (auto & renderData : m_colorSymbolRenderData)
  {
    ref_ptr<dp::GpuProgram> program = mng->GetProgram(renderData.m_state.GetProgramIndex());
    program->Bind();
    dp::ApplyState(renderData.m_state, program);

    for (auto const & bucket : renderData.m_buckets)
    {
      bucket->Update(screen);
      bucket->GetBuffer()->Build(program);
    }

    dp::UniformValuesStorage uniforms = commonUniforms;
    math::Matrix<float, 4, 4> mv = screen.GetModelView(renderData.m_pivot, kShapeCoordScalar);
    uniforms.SetMatrix4x4Value("modelView", mv.m_data);
    uniforms.SetFloatValue("u_opacity", 1.0);
    dp::ApplyUniforms(uniforms, program);

    GLFunctions::glEnable(gl_const::GLDepthTest);
    for (auto const & bucket : renderData.m_buckets)
      bucket->Render(false /* draw as line */);
  }
}

void TransitSchemeRenderer::CollectOverlays(ref_ptr<dp::OverlayTree> tree, ScreenBase const & modelView)
{
  for (auto & renderData : m_textRenderData)
  {
    for (auto const & bucket : renderData.m_buckets)
    {
      if (tree->IsNeedUpdate())
        bucket->CollectOverlayHandles(tree);
      else
        bucket->Update(modelView);
    }
  }

  for (auto & renderData : m_colorSymbolRenderData)
  {
    for (auto const & bucket : renderData.m_buckets)
    {
      if (tree->IsNeedUpdate())
        bucket->CollectOverlayHandles(tree);
      else
        bucket->Update(modelView);
    }
  }
}

}  // namespace df
