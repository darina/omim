#pragma once

#include "drape/pointers.hpp"

#include "indexer/feature_decl.hpp"

class ScreenBase;

namespace df
{
class BatchMergeHelper;
}

namespace dp
{

class OverlayHandle;
class OverlayTree;
class VertexArrayBuffer;

class RenderBucket
{
  friend class df::BatchMergeHelper;
public:
  RenderBucket(drape_ptr<VertexArrayBuffer> && buffer);
  ~RenderBucket();

  ref_ptr<VertexArrayBuffer> GetBuffer();
  drape_ptr<VertexArrayBuffer> && MoveBuffer();

  size_t GetOverlayHandlesCount() const;
  drape_ptr<OverlayHandle> PopOverlayHandle();
  ref_ptr<OverlayHandle> GetOverlayHandle(size_t index);
  void AddOverlayHandle(drape_ptr<OverlayHandle> && handle);

  void Update(ScreenBase const & modelView);
  void CollectOverlayHandles(ref_ptr<OverlayTree> tree);
  void Render(ScreenBase const & screen);

  // Only for testing! Don't use this function in production code!
  void RenderDebug(ScreenBase const & screen, m2::PointD const & tileCenter) const;

  // Only for testing! Don't use this function in production code!
  template <typename ToDo>
  void ForEachOverlay(ToDo const & todo)
  {
    for (drape_ptr<OverlayHandle> const & h : m_overlay)
      todo(make_ref(h));
  }

  struct FeatureGeometryInfo
  {
    FeatureGeometryInfo(uint32_t indexOffset, uint32_t indexCount, m2::RectD const & limitRect)
      : m_indexOffset(indexOffset)
      , m_indexCount(indexCount)
      , m_limitRect(limitRect)
    {}

    uint32_t m_indexOffset;
    uint32_t m_indexCount;
    m2::RectD m_limitRect;
    bool m_featureCompleted = true;
  };
  using TFeaturesRanges = map<FeatureGeometryId, FeatureGeometryInfo>;

  void BeginFeatureRecord(FeatureGeometryId feature, m2::RectD const & limitRect);
  void EndFeatureRecord(bool featureCompleted);

  TFeaturesRanges m_featuresRanges;

private:
  vector<drape_ptr<OverlayHandle> > m_overlay;
  drape_ptr<VertexArrayBuffer> m_buffer;

  FeatureGeometryId m_featureInfo;
  m2::RectD m_featureLimitRect;

};

} // namespace dp
