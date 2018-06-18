#pragma once

#include "drape_frontend/transit_scheme_builder.hpp"

#include "drape/gpu_program_manager.hpp"
#include "drape/pointers.hpp"
#include "drape/uniform_values_storage.hpp"

#include "geometry/screenbase.hpp"

namespace df
{

class TransitSchemeRenderer
{
public:
  TransitSchemeRenderer() = default;

  void AddRenderData(ref_ptr<dp::GpuProgramManager> mng, TransitRenderData && renderData);
  void AddMarkersRenderData(ref_ptr<dp::GpuProgramManager> mng, TransitRenderData && renderData);
  void AddTextRenderData(ref_ptr<dp::GpuProgramManager> mng, TransitRenderData && renderData);
  void AddStubsRenderData(ref_ptr<dp::GpuProgramManager> mng, TransitRenderData && renderData);

  void RenderTransit(ScreenBase const & screen, int zoomLevel,
                     ref_ptr<dp::GpuProgramManager> mng,
                     dp::UniformValuesStorage const & commonUniforms);

  bool HasRenderData(int zoomLevel) const;
  void ClearGLDependentResources();
  void Clear(MwmSet::MwmId const & mwmId);

  void CollectOverlays(ref_ptr<dp::OverlayTree> tree, ScreenBase const & modelView);

private:
  void PrepareRenderData(ref_ptr<dp::GpuProgramManager> mng, std::vector<TransitRenderData> & currentRenderData,
                         TransitRenderData & newRenderData);
  void ClearRenderData(MwmSet::MwmId const & mwmId, std::vector<TransitRenderData> & renderData);

  uint32_t m_lastRecacheId;
  std::vector<TransitRenderData> m_renderData;
  std::vector<TransitRenderData> m_markersRenderData;
  std::vector<TransitRenderData> m_textRenderData;
  std::vector<TransitRenderData> m_colorSymbolRenderData;
};
}  // namespace df
