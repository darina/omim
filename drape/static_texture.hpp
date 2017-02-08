#pragma once

#include "drape/texture.hpp"

#include "indexer/map_style_reader.hpp"

#include "std/string.hpp"

namespace dp
{

class StaticTexture : public Texture
{
public:
  class StaticKey : public Key
  {
  public:
    ResourceType GetType() const override { return ResourceType::Static; }
  };

  StaticTexture();
  virtual ~StaticTexture() {}

  ref_ptr<ResourceInfo> FindResource(Key const & key, bool & newResource) override;

  bool IsLoadingCorrect() const { return m_isLoadingCorrect; }

protected:
  bool Load(ReaderPtr<Reader> reader, ref_ptr<HWTextureAllocator> allocator);

private:
  void Fail();

  drape_ptr<Texture::ResourceInfo> m_info;
  bool m_isLoadingCorrect;
};

class StaticResourceTexture: public StaticTexture
{
  using TBase = StaticTexture;
public:
  StaticResourceTexture(string const & textureName, string const & skinPathName,
                        ref_ptr<HWTextureAllocator> allocator);

  void Invalidate(string const & skinPathName, ref_ptr<HWTextureAllocator> allocator);

private:
  bool Load(string const & skinPathName, ref_ptr<HWTextureAllocator> allocator);

  string m_textureName;
};

class StaticColorTexture: public StaticTexture
{
public:
  StaticColorTexture(ref_ptr<HWTextureAllocator> allocator);

  void Invalidate(ref_ptr<HWTextureAllocator> allocator);
};

} // namespace dp
