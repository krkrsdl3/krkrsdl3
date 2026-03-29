/*
 * Platform.cpp - HarmonyOS platform implementations for krkr2
 *
 * Provides platform-specific functions needed by the engine on
 * the HarmonyOS sandbox environment. Mirrors the interface in
 * windows/Platform.cpp and linux/Platform.cpp.
 */

#include "tjsCommHead.h"
#include "Platform.h"

#include <algorithm>
#include <string>
#include <vector>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <SDL2/SDL.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#include <dlfcn.h>
#include <dirent.h>

#include "TVPSystem.h"
#include "TVPEvent.h"
#include "TVPMsg.h"
#include "TVPStorage.h"
#include "Random.h"
#include "RenderManager.h"
#include "TVPApplication.h"

#ifdef __OHOS__
#include <hilog/log.h>
#define PLAT_LOG(fmt, ...) \
    OH_LOG_Print(LOG_APP, LOG_INFO, 0xFF00, "krkr2Plat", fmt, ##__VA_ARGS__)
#else
#define PLAT_LOG(fmt, ...) SDL_Log("[krkr2Plat] " fmt, ##__VA_ARGS__)
#endif

// ============================================================
// KR2ExitException (same class as in krkrsdl_harmony.cpp)
// ============================================================
class KR2ExitException : public std::exception {
public:
    int code;
    KR2ExitException(int c) : code(c) {}
    const char* what() const noexcept override { return "KR2ExitApplication"; }
};

// ============================================================
// tTVPFileMedia
// ============================================================
class tTVPFileMedia : public iTVPStorageMedia
{
    tjs_uint RefCount;

public:
    tTVPFileMedia() { RefCount = 1; }
    ~tTVPFileMedia() { ; }

    void AddRef() { RefCount++; }
    void Release()
    {
        if (RefCount == 1)
            delete this;
        else
            RefCount--;
    }

    void GetName(ttstr& name) { name = TJS_N("file"); }

    void NormalizeDomainName(ttstr& name);
    void NormalizePathName(ttstr& name);
    bool CheckExistentStorage(const ttstr& name);
    tTJSBinaryStream* Open(const ttstr& name, tjs_uint32 flags);
    void GetListAt(const ttstr& name, iTVPStorageLister* lister);
    void GetLocallyAccessibleName(ttstr& name);

public:
    void GetLocalName(ttstr& name);
};

void tTVPFileMedia::NormalizeDomainName(ttstr& name)
{
    tjs_char* p = name.Independ();
    while (*p)
    {
        if (*p >= TJS_N('A') && *p <= TJS_N('Z'))
            *p += TJS_N('a') - TJS_N('A');
        p++;
    }
}

void tTVPFileMedia::NormalizePathName(ttstr& name)
{
    // OHOS filesystem is case-sensitive — do NOT lowercase.
    // Only normalize path separators (backslash → forward slash).
    tjs_char* p = name.Independ();
    while (*p)
    {
        if (*p == TJS_N('\\'))
            *p = TJS_N('/');
        p++;
    }
}

bool tTVPFileMedia::CheckExistentStorage(const ttstr& name)
{
    if (name.IsEmpty())
        return false;

    ttstr _name(name);
    GetLocalName(_name);

    return TVPCheckExistentLocalFile(_name);
}

tTJSBinaryStream* tTVPFileMedia::Open(const ttstr& name, tjs_uint32 flags)
{
    if (name.IsEmpty())
        TVPThrowExceptionMessage(TVPCannotOpenStorage, TJS_N("\"\""));

    ttstr origname = name;
    ttstr _name(name);
    GetLocalName(_name);

    return new tTVPLocalFileStream(origname, _name, flags);
}

void tTVPFileMedia::GetListAt(const ttstr& _name, iTVPStorageLister* lister)
{
    ttstr name(_name);
    GetLocalName(name);
    TVPGetLocalFileListAt(name,
                          [lister](const ttstr& name, tTVPLocalFileInfo* s)
                          {
                              if (s->Mode & (S_IFREG))
                              {
                                  lister->Add(name);
                              }
                          });
}

// ASCII-only case-insensitive comparison (sufficient for filenames)
static int _utf8_strcasecmp(const char* a, const char* b)
{
    for (; *a && *b; ++a, ++b)
    {
        int ca = *a, cb = *b;
        if ('A' <= ca && ca <= 'Z') ca += 'a' - 'A';
        if ('A' <= cb && cb <= 'Z') cb += 'a' - 'A';
        int ret = ca - cb;
        if (ret) return ret;
    }
    return *a - *b;
}

void tTVPFileMedia::GetLocallyAccessibleName(ttstr& name)
{
    // Convert TVP storage name to a real local filesystem path.
    //
    // On HarmonyOS the app sandbox prevents opendir("/") (EACCES),
    // so we cannot walk from the filesystem root like Android does.
    // Instead we use TVPNativeProjectDir as a known-good prefix
    // (its case is already correct from the launcher) and only
    // resolve case via opendir()/readdir() for components BEYOND
    // that prefix.  This mirrors the Kirikiroid2 SystemStubsExtra
    // approach.
    const tjs_char* ptr = name.c_str();
    ttstr newname;

    if (!TJS_strncmp(ptr, TJS_N("./"), 2))
    {
        ptr += 2; // skip "./"
        newname.Clear();
    }

    // Build absolute path for comparison: "/" + ptr  (strip trailing slashes)
    std::string inputAbs = std::string("/") + std::string(ptr);
    bool hadTrailingSlash = false;
    while (inputAbs.size() > 1 && inputAbs.back() == '/')
    {
        inputAbs.pop_back();
        hadTrailingSlash = true;
    }

    // Get the trusted native project directory (correct original case)
    extern ttstr TVPNativeProjectDir;
    std::string nativeDir = TVPNativeProjectDir.AsStdString();
    while (!nativeDir.empty() && nativeDir.back() == '/')
        nativeDir.pop_back();

    // Lowercase copy for prefix comparison
    std::string nativeDirLower = nativeDir;
    for (auto& c : nativeDirLower)
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';

    // Find longest prefix of inputAbs matching nativeDirLower at a path boundary
    size_t matchLen = 0;
    if (!nativeDirLower.empty())
    {
        size_t limit = std::min(inputAbs.size(), nativeDirLower.size());
        bool ok = true;
        for (size_t i = 0; i < limit && ok; i++)
        {
            char ic = inputAbs[i];
            if (ic >= 'A' && ic <= 'Z') ic += 'a' - 'A';
            if (ic == nativeDirLower[i])
            {
                if (ic == '/')
                    matchLen = i + 1;
            }
            else
            {
                ok = false;
            }
        }
        if (ok && nativeDirLower.size() <= inputAbs.size())
        {
            if (inputAbs.size() == nativeDirLower.size() ||
                inputAbs[nativeDirLower.size()] == '/')
                matchLen = nativeDirLower.size();
        }
    }

    if (matchLen > 1)
    {
        // Substitute matched portion with correctly-cased TVPNativeProjectDir
        std::string base = nativeDir.substr(0, matchLen);
        newname = ttstr(base.c_str());

        // Resolve remaining components via opendir()/readdir()
        std::string rest = inputAbs.substr(matchLen);
        const char* rp = rest.c_str();
        while (*rp)
        {
            if (*rp == '/') { rp++; continue; }
            const char* rend = rp;
            while (*rend && *rend != '/') rend++;
            std::string comp(rp, rend);
            rp = rend;

            if (newname.IsEmpty() || newname.GetLastChar() != TJS_N('/'))
                newname += "/";
            std::string dirPath = newname.AsStdString();
            DIR* dirp = opendir(dirPath.c_str());
            if (dirp)
            {
                bool found = false;
                struct dirent* entry;
                while ((entry = readdir(dirp)) != NULL)
                {
                    if (!_utf8_strcasecmp(comp.c_str(), entry->d_name))
                    {
                        newname += entry->d_name;
                        found = true;
                        break;
                    }
                }
                closedir(dirp);
                if (!found)
                {
                    // Not found — append as-is (e.g. path doesn't exist yet)
                    newname += comp.c_str();
                }
            }
            else
            {
                // Cannot open directory — append as-is
                newname += comp.c_str();
            }
        }
    }
    else
    {
        // No matching prefix — use absolute path as-is
        newname = ttstr(inputAbs.c_str());
    }

    // Restore trailing slash for directory paths
    if (hadTrailingSlash && !newname.IsEmpty() && newname.GetLastChar() != TJS_N('/'))
    {
        newname += "/";
    }

    name = newname;
}

void tTVPFileMedia::GetLocalName(ttstr& name)
{
    ttstr tmp = name;
    GetLocallyAccessibleName(tmp);
    if (tmp.IsEmpty())
        TVPThrowExceptionMessage(TVPCannotGetLocalName, name);
    name = tmp;
}

iTVPStorageMedia* TVPCreateFileMedia()
{
    return new tTVPFileMedia;
}

// ============================================================
// Memory info
// ============================================================
tjs_int TVPGetSystemFreeMemory()
{
    // Read from /proc/meminfo
    FILE* f = fopen("/proc/meminfo", "r");
    if (!f) return 256;
    char buf[256];
    long memFree = 0;
    while (fgets(buf, sizeof(buf), f)) {
        if (sscanf(buf, "MemAvailable: %ld kB", &memFree) == 1) break;
        if (sscanf(buf, "MemFree: %ld kB", &memFree) == 1) { /* keep looking for Available */ }
    }
    fclose(f);
    return (tjs_int)(memFree / 1024);
}

tjs_int TVPGetSelfUsedMemory()
{
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return 0;
    char buf[256];
    long rss = 0;
    while (fgets(buf, sizeof(buf), f)) {
        if (sscanf(buf, "VmRSS: %ld kB", &rss) == 1) break;
    }
    fclose(f);
    return (tjs_int)(rss / 1024);
}

void TVPGetMemoryInfo(TVPMemoryInfo& m)
{
    FILE* f = fopen("/proc/meminfo", "r");
    if (!f) {
        memset(&m, 0, sizeof(m));
        return;
    }
    char buf[256];
    long memTotal = 0, memFree = 0, swapTotal = 0, swapFree = 0;
    while (fgets(buf, sizeof(buf), f)) {
        sscanf(buf, "MemTotal: %ld kB", &memTotal);
        sscanf(buf, "MemFree: %ld kB", &memFree);
        sscanf(buf, "SwapTotal: %ld kB", &swapTotal);
        sscanf(buf, "SwapFree: %ld kB", &swapFree);
    }
    fclose(f);
    m.MemTotal = (unsigned long)memTotal;
    m.MemFree = (unsigned long)memFree;
    m.SwapTotal = (unsigned long)swapTotal;
    m.SwapFree = (unsigned long)swapFree;
    m.VirtualTotal = m.MemTotal + m.SwapTotal;
    m.VirtualUsed = m.VirtualTotal - m.MemFree - m.SwapFree;
}

// ============================================================
// TVPGetDefaultFileDir - returns writable data dir
// (Overridden in krkrsdl_harmony.cpp via g_krkr2_data_dir)
// ============================================================
// Note: On Harmony, TVPGetDefaultFileDir is defined in krkrsdl_harmony.cpp
// to return the injected sandbox data directory. We don't define it here
// to avoid duplicate symbol.
extern std::string TVPGetDefaultFileDir();

// ============================================================
// Startup / path helpers
// ============================================================
bool TVPCheckStartupArg()
{
    return false;
}

std::vector<std::string> TVPGetDriverPath()
{
    std::vector<std::string> ret;
    ret.emplace_back("/");
    return ret;
}

bool TVPCheckStartupPath(const std::string& path)
{
    return true;
}

std::string TVPGetPackageVersionString()
{
    return "krkr2-harmony-1.0.0";
}

std::vector<std::string> TVPGetAppStoragePath()
{
    std::vector<std::string> ret;
    ret.emplace_back(TVPGetDefaultFileDir());
    return ret;
}

// ============================================================
// TVPExitApplication - throw exception instead of exit()
// ============================================================
void TVPExitApplication(int code)
{
    TVPDeliverCompactEvent(TVP_COMPACT_LEVEL_MAX);
    if (!TVPIsSoftwareRenderManager())
        iTVPTexture2D::RecycleProcess();
    throw KR2ExitException(code);
}

// ============================================================
// File operations
// ============================================================
bool TVPDeleteFile(const std::string& filename)
{
    return unlink(filename.c_str()) == 0;
}

bool TVPRenameFile(const std::string& from, const std::string& to)
{
    return rename(from.c_str(), to.c_str()) == 0;
}

bool TVPCopyFile(const std::string& from, const std::string& to)
{
    FILE* in = fopen(from.c_str(), "rb");
    if (!in) return false;
    FILE* out = fopen(to.c_str(), "wb");
    if (!out) { fclose(in); return false; }
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        fwrite(buf, 1, n, out);
    }
    fclose(in);
    fclose(out);
    return true;
}

// ============================================================
// IME control
// ============================================================
void TVPShowIME(int x, int y, int w, int h)
{
    SDL_Rect rect = { x, y, w, h };
    SDL_SetTextInputRect(&rect);
    SDL_StartTextInput();
}

void TVPHideIME()
{
    SDL_StopTextInput();
}

// ============================================================
// Process / timing
// ============================================================
void TVPProcessInputEvents()
{
}

void TVPRelinquishCPU()
{
    usleep(0);
}

tjs_uint32 TVPGetRoughTickCount32()
{
    return SDL_GetTicks();
}

void TVPPrintLog(const char* str)
{
#ifdef __OHOS__
    OH_LOG_Print(LOG_APP, LOG_INFO, 0xFF00, "krkr2Log", "%{public}s", str);
#else
    printf("%s", str);
#endif
}

// ============================================================
// File stat / utime
// ============================================================
bool TVP_stat(const char* name, tTVP_stat& s)
{
    struct stat t;
    bool ret = !stat(name, &t);
    s.tvp_mode = t.st_mode;
    s.tvp_size = t.st_size;
    s.tvp_atime = t.st_atime;
    s.tvp_mtime = t.st_mtime;
    s.tvp_ctime = t.st_ctime;
    return ret;
}

bool TVP_utime(const char* name, time_t modtime)
{
    struct utimbuf utb;
    utb.modtime = modtime;
    utb.actime = modtime;
    return utime(name, &utb) == 0;
}

// ============================================================
// Message boxes (SDL2 based)
// ============================================================
int TVPShowSimpleMessageBox(const ttstr& text,
                            const ttstr& caption,
                            const std::vector<ttstr>& vecButtons)
{
    // On HarmonyOS, use SDL_ShowSimpleMessageBox for basic UI
    std::string textStr = text.AsStdString();
    std::string capStr = caption.AsStdString();
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION,
                             capStr.c_str(), textStr.c_str(), nullptr);
    return 0;
}

int TVPShowSimpleMessageBox(const ttstr& text, const ttstr& caption)
{
    std::vector<ttstr> btns;
    btns.emplace_back(TJS_W("OK"));
    return TVPShowSimpleMessageBox(text, caption, btns);
}

int TVPShowSimpleMessageBoxYesNo(const ttstr& text, const ttstr& caption)
{
    std::vector<ttstr> btns;
    btns.emplace_back(TJS_W("Yes"));
    btns.emplace_back(TJS_W("No"));
    return TVPShowSimpleMessageBox(text, caption, btns);
}

int TVPShowSimpleInputBox(ttstr& text, const ttstr& caption, const ttstr& prompt,
                          const std::vector<ttstr>& vecButtons)
{
    return -1;
}

extern "C" int TVPShowSimpleMessageBox(const char* text,
                                       const char* caption,
                                       unsigned int nButton,
                                       const char** btnText)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, caption, text, nullptr);
    return 0;
}

// ============================================================
// Preferences
// ============================================================
const std::string& TVPGetInternalPreferencePath()
{
    static std::string ret;
    if (ret.empty()) {
        char* path = SDL_GetPrefPath("krkr2", "krkr2");
        if (path) {
            ret = path;
            SDL_free(path);
        } else {
            ret = "./";
        }
        ret += ".preference/";
        mkdir(ret.c_str(), 0755);
    }
    return ret;
}

// ============================================================
// Misc stubs
// ============================================================
void TVPCheckMemory() {}
void TVPFetchSDCardPermission() {}
void TVPControlAdDialog(int adType, int arg1, int arg2) {}
void TVPForceSwapBuffer() {}
void TVPSendToOtherApp(const std::string& path)
{
    SDL_OpenURL(path.c_str());
}

void TVPCheckAndSendDumps(const std::string&, const std::string&, const std::string&) {}

std::string TVPShowFileSelector(const std::string& title,
                                const std::string& filename,
                                std::string initdir,
                                bool issave)
{
    return "";
}

std::string TVPShowDirectorySelector(const std::string& title,
                                     std::string initdir,
                                     std::string rootdir)
{
    return "";
}

tTVPMemoryStream* GetResourceStream(const ttstr& filename)
{
    // First try ExePath/filename (bundled resource alongside the exe/lib)
    try {
        tTJSBinaryStream* tmp = TVPCreateBinaryStreamForRead(ExePath() + ttstr("/") + filename, 0);
        tTVPMemoryStream* ret = new tTVPMemoryStream(nullptr, tmp->GetSize());
        tmp->ReadBuffer(ret->GetInternalBuffer(), tmp->GetSize());
        delete tmp;
        return ret;
    } catch (...) {}

    // Fallback: try reading from the OHOS system font directory via fopen()
    // (same approach as Kirikiroid2's KR2_HARMONY_BUILD path)
    std::string fn = filename.AsStdString();
    static const char* ohosFontPaths[] = {
        "/system/fonts/HarmonyOS_Sans_SC.ttf",
        "/system/fonts/NotoSansCJK-Regular.ttc",
        "/system/fonts/DroidSansFallback.ttf",
        "/system/fonts/NotoSansHans-Regular.otf",
        "/system/fonts/HarmonyOS_Sans.ttf",
        "/system/fonts/HarmonyOS_Sans_SC_Regular.ttf",
        nullptr
    };
    for (int i = 0; ohosFontPaths[i]; ++i) {
        const char* base = strrchr(ohosFontPaths[i], '/');
        if (!base) continue;
        bool exactMatch = (fn == (base + 1));
        bool anyFont   = (fn == "DroidSansFallback.ttf"); // generic fallback request
        if (!exactMatch && !anyFont) continue;
        FILE* fp = fopen(ohosFontPaths[i], "rb");
        if (!fp) continue;
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (sz > 0) {
            tTVPMemoryStream* ret = new tTVPMemoryStream(nullptr, (tjs_uint)sz);
            fread(ret->GetInternalBuffer(), 1, sz, fp);
            fclose(fp);
            SDL_Log("[krkr2-font] Loaded system font: %s", ohosFontPaths[i]);
            return ret;
        }
        fclose(fp);
    }
    return nullptr;
}

// ============================================================
// TVPPreNormalizeStorageName - POSIX version
// ============================================================
void TVPPreNormalizeStorageName(ttstr& name)
{
    tjs_int namelen = name.GetLen();
    if (namelen == 0)
        return;

    // Check for absolute POSIX path: /path/to/file
    if (name[0] == TJS_N('/'))
    {
        ttstr newname(TJS_N("file://."));
        newname += name;
        name = newname;
        return;
    }
}

// ============================================================
// Screen info
// ============================================================
#include "TVPScreen.h"
int tTVPScreen::GetWidth()
{
    // Try to get actual display size
    SDL_DisplayMode mode;
    if (SDL_GetCurrentDisplayMode(0, &mode) == 0) {
        return mode.w;
    }
    return 1920;
}
int tTVPScreen::GetHeight()
{
    SDL_DisplayMode mode;
    if (SDL_GetCurrentDisplayMode(0, &mode) == 0) {
        return mode.h;
    }
    return 1080;
}
int tTVPScreen::GetDesktopLeft() { return 0; }
int tTVPScreen::GetDesktopTop() { return 0; }
int tTVPScreen::GetDesktopWidth() { return GetWidth(); }
int tTVPScreen::GetDesktopHeight() { return GetHeight(); }
