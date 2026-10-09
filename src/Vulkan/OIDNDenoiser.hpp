#pragma once

#include "Vulkan.hpp"
#include <OpenImageDenoise/oidn.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace Vulkan
{
	struct OIDNDeviceInfo
	{
		int Id;
		oidn::DeviceType Type;
		std::string Name;
	};

	class DenoiserOIDN final
	{
	public:

		static std::vector<OIDNDeviceInfo> EnumerateDevices();

		DenoiserOIDN(int physicalDeviceId, int cpuPhysicalDeviceId);
		void Denoise(const uint8_t* input, uint8_t* output, VkExtent2D extent, VkFormat format);
		bool CanShareWith(VkPhysicalDevice physicalDevice) const;
		bool IsSYCLDevice() const;
		void AddSharedFrame(void* inputHandle, size_t inputSize, void* outputHandle, size_t outputSize, VkExtent2D extent);
		void DenoiseShared(size_t frameIndex);
		void ClearSharedFrames();
		void UseHostStaging(const std::string& reason);
		void SetDevice(int physicalDeviceId, int cpuPhysicalDeviceId);
		void Disable(const std::string& reason);
		bool IsUsable() const { return device_ && physicalDeviceId_ >= 0; }
		int PhysicalDeviceId() const { return physicalDeviceId_; }
		int RequestedPhysicalDeviceId() const { return requestedPhysicalDeviceId_; }
		const std::string& Status() const { return status_; }

	private:

		void Resize(VkExtent2D extent);
		void InitializeDevice(int physicalDeviceId);
		void ExecuteFilter();
		void CheckError(const char* operation);

		oidn::DeviceRef device_;
		oidn::FilterRef filter_;
		oidn::BufferRef colorBuffer_;
		oidn::BufferRef denoisedBuffer_;
		struct SharedFrame
		{
			oidn::BufferRef input;
			oidn::BufferRef output;
			oidn::FilterRef filter;
		};
		std::vector<SharedFrame> sharedFrames_;
		int physicalDeviceId_{ -1 };
		int requestedPhysicalDeviceId_{ -1 };
		int cpuPhysicalDeviceId_{ -1 };
		VkExtent2D extent_{};
		std::vector<float> color_;
		std::vector<float> denoised_;
		std::string status_;
	};
}