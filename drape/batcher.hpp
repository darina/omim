#pragma once

#include "drape/attribute_provider.hpp"
#include "drape/feature_geometry_decl.hpp"
#include "drape/glstate.hpp"
#include "drape/overlay_handle.hpp"
#include "drape/pointers.hpp"
#include "drape/render_bucket.hpp"
#include "drape/vertex_array_buffer.hpp"

#include "base/macros.hpp"

#include "std/map.hpp"
#include "std/function.hpp"

namespace dp
{

class RenderBucket;
class AttributeProvider;
class OverlayHandle;

class Batcher
{
public:
  static uint32_t const IndexPerTriangle = 3;
  static uint32_t const IndexPerQuad = 6;
  static uint32_t const VertexPerQuad = 4;

  Batcher(uint32_t indexBufferSize, uint32_t vertexBufferSize);
  ~Batcher();

  void InsertTriangleList(GLState const & state, ref_ptr<AttributeProvider> params);
  IndicesRange InsertTriangleList(GLState const & state, ref_ptr<AttributeProvider> params,
                                  drape_ptr<OverlayHandle> && handle);

  void InsertTriangleStrip(GLState const & state, ref_ptr<AttributeProvider> params);
  IndicesRange InsertTriangleStrip(GLState const & state, ref_ptr<AttributeProvider> params,
                                   drape_ptr<OverlayHandle> && handle);

  void InsertTriangleFan(GLState const & state, ref_ptr<AttributeProvider> params);
  IndicesRange InsertTriangleFan(GLState const & state, ref_ptr<AttributeProvider> params,
                                 drape_ptr<OverlayHandle> && handle);

  void InsertListOfStrip(GLState const & state, ref_ptr<AttributeProvider> params, uint8_t vertexStride);
  IndicesRange InsertListOfStrip(GLState const & state, ref_ptr<AttributeProvider> params,
                                 drape_ptr<OverlayHandle> && handle, uint8_t vertexStride);

  typedef function<void (GLState const &, drape_ptr<RenderBucket> &&)> TFlushFn;
  void StartSession(TFlushFn const & flusher);
  void EndSession();

  void StartFeatureRecord(FeatureShapeInfo const & feature);
  void EndFeatureRecord();

private:
  template<typename TBacher>
  IndicesRange InsertTriangles(GLState const & state, ref_ptr<AttributeProvider> params,
                               drape_ptr<OverlayHandle> && handle, uint8_t vertexStride = 0);

  class CallbacksWrapper;
  void ChangeBuffer(ref_ptr<CallbacksWrapper> wrapper);
  ref_ptr<RenderBucket> GetBucket(GLState const & state);

  void FinalizeBucket(GLState const & state);
  void Flush();

private:
  TFlushFn m_flushInterface;

private:
  struct BucketId
  {
    BucketId() = default;
    BucketId(GLState const & state, uint32_t quadrantId)
      : m_state(state)
      , m_quadrantId(quadrantId)
    {}

    bool operator < (BucketId const & other) const
    {
      if (m_state == other.m_state)
        return m_quadrantId < other.m_quadrantId;
      return m_state < other.m_state;
    }

    GLState m_state;
    uint32_t m_quadrantId = 0;
  };

  using TBuckets = map<BucketId, drape_ptr<RenderBucket>>;
  TBuckets m_buckets;

  uint32_t m_indexBufferSize;
  uint32_t m_vertexBufferSize;

  FeatureShapeInfo m_currentFeature;
};

class BatcherFactory
{
public:
  Batcher * GetNew() const;
};

class SessionGuard
{
public:
  SessionGuard(Batcher & batcher, Batcher::TFlushFn const & flusher);
  ~SessionGuard();

  DISALLOW_COPY_AND_MOVE(SessionGuard);
private:
  Batcher & m_batcher;
};

} // namespace dp
