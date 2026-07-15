#pragma once

#include <functional>

#include "UtilStreams.h"

//---------------------------------------------------------------------------
// tTVPFileMedia
//---------------------------------------------------------------------------
iTVPStorageMedia* TVPCreateFileMedia();
// creates a platform-specific local file stream
tTJSBinaryStream* TVPCreateLocalFileStream(const ttstr& origname,
                                           const ttstr& localname,
                                           tjs_uint32 flag);
//
//
std::string TVPGetDefaultFileDir();
std::vector<std::string> TVPGetAppStoragePath();
//
bool TVPCheckExistentLocalFolder(const ttstr& name);
bool TVPCheckExistentLocalFile(const ttstr& name);
std::string TVPSearchPath(const std::string& filename, const std::string& searchpath);
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
bool TVPTruncateFile(const std::string& path, size_t size);
uint16_t TVPGetFileAttributes(const std::string& path);
bool TVPSetFileAttributes(const std::string& path, uint16_t attr, uint16_t mask);
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
//
void TVPPreNormalizeStorageName(ttstr& name);