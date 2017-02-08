#pragma once

#include "drape/color.hpp"
#include "drape/glyph_manager.hpp"
#include "drape/pointers.hpp"
#include "drape/texture.hpp"
#include "drape/font_texture.hpp"

#include "base/string_utils.hpp"

#include "std/atomic.hpp"
#include "std/unordered_set.hpp"

namespace dp
{

class HWTextureAllocator;

class TextureManager
{
public:
  class BaseRegion
  {
  public:
    BaseRegion();
    void SetResourceInfo(ref_ptr<Texture::ResourceInfo> info);
    void SetTexture(ref_ptr<Texture> texture);
    ref_ptr<Texture> GetTexture() const { return m_texture; }
    bool IsValid() const;

    m2::PointF GetPixelSize() const;
    float GetPixelHeight() const;
    m2::RectF const & GetTexRect() const;

  protected:
    ref_ptr<Texture::ResourceInfo> m_info;
    ref_ptr<Texture> m_texture;
  };

  class SymbolRegion : public BaseRegion {};

  class GlyphRegion : public BaseRegion
  {
  public:
    GlyphRegion();

    float GetOffsetX() const;
    float GetOffsetY() const;
    float GetAdvanceX() const;
    float GetAdvanceY() const;
  };

  class StippleRegion : public BaseRegion
  {
  public:
    StippleRegion() : BaseRegion() {}

    uint32_t GetMaskPixelLength() const;
    uint32_t GetPatternPixelLength() const;
  };

  class ColorRegion : public BaseRegion
  {
  public:
    ColorRegion() : BaseRegion() {}

    void SetTexCoords(m2::PointF const & texCoords);
    m2::PointF GetTexCoords() const;

  private:
    m2::PointF m_textureCoords = m2::PointF(0.0f, 0.0f);
  };

  struct Params
  {
    string m_resPostfix;
    double m_visualScale;
    string m_colors;
    string m_patterns;
    GlyphManager::Params m_glyphMngParams;
  };

  TextureManager();
  void Release();

  void Init(Params const & params);
  void Invalidate(string const & resPostfix);

  void GetSymbolRegion(string const & symbolName, SymbolRegion & region);

  typedef buffer_vector<uint8_t, 8> TStipplePattern;
  void GetStippleRegion(TStipplePattern const & pen, StippleRegion & region);
  void GetColorRegion(Color const & color, ColorRegion & region);
  void GetColorRegion(ColorInfo const & colorInfo, ColorRegion & region);

  typedef buffer_vector<strings::UniString, 4> TMultilineText;
  typedef buffer_vector<GlyphRegion, 128> TGlyphsBuffer;
  typedef buffer_vector<TGlyphsBuffer, 4> TMultilineGlyphsBuffer;

  void GetGlyphRegions(TMultilineText const & text, int fixedHeight, TMultilineGlyphsBuffer & buffers);
  void GetGlyphRegions(strings::UniString const & text, int fixedHeight, TGlyphsBuffer & regions);

  /// On some devices OpenGL driver can't resolve situation when we upload on texture from one thread
  /// and use this texture to render on other thread. By this we move UpdateDynamicTextures call into render thread
  /// If you implement some kind of dynamic texture, you must synchronyze UploadData and index creation operations
  bool UpdateDynamicTextures();

  /// This method must be called only on Frontend renderer's thread.
  bool AreGlyphsReady(strings::UniString const & str, int fixedHeight) const;

  ref_ptr<Texture> GetSymbolsTexture() const;
  ref_ptr<Texture> GetTrafficArrowTexture() const;
  ref_ptr<Texture> GetStaticColorTexture() const;

private:
  struct GlyphGroup
  {
    GlyphGroup()
      : m_startChar(0), m_endChar(0), m_texture(nullptr)
    {}

    GlyphGroup(strings::UniChar const & start, strings::UniChar const & end)
      : m_startChar(start), m_endChar(end), m_texture(nullptr)
    {}

    strings::UniChar m_startChar;
    strings::UniChar m_endChar;
    ref_ptr<Texture> m_texture;
  };

  struct HybridGlyphGroup
  {
    HybridGlyphGroup()
      : m_texture(nullptr)
    {}

    std::set<pair<strings::UniChar, int> > m_glyphs;
    ref_ptr<Texture> m_texture;
  };

  uint32_t m_maxTextureSize;
  uint32_t m_maxGlypsCount;

  ref_ptr<Texture> AllocateGlyphTexture();
  void GetRegionBase(ref_ptr<Texture> tex, TextureManager::BaseRegion & region, Texture::Key const & key);

  size_t FindGlyphsGroup(strings::UniChar const & c) const;
  size_t FindGlyphsGroup(strings::UniString const & text) const;
  size_t FindGlyphsGroup(TMultilineText const & text) const;

  size_t FindHybridGlyphsGroup(strings::UniString const & text, int fixedHeight);
  size_t FindHybridGlyphsGroup(TMultilineText const & text, int fixedHeight);

  uint32_t GetNumberOfUnfoundCharacters(strings::UniString const & text, int fixedHeight, HybridGlyphGroup const & group) const;

  void MarkCharactersUsage(strings::UniString const & text, int fixedHeight, HybridGlyphGroup & group);
  /// it's a dummy method to support generic code
  void MarkCharactersUsage(strings::UniString const & text, int fixedHeight, GlyphGroup & group) {}

  template<typename TGlyphGroup>
  void FillResultBuffer(strings::UniString const & text, int fixedHeight, TGlyphGroup & group, TGlyphsBuffer & regions)
  {
    if (group.m_texture == nullptr)
      group.m_texture = AllocateGlyphTexture();

    regions.reserve(text.size());
    for (strings::UniChar const & c : text)
    {
      GlyphRegion reg;
      GetRegionBase(group.m_texture, reg, GlyphKey(c, fixedHeight));
      regions.push_back(reg);
    }
  }

  template<typename TGlyphGroup>
  void FillResults(strings::UniString const & text, int fixedHeight, TGlyphsBuffer & buffers, TGlyphGroup & group)
  {
    MarkCharactersUsage(text, fixedHeight, group);
    FillResultBuffer<TGlyphGroup>(text, fixedHeight, group, buffers);
  }

  template<typename TGlyphGroup>
  void FillResults(TMultilineText const & text, int fixedHeight, TMultilineGlyphsBuffer & buffers, TGlyphGroup & group)
  {
     buffers.resize(text.size());
     for (size_t i = 0; i < text.size(); ++i)
     {
       strings::UniString const & str = text[i];
       TGlyphsBuffer & buffer = buffers[i];
       FillResults<TGlyphGroup>(str, fixedHeight, buffer, group);
     }
  }

  template<typename TText, typename TBuffer>
  void CalcGlyphRegions(TText const & text, int fixedHeight, TBuffer & buffers)
  {
    size_t const groupIndex = FindGlyphsGroup(text);
    bool useHybridGroup = false;
    if (fixedHeight < 0 && groupIndex != GetInvalidGlyphGroup())
    {
      GlyphGroup & group = m_glyphGroups[groupIndex];
      uint32_t const absentGlyphs = GetAbsentGlyphsCount(group.m_texture, text, fixedHeight);
      if (group.m_texture == nullptr || group.m_texture->HasEnoughSpace(absentGlyphs))
        FillResults<GlyphGroup>(text, fixedHeight, buffers, group);
      else
        useHybridGroup = true;
    }
    else
    {
      useHybridGroup = true;
    }

    if (useHybridGroup)
    {
      size_t const hybridGroupIndex = FindHybridGlyphsGroup(text, fixedHeight);
      ASSERT(hybridGroupIndex != GetInvalidGlyphGroup(), ());
      HybridGlyphGroup & group = m_hybridGlyphGroups[hybridGroupIndex];
      FillResults<HybridGlyphGroup>(text, fixedHeight, buffers, group);
    }
  }

  uint32_t GetAbsentGlyphsCount(ref_ptr<Texture> texture, strings::UniString const & text, int fixedHeight);
  uint32_t GetAbsentGlyphsCount(ref_ptr<Texture> texture, TMultilineText const & text, int fixedHeight);

  template<typename TGlyphGroups>
  void UpdateGlyphTextures(TGlyphGroups & groups)
  {
    for (auto & g : groups)
      if (g.m_texture != nullptr)
        g.m_texture->UpdateState();
  }

  template<typename TGlyphGroups>
  bool HasAsyncRoutines(TGlyphGroups const & groups) const
  {
    for (auto const & g : groups)
      if (g.m_texture != nullptr && g.m_texture->HasAsyncRoutines())
        return true;

    return false;
  }

  static constexpr size_t GetInvalidGlyphGroup();

private:
  drape_ptr<Texture> m_symbolTexture;
  drape_ptr<Texture> m_stipplePenTexture;
  drape_ptr<Texture> m_colorTexture;
  drape_ptr<Texture> m_staticColorTexture;
  list<drape_ptr<Texture>> m_glyphTextures;

  drape_ptr<Texture> m_trafficArrowTexture;

  drape_ptr<GlyphManager> m_glyphManager;
  drape_ptr<HWTextureAllocator> m_textureAllocator;

  buffer_vector<GlyphGroup, 64> m_glyphGroups;
  buffer_vector<HybridGlyphGroup, 4> m_hybridGlyphGroups;

  atomic_flag m_nothingToUpload;
};

} // namespace dp
