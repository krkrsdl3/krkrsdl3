cmake_minimum_required(VERSION 3.16)

#
# 1. 准备阶段 (SDL2 + MSYS2/pkg-config)
#

# 建立项目
project(krkrsdl2 VERSION "0.0.7" LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# HarmonyOS detection: OHOS toolchain sets CMAKE_SYSTEM_NAME=OHOS
if(CMAKE_SYSTEM_NAME STREQUAL "OHOS")
    set(OHOS TRUE)
endif()

if(OHOS)
    add_definitions(
        -D_KRKRSDL3_OHOS=1
        -D_KRKRSDL3_EGL=1
        -D_KRKRSDL3_LIB=1
        -DSDL_MAIN_HANDLED
    )
elseif(ANDROID)
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
elseif(WIN32)
    add_definitions(
        -D_KRKRSDL3_WINDOWS=1
        -D_KRKRSDL3_GL=1
        -D_KRKRSDL3_CMD=1
    )
else()
    message(FATAL_ERROR "unsupported: ${CMAKE_SYSTEM_NAME}")
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
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/krkrsdl_gl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/krkrsdl_menu.cpp
    )

#
# 3. 确定目标文件
#

# 可执行文件 / 共享库
if(OHOS)
    add_library(${PROJECT_NAME} SHARED
        ${CMAKE_CURRENT_SOURCE_DIR}/cpp/krkrsdl_harmony.cpp
        ${KRKRSDL_SRC})
    set_target_properties(${PROJECT_NAME} PROPERTIES
        OUTPUT_NAME "krkr2"
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
elseif(ANDROID)
    add_library(${PROJECT_NAME} SHARED
	    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/krkrsdl_android.cpp
		${KRKRSDL_SRC})
elseif(LINUX OR WIN32)
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
    ${KRKRSDL_CORE_PATH}/utils/math
    ${KRKRSDL_CORE_PATH}/utils/simd
	${KRKRSDL_ENVIRON_PATH}
	${KRKRSDL_PLUGINS_PATH}
)

# 依赖库
if(OHOS)
    # =================================================================
    # HarmonyOS: prebuilt .so + vendored source (oniguruma, vorbis)
    # =================================================================
    # Required:
    #   PREBUILT_LIBS_DIR          - flat dir with all .so (VintagePomelo/entry/libs/arm64-v8a)
    #   SDL2_INCLUDE_DIR           - SDL2 header directory
    #   FFMPEG_INCLUDE_DIR         - FFmpeg header directory
    # Optional:
    #   HARMONY_DEPS_INCLUDE_DIR   - Python harmony_deps headers (jpeg/png/freetype/turbojpeg)
    #   GLM_INCLUDE_DIR            - GLM header-only math library
    #   WEBP_INCLUDE_DIR           - WebP headers
    #   OPUSFILE_INCLUDE_DIR       - opusfile headers
    #   OPUS_INCLUDE_DIR           - opus headers
    #   TURBOJPEG_LIB_DIR          - dir containing libturbojpeg.so (if not in PREBUILT)
    if(NOT DEFINED PREBUILT_LIBS_DIR)
        message(FATAL_ERROR "PREBUILT_LIBS_DIR must be set for HarmonyOS build")
    endif()
    if(NOT DEFINED SDL2_INCLUDE_DIR)
        message(FATAL_ERROR "SDL2_INCLUDE_DIR must be set for HarmonyOS build")
    endif()
    if(NOT DEFINED FFMPEG_INCLUDE_DIR)
        message(FATAL_ERROR "FFMPEG_INCLUDE_DIR must be set for HarmonyOS build")
    endif()

    # Allow find_library to search outside sysroot for our prebuilt libs
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)

    # Convenience: global link search path
    target_link_directories(${PROJECT_NAME} PRIVATE ${PREBUILT_LIBS_DIR})

    # --- SDL2 (code uses #include <SDL2/SDL.h>, provide third_party/ wrapper) ---
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${SDL2_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
    find_library(SDL2_LIBRARY NAMES SDL2 PATHS ${PREBUILT_LIBS_DIR} NO_DEFAULT_PATH)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${SDL2_LIBRARY})

    # --- SDL2_ttf (prebuilt name: libSDL2_ttf-2.0.so) ---
    find_library(SDL2_TTF_LIBRARY NAMES SDL2_ttf-2.0 SDL2_ttf PATHS ${PREBUILT_LIBS_DIR} NO_DEFAULT_PATH)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${SDL2_TTF_LIBRARY})

    # --- FreeType ---
    if(HARMONY_DEPS_INCLUDE_DIR)
        target_include_directories(${PROJECT_NAME} PRIVATE
            ${HARMONY_DEPS_INCLUDE_DIR}/freetype2
            ${HARMONY_DEPS_INCLUDE_DIR})
    endif()
    find_library(FREETYPE_LIBRARY NAMES freetype PATHS ${PREBUILT_LIBS_DIR} NO_DEFAULT_PATH)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${FREETYPE_LIBRARY})

    # --- FFmpeg ---
    target_include_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_INCLUDE_DIR})
    foreach(_fflib avcodec avformat avutil swscale swresample)
        find_library(_FFLIB_${_fflib} NAMES ${_fflib} PATHS ${PREBUILT_LIBS_DIR} NO_DEFAULT_PATH)
        if(_FFLIB_${_fflib})
            target_link_libraries(${PROJECT_NAME} PRIVATE ${_FFLIB_${_fflib}})
        else()
            target_link_libraries(${PROJECT_NAME} PRIVATE ${_fflib})
        endif()
    endforeach()

    # --- PNG ---
    find_library(PNG_LIBRARY NAMES png16 png PATHS ${PREBUILT_LIBS_DIR} NO_DEFAULT_PATH)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${PNG_LIBRARY})

    # --- GLM (header-only) ---
    if(GLM_INCLUDE_DIR)
        target_include_directories(${PROJECT_NAME} PRIVATE ${GLM_INCLUDE_DIR})
    else()
        find_path(_GLM_INC glm/glm.hpp)
        if(_GLM_INC)
            target_include_directories(${PROJECT_NAME} PRIVATE ${_GLM_INC})
        endif()
    endif()

    # --- JPEG + TurboJPEG ---
    find_library(JPEG_LIBRARY NAMES jpeg PATHS ${PREBUILT_LIBS_DIR} NO_DEFAULT_PATH)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${JPEG_LIBRARY})
    find_library(TURBOJPEG_LIBRARY NAMES turbojpeg
        PATHS ${TURBOJPEG_LIB_DIR} ${PREBUILT_LIBS_DIR} NO_DEFAULT_PATH)
    if(TURBOJPEG_LIBRARY)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${TURBOJPEG_LIBRARY})
    else()
        message(WARNING "libturbojpeg not found; set TURBOJPEG_LIB_DIR")
    endif()

    # --- Vorbis + Vorbisfile (vendored source from SDL2_mixer/external) ---
    set(_VORBIS_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../SDL2/SDL_mixer/external/vorbis)
    set(_OGG_INC    ${CMAKE_CURRENT_SOURCE_DIR}/../../SDL2/SDL_mixer/external/ogg/include)
    file(GLOB _VORBIS_SOURCES ${_VORBIS_SRC}/lib/*.c)
    list(FILTER _VORBIS_SOURCES EXCLUDE REGEX "(barkmel|psytune|tone)\\.c$")
    add_library(vorbis_vendored STATIC ${_VORBIS_SOURCES})
    target_include_directories(vorbis_vendored PRIVATE
        ${_VORBIS_SRC}/include ${_VORBIS_SRC}/lib ${_OGG_INC})
    target_link_libraries(${PROJECT_NAME} PRIVATE vorbis_vendored)
    target_include_directories(${PROJECT_NAME} PRIVATE ${_VORBIS_SRC}/include ${_OGG_INC})
    find_library(OGG_LIBRARY NAMES ogg PATHS ${PREBUILT_LIBS_DIR} NO_DEFAULT_PATH)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${OGG_LIBRARY})

    # --- WebP ---
    if(WEBP_INCLUDE_DIR)
        target_include_directories(${PROJECT_NAME} PRIVATE ${WEBP_INCLUDE_DIR})
    endif()
    find_library(WEBP_LIBRARY NAMES webp PATHS ${PREBUILT_LIBS_DIR} NO_DEFAULT_PATH)
    find_library(WEBPDEMUX_LIBRARY NAMES webpdemux PATHS ${PREBUILT_LIBS_DIR} NO_DEFAULT_PATH)
    if(WEBP_LIBRARY)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${WEBP_LIBRARY})
    endif()
    if(WEBPDEMUX_LIBRARY)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${WEBPDEMUX_LIBRARY})
    endif()

    # --- Oniguruma (vendored source from Kirikiroid2/third_party) ---
    set(_ONIG_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../Kirikiroid2/third_party/oniguruma)
    file(GLOB _ONIG_SOURCES ${_ONIG_SRC}/*.c)
    list(FILTER _ONIG_SOURCES EXCLUDE REGEX
        "unicode_(fold_data|property_data|property_data_posix|wb_data|egcb_data)\\.c$")
    add_library(onig_vendored STATIC ${_ONIG_SOURCES})
    target_include_directories(onig_vendored PUBLIC ${_ONIG_SRC})
    target_link_libraries(${PROJECT_NAME} PRIVATE onig_vendored)
    target_include_directories(${PROJECT_NAME} PRIVATE ${_ONIG_SRC})

    # --- Opus ---
    if(OPUSFILE_INCLUDE_DIR)
        target_include_directories(${PROJECT_NAME} PRIVATE ${OPUSFILE_INCLUDE_DIR})
    endif()
    if(OPUS_INCLUDE_DIR)
        target_include_directories(${PROJECT_NAME} PRIVATE ${OPUS_INCLUDE_DIR})
    endif()
    find_library(OPUS_LIBRARY NAMES opus PATHS ${PREBUILT_LIBS_DIR} NO_DEFAULT_PATH)
    find_library(OPUSFILE_LIBRARY NAMES opusfile PATHS ${PREBUILT_LIBS_DIR} NO_DEFAULT_PATH)
    if(OPUS_LIBRARY)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${OPUS_LIBRARY})
    endif()
    if(OPUSFILE_LIBRARY)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${OPUSFILE_LIBRARY})
    endif()

    # --- zlib (needed by minizip in archive) ---
    target_link_libraries(${PROJECT_NAME} PRIVATE z)

    # --- OpenGL ES 3 + EGL + system libs ---
    target_link_libraries(${PROJECT_NAME} PRIVATE EGL GLESv3 m dl hilog_ndk.z)

else()
    # =================================================================
    # Desktop (MSYS2/pkg-config) / Android
    # =================================================================
    include(FindPkgConfig)

    pkg_check_modules(SDL2 REQUIRED sdl2)
    target_include_directories(${PROJECT_NAME} PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${SDL2_LIBRARIES})

    pkg_check_modules(SDL2TTF REQUIRED SDL2_ttf)
    target_include_directories(${PROJECT_NAME} PRIVATE ${SDL2TTF_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${SDL2TTF_LIBRARIES})

    pkg_check_modules(FREETYPE REQUIRED freetype2)
    target_include_directories(${PROJECT_NAME} PRIVATE ${FREETYPE_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${FREETYPE_LIBRARIES})

    find_package(GLEW REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE GLEW::GLEW)

    pkg_check_modules(AVCODEC REQUIRED libavcodec)
    pkg_check_modules(AVFORMAT REQUIRED libavformat)
    pkg_check_modules(AVUTIL REQUIRED libavutil)
    pkg_check_modules(SWSCALE REQUIRED libswscale)
    pkg_check_modules(SWRESAMPLE REQUIRED libswresample)
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${AVCODEC_INCLUDE_DIRS} ${AVFORMAT_INCLUDE_DIRS} ${AVUTIL_INCLUDE_DIRS}
        ${SWSCALE_INCLUDE_DIRS} ${SWRESAMPLE_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${AVCODEC_LIBRARIES} ${AVFORMAT_LIBRARIES} ${AVUTIL_LIBRARIES}
        ${SWSCALE_LIBRARIES} ${SWRESAMPLE_LIBRARIES})

    find_package(PNG REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE PNG::PNG)

    find_package(glm QUIET)
    if(glm_FOUND)
        target_link_libraries(${PROJECT_NAME} PRIVATE glm::glm)
    else()
        find_path(GLM_INCLUDE_DIR glm/glm.hpp REQUIRED)
        target_include_directories(${PROJECT_NAME} PRIVATE ${GLM_INCLUDE_DIR})
    endif()

    find_package(JPEG REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE JPEG::JPEG)

    pkg_check_modules(TURBOJPEG REQUIRED libturbojpeg)
    target_include_directories(${PROJECT_NAME} PRIVATE ${TURBOJPEG_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${TURBOJPEG_LIBRARIES})

    pkg_check_modules(VORBIS REQUIRED vorbis vorbisfile vorbisenc)
    target_include_directories(${PROJECT_NAME} PRIVATE ${VORBIS_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${VORBIS_LIBRARIES})

    pkg_check_modules(WEBP REQUIRED libwebp libwebpdecoder libwebpdemux)
    target_include_directories(${PROJECT_NAME} PRIVATE ${WEBP_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${WEBP_LIBRARIES})

    pkg_check_modules(ONIG REQUIRED oniguruma)
    target_include_directories(${PROJECT_NAME} PRIVATE ${ONIG_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${ONIG_LIBRARIES})

    pkg_check_modules(OPUS REQUIRED opus opusfile)
    target_include_directories(${PROJECT_NAME} PRIVATE ${OPUS_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${OPUS_LIBRARIES})

    if(ANDROID)
        target_link_libraries(${PROJECT_NAME} PRIVATE EGL GLESv3)
    elseif(LINUX)
        target_link_libraries(${PROJECT_NAME} PRIVATE GL)
    elseif(WIN32)
        target_link_libraries(${PROJECT_NAME} PRIVATE opengl32)
    endif()
endif()

# ============================================================
# Compiler flags
# ============================================================
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wno-invalid-offsetof
        -Wno-multichar
        -Wno-write-strings
        -Wno-unused-result
        -Wno-narrowing
        -fno-strict-aliasing
        -fexceptions
        -fno-delete-null-pointer-checks
        -Wno-deprecated-declarations
        -Wno-sign-compare
        -Wno-unused-variable
        -Wno-unused-but-set-variable
        -Wno-format
        -Wno-reorder
    )
    if(OHOS)
        # OHOS aarch64: enable ARMv8 NEON auto-vectorization
        target_compile_options(${PROJECT_NAME} PRIVATE
            -march=armv8-a
        )
    endif()
endif()

#
# 4. 完事后拷贝资源
#

if(NOT OHOS)
    if(LINUX OR WIN32)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/Res
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/Res)
    endif()
endif()
