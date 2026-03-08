#include "tjsCommHead.h"
#include "Platform.h"

#include <filesystem>
#include <codecvt>
#include <algorithm>

#include <SDL3/SDL.h>
#include <SDL3/SDL_iostream.h>
#include <sys/utime.h>
#include <fcntl.h>

#include "TVPSystem.h"
#include "TVPEvent.h"
#include "TVPMsg.h"
#include "TVPStorage.h"
#include "Random.h"
#include "RenderManager.h"
#include "TVPApplication.h"

//---------------------------------------------------------------------------
// tTVPFileMedia
//---------------------------------------------------------------------------
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
//---------------------------------------------------------------------------
void tTVPFileMedia::NormalizeDomainName(ttstr& name)
{
    // normalize domain name
    // make all characters small
    tjs_char* p = name.Independ();
    while (*p)
    {
        if (*p >= TJS_N('A') && *p <= TJS_N('Z'))
            *p += TJS_N('a') - TJS_N('A');
        p++;
    }
}
//---------------------------------------------------------------------------
void tTVPFileMedia::NormalizePathName(ttstr& name)
{
    // normalize path name
    // make all characters small
    tjs_char* p = name.Independ();
    while (*p)
    {
        if (*p >= TJS_N('A') && *p <= TJS_N('Z'))
            *p += TJS_N('a') - TJS_N('A');
        p++;
    }
}
//---------------------------------------------------------------------------
bool tTVPFileMedia::CheckExistentStorage(const ttstr& name)
{
    if (name.IsEmpty())
        return false;

    ttstr _name(name);
    GetLocalName(_name);

    return TVPCheckExistentLocalFile(_name);
}
//---------------------------------------------------------------------------
tTJSBinaryStream* tTVPFileMedia::Open(const ttstr& name, tjs_uint32 flags)
{
    // open storage named "name".
    // currently only local/network(by OS) storage systems are supported.
    if (name.IsEmpty())
        TVPThrowExceptionMessage(TVPCannotOpenStorage, TJS_N("\"\""));

    ttstr origname = name;
    ttstr _name(name);
    GetLocalName(_name);

    return new tTVPLocalFileStream(origname, _name, flags);
}

//---------------------------------------------------------------------------
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

static int _utf8_strcasecmp(const char* a, const char* b)
{
    for (; *a && *b; ++a, ++b)
    {
        int ca = *a, cb = *b;
        if ('A' <= ca && ca <= 'Z')
            ca += 'a' - 'A';
        if ('A' <= cb && cb <= 'Z')
            cb += 'a' - 'A';
        int ret = ca - cb;
        if (ret)
            return ret;
    }
    return *a - *b;
}

//---------------------------------------------------------------------------
void tTVPFileMedia::GetLocallyAccessibleName(ttstr& name)
{
    ttstr newname;

    const tjs_char* ptr = name.c_str();

    if (TJS_strncmp(ptr, TJS_N("./"), 2))
    {
        // differs from "./",
        // this may be a UNC file name.
        // UNC first two chars must be "\\\\" ?
        // AFAIK 32-bit version of Windows assumes that '/' can be used as a path
        // delimiter. Can UNC "\\\\" be replaced by "//" though ?

        newname = ttstr(TJS_N("\\\\")) + ptr;
    }
    else
    {
        ptr += 2; // skip "./"
        if (!*ptr)
        {
            newname = TJS_N("");
        }
        else
        {
            tjs_char dch = *ptr;
            if (*ptr < TJS_N('a') || *ptr > TJS_N('z'))
            {
                newname = TJS_N("");
            }
            else
            {
                ptr++;
                if (*ptr != TJS_N('/'))
                {
                    newname = TJS_N("");
                }
                else
                {
                    newname = ttstr(dch) + TJS_N(":") + ptr;
                }
            }
        }
    }

    // change path delimiter to '\\'
    tjs_char* pp = newname.Independ();
    while (*pp)
    {
        if (*pp == TJS_N('/'))
            *pp = TJS_N('\\');
        pp++;
    }
    name = newname;
}
//---------------------------------------------------------------------------
void tTVPFileMedia::GetLocalName(ttstr& name)
{
    ttstr tmp = name;
    GetLocallyAccessibleName(tmp);
    if (tmp.IsEmpty())
        TVPThrowExceptionMessage(TVPCannotGetLocalName, name);
    name = tmp;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
iTVPStorageMedia* TVPCreateFileMedia()
{
    return new tTVPFileMedia;
}
//---------------------------------------------------------------------------

#include <windows.h>
#include <Psapi.h>

tjs_int TVPGetSystemFreeMemory()
{
    MEMORYSTATUS info;
    GlobalMemoryStatus(&info);
    return info.dwAvailPhys / (1024 * 1024);
}

tjs_int TVPGetSelfUsedMemory()
{
    PROCESS_MEMORY_COUNTERS info;
    GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info));
    return info.WorkingSetSize / (1024 * 1024);
}

void TVPGetMemoryInfo(TVPMemoryInfo& m)
{
    MEMORYSTATUS status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatus(&status);

    m.MemTotal = status.dwTotalPhys / 1024;
    m.MemFree = status.dwAvailPhys / 1024;
    m.SwapTotal = status.dwTotalPageFile / 1024;
    m.SwapFree = status.dwAvailPageFile / 1024;
    m.VirtualTotal = status.dwTotalVirtual / 1024;
    m.VirtualUsed = (status.dwTotalVirtual - status.dwAvailVirtual) / 1024;
}

// int gettimeofday(struct timeval * val, struct timezone *)
// {
// 	if (val)
// 	{
// 		LARGE_INTEGER liTime, liFreq;
// 		QueryPerformanceFrequency(&liFreq);
// 		QueryPerformanceCounter(&liTime);
// 		val->tv_sec = (long)(liTime.QuadPart / liFreq.QuadPart);
// 		val->tv_usec = (long)(liTime.QuadPart * 1000000.0 / liFreq.QuadPart - val->tv_sec *
// 1000000.0);
// 	}
// 	return 0;
// }

void* dlopen(const char* filename, int flag)
{
    return (void*)LoadLibraryA(filename);
}

void* dlsym(void* handle, const char* funcname)
{
    return (void*)GetProcAddress((HMODULE)handle, funcname);
}

extern "C" int usleep(unsigned long us)
{
    Sleep(us / 1000);
    return 0;
}

void TVPCheckAndSendDumps(const std::string& dumpdir,
                          const std::string& packageName,
                          const std::string& versionStr);
bool TVPCheckStartupArg()
{
    return false;
}

std::vector<std::string> TVPGetDriverPath()
{
    std::vector<std::string> ret;
    char drv[4] = {'C', ':', '/', 0};
    for (char c = 'C'; c <= 'Z'; ++c)
    {
        drv[0] = c;
        switch (GetDriveTypeA(drv))
        {
            case DRIVE_REMOVABLE:
            case DRIVE_FIXED:
            case DRIVE_REMOTE:
                ret.emplace_back(drv);
                break;
        }
    }
    return ret;
}

bool TVPCheckStartupPath(const std::string& path)
{
    return true;
}

std::string TVPGetPackageVersionString()
{
    return "win32";
}

void TVPControlAdDialog(int adType, int arg1, int arg2)
{
}
void TVPForceSwapBuffer()
{
}

void TVPReleaseFontLibrary();
void TVPExitApplication(int code)
{
    // clear some static data for memory leak detect
    TVPDeliverCompactEvent(TVP_COMPACT_LEVEL_MAX);
    if (!TVPIsSoftwareRenderManager())
        iTVPTexture2D::RecycleProcess();
    exit(code);
}

bool TVPDeleteFile(const std::string& filename)
{
    return unlink(filename.c_str()) == 0;
}

bool TVPRenameFile(const std::string& from, const std::string& to)
{
    tjs_int ret = rename(from.c_str(), to.c_str());
    return !ret;
}

void CopyFileAttributes(const wchar_t* src, const wchar_t* dst)
{
    DWORD attr = GetFileAttributesW(src);
    if (attr != INVALID_FILE_ATTRIBUTES)
    {
        SetFileAttributesW(dst, attr);
    }

    HANDLE hSrc = CreateFileW(src, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    HANDLE hDst = CreateFileW(dst, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

    if (hSrc != INVALID_HANDLE_VALUE && hDst != INVALID_HANDLE_VALUE)
    {
        FILETIME createTime, accessTime, writeTime;
        if (GetFileTime(hSrc, &createTime, &accessTime, &writeTime))
        {
            SetFileTime(hDst, &createTime, &accessTime, &writeTime);
        }
        CloseHandle(hSrc);
        CloseHandle(hDst);
    }
}
void CopyFolderAttributes(const wchar_t* src, const wchar_t* dst)
{
    DWORD attr = GetFileAttributesW(src);
    if (attr != INVALID_FILE_ATTRIBUTES)
    {
        SetFileAttributesW(dst, attr);
    }
}
bool TVPCopyFolder(const std::string& from, const std::string& to)
{
    if (!TVPCheckExistentLocalFolder(to) && !TVPCreateFolders(to))
    {
        return false;
    }

    bool success = true;
    TVPListDir(from,
               [&](const std::string& _name, int mask)
               {
                   if (_name == "." || _name == "..")
                       return;
                   if (!success)
                       return;
                   if (mask & S_IFREG)
                   {
                       success = TVPCopyFile(from + "/" + _name, to + "/" + _name);
                   }
                   else if (mask & S_IFDIR)
                   {
                       success = TVPCopyFolder(from + "/" + _name, to + "/" + _name);
                   }
               });
    return success;
}
bool TVPCopyFile(const std::string& from, const std::string& to)
{
    SDL_IOStream* fFrom = SDL_IOFromFile(from.c_str(), "rb");
    if (!fFrom) {
        return TVPCopyFolder(from, to);
    }
    SDL_IOStream* fTo = SDL_IOFromFile(to.c_str(), "wb");
    if (!fTo) {
        SDL_CloseIO(fFrom);
        return false;
    }
    const int bufSize = 1 * 1024 * 1024;
    std::vector<char> buffer(bufSize);
    size_t bytesRead;
    while ((bytesRead = SDL_ReadIO(fFrom, buffer.data(), bufSize)) > 0) {
        if (SDL_WriteIO(fTo, buffer.data(), bytesRead) != bytesRead) {
            SDL_CloseIO(fFrom);
            SDL_CloseIO(fTo);
            return false;
        }
    }
    if (SDL_GetIOStatus(fFrom) != SDL_IO_STATUS_EOF) {
        SDL_CloseIO(fFrom);
        SDL_CloseIO(fTo);
        return false;
    }
    
    SDL_CloseIO(fFrom);
    SDL_CloseIO(fTo);
    return true;
}

void TVPProcessInputEvents()
{
}
void TVPShowIME(int x, int y, int w, int h)
{
}
void TVPHideIME()
{
}

void TVPRelinquishCPU()
{
    Sleep(0);
}

tjs_uint32 TVPGetRoughTickCount32()
{
    return SDL_GetTicks();
}

void TVPPrintLog(const char* str)
{
    printf("%s", str);
}

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
    _utimbuf utb;
    utb.modtime = modtime;
    utb.actime = modtime;
    ttstr filename(name);
    return _wutime((const wchar_t*)filename.c_str(), &utb) == 0;
}

void TVPSendToOtherApp(const std::string& filename)
{
}

tTVPMemoryStream* GetResourceStream(const ttstr& filename)
{
    tTJSBinaryStream* tmp = TVPCreateBinaryStreamForRead(ExePath() + ttstr("/") + filename, 0);
    tTVPMemoryStream* ret = new tTVPMemoryStream(nullptr, tmp->GetSize());
    tmp->ReadBuffer(ret->GetInternalBuffer(), tmp->GetSize());
    delete tmp;
    return ret;
}

void TVPPreNormalizeStorageName(ttstr& name)
{
    // if the name is an OS's native expression, change it according with the
    // TVP storage system naming rule.
    tjs_int namelen = name.GetLen();
    if (namelen == 0)
        return;
    if (namelen >= 2)
    {
        if ((name[0] >= TJS_N('a') && name[0] <= TJS_N('z') ||
             name[0] >= TJS_N('A') && name[0] <= TJS_N('Z')) &&
            name[1] == TJS_N(':'))
        {
            // Windows drive:path expression
            ttstr newname(TJS_N("file://./"));
            newname += name[0];
            newname += (name.c_str() + 2);
            name = newname;
            return;
        }
    }

    if (namelen >= 3)
    {
        if (name[0] == TJS_N('\\') && name[1] == TJS_N('\\') ||
            name[0] == TJS_N('/') && name[1] == TJS_N('/'))
        {
            // unc expression
            name = ttstr(TJS_N("file:")) + name;
            return;
        }
    }
}
