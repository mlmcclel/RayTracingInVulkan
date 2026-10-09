cd build || goto :error
if not exist windows mkdir windows || goto :error
cd windows || goto :error
cmake -D VCPKG_TARGET_TRIPLET=x64-windows-static -D CMAKE_TOOLCHAIN_FILE=../vcpkg.windows/scripts/buildsystems/vcpkg.cmake -D OpenImageDenoise_DIR=%CD%/../oidn/oidn-2.5.1.x64.windows/lib/cmake/OpenImageDenoise-2.5.1 -G "Visual Studio 17 2022" -A "x64" ../.. || goto :error
set CONFIGURATION=%~1
if "%CONFIGURATION%"=="" set CONFIGURATION=Release
msbuild RayTracingInVulkan.sln /t:Rebuild /p:Configuration=%CONFIGURATION% || goto :error
cd ..
cd ..

exit /b


:error
echo Failed with error #%errorlevel%.
exit /b %errorlevel%