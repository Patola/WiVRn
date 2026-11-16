# Cross-build the Frame client using an unpacked ARM64 Steam Runtime SDK.
# Run CMake on Linux with aarch64-linux-gnu GCC and native asset-build tools.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT DEFINED ENV{WIVRN_STEAM_FRAME_SYSROOT})
    message(FATAL_ERROR "Set WIVRN_STEAM_FRAME_SYSROOT to the unpacked ARM64 Steam Runtime SDK")
endif()
file(REAL_PATH "$ENV{WIVRN_STEAM_FRAME_SYSROOT}" CMAKE_SYSROOT)
if(NOT EXISTS "${CMAKE_SYSROOT}/usr/include")
    message(FATAL_ERROR "WIVRN_STEAM_FRAME_SYSROOT does not contain an SDK root filesystem")
endif()

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${CMAKE_SYSROOT}")
set(ENV{PKG_CONFIG_LIBDIR} "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig:${CMAKE_SYSROOT}/usr/lib/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig")

# The sources use headers rather than C++ modules; avoid requiring a separate
# target-aware dependency scanner when cross-compiling.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
