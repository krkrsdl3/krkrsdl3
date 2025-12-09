#include "ncbind/ncbind.hpp"
#include "TVPMsg.h"

#define NCB_MODULE_NAME TJS_W("getabout.dll")
NCB_ATTACH_FUNCTION(getAboutString, System, TVPGetAboutString);
