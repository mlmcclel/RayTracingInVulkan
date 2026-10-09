#pragma once

#include "Vulkan.hpp"

namespace Vulkan
{
	class Device;

	class DeviceMemory final
	{
	public:

		DeviceMemory(const DeviceMemory&) = delete;
		DeviceMemory& operator = (const DeviceMemory&) = delete;
		DeviceMemory& operator = (DeviceMemory&&) = delete;

		DeviceMemory(const Device& device, size_t size, uint32_t memoryTypeBits, VkMemoryAllocateFlags allocateFlags, VkMemoryPropertyFlags propertyFlags,
			VkExternalMemoryHandleTypeFlagBits externalHandleType = {}, VkBuffer dedicatedBuffer = VK_NULL_HANDLE);
		DeviceMemory(DeviceMemory&& other) noexcept;
		~DeviceMemory();

		const class Device& Device() const { return device_; }

		void* Map(size_t offset, size_t size);
		void Unmap();
		void* ExportWin32Handle() const;

	private:

		uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

		const class Device& device_;

		VULKAN_HANDLE(VkDeviceMemory, memory_)
	};

}
