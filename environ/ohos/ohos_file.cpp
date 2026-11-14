// OHOS file I/O — POSIX (musl libc)
// Replaces sdl3 file.cpp

#include "tjsCommHead.h"
#include "Platform.h"
#include "PlatformFile.h"
#include "Random.h"
#include "TVPStorage.h"
#include "TVPMsg.h"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <functional>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>

// ─── Helpers ───────────────────────────────────────────────────
static ttstr TVPLocalExtractFilePath(const ttstr& name)
{
    const tjs_char* p = name.c_str();
    tjs_int i = name.GetLen() - 1;
    for (; i >= 0; i--) {
        if (p[i] == TJS_N(':') || p[i] == TJS_N('/') || p[i] == TJS_N('\\'))
            break;
    }
    return ttstr(p, i + 1);
}

static bool TVPWriteDataToFile(const ttstr& filepath, const void* data, unsigned int len)
{
    FILE* fp = fopen(filepath.AsStdString().c_str(), "wb");
    if (!fp) return false;
    size_t written = fwrite(data, 1, len, fp);
    fclose(fp);
    return written == len;
}

static bool TVPCopyFolder(const std::string& from, const std::string& to)
{
    if (!TVPCheckExistentLocalFolder(to) && !TVPCreateFolders(to))
        return false;
    bool success = true;
    TVPListDir(from, [&](const std::string& name, int mask) {
        if (name == "." || name == "..") return;
        if (!success) return;
        if (mask & S_IFREG)
            success = TVPCopyFile(from + "/" + name, to + "/" + name);
        else if (mask & S_IFDIR)
            success = TVPCopyFolder(from + "/" + name, to + "/" + name);
    });
    return success;
}

static bool TVPRemoveDirectory(const std::string& path)
{
    DIR* dir = opendir(path.c_str());
    if (!dir) return rmdir(path.c_str()) == 0;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        std::string full = path + "/" + entry->d_name;
        struct stat st;
        if (stat(full.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) TVPRemoveDirectory(full);
            else unlink(full.c_str());
        }
    }
    closedir(dir);
    return rmdir(path.c_str()) == 0;
}

// ─── File stream ───────────────────────────────────────────────
class tTVPLocalFileStream : public tTJSBinaryStream
{
    FILE* Handle;
    tTVPMemoryStream* MemBuffer = nullptr;
    ttstr FileName;

public:
    tTVPLocalFileStream(const ttstr& origname, const ttstr& localname, tjs_uint32 flag);
    ~tTVPLocalFileStream();

    tjs_uint64 Seek(tjs_int64 offset, tjs_int whence);
    tjs_uint Read(void* buffer, tjs_uint read_size);
    tjs_uint Write(const void* buffer, tjs_uint write_size);
    bool Flush();
    void SetEndOfStorage();
    tjs_uint64 GetSize();
    const std::string GetFileName() { return FileName.AsStdString(); }
};

tTJSBinaryStream* TVPCreateLocalFileStream(const ttstr& origname,
                                           const ttstr& localname,
                                           tjs_uint32 flag)
{
    return new tTVPLocalFileStream(origname, localname, flag);
}

tTVPLocalFileStream::tTVPLocalFileStream(const ttstr& origname,
                                         const ttstr& localname,
                                         tjs_uint32 flag)
  : FileName(localname), Handle(nullptr)
{
    tjs_uint32 access = flag & TJS_BS_ACCESS_MASK;
    if (access == TJS_BS_WRITE) {
        if (!TVPCheckExistentLocalFile(localname)) {
            ttstr dirpath = TVPLocalExtractFilePath(localname);
            const tjs_char* p = dirpath.c_str();
            tjs_int i = dirpath.GetLen();
            if (p[i - 1] == TJS_N('/') || p[i - 1] == TJS_N('\\')) i--;
            dirpath = dirpath.SubString(0, i);
            if (!TVPCheckExistentLocalFolder(dirpath) && !TVPCreateFolders(dirpath))
                TVPThrowExceptionMessage(TVPCannotOpenStorage, origname);
        }
    }

    const char* mode = nullptr;
    switch (access) {
        case TJS_BS_READ:   mode = "rb";  break;
        case TJS_BS_WRITE:  mode = "wb+"; break;
        case TJS_BS_APPEND: mode = "ab+"; break;
        case TJS_BS_UPDATE: mode = "rb+"; break;
    }

    Handle = fopen(localname.AsStdString().c_str(), mode);
    if (!Handle) {
        if (access == TJS_BS_APPEND || access == TJS_BS_UPDATE) {
            Handle = fopen(localname.AsStdString().c_str(), "rb");
            if (Handle) {
                fseek(Handle, 0, SEEK_END);
                long size = ftell(Handle);
                if (size > 0 && size < 4 * 1024 * 1024) {
                    MemBuffer = new tTVPMemoryStream();
                    MemBuffer->SetSize(size);
                    fseek(Handle, 0, SEEK_SET);
                    fread(MemBuffer->GetInternalBuffer(), 1, size, Handle);
                }
                fclose(Handle);
                Handle = nullptr;
            }
            if (!MemBuffer)
                TVPThrowExceptionMessage(TVPCannotOpenStorage, origname);
        }
    }

    uint64_t tick = TVPGetRoughTickCount();
    TVPPushEnvironNoise(&tick, sizeof(tick));
}

tTVPLocalFileStream::~tTVPLocalFileStream()
{
    if (MemBuffer) {
        if (!TVPWriteDataToFile(FileName, MemBuffer->GetInternalBuffer(), MemBuffer->GetSize())) {
            delete MemBuffer;
            ttstr fn(FileName);
            FileName.~tTJSString();
            free(this);
            TVPThrowExceptionMessage(TJS_N("File Writing Error: %1"), fn);
        }
        delete MemBuffer;
    }
    if (Handle) fclose(Handle);

    uint64_t tick = TVPGetRoughTickCount();
    TVPPushEnvironNoise(&tick, sizeof(tick));
}

tjs_uint64 tTVPLocalFileStream::Seek(tjs_int64 offset, tjs_int whence)
{
    if (MemBuffer) return MemBuffer->Seek(offset, whence);
    fseek(Handle, offset, whence);
    return ftell(Handle);
}

tjs_uint tTVPLocalFileStream::Read(void* buffer, tjs_uint read_size)
{
    if (MemBuffer) return MemBuffer->Read(buffer, read_size);
    return (tjs_uint)fread(buffer, 1, read_size, Handle);
}

tjs_uint tTVPLocalFileStream::Write(const void* buffer, tjs_uint write_size)
{
    if (MemBuffer) return MemBuffer->Write(buffer, write_size);
    return (tjs_uint)fwrite(buffer, 1, write_size, Handle);
}

bool tTVPLocalFileStream::Flush()
{
    if (MemBuffer) return MemBuffer->Flush();
    return fflush(Handle) == 0;
}

void tTVPLocalFileStream::SetEndOfStorage() { Seek(0, SEEK_END); }

tjs_uint64 tTVPLocalFileStream::GetSize()
{
    if (MemBuffer) return MemBuffer->GetSize();
    long cur = ftell(Handle);
    fseek(Handle, 0, SEEK_END);
    long sz = ftell(Handle);
    fseek(Handle, cur, SEEK_SET);
    return sz;
}

// ─── Platform file ops ─────────────────────────────────────────

std::string TVPGetDefaultFileDir() { return "/data/storage/el2/base/haps/entry/files"; }

std::vector<std::string> TVPGetAppStoragePath()
{
    std::vector<std::string> ret;
    ret.emplace_back(TVPGetDefaultFileDir());
    return ret;
}

bool TVPCheckExistentLocalFile(const ttstr& name)
{
    if (name.IsEmpty()) return false;
    const char* path = name.c_str();
    struct stat st;
    int rc = stat(path, &st);
    return (rc == 0) && S_ISREG(st.st_mode);
}

bool TVPCheckExistentLocalFolder(const ttstr& name)
{
    if (name.IsEmpty()) return false;
    struct stat st;
    if (stat(name.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

std::string TVPSearchPath(const std::string& filename, const std::string& searchpath)
{
    std::vector<std::string> paths;
    std::string pathStr = searchpath.empty() ? (getenv("PATH") ? getenv("PATH") : "") : searchpath;
    if (pathStr.empty()) return "";

    std::stringstream ss(pathStr);
    std::string p;
    while (std::getline(ss, p, ':')) {
        if (!p.empty()) paths.push_back(p);
    }
    for (const auto& p : paths) {
        std::string full = p + "/" + filename;
        if (TVPCheckExistentLocalFile(full)) return full;
    }
    return "";
}

bool TVPDeleteFile(const std::string& filename) { return unlink(filename.c_str()) == 0; }
bool TVPDeleteFolder(const std::string& foldername) { return TVPRemoveDirectory(foldername); }
bool TVPRenameFile(const std::string& from, const std::string& to) { return rename(from.c_str(), to.c_str()) == 0; }

bool TVPCopyFile(const std::string& from, const std::string& to)
{
    FILE* ff = fopen(from.c_str(), "rb");
    if (!ff) return TVPCopyFolder(from, to);
    FILE* ft = fopen(to.c_str(), "wb");
    if (!ft) { fclose(ff); return false; }
    const int BUF = 1024 * 1024;
    std::vector<char> buf(BUF);
    size_t n;
    while ((n = fread(buf.data(), 1, BUF, ff)) > 0)
        fwrite(buf.data(), 1, n, ft);
    fclose(ff); fclose(ft);
    return true;
}

void TVPListDir(const std::string& folder, std::function<void(const std::string&, int)> cb)
{
    DIR* dir = opendir(folder.c_str());
    if (!dir) return;
    struct dirent* e;
    while ((e = readdir(dir)) != NULL) {
        std::string full = folder + "/" + e->d_name;
        struct stat st;
        int mode = 0;
        if (stat(full.c_str(), &st) == 0)
            mode = S_ISDIR(st.st_mode) ? S_IFDIR : S_IFREG;
        cb(e->d_name, mode);
    }
    closedir(dir);
}

void TVPGetLocalFileListAt(const ttstr& name,
                           const std::function<void(const ttstr&, tTVPLocalFileInfo*)>& cb)
{
    std::string folder = name.AsStdString();
    DIR* dir = opendir(folder.c_str());
    if (!dir) return;
    struct dirent* e;
    while ((e = readdir(dir)) != NULL) {
        std::string full = folder + "/" + e->d_name;
        struct stat st;
        if (stat(full.c_str(), &st) == 0) {
            tTVPLocalFileInfo info;
            info.NativeName = e->d_name;
            info.Mode = S_ISDIR(st.st_mode) ? S_IFDIR : S_IFREG;
            info.Size = st.st_size;
            info.AccessTime = st.st_atime;
            info.ModifyTime = st.st_mtime;
            info.CreationTime = st.st_ctime;

            ttstr lowerFilename(e->d_name);
            // Lowercase the filename for case-insensitive auto-path lookup.
            // NormalizePathName lowercases all lookup keys, so stored keys must match.
            {
                tjs_char* p = lowerFilename.Independ();
                while (*p)
                {
                    if (*p >= TJS_N('A') && *p <= TJS_N('Z'))
                        *p += TJS_N('a') - TJS_N('A');
                    p++;
                }
            }

            cb(lowerFilename, &info);
        }
    }
    closedir(dir);
}

bool TVPCreateFolders(const ttstr& folderttstr)
{
    std::string folder = folderttstr.AsStdString();
    if (folder.empty()) return true;
    struct stat st;
    if (stat(folder.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return true;

    size_t pos = folder.find_last_of("/\\");
    if (pos != std::string::npos && pos > 0) {
        std::string parent = folder.substr(0, pos);
        if (!TVPCreateFolders(parent)) return false;
    }
    return mkdir(folder.c_str(), 0755) == 0;
}
