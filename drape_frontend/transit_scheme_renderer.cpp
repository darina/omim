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

TransitSchemeRenderer::TransitSchemeRenderer()
{

}

TransitSchemeRenderer::~TransitSchemeRenderer()
{

}

bool TransitSchemeRenderer::HasRenderData(int zoomLevel) const
{
  return !(m_renderData.empty() || zoomLevel < kTransitSchemeMinZoomLevel);
}

void TransitSchemeRenderer::AddRenderData(ref_ptr<dp::GpuProgramManager> mng,
                                          TransitRenderData && renderData)
{
  // Remove obsolete render data.
  m_renderData.erase(remove_if(m_renderData.begin(), m_renderData.end(), [&renderData](TransitRenderData const & rd)
  {
    return rd.m_mwmId == renderData.m_mwmId && renderData.m_shapeId == rd.m_shapeId
      && renderData.m_lineId == rd.m_lineId;
  }), m_renderData.end());

  // Add new render data.
  m_renderData.emplace_back(std::move(renderData));
  TransitRenderData & rd = m_renderData.back();

  ref_ptr<dp::GpuProgram> program = mng->GetProgram(rd.m_state.GetProgramIndex());
  program->Bind();
  for (auto const & bucket : rd.m_buckets)
    bucket->GetBuffer()->Build(program);
}

void TransitSchemeRenderer::AddMarkersRenderData(ref_ptr<dp::GpuProgramManager> mng,
                                                 TransitMarkersRenderData && renderData)
{
  // Remove obsolete render data.
  m_markersRenderData.erase(remove_if(m_markersRenderData.begin(), m_markersRenderData.end(),
                                      [&renderData](TransitMarkersRenderData const & rd)
  {
    return rd.m_mwmId == renderData.m_mwmId;
  }), m_markersRenderData.end());

  // Add new render data.
  m_markersRenderData.emplace_back(std::move(renderData));
  TransitMarkersRenderData & rd = m_markersRenderData.back();

  ref_ptr<dp::GpuProgram> program = mng->GetProgram(rd.m_state.GetProgramIndex());
  program->Bind();
  for (auto const & bucket : rd.m_buckets)
    bucket->GetBuffer()->Build(program);
}

void TransitSchemeRenderer::AddTextRenderData(ref_ptr<dp::GpuProgramManager> mng,
                                              TransitTextRenderData && renderData)
{
  auto & data = (renderData.m_state.GetProgramIndex() == gpu::TEXT_OUTLINED_PROGRAM) ? m_textRenderData
                                                                                     : m_colorSymbolRenderData;

  // Remove obsolete render data.
  data.erase(remove_if(data.begin(), data.end(),
                      [&renderData](TransitTextRenderData const & rd)
                      {
                        return rd.m_mwmId == renderData.m_mwmId;
                      }), data.end());

  // Add new render data.
  data.emplace_back(std::move(renderData));
  TransitTextRenderData & rd = data.back();

  ref_ptr<dp::GpuProgram> program = mng->GetProgram(rd.m_state.GetProgramIndex());
  program->Bind();
  for (auto const & bucket : rd.m_buckets)
    bucket->GetBuffer()->Build(program);
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
