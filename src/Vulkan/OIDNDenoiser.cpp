#include "OIDNDenoiser.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace Vulkan
{
	namespace
	{
		bool IsSelectableDeviceType(const oidn::DeviceType type)
		{
			return type == oidn::DeviceType::CPU || type == oidn::DeviceType::SYCL ||
				type == oidn::DeviceType::CUDA || type == oidn::DeviceType::HIP;
		}

		const char* DeviceTypeName(const oidn::DeviceType type)
		{
			switch (type)
			{
			case oidn::DeviceType::CPU: return "CPU";
			case oidn::DeviceType::SYCL: return "SYCL";
			case oidn::DeviceType::CUDA: return "CUDA";
			case oidn::DeviceType::HIP: return "HIP";
			default: return "Unknown";
			}
		}
	}

	std::vector<OIDNDeviceInfo> DenoiserOIDN::EnumerateDevices()
	{
		std::vector<OIDNDeviceInfo> devices;
		const int deviceCount = oidn::getNumPhysicalDevices();
		for (int id = 0; id < deviceCount; ++id)
		{
			const oidn::PhysicalDeviceRef physicalDevice(id);
			const auto type = physicalDevice.get<oidn::DeviceType>("type");
			if (!IsSelectableDeviceType(type))
			{
				continue;
			}

			devices.push_back({ id, type, physicalDevice.get<std::string>("name") });
		}
		return devices;
	}

	DenoiserOIDN::DenoiserOIDN(const int physicalDeviceId, const int cpuPhysicalDeviceId)
	{
		SetDevice(physicalDeviceId, cpuPhysicalDeviceId);
	}

	void DenoiserOIDN::SetDevice(const int physicalDeviceId, const int cpuPhysicalDeviceId)
	{
		requestedPhysicalDeviceId_ = physicalDeviceId;
		cpuPhysicalDeviceId_ = cpuPhysicalDeviceId;
		try
		{
			InitializeDevice(physicalDeviceId);
			const auto devices = EnumerateDevices();
			const auto found = std::find_if(devices.begin(), devices.end(), [physicalDeviceId](const OIDNDeviceInfo& info)
			{
				return info.Id == physicalDeviceId;
			});
			status_ = found == devices.end() ? "OIDN device active" :
				std::string("OIDN ") + DeviceTypeName(found->Type) + ": " + found->Name;
		}
		catch (const std::exception& exception)
		{
			if (physicalDeviceId == cpuPhysicalDeviceId_ || cpuPhysicalDeviceId_ < 0)
			{
				physicalDeviceId_ = -1;
				status_ = std::string("OIDN device unavailable: ") + exception.what();
				return;
			}

			status_ = std::string("OIDN accelerator unavailable; using CPU: ") + exception.what();
			try
			{
				InitializeDevice(cpuPhysicalDeviceId_);
			}
			catch (const std::exception& cpuException)
			{
				physicalDeviceId_ = -1;
				status_ += std::string("; CPU initialization failed: ") + cpuException.what();
			}
		}
	}

	void DenoiserOIDN::InitializeDevice(const int physicalDeviceId)
	{
		ClearSharedFrames();
		filter_.release();
		colorBuffer_.release();
		denoisedBuffer_.release();
		device_.release();
		extent_ = {};
		physicalDeviceId_ = -1;

		device_ = oidn::newDevice(physicalDeviceId);
		if (!device_)
		{
			const char* message = nullptr;
			const auto error = oidn::getError(message);
			throw std::runtime_error(std::string("create OIDN device failed (") +
				(message ? message : (error == oidn::Error::None ? "unknown error" : "device unavailable")) + ")");
		}

		device_.commit();
		CheckError("commit OIDN device");
		physicalDeviceId_ = physicalDeviceId;
	}

	bool DenoiserOIDN::CanShareWith(const VkPhysicalDevice physicalDevice) const
	{
	#ifdef _WIN32
		if (!IsSYCLDevice() ||
			!(device_.get<unsigned int>("externalMemoryTypes") & OIDN_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32))
		{
			return false;
		}

		VkPhysicalDeviceIDProperties id{};
		id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
		VkPhysicalDeviceProperties2 properties{};
		properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		properties.pNext = &id;
		vkGetPhysicalDeviceProperties2(physicalDevice, &properties);
		const oidn::PhysicalDeviceRef selected(physicalDeviceId_);
		if (selected.get<bool>("uuidSupported"))
		{
			const auto uuid = selected.getData("uuid");
			if (uuid.first && uuid.second == VK_UUID_SIZE && std::memcmp(uuid.first, id.deviceUUID, VK_UUID_SIZE) == 0)
			{
				return true;
			}
		}
		if (id.deviceLUIDValid && selected.get<bool>("luidSupported"))
		{
			const auto luid = selected.getData("luid");
			return luid.first && luid.second == VK_LUID_SIZE && std::memcmp(luid.first, id.deviceLUID, VK_LUID_SIZE) == 0;
		}
	#endif
		return false;
	}

	bool DenoiserOIDN::IsSYCLDevice() const
	{
		return IsUsable() && oidn::PhysicalDeviceRef(physicalDeviceId_).get<oidn::DeviceType>("type") == oidn::DeviceType::SYCL;
	}

	void DenoiserOIDN::AddSharedFrame(void* inputHandle, const size_t inputSize, void* outputHandle, const size_t outputSize, const VkExtent2D extent)
	{
		SharedFrame frame;
		const auto handleType = oidn::ExternalMemoryTypeFlag::OpaqueWin32 | oidn::ExternalMemoryTypeFlag::Dedicated;
		frame.input = device_.newBuffer(handleType, inputHandle, nullptr, inputSize);
		frame.output = device_.newBuffer(handleType, outputHandle, nullptr, outputSize);
		if (!frame.input || !frame.output)
		{
			CheckError("import OIDN external memory");
			throw std::runtime_error("OIDN external memory import failed");
		}
		frame.filter = device_.newFilter("RT");
		if (!frame.filter)
		{
			CheckError("create OIDN shared RT filter");
			throw std::runtime_error("OIDN shared filter creation failed");
		}
		frame.filter.setImage("color", frame.input, oidn::Format::Float3, extent.width, extent.height, 0, 4 * sizeof(float), extent.width * 4ull * sizeof(float));
		frame.filter.setImage("output", frame.output, oidn::Format::Float3, extent.width, extent.height, 0, 4 * sizeof(float), extent.width * 4ull * sizeof(float));
		frame.filter.set("hdr", false);
		frame.filter.set("srgb", true);
		frame.filter.set("quality", oidn::Quality::Balanced);
		frame.filter.commit();
		CheckError("commit OIDN shared filter");
		sharedFrames_.push_back(std::move(frame));
		status_ = "OIDN SYCL: shared GPU buffers";
	}

	void DenoiserOIDN::DenoiseShared(const size_t frameIndex)
	{
		sharedFrames_.at(frameIndex).filter.execute();
		CheckError("execute OIDN shared filter");
	}

	void DenoiserOIDN::ClearSharedFrames()
	{
		sharedFrames_.clear();
	}

	void DenoiserOIDN::UseHostStaging(const std::string& reason)
	{
		status_ = std::string("OIDN host staging: ") + reason;
	}

	void DenoiserOIDN::Disable(const std::string& reason)
	{
		physicalDeviceId_ = -1;
		status_ = std::string("OIDN disabled for this device: ") + reason;
	}

	void DenoiserOIDN::Denoise(const uint8_t* input, uint8_t* output, const VkExtent2D extent, const VkFormat format)
	{
		if (!device_)
		{
			throw std::runtime_error(status_.empty() ? "no OIDN device is active" : status_);
		}

		if (!input || !output || extent.width == 0 || extent.height == 0)
		{
			throw std::invalid_argument("invalid OIDN image input");
		}

		if (format != VK_FORMAT_B8G8R8A8_UNORM && format != VK_FORMAT_R8G8B8A8_UNORM &&
			format != VK_FORMAT_B8G8R8A8_SRGB && format != VK_FORMAT_R8G8B8A8_SRGB)
		{
			throw std::runtime_error("OIDN requires an 8-bit RGBA swap-chain format");
		}

		const bool bgra = format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
		const auto convertInput = [&]()
		{
			const size_t pixelCount = static_cast<size_t>(extent.width) * extent.height;
			for (size_t pixel = 0; pixel != pixelCount; ++pixel)
			{
				const size_t byteOffset = pixel * 4;
				const size_t redOffset = byteOffset + (bgra ? 2 : 0);
				const size_t blueOffset = byteOffset + (bgra ? 0 : 2);
				color_[pixel * 3 + 0] = input[redOffset] / 255.0f;
				color_[pixel * 3 + 1] = input[byteOffset + 1] / 255.0f;
				color_[pixel * 3 + 2] = input[blueOffset] / 255.0f;
			}
		};

		try
		{
			Resize(extent);
			convertInput();
			ExecuteFilter();
		}
		catch (const std::exception& exception)
		{
			if (physicalDeviceId_ == cpuPhysicalDeviceId_ || cpuPhysicalDeviceId_ < 0)
			{
				throw;
			}

			status_ = std::string("OIDN accelerator failed; using CPU: ") + exception.what();
			try
			{
				InitializeDevice(cpuPhysicalDeviceId_);
				Resize(extent);
				convertInput();
				ExecuteFilter();
			}
			catch (const std::exception& cpuException)
			{
				physicalDeviceId_ = -1;
				status_ += std::string("; CPU denoising failed: ") + cpuException.what();
				throw;
			}
		}

		const size_t pixelCount = static_cast<size_t>(extent.width) * extent.height;
		for (size_t pixel = 0; pixel != pixelCount; ++pixel)
		{
			const size_t byteOffset = pixel * 4;
			const size_t redOffset = byteOffset + (bgra ? 2 : 0);
			const size_t blueOffset = byteOffset + (bgra ? 0 : 2);
			output[redOffset] = static_cast<uint8_t>(std::lround(std::clamp(denoised_[pixel * 3 + 0], 0.0f, 1.0f) * 255.0f));
			output[byteOffset + 1] = static_cast<uint8_t>(std::lround(std::clamp(denoised_[pixel * 3 + 1], 0.0f, 1.0f) * 255.0f));
			output[blueOffset] = static_cast<uint8_t>(std::lround(std::clamp(denoised_[pixel * 3 + 2], 0.0f, 1.0f) * 255.0f));
			output[byteOffset + 3] = input[byteOffset + 3];
		}
	}

	void DenoiserOIDN::Resize(const VkExtent2D extent)
	{
		if (extent.width == extent_.width && extent.height == extent_.height)
		{
			return;
		}

		extent_ = extent;
		const size_t floatCount = static_cast<size_t>(extent.width) * extent.height * 3;
		color_.resize(floatCount);
		denoised_.resize(floatCount);

		filter_.release();
		colorBuffer_.release();
		denoisedBuffer_.release();
		const size_t bufferSize = floatCount * sizeof(float);
		colorBuffer_ = device_.newBuffer(bufferSize);
		denoisedBuffer_ = device_.newBuffer(bufferSize);
		if (!colorBuffer_ || !denoisedBuffer_)
		{
			CheckError("create OIDN image buffers");
			throw std::runtime_error("failed to create OIDN image buffers");
		}

		filter_ = device_.newFilter("RT");
		if (!filter_)
		{
			CheckError("create OIDN RT filter");
			throw std::runtime_error("failed to create the OIDN RT filter");
		}

		const size_t rowStride = static_cast<size_t>(extent.width) * 3 * sizeof(float);
		filter_.setImage("color", colorBuffer_, oidn::Format::Float3, extent.width, extent.height, 0, 0, rowStride);
		filter_.setImage("output", denoisedBuffer_, oidn::Format::Float3, extent.width, extent.height, 0, 0, rowStride);
		filter_.set("hdr", false);
		filter_.set("srgb", true);
		filter_.set("quality", oidn::Quality::Balanced);
		filter_.commit();
		CheckError("commit OIDN filter");
	}

	void DenoiserOIDN::ExecuteFilter()
	{
		const size_t bufferSize = color_.size() * sizeof(float);
		colorBuffer_.write(0, bufferSize, color_.data());
		filter_.execute();
		CheckError("execute OIDN filter");
		denoisedBuffer_.read(0, bufferSize, denoised_.data());
		CheckError("read OIDN output buffer");
	}

	void DenoiserOIDN::CheckError(const char* operation)
	{
		const char* message = nullptr;
		if (device_.getError(message) != oidn::Error::None)
		{
			throw std::runtime_error(std::string(operation) + ": " + (message ? message : "unknown OIDN error"));
		}
	}
}