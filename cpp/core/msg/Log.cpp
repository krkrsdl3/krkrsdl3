#include "tjsCommHead.h"
#include "Log.h"

#include <stdio.h>
#include <assert.h>
#include <locale>

#include "SDL2/SDL.h"
static const int MAX_LOG_LENGTH = 16 * 1024;

void TVPConsoleLog(const tjs_char* format, ...)
{
    va_list args;
    va_start(args, format);
    char buf[MAX_LOG_LENGTH];
    vsnprintf(buf, MAX_LOG_LENGTH - 3, format, args);
    SDL_Log("%s", buf);
    va_end(args);
}

void TVPConsoleLog(const ttstr& l, bool important)
{
    SDL_Log("%s", l.c_str());
}
