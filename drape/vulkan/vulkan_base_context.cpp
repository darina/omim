#include "drape/vulkan/vulkan_base_context.hpp"
#include "drape/vulkan/vulkan_utils.hpp"

#include "drape/framebuffer.hpp"

#include "base/assert.hpp"

#include <sstream>

namespace dp
{
namespace vulkan
{
VulkanBaseContext::VulkanBaseContext(VkInstance vulkanInstance, VkPhysicalDevice gpu,
                                     VkDevice device, uint32_t queueFamilyIndex)
  : m_vulkanInstance(vulkanInstance)
  , m_gpu(gpu)
  , m_device(device)
  , m_queueFamilyIndex(queueFamilyIndex)
{
  vkGetPhysicalDeviceProperties(m_gpu, &m_gpuProperties);
}

std::string VulkanBaseContext::GetRendererName() const
{
  return m_gpuProperties.deviceName;
}

std::string VulkanBaseContext::GetRendererVersion() const
{
  std::ostringstream ss;
  ss << "API:" << VK_VERSION_MAJOR(m_gpuProperties.apiVersion) << "."
     << VK_VERSION_MINOR(m_gpuProperties.apiVersion) << "."
     << VK_VERSION_PATCH(m_gpuProperties.apiVersion)
     << "/Driver:" << VK_VERSION_MAJOR(m_gpuProperties.driverVersion) << "."
     << VK_VERSION_MINOR(m_gpuProperties.driverVersion) << "."
     << VK_VERSION_PATCH(m_gpuProperties.driverVersion);
  return ss.str();
}

void VulkanBaseContext::SetStencilReferenceValue(uint32_t stencilReferenceValue)
{
  m_stencilReferenceValue = stencilReferenceValue;
}

void VulkanBaseContext::CreateSwapchain()
{
  CHECK(m_surface.is_initialized(), ());

  VkSwapchainCreateInfoKHR swapchainCreateInfo = {};
  swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapchainCreateInfo.pNext = nullptr;
  swapchainCreateInfo.surface = m_surface.get();
  swapchainCreateInfo.minImageCount = m_surfaceCapabilities.minImageCount;
  swapchainCreateInfo.imageFormat = m_surfaceFormat.format;
  swapchainCreateInfo.imageColorSpace = m_surfaceFormat.colorSpace;
  swapchainCreateInfo.imageExtent = m_surfaceCapabilities.currentExtent;
  swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  swapchainCreateInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  swapchainCreateInfo.imageArrayLayers = 1;
  swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  swapchainCreateInfo.queueFamilyIndexCount = 0;
  swapchainCreateInfo.pQueueFamilyIndices = nullptr;
  swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
  swapchainCreateInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  swapchainCreateInfo.oldSwapchain = VK_NULL_HANDLE;
  swapchainCreateInfo.clipped = VK_FALSE;

  CHECK_VK_CALL(vkCreateSwapchainKHR(m_device, &swapchainCreateInfo, nullptr, &m_swapchain));
}

void VulkanBaseContext::CreateImageViews()
{
  uint32_t swapchainImageCount = 0;
  CHECK_VK_CALL(vkGetSwapchainImagesKHR(m_device, m_swapchain, &swapchainImageCount, nullptr));

  std::vector<VkImage> swapchainImages(swapchainImageCount);
  CHECK_VK_CALL(vkGetSwapchainImagesKHR(m_device, m_swapchain, &swapchainImageCount, swapchainImages.data()));

  m_swapchainImageViews.resize(swapchainImages.size());
  for (size_t i = 0; i < m_swapchainImageViews.size(); ++i)
  {
    VkImageViewCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.image = swapchainImages[i];
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format = m_surfaceFormat.format;
    createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    createInfo.subresourceRange.baseMipLevel = 0;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.baseArrayLayer = 0;
    createInfo.subresourceRange.layerCount = 1;
    CHECK_VK_CALL(vkCreateImageView(m_device, &createInfo, nullptr, &m_swapchainImageViews[i]));
  }
}

void VulkanBaseContext::CreateRenderPass()
{
  VkSemaphoreCreateInfo imageAcquiredSemaphoreCreateInfo = {};
  imageAcquiredSemaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  imageAcquiredSemaphoreCreateInfo.pNext = NULL;
  imageAcquiredSemaphoreCreateInfo.flags = 0;

  VkSemaphore imageAcquiredSemaphore;
  CHECK_VK_CALL(vkCreateSemaphore(m_device, &imageAcquiredSemaphoreCreateInfo, nullptr, &imageAcquiredSemaphore));

  // Acquire the swapchain image in order to set its layout
  CHECK_VK_CALL(vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, imageAcquiredSemaphore, VK_NULL_HANDLE,
                                      &m_currentBuffer));

  VkAttachmentDescription attachments[2];
  attachments[0].format = m_surfaceFormat.format;
  attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  attachments[0].flags = 0;

  attachments[1].format = VK_FORMAT_D16_UNORM;//info.depth.format;
  attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  attachments[1].flags = 0;

  VkAttachmentReference colorReference = {};
  colorReference.attachment = 0;
  colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference depthReference = {};
  depthReference.attachment = 1;
  depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.flags = 0;
  subpass.inputAttachmentCount = 0;
  subpass.pInputAttachments = nullptr;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorReference;
  subpass.pResolveAttachments = nullptr;
  subpass.pDepthStencilAttachment = &depthReference;
  subpass.preserveAttachmentCount = 0;
  subpass.pPreserveAttachments = nullptr;

  VkRenderPassCreateInfo rpInfo = {};
  rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rpInfo.pNext = nullptr;
  rpInfo.attachmentCount = 2;
  rpInfo.pAttachments = attachments;
  rpInfo.subpassCount = 1;
  rpInfo.pSubpasses = &subpass;
  rpInfo.dependencyCount = 0;
  rpInfo.pDependencies = nullptr;

  CHECK_VK_CALL(vkCreateRenderPass(m_device, &rpInfo, nullptr, &m_renderPass));
}

void VulkanBaseContext::CreateCommandBuffer()
{
  VkCommandPoolCreateInfo commandPoolCreateInfo;
  commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  commandPoolCreateInfo.pNext = nullptr;
  commandPoolCreateInfo.flags = 0;
  commandPoolCreateInfo.queueFamilyIndex = m_queueFamilyIndex;

  CHECK_VK_CALL(vkCreateCommandPool(m_device, &commandPoolCreateInfo, NULL, &m_commandPool));

  uint32_t const swapchainImageCount = static_cast<uint32_t>(m_swapchainImageViews.size());
  VkCommandBufferAllocateInfo commandBufferAllocateInfo;
  commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  commandBufferAllocateInfo.pNext = nullptr;
  commandBufferAllocateInfo.commandPool = m_commandPool;
  commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  commandBufferAllocateInfo.commandBufferCount = swapchainImageCount;

  // TODO(darina, rokuz): Isn't it enough single command buffer?
  m_commandBuffers.resize(swapchainImageCount);
  CHECK_VK_CALL(vkAllocateCommandBuffers(m_device, &commandBufferAllocateInfo, m_commandBuffers.data()));

  //VkCommandBufferBeginInfo commandBufferBeginInfo;
  //commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  //commandBufferBeginInfo.pNext = nullptr;
  //commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
  //commandBufferBeginInfo.pInheritanceInfo = nullptr;
}

void VulkanBaseContext::SetSurface(VkSurfaceKHR surface, VkSurfaceFormatKHR surfaceFormat,
                                   VkSurfaceCapabilitiesKHR surfaceCapabilities)
{
  m_surface = surface;
  m_surfaceFormat = surfaceFormat;
  m_surfaceCapabilities = surfaceCapabilities;

  CreateSwapchain();
  CreateImageViews();
  // TODO(darina, rokuz): Allocate memory for images.
  CreateRenderPass();
  CreateCommandBuffer();
}

void VulkanBaseContext::ResetSurface()
{
  vkFreeCommandBuffers(m_device, m_commandPool, static_cast<uint32_t>(m_commandBuffers.size()),
                       m_commandBuffers.data());
  m_commandBuffers.clear();

  vkDestroyCommandPool(m_device, m_commandPool, nullptr);

  vkDestroyRenderPass(m_device, m_renderPass, nullptr);

  vkDestroySemaphore(m_device, m_imageAcquiredSemaphore, nullptr);

  for (auto const & imageView : m_swapchainImageViews)
    vkDestroyImageView(m_device, imageView, nullptr);
  m_swapchainImageViews.clear();

  vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);

  m_surface.reset();
}
}  // namespace vulkan
}  // namespace dp
