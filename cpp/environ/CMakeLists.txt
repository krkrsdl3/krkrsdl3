set(KRKRSDL_ENVIRON_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cpp/environ)

set(KRKRSDL_ENVIRON_SRC
    ${KRKRSDL_ENVIRON_PATH}/DetectCPU.cpp
    ${KRKRSDL_ENVIRON_PATH}/KeyCodeConv.cpp
    ${KRKRSDL_ENVIRON_PATH}/MainWindowLayer.cpp
    ${KRKRSDL_ENVIRON_PATH}/Platform.cpp
    ${KRKRSDL_ENVIRON_PATH}/WaveMixer.cpp
    ${KRKRSDL_ENVIRON_PATH}/sdl3/sdl3_dialog.cpp
    ${KRKRSDL_ENVIRON_PATH}/sdl3/sdl3_file.cpp
    ${KRKRSDL_ENVIRON_PATH}/sdl3/sdl3_utils.cpp
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
