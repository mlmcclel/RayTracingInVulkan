#include "DeviceMemory.hpp"
#include "Device.hpp"
#include "Utilities/Exception.hpp"

namespace Vulkan {

DeviceMemory::DeviceMemory(
	const class Device& device, 
	const size_t size, 
	const uint32_t memoryTypeBits,
	const VkMemoryAllocateFlags allocateFlags,
	const VkMemoryPropertyFlags propertyFlags,
	const VkExternalMemoryHandleTypeFlagBits externalHandleType,
	const VkBuffer dedicatedBuffer) :
	device_(device)
{
	VkMemoryDedicatedAllocateInfo dedicatedInfo{};
	dedicatedInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
	dedicatedInfo.buffer = dedicatedBuffer;
	VkExportMemoryAllocateInfo exportInfo{};
	exportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
	exportInfo.handleTypes = externalHandleType;
	exportInfo.pNext = dedicatedBuffer ? &dedicatedInfo : nullptr;
	VkMemoryAllocateFlagsInfo flagsInfo = {};
	flagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
	flagsInfo.pNext = externalHandleType ? &exportInfo : nullptr;
	flagsInfo.flags = allocateFlags;
	
	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.pNext = &flagsInfo;
	allocInfo.allocationSize = size;
	allocInfo.memoryTypeIndex = FindMemoryType(memoryTypeBits, propertyFlags);

	Check(vkAllocateMemory(device.Handle(), &allocInfo, nullptr, &memory_),
		"allocate memory");
}

void* DeviceMemory::ExportWin32Handle() const
{
#ifdef _WIN32
	const auto getHandle = reinterpret_cast<PFN_vkGetMemoryWin32HandleKHR>(vkGetDeviceProcAddr(device_.Handle(), "vkGetMemoryWin32HandleKHR"));
	if (!getHandle)
	{
		throw std::runtime_error("external Win32 memory export is unavailable");
	}
	VkMemoryGetWin32HandleInfoKHR info{};
	info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
	info.memory = memory_;
	info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
	HANDLE handle = nullptr;
	Check(getHandle(device_.Handle(), &info, &handle), "export Win32 memory handle");
	return handle;
#else
	throw std::runtime_error("Win32 memory export is unavailable on this platform");
#endif
}

DeviceMemory::DeviceMemory(DeviceMemory&& other) noexcept :
	device_(other.device_),
	memory_(other.memory_)
{
	other.memory_ = nullptr;
}

DeviceMemory::~DeviceMemory()
{
	if (memory_ != nullptr)
	{
		vkFreeMemory(device_.Handle(), memory_, nullptr);
		memory_ = nullptr;
	}
}

void* DeviceMemory::Map(const size_t offset, const size_t size)
{
	void* data;
	Check(vkMapMemory(device_.Handle(), memory_, offset, size, 0, &data),
		"map memory");

	return data;
}

void DeviceMemory::Unmap()
{
	vkUnmapMemory(device_.Handle(), memory_);
}

uint32_t DeviceMemory::FindMemoryType(const uint32_t typeFilter, const VkMemoryPropertyFlags propertyFlags) const
{
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(device_.PhysicalDevice(), &memProperties);

	for (uint32_t i = 0; i != memProperties.memoryTypeCount; ++i)
	{
		if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & propertyFlags) == propertyFlags)
		{
			return i;
		}
	}

	Throw(std::runtime_error("failed to find suitable memory type"));
}

}
