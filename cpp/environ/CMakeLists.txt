set(KRKRSDL_ENVIRON_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cpp/environ)

# 1.命令行
set(KRKRSDL_ENVIRON_CONSOLE_SRC
	${KRKRSDL_ENVIRON_PATH}/std/std_utils.cpp
    ${KRKRSDL_ENVIRON_PATH}/console/console_entry.cpp
    )

# 2.全量
set(KRKRSDL_ENVIRON_SRC
    ${KRKRSDL_ENVIRON_PATH}/Platform.cpp
    ${KRKRSDL_ENVIRON_PATH}/sdl3/sdl3_dialog.cpp
    ${KRKRSDL_ENVIRON_PATH}/sdl3/sdl3_file.cpp
    ${KRKRSDL_ENVIRON_PATH}/sdl3/sdl3_utils.cpp
    ${KRKRSDL_ENVIRON_PATH}/sdl3/sdl3_audio.cpp
    ${KRKRSDL_ENVIRON_PATH}/sdl3/sdl3_thread.cpp
    ${KRKRSDL_ENVIRON_PATH}/sdl3/sdl3_entry.cpp
    ${KRKRSDL_ENVIRON_PATH}/sdl3/sdl3_render.cpp
    )
	
if(ANDROID)
    list(APPEND KRKRSDL_ENVIRON_SRC ${KRKRSDL_ENVIRON_PATH}/android/android_core.cpp)
    list(APPEND KRKRSDL_ENVIRON_SRC ${KRKRSDL_ENVIRON_PATH}/unix/unix_file.cpp)
elseif(LINUX)
    list(APPEND KRKRSDL_ENVIRON_SRC ${KRKRSDL_ENVIRON_PATH}/linux/linux_core.cpp)
    list(APPEND KRKRSDL_ENVIRON_SRC ${KRKRSDL_ENVIRON_PATH}/unix/unix_file.cpp)
    list(APPEND KRKRSDL_ENVIRON_SRC ${KRKRSDL_ENVIRON_PATH}/sdl3/sdl3_input.cpp)
elseif(WINDOWS)
    list(APPEND KRKRSDL_ENVIRON_SRC ${KRKRSDL_ENVIRON_PATH}/windows/windows_core.cpp)
    list(APPEND KRKRSDL_ENVIRON_SRC ${KRKRSDL_ENVIRON_PATH}/sdl3/sdl3_input.cpp)
else()
    message(FATAL_ERROR "unsupported: ${CMAKE_SYSTEM_NAME}")
endif()

if(NOT USE_FFMPEG)
    if(ANDROID)
        list(APPEND KRKRSDL_ENVIRON_SRC ${KRKRSDL_ENVIRON_PATH}/android/android_video.cpp)
    elseif(WINDOWS)
        list(APPEND KRKRSDL_ENVIRON_SRC ${KRKRSDL_ENVIRON_PATH}/windows/windows_video.cpp)
    else()
        message(FATAL_ERROR "unsupported: ${CMAKE_SYSTEM_NAME}")
    endif()
endif()