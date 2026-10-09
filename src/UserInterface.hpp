#pragma once
#include "Vulkan/Vulkan.hpp"
#include "Vulkan/OIDNDenoiser.hpp"
#include <memory>

namespace Vulkan
{
	class CommandPool;
	class DepthBuffer;
	class DescriptorPool;
	class FrameBuffer;
	class RenderPass;
	class SwapChain;
}

struct UserSettings;

struct Statistics final
{
	VkExtent2D FramebufferSize;
	float FrameRate;
	float RayRate;
	uint32_t TotalSamples;
};

class UserInterface final
{
public:

	VULKAN_NON_COPIABLE(UserInterface)

	UserInterface(
		Vulkan::CommandPool& commandPool, 
		const Vulkan::SwapChain& swapChain, 
		const Vulkan::DepthBuffer& depthBuffer,
		UserSettings& userSettings,
		const std::vector<Vulkan::OIDNDeviceInfo>& oidnDevices);
	~UserInterface();

	void Render(VkCommandBuffer commandBuffer, const Vulkan::FrameBuffer& frameBuffer, const Statistics& statistics, const std::string& denoiserStatus);

	bool WantsToCaptureKeyboard() const;
	bool WantsToCaptureMouse() const;

	UserSettings& Settings() { return userSettings_; }

private:

	void DrawSettings(const std::string& denoiserStatus);
	void DrawOverlay(const Statistics& statistics);

	std::unique_ptr<Vulkan::DescriptorPool> descriptorPool_;
	std::unique_ptr<Vulkan::RenderPass> renderPass_;
	UserSettings& userSettings_;
	const std::vector<Vulkan::OIDNDeviceInfo>& oidnDevices_;
};
