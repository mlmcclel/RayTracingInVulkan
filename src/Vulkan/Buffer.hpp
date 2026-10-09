#pragma once

#include "Vulkan.hpp"
#include "DeviceMemory.hpp"


namespace Vulkan
{
	class CommandPool;
	class Device;

	class Buffer final
	{
	public:

		VULKAN_NON_COPIABLE(Buffer)

		Buffer(const Device& device, size_t size, VkBufferUsageFlags usage, VkExternalMemoryHandleTypeFlagBits externalHandleType = {});
		~Buffer();

		const class Device& Device() const { return device_; }

		DeviceMemory AllocateMemory(VkMemoryPropertyFlags propertyFlags);
		DeviceMemory AllocateMemory(VkMemoryAllocateFlags allocateFlags, VkMemoryPropertyFlags propertyFlags);
		DeviceMemory AllocateExternalMemory(VkMemoryPropertyFlags propertyFlags);
		VkMemoryRequirements GetMemoryRequirements() const;
		VkDeviceAddress GetDeviceAddress() const;

		void CopyFrom(CommandPool& commandPool, const Buffer& src, VkDeviceSize size);

	private:

		const class Device& device_;
		VkExternalMemoryHandleTypeFlagBits externalHandleType_{};

		VULKAN_HANDLE(VkBuffer, buffer_)
	};
}
