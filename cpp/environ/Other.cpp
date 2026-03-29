#include <string>
#ifndef _KRKRSDL3_OHOS
#include <filesystem>
#endif
#ifdef _KRKRSDL3_OHOS
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <cstring>
#endif

#include "Platform.h"
#include "tjsCommHead.h"
#include "WindowIntf.h"
#include "TVPWindow.h"
#include "TVPStorage.h"
#include "TVPMsg.h"
#include "Random.h"
#include "TVPApplication.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

// SDL_dialog removed in SDL2 migration

#include "tjsNativeMenuItem.h"

//---------------------------------------------------------------------------
// TVPLocalExtrectFilePath
//---------------------------------------------------------------------------
ttstr TVPLocalExtractFilePath(const ttstr& name)
{
    // this extracts given name's path under local filename rule
    const tjs_char* p = name.c_str();
    tjs_int i = name.GetLen() - 1;
    for (; i >= 0; i--)
    {
        if (p[i] == TJS_N(':') || p[i] == TJS_N('/') || p[i] == TJS_N('\\'))
            break;
    }
    return ttstr(p, i + 1);
}
bool TVPWriteDataToFile(const ttstr& filepath, const void* data, unsigned int len)
{
    SDL_RWops* handle = SDL_RWFromFile(filepath.c_str(), "wb");
    if (!handle) {
        return false;
    }
    size_t written = SDL_RWwrite(handle, data, 1, len);
    SDL_RWclose(handle);
    return written == len;
}
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
// TVPCreateFolders
//---------------------------------------------------------------------------
static bool _TVPCreateFolders(const ttstr& folder)
{
    // create directories along with "folder"
    if (folder.IsEmpty())
        return true;

    if (TVPCheckExistentLocalFolder(folder))
        return true; // already created

    const tjs_char* p = folder.c_str();
    tjs_int i = folder.GetLen() - 1;

    if (p[i] == TJS_N(':'))
        return true;

    while (i >= 0 && (p[i] == TJS_N('/') || p[i] == TJS_N('\\')))
        i--;

    if (i >= 0 && p[i] == TJS_N(':'))
        return true;

    for (; i >= 0; i--)
    {
        if (p[i] == TJS_N(':') || p[i] == TJS_N('/') || p[i] == TJS_N('\\'))
            break;
    }

    ttstr parent(p, i + 1);
    if (!TVPCreateFolders(parent))
        return false;

#ifdef _KRKRSDL3_OHOS
    return mkdir(folder.AsStdString().c_str(), 0755) == 0;
#else
    return std::filesystem::create_directory(folder.AsStdString().c_str());
#endif
}
bool TVPCreateFolders(const ttstr& folder)
{
    if (folder.IsEmpty())
        return true;

    const tjs_char* p = folder.c_str();
    tjs_int i = folder.GetLen() - 1;

    if (p[i] == TJS_N(':'))
        return true;

    if (p[i] == TJS_N('/') || p[i] == TJS_N('\\'))
        i--;

    return _TVPCreateFolders(ttstr(p, i + 1));
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPLocalFileStream
//---------------------------------------------------------------------------
tTVPLocalFileStream::tTVPLocalFileStream(const ttstr& origname,
                                         const ttstr& localname,
                                         tjs_uint32 flag)
  : MemBuffer(nullptr),
    FileName(localname),
    Handle(nullptr)
{
    tjs_uint32 access = flag & TJS_BS_ACCESS_MASK;
    if (access == TJS_BS_WRITE)
    {
        if (TVPCheckExistentLocalFile(localname))
        {
        }
        else
        {
            ttstr dirpath = TVPLocalExtractFilePath(localname);
            const tjs_char* p = dirpath.c_str();
            tjs_int i = dirpath.GetLen();
            if (p[i - 1] == TJS_N('/') || p[i - 1] == TJS_N('\\'))
                i--;
            dirpath = dirpath.SubString(0, i);
            if (!TVPCheckExistentLocalFolder(dirpath) && !TVPCreateFolders(dirpath))
            {
                TVPThrowExceptionMessage(TVPCannotOpenStorage, origname);
            }
        }
        MemBuffer = new tTVPMemoryStream();
        return;
    }

    const char* mode = nullptr;
    switch (access)
    {
        case TJS_BS_READ:
            mode = "rb";
            break;
        case TJS_BS_WRITE:
            mode = "wb+";
            break;
        case TJS_BS_APPEND:
            mode = "ab+";
            break;
        case TJS_BS_UPDATE:
            mode = "rb+";
            break;
    }

    Handle = SDL_RWFromFile(localname.c_str(), mode);

    if (!Handle)
    {
        if (access == TJS_BS_APPEND || access == TJS_BS_UPDATE)
        {
            Handle = SDL_RWFromFile(localname.c_str(), "rb");
            if (Handle)
            {
                Sint64 size = SDL_RWsize((SDL_RWops*)Handle);
                if (size > 0 && size < 4 * 1024 * 1024)
                {
                    MemBuffer = new tTVPMemoryStream();
                    MemBuffer->SetSize(static_cast<tjs_uint64>(size));
                    SDL_RWread((SDL_RWops*)Handle, MemBuffer->GetInternalBuffer(), 1, size);
                }
                SDL_RWclose((SDL_RWops*)Handle);
            }
            if (!MemBuffer)
                TVPThrowExceptionMessage(TVPCannotOpenStorage, origname);
        }
    }

    // push current tick as an environment noise
    uint32_t tick = TVPGetRoughTickCount32();
    TVPPushEnvironNoise(&tick, sizeof(tick));
}
//---------------------------------------------------------------------------
tTVPLocalFileStream::~tTVPLocalFileStream()
{
    if (MemBuffer)
    {
        if (!TVPWriteDataToFile(FileName, MemBuffer->GetInternalBuffer(), MemBuffer->GetSize()))
        {
            delete MemBuffer;
            ttstr filename(FileName);
            FileName.~tTJSString();
            free(this);
            TVPThrowExceptionMessage(TJS_N("File Writing Error: %1"), filename);
        }
        delete MemBuffer;
    }
    if (Handle)
    {
        SDL_RWclose((SDL_RWops*)Handle);
    }

    // push current tick as an environment noise
    // (timing information from file accesses may be good noises)
    uint32_t tick = TVPGetRoughTickCount32();
    TVPPushEnvironNoise(&tick, sizeof(tick));
}
//---------------------------------------------------------------------------
tjs_uint64 tTVPLocalFileStream::Seek(tjs_int64 offset, tjs_int whence)
{
    if (MemBuffer)
    {
        return MemBuffer->Seek(offset, whence);
    }
    int sdl_whence;
    switch (whence)
    {
        case SEEK_SET:
            sdl_whence = RW_SEEK_SET;
            break;
        case SEEK_CUR:
            sdl_whence = RW_SEEK_CUR;
            break;
        case SEEK_END:
            sdl_whence = RW_SEEK_END;
            break;
        default:
            sdl_whence = RW_SEEK_SET;
            break;
    }
    return static_cast<tjs_uint64>(SDL_RWseek((SDL_RWops*)Handle, offset, sdl_whence));
}
//---------------------------------------------------------------------------
tjs_uint tTVPLocalFileStream::Read(void* buffer, tjs_uint read_size)
{
    if (MemBuffer)
    {
        return MemBuffer->Read(buffer, read_size);
    }
    return static_cast<tjs_uint>(SDL_RWread((SDL_RWops*)Handle, buffer, 1, read_size));
}
//---------------------------------------------------------------------------
tjs_uint tTVPLocalFileStream::Write(const void* buffer, tjs_uint write_size)
{
    if (MemBuffer)
    {
        return MemBuffer->Write(buffer, write_size);
    }
    return static_cast<tjs_uint>(SDL_RWwrite((SDL_RWops*)Handle, buffer, 1, write_size));
}
//---------------------------------------------------------------------------
void tTVPLocalFileStream::SetEndOfStorage()
{
    if (MemBuffer)
    {
        return MemBuffer->SetEndOfStorage();
    }

    SDL_RWseek((SDL_RWops*)Handle, 0, RW_SEEK_END);
}
//---------------------------------------------------------------------------
tjs_uint64 tTVPLocalFileStream::GetSize()
{
    if (MemBuffer)
    {
        return MemBuffer->GetSize();
    }
    return static_cast<tjs_uint64>(SDL_RWsize((SDL_RWops*)Handle));
}
//---------------------------------------------------------------------------

#ifndef _KRKRSDL3_OHOS
std::string TVPShowFileSelector(const std::string& title,
                                const std::string& filename,
                                std::string initdir,
                                bool issave)
{
    // SDL2 stub — SDL3 file dialog API not available
    (void)title; (void)filename; (void)initdir; (void)issave;
    return std::string();
}

std::string TVPShowDirectorySelector(const std::string& title,
                                     std::string initdir,
                                     std::string rootdir)
{
    // SDL2 stub — SDL3 file dialog API not available
    (void)title; (void)initdir; (void)rootdir;
    return std::string();
}

bool TVPInputQuery(const std::string& title, const std::string& prompt, std::string& value)
{
    // SDL2 stub — use SDL_ShowSimpleMessageBox as fallback
    // Full input dialog was SDL3_ttf-based; can be re-implemented if needed
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title.c_str(), prompt.c_str(), NULL);
    return false;
}

int TVPShowSimpleInputBox(ttstr& text,
                          const ttstr& caption,
                          const ttstr& prompt,
                          const std::vector<ttstr>&)
{
    std::string inputStr = text.AsStdString();
    bool res = TVPInputQuery(caption.AsStdString(), prompt.AsStdString(), inputStr);
    text = inputStr;
    if (res)
        return 0;
    else
        return 1;
}

int TVPShowSimpleMessageBox(const ttstr& text,
                            const ttstr& caption,
                            const std::vector<ttstr>& vecButtons)
{
    std::vector<std::string> sdlButtonTexts;
    std::vector<SDL_MessageBoxButtonData> sdlButtons;
    sdlButtons.resize(vecButtons.size());
    for (const auto& btn : vecButtons)
    {
        sdlButtonTexts.push_back(btn.AsStdString());
    }
    for (size_t i = 0; i < vecButtons.size(); ++i)
    {
        SDL_MessageBoxButtonData btn = {0};
        btn.buttonid = static_cast<int>(i);

        if (i == 0)
        {
            btn.flags |= SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
        }
        if (i == vecButtons.size() - 1)
        {
            btn.flags |= SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
        }

        btn.text = sdlButtonTexts.at(i).c_str();
        sdlButtons.at(i) = btn;
    }

    std::string titleStr = caption.AsStdString();
    std::string textStr = text.AsStdString();
    SDL_MessageBoxData msgboxData = {SDL_MESSAGEBOX_INFORMATION,
                                     nullptr,
                                     titleStr.c_str(),
                                     textStr.c_str(),
                                     static_cast<int>(vecButtons.size()),
                                     vecButtons.empty() ? nullptr : sdlButtons.data(),
                                     nullptr};

    if (vecButtons.empty())
    {
        SDL_MessageBoxButtonData defaultButton = {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT |
                                                      SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,
                                                  0, "确定"};
        msgboxData.buttons = &defaultButton;
        msgboxData.numbuttons = 1;
    }

    int buttonid = -1;
    if (SDL_ShowMessageBox(&msgboxData, &buttonid) < 0)
    {
        SDL_Log("SDL_ShowMessageBox failed: %s", SDL_GetError());
        return -1;
    }
    return buttonid;
}

int TVPShowSimpleMessageBox(const char* pszText,
                            const char* pszTitle,
                            unsigned int nButton,
                            const char** btnText)
{
    std::vector<ttstr> vecButtons;
    for (unsigned int i = 0; i < nButton; ++i)
    {
        vecButtons.emplace_back(btnText[i]);
    }
    return TVPShowSimpleMessageBox(pszText, pszTitle, vecButtons);
}
#endif // !_KRKRSDL3_OHOS

ttstr TVPGetPlatformName()
{
    const char* platform = SDL_GetPlatform();
    return ttstr(platform);
}

#ifdef _KRKRSDL3_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#ifdef _KRKRSDL3_LINUX
#include <sys/utsname.h>
#include <fstream>
#endif
#ifdef _KRKRSDL3_ANDROID
#include <sys/system_properties.h>
#endif
ttstr TVPGetOSName()
{
#ifdef _KRKRSDL3_WINDOWS
    std::string result = "Windows";
    HMODULE hMod = GetModuleHandleW(L"ntdll.dll");
    if (hMod)
    {
        typedef LONG(NTAPI * RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
        RtlGetVersionPtr RtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hMod, "RtlGetVersion");
        if (RtlGetVersion)
        {
            RTL_OSVERSIONINFOW osvi = {sizeof(osvi)};
            if (RtlGetVersion(&osvi) == 0)
            {
                if (osvi.dwMajorVersion == 10 && osvi.dwBuildNumber >= 22000)
                {
                    return "Windows 11";
                }
                switch (osvi.dwMajorVersion)
                {
                    case 10:
                        return "Windows 10";
                    case 6:
                        switch (osvi.dwMinorVersion)
                        {
                            case 3:
                                return "Windows 8.1";
                            case 2:
                                return "Windows 8";
                            case 1:
                                return "Windows 7";
                            case 0:
                                return "Windows Vista";
                        }
                        break;
                    case 5:
                        switch (osvi.dwMinorVersion)
                        {
                            case 2:
                                return "Windows XP x64";
                            case 1:
                                return "Windows XP";
                            case 0:
                                return "Windows 2000";
                        }
                        break;
                }
                return result + " " + std::to_string(osvi.dwMajorVersion) + "." +
                       std::to_string(osvi.dwMinorVersion);
            }
        }
    }

    OSVERSIONINFOW osvi = {sizeof(osvi)};
#pragma warning(push)
#pragma warning(disable : 4996)
    if (GetVersionExW(&osvi))
    {
#pragma warning(pop)
        if (osvi.dwMajorVersion == 10 && osvi.dwBuildNumber >= 22000)
            return "Windows 11";
        return result + " " + std::to_string(osvi.dwMajorVersion) + "." +
               std::to_string(osvi.dwMinorVersion);
    }
    return "Windows";
#elif defined(_KRKRSDL3_ANDROID)
    // Android版本检测
    char sdk_ver[PROP_VALUE_MAX];
    char release[PROP_VALUE_MAX];

    __system_property_get("ro.build.version.sdk", sdk_ver);
    __system_property_get("ro.build.version.release", release);

    std::string version = release;

    // 添加Android版本名称
    int sdk = atoi(sdk_ver);
    switch (sdk)
    {
        case 34:
            version += " (14)";
            break;
        case 33:
            version += " (13)";
            break;
        case 32:
            version += " (12L)";
            break;
        case 31:
            version += " (12)";
            break;
        case 30:
            version += " (11)";
            break;
        case 29:
            version += " (10)";
            break;
        case 28:
            version += " (9 Pie)";
            break;
        case 27:
            version += " (8.1 Oreo)";
            break;
        case 26:
            version += " (8.0 Oreo)";
            break;
        case 25:
            version += " (7.1 Nougat)";
            break;
        case 24:
            version += " (7.0 Nougat)";
            break;
        case 23:
            version += " (6.0 Marshmallow)";
            break;
        case 22:
            version += " (5.1 Lollipop)";
            break;
        case 21:
            version += " (5.0 Lollipop)";
            break;
    }

    return "Android " + version;

#elif defined(_KRKRSDL3_LINUX)
    // Linux发行版检测
    std::string id;
    std::string version_id;
    std::string pretty_name;
    std::ifstream file("/etc/os-release");
    std::string line;
    while (std::getline(file, line))
    {
        if (line.find("ID=") == 0)
        {
            id = line.substr(3);
            if (!id.empty() && id.front() == '"')
            {
                id = id.substr(1, id.size() - 2);
            }
        }
        else if (line.find("VERSION_ID=") == 0)
        {
            version_id = line.substr(11);
            if (!version_id.empty() && version_id.front() == '"')
            {
                version_id = version_id.substr(1, version_id.size() - 2);
            }
        }
        else if (line.find("PRETTY_NAME=") == 0)
        {
            pretty_name = line.substr(12);
            if (!pretty_name.empty() && pretty_name.front() == '"')
            {
                pretty_name = pretty_name.substr(1, pretty_name.size() - 2);
            }
        }
    }
    if (!pretty_name.empty())
    {
        return pretty_name;
    }
    if (!id.empty())
    {
        if (id == "ubuntu")
            return "Ubuntu " + version_id;
        if (id == "debian")
            return "Debian " + version_id;
        if (id == "fedora")
            return "Fedora " + version_id;
        if (id == "centos")
            return "CentOS " + version_id;
        if (id == "rhel")
            return "Red Hat " + version_id;
        if (id == "arch")
            return "Arch Linux";
        return id + " " + version_id;
    }
    struct utsname buffer;
    if (uname(&buffer) == 0)
    {
        return std::string(buffer.sysname) + " " + buffer.release;
    }

    return "Linux";
#elif defined(_KRKRSDL3_OHOS)
    // HarmonyOS version detection
    char ohos_ver[128] = "";
    FILE* fp = fopen("/etc/os-release", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
                std::string val = line + 12;
                // strip newline and quotes
                while (!val.empty() && (val.back() == '\n' || val.back() == '\r' || val.back() == '"'))
                    val.pop_back();
                if (!val.empty() && val.front() == '"') val = val.substr(1);
                snprintf(ohos_ver, sizeof(ohos_ver), "%s", val.c_str());
                break;
            }
        }
        fclose(fp);
    }
    if (ohos_ver[0] != '\0')
        return ohos_ver;
    return "HarmonyOS";
#else
    return "Unknown";
#endif
}

std::string TVPGetCurrentLanguage()
{
    // SDL2: SDL_GetPreferredLocales returns a NULL-terminated SDL_Locale array
    SDL_Locale *locales = SDL_GetPreferredLocales();
    if (!locales) return "";
    int count = 0;
    while (locales[count].language) count++;
    for (int i = 0; i < count; i++) {
        SDL_Log("Preferred locale %d: %s_%s\n", i + 1, 
            locales[i].language, 
            locales[i].country ? locales[i].country : "ANY");
    }
    std::string ret = "";
    if( count > 0)
    {
        ret += std::string(locales[0].language);
        if(locales[0].country)
        {
             ret += std::string("_") + std::string(locales[0].country);
        }
        else
        {
            ret += std::string("_ANY");
        }
    }
    SDL_free(locales);
    return ret;
}

void TVPShowPopMenu(tTJSNI_MenuItem* menu)
{
}

#ifndef _KRKRSDL3_OHOS
void TVPCheckAndSendDumps(const std::string& dumpdir,
                          const std::string& packageName,
                          const std::string& versionStr)
{
}
#endif // !_KRKRSDL3_OHOS

void TVPOpenPatchLibUrl()
{
    std::string url = "https://zeas2.github.io/Kirikiroid2_patch/patch";
    SDL_OpenURL(url.c_str());
}

#ifndef _KRKRSDL3_OHOS
std::string TVPGetDefaultFileDir()
{
    // SDL2 has no SDL_GetCurrentDirectory; use SDL_GetBasePath as fallback
    char* path = SDL_GetBasePath();
    if (!path)
    {
        return std::string();
    }
    std::string result(path);
    SDL_free(path);
    return result;
}
#endif

void TVPListDir(const std::string& folder, std::function<void(const std::string&, int)> cb)
{
#ifdef _KRKRSDL3_OHOS
    DIR* dir = opendir(folder.c_str());
    if (!dir) return;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        std::string fullpath = folder + "/" + ent->d_name;
        struct stat st;
        int mode = 0x8000;
        if (stat(fullpath.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) mode = 0x4000;
        cb(ent->d_name, mode);
    }
    closedir(dir);
#else
    try
    {
        for (const auto& entry : std::filesystem::directory_iterator(folder))
        {
            std::string filename = entry.path().filename().string();
            int mode = entry.is_directory() ? 0x4000 : 0x8000;
            cb(filename, mode);
        }
    }
    catch (...)
    {
    }
#endif
}
void TVPGetLocalFileListAt(const ttstr& name,
                           const std::function<void(const ttstr&, tTVPLocalFileInfo*)>& cb)
{
    std::string folder(name.AsStdString());
#ifdef _KRKRSDL3_OHOS
    DIR* dir = opendir(folder.c_str());
    if (!dir) return;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        std::string filename(ent->d_name);
        if (filename == "." || filename == "..") continue;
        ttstr lowerFilename(filename);
        std::string fullpath = folder + "/" + filename;
        struct stat st;
        memset(&st, 0, sizeof(st));
        stat(fullpath.c_str(), &st);
        tTVPLocalFileInfo info;
        info.NativeName = filename.c_str();
        info.Mode = S_ISDIR(st.st_mode) ? 0x4000 : 0x8000;
        info.Size = S_ISDIR(st.st_mode) ? 0 : st.st_size;
        info.AccessTime = st.st_atime;
        info.ModifyTime = st.st_mtime;
        info.CreationTime = st.st_ctime;
        cb(lowerFilename, &info);
    }
    closedir(dir);
#else
    try
    {
        for (const auto& entry : std::filesystem::directory_iterator(folder))
        {
            std::string filename = entry.path().filename().string();
            if (filename == "." || filename == "..")
            {
                continue;
            }

            ttstr lowerFilename(filename);

            auto status = entry.status();
            auto ftime = std::filesystem::last_write_time(entry);

            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - std::filesystem::file_time_type::clock::now() +
                std::chrono::system_clock::now());
            std::time_t cftime = std::chrono::system_clock::to_time_t(sctp);

            tTVPLocalFileInfo info;
            info.NativeName = filename.c_str();
            info.Mode = std::filesystem::is_directory(status) ? 0x4000 : 0x8000;
            info.Size = entry.is_directory() ? 0 : std::filesystem::file_size(entry);
            info.AccessTime = cftime;
            info.ModifyTime = cftime;
            info.CreationTime = cftime;
            cb(lowerFilename, &info);
        }
    }
    catch (...)
    {
    }
#endif
}

#ifndef _KRKRSDL3_OHOS
std::vector<std::string> TVPGetAppStoragePath()
{
    std::vector<std::string> ret;
    ret.emplace_back(TVPGetDefaultFileDir());
    return ret;
}
#endif

#ifndef _KRKRSDL3_OHOS
#include "TVPScreen.h"
int tTVPScreen::GetWidth()
{
    return 1920;
}
int tTVPScreen::GetHeight()
{
    int w = GetWidth();
    int h = (w * 720) / 1280;
    return h;
}

int tTVPScreen::GetDesktopLeft()
{
    return 0;
}
int tTVPScreen::GetDesktopTop()
{
    return 0;
}
int tTVPScreen::GetDesktopWidth()
{
    return GetWidth();
}
int tTVPScreen::GetDesktopHeight()
{
    return GetHeight();
}
#endif
