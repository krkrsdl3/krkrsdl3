#include "tjsCommHead.h"
#include "Platform.h"

#include <codecvt>
#include <algorithm>

#include <windows.h>
#include <Psapi.h>
#include <sys/utime.h>
#include <fcntl.h>
#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include "io_public.h"

#include "TVPSystem.h"
#include "TVPEvent.h"
#include "TVPMsg.h"
#include "Random.h"
#include "RenderManager.h"
#include "TVPApplication.h"

#pragma comment(lib, "winmm.lib")

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
		if (p[i] == TJS_W(':') || p[i] == TJS_W('/') ||
			p[i] == TJS_W('\\'))
			break;
	}
	return ttstr(p, i + 1);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPLocalFileStream
//---------------------------------------------------------------------------
#ifdef WIN32
extern std::string utf8_to_gbk(const std::string& str);
#endif
tTVPLocalFileStream::tTVPLocalFileStream(const ttstr& origname,
	const ttstr& localname, tjs_uint32 flag)
	: MemBuffer(nullptr), FileName(localname), Handle(-1)
{
	tjs_uint32 access = flag & TJS_BS_ACCESS_MASK;
	if (access == TJS_BS_WRITE) {
		if (TVPCheckExistentLocalFile(localname)) {
		}
		else {
			ttstr dirpath = TVPLocalExtractFilePath(localname);
			const tjs_char* p = dirpath.c_str();
			tjs_int i = dirpath.GetLen();
			if (p[i - 1] == TJS_W('/') || p[i - 1] == TJS_W('\\')) i--;
			dirpath = dirpath.SubString(0, i);
			if (!TVPCheckExistentLocalFolder(dirpath) && !TVPCreateFolders(dirpath)) {
				TVPThrowExceptionMessage(TVPCannotOpenStorage, origname);
			}
			//			_lastFileSystemChanged = true;
		}
		MemBuffer = new tTVPMemoryStream();
		return;
	}

	unsigned int rw = 0;
	switch (access)
	{
	case TJS_BS_READ:
		rw |= O_RDONLY;				break;
	case TJS_BS_WRITE:
		rw |= O_RDWR | O_CREAT | O_TRUNC;	break;
	case TJS_BS_APPEND:
		rw |= O_APPEND;	    break;
	case TJS_BS_UPDATE:
		rw |= O_RDWR;			    break;
	}

#ifdef WIN32
	rw |= O_BINARY;
	Handle = open(utf8_to_gbk(localname.AsStdString()).c_str(), rw, 0666);
#else
	tTJSNarrowStringHolder holder(localname().c_str());
	Handle = open(holder, rw, 0666);
#endif
	if (Handle < 0) {
		if (access == TJS_BS_APPEND || access == TJS_BS_UPDATE) {
			// use whole file writing
			Handle = open(utf8_to_gbk(localname.AsStdString()).c_str(), O_RDONLY);
			if (Handle >= 0) {
				tjs_uint64 size = GetSize();
				if (size < 4 * 1024 * 1024) { // only support file size <= 4M
					MemBuffer = new tTVPMemoryStream();
					MemBuffer->SetSize(size);
					read(Handle, MemBuffer->GetInternalBuffer(), size);
				}
				close(Handle);
				Handle = -1;
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
bool TVPWriteDataToFile(const ttstr& filepath, const void* data, unsigned int len);
tTVPLocalFileStream::~tTVPLocalFileStream()
{
	if (MemBuffer) {
		if (!TVPWriteDataToFile(FileName, MemBuffer->GetInternalBuffer(), MemBuffer->GetSize())) {
			delete MemBuffer;
			ttstr filename(FileName);
			FileName.~tTJSString();
			free(this);
			TVPThrowExceptionMessage(TJS_W("File Writing Error: %1"), filename);
		}
		delete MemBuffer;
	}
	if (Handle >= 0) {
		close(Handle);
	}

	// push current tick as an environment noise
	// (timing information from file accesses may be good noises)
	uint32_t tick = TVPGetRoughTickCount32();
	TVPPushEnvironNoise(&tick, sizeof(tick));
}
//---------------------------------------------------------------------------
tjs_uint64 TJS_INTF_METHOD tTVPLocalFileStream::Seek(tjs_int64 offset, tjs_int whence)
{
	if (MemBuffer) {
		return MemBuffer->Seek(offset, whence);
	}
	return lseek64(Handle, offset, whence);
}
//---------------------------------------------------------------------------
tjs_uint TJS_INTF_METHOD tTVPLocalFileStream::Read(void* buffer, tjs_uint read_size)
{
	if (MemBuffer) {
		return MemBuffer->Read(buffer, read_size);
	}
	return read(Handle, buffer, read_size);
}
//---------------------------------------------------------------------------
tjs_uint TJS_INTF_METHOD tTVPLocalFileStream::Write(const void* buffer, tjs_uint write_size)
{
	if (MemBuffer) {
		return MemBuffer->Write(buffer, write_size);
	}
	return write(Handle, buffer, write_size);
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPLocalFileStream::SetEndOfStorage()
{
	if (MemBuffer) {
		return MemBuffer->SetEndOfStorage();
	}
	lseek64(Handle, 0, SEEK_END);
}
//---------------------------------------------------------------------------
tjs_uint64 TJS_INTF_METHOD tTVPLocalFileStream::GetSize()
{
	if (MemBuffer) {
		return MemBuffer->GetSize();
	}
	tjs_uint64 ret;
	tjs_int64 curpos = lseek64(Handle, 0, SEEK_CUR);
	ret = lseek64(Handle, 0, SEEK_END);
	lseek64(Handle, curpos, SEEK_SET);
	return ret;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPFileMedia
//---------------------------------------------------------------------------
class tTVPFileMedia : public iTVPStorageMedia
{
	tjs_uint RefCount;

public:
	tTVPFileMedia() { RefCount = 1; }
	~tTVPFileMedia() { ; }

	void TJS_INTF_METHOD AddRef() { RefCount++; }
	void TJS_INTF_METHOD Release()
	{
		if (RefCount == 1)
			delete this;
		else
			RefCount--;
	}

	void TJS_INTF_METHOD GetName(ttstr& name) { name = TJS_W("file"); }

	void TJS_INTF_METHOD NormalizeDomainName(ttstr& name);
	void TJS_INTF_METHOD NormalizePathName(ttstr& name);
	bool TJS_INTF_METHOD CheckExistentStorage(const ttstr& name);
	tTJSBinaryStream* TJS_INTF_METHOD Open(const ttstr& name, tjs_uint32 flags);
	void TJS_INTF_METHOD GetListAt(const ttstr& name, iTVPStorageLister* lister);
	void TJS_INTF_METHOD GetLocallyAccessibleName(ttstr& name);

public:
	void TJS_INTF_METHOD GetLocalName(ttstr& name);
};
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPFileMedia::NormalizeDomainName(ttstr& name)
{
	// normalize domain name
	// make all characters small
	tjs_char* p = name.Independ();
	while (*p)
	{
		if (*p >= TJS_W('A') && *p <= TJS_W('Z'))
			*p += TJS_W('a') - TJS_W('A');
		p++;
	}
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPFileMedia::NormalizePathName(ttstr& name)
{
	// normalize path name
	// make all characters small
	tjs_char* p = name.Independ();
	while (*p)
	{
		if (*p >= TJS_W('A') && *p <= TJS_W('Z'))
			*p += TJS_W('a') - TJS_W('A');
		p++;
	}
}
//---------------------------------------------------------------------------
bool TJS_INTF_METHOD tTVPFileMedia::CheckExistentStorage(const ttstr& name)
{
	if (name.IsEmpty()) return false;

	ttstr _name(name);
	GetLocalName(_name);

	return TVPCheckExistentLocalFile(_name);
}
//---------------------------------------------------------------------------
tTJSBinaryStream* TJS_INTF_METHOD tTVPFileMedia::Open(const ttstr& name, tjs_uint32 flags)
{
	// open storage named "name".
	// currently only local/network(by OS) storage systems are supported.
	if (name.IsEmpty())
		TVPThrowExceptionMessage(TVPCannotOpenStorage, TJS_W("\"\""));

	ttstr origname = name;
	ttstr _name(name);
	GetLocalName(_name);

	return new tTVPLocalFileStream(origname, _name, flags);
}

//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPFileMedia::GetListAt(const ttstr& _name, iTVPStorageLister* lister)
{
	ttstr name(_name);
	GetLocalName(name);
	TVPGetLocalFileListAt(name, [lister](const ttstr& name, tTVPLocalFileInfo* s) {
		if (s->Mode & (S_IFREG)) {
			lister->Add(name);
		}
		});
}

static int _utf8_strcasecmp(const char* a, const char* b) {
	for (; *a && *b; ++a, ++b) {
		int ca = *a, cb = *b;
		if ('A' <= ca && ca <= 'Z') ca += 'a' - 'A';
		if ('A' <= cb && cb <= 'Z') cb += 'a' - 'A';
		int ret = ca - cb;
		if (ret) return ret;
	}
	return *a - *b;
}

//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPFileMedia::GetLocallyAccessibleName(ttstr& name)
{
	ttstr newname;

	const tjs_char* ptr = name.c_str();

#ifdef WIN32
	if (TJS_strncmp(ptr, TJS_W("./"), 2))
	{
		// differs from "./",
		// this may be a UNC file name.
		// UNC first two chars must be "\\\\" ?
		// AFAIK 32-bit version of Windows assumes that '/' can be used as a path
		// delimiter. Can UNC "\\\\" be replaced by "//" though ?

		newname = ttstr(TJS_W("\\\\")) + ptr;
	}
	else
	{
		ptr += 2;  // skip "./"
		if (!*ptr) {
			newname = TJS_W("");
		}
		else {
			tjs_char dch = *ptr;
			if (*ptr < TJS_W('a') || *ptr > TJS_W('z')) {
				newname = TJS_W("");
			}
			else {
				ptr++;
				if (*ptr != TJS_W('/')) {
					newname = TJS_W("");
				}
				else {
					newname = ttstr(dch) + TJS_W(":") + ptr;
				}
			}
		}
	}

	// change path delimiter to '\\'
	tjs_char* pp = newname.Independ();
	while (*pp)
	{
		if (*pp == TJS_W('/')) *pp = TJS_W('\\');
		pp++;
	}
#else // posix
	if (!TJS_strncmp(ptr, TJS_W("./"), 2)) {
		ptr += 2;  // skip "./"
		newname.Clear();
	}
#if CC_TARGET_PLATFORM == CC_PLATFORM_IOS
	{
		std::string prefix = "/";
		prefix += tTJSNarrowStringHolder(ptr).Buf;
		static const std::vector<ttstr>& prefixPath = _getPrefixPath();
		static const std::vector<std::string>& homeDir = _getHomeDir();
		for (int i = 0; i < prefixPath.size(); ++i) {
			const std::string& dir = homeDir[i];
			if (prefix.length() < dir.length()) continue;
			std::string actualPrefix = prefix.substr(0, dir.length());
			if (!_utf8_strcasecmp(actualPrefix.c_str(), dir.c_str())) {
				newname = prefixPath[i];
				ptr += prefixPath[i].length();
				while (*ptr && *ptr == TJS_W('/')) ++ptr;
				break;
			}
		}
	}
#endif
	while (*ptr) {
		const tjs_char* ptr_end = ptr;
		while (*ptr_end && *ptr_end != TJS_W('/')) ++ptr_end;
		if (ptr_end == ptr) break;
		const tjs_char* ptr_cur = ptr;
		tTJSNarrowStringHolder walker(ttstr(ptr, ptr_end - ptr).c_str());
		while (*ptr_end && *ptr_end == TJS_W('/')) ++ptr_end;
		ptr = ptr_end;

		DIR* dirp;
		struct dirent* direntp;
		newname += "/";
		if ((dirp = opendir(tTJSNarrowStringHolder(newname.c_str())))) {
			bool found = false;
			while ((direntp = readdir(dirp)) != NULL) {
				if (!_utf8_strcasecmp(walker, direntp->d_name)) {
					newname += direntp->d_name;
					found = true;
					break;
				}
			}
			closedir(dirp);
			if (!found) {
				newname += ptr_cur;
				break;
			}
		}
		else {
			newname += ptr_cur;
			break;
		}
	}

#endif
	name = newname;
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPFileMedia::GetLocalName(ttstr& name)
{
	ttstr tmp = name;
	GetLocallyAccessibleName(tmp);
	if (tmp.IsEmpty()) TVPThrowExceptionMessage(TVPCannotGetLocalName, name);
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
// 		val->tv_usec = (long)(liTime.QuadPart * 1000000.0 / liFreq.QuadPart - val->tv_sec * 1000000.0);
// 	}
// 	return 0;
// }

void* dlopen(const char* filename, int flag) {
	return (void*)LoadLibraryA(filename);
}

void* dlsym(void* handle, const char* funcname) {
	return (void*)GetProcAddress((HMODULE)handle, funcname);
}

extern "C" int usleep(unsigned long us) {
	Sleep(us / 1000);
	return 0;
}

//extern "C" __declspec(dllimport) int __cdecl __wgetmainargs(int * _Argc, wchar_t *** _Argv, wchar_t *** _Env, int _DoWildCard, void * _StartInfo);
std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
std::string TVPGetDefaultFileDir() {
	wchar_t buf[MAX_PATH];
	_wgetcwd(buf, sizeof(buf) / sizeof(buf[0]));
	wchar_t* p = buf;
	while (*p) {
		if (*p == '\\') *p = '/';
		++p;
	}
	return converter.to_bytes(buf);
}

void TVPCheckAndSendDumps(const std::string& dumpdir, const std::string& packageName, const std::string& versionStr);
bool TVPCheckStartupArg() {
	int argc;
	wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	//	__wgetmainargs(&argc, &argv, &env, 0, &info);
	TVPCheckAndSendDumps(TVPGetDefaultFileDir() + "/dumps", "win32-test", "test");
	if (argc > 1) {
		if (TVPCheckExistentLocalFile(argv[1])) {
			if (TVPCheckArchive(argv[1]) == 1) {
				//TVPMainScene::GetInstance()->startupFrom(converter.to_bytes(argv[1]));
				return true;
			}
			return false;
		}
		bool bootable = false;
		TVPListDir(converter.to_bytes(argv[1]), [&](const std::string& _name, int mask) {
			if (mask & (S_IFREG)) {
				std::string name(_name);
				std::transform(name.begin(), name.end(), name.begin(), [](int c)->int {
					if (c <= 'Z' && c >= 'A')
						return c - ('A' - 'a');
					return c;
					});
				if (name == "startup.tjs") {
					bootable = true;
				}
			}
			});
		for (int i = 2; i < argc; ++i) {
			std::wstring str = argv[i];
			size_t pos = str.find(L'=');
			if (pos == str.npos) {
				TVPSetCommandLine(argv[i], "yes");
			}
			else {
				ttstr val = str.c_str() + pos + 1;
				TVPSetCommandLine(str.substr(0, pos).c_str(), val);
			}
		}
		if (bootable) {
			//TVPMainScene::GetInstance()->startupFrom(converter.to_bytes(argv[1]));
			return true;
		}
	}
	return false;
}

int TVPShowSimpleMessageBox(const ttstr& text, const ttstr& caption, const std::vector<ttstr>& vecButtons)
{
	// there has no implement under android
	switch (vecButtons.size()) {
	case 1:
		MessageBoxW(0, text.c_str(), caption.c_str(), /*MB_OK*/0);
		return 0;
		break;
	case 2:
		switch (MessageBoxW(0, text.c_str(), caption.c_str(), /*MB_YESNO*/4)) {
		case 6:
			return 0;
		default:
			return 1;
		}
		break;
	}
	return -1;
}

extern "C" int TVPShowSimpleMessageBox(const char* pszText, const char* pszTitle, unsigned int nButton, const char** btnText) {
	std::vector<ttstr> vecButtons;
	for (unsigned int i = 0; i < nButton; ++i) {
		vecButtons.emplace_back(btnText[i]);
	}
	return TVPShowSimpleMessageBox(pszText, pszTitle, vecButtons);
}

std::vector<std::string> TVPGetDriverPath() {
	std::vector<std::string> ret;
	char drv[4] = { 'C', ':', '/', 0 };
	for (char c = 'C'; c <= 'Z'; ++c) {
		drv[0] = c;
		switch (GetDriveTypeA(drv)) {
		case DRIVE_REMOVABLE:
		case DRIVE_FIXED:
		case DRIVE_REMOTE:
			ret.emplace_back(drv);
			break;
		}
	}
	return ret;
}

std::vector<std::string> TVPGetAppStoragePath() {
	std::vector<std::string> ret;
	ret.emplace_back(TVPGetDefaultFileDir());
	return ret;
}

bool TVPCheckStartupPath(const std::string& path) { return true; }

std::string TVPGetPackageVersionString() {
	return "win32";
}

void TVPControlAdDialog(int adType, int arg1, int arg2) {}
void TVPForceSwapBuffer() {}


//---------------------------------------------------------------------------
// TVPCreateFolders
//---------------------------------------------------------------------------
static bool _TVPCreateFolders(const ttstr& folder)
{
	// create directories along with "folder"
	if (folder.IsEmpty()) return true;

	if (TVPCheckExistentLocalFolder(folder))
		return true; // already created

	const tjs_char* p = folder.c_str();
	tjs_int i = folder.GetLen() - 1;

	if (p[i] == TJS_W(':')) return true;

	while (i >= 0 && (p[i] == TJS_W('/') || p[i] == TJS_W('\\'))) i--;

	if (i >= 0 && p[i] == TJS_W(':')) return true;

	for (; i >= 0; i--)
	{
		if (p[i] == TJS_W(':') || p[i] == TJS_W('/') ||
			p[i] == TJS_W('\\'))
			break;
	}

	ttstr parent(p, i + 1);
	if (!TVPCreateFolders(parent)) return false;

	return !_wmkdir(folder.c_str());

}
//---------------------------------------------------------------------------

bool TVPCreateFolders(const ttstr& folder)
{
	if (folder.IsEmpty()) return true;

	const tjs_char* p = folder.c_str();
	tjs_int i = folder.GetLen() - 1;

	if (p[i] == TJS_W(':')) return true;

	if (p[i] == TJS_W('/') || p[i] == TJS_W('\\')) i--;

	return _TVPCreateFolders(ttstr(p, i + 1));
}

bool TVPWriteDataToFile(const ttstr& filepath, const void* data, unsigned int len) {
	FILE* handle = _wfopen(filepath.c_str(), L"wb");
	if (handle) {
		bool ret = fwrite(data, 1, len, handle) == len;
		fclose(handle);
		return ret;
	}
	return false;
}

std::string TVPGetCurrentLanguage() {
	LANGID lid = GetUserDefaultUILanguage();
	const LCID locale_id = MAKELCID(lid, SORT_DEFAULT);
	char code[10] = { 0 };
	char country[10] = { 0 };
	GetLocaleInfoA(locale_id, LOCALE_SISO639LANGNAME, code, sizeof(code));
	GetLocaleInfoA(locale_id, LOCALE_SISO3166CTRYNAME, country, sizeof(country));
	std::string ret = code;
	if (country[0]) {
		for (int i = 0; i < sizeof(country) && country[i]; ++i) {
			char c = country[i];
			if (c <= 'Z' && c >= 'A') {
				country[i] += 'a' - 'A';
			}
		}
		ret += "_";
		ret += country;
	}
	return ret;
}

void TVPReleaseFontLibrary();
void TVPExitApplication(int code) {
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
    std::wstring wfrom = ttstr(from).c_str();
    std::wstring wto = ttstr(to).c_str();

    if (!CreateDirectoryW(wto.c_str(), NULL))
    {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS)
        {
            return false;
        }
    }

    WIN32_FIND_DATAW findData;
    std::wstring searchPath = wfrom + L"\\*";

    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE)
        return false;

    bool success = true;

    do
    {
        if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0)
        {
            continue;
        }

        if (!success)
            break;

        std::wstring srcPath = wfrom + L"\\" + findData.cFileName;
        std::wstring dstPath = wto + L"\\" + findData.cFileName;

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            success = TVPCopyFolder(ttstr(srcPath.c_str()).AsStdString(),
                                    ttstr(dstPath.c_str()).AsStdString());
        }
        else
        {
            success = TVPCopyFile(ttstr(srcPath.c_str()).AsStdString(),
                                  ttstr(dstPath.c_str()).AsStdString());
        }
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);

    CopyFolderAttributes(wfrom.c_str(), wto.c_str());

    return success;
}
bool TVPCopyFile(const std::string& from, const std::string& to)
{
    std::wstring wfrom = ttstr(from).c_str();
    std::wstring wto = ttstr(to).c_str();
    
    DWORD attr = GetFileAttributesW(wfrom.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return false;
    
    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        return TVPCopyFolder(from, to);
    }
    
    if (CopyFileW(wfrom.c_str(), wto.c_str(), FALSE)) {
        return true;
    }
    
    HANDLE hFrom = CreateFileW(wfrom.c_str(), GENERIC_READ, 
                              FILE_SHARE_READ, NULL, OPEN_EXISTING, 
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFrom == INVALID_HANDLE_VALUE) return false;
    
    HANDLE hTo = CreateFileW(wto.c_str(), GENERIC_WRITE, 0, NULL, 
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hTo == INVALID_HANDLE_VALUE) {
        CloseHandle(hFrom);
        return false;
    }
    
    const DWORD bufferSize = 1 * 1024 * 1024;
    std::vector<char> buffer(bufferSize);
    
    BOOL success = TRUE;
    DWORD bytesRead, bytesWritten;
    
    while (success) {
        if (!ReadFile(hFrom, buffer.data(), bufferSize, &bytesRead, NULL)) {
            success = FALSE;
            break;
        }
        if (bytesRead == 0) break; // EOF
        
        if (!WriteFile(hTo, buffer.data(), bytesRead, &bytesWritten, NULL) || 
            bytesWritten != bytesRead) {
            success = FALSE;
            break;
        }
    }
    
    CloseHandle(hFrom);
    CloseHandle(hTo);
    
    if (!success) {
        DeleteFileW(wto.c_str());
        return false;
    }
    
    CopyFileAttributes(wfrom.c_str(), wto.c_str());
    
    return true;
}

void TVPProcessInputEvents() {}
void TVPShowIME(int x, int y, int w, int h) {}
void TVPHideIME() {}

void TVPRelinquishCPU() { Sleep(0); }

tjs_uint32 TVPGetRoughTickCount32()
{
	return timeGetTime();
}

void TVPPrintLog(const char* str) {
	printf("%s", str);
}

bool TVP_stat(const tjs_char* name, tTVP_stat& s) {
	tTJSNarrowStringHolder holder(name);
	return TVP_stat(holder, s);
}
extern std::string utf8_to_gbk(const std::string& str);
bool TVP_stat(const char* name, tTVP_stat& s) {
    struct _stat64 t;
    bool ret = !_stat64(utf8_to_gbk(std::string(name)).c_str(), &t);
	s.tvp_mode = t.st_mode;
	s.tvp_size = t.st_size;
	s.tvp_atime = t.st_atime;
	s.tvp_mtime = t.st_mtime;
	s.tvp_ctime = t.st_ctime;
	return ret;
}

bool TVP_utime(const char* name, time_t modtime) {
	_utimbuf utb;
	utb.modtime = modtime;
	utb.actime = modtime;
	ttstr filename(name);
	return _wutime(filename.c_str(), &utb) == 0;
}

void TVPSendToOtherApp(const std::string& filename) {

}

#include <locale>
std::wstring utf8_to_wstr(const std::string &src) {
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    return converter.from_bytes(src);
}
std::string wstr_to_utf8(const std::wstring &src) {
    std::wstring_convert<std::codecvt_utf8<wchar_t>> convert;
    return convert.to_bytes(src);
}
std::string utf8_to_gbk(const std::string &str) {
    std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
    std::wstring tmp_wstr = conv.from_bytes(str);

    const char *GBK_LOCALE_NAME = ".936";
    std::wstring_convert<std::codecvt_byname<wchar_t, char, mbstate_t>> convert(
        new std::codecvt_byname<wchar_t, char, mbstate_t>(GBK_LOCALE_NAME));
    return convert.to_bytes(tmp_wstr);
}
std::string gbk_to_utf8(const std::string &str) {
    const char *GBK_LOCALE_NAME = ".936";
    std::wstring_convert<std::codecvt_byname<wchar_t, char, mbstate_t>> convert(
        new std::codecvt_byname<wchar_t, char, mbstate_t>(GBK_LOCALE_NAME));
    std::wstring tmp_wstr = convert.from_bytes(str);

    std::wstring_convert<std::codecvt_utf8<wchar_t>> cv2;
    return cv2.to_bytes(tmp_wstr);
}
static void GetAllFilesInPath(const std::string path, std::vector<std::string>& files)
{
	long long hFile = 0;
	struct _finddata_t fileinfo;
	if ((hFile = _findfirst(utf8_to_gbk(path + "\\*").c_str(), &fileinfo)) != -1)
	{
		do
		{
			std::string utf8Name = gbk_to_utf8(fileinfo.name);
			files.push_back(utf8Name);
		} while (!_findnext(hFile, &fileinfo));
		_findclose(hFile);
	}
}
void TVPListDir(const std::string& folder, std::function<void(const std::string&, int)> cb) {
	std::vector<std::string> dirLists;
	GetAllFilesInPath(folder, dirLists);
	tTVP_stat stat_buf;

	if (dirLists.size() > 0)
	{
		std::vector<std::string>::iterator it = dirLists.begin();
		for (; it != dirLists.end(); it++)
		{
			std::string fullpath = folder + "/" + *it;
			if (!TVP_stat(fullpath.c_str(), stat_buf)) {
				//cb(*it, 0x81b6);
				continue;
			}
			cb(*it, stat_buf.tvp_mode);
		}
	}
}
void TVPGetLocalFileListAt(const ttstr& name, const std::function<void(const ttstr&, tTVPLocalFileInfo*)>& cb) {
	std::vector<std::string> dirLists;
	std::string folder(name.AsNarrowStdString());
	GetAllFilesInPath(folder, dirLists);
	tTVP_stat stat_buf;

	if (dirLists.size() > 0)
	{
		std::vector<std::string>::iterator it = dirLists.begin();
		for (; it != dirLists.end(); it++)
		{
			std::string fullpath = folder + "/" + *it;
			if (!TVP_stat(fullpath.c_str(), stat_buf))
				continue;
			ttstr file(*it);
			if (file.length() <= 2) {
				if (file == TJS_W(".") || file == TJS_W(".."))
					continue;
			}
			tjs_char* p = file.Independ();
			while (*p)
			{
				// make all characters small
				if (*p >= TJS_W('A') && *p <= TJS_W('Z'))
					*p += TJS_W('a') - TJS_W('A');
				p++;
			}
			tTVPLocalFileInfo info;
			info.NativeName = (*it).c_str();
			info.Mode = stat_buf.tvp_mode;
			info.Size = stat_buf.tvp_size;
			info.AccessTime = stat_buf.tvp_atime;
			info.ModifyTime = stat_buf.tvp_mtime;
			info.CreationTime = stat_buf.tvp_ctime;
			cb(file, &info);
		}
	}
}

tTVPMemoryStream* GetResourceStream(const ttstr& filename)
{
    tTJSBinaryStream* tmp =
        TVPCreateBinaryStreamForRead(ExePath() + ttstr("/") + filename, 0);
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
	if (namelen == 0) return;
	if (namelen >= 2)
	{
		if ((name[0] >= TJS_W('a') && name[0] <= TJS_W('z') ||
			name[0] >= TJS_W('A') && name[0] <= TJS_W('Z')) &&
			name[1] == TJS_W(':'))
		{
			// Windows drive:path expression
			ttstr newname(TJS_W("file://./"));
			newname += name[0];
			newname += (name.c_str() + 2);
			name = newname;
			return;
		}
	}

	if (namelen >= 3)
	{
		if (name[0] == TJS_W('\\') && name[1] == TJS_W('\\') ||
			name[0] == TJS_W('/') && name[1] == TJS_W('/'))
		{
			// unc expression
			name = ttstr(TJS_W("file:")) + name;
			return;
		}
	}
}