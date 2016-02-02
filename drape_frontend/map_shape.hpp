#pragma once

#include "drape_frontend/message.hpp"
#include "drape_frontend/tile_key.hpp"

#include "drape/pointers.hpp"

#include "indexer/feature_decl.hpp"

namespace dp
{
  class Batcher;
  class TextureManager;
}

namespace df
{

// Priority of shapes' processing (descending order).
enum MapShapePriority
{
  AreaPriority = 0,
  TextAndPoiPriority,
  LinePriority,

  PrioritiesCount
};

class MapShape
{
public:
  virtual ~MapShape(){}
  virtual void Prepare(ref_ptr<dp::TextureManager> textures) const {}
  virtual void Draw(ref_ptr<dp::Batcher> batcher, ref_ptr<dp::TextureManager> textures) const = 0;
  virtual MapShapePriority GetPriority() const { return MapShapePriority::AreaPriority; }

  void SetFeatureInfo(FeatureGeometryId const & feature) { m_featureInfo = feature; }
  FeatureGeometryId GetFeatureInfo() const { return m_featureInfo; }

  void SetFeatureLimitRect(m2::RectD rect) { m_limitRect = rect; }
  m2::RectD const & GetFeatureLimitRect() const { return m_limitRect; }

private:
  FeatureGeometryId m_featureInfo;
  m2::RectD m_limitRect;
};

using TMapShapes = vector<drape_ptr<MapShape>>;

class MapShapeReadedMessage : public Message
{
public:
  MapShapeReadedMessage(TileKey const & key, TMapShapes && shapes)
    : m_key(key), m_shapes(move(shapes))
  {}

  Type GetType() const override { return Message::MapShapeReaded; }

  TileKey const & GetKey() const { return m_key; }

  TMapShapes const & GetShapes() { return m_shapes; }

private:
  TileKey m_key;
  TMapShapes m_shapes;
};

} // namespace df
