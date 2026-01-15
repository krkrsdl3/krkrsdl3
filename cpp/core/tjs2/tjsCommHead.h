#pragma once

#include <string.h>
#include <stdarg.h>
#include <wchar.h>
#include <stdlib.h>
#include <stdio.h>
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <math.h>
#include <assert.h>

#include <climits>
#include <sstream>
#include <memory>
#include <algorithm>
#include <functional>
#include <vector>
#include <queue>
#include <deque>
#include <string>
#include <stdexcept>

#ifdef _MSC_VER
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#endif

#include "tjsConfig.h"
#include "tjs.h"
