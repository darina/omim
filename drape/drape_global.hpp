#pragma once

#include "color.hpp"

#include "geometry/point2d.hpp"

#include "base/assert.hpp"

namespace dp
{

enum TextureFormat
{
  RGBA8,
  ALPHA,
  UNSPECIFIED
};

inline uint8_t GetBytesPerPixel(TextureFormat format)
{
  uint8_t result = 0;
  switch (format)
  {
  case RGBA8:
    result = 4;
    break;
  case ALPHA:
    result = 1;
    break;
  default:
    ASSERT(false, ());
    break;
  }

  return result;
}

enum Anchor
{
  Center      = 0,
  Left        = 0x1,
  Right       = Left << 1,
  Top         = Right << 1,
  Bottom      = Top << 1,
  LeftTop     = Left | Top,
  RightTop    = Right | Top,
  LeftBottom  = Left | Bottom,
  RightBottom = Right | Bottom
};

enum LineCap
{
  SquareCap = -1,
  RoundCap = 0,
  ButtCap = 1,
};

enum LineJoin
{
  MiterJoin = -1,
  BevelJoin = 0,
  RoundJoin = 1,
};

struct ColorInfo
{
  enum class Type
  {
    Dynamic,
    Static
  };

  ColorInfo() = default;

  ColorInfo(Color const & color, m2::PointF const & colorCoords)
    : m_type(Type::Static)
    , m_texCoords(colorCoords)
    , m_color(color)
  {}

  explicit ColorInfo(Color const & color)
    : m_type(Type::Dynamic)
    , m_color(color)
  {}

  Type m_type = Type::Dynamic;
  m2::PointF m_texCoords = m2::PointF(0.0f, 0.0f);
  Color m_color = Color::Transparent();
};

struct FontDecl
{
  FontDecl() = default;
  FontDecl(ColorInfo const & color, float size, bool isSdf = true, ColorInfo const & outlineColor = ColorInfo())
    : m_color(color)
    , m_outlineColor(outlineColor)
    , m_size(size)
    , m_isSdf(isSdf)
  {
  }

  ColorInfo m_color;
  ColorInfo m_outlineColor;
  float m_size = 0;
  bool m_isSdf = true;
};

}
