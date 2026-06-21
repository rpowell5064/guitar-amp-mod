# Cross-compile toolchain for the PatchStorage `patchbox-os-arm32` target:
# 32-bit ARM (armv7-a, hard-float NEON) — MODEP / Patchbox OS on Raspberry Pi 3/4.
#
# ALWAYS pair this with -DGUITARAMP_DISABLE_NAM=ON: NAM neural inference would not
# run in realtime on a 32-bit Pi, and we don't want to compile NamCore/Eigen here.
#
# Usage:
#   cmake -S . -B build-armhf \
#         -DCMAKE_TOOLCHAIN_FILE=armhf-toolchain.cmake \
#         -DCMAKE_BUILD_TYPE=Release \
#         -DGUITARAMP_DISABLE_NAM=ON \
#         -DCMAKE_SYSROOT=/path/to/patchbox-armhf-sysroot
#
# Needs: gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf and a Patchbox/Raspberry
# Pi OS (32-bit) sysroot with lv2-dev. The sysroot can be copied from a device or
# built with multistrap.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER   arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

# Raspberry Pi 2/3/4 (32-bit): Cortex-A7/A53/A72, NEON, hard-float ABI. armv7-a
# keeps one binary that runs across that whole 32-bit Pi range. The
# arm-linux-gnueabihf toolchain defaults to -mfloat-abi=hard, so we don't pass it
# explicitly (passing it can trip "selected architecture lacks an FPU" depending
# on flag ordering); -mfpu=neon-vfpv4 selects the FPU.
set(CMAKE_C_FLAGS_INIT   "-march=armv7-a -mfpu=neon-vfpv4")
set(CMAKE_CXX_FLAGS_INIT "-march=armv7-a -mfpu=neon-vfpv4")

# Set SYSROOT to a Patchbox / Pi OS (32-bit) rootfs. Override on the command line:
#   -DCMAKE_SYSROOT=/path/to/patchbox-armhf-sysroot
if(NOT DEFINED CMAKE_SYSROOT)
    set(CMAKE_SYSROOT "$ENV{HOME}/patchbox-armhf-sysroot")
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(ENV{PKG_CONFIG_PATH} "${CMAKE_SYSROOT}/usr/lib/arm-linux-gnueabihf/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_LIBDIR} "${CMAKE_SYSROOT}/usr/lib/arm-linux-gnueabihf/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${CMAKE_SYSROOT}")
