#pragma once

#include "drape/graphics_context.hpp"
#include "drape/pointers.hpp"

#include "geometry/point2d.hpp"

#include <vulkan_wrapper.h>
#include <vulkan/vulkan.h>

#include <boost/optional.hpp>

#include <cstdint>
#include <vector>

namespace dp
{
namespace vulkan
{
class VulkanBaseContext : public dp::GraphicsContext
{
public:
  VulkanBaseContext(VkInstance vulkanInstance, VkPhysicalDevice gpu,
                    VkDevice device, uint32_t queueFamilyIndex);

  void Present() override {}
  void MakeCurrent() override {}
  void DoneCurrent() override {}
  bool Validate() override { return true; }
  void Resize(int w, int h) override {}
  void SetFramebuffer(ref_ptr<dp::BaseFramebuffer> framebuffer) override {}
  void ApplyFramebuffer(std::string const & framebufferLabel) override {}
  void Init(ApiVersion apiVersion) override {}
  ApiVersion GetApiVersion() const override { return dp::ApiVersion::Vulkan; }
  std::string GetRendererName() const override;
  std::string GetRendererVersion() const override;

  void DebugSynchronizeWithCPU() override {}
  void PushDebugLabel(std::string const & label) override {}
  void PopDebugLabel() override {}
  
  void SetClearColor(Color const & color) override {}
  void Clear(uint32_t clearBits, uint32_t storeBits) override {}
  void Flush() override {}
  void SetViewport(uint32_t x, uint32_t y, uint32_t w, uint32_t h) override {}
  void SetDepthTestEnabled(bool enabled) override {}
  void SetDepthTestFunction(TestFunction depthFunction) override {}
  void SetStencilTestEnabled(bool enabled) override {}
  void SetStencilFunction(StencilFace face, TestFunction stencilFunction) override {}
  void SetStencilActions(StencilFace face, StencilAction stencilFailAction,
                         StencilAction depthFailAction, StencilAction passAction) override {}
  void SetStencilReferenceValue(uint32_t stencilReferenceValue) override;

  void SetSurface(VkSurfaceKHR surface, VkSurfaceFormatKHR surfaceFormat,
                  VkSurfaceCapabilitiesKHR surfaceCapabilities);
  void ResetSurface();

  VkDevice GetDevice() const { return m_device; }

protected:
  void CreateSwapchain();
  void CreateImageViews();
  void CreateRenderPass();
  void CreateCommandBuffer();

  VkInstance const m_vulkanInstance;
  VkPhysicalDevice const m_gpu;
  VkDevice const m_device;
  uint32_t m_queueFamilyIndex;

  VkSwapchainKHR m_swapchain;
  std::vector<VkImageView> m_swapchainImageViews;
  VkRenderPass m_renderPass;
  VkSemaphore m_imageAcquiredSemaphore;
  uint32_t m_currentBuffer = 0;
  VkCommandPool m_commandPool;
  std::vector<VkCommandBuffer> m_commandBuffers;

  VkPhysicalDeviceProperties m_gpuProperties;

  boost::optional<VkSurfaceKHR> m_surface;
  VkSurfaceCapabilitiesKHR m_surfaceCapabilities;
  VkSurfaceFormatKHR m_surfaceFormat;

  VkPipelineLayout m_pipelineLayout;
  std::vector<VkDescriptorSetLayout> m_descLayout;

  uint32_t m_stencilReferenceValue = 1;
};
}  // namespace vulkan
}  // namespace dp
