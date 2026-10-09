#pragma once

#define NOMINMAX
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#undef APIENTRY
#ifdef _WIN32
#undef MemoryBarrier
#endif

#define VULKAN_NON_COPIABLE(ClassName) \
	ClassName(const ClassName&) = delete; \
	ClassName(ClassName&&) = delete; \
	ClassName& operator = (const ClassName&) = delete; \
	ClassName& operator = (ClassName&&) = delete;

#define VULKAN_HANDLE(VulkanHandleType, name) \
public: \
	VulkanHandleType Handle() const { return name; } \
private: \
	VulkanHandleType name{};

namespace Vulkan
{
	void Check(VkResult result, const char* operation);
	const char* ToString(VkResult result);
}
