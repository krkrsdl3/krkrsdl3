#include "tjsCommHead.h"

#include "Platform.h"
#include "PlatformFile.h"

#include <SDL3/SDL_locale.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_platform.h>
#include <SDL3/SDL_misc.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_keyboard.h>

bool TVP_stat(const char* name, tTVP_stat& s)
{
    if (!name)
        return false;

    SDL_PathInfo info;
    if (!SDL_GetPathInfo(name, &info))
    {
        return false;
    }

    s.tvp_isdir = info.type == SDL_PATHTYPE_DIRECTORY;
    s.tvp_size = info.size;
    s.tvp_atime = (time_t)(info.access_time / 1000000000LL);
    s.tvp_mtime = (time_t)(info.modify_time / 1000000000LL);
    s.tvp_ctime = (time_t)(info.create_time / 1000000000LL);

    return true;
}

int tTVPScreen::GetWidth()
{
    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(display);
    return mode ? mode->w : 0;
}

int tTVPScreen::GetHeight()
{
    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(display);
    return mode ? mode->h : 0;
}

int tTVPScreen::GetDesktopLeft()
{
    SDL_Rect rect;
    if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &rect))
    {
        return rect.x;
    }
    return 0;
}

int tTVPScreen::GetDesktopTop()
{
    SDL_Rect rect;
    if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &rect))
    {
        return rect.y;
    }
    return 0;
}

int tTVPScreen::GetDesktopWidth()
{
    SDL_Rect rect;
    if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &rect))
    {
        return rect.w;
    }
    return GetWidth();
}

int tTVPScreen::GetDesktopHeight()
{
    SDL_Rect rect;
    if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &rect))
    {
        return rect.h;
    }
    return GetHeight();
}

void TVPConsoleLog(const tjs_char* format, ...)
{
    va_list args;
    va_start(args, format);
    SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, format, args);
    va_end(args);
}

ttstr TVPGetPlatformName()
{
    const char* platform = SDL_GetPlatform();
    return ttstr(platform);
}

std::string TVPGetCurrentLanguage()
{
    SDL_Locale** locales;
    int count;
    locales = SDL_GetPreferredLocales(&count);
    for (int i = 0; i < count; i++)
    {
        SDL_Log("Preferred locale %d: %s_%s\n", i + 1, locales[i]->language,
                locales[i]->country ? locales[i]->country : "ANY");
    }
    std::string ret = "";
    if (count > 0)
    {
        ret += std::string(locales[0]->language);
        if (locales[0]->country)
        {
            ret += std::string("_") + std::string(locales[0]->country);
        }
        else
        {
            ret += std::string("_ANY");
        }
    }
    SDL_free(locales);
    return ret;
}

void TVPOpenPatchLibUrl()
{
    std::string url = "https://zeas2.github.io/Kirikiroid2_patch/patch";
    SDL_OpenURL(url.c_str());
}

tjs_uint64 TVPGetRoughTickCount()
{
    return SDL_GetTicks();
}