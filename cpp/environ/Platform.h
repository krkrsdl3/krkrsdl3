#pragma once
#if defined(_WIN32) || defined(_WIN64)
#else
#include <sys/stat.h>
#endif

#include "UtilStreams.h"

struct TVPMemoryInfo
{ // all in kB
    unsigned long MemTotal;
    unsigned long MemFree;
    unsigned long SwapTotal;
    unsigned long SwapFree;
    unsigned long VirtualTotal;
    unsigned long VirtualUsed;
};

//---------------------------------------------------------------------------
// tTVPLocalFileStream
//---------------------------------------------------------------------------
class tTVPLocalFileStream : public tTJSBinaryStream
{
private:
	//HANDLE Handle;
	int Handle;
	tTVPMemoryStream* MemBuffer = nullptr;
	ttstr FileName;

public:
	tTVPLocalFileStream(const ttstr& origname, const ttstr& localname,
		tjs_uint32 flag);
	~tTVPLocalFileStream();

	tjs_uint64 TJS_INTF_METHOD Seek(tjs_int64 offset, tjs_int whence);

	tjs_uint TJS_INTF_METHOD Read(void* buffer, tjs_uint read_size);
	tjs_uint TJS_INTF_METHOD Write(const void* buffer, tjs_uint write_size);

	void TJS_INTF_METHOD SetEndOfStorage();

	tjs_uint64 TJS_INTF_METHOD GetSize();
	const std::string& TJS_INTF_METHOD GetFileName() { return FileName.AsStdString(); }

	int GetHandle() const { return Handle; }
};
//---------------------------------------------------------------------------

void TVPGetMemoryInfo(TVPMemoryInfo& m);
tjs_int TVPGetSystemFreeMemory(); // in MB
tjs_int TVPGetSelfUsedMemory(); // in MB

extern "C" int TVPShowSimpleMessageBox(const char* text, const char* caption, unsigned int nButton, const char** btnText); // C-style

int TVPShowSimpleMessageBox(const ttstr& text, const ttstr& caption, const std::vector<ttstr>& vecButtons);
int TVPShowSimpleMessageBox(const ttstr& text, const ttstr& caption);
int TVPShowSimpleMessageBoxYesNo(const ttstr& text, const ttstr& caption);

int TVPShowSimpleInputBox(ttstr& text, const ttstr& caption, const ttstr& prompt, const std::vector<ttstr>& vecButtons);

std::vector<std::string> TVPGetDriverPath();
std::vector<std::string> TVPGetAppStoragePath();
bool TVPCheckStartupPath(const std::string& path);
std::string TVPGetPackageVersionString();
void TVPExitApplication(int code);
void TVPCheckMemory();
const std::string& TVPGetInternalPreferencePath();
bool TVPDeleteFile(const std::string& filename);
bool TVPRenameFile(const std::string& from, const std::string& to);
bool TVPCopyFile(const std::string& from, const std::string& to);

void TVPShowIME(int x, int y, int w, int h);
void TVPHideIME();

void TVPRelinquishCPU();
tjs_uint32 TVPGetRoughTickCount32();
void TVPPrintLog(const char* str);

std::string TVPShowFileSelector(const std::string& title,
                                const std::string& filename,
                                std::string initdir,
                                bool issave);
std::string TVPShowDirectorySelector(const std::string& title,
                                     std::string initdir,
                                     std::string rootdir);
void TVPFetchSDCardPermission(); // for android only

struct tTVP_stat {
    uint16_t tvp_mode;
    uint64_t tvp_size;
    uint64_t tvp_atime;
    uint64_t tvp_mtime;
    uint64_t tvp_ctime;
};

bool TVP_stat(const tjs_char* name, tTVP_stat& s);
bool TVP_stat(const char* name, tTVP_stat& s);
bool TVP_utime(const char* name, time_t modtime);

void TVPSendToOtherApp(const std::string& filename);
std::string TVPGetCurrentLanguage();

void TVPListDir(const std::string& folder, std::function<void(const std::string&, int)> cb);
bool TVPSaveStreamToFile(tTJSBinaryStream* st, tjs_uint64 offset, tjs_uint64 size, ttstr outpath);
extern iTVPStorageMedia *TVPCreateFileMedia();
bool TVPCreateFolders(const ttstr &folder);
void TVPGetLocalFileListAt(
    const ttstr &name,
    const std::function<void(const ttstr &, tTVPLocalFileInfo *)> &cb);
extern bool TVPCreateFolders(const ttstr &folder);
bool TVPGetJoyPadAsyncState(tjs_uint keycode, bool getcurrent);
tTVPMemoryStream* GetResourceStream(const ttstr& filename);