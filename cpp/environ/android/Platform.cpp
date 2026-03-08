#include "tjsCommHead.h"
#include "Platform.h"

#include <codecvt>
#include <algorithm>
#include <set>
#include <filesystem>

#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

#include "TVPSystem.h"
#include "TVPEvent.h"
#include "TVPMsg.h"
#include "Random.h"
#include "RenderManager.h"
#include "TVPApplication.h"

#include <SDL3/SDL.h>

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

    if (!TJS_strncmp(ptr, TJS_N("./"), 2))
    {
        ptr += 2; // skip "./"
        newname.Clear();
    }

    while (*ptr)
    {
        const tjs_char* ptr_end = ptr;
        while (*ptr_end && *ptr_end != TJS_N('/'))
            ++ptr_end;
        if (ptr_end == ptr)
            break;
        const tjs_char* ptr_cur = ptr;
        ttstr walker(ptr, ptr_end - ptr);
        while (*ptr_end && *ptr_end == TJS_N('/'))
            ++ptr_end;
        ptr = ptr_end;

        DIR* dirp;
        struct dirent* direntp;
        newname += "/";
        if ((dirp = opendir(newname.c_str())))
        {
            bool found = false;
            while ((direntp = readdir(dirp)) != NULL)
            {
                if (!_utf8_strcasecmp(walker.c_str(), direntp->d_name))
                {
                    newname += direntp->d_name;
                    found = true;
                    break;
                }
            }
            closedir(dirp);
            if (!found)
            {
                newname += ptr_cur;
                break;
            }
        }
        else
        {
            newname += ptr_cur;
            break;
        }
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

tjs_int TVPGetSystemFreeMemory()
{
    return 0;
}

tjs_int TVPGetSelfUsedMemory()
{
    return 0;
}

void TVPGetMemoryInfo(TVPMemoryInfo& m)
{
    /* to read /proc/meminfo */
    FILE* meminfo;
    char buffer[100] = {0};
    char* end;
    int found = 0;

    /* Try to read /proc/meminfo, bail out if fails */
    meminfo = fopen("/proc/meminfo", "r");

    static const char pszMemFree[] = "MemFree:", pszMemTotal[] = "MemTotal:",
                      pszSwapTotal[] = "SwapTotal:", pszSwapFree[] = "SwapFree:",
                      pszVmallocTotal[] = "VmallocTotal:", pszVmallocUsed[] = "VmallocUsed:";

    /* Read each line untill we got all we ned */
    while (fgets(buffer, sizeof(buffer), meminfo))
    {
        if (strstr(buffer, pszMemFree) == buffer)
        {
            m.MemFree = strtol(buffer + sizeof(pszMemFree), &end, 10);
            found++;
        }
        else if (strstr(buffer, pszMemTotal) == buffer)
        {
            m.MemTotal = strtol(buffer + sizeof(pszMemTotal), &end, 10);
            found++;
        }
        else if (strstr(buffer, pszSwapTotal) == buffer)
        {
            m.SwapTotal = strtol(buffer + sizeof(pszSwapTotal), &end, 10);
            found++;
            break;
        }
        else if (strstr(buffer, pszSwapFree) == buffer)
        {
            m.SwapFree = strtol(buffer + sizeof(pszSwapFree), &end, 10);
            found++;
            break;
        }
        else if (strstr(buffer, pszVmallocTotal) == buffer)
        {
            m.VirtualTotal = strtol(buffer + sizeof(pszVmallocTotal), &end, 10);
            found++;
            break;
        }
        else if (strstr(buffer, pszVmallocUsed) == buffer)
        {
            m.VirtualUsed = strtol(buffer + sizeof(pszVmallocUsed), &end, 10);
            found++;
            break;
        }
    }
    fclose(meminfo);
}

static std::vector<std::string>& split(const std::string& s,
                                       char delim,
                                       std::vector<std::string>& elems)
{
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim))
    {
        elems.emplace_back(item);
    }
    return elems;
}

std::vector<std::string> TVPGetDriverPath()
{
    return {"/"};
}

bool TVPCheckStartupPath(const std::string& path)
{
    return true;
}

std::string TVPGetPackageVersionString()
{
    return "android";
}

void TVPControlAdDialog(int adType, int arg1, int arg2)
{
}
void TVPForceSwapBuffer()
{
}

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
    FILE* fFrom = fopen(from.c_str(), "rb");
    if (!fFrom)
    {
        return TVPCopyFolder(from, to);
    }
    FILE* fTo = fopen(to.c_str(), "wb");
    if (!fTo)
    {
        fclose(fFrom);
        return false;
    }
    const int bufSize = 1 * 1024 * 1024;
    std::vector<char> buffer;
    buffer.resize(bufSize);
    size_t index = 0;
    while ((index = fread(&buffer.front(), 1, bufSize, fFrom)))
    {
        fwrite(&buffer.front(), 1, index, fTo);
    }
    fclose(fFrom);
    fclose(fTo);
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
    sched_yield();
}

tjs_uint32 TVPGetRoughTickCount32()
{
    tjs_uint32 uptime = 0;
    struct timespec on;
    if (clock_gettime(CLOCK_MONOTONIC, &on) == 0)
        uptime = on.tv_sec * 1000 + on.tv_nsec / 1000000;
    return uptime;
}

void TVPPrintLog(const char* str)
{
    printf("%s", str);
}

bool TVP_stat(const char* name, tTVP_stat& s)
{
    struct stat t;
    // static_assert(sizeof(t.st_size) == 4, "");
    static_assert(sizeof(t.st_size) == 8, "");
    bool ret = !stat(name, &t);
    s.tvp_mode = t.st_mode;
    s.tvp_size = t.st_size;
    s.tvp_atime = t.st_atim.tv_sec;
    s.tvp_mtime = t.st_mtim.tv_sec;
    s.tvp_ctime = t.st_ctim.tv_sec;
    return ret;
}

bool TVP_utime(const char* name, time_t modtime)
{
    timeval mt[2];
    mt[0].tv_sec = modtime;
    mt[0].tv_usec = 0;
    mt[1].tv_sec = modtime;
    mt[1].tv_usec = 0;
    return utimes(name, mt) == 0;
}

void TVPSendToOtherApp(const std::string& filename)
{
}

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
AAssetManager* mgr = NULL;
extern "C" JNIEXPORT void JNICALL Java_org_tvp_krkrsdl3_KRKRActivity_setNativeAssetManager(
    JNIEnv* env, jobject instance, jobject assetManager)
{
    mgr = AAssetManager_fromJava(env, assetManager);
}
tTVPMemoryStream* GetResourceStream(const ttstr& filename)
{
    AAsset* assetFile = AAssetManager_open(mgr, filename.AsStdString().c_str(), AASSET_MODE_BUFFER);
    if (assetFile)
    {
        size_t fileLength = AAsset_getLength(assetFile);
        tTVPMemoryStream* ret = new tTVPMemoryStream(nullptr, fileLength);
        AAsset_read(assetFile, ret->GetInternalBuffer(), fileLength);
        AAsset_close(assetFile);
        return ret;
    }
    return nullptr;
}

void TVPPreNormalizeStorageName(ttstr& name)
{
    // if the name is an OS's native expression, change it according
    // with the TVP storage system naming rule.
    tjs_int namelen = name.GetLen();
    if (namelen == 0)
        return;
    if (namelen >= 1)
    {
        if (name[0] == TJS_N('/'))
        {
            name = ttstr(TJS_N("file://.")) + name;
            return;
        }
    }
}
