#include "drape/vulkan/vulkan_pipeline.hpp"
#include "drape/vulkan/vulkan_utils.hpp"

#include "platform/platform.hpp"

#include "coding/file_name_utils.hpp"
#include "coding/file_reader.hpp"
#include "coding/file_writer.hpp"
#include "coding/write_to_sink.hpp"

#include "base/assert.hpp"

#include <string>
#include <utility>
#include <vector>

namespace dp
{
namespace vulkan
{
namespace
{
std::string const kDumpFileName = "vulkan_dump.bin";

// Stencil package.
uint8_t constexpr kStencilBackFunctionByte = 7;
uint8_t constexpr kStencilBackFailActionByte = 6;
uint8_t constexpr kStencilBackDepthFailActionByte = 5;
uint8_t constexpr kStencilBackPassActionByte = 4;
uint8_t constexpr kStencilFrontFunctionByte = 3;
uint8_t constexpr kStencilFrontFailActionByte = 2;
uint8_t constexpr kStencilFrontDepthFailActionByte = 1;
uint8_t constexpr kStencilFrontPassActionByte = 0;

VkCompareOp DecodeTestFunction(uint8_t testFunctionByte)
{
  switch (static_cast<TestFunction>(testFunctionByte))
  {
  case TestFunction::Never: return VK_COMPARE_OP_NEVER;
  case TestFunction::Less: return VK_COMPARE_OP_LESS;
  case TestFunction::Equal: return VK_COMPARE_OP_EQUAL;
  case TestFunction::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
  case TestFunction::Greater: return VK_COMPARE_OP_GREATER;
  case TestFunction::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
  case TestFunction::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
  case TestFunction::Always: return VK_COMPARE_OP_ALWAYS;
  }
  ASSERT(false, ());
}

VkStencilOp DecodeStencilAction(uint8_t stencilActionByte)
{
  switch (static_cast<StencilAction>(stencilActionByte))
  {
  case StencilAction::Keep: return VK_STENCIL_OP_KEEP;
  case StencilAction::Zero: return VK_STENCIL_OP_ZERO;
  case StencilAction::Replace: return VK_STENCIL_OP_REPLACE;
  case StencilAction::Increment: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
  case StencilAction::IncrementWrap: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
  case StencilAction::Decrement: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
  case StencilAction::DecrementWrap: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
  case StencilAction::Invert: return VK_STENCIL_OP_INVERT;
  }
  ASSERT(false, ());
}

VkFormat GetAttributeFormat(uint8_t componentCount, glConst componentType)
{
  if (componentType == gl_const::GLFloatType)
  {
    switch (componentCount)
    {
    case 1: return VK_FORMAT_R32_SFLOAT;
    case 2: return VK_FORMAT_R32G32_SFLOAT;
    case 3: return VK_FORMAT_R32G32B32_SFLOAT;
    case 4: return VK_FORMAT_R32G32B32A32_SFLOAT;
    }
  }
  else if (componentType == gl_const::GLByteType)
  {
    switch (componentCount)
    {
    case 1: return VK_FORMAT_R8_SINT;
    case 2: return VK_FORMAT_R8G8_SINT;
    case 3: return VK_FORMAT_R8G8B8_SINT;
    case 4: return VK_FORMAT_R8G8B8A8_SINT;
    }
  }
  else if (componentType == gl_const::GLUnsignedByteType)
  {
    switch (componentCount)
    {
    case 1: return VK_FORMAT_R8_UINT;
    case 2: return VK_FORMAT_R8G8_UINT;
    case 3: return VK_FORMAT_R8G8B8_UINT;
    case 4: return VK_FORMAT_R8G8B8A8_UINT;
    }
  }
  else if (componentType == gl_const::GLShortType)
  {
    switch (componentCount)
    {
    case 1: return VK_FORMAT_R16_SINT;
    case 2: return VK_FORMAT_R16G16_SINT;
    case 3: return VK_FORMAT_R16G16B16_SINT;
    case 4: return VK_FORMAT_R16G16B16A16_SINT;
    }
  }
  else if (componentType == gl_const::GLUnsignedShortType)
  {
    switch (componentCount)
    {
    case 1: return VK_FORMAT_R16_UINT;
    case 2: return VK_FORMAT_R16G16_UINT;
    case 3: return VK_FORMAT_R16G16B16_UINT;
    case 4: return VK_FORMAT_R16G16B16A16_UINT;
    }
  }
  else if (componentType == gl_const::GLIntType)
  {
    switch (componentCount)
    {
    case 1: return VK_FORMAT_R32_SINT;
    case 2: return VK_FORMAT_R32G32_SINT;
    case 3: return VK_FORMAT_R32G32B32_SINT;
    case 4: return VK_FORMAT_R32G32B32A32_SINT;
    }
  }
  else if (componentType == gl_const::GLUnsignedIntType)
  {
    switch (componentCount)
    {
    case 1: return VK_FORMAT_R32_UINT;
    case 2: return VK_FORMAT_R32G32_UINT;
    case 3: return VK_FORMAT_R32G32B32_UINT;
    case 4: return VK_FORMAT_R32G32B32A32_UINT;
    }
  }

  CHECK(false, ("Unsupported attribute format.", componentCount, componentType));
  return VK_FORMAT_UNDEFINED;
}

std::string GetDumpFilePath()
{
  return base::JoinPath(GetPlatform().TmpDir(), kDumpFileName);
}
}  // namespace

VulkanPipeline::VulkanPipeline(VkDevice device, int appVersionCode)
  : m_appVersionCode(appVersionCode)
{
  // Read dump.
  std::vector<uint8_t> dumpData;
  auto const dumpFilePath = GetDumpFilePath();
  if (GetPlatform().IsFileExistsByFullPath(dumpFilePath))
  {
    try
    {
      FileReader r(dumpFilePath);
      NonOwningReaderSource src(r);

      auto const v = ReadPrimitiveFromSource<int>(src);
      if (v != appVersionCode)
      {
        // Dump is obsolete.
        FileWriter::DeleteFileX(dumpFilePath);
      }
      else
      {
        dumpData.resize(static_cast<size_t>(r.Size() - sizeof(int)));
        src.Read(dumpData.data(), dumpData.size());
      }
    }
    catch (FileReader::Exception const & exception)
    {
      LOG(LWARNING, ("Exception while reading file:", dumpFilePath,
        "reason:", exception.what()));
    }
  }

  VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
  pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  pipelineCacheCreateInfo.initialDataSize = dumpData.size();
  pipelineCacheCreateInfo.pInitialData = dumpData.data();
  CHECK_VK_CALL(vkCreatePipelineCache(device, &pipelineCacheCreateInfo, nullptr, &m_vulkanPipelineCache));
}

void VulkanPipeline::Dump(VkDevice device)
{
  if (!m_isChanged)
    return;

  size_t constexpr kMaxCacheSizeInBytes = 500 * 1024;

  size_t cacheSize;
  VkResult statusCode;
  statusCode = vkGetPipelineCacheData(device, m_vulkanPipelineCache, &cacheSize, nullptr);
  if (statusCode == VK_SUCCESS && cacheSize > 0)
  {
    if (cacheSize <= kMaxCacheSizeInBytes)
    {
      std::vector<uint8_t> dumpData(cacheSize);
      statusCode = vkGetPipelineCacheData(device, m_vulkanPipelineCache, &cacheSize, dumpData.data());
      if (statusCode == VK_SUCCESS)
      {
        auto const dumpFilePath = GetDumpFilePath();
        try
        {
          FileWriter w(dumpFilePath);
          WriteToSink(w, m_appVersionCode);
          w.Write(dumpData.data(), dumpData.size());
        }
        catch (FileWriter::Exception const & exception)
        {
          LOG(LWARNING, ("Exception while writing file:", dumpFilePath,
            "reason:", exception.what()));
        }
        m_isChanged = false;
      }
    }
    else
    {
      LOG(LWARNING, ("Maximum pipeline cache size exceeded (", cacheSize, "/", kMaxCacheSizeInBytes,
                     "bytes)"));
    }
  }
}

void VulkanPipeline::ResetCache(VkDevice device)
{
  for (auto const & p : m_pipelineCache)
    vkDestroyPipeline(device, p.second, nullptr);
  m_pipelineCache.clear();
  m_isChanged = true;
}

void VulkanPipeline::Destroy(VkDevice device)
{
  Dump(device);

  for (auto const & p : m_pipelineCache)
    vkDestroyPipeline(device, p.second, nullptr);
  m_pipelineCache.clear();

  vkDestroyPipelineCache(device, m_vulkanPipelineCache, nullptr);
}

VkPipeline VulkanPipeline::GetPipeline(VkDevice device, PipelineKey const & key)
{
  CHECK(key.m_renderPass != VK_NULL_HANDLE, ());

  auto const it = m_pipelineCache.find(key);
  if (it != m_pipelineCache.end())
    return it->second;

  // Primitives.
  VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCreateInfo = {};
  inputAssemblyStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssemblyStateCreateInfo.topology = key.m_primitiveTopology;

  // Rasterization.
  VkPipelineRasterizationStateCreateInfo rasterizationStateCreateInfo = {};
  rasterizationStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizationStateCreateInfo.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizationStateCreateInfo.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizationStateCreateInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rasterizationStateCreateInfo.lineWidth = 1.0f;

  // Blending.
  VkPipelineColorBlendAttachmentState pipelineColorBlendAttachmentState = {};
  pipelineColorBlendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  pipelineColorBlendAttachmentState.blendEnable = key.m_blendingEnabled ? VK_TRUE : VK_FALSE;
  if (key.m_blendingEnabled)
  {
    pipelineColorBlendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
    pipelineColorBlendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
    pipelineColorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    pipelineColorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    pipelineColorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    pipelineColorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  }
  VkPipelineColorBlendStateCreateInfo colorBlendStateCreateInfo = {};
  colorBlendStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlendStateCreateInfo.attachmentCount = 1;
  colorBlendStateCreateInfo.pAttachments = &pipelineColorBlendAttachmentState;

  // Viewport.
  VkPipelineViewportStateCreateInfo viewportStateCreateInfo = {};
  viewportStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportStateCreateInfo.viewportCount = 1;
  viewportStateCreateInfo.scissorCount = 1;

  // Multisampling.
  VkPipelineMultisampleStateCreateInfo multisampleStateCreateInfo = {};
  multisampleStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampleStateCreateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  // Dynamic.
  static std::vector<VkDynamicState> dynamicState = {
    VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_LINE_WIDTH,
    VK_DYNAMIC_STATE_STENCIL_REFERENCE};
  VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo = {};
  dynamicStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicStateCreateInfo.pDynamicStates = dynamicState.data();
  dynamicStateCreateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicState.size());

  // Input state.
  std::vector<VkVertexInputBindingDescription> bindingDescriptions(key.m_bindingInfo.size());
  size_t attribsCount = 0;
  for (size_t i = 0; i < key.m_bindingInfo.size(); ++i)
  {
    bindingDescriptions[i].binding = static_cast<uint32_t>(i);
    bindingDescriptions[i].stride = key.m_bindingInfo[i].GetElementSize();
    bindingDescriptions[i].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    attribsCount += key.m_bindingInfo[i].GetCount();
  }

  std::vector<VkVertexInputAttributeDescription> attributeDescriptions(attribsCount);
  uint32_t bindingCounter = 0;
  for (size_t i = 0; i < key.m_bindingInfo.size(); ++i)
  {
    for (uint8_t j = 0; j < key.m_bindingInfo[i].GetCount(); ++j)
    {
      BindingDecl const & bindingDecl = key.m_bindingInfo[i].GetBindingDecl(j);
      attributeDescriptions[bindingCounter].location = bindingCounter;
      attributeDescriptions[bindingCounter].binding = static_cast<uint32_t>(i);
      attributeDescriptions[bindingCounter].format = GetAttributeFormat(bindingDecl.m_componentCount,
                                                                        bindingDecl.m_componentType);
      attributeDescriptions[bindingCounter].offset = bindingDecl.m_offset;

      bindingCounter++;
    }
  }

  VkPipelineVertexInputStateCreateInfo inputStateCreateInfo = {};
  inputStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  inputStateCreateInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
  inputStateCreateInfo.pVertexBindingDescriptions = bindingDescriptions.data();
  inputStateCreateInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
  inputStateCreateInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

  // Depth stencil.
  VkPipelineDepthStencilStateCreateInfo depthStencilState = {};
  depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencilState.depthTestEnable = key.m_depthStencil.m_depthEnabled ? VK_TRUE : VK_FALSE;
  if (key.m_depthStencil.m_depthEnabled)
  {
    depthStencilState.depthWriteEnable = VK_TRUE;
    depthStencilState.depthCompareOp =
      DecodeTestFunction(static_cast<uint8_t>(key.m_depthStencil.m_depthFunction));
  }
  else
  {
    depthStencilState.depthWriteEnable = VK_FALSE;
    depthStencilState.depthCompareOp = VK_COMPARE_OP_ALWAYS;
  }

  if (key.m_depthStencil.m_stencilEnabled)
  {
    depthStencilState.front.compareOp =
      DecodeTestFunction(GetStateByte(key.m_depthStencil.m_stencil, kStencilFrontFunctionByte));
    depthStencilState.front.failOp =
      DecodeStencilAction(GetStateByte(key.m_depthStencil.m_stencil, kStencilFrontFailActionByte));
    depthStencilState.front.depthFailOp = DecodeStencilAction(
      GetStateByte(key.m_depthStencil.m_stencil, kStencilFrontDepthFailActionByte));
    depthStencilState.front.passOp =
      DecodeStencilAction(GetStateByte(key.m_depthStencil.m_stencil, kStencilFrontPassActionByte));
    depthStencilState.front.writeMask = 0xffffffff;
    depthStencilState.front.compareMask = 0xffffffff;
    depthStencilState.front.reference = 1;

    depthStencilState.back.compareOp =
      DecodeTestFunction(GetStateByte(key.m_depthStencil.m_stencil, kStencilBackFunctionByte));
    depthStencilState.back.failOp =
      DecodeStencilAction(GetStateByte(key.m_depthStencil.m_stencil, kStencilBackFailActionByte));
    depthStencilState.back.depthFailOp = DecodeStencilAction(
      GetStateByte(key.m_depthStencil.m_stencil, kStencilBackDepthFailActionByte));
    depthStencilState.back.passOp =
      DecodeStencilAction(GetStateByte(key.m_depthStencil.m_stencil, kStencilBackPassActionByte));
    depthStencilState.back.writeMask = 0xffffffff;
    depthStencilState.back.compareMask = 0xffffffff;
    depthStencilState.back.reference = 1;
  }
  else
  {
    depthStencilState.front.compareOp = VK_COMPARE_OP_ALWAYS;
    depthStencilState.back.compareOp = VK_COMPARE_OP_ALWAYS;
  }

  // Pipeline.
  VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
  pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineCreateInfo.layout = key.m_program->GetPipelineLayout();
  CHECK(pipelineCreateInfo.layout != VK_NULL_HANDLE, ());
  pipelineCreateInfo.renderPass = key.m_renderPass;
  pipelineCreateInfo.basePipelineIndex = -1;
  pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
  pipelineCreateInfo.pVertexInputState = &inputStateCreateInfo;
  pipelineCreateInfo.pInputAssemblyState = &inputAssemblyStateCreateInfo;
  pipelineCreateInfo.pRasterizationState = &rasterizationStateCreateInfo;
  pipelineCreateInfo.pColorBlendState = &colorBlendStateCreateInfo;
  pipelineCreateInfo.pMultisampleState = &multisampleStateCreateInfo;
  pipelineCreateInfo.pViewportState = &viewportStateCreateInfo;
  pipelineCreateInfo.pDepthStencilState = &depthStencilState;
  pipelineCreateInfo.pDynamicState = &dynamicStateCreateInfo;
  auto shaders = key.m_program->GetShaders();
  pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaders.size());
  pipelineCreateInfo.pStages = shaders.data();

  VkPipeline pipeline;
  CHECK_VK_CALL(vkCreateGraphicsPipelines(device, m_vulkanPipelineCache, 1, &pipelineCreateInfo,
                                          nullptr, &pipeline));

  m_pipelineCache.insert(std::make_pair(key, pipeline));
  m_isChanged = true;

  return pipeline;
}

void VulkanPipeline::DepthStencilKey::SetDepthTestEnabled(bool enabled)
{
  m_depthEnabled = enabled;
}
  
void VulkanPipeline::DepthStencilKey::SetDepthTestFunction(TestFunction depthFunction)
{
  m_depthFunction = depthFunction;
}
  
void VulkanPipeline::DepthStencilKey::SetStencilTestEnabled(bool enabled)
{
  m_stencilEnabled = enabled;
}
  
void VulkanPipeline::DepthStencilKey::SetStencilFunction(StencilFace face, TestFunction stencilFunction)
{
  switch (face)
  {
  case StencilFace::Front:
    SetStateByte(m_stencil, static_cast<uint8_t>(stencilFunction), kStencilFrontFunctionByte);
    break;
  case StencilFace::Back:
    SetStateByte(m_stencil, static_cast<uint8_t>(stencilFunction), kStencilBackFunctionByte);
    break;
  case StencilFace::FrontAndBack:
    SetStateByte(m_stencil, static_cast<uint8_t>(stencilFunction), kStencilFrontFunctionByte);
    SetStateByte(m_stencil, static_cast<uint8_t>(stencilFunction), kStencilBackFunctionByte);
    break;
  }
}
  
void VulkanPipeline::DepthStencilKey::SetStencilActions(StencilFace face, StencilAction stencilFailAction,
                                                        StencilAction depthFailAction, StencilAction passAction)
{
  switch (face)
  {
  case StencilFace::Front:
    SetStateByte(m_stencil, static_cast<uint8_t>(stencilFailAction), kStencilFrontFailActionByte);
    SetStateByte(m_stencil, static_cast<uint8_t>(depthFailAction), kStencilFrontDepthFailActionByte);
    SetStateByte(m_stencil, static_cast<uint8_t>(passAction), kStencilFrontPassActionByte);
    break;
  case StencilFace::Back:
    SetStateByte(m_stencil, static_cast<uint8_t>(stencilFailAction), kStencilBackFailActionByte);
    SetStateByte(m_stencil, static_cast<uint8_t>(depthFailAction), kStencilBackDepthFailActionByte);
    SetStateByte(m_stencil, static_cast<uint8_t>(passAction), kStencilBackPassActionByte);
    break;
  case StencilFace::FrontAndBack:
    SetStateByte(m_stencil, static_cast<uint8_t>(stencilFailAction), kStencilFrontFailActionByte);
    SetStateByte(m_stencil, static_cast<uint8_t>(depthFailAction), kStencilFrontDepthFailActionByte);
    SetStateByte(m_stencil, static_cast<uint8_t>(passAction), kStencilFrontPassActionByte);
    SetStateByte(m_stencil, static_cast<uint8_t>(stencilFailAction), kStencilBackFailActionByte);
    SetStateByte(m_stencil, static_cast<uint8_t>(depthFailAction), kStencilBackDepthFailActionByte);
    SetStateByte(m_stencil, static_cast<uint8_t>(passAction), kStencilBackPassActionByte);
    break;
  }
}

bool VulkanPipeline::DepthStencilKey::operator<(DepthStencilKey const & rhs) const
{
  if (m_depthEnabled != rhs.m_depthEnabled)
    return m_depthEnabled < rhs.m_depthEnabled;
  
  if (m_stencilEnabled != rhs.m_stencilEnabled)
    return m_stencilEnabled < rhs.m_stencilEnabled;
  
  if (m_depthFunction != rhs.m_depthFunction)
    return m_depthFunction < rhs.m_depthFunction;
  
  return m_stencil < rhs.m_stencil;
}

bool VulkanPipeline::DepthStencilKey::operator!=(DepthStencilKey const & rhs) const
{
  return m_depthEnabled != rhs.m_depthEnabled || m_stencilEnabled != rhs.m_stencilEnabled ||
         m_depthFunction != rhs.m_depthFunction || m_stencil != rhs.m_stencil;
}

bool VulkanPipeline::PipelineKey::operator<(PipelineKey const & rhs) const
{
  if (m_renderPass != rhs.m_renderPass)
    return m_renderPass < rhs.m_renderPass;

  if (m_program != rhs.m_program)
    return m_program < rhs.m_program;
  
  if (m_depthStencil != rhs.m_depthStencil)
    return m_depthStencil < rhs.m_depthStencil;

  if (m_bindingInfo != rhs.m_bindingInfo)
    return m_bindingInfo < rhs.m_bindingInfo;

  if (m_primitiveTopology != rhs.m_primitiveTopology)
    return m_primitiveTopology < rhs.m_primitiveTopology;

  return m_blendingEnabled < rhs.m_blendingEnabled;
}
}  // namespace vulkan
}  // namespace dp
