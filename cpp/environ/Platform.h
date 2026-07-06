#pragma once

#include <functional>

#include "UtilStreams.h"

// 文件/文件夹相关
//---------------------------------------------------------------------------
// tTVPLocalFileStream
//---------------------------------------------------------------------------
class tTVPLocalFileStream : public tTJSBinaryStream
{
private:
    void* Handle;
    tTVPMemoryStream* MemBuffer = nullptr;
    ttstr FileName;

public:
    tTVPLocalFileStream(const ttstr& origname, const ttstr& localname, tjs_uint32 flag);
    ~tTVPLocalFileStream();

    tjs_uint64 Seek(tjs_int64 offset, tjs_int whence);

    tjs_uint Read(void* buffer, tjs_uint read_size);
    tjs_uint Write(const void* buffer, tjs_uint write_size);

    void SetEndOfStorage();

    tjs_uint64 GetSize();
    const std::string GetFileName() { return FileName.AsStdString(); }

    void* GetHandle() const { return Handle; }
};
//---------------------------------------------------------------------------
// tTVPFileMedia
//---------------------------------------------------------------------------
iTVPStorageMedia* TVPCreateFileMedia();
//
std::string TVPGetDefaultFileDir();
std::vector<std::string> TVPGetAppStoragePath();
// 
bool TVPDeleteFile(const std::string& filename);
bool TVPDeleteFolder(const std::string& foldername);
bool TVPRenameFile(const std::string& from, const std::string& to);
bool TVPCopyFile(const std::string& from, const std::string& to);
// 
void TVPListDir(const std::string& folder, std::function<void(const std::string&, int)> cb);
void TVPGetLocalFileListAt(const ttstr& name,
                           const std::function<void(const ttstr&, tTVPLocalFileInfo*)>& cb);
bool TVPCreateFolders(const ttstr& folder);
//
tTVPMemoryStream* GetResourceStream(const ttstr& filename);
//
#ifndef S_IFDIR
#define S_IFDIR 0x4000
#endif
#ifndef S_IFREG
#define S_IFREG 0x8000
#endif
struct tTVP_stat
{
    bool tvp_isdir;
    uint64_t tvp_size;
    uint64_t tvp_atime;
    uint64_t tvp_mtime;
    uint64_t tvp_ctime;
};
bool TVP_stat(const char* name, tTVP_stat& s);
bool TVP_utime(const char* name, time_t modtime);

// CPU
//---------------------------------------------------------------------------
struct TVPMemoryInfo
{ // all in kB
    unsigned long MemTotal;
    unsigned long MemFree;
    unsigned long SwapTotal;
    unsigned long SwapFree;
    unsigned long VirtualTotal;
    unsigned long VirtualUsed;
};
void TVPGetMemoryInfo(TVPMemoryInfo& m);
tjs_int TVPGetSystemFreeMemory(); // in MB
tjs_int TVPGetSelfUsedMemory();   // in MB
void TVPRelinquishCPU();
//
class tTVPScreen
{
public:
    static int GetWidth();
    static int GetHeight();
    static int GetDesktopLeft();
    static int GetDesktopTop();
    static int GetDesktopWidth();
    static int GetDesktopHeight();
};

// 衍生UI
int TVPShowSimpleMessageBox(const ttstr& text, const ttstr& caption);
int TVPShowSimpleMessageBox(const char* text,
                                       const char* caption,
                                       unsigned int nButton,
                                       const char** btnText);
int TVPShowSimpleMessageBox(const ttstr& text,
                            const ttstr& caption,
                            const std::vector<ttstr>& vecButtons);
std::string TVPShowFileSelector(const std::string& title,
                                const std::string& filename,
                                std::string initdir,
                                bool issave);
std::string TVPShowDirectorySelector(const std::string& title,
                                     std::string initdir,
                                     std::string rootdir);
int TVPShowSimpleInputBox(ttstr& text,
                          const ttstr& caption,
                          const ttstr& prompt,
                          const std::vector<ttstr>& vecButtons);

// 日志管理
void TVPConsoleLog(const tjs_char* format, ...);

// 其它
std::string TVPGetPackageVersionString();
ttstr TVPGetOSName();
ttstr TVPGetPlatformName();
std::string TVPGetCurrentLanguage();
void TVPOpenPatchLibUrl();
tjs_uint32 TVPGetRoughTickCount32();
//
void TVPShowIME(int x, int y, int w, int h);
void TVPHideIME();
//
void TVPExitApplication(int code);
void TVPPreNormalizeStorageName(ttstr& name);
//
void TVPInvokeMenu(int x, int y, void* _menu = NULL);