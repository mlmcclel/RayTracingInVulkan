mkdir build
cd build || goto :error
if not exist vcpkg.windows\bootstrap-vcpkg.bat git clone https://github.com/Microsoft/vcpkg.git vcpkg.windows || goto :error
cd vcpkg.windows || goto :error
git checkout 2025.06.13 || goto :error
call bootstrap-vcpkg.bat || goto :error

vcpkg.exe install ^
	boost-exception:x64-windows-static ^
	boost-program-options:x64-windows-static ^
	boost-stacktrace:x64-windows-static ^
	glfw3:x64-windows-static ^
	glm:x64-windows-static ^
	imgui[core,freetype,glfw-binding,vulkan-binding]:x64-windows-static ^
	stb:x64-windows-static ^
	tinyobjloader:x64-windows-static ^
	|| goto :error

cd ..
set OIDN_ARCHIVE=oidn-2.5.1.x64.windows.zip
set OIDN_URL=https://github.com/RenderKit/oidn/releases/download/v2.5.1/%OIDN_ARCHIVE%
set OIDN_SHA256=f11f91bc072a5e3a564515724cb72ab8fcfbc445c84a84c197ff9b16cc01396f
set OIDN_PACKAGE=oidn\oidn-2.5.1.x64.windows
if not exist oidn mkdir oidn
if not exist "%OIDN_ARCHIVE%" curl.exe --fail --location --output "%OIDN_ARCHIVE%" "%OIDN_URL%" || goto :error
powershell -NoProfile -Command "if ((Get-FileHash -Algorithm SHA256 -LiteralPath '%OIDN_ARCHIVE%').Hash -ne '%OIDN_SHA256%') { exit 1 }" || goto :error
if not exist "%OIDN_PACKAGE%\include\OpenImageDenoise\oidn.hpp" powershell -NoProfile -Command "Expand-Archive -LiteralPath '%OIDN_ARCHIVE%' -DestinationPath oidn" || goto :error
if not exist "%OIDN_PACKAGE%\include\OpenImageDenoise\oidn.hpp" goto :error
cd ..

exit /b


:error
echo Failed with error #%errorlevel%.
exit /b %errorlevel%
