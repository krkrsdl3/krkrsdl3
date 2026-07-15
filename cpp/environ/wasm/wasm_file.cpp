// WASM platform file I/O
// Game data: HTTP Range requests (on-demand, no MEMFS).
// Save data: IDBFS (Emscripten built-in) — fread/fwrite → MEMFS → IndexedDB.

#include "tjsCommHead.h"
#include "Platform.h"
#include "PlatformFile.h"
#include "TVPStorage.h"
#include "TVPMsg.h"
#include "UtilStreams.h"
#include "tjsError.h"

#include <emscripten.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <string>
#include <cstring>
#include <vector>

// ========================================================================
// JS bridge — HTTP Range reads only (no manual IDB, use IDBFS instead)
// ========================================================================
EM_ASYNC_JS(uint32_t, wasmFileGetSize, (const char* url), {
    try {
        var resp = await fetch(UTF8ToString(url), { method: 'HEAD' });
        if (!resp.ok) return 0;
        var len = parseInt(resp.headers.get('Content-Length'));
     return Number.isNaN(len) ? 0 : len;
    } catch(e) { return 0; }
});

EM_ASYNC_JS(int, wasmFileReadRange, (const char* url, int64_t offset,
                                     int32_t size, void* buf), {
    try {
        var u = UTF8ToString(url);
        var off = Number(offset);
        var end = off + size - 1;
        var resp = await fetch(u, {
            headers: { 'Range': 'bytes=' + offset + '-' + end }
        });
        if (!resp.ok && resp.status !== 206) return -1;
        var ab = await resp.arrayBuffer();
        var n = Math.min(ab.byteLength, size);
        HEAP8.set(new Uint8Array(ab, 0, n), buf);
        return n;
    } catch(e) { return -1; }
});

EM_ASYNC_JS(int, wasmFileExists, (const char* url), {
    try {
        var resp = await fetch(UTF8ToString(url), { method: 'HEAD' });
        return resp.ok ? 1 : 0;
    } catch(e) { return 0; }
});

// ========================================================================
// Path helpers
// ========================================================================
static bool isSavePath(const std::string& path)
{
    return path.find("/savedata") == 0;
}

static std::string toURL(const std::string& path)
{
    if (path.empty()) return "";
    if (path[0] == '/') return "." + path;
    return path;
}

// MEMFS-first existence check: local stat first, fall back to HTTP HEAD
static bool checkFileExists(const std::string& spath)
{
    struct stat st;
    if (stat(spath.c_str(), &st) == 0) return true;
    return wasmFileExists(toURL(spath).c_str()) == 1;
}

// ========================================================================
// tTVPWasmStream — HTTP Range reads (game data). No caching.
// ========================================================================
class tTVPWasmStream : public tTJSBinaryStream
{
    std::string _url;
    tjs_uint64 _pos;
    tjs_uint64 _size;
    bool _sizeKnown;
    tjs_uint32 _flags;
    bool _isSaveData;

    std::vector<uint8_t> _writeBuf;
    std::string _saveKey;

public:
    tTVPWasmStream(const ttstr& name, tjs_uint32 flags);
    ~tTVPWasmStream();

    tjs_uint Read(void* buffer, tjs_uint read_size) override;
    tjs_uint Write(const void* buffer, tjs_uint write_size) override;
    tjs_uint64 Seek(tjs_int64 offset, tjs_int whence) override;
    tjs_uint64 GetSize() override;
    bool Flush() override { return true; }
    void SetEndOfStorage() override;
};

tTVPWasmStream::tTVPWasmStream(const ttstr& name, tjs_uint32 flags)
    : _pos(0), _size(0), _sizeKnown(false), _flags(flags), _isSaveData(false)
{
    std::string spath = name.AsStdString();
    _isSaveData = isSavePath(spath);
    if (_isSaveData)
    {
        _saveKey = spath;
    }
    else
    {
        _url = toURL(spath);
        if ((flags & TJS_BS_ACCESS_MASK) != TJS_BS_READ)
            _saveKey = spath;
    }
}

tTVPWasmStream::~tTVPWasmStream()
{
    if (!_writeBuf.empty() && !_saveKey.empty())
    {
        // Write buffer to IDB via EM_ASYNC_JS (fallback for non-save writes)
        EM_ASM({
            try {
                var open = indexedDB.open('krkrsdl3_savedata', 1);
                open.onupgradeneeded = function(e) { e.target.result.createObjectStore('savedata'); };
                open.onsuccess = function(e) {
                    var db = e.target.result;
                    var tx = db.transaction(['savedata'], 'readwrite');
                    var store = tx.objectStore('savedata');
                    store.put(HEAP8.slice($0, $0 + $1), UTF8ToString($2));
                    tx.oncomplete = function() { db.close(); };
                };
            } catch(e) {}
        }, _writeBuf.data(), (int32_t)_writeBuf.size(), _saveKey.c_str());
    }
}

tjs_uint tTVPWasmStream::Read(void* buffer, tjs_uint read_size)
{
    if (_isSaveData || !_url.empty())
    {
        if (!_writeBuf.empty())
        {
            tjs_uint64 avail = _writeBuf.size() - (_pos >= _writeBuf.size() ? _writeBuf.size() : (size_t)_pos);
            tjs_uint n = read_size < avail ? read_size : (tjs_uint)avail;
            if (n > 0) { std::memcpy(buffer, _writeBuf.data() + _pos, n); _pos += n; }
            return n;
        }
    }
    if (_url.empty()) return 0;
    int n = wasmFileReadRange(_url.c_str(), _pos, (int32_t)read_size, buffer);
    if (n > 0) _pos += n;
    return n > 0 ? n : 0;
}

tjs_uint tTVPWasmStream::Write(const void* buffer, tjs_uint write_size)
{
    if (_saveKey.empty()) _saveKey = _url;
    size_t old = _writeBuf.size();
    _writeBuf.resize(old + write_size);
    std::memcpy(_writeBuf.data() + old, buffer, write_size);
    _pos = _writeBuf.size();
    if (_pos > _size) { _size = _pos; _sizeKnown = true; }
    return write_size;
}

tjs_uint64 tTVPWasmStream::Seek(tjs_int64 offset, tjs_int whence)
{
    switch (whence) {
        case SEEK_SET: _pos = offset; break;
        case SEEK_CUR: _pos += offset; break;
        case SEEK_END: _pos = GetSize() + offset; break;
    }
    if (_pos < 0) _pos = 0;
    return _pos;
}

tjs_uint64 tTVPWasmStream::GetSize()
{
    if (_sizeKnown) return _size;
    if (!_url.empty()) {
        int64_t sz = wasmFileGetSize(_url.c_str());
        if (sz > 0) { _size = (tjs_uint64)sz; _sizeKnown = true; }
    }
    return _size;
}

void tTVPWasmStream::SetEndOfStorage() {}

// ========================================================================
// tTVPLocalFileStream — IDBFS via stdio, with HTTP Range fallback for game data
// ========================================================================
class tTVPLocalFileStream : public tTJSBinaryStream
{
    FILE* _file = nullptr;
    tTVPWasmStream* _http = nullptr;  // fallback HTTP stream
    bool _isWrite = false;

public:
    tTVPLocalFileStream(const ttstr& origname, const ttstr& localname, tjs_uint32 flag)
    {
        const char* mode = "rb";
        _isWrite = (flag & TJS_BS_ACCESS_MASK) != TJS_BS_READ;
        if (_isWrite) {
            if ((flag & TJS_BS_ACCESS_MASK) == TJS_BS_WRITE) mode = "wb+";
            else if ((flag & TJS_BS_ACCESS_MASK) == TJS_BS_APPEND) mode = "ab+";
            else mode = "rb+";
        }
        _file = fopen(localname.AsStdString().c_str(), mode);
        if (_file) return;

        // fopen failed — try HTTP Range for game data reads
        if (!_isWrite)
        {
            _http = new tTVPWasmStream(localname, flag);
            if (_http) return;
        }
        TVPThrowExceptionMessage(TVPCannotOpenStorage, origname);
    }

    ~tTVPLocalFileStream()
    {
        if (_http) delete _http;
        else if (_file) fclose(_file);
    }

    tjs_uint64 Seek(tjs_int64 offset, tjs_int whence) override
    {
        if (_http) return _http->Seek(offset, whence);
        if (!_file) return 0;
        int w;
        switch (whence) {
            case SEEK_SET: w = SEEK_SET; break;
            case SEEK_CUR: w = SEEK_CUR; break;
            case SEEK_END: w = SEEK_END; break;
            default: w = SEEK_SET;
        }
        fseeko(_file, offset, w);
        return (tjs_uint64)ftello(_file);
    }

    tjs_uint Read(void* buffer, tjs_uint read_size) override
    {
        if (_http) return _http->Read(buffer, read_size);
        if (!_file) return 0;
        return (tjs_uint)fread(buffer, 1, read_size, _file);
    }

    tjs_uint Write(const void* buffer, tjs_uint write_size) override
    {
        if (_http) return 0;
        if (!_file) return 0;
        return (tjs_uint)fwrite(buffer, 1, write_size, _file);
    }

    bool Flush() override
    {
        if (!_file) return _http != nullptr;
        return fflush(_file) == 0;
    }

    void SetEndOfStorage() override
    {
        if (_file) fseeko(_file, 0, SEEK_END);
    }

    tjs_uint64 GetSize() override
    {
        if (_http) return _http->GetSize();
        if (!_file) return 0;
        off_t cur = ftello(_file);
        fseeko(_file, 0, SEEK_END);
        off_t sz = ftello(_file);
        fseeko(_file, cur, SEEK_SET);
        return (tjs_uint64)sz;
    }
};

tTJSBinaryStream* TVPCreateLocalFileStream(const ttstr& origname,
                                           const ttstr& localname,
                                           tjs_uint32 flag)
{
    return new tTVPLocalFileStream(origname, localname, flag);
}

// ========================================================================
// tTVPFileMedia
// ========================================================================
class tTVPFileMedia : public iTVPStorageMedia
{
    tjs_uint RefCount;
public:
    tTVPFileMedia() { RefCount = 1; }
    ~tTVPFileMedia() { ; }
    void AddRef() { RefCount++; }
    void Release() { if (RefCount == 1) delete this; else RefCount--; }
    void GetName(ttstr& name) { name = TJS_N("file"); }
    void NormalizeDomainName(ttstr& name);
    void NormalizePathName(ttstr& name);
    bool CheckExistentStorage(const ttstr& name);
    tTJSBinaryStream* Open(const ttstr& name, tjs_uint32 flags);
    void GetListAt(const ttstr& name, iTVPStorageLister* lister);
    void GetLocallyAccessibleName(ttstr& name);
};

void tTVPFileMedia::NormalizeDomainName(ttstr& name)
{
    tjs_char* p = name.Independ();
    while (*p) { if (*p >= TJS_N('A') && *p <= TJS_N('Z')) *p += TJS_N('a') - TJS_N('A'); p++; }
}

void tTVPFileMedia::NormalizePathName(ttstr& name) {}

bool tTVPFileMedia::CheckExistentStorage(const ttstr& name)
{
    if (name.IsEmpty()) return false;
    ttstr _name(name);
    GetLocallyAccessibleName(_name);
    std::string spath = _name.AsStdString();
    if (spath.empty()) return false;
    struct stat st_buf;
    if (stat(spath.c_str(), &st_buf) == 0) return true;
    if (!isSavePath(spath))
        return checkFileExists(spath);
    return false;
}

tTJSBinaryStream* tTVPFileMedia::Open(const ttstr& name, tjs_uint32 flags)
{
    if (name.IsEmpty())
        TVPThrowExceptionMessage(TVPCannotOpenStorage, TJS_N("\"\""));
    ttstr _name(name);
    GetLocallyAccessibleName(_name);
    std::string spath = _name.AsStdString();
    if (isSavePath(spath))
        return new tTVPLocalFileStream(name, _name, flags);

    // Check MEMFS first (preloaded files via ?preload=1). 
    // If fopen succeeds, use local file stream; otherwise fall back to HTTP Range.
    FILE* f = fopen(spath.c_str(), "rb");
    if (f) { fclose(f); return new tTVPLocalFileStream(name, _name, flags); }
    return new tTVPWasmStream(_name, flags);
}

void tTVPFileMedia::GetListAt(const ttstr& _name, iTVPStorageLister* lister)
{
    ttstr name(_name);
    GetLocallyAccessibleName(name);
    std::string spath = name.AsStdString();
    if (!isSavePath(spath)) return;
    DIR* d = opendir(spath.c_str());
    if (!d) return;
    struct dirent* de;
    while ((de = readdir(d)) != nullptr) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        lister->Add(ttstr(de->d_name));
    }
    closedir(d);
}

void tTVPFileMedia::GetLocallyAccessibleName(ttstr& name)
{
    ttstr newname;
    const tjs_char* ptr = name.c_str();
    if (TJS_strncmp(ptr, TJS_N("file://./"), 9) == 0) {
        ptr += 9;
        if (ptr[1] == TJS_N(':') && ptr[2] == TJS_N('/')) ptr += 3;
        newname = TJS_N("/") + ttstr(ptr);
    } else if (ptr[0] == TJS_N('.') && (ptr[1] == TJS_N('/') || ptr[1] == 0)) {
        newname = TJS_N("/") + ttstr(ptr + (ptr[1] == TJS_N('/') ? 2 : 1));
    } else if (ptr[0] != TJS_N('/')) {
        newname = TJS_N("/") + ttstr(ptr);
    } else {
        newname = name;
    }
    tjs_char* pp = newname.Independ();
    while (*pp) { if (*pp == TJS_N('\\')) *pp = TJS_N('/'); pp++; }
    name = newname;
}

iTVPStorageMedia* TVPCreateFileMedia()
{
    return new tTVPFileMedia;
}

// ========================================================================
// File utility functions
// ========================================================================

std::string TVPGetDefaultFileDir() { return "/"; }

std::vector<std::string> TVPGetAppStoragePath()
{
    std::vector<std::string> ret;
    ret.emplace_back("/");
    return ret;
}

bool TVPCheckExistentLocalFolder(const ttstr& name)
{
    if (name.IsEmpty()) return false;
    struct stat st;
    if (stat(name.AsStdString().c_str(), &st) == 0 && S_ISDIR(st.st_mode))
        return true;
    return false;
}

bool TVPCheckExistentLocalFile(const ttstr& name)
{
    if (name.IsEmpty()) return false;
    std::string spath = name.AsStdString();
    struct stat st;
    if (stat(spath.c_str(), &st) == 0 && S_ISREG(st.st_mode))
        return true;
    // Not in MEMFS/IDBFS — try HTTP for non-save game data
    if (!isSavePath(spath))
        return checkFileExists(spath);
    return false;
}

std::string TVPSearchPath(const std::string& filename, const std::string& searchpath)
{
    return "";
}

bool TVPDeleteFile(const std::string& filename) { return unlink(filename.c_str()) == 0; }
bool TVPDeleteFolder(const std::string& foldername) { return rmdir(foldername.c_str()) == 0; }
bool TVPRenameFile(const std::string& from, const std::string& to) { return rename(from.c_str(), to.c_str()) == 0; }

bool TVPCopyFile(const std::string& from, const std::string& to)
{
    FILE* f = fopen(from.c_str(), "rb");
    if (!f) return false;
    FILE* t = fopen(to.c_str(), "wb");
    if (!t) { fclose(f); return false; }
    char buf[65536];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (fwrite(buf, 1, n, t) != n) { ok = false; break; }
    }
    fclose(f); fclose(t);
    return ok;
}

void TVPListDir(const std::string& folder, std::function<void(const std::string&, int)> cb)
{
    DIR* d = opendir(folder.c_str());
    if (!d) return;
    struct dirent* de;
    while ((de = readdir(d)) != nullptr) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        int mode = 0;
        std::string full = folder + "/" + de->d_name;
        struct stat st;
        if (stat(full.c_str(), &st) == 0)
            mode = S_ISREG(st.st_mode) ? S_IFREG : (S_ISDIR(st.st_mode) ? S_IFDIR : 0);
        cb(de->d_name, mode);
    }
    closedir(d);
}

void TVPGetLocalFileListAt(const ttstr& name,
                           const std::function<void(const ttstr&, tTVPLocalFileInfo*)>& cb)
{
    std::string dir = name.AsStdString();
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    struct dirent* de;
    while ((de = readdir(d)) != nullptr) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        std::string full = dir + "/" + de->d_name;
        struct stat st;
        if (stat(full.c_str(), &st) == 0) {
            tTVPLocalFileInfo info;
            info.NativeName = de->d_name;
            info.Size = st.st_size;
            info.AccessTime = st.st_atime;
            info.ModifyTime = st.st_mtime;
            info.CreationTime = st.st_ctime;
            info.Mode = S_ISREG(st.st_mode) ? S_IFREG : (S_ISDIR(st.st_mode) ? S_IFDIR : 0);
            cb(ttstr(de->d_name), &info);
        }
    }
    closedir(d);
}

bool TVPCreateFolders(const ttstr& folderttstr)
{
    std::string folder = folderttstr.AsStdString();
    if (folder.empty()) return true;
    struct stat st;
    if (stat(folder.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return true;
    size_t pos = folder.find_last_of("/");
    if (pos != std::string::npos && pos > 0) {
        std::string parent = folder.substr(0, pos);
        if (parent.size() > 1 && !TVPCreateFolders(parent))
            return false;
    }
    return mkdir(folder.c_str(), 0777) == 0;
}

bool TVPTruncateFile(const std::string& path, size_t size)
{
    return truncate(path.c_str(), size) == 0;
}

uint16_t TVPGetFileAttributes(const std::string& path) { return 0; }
bool TVPSetFileAttributes(const std::string& path, uint16_t attr, uint16_t mask) { return true; }

// Exported stub for Emscripten
extern "C" void wasmIDBListCallback(int count, void* ptr) {}

