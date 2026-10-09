## Plan: OIDN Denoising and VS Code Build

Integrate Intel Open Image Denoise 2.5.1 using Intel's official platform binaries, route rendered pixels through a CPU readback/denoise/upload pass when the existing UI checkbox is enabled, and repair the VS Code build entry point. Keep denoising optional and preserve the current direct-present path when disabled.

**Steps**
1. **Package and link OIDN.** Update `vcpkg_linux.sh` and `vcpkg_windows.bat` to download the matching official x64 Linux and Windows OIDN 2.5.1 packages, verify SHA-256, and unpack to a predictable dependency directory. Inspect the archive's package config/imported target, headers, and runtime layout before wiring target names. Update root and source CMake configuration to use the package's headers and libraries and stage/locate required runtime dependencies; stop including the stale vendored 2.4.1 headers. Remove OIDN enumeration/API calls from `main.cpp` if they are not required; they currently call symbols without any OIDN library linked.
2. **Make VS Code builds reproducible.** Align `.vscode/msbuild-tools.json` with the generated solution path and x64 platform, and provide a VS Code task to configure/build using the existing Windows build script. Keep platform-specific scripts usable from the VS Code integrated terminal on Linux. The current config names a root-level solution and `Any CPU`, but `build_windows.bat` generates `build/windows/RayTracingInVulkan.sln` for x64.
3. **Implement OIDN lifecycle and image conversion.** Replace the commented OptiX sample in `src/Vulkan/Denoiser.hpp/.cpp` with a small OIDN CPU RT-filter owner using OIDN RAII handles, one committed CPU device, reusable filter/buffers per extent, resize handling, and surfaced device/filter errors. Start without albedo/normal guides because the ray tracer does not emit those AOVs. The ray tracing output is gamma-corrected `rgba8`; convert its pixels into OIDN's supported float image format and use the RT filter's LDR/sRGB mode, including format-aware channel ordering for the selected Vulkan output format.
4. **Integrate at a valid GPU/CPU synchronization boundary.** `RayTracing::Application::Render` only records Vulkan commands, so it cannot map output pixels and run CPU OIDN before those commands execute. Add a narrow opt-in split-frame path around `Vulkan::Application::DrawFrame`: record ray tracing plus image-to-host staging readback, submit and wait, denoise on CPU, then record/submit the upload to the acquired swapchain image before presentation. Keep the existing direct path when denoising is off. Extend host-visible staging allocation/mapping as needed, handling non-coherent flush/invalidate requirements correctly; preserve barriers/layouts and fence/semaphore ordering through resize and teardown.
5. **Wire settings and document operation.** Pass `UserSettings::IsDenoised` into the ray-tracing frame path, retaining the existing ImGui checkbox and `O` hotkey. Make toggles take effect promptly, keep denoising off by default, and bypass it in raster mode. Document dependency setup, VS Code build steps, CPU-only behavior, and expected frame-time impact in `README.md`.

**Relevant files**
- `vcpkg_linux.sh`, `vcpkg_windows.bat`, `CMakeLists.txt`, `src/CMakeLists.txt` — download, find, link, and stage OIDN 2.5.1.
- `.vscode/msbuild-tools.json` and likely `.vscode/tasks.json` — correct the generated solution/platform and expose the established build command in VS Code.
- `src/Vulkan/Denoiser.hpp`, `src/Vulkan/Denoiser.cpp` — replace the commented OptiX code with an OIDN CPU wrapper.
- `src/Vulkan/RayTracing/Application.hpp/.cpp`, `src/Vulkan/Application.hpp/.cpp` — add staged readback/denoising/upload around frame submission; current ray-tracing output is copied directly to the swapchain.
- `src/Vulkan/Buffer.hpp/.cpp`, `src/Vulkan/DeviceMemory.hpp/.cpp` — host staging/mapping and cache-coherency support.
- `src/main.cpp` — calls OIDN device enumeration functions declared by vendored headers, but no OIDN library is currently linked.
- `src/RayTracer.cpp`, `src/UserInterface.cpp`, `src/UserSettings.hpp` — checkbox and `O` key exist, but `IsDenoised` is not consumed by rendering; default is false.
- `README.md` — currently states there is no denoising filter and documents build scripts.

**Verification**
1. Run each dependency script; verify downloaded archive hashes, OIDN version, architecture, headers, binaries, and required runtime dependencies.
2. Build Windows Debug and Release and Linux Release using the existing scripts. Build from VS Code using the corrected x64 solution/task. Confirm OIDN references resolve and runtime DLL/shared-library discovery works; check compatibility with the project's Windows `/MT` setting.
3. On a Vulkan ray-tracing-capable machine, compare checkbox off/on at low samples: direct output remains unchanged when off; enabling it reduces noise without color/order errors. Exercise checkbox and `O` toggles, resize/swapchain recreation, raster mode, repeated frames, and clean shutdown with Vulkan validation enabled.

**Decisions**
- User selected CPU OIDN first and official Intel binaries. GPU external-memory/semaphore interop is excluded from this milestone.
- Pin OIDN 2.5.1 across headers and binaries; obtain and check in trusted archive hashes during implementation. The repository currently vendors 2.4.1 headers.
- CPU readback is the first complete path and may reduce frame rate substantially. Keep the denoiser interface separable so a future accelerated path can replace transfers.
- Exclude albedo/normal AOVs from the first pass; current shaders emit beauty output only.
- Editor diagnostics were clean for the inspected C++ files, but CMake has no OIDN target/link while `main.cpp` calls OIDN symbols. The VS Code MSBuild config points to a solution path/platform inconsistent with the generated Windows build.
