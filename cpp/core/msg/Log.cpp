#include "tjsCommHead.h"
#include "Log.h"

#include <stdio.h>
#include <assert.h>
#include <locale>
#include <codecvt>

#include "SDL3/SDL.h"
static const int MAX_LOG_LENGTH = 16 * 1024;

namespace TJS
{
std::string TVP_codecvt_utf8_utf16(const tjs_char* indata)
{
	std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
    return converter.to_bytes(indata);
}

std::u16string TVP_codecvt_utf16_utf8(const char* indata)
{
	std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
    return converter.from_bytes(indata);
}
}

void TVPConsoleLog(const tjs_char* l)
{
	assert(sizeof(tjs_char) == sizeof(char16_t));
	std::u16string buf((const char16_t*)l);
	std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
	std::string str = converter.to_bytes(buf);
    SDL_Log("%s", str.c_str());
}

void TVPConsoleLog(const tjs_nchar* format, ...)
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
	if (important)
	{
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", l.AsStdString().c_str());
	}
	else
	{
		SDL_Log("%s", l.AsStdString().c_str());
	}
}
