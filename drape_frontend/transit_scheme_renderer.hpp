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
  TransitSchemeRenderer();
  ~TransitSchemeRenderer();

  void AddRenderData(ref_ptr<dp::GpuProgramManager> mng,
                     TransitRenderData && renderData);

  void AddMarkersRenderData(ref_ptr<dp::GpuProgramManager> mng,
                            TransitMarkersRenderData && renderData);

  void AddTextRenderData(ref_ptr<dp::GpuProgramManager> mng,
                         TransitTextRenderData && renderData);

  void RenderTransit(ScreenBase const & screen, int zoomLevel,
                     ref_ptr<dp::GpuProgramManager> mng,
                     dp::UniformValuesStorage const & commonUniforms);

  bool HasRenderData(int zoomLevel) const;

  void CollectOverlays(ref_ptr<dp::OverlayTree> tree, ScreenBase const & modelView);

private:
  std::vector<TransitRenderData> m_renderData;
  std::vector<TransitMarkersRenderData> m_markersRenderData;
  std::vector<TransitTextRenderData> m_textRenderData;
  std::vector<TransitTextRenderData> m_colorSymbolRenderData;
};
}  // namespace df
