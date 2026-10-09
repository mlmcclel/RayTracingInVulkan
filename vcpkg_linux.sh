#!/bin/bash
set -e

arch=$(uname -m)
vcpkg_arch=""
case ${arch} in
	"x86_64")
		vcpkg_arch="x64"
		;;
	*)
		echo "The official OIDN binary package in this script supports x86_64 Linux only."
		exit 1
		;;
esac

mkdir -p build
cd build
if [ ! -f vcpkg.linux/bootstrap-vcpkg.sh ]; then
	git clone https://github.com/Microsoft/vcpkg.git vcpkg.linux
fi
cd vcpkg.linux
git checkout 2025.06.13
./bootstrap-vcpkg.sh

./vcpkg install \
	boost-exception:${vcpkg_arch}-linux \
	boost-program-options:${vcpkg_arch}-linux \
	boost-stacktrace:${vcpkg_arch}-linux \
	glfw3:${vcpkg_arch}-linux \
	glm:${vcpkg_arch}-linux \
	imgui[core,freetype,glfw-binding,vulkan-binding]:${vcpkg_arch}-linux \
	stb:${vcpkg_arch}-linux \
	tinyobjloader:${vcpkg_arch}-linux

cd ..
oidn_archive="oidn-2.5.1.x86_64.linux.tar.gz"
oidn_url="https://github.com/RenderKit/oidn/releases/download/v2.5.1/${oidn_archive}"
oidn_sha256="743c3e2aff8c220d5d70fe6cb970fb3d36f2702d2693c61d1d148e404cf37cd6"
mkdir -p oidn
if [ ! -f "${oidn_archive}" ]; then
	curl --fail --location --output "${oidn_archive}" "${oidn_url}"
fi
echo "${oidn_sha256}  ${oidn_archive}" | sha256sum --check --status || {
	echo "OIDN archive SHA-256 verification failed"
	exit 1
}
if [ ! -f oidn/oidn-2.5.1.x86_64.linux/include/OpenImageDenoise/oidn.hpp ]; then
	tar -xzf "${oidn_archive}" -C oidn
fi
