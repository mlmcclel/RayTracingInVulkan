#include "Application.hpp"
#include "BottomLevelAccelerationStructure.hpp"
#include "DeviceProcedures.hpp"
#include "RayTracingPipeline.hpp"
#include "ShaderBindingTable.hpp"
#include "TopLevelAccelerationStructure.hpp"
#include "Assets/Model.hpp"
#include "Assets/Scene.hpp"
#include "Utilities/Glm.hpp"
#include "Vulkan/Buffer.hpp"
#include "Vulkan/BufferUtil.hpp"
#include "Vulkan/DescriptorBinding.hpp"
#include "Vulkan/DescriptorSetLayout.hpp"
#include "Vulkan/DescriptorSetManager.hpp"
#include "Vulkan/DescriptorSets.hpp"
#include "Vulkan/Enumerate.hpp"
#include "Vulkan/Image.hpp"
#include "Vulkan/ImageMemoryBarrier.hpp"
#include "Vulkan/ImageView.hpp"
#include "Vulkan/OIDNDenoiser.hpp"
#include "Vulkan/PipelineLayout.hpp"
#include "Vulkan/SingleTimeCommands.hpp"
#include "Vulkan/ShaderModule.hpp"
#include "Vulkan/SwapChain.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <numeric>
#include <stdexcept>


namespace Vulkan::RayTracing {

struct Application::DenoiserFrame
{
	std::unique_ptr<DeviceMemory> readbackMemory;
	std::unique_ptr<Buffer> readbackBuffer;
	std::unique_ptr<DeviceMemory> uploadMemory;
	std::unique_ptr<Buffer> uploadBuffer;
	std::unique_ptr<DeviceMemory> sharedInputMemory;
	std::unique_ptr<Buffer> sharedInput;
	std::unique_ptr<DeviceMemory> sharedOutputMemory;
	std::unique_ptr<Buffer> sharedOutput;
	std::unique_ptr<DeviceMemory> packedMemory;
	std::unique_ptr<Buffer> packedBuffer;
	bool hasReadback{};
	bool hasDenoisedOutput{};
};

namespace
{
	class ScopedMemoryMap final
	{
	public:
		ScopedMemoryMap(DeviceMemory& memory, const VkDeviceSize size) :
			memory_(memory),
			data_(memory.Map(0, static_cast<size_t>(size)))
		{
		}

		~ScopedMemoryMap()
		{
			memory_.Unmap();
		}

		void* Data() const { return data_; }

	private:
		DeviceMemory& memory_;
		void* data_;
	};

	void ExternalBufferBarrier(const VkCommandBuffer commandBuffer, const VkBuffer buffer,
		const uint32_t sourceFamily, const uint32_t destinationFamily,
		const VkAccessFlags sourceAccess, const VkAccessFlags destinationAccess,
		const VkPipelineStageFlags sourceStage, const VkPipelineStageFlags destinationStage)
	{
		VkBufferMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		barrier.srcAccessMask = sourceAccess;
		barrier.dstAccessMask = destinationAccess;
		barrier.srcQueueFamilyIndex = sourceFamily;
		barrier.dstQueueFamilyIndex = destinationFamily;
		barrier.buffer = buffer;
		barrier.size = VK_WHOLE_SIZE;
		vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 1, &barrier, 0, nullptr);
	}

	template <class TAccelerationStructure>
	VkAccelerationStructureBuildSizesInfoKHR GetTotalRequirements(const std::vector<TAccelerationStructure>& accelerationStructures)
	{
		VkAccelerationStructureBuildSizesInfoKHR total{};

		for (const auto& accelerationStructure : accelerationStructures)
		{
			total.accelerationStructureSize += accelerationStructure.BuildSizes().accelerationStructureSize;
			total.buildScratchSize += accelerationStructure.BuildSizes().buildScratchSize;
			total.updateScratchSize += accelerationStructure.BuildSizes().updateScratchSize;
		}

		return total;
	}
}

Application::Application(const WindowConfig& windowConfig, const VkPresentModeKHR presentMode, const bool enableValidationLayers) :
	Vulkan::Application(windowConfig, presentMode, enableValidationLayers)
{
}

Application::~Application()
{
	Application::DeleteSwapChain();
	DeleteAccelerationStructures();

	rayTracingProperties_.reset();
	deviceProcedures_.reset();
}

void Application::SetPhysicalDevice(
	VkPhysicalDevice physicalDevice,
	std::vector<const char*>& requiredExtensions,
	VkPhysicalDeviceFeatures& deviceFeatures,
	void* nextDeviceFeatures)
{
#ifdef _WIN32
	const auto availableExtensions = GetEnumerateVector(physicalDevice, static_cast<const char*>(nullptr), vkEnumerateDeviceExtensionProperties);
	externalMemoryAvailable_ = std::any_of(availableExtensions.begin(), availableExtensions.end(), [](const VkExtensionProperties& extension)
	{
		return std::strcmp(extension.extensionName, VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME) == 0;
	});
	VkPhysicalDeviceExternalBufferInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO;
	bufferInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
	bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	VkExternalBufferProperties properties{};
	properties.sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES;
	vkGetPhysicalDeviceExternalBufferProperties(physicalDevice, &bufferInfo, &properties);
	externalMemoryAvailable_ = externalMemoryAvailable_ &&
		(properties.externalMemoryProperties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT);
	if (externalMemoryAvailable_)
	{
		requiredExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
	}
#endif
	// Required extensions.
	requiredExtensions.insert(requiredExtensions.end(),
	{	
		VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
		VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
		VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME
	});

	// Required device features.
	VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures = {};
	bufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
	bufferDeviceAddressFeatures.pNext = nextDeviceFeatures;
	bufferDeviceAddressFeatures.bufferDeviceAddress = true;

	VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures = {};
	indexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
	indexingFeatures.pNext = &bufferDeviceAddressFeatures;
	indexingFeatures.runtimeDescriptorArray = true;
	indexingFeatures.shaderSampledImageArrayNonUniformIndexing = true;

	VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures = {};
	accelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
	accelerationStructureFeatures.pNext = &indexingFeatures;
	accelerationStructureFeatures.accelerationStructure = true;
	
	VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingFeatures = {};
	rayTracingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
	rayTracingFeatures.pNext = &accelerationStructureFeatures;
	rayTracingFeatures.rayTracingPipeline = true;

	Vulkan::Application::SetPhysicalDevice(physicalDevice, requiredExtensions, deviceFeatures, &rayTracingFeatures);
}

void Application::OnDeviceSet()
{
	Vulkan::Application::OnDeviceSet();
	if (externalMemoryAvailable_)
	{
		const auto families = GetEnumerateVector(Device().PhysicalDevice(), vkGetPhysicalDeviceQueueFamilyProperties);
		externalMemoryAvailable_ = (families[Device().GraphicsFamilyIndex()].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
	}

	deviceProcedures_.reset(new DeviceProcedures(Device()));
	rayTracingProperties_.reset(new RayTracingProperties(Device()));
}

void Application::CreateAccelerationStructures()
{
	const auto timer = std::chrono::high_resolution_clock::now();

	SingleTimeCommands::Submit(CommandPool(), [this](VkCommandBuffer commandBuffer)
	{
		CreateBottomLevelStructures(commandBuffer);
		CreateTopLevelStructures(commandBuffer);
	});

	topScratchBuffer_.reset();
	topScratchBufferMemory_.reset();
	bottomScratchBuffer_.reset();
	bottomScratchBufferMemory_.reset();

	const auto elapsed = std::chrono::duration<float, std::chrono::seconds::period>(std::chrono::high_resolution_clock::now() - timer).count();
	std::cout << "- built acceleration structures in " << elapsed << "s" << std::endl;
}

void Application::DeleteAccelerationStructures()
{
	topAs_.clear();
	instancesBuffer_.reset();
	instancesBufferMemory_.reset();
	topScratchBuffer_.reset();
	topScratchBufferMemory_.reset();
	topBuffer_.reset();
	topBufferMemory_.reset();

	bottomAs_.clear();
	bottomScratchBuffer_.reset();
	bottomScratchBufferMemory_.reset();
	bottomBuffer_.reset();
	bottomBufferMemory_.reset();
}

const std::string& Application::DenoiserStatus() const
{
	static const std::string noStatus;
	return denoiser_ ? denoiser_->Status() : noStatus;
}

void Application::CreateSwapChain()
{
	Vulkan::Application::CreateSwapChain();

	CreateOutputImage();

	rayTracingPipeline_.reset(new RayTracingPipeline(*deviceProcedures_, SwapChain(), topAs_[0], *accumulationImageView_, *outputImageView_, UniformBuffers(), GetScene()));

	const std::vector<ShaderBindingTable::Entry> rayGenPrograms = { {rayTracingPipeline_->RayGenShaderIndex(), {}} };
	const std::vector<ShaderBindingTable::Entry> missPrograms = { {rayTracingPipeline_->MissShaderIndex(), {}} };
	const std::vector<ShaderBindingTable::Entry> hitGroups = { {rayTracingPipeline_->TriangleHitGroupIndex(), {}}, {rayTracingPipeline_->ProceduralHitGroupIndex(), {}} };

	shaderBindingTable_.reset(new ShaderBindingTable(*deviceProcedures_, *rayTracingPipeline_, *rayTracingProperties_, rayGenPrograms, missPrograms, hitGroups));
}

void Application::DeleteSwapChain()
{
	if (denoiser_)
	{
		denoiser_->ClearSharedFrames();
	}
	DestroyConversionPipeline();
	sharedMode_ = false;
	denoiserFrames_.clear();
	shaderBindingTable_.reset();
	rayTracingPipeline_.reset();
	outputImageView_.reset();
	outputImage_.reset();
	outputImageMemory_.reset();
	accumulationImageView_.reset();
	accumulationImage_.reset();
	accumulationImageMemory_.reset();

	Vulkan::Application::DeleteSwapChain();
}

void Application::Render(VkCommandBuffer commandBuffer, const size_t currentFrame, const uint32_t imageIndex)
{
	const auto extent = SwapChain().Extent();
	bool denoisingEnabled = IsDenoisingEnabled();
	if (denoisingEnabled)
	{
		if (!denoiser_)
		{
			denoiser_.reset(new Vulkan::DenoiserOIDN(OIDNPhysicalDeviceId(), OIDNCpuPhysicalDeviceId()));
		}
		else if (denoiser_->RequestedPhysicalDeviceId() != OIDNPhysicalDeviceId())
		{
			Device().WaitIdle();
			denoiser_->ClearSharedFrames();
			DestroyConversionPipeline();
			denoiserFrames_.clear();
			sharedMode_ = false;
			sharedFailedDeviceId_ = -1;
			denoiser_->SetDevice(OIDNPhysicalDeviceId(), OIDNCpuPhysicalDeviceId());
		}
		denoisingEnabled = denoiser_->IsUsable();
	}
	const bool wantsShared = denoisingEnabled && externalMemoryAvailable_ &&
		OIDNPhysicalDeviceId() != sharedFailedDeviceId_ && denoiser_->CanShareWith(Device().PhysicalDevice());
	if (!denoiserFrames_.empty() && sharedMode_ != wantsShared)
	{
		Device().WaitIdle();
		denoiser_->ClearSharedFrames();
		DestroyConversionPipeline();
		denoiserFrames_.clear();
		sharedMode_ = false;
	}

	if (denoisingEnabled != denoisingEnabled_ || sharedMode_ != wantsShared)
	{
		for (const auto& frame : denoiserFrames_)
		{
			frame->hasReadback = false;
			frame->hasDenoisedOutput = false;
		}
		denoisingEnabled_ = denoisingEnabled;
	}

	if (denoisingEnabled)
	{
		if (denoiserFrames_.empty())
		{
			if (wantsShared)
			{
				try
				{
					CreateDenoiserFrames(true);
					CreateConversionPipeline();
					sharedMode_ = true;
				}
				catch (const std::exception& exception)
				{
					denoiser_->ClearSharedFrames();
					DestroyConversionPipeline();
					denoiserFrames_.clear();
					sharedFailedDeviceId_ = OIDNPhysicalDeviceId();
					denoiser_->UseHostStaging(exception.what());
				}
			}
			if (!sharedMode_)
			{
				if (!wantsShared && denoiser_->IsSYCLDevice() && sharedFailedDeviceId_ != OIDNPhysicalDeviceId())
				{
					denoiser_->UseHostStaging("external-memory sharing unavailable");
				}
				CreateDenoiserFrames(false);
			}
			std::cout << "OIDN: " << (sharedMode_ ? "shared GPU buffers" : "host staging")
				<< " (" << denoiser_->Status() << ")" << std::endl;
		}

		auto& frame = *denoiserFrames_[currentFrame];
		frame.hasDenoisedOutput = frame.hasReadback;
		if (frame.hasReadback)
		{
			try
			{
				if (sharedMode_)
				{
					denoiser_->DenoiseShared(currentFrame);
				}
				else
				{
					const VkDeviceSize byteSize = static_cast<VkDeviceSize>(extent.width) * extent.height * 4;
					ScopedMemoryMap input(*frame.readbackMemory, byteSize);
					ScopedMemoryMap output(*frame.uploadMemory, byteSize);
					denoiser_->Denoise(static_cast<const uint8_t*>(input.Data()), static_cast<uint8_t*>(output.Data()), extent, SwapChain().Format());
				}
			}
			catch (const std::exception& exception)
			{
				std::cerr << "OIDN disabled for this frame: " << exception.what() << std::endl;
				denoiser_->Disable(exception.what());
				frame.hasDenoisedOutput = false;
				frame.hasReadback = false;
			}
		}
	}

	VkDescriptorSet descriptorSets[] = { rayTracingPipeline_->DescriptorSet(currentFrame) };

	VkImageSubresourceRange subresourceRange = {};
	subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subresourceRange.baseMipLevel = 0;
	subresourceRange.levelCount = 1;
	subresourceRange.baseArrayLayer = 0;
	subresourceRange.layerCount = 1;

	// Acquire destination images for rendering.
	ImageMemoryBarrier::Insert(commandBuffer, accumulationImage_->Handle(), subresourceRange, 0,
		VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

	ImageMemoryBarrier::Insert(commandBuffer, outputImage_->Handle(), subresourceRange, 0,
		VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

	// Bind ray tracing pipeline.
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rayTracingPipeline_->Handle());
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rayTracingPipeline_->PipelineLayout().Handle(), 0, 1, descriptorSets, 0, nullptr);

	// Describe the shader binding table.
	VkStridedDeviceAddressRegionKHR raygenShaderBindingTable = {};
	raygenShaderBindingTable.deviceAddress = shaderBindingTable_->RayGenDeviceAddress();
	raygenShaderBindingTable.stride = shaderBindingTable_->RayGenEntrySize();
	raygenShaderBindingTable.size = shaderBindingTable_->RayGenSize();

	VkStridedDeviceAddressRegionKHR missShaderBindingTable = {};
	missShaderBindingTable.deviceAddress = shaderBindingTable_->MissDeviceAddress();
	missShaderBindingTable.stride = shaderBindingTable_->MissEntrySize();
	missShaderBindingTable.size = shaderBindingTable_->MissSize();

	VkStridedDeviceAddressRegionKHR hitShaderBindingTable = {};
	hitShaderBindingTable.deviceAddress = shaderBindingTable_->HitGroupDeviceAddress();
	hitShaderBindingTable.stride = shaderBindingTable_->HitGroupEntrySize();
	hitShaderBindingTable.size = shaderBindingTable_->HitGroupSize();

	VkStridedDeviceAddressRegionKHR callableShaderBindingTable = {};

	// Execute ray tracing shaders.
	deviceProcedures_->vkCmdTraceRaysKHR(commandBuffer,
		&raygenShaderBindingTable, &missShaderBindingTable, &hitShaderBindingTable, &callableShaderBindingTable,
		extent.width, extent.height, 1);

	if (denoisingEnabled && sharedMode_)
	{
		auto& frame = *denoiserFrames_[currentFrame];
		const auto graphicsFamily = Device().GraphicsFamilyIndex();
		ImageMemoryBarrier::Insert(commandBuffer, outputImage_->Handle(), subresourceRange,
			VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, conversionPipeline_);
		const VkDescriptorSet descriptorSet = conversionDescriptors_->DescriptorSets().Handle(currentFrame);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, conversionLayout_, 0, 1, &descriptorSet, 0, nullptr);
		const uint32_t bgra = SwapChain().Format() == VK_FORMAT_B8G8R8A8_UNORM || SwapChain().Format() == VK_FORMAT_B8G8R8A8_SRGB;
		const uint32_t parameters[4] = {extent.width, extent.height, 1, bgra};
		if (frame.hasReadback)
		{
			ExternalBufferBarrier(commandBuffer, frame.sharedInput->Handle(), VK_QUEUE_FAMILY_EXTERNAL, graphicsFamily,
				0, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		}
		if (frame.hasDenoisedOutput)
		{
			ExternalBufferBarrier(commandBuffer, frame.sharedOutput->Handle(), VK_QUEUE_FAMILY_EXTERNAL, graphicsFamily,
				0, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
			vkCmdPushConstants(commandBuffer, conversionLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(parameters), parameters);
			vkCmdDispatch(commandBuffer, (extent.width + 7) / 8, (extent.height + 7) / 8, 1);
			ExternalBufferBarrier(commandBuffer, frame.sharedOutput->Handle(), graphicsFamily, VK_QUEUE_FAMILY_EXTERNAL,
				VK_ACCESS_SHADER_READ_BIT, 0, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
			ExternalBufferBarrier(commandBuffer, frame.sharedInput->Handle(), VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
				VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		}
		const uint32_t captureParameters[4] = {extent.width, extent.height, 0, bgra};
		vkCmdPushConstants(commandBuffer, conversionLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(captureParameters), captureParameters);
		vkCmdDispatch(commandBuffer, (extent.width + 7) / 8, (extent.height + 7) / 8, 1);
		ExternalBufferBarrier(commandBuffer, frame.sharedInput->Handle(), graphicsFamily, VK_QUEUE_FAMILY_EXTERNAL,
			VK_ACCESS_SHADER_WRITE_BIT, 0, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
	}

	// Acquire output image and swap-chain image for copying.
	ImageMemoryBarrier::Insert(commandBuffer, outputImage_->Handle(), subresourceRange, 
		sharedMode_ && denoisingEnabled ? VK_ACCESS_SHADER_READ_BIT : VK_ACCESS_SHADER_WRITE_BIT,
		VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

	ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange, 0,
		VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	VkBufferImageCopy copyRegion{};
	copyRegion.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	copyRegion.imageExtent = { extent.width, extent.height, 1 };

	if (denoisingEnabled && denoiserFrames_[currentFrame]->hasDenoisedOutput)
	{
		if (sharedMode_)
		{
			const auto packed = denoiserFrames_[currentFrame]->packedBuffer->Handle();
			ExternalBufferBarrier(commandBuffer, packed, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
				VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
			vkCmdCopyBufferToImage(commandBuffer, packed, SwapChain().Images()[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
		}
		else
		{
			const auto& uploadBuffer = *denoiserFrames_[currentFrame]->uploadBuffer;
			ExternalBufferBarrier(commandBuffer, uploadBuffer.Handle(), VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
				VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
			vkCmdCopyBufferToImage(commandBuffer, uploadBuffer.Handle(), SwapChain().Images()[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
		}
	}
	else
	{
		VkImageCopy imageCopyRegion{};
		imageCopyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		imageCopyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		imageCopyRegion.extent = { extent.width, extent.height, 1 };
		vkCmdCopyImage(commandBuffer,
			outputImage_->Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			SwapChain().Images()[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &imageCopyRegion);
	}

	if (denoisingEnabled)
	{
		auto& frame = *denoiserFrames_[currentFrame];
		if (!sharedMode_)
		{
			vkCmdCopyImageToBuffer(commandBuffer, outputImage_->Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, frame.readbackBuffer->Handle(), 1, &copyRegion);
		}
		frame.hasReadback = true;
	}

	ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange, VK_ACCESS_TRANSFER_WRITE_BIT,
		0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}

void Application::CreateDenoiserFrames(const bool shared)
{
	denoiserFrames_.clear();
	const auto extent = SwapChain().Extent();
	const VkDeviceSize byteSize = static_cast<VkDeviceSize>(extent.width) * extent.height * 4;
	const auto hostMemory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	for (size_t index = 0; index != SwapChain().Images().size(); ++index)
	{
		auto frame = std::make_unique<DenoiserFrame>();
		if (shared)
		{
#ifdef _WIN32
			const auto floatBytes = static_cast<size_t>(byteSize) * sizeof(float);
			const auto handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
			frame->sharedInput.reset(new Buffer(Device(), floatBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, handleType));
			frame->sharedInputMemory.reset(new DeviceMemory(frame->sharedInput->AllocateExternalMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));
			frame->sharedOutput.reset(new Buffer(Device(), floatBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, handleType));
			frame->sharedOutputMemory.reset(new DeviceMemory(frame->sharedOutput->AllocateExternalMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));
			frame->packedBuffer.reset(new Buffer(Device(), static_cast<size_t>(byteSize), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
			frame->packedMemory.reset(new DeviceMemory(frame->packedBuffer->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));
			HANDLE inputHandle = static_cast<HANDLE>(frame->sharedInputMemory->ExportWin32Handle());
			HANDLE outputHandle = nullptr;
			try
			{
				outputHandle = static_cast<HANDLE>(frame->sharedOutputMemory->ExportWin32Handle());
				denoiser_->AddSharedFrame(inputHandle, frame->sharedInput->GetMemoryRequirements().size,
					outputHandle, frame->sharedOutput->GetMemoryRequirements().size, extent);
			}
			catch (...)
			{
				CloseHandle(inputHandle);
				if (outputHandle) CloseHandle(outputHandle);
				throw;
			}
			CloseHandle(inputHandle);
			CloseHandle(outputHandle);
#else
			throw std::runtime_error("Win32 shared memory unavailable");
#endif
		}
		else
		{
			frame->readbackBuffer.reset(new Buffer(Device(), static_cast<size_t>(byteSize), VK_BUFFER_USAGE_TRANSFER_DST_BIT));
			frame->readbackMemory.reset(new DeviceMemory(frame->readbackBuffer->AllocateMemory(hostMemory)));
			frame->uploadBuffer.reset(new Buffer(Device(), static_cast<size_t>(byteSize), VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
			frame->uploadMemory.reset(new DeviceMemory(frame->uploadBuffer->AllocateMemory(hostMemory)));
		}
		denoiserFrames_.push_back(std::move(frame));
	}
	if (shared)
	{
		SingleTimeCommands::Submit(CommandPool(), [this](VkCommandBuffer commandBuffer)
		{
			for (const auto& frame : denoiserFrames_)
			{
				ExternalBufferBarrier(commandBuffer, frame->sharedOutput->Handle(), Device().GraphicsFamilyIndex(), VK_QUEUE_FAMILY_EXTERNAL,
					0, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
			}
		});
	}
}

void Application::CreateConversionPipeline()
{
	const std::vector<DescriptorBinding> bindings =
	{
		{0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
		{1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
		{2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
		{3, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT}
	};
	conversionDescriptors_.reset(new DescriptorSetManager(Device(), bindings, denoiserFrames_.size()));
	auto& descriptors = conversionDescriptors_->DescriptorSets();
	for (size_t index = 0; index != denoiserFrames_.size(); ++index)
	{
		const auto& frame = *denoiserFrames_[index];
		VkDescriptorImageInfo image{};
		image.imageView = outputImageView_->Handle();
		image.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		VkDescriptorBufferInfo input{frame.sharedInput->Handle(), 0, VK_WHOLE_SIZE};
		VkDescriptorBufferInfo output{frame.sharedOutput->Handle(), 0, VK_WHOLE_SIZE};
		VkDescriptorBufferInfo packed{frame.packedBuffer->Handle(), 0, VK_WHOLE_SIZE};
		descriptors.UpdateDescriptors({descriptors.Bind(index, 0, image), descriptors.Bind(index, 1, input),
			descriptors.Bind(index, 2, output), descriptors.Bind(index, 3, packed)});
	}
	VkPushConstantRange constants{};
	constants.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	constants.size = 4 * sizeof(uint32_t);
	const auto layout = conversionDescriptors_->DescriptorSetLayout().Handle();
	VkPipelineLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 1;
	layoutInfo.pSetLayouts = &layout;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &constants;
	Check(vkCreatePipelineLayout(Device().Handle(), &layoutInfo, nullptr, &conversionLayout_), "create OIDN conversion pipeline layout");
	const ShaderModule shader(Device(), "../assets/shaders/OIDNConvert.comp.spv");
	VkComputePipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipelineInfo.stage = shader.CreateShaderStage(VK_SHADER_STAGE_COMPUTE_BIT);
	pipelineInfo.layout = conversionLayout_;
	Check(vkCreateComputePipelines(Device().Handle(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &conversionPipeline_),
		"create OIDN conversion pipeline");
}

void Application::DestroyConversionPipeline()
{
	if (conversionPipeline_) vkDestroyPipeline(Device().Handle(), conversionPipeline_, nullptr);
	if (conversionLayout_) vkDestroyPipelineLayout(Device().Handle(), conversionLayout_, nullptr);
	conversionPipeline_ = VK_NULL_HANDLE;
	conversionLayout_ = VK_NULL_HANDLE;
	conversionDescriptors_.reset();
}

void Application::CreateBottomLevelStructures(VkCommandBuffer commandBuffer)
{
	const auto& scene = GetScene();
	const auto& debugUtils = Device().DebugUtils();
	
	// Bottom level acceleration structure
	// Triangles via vertex buffers. Procedurals via AABBs.
	uint32_t vertexOffset = 0;
	uint32_t indexOffset = 0;
	uint32_t aabbOffset = 0;

	for (const auto& model : scene.Models())
	{
		const auto vertexCount = static_cast<uint32_t>(model.NumberOfVertices());
		const auto indexCount = static_cast<uint32_t>(model.NumberOfIndices());
		BottomLevelGeometry geometries;
		
		model.Procedural()
			? geometries.AddGeometryAabb(scene, aabbOffset, 1, true)
			: geometries.AddGeometryTriangles(scene, vertexOffset, vertexCount, indexOffset, indexCount, true);

		bottomAs_.emplace_back(*deviceProcedures_, *rayTracingProperties_, geometries);

		vertexOffset += vertexCount * sizeof(Assets::Vertex);
		indexOffset += indexCount * sizeof(uint32_t);
		aabbOffset += sizeof(VkAabbPositionsKHR);
	}

	// Allocate the structures memory.
	const auto total = GetTotalRequirements(bottomAs_);

	bottomBuffer_.reset(new Buffer(Device(), total.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT));
	bottomBufferMemory_.reset(new DeviceMemory(bottomBuffer_->AllocateMemory(VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));
	bottomScratchBuffer_.reset(new Buffer(Device(), total.buildScratchSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
	bottomScratchBufferMemory_.reset(new DeviceMemory(bottomScratchBuffer_->AllocateMemory(VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));

	debugUtils.SetObjectName(bottomBuffer_->Handle(), "BLAS Buffer");
	debugUtils.SetObjectName(bottomBufferMemory_->Handle(), "BLAS Memory");
	debugUtils.SetObjectName(bottomScratchBuffer_->Handle(), "BLAS Scratch Buffer");
	debugUtils.SetObjectName(bottomScratchBufferMemory_->Handle(), "BLAS Scratch Memory");

	// Generate the structures.
	VkDeviceSize resultOffset = 0;
	VkDeviceSize scratchOffset = 0;

	for (size_t i = 0; i != bottomAs_.size(); ++i)
	{
		bottomAs_[i].Generate(commandBuffer, *bottomScratchBuffer_, scratchOffset, *bottomBuffer_, resultOffset);
		
		resultOffset += bottomAs_[i].BuildSizes().accelerationStructureSize;
		scratchOffset += bottomAs_[i].BuildSizes().buildScratchSize;

		debugUtils.SetObjectName(bottomAs_[i].Handle(), ("BLAS #" + std::to_string(i)).c_str());
	}
}

void Application::CreateTopLevelStructures(VkCommandBuffer commandBuffer)
{
	const auto& scene = GetScene();
	const auto& debugUtils = Device().DebugUtils();

	// Top level acceleration structure
	std::vector<VkAccelerationStructureInstanceKHR> instances;

	// Hit group 0: triangles
	// Hit group 1: procedurals
	uint32_t instanceId = 0;

	for (const auto& model : scene.Models())
	{
		instances.push_back(TopLevelAccelerationStructure::CreateInstance(
			bottomAs_[instanceId], glm::mat4(1), instanceId, model.Procedural() ? 1 : 0));
		instanceId++;
	}

	// Create and copy instances buffer (do it in a separate one-time synchronous command buffer).
	BufferUtil::CreateDeviceBuffer(CommandPool(), "TLAS Instances", VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, instances, instancesBuffer_, instancesBufferMemory_);

	// Memory barrier for the bottom level acceleration structure builds.
	AccelerationStructure::MemoryBarrier(commandBuffer);
	
	topAs_.emplace_back(*deviceProcedures_, *rayTracingProperties_, instancesBuffer_->GetDeviceAddress(), static_cast<uint32_t>(instances.size()));

	// Allocate the structure memory.
	const auto total = GetTotalRequirements(topAs_);

	topBuffer_.reset(new Buffer(Device(), total.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR));
	topBufferMemory_.reset(new DeviceMemory(topBuffer_->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));

	topScratchBuffer_.reset(new Buffer(Device(), total.buildScratchSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
	topScratchBufferMemory_.reset(new DeviceMemory(topScratchBuffer_->AllocateMemory(VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));

	
	debugUtils.SetObjectName(topBuffer_->Handle(), "TLAS Buffer");
	debugUtils.SetObjectName(topBufferMemory_->Handle(), "TLAS Memory");
	debugUtils.SetObjectName(topScratchBuffer_->Handle(), "TLAS Scratch Buffer");
	debugUtils.SetObjectName(topScratchBufferMemory_->Handle(), "TLAS Scratch Memory");
	debugUtils.SetObjectName(instancesBuffer_->Handle(), "TLAS Instances Buffer");
	debugUtils.SetObjectName(instancesBufferMemory_->Handle(), "TLAS Instances Memory");

	// Generate the structures.
	topAs_[0].Generate(commandBuffer, *topScratchBuffer_, 0, *topBuffer_, 0);

	debugUtils.SetObjectName(topAs_[0].Handle(), "TLAS");
}

void Application::CreateOutputImage()
{
	const auto extent = SwapChain().Extent();
	const auto format = SwapChain().Format();
	const auto tiling = VK_IMAGE_TILING_OPTIMAL;

	accumulationImage_.reset(new Image(Device(), extent, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT));
	accumulationImageMemory_.reset(new DeviceMemory(accumulationImage_->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));
	accumulationImageView_.reset(new ImageView(Device(), accumulationImage_->Handle(), VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT));

	outputImage_.reset(new Image(Device(), extent, format, tiling, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
	outputImageMemory_.reset(new DeviceMemory(outputImage_->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));
	outputImageView_.reset(new ImageView(Device(), outputImage_->Handle(), format, VK_IMAGE_ASPECT_COLOR_BIT));

	const auto& debugUtils = Device().DebugUtils();
	
	debugUtils.SetObjectName(accumulationImage_->Handle(), "Accumulation Image");
	debugUtils.SetObjectName(accumulationImageMemory_->Handle(), "Accumulation Image Memory");
	debugUtils.SetObjectName(accumulationImageView_->Handle(), "Accumulation ImageView");
	
	debugUtils.SetObjectName(outputImage_->Handle(), "Output Image");
	debugUtils.SetObjectName(outputImageMemory_->Handle(), "Output Image Memory");
	debugUtils.SetObjectName(outputImageView_->Handle(), "Output ImageView");

}

}
