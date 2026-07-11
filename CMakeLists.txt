cmake_minimum_required(VERSION 3.28)

#
# 1. 准备阶段
#

# 使用vcpkg进行包管理
if(ANDROID)
    if (ANDROID_ABI MATCHES "arm64-v8a")
        set(VCPKG_TARGET_TRIPLET "arm64-android" CACHE STRING "" FORCE)
    elseif(ANDROID_ABI MATCHES "armeabi-v7a")
        set(VCPKG_TARGET_TRIPLET "arm-android" CACHE STRING "" FORCE)
    elseif(ANDROID_ABI MATCHES "x86_64")
        set(VCPKG_TARGET_TRIPLET "x64-android" CACHE STRING "" FORCE)
    else()
        message(FATAL_ERROR "安卓当前仅支持arm64-v8a、armeabi-v7a和x86_64架构")
    endif()
	if(ONLYCONSOLE)
		message(FATAL_ERROR "安卓不支持仅编译tjs2")
	endif()
    set(CMAKE_ANDROID_NDK $ENV{ANDROID_NDK_HOME})
    set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE $ENV{ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake)
elseif(LINUX)
    set(VCPKG_TARGET_TRIPLET "x64-linux-release")
elseif(WINDOWS)
    if(MSVC)
        add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
        set(VCPKG_TARGET_TRIPLET "x64-windows")
    else()
        set(VCPKG_TARGET_TRIPLET "x64-mingw-dynamic")
    endif()
endif()
set(CMAKE_TOOLCHAIN_FILE $ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)

# 视频解码后端: ON=FFmpeg, OFF=系统API(MediaFoundation/MediaCodec)
# 感觉没必要禁用，OFF的实现基本没用༼ ༎ຶ ෴ ༎ຶ༽
option(USE_FFMPEG "Use FFmpeg for video/audio decoding" ON)

# 建立项目
if(ONLYCONSOLE)
    project(tjs2 VERSION "0.0.6" LANGUAGES C CXX)
else()
    project(krkrsdl3 VERSION "0.0.6" LANGUAGES C CXX)
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(ANDROID)
    add_definitions(
        -D_KRKRSDL3_ANDROID=1
        -D_KRKRSDL3_EGL=1
        -D_KRKRSDL3_LIB=1
    )
elseif(LINUX)
    add_definitions(
        -D_KRKRSDL3_LINUX=1
        -D_KRKRSDL3_GL=1
        -D_KRKRSDL3_CMD=1
    )
elseif(WINDOWS)
    add_definitions(
        -D_KRKRSDL3_WINDOWS=1
        -D_KRKRSDL3_GL=1
        -D_KRKRSDL3_CMD=1
    )
else()
    message(FATAL_ERROR "unsupported: ${CMAKE_SYSTEM_NAME}")
endif()

#
# 2. 区分编译
#

if(ONLYCONSOLE)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cpp/cmake_console.txt)
else()
    include(${CMAKE_CURRENT_SOURCE_DIR}/cpp/cmake_main.txt)
endif()