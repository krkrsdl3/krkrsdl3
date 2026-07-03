set(KRKRSDL_ENVIRON_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cpp/environ)

set(KRKRSDL_ENVIRON_SRC
    ${KRKRSDL_ENVIRON_PATH}/DetectCPU.cpp
    ${KRKRSDL_ENVIRON_PATH}/KeyCodeConv.cpp
    ${KRKRSDL_ENVIRON_PATH}/MainWindowLayer.cpp
    ${KRKRSDL_ENVIRON_PATH}/Other.cpp
    ${KRKRSDL_ENVIRON_PATH}/WaveMixer.cpp
    )
	
if(ANDROID)
    list(APPEND KRKRSDL_ENVIRON_SRC ${KRKRSDL_ENVIRON_PATH}/android/Platform.cpp)
elseif(LINUX)
    list(APPEND KRKRSDL_ENVIRON_SRC ${KRKRSDL_ENVIRON_PATH}/linux/Platform.cpp)
elseif(WINDOWS)
    list(APPEND KRKRSDL_ENVIRON_SRC ${KRKRSDL_ENVIRON_PATH}/windows/Platform.cpp)
else()
    message(FATAL_ERROR "unsupported: ${CMAKE_SYSTEM_NAME}")
endif()
