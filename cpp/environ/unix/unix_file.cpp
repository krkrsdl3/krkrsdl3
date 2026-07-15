#include "tjsCommHead.h"
#include "Platform.h"
#include "PlatformFile.h"

#include <fstream>

#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/time.h>
#include <sys/stat.h>

#include "TVPMsg.h"

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

    return TVPCreateLocalFileStream(origname, _name, flags);
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
iTVPStorageMedia* TVPCreateFileMedia()
{
    return new tTVPFileMedia;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
bool TVP_utime(const char* name, time_t modtime)
{
    timeval mt[2];
    mt[0].tv_sec = modtime;
    mt[0].tv_usec = 0;
    mt[1].tv_sec = modtime;
    mt[1].tv_usec = 0;
    return utimes(name, mt) == 0;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
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
tjs_int TVPGetSystemFreeMemory()
{
    TVPMemoryInfo m;
    m.MemFree = 0;
    TVPGetMemoryInfo(m);
    return m.MemFree / 1024;
}
tjs_int TVPGetSelfUsedMemory()
{
    std::ifstream statm{"/proc/self/statm"};
    tjs_int pages = 0;
    statm >> pages;
    return (pages * sysconf(_SC_PAGESIZE)) / (1024 * 1024);
}
void TVPRelinquishCPU()
{
    sched_yield();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
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

bool TVPTruncateFile(const std::string& path, size_t size)
{
    int fd = open(path.c_str(), O_WRONLY);
    if (fd < 0)
        return false;
    bool ok = (ftruncate(fd, (off_t)size) == 0);
    close(fd);
    return ok;
}

uint16_t TVPGetFileAttributes(const std::string& path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return 0xFFFF;
    uint16_t attr = 0;
    if (S_ISDIR(st.st_mode))
        attr |= 0x10;
    if (access(path.c_str(), W_OK) != 0)
        attr |= 0x01;
    return attr;
}

bool TVPSetFileAttributes(const std::string& path, uint16_t attr, uint16_t mask)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return false;
    mode_t mode = st.st_mode;
    if (mask & 0x01)
    {
        if (attr & 0x01)
            mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
        else
            mode |= (S_IWUSR | S_IWGRP | S_IWOTH);
    }
    return chmod(path.c_str(), mode) == 0;
}
//---------------------------------------------------------------------------