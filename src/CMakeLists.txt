
set(exe_name ${MAIN_PROJECT})

set(src_files_assets
	Assets/CornellBox.cpp
	Assets/CornellBox.hpp
	Assets/Material.hpp
	Assets/Model.cpp
	Assets/Model.hpp
	Assets/Procedural.hpp
	Assets/Scene.cpp
	Assets/Scene.hpp
	Assets/Sphere.hpp
	Assets/Texture.cpp
	Assets/Texture.hpp
	Assets/TextureImage.cpp
	Assets/TextureImage.hpp
	Assets/UniformBuffer.cpp
	Assets/UniformBuffer.hpp
	Assets/Vertex.hpp
)

set(src_files_utilities
	Utilities/Console.cpp
	Utilities/Console.hpp
	Utilities/Exception.hpp
	Utilities/Glm.hpp
	Utilities/StbImage.cpp
	Utilities/StbImage.hpp
)

set(src_files_vulkan
	Vulkan/Application.cpp
	Vulkan/Application.hpp
	Vulkan/Buffer.cpp
	Vulkan/Buffer.hpp
	Vulkan/BufferUtil.hpp
	Vulkan/CommandBuffers.cpp
	Vulkan/CommandBuffers.hpp
	Vulkan/CommandPool.cpp
	Vulkan/CommandPool.hpp
	Vulkan/DebugUtils.cpp
	Vulkan/DebugUtils.hpp
	Vulkan/DebugUtilsMessenger.cpp
	Vulkan/DebugUtilsMessenger.hpp
	Vulkan/DepthBuffer.cpp
	Vulkan/DepthBuffer.hpp
	Vulkan/DescriptorBinding.hpp
	Vulkan/DescriptorPool.cpp
	Vulkan/DescriptorPool.hpp
	Vulkan/DescriptorSetLayout.cpp
	Vulkan/DescriptorSetLayout.hpp
	Vulkan/DescriptorSetManager.cpp
	Vulkan/DescriptorSetManager.hpp
	Vulkan/DescriptorSets.cpp
	Vulkan/DescriptorSets.hpp
	Vulkan/Device.cpp
	Vulkan/Device.hpp
	Vulkan/DeviceMemory.cpp
	Vulkan/DeviceMemory.hpp
	Vulkan/Enumerate.hpp
	Vulkan/Fence.cpp
	Vulkan/Fence.hpp
	Vulkan/FrameBuffer.cpp
	Vulkan/FrameBuffer.hpp
	Vulkan/GraphicsPipeline.cpp
	Vulkan/GraphicsPipeline.hpp
	Vulkan/Image.cpp
	Vulkan/Image.hpp	
	Vulkan/ImageMemoryBarrier.hpp	
	Vulkan/ImageView.cpp
	Vulkan/ImageView.hpp	
	Vulkan/Instance.cpp
	Vulkan/Instance.hpp
	Vulkan/PipelineLayout.cpp
	Vulkan/PipelineLayout.hpp
	Vulkan/RenderPass.cpp
	Vulkan/RenderPass.hpp
	Vulkan/Sampler.cpp
	Vulkan/Sampler.hpp
	Vulkan/Semaphore.cpp
	Vulkan/Semaphore.hpp
	Vulkan/ShaderModule.cpp
	Vulkan/ShaderModule.hpp	
	Vulkan/SingleTimeCommands.hpp
	Vulkan/Strings.cpp
	Vulkan/Strings.hpp	
	Vulkan/Surface.cpp
	Vulkan/Surface.hpp	
	Vulkan/SwapChain.cpp
	Vulkan/SwapChain.hpp
	Vulkan/Version.hpp
	Vulkan/Vulkan.cpp
	Vulkan/Vulkan.hpp
	Vulkan/Window.cpp
	Vulkan/Window.hpp
	Vulkan/WindowConfig.hpp
	Vulkan/OIDNDenoiser.cpp
	Vulkan/OIDNDenoiser.hpp
)

set(src_files_vulkan_raytracing
	Vulkan/RayTracing/AccelerationStructure.cpp
	Vulkan/RayTracing/AccelerationStructure.hpp
	Vulkan/RayTracing/Application.cpp
	Vulkan/RayTracing/Application.hpp
	Vulkan/RayTracing/BottomLevelAccelerationStructure.cpp
	Vulkan/RayTracing/BottomLevelAccelerationStructure.hpp
	Vulkan/RayTracing/BottomLevelGeometry.cpp
	Vulkan/RayTracing/BottomLevelGeometry.hpp
	Vulkan/RayTracing/DeviceProcedures.cpp
	Vulkan/RayTracing/DeviceProcedures.hpp
	Vulkan/RayTracing/RayTracingPipeline.cpp
	Vulkan/RayTracing/RayTracingPipeline.hpp
	Vulkan/RayTracing/RayTracingProperties.cpp
	Vulkan/RayTracing/RayTracingProperties.hpp
	Vulkan/RayTracing/ShaderBindingTable.cpp
	Vulkan/RayTracing/ShaderBindingTable.hpp
	Vulkan/RayTracing/TopLevelAccelerationStructure.cpp
	Vulkan/RayTracing/TopLevelAccelerationStructure.hpp
)

set(src_files
	main.cpp
	ModelViewController.cpp
	ModelViewController.hpp
	Options.cpp
	Options.hpp
	RayTracer.cpp
	RayTracer.hpp
	SceneList.cpp
	SceneList.hpp
	UserInterface.cpp
	UserInterface.hpp
	UserSettings.hpp
)

source_group("Assets" FILES ${src_files_assets})
source_group("Utilities" FILES ${src_files_utilities})
source_group("Vulkan" FILES ${src_files_vulkan})
source_group("Vulkan.RayTracing" FILES ${src_files_vulkan_raytracing})
source_group("Main" FILES ${src_files})

add_executable(${exe_name} 
	${src_files_assets} 
	${src_files_utilities} 
	${src_files_vulkan} 
	${src_files_vulkan_raytracing} 
	${src_files}
)

if (UNIX)
	# GCC8 needs an extra lib for <filesystem>.
	# This is not needed with GCC9 or higher.
	set(extra_libs -lstdc++fs ${Backtrace_LIBRARIES})
endif()

add_dependencies(${exe_name} Assets)
set_target_properties(${exe_name} PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})
set_target_properties(${exe_name} PROPERTIES NO_SYSTEM_FROM_IMPORTED TRUE)
target_include_directories(${exe_name} PRIVATE . ${STB_INCLUDE_DIRS} ${Vulkan_INCLUDE_DIRS})
target_link_directories(${exe_name} PRIVATE ${Vulkan_LIBRARY})
target_link_libraries(${exe_name} PRIVATE Boost::boost Boost::exception Boost::program_options glfw glm::glm imgui::imgui tinyobjloader::tinyobjloader OpenImageDenoise ${Vulkan_LIBRARIES} ${extra_libs})

get_filename_component(OIDN_PACKAGE_ROOT "${OpenImageDenoise_DIR}/../../.." ABSOLUTE)
target_include_directories(${exe_name} BEFORE PRIVATE "${OIDN_PACKAGE_ROOT}/include")
if (WIN32)
	set(OIDN_RUNTIME_DIRECTORY "${OIDN_PACKAGE_ROOT}/bin")
	file(GLOB OIDN_RUNTIME_FILES "${OIDN_RUNTIME_DIRECTORY}/*.dll")
elseif (UNIX)
	set(OIDN_RUNTIME_DIRECTORY "${OIDN_PACKAGE_ROOT}/lib")
	file(GLOB OIDN_RUNTIME_FILES "${OIDN_RUNTIME_DIRECTORY}/*.so" "${OIDN_RUNTIME_DIRECTORY}/*.so.*")
	set_property(TARGET ${exe_name} APPEND PROPERTY BUILD_RPATH "$ORIGIN")
endif ()

foreach (OIDN_RUNTIME_FILE ${OIDN_RUNTIME_FILES})
	add_custom_command(TARGET ${exe_name} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OIDN_RUNTIME_FILE}" "$<TARGET_FILE_DIR:${exe_name}>"
		VERBATIM
	)
endforeach ()
