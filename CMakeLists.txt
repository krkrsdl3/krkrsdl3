cmake_minimum_required(VERSION 3.28)

#
# 1. 准备阶段
#

# 使用vcpkg进行包管理
if(ANDROID)
    if (ANDROID_ABI MATCHES "arm64-v8a")
        set(VCPKG_TARGET_TRIPLET "arm64-android" CACHE STRING "" FORCE)
    elseif(ANDROID_ABI MATCHES "x86_64")
        set(VCPKG_TARGET_TRIPLET "x64-android" CACHE STRING "" FORCE)
    else()
        message(FATAL_ERROR "
        Please specify ANDROID_ABI
        For example
        cmake ... -DANDROID_ABI=arm64-v8a

        Possible ABIs are: arm64-v8a, x64-android
        ")
    endif()
    set(CMAKE_ANDROID_NDK $ENV{ANDROID_NDK_HOME})
    set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE $ENV{ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake)
    set(CMAKE_TOOLCHAIN_FILE $ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)
endif()
set(CMAKE_TOOLCHAIN_FILE $ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)

# 建立项目
project(krkrsdl3 VERSION "0.0.5" LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# MSVC编译器设置
if(MSVC)
    add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
    add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
    add_compile_definitions(_UNICODE UNICODE)
    add_definitions(-D_WIN32_WINNT=0x0A00)
endif()

#
# 2. 确定源文件
#

include(${CMAKE_CURRENT_SOURCE_DIR}/cpp/core/CMakeLists.txt)
include(${CMAKE_CURRENT_SOURCE_DIR}/cpp/environ/CMakeLists.txt)
include(${CMAKE_CURRENT_SOURCE_DIR}/cpp/plugins/CMakeLists.txt)
set(KRKRSDL_SRC
    ${KRKRSDL_CORE_SRC}
    ${KRKRSDL_ENVIRON_SRC}
    ${KRKRSDL_PLUGINS_SRC}
    )

#
# 3. 确定目标文件
#

# 可执行文件
if(ANDROID)
    add_library(${PROJECT_NAME} SHARED
	    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/krkrsdl_android.cpp
		${KRKRSDL_SRC})
elseif(LINUX OR WINDOWS)
    add_executable(${PROJECT_NAME}
	    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/krkrsdl.cpp
		${KRKRSDL_SRC})
else()
    message(FATAL_ERROR "unsupported: ${CMAKE_SYSTEM_NAME}")
endif()

# 依赖头
target_include_directories(${PROJECT_NAME} PUBLIC
    ${KRKRSDL_CORE_PATH}/archive
	${KRKRSDL_CORE_PATH}/main
	${KRKRSDL_CORE_PATH}/media/font
	${KRKRSDL_CORE_PATH}/media/graphics/public
	${KRKRSDL_CORE_PATH}/media/graphics
	${KRKRSDL_CORE_PATH}/media/image
	${KRKRSDL_CORE_PATH}/media/movie
	${KRKRSDL_CORE_PATH}/media/sound
	${KRKRSDL_CORE_PATH}/msg
	${KRKRSDL_CORE_PATH}/script
	${KRKRSDL_CORE_PATH}/tjs2
	${KRKRSDL_CORE_PATH}/utils
	${KRKRSDL_ENVIRON_PATH}
	${KRKRSDL_ENVIRON_PATH}/math
	${KRKRSDL_PLUGINS_PATH}
)

# 依赖库
find_package(lz4 CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE lz4::lz4)

find_package(FFMPEG REQUIRED)
target_include_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${FFMPEG_LIBRARIES})

find_package(PNG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE PNG::PNG)

find_package(Freetype REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Freetype::Freetype) # since CMake 3.10

find_package(glad CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE glad::glad)

find_package(glm CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE glm::glm)

find_package(glm CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE glm::glm-header-only)

find_package(LibArchive REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE LibArchive::LibArchive)

find_package(JPEG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE JPEG::JPEG)

find_package(libjpeg-turbo CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE $<IF:$<TARGET_EXISTS:libjpeg-turbo::turbojpeg>,libjpeg-turbo::turbojpeg,libjpeg-turbo::turbojpeg-static>)

find_package(Vorbis CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Vorbis::vorbis)

find_package(Vorbis CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Vorbis::vorbisfile)

find_package(Vorbis CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Vorbis::vorbisenc)

find_package(WebP CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE WebP::webp WebP::webpdecoder WebP::webpdemux)

find_package(oniguruma CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE oniguruma::onig)

set(OpenCV_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/opencv4")
find_package(OpenCV REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE
    opencv_imgproc
    opencv_core
)
if(LINUX OR WINDOWS)
    target_link_libraries(${PROJECT_NAME} PRIVATE opencv_highgui)
endif()

find_package(Opus CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Opus::opus)

find_package(OpusFile CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE OpusFile::opusfile)

find_package(SDL3 CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3)

find_package(SDL3_ttf CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE SDL3_ttf::SDL3_ttf)

find_package(blend2d CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE blend2d::blend2d)

if(ANDROID)
    # iconv
    find_library(ICONV_LIBRARY iconv)
    if(ICONV_LIBRARY)
        set(Iconv_LIBRARY ${ICONV_LIBRARY})
        set(Iconv_IS_BUILT_IN TRUE)
        set(HAVE_ICONV 1)
        target_link_libraries(${PROJECT_NAME} PRIVATE iconv)
    else()
        set(Iconv_IS_BUILT_IN TRUE)
        set(HAVE_ICONV 1)
    endif()
    # camera2ndk/mediandk
    find_library(CAMERA2NDK_LIB camera2ndk)
    find_library(MEDIANDK_LIB mediandk)
    if(CAMERA2NDK_LIB)
        set(FFMPEG_DEPENDENCY_camera2ndk_RELEASE ${CAMERA2NDK_LIB})
        set(FFMPEG_DEPENDENCY_camera2ndk_DEBUG ${CAMERA2NDK_LIB})
        target_link_libraries(${PROJECT_NAME} PRIVATE camera2ndk)
    else()
        set(FFMPEG_DEPENDENCY_camera2ndk_RELEASE "")
        set(FFMPEG_DEPENDENCY_camera2ndk_DEBUG "")
    endif()
    if(MEDIANDK_LIB)
        set(FFMPEG_DEPENDENCY_mediandk_RELEASE ${MEDIANDK_LIB})
        set(FFMPEG_DEPENDENCY_mediandk_DEBUG ${MEDIANDK_LIB})
        target_link_libraries(${PROJECT_NAME} PRIVATE mediandk)
    else()
        set(FFMPEG_DEPENDENCY_mediandk_RELEASE "")
        set(FFMPEG_DEPENDENCY_mediandk_DEBUG "")
    endif()
    # egl
    target_link_libraries(${PROJECT_NAME} PRIVATE EGL GLESv3)
elseif(LINUX)
    target_link_libraries(${PROJECT_NAME} PRIVATE GL)
endif()

#
# 4. 完事后拷贝资源
#

if(LINUX OR WINDOWS)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/Res
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/Res)
endif()
