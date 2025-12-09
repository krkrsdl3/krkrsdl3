#pragma once

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#ifdef _WIN32
// posix io api
extern "C" {
#define lseek64 _lseeki64
}
#endif
#ifdef CC_TARGET_OS_IPHONE
#define lseek64 lseek
#endif