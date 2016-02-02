#include "drape/render_bucket.hpp"

#include "drape/attribute_buffer_mutator.hpp"
#include "drape/debug_rect_renderer.hpp"
#include "drape/overlay_handle.hpp"
#include "drape/overlay_tree.hpp"
#include "drape/vertex_array_buffer.hpp"

#include "base/stl_add.hpp"
#include "std/bind.hpp"

namespace dp
{

RenderBucket::RenderBucket(drape_ptr<VertexArrayBuffer> && buffer)
  : m_buffer(move(buffer))
{
}

RenderBucket::~RenderBucket()
{
}

ref_ptr<VertexArrayBuffer> RenderBucket::GetBuffer()
{
  return make_ref(m_buffer);
}

drape_ptr<VertexArrayBuffer> && RenderBucket::MoveBuffer()
{
  return move(m_buffer);
}

size_t RenderBucket::GetOverlayHandlesCount() const
{
  return m_overlay.size();
}

drape_ptr<OverlayHandle> RenderBucket::PopOverlayHandle()
{
  ASSERT(!m_overlay.empty(), ());
  size_t lastElement = m_overlay.size() - 1;
  swap(m_overlay[0], m_overlay[lastElement]);
  drape_ptr<OverlayHandle> h = move(m_overlay[lastElement]);
  m_overlay.pop_back();
  return h;
}

ref_ptr<OverlayHandle> RenderBucket::GetOverlayHandle(size_t index)
{
  return make_ref(m_overlay[index]);
}

void RenderBucket::AddOverlayHandle(drape_ptr<OverlayHandle> && handle)
{
  m_overlay.push_back(move(handle));
}

void RenderBucket::Update(ScreenBase const & modelView)
{
  for (drape_ptr<OverlayHandle> & overlayHandle : m_overlay)
  {
    if (overlayHandle->IsVisible())
      overlayHandle->Update(modelView);
  }
}

void RenderBucket::CollectOverlayHandles(ref_ptr<OverlayTree> tree)
{
  for (drape_ptr<OverlayHandle> const & overlayHandle : m_overlay)
    tree->Add(make_ref(overlayHandle));
}

void RenderBucket::Render(ScreenBase const & screen)
{
  ASSERT(m_buffer != nullptr, ());

  if (!m_overlay.empty())
  {
    // in simple case when overlay is symbol each element will be contains 6 indexes
    AttributeBufferMutator attributeMutator;
    IndexBufferMutator indexMutator(6 * m_overlay.size());
    ref_ptr<IndexBufferMutator> rfpIndex = make_ref(&indexMutator);
    ref_ptr<AttributeBufferMutator> rfpAttrib = make_ref(&attributeMutator);

    bool hasIndexMutation = false;
    for (drape_ptr<OverlayHandle> const & handle : m_overlay)
    {
      if (handle->IndexesRequired())
      {
        if (handle->IsVisible())
          handle->GetElementIndexes(rfpIndex);
        hasIndexMutation = true;
      }

      if (handle->HasDynamicAttributes())
        handle->GetAttributeMutation(rfpAttrib, screen);
    }

    m_buffer->ApplyMutation(hasIndexMutation ? rfpIndex : nullptr, rfpAttrib);
  }
  m_buffer->Render();
}

void RenderBucket::BeginFeatureRecord(FeatureGeometryId feature, const m2::RectD & limitRect)
{
  m_featureInfo = feature;
  m_featureLimitRect = limitRect;
  m_featuresRanges.insert(make_pair(feature, FeatureGeometryInfo(m_buffer->GetIndexCount(), 0, limitRect)));
}

void RenderBucket::EndFeatureRecord(bool featureCompleted)
{
  auto it = m_featuresRanges.find(m_featureInfo);
  ASSERT(it != m_featuresRanges.end(), ());
  it->second.m_indexCount = m_buffer->GetIndexCount() - it->second.m_indexOffset;
  it->second.m_featureCompleted = featureCompleted;
  if (it->second.m_indexCount == 0)
    m_featuresRanges.erase(it);
  m_featureInfo = FeatureGeometryId();
}

void RenderBucket::RenderDebug(ScreenBase const & screen, const m2::PointD & tileCenter) const
{
#ifdef RENDER_DEBUG_RECTS
  m2::PointF arrowStart(tileCenter);
  if (m_overlay.empty())
  {
    for (auto const & feature : m_featuresRanges)
    {
      m2::RectD pxRect;
      screen.GtoP(feature.second.m_limitRect, pxRect);
      m2::PointF arrowEnd(pxRect.Center());
      DebugRectRenderer::Instance().DrawRect(screen, m2::RectF(pxRect));
      DebugRectRenderer::Instance().DrawArrow(screen, OverlayTree::DisplacementData(arrowStart, arrowEnd, dp::Color(0, 0, 255, 255)));
    }
  }
  /*if (!m_overlay.empty())
  {
    for (auto const & handle : m_overlay)
    {
      if (!screen.PixelRect().IsIntersect(handle->GetPixelRect(screen, false)))
        continue;

      OverlayHandle::Rects rects;
      handle->GetExtendedPixelShape(screen, rects);
      for (auto const & rect : rects)
      {
        if (screen.isPerspective() && !screen.PixelRectIn3d().IsIntersect(m2::RectD(rect)))
          continue;

        DebugRectRenderer::Instance().DrawRect(screen, rect);
      }
    }
  }
  */
#endif
}

} // namespace dp
