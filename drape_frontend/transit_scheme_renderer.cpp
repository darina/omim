#include "drape_frontend/transit_scheme_renderer.hpp"

#include "drape_frontend/shape_view_params.hpp"
#include "drape_frontend/visual_params.hpp"

#include "drape/vertex_array_buffer.hpp"

namespace df
{
namespace
{
std::vector<float> const kLineWidthInPixel =
{
  // 1   2     3     4     5     6     7     8     9    10
  1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
  //11  12    13    14    15    16    17    18    19     20
  1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f, 5.0f, 5.5f, 6.0f, 6.0f
};

float CalculateHalfWidth(ScreenBase const & screen)
{
  double zoom = 0.0;
  int index = 0;
  float lerpCoef = 0.0f;
  ExtractZoomFactors(screen, zoom, index, lerpCoef);

  return InterpolateByZoomLevels(index, lerpCoef, kLineWidthInPixel)
    * static_cast<float>(VisualParams::Instance().GetVisualScale());
}
}  // namespace

TransitSchemeRenderer::TransitSchemeRenderer()
{

}

TransitSchemeRenderer::~TransitSchemeRenderer()
{

}

void TransitSchemeRenderer::AddRenderData(ref_ptr<dp::GpuProgramManager> mng,
                                          TransitRenderData && renderData)
{
  // Remove obsolete render data.
  m_renderData.erase(remove_if(m_renderData.begin(), m_renderData.end(), [&renderData](TransitRenderData const & rd)
  {
    return rd.m_mwmId == renderData.m_mwmId && renderData.m_shapeId == rd.m_shapeId;
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

void TransitSchemeRenderer::RenderTransit(ScreenBase const & screen, int zoomLevel,
                                          ref_ptr<dp::GpuProgramManager> mng,
                                          dp::UniformValuesStorage const & commonUniforms)
{
  if (m_renderData.empty() || zoomLevel < kTransitSchemeMinZoomLevel)
    return;

  for (TransitRenderData & renderData : m_renderData)
  {
    float const pixelHalfWidth =
      CalculateHalfWidth(screen);

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
  for (TransitMarkersRenderData & renderData : m_markersRenderData)
  {
    float const pixelHalfWidth =
      CalculateHalfWidth(screen);

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
}

}  // namespace df
