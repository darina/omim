#include "drape/static_texture.hpp"

#include "platform/platform.hpp"

#include "coding/reader.hpp"
#include "coding/parse_xml.hpp"

#include "base/string_utils.hpp"

#ifdef DEBUG
#include "3party/glm/glm/gtx/bit.hpp"
#endif
#include "3party/stb_image/stb_image.h"

namespace dp
{

namespace
{

using TLoadingCompletion = function<void(unsigned char *, uint32_t, uint32_t)>;
using TLoadingFailure = function<void(string const &)>;

bool LoadData(ReaderPtr<Reader> reader,
              TLoadingCompletion const & completionHandler,
              TLoadingFailure const & failureHandler)
{
  ASSERT(completionHandler != nullptr, ());
  ASSERT(failureHandler != nullptr, ());

  vector<unsigned char> rawData;
  try
  {
    CHECK_LESS(reader.Size(), static_cast<uint64_t>(numeric_limits<size_t>::max()), ());
    size_t const size = static_cast<size_t>(reader.Size());
    rawData.resize(size);
    reader.Read(0, &rawData[0], size);
  }
  catch (RootException & e)
  {
    failureHandler(e.what());
    return false;
  }

  int w, h, bpp;
  unsigned char * data = stbi_png_load_from_memory(&rawData[0], static_cast<int>(rawData.size()),
                                                   &w, &h, &bpp, 0);
  ASSERT_EQUAL(bpp, 4, ("Incorrect texture format"));
  ASSERT(glm::isPowerOfTwo(w), (w));
  ASSERT(glm::isPowerOfTwo(h), (h));
  completionHandler(data, w, h);

  stbi_image_free(data);
  return true;
}

class StaticResourceInfo : public Texture::ResourceInfo
{
public:
  StaticResourceInfo() : Texture::ResourceInfo(m2::RectF(0.0f, 0.0f, 1.0f, 1.0f)) {}
  virtual ~StaticResourceInfo(){}

  Texture::ResourceType GetType() const override { return Texture::Static; }
};

} // namespace

StaticTexture::StaticTexture()
  : m_info(make_unique_dp<StaticResourceInfo>())
  , m_isLoadingCorrect(false)
{
}

bool StaticTexture::Load(ReaderPtr<Reader> reader, ref_ptr<HWTextureAllocator> allocator)
{
  auto completionHandler = [this, &allocator](unsigned char * data, uint32_t width, uint32_t height)
  {
    Texture::Params p;
    p.m_allocator = allocator;
    p.m_format = dp::RGBA8;
    p.m_width = width;
    p.m_height = height;
    p.m_wrapSMode = gl_const::GLRepeate;
    p.m_wrapTMode = gl_const::GLRepeate;

    Create(p, make_ref(data));
  };

  auto failureHandler = [this](string const & reason)
  {
    LOG(LERROR, (reason));
    Fail();
  };

  m_isLoadingCorrect = LoadData(reader, completionHandler, failureHandler);
  return m_isLoadingCorrect;
}

ref_ptr<Texture::ResourceInfo> StaticTexture::FindResource(Texture::Key const & key, bool & newResource)
{
  newResource = false;
  if (key.GetType() != Texture::Static)
    return nullptr;
  return make_ref(m_info);
}

void StaticTexture::Fail()
{
  int32_t alfaTexture = 0;
  Texture::Params p;
  p.m_allocator = GetDefaultAllocator();
  p.m_format = dp::RGBA8;
  p.m_width = 1;
  p.m_height = 1;

  Create(p, make_ref(&alfaTexture));
}

StaticResourceTexture::StaticResourceTexture(string const & textureName, string const & skinPathName,
                                             ref_ptr<HWTextureAllocator> allocator)
  : m_textureName(textureName)
{
  Load(skinPathName, allocator);
}

bool StaticResourceTexture::Load(string const & skinPathName, ref_ptr<HWTextureAllocator> allocator)
{
  ReaderPtr<Reader> reader = GetStyleReader().GetResourceReader(m_textureName + ".png", skinPathName);
  return TBase::Load(reader, allocator);
}

void StaticResourceTexture::Invalidate(string const & skinPathName, ref_ptr<HWTextureAllocator> allocator)
{
  Destroy();
  Load(skinPathName, allocator);
}

StaticColorTexture::StaticColorTexture(ref_ptr<HWTextureAllocator> allocator)
{
  Load(GetStyleReader().GetStaticColorsReader(), allocator);
}

void StaticColorTexture::Invalidate(ref_ptr<HWTextureAllocator> allocator)
{
  Destroy();
  Load(GetStyleReader().GetStaticColorsReader(), allocator);
}

} // namespace dp
