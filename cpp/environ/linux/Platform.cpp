#include "tjsCommHead.h"
#include "Platform.h"

#include <filesystem>
#include <algorithm>
#include <set>
#include <fstream>

#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include "io_public.h"

#include "TVPSystem.h"
#include "TVPEvent.h"
#include "TVPMsg.h"
#include "Random.h"
#include "RenderManager.h"
#include "TVPApplication.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_messagebox.h>

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

    tTJSNarrowStringHolder holder(localname.c_str());
    Handle = open(holder, rw, 0666);

	if (Handle < 0) {
		if (access == TJS_BS_APPEND || access == TJS_BS_UPDATE) {
			// use whole file writing
			Handle = open(holder, O_RDONLY);
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

	if (!TJS_strncmp(ptr, TJS_W("./"), 2)) {
		ptr += 2;  // skip "./"
		newname.Clear();
	}

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
	struct sysinfo info;
    if(sysinfo(&info) == -1) {
        return -1;
    }
    return (info.freeram * info.mem_unit) / (1024 * 1024);
}

tjs_int TVPGetSelfUsedMemory()
{
	std::ifstream statm{ "/proc/self/statm" };
    tjs_int pages = 0;
    statm >> pages;
    return (pages * sysconf(_SC_PAGESIZE)) / (1024 * 1024);
}

void TVPGetMemoryInfo(TVPMemoryInfo& m)
{
	/* to read /proc/meminfo */
    FILE *meminfo;
    char buffer[100] = { 0 };
    char *end;
    int found = 0;

    /* Try to read /proc/meminfo, bail out if fails */
    meminfo = fopen("/proc/meminfo", "r");

    static const char pszMemFree[] = "MemFree:", pszMemTotal[] = "MemTotal:",
                      pszSwapTotal[] = "SwapTotal:",
                      pszSwapFree[] = "SwapFree:",
                      pszVmallocTotal[] = "VmallocTotal:",
                      pszVmallocUsed[] = "VmallocUsed:";

    /* Read each line untill we got all we ned */
    while(fgets(buffer, sizeof(buffer), meminfo)) {
        if(strstr(buffer, pszMemFree) == buffer) {
            m.MemFree = strtol(buffer + sizeof(pszMemFree), &end, 10);
            found++;
        } else if(strstr(buffer, pszMemTotal) == buffer) {
            m.MemTotal = strtol(buffer + sizeof(pszMemTotal), &end, 10);
            found++;
        } else if(strstr(buffer, pszSwapTotal) == buffer) {
            m.SwapTotal = strtol(buffer + sizeof(pszSwapTotal), &end, 10);
            found++;
            break;
        } else if(strstr(buffer, pszSwapFree) == buffer) {
            m.SwapFree = strtol(buffer + sizeof(pszSwapFree), &end, 10);
            found++;
            break;
        } else if(strstr(buffer, pszVmallocTotal) == buffer) {
            m.VirtualTotal = strtol(buffer + sizeof(pszVmallocTotal), &end, 10);
            found++;
            break;
        } else if(strstr(buffer, pszVmallocUsed) == buffer) {
            m.VirtualUsed = strtol(buffer + sizeof(pszVmallocUsed), &end, 10);
            found++;
            break;
        }
    }
    fclose(meminfo);
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

//extern "C" __declspec(dllimport) int __cdecl __wgetmainargs(int * _Argc, wchar_t *** _Argv, wchar_t *** _Env, int _DoWildCard, void * _StartInfo);
#define PATH_MAX 260
std::string TVPGetDefaultFileDir() {
	char buffer[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if(len == -1) {
        // 错误处理（例如抛出异常或返回空字符串）
        return "";
    }
    buffer[len] = '\0'; // 手动添加终止符
    // symbol link
    char* resolved = realpath(buffer, nullptr);
    if(resolved != nullptr) {
        std::string result(resolved);
        free(resolved);  // 记得释放内存
        return result;
    }
    return std::string(buffer);
}

void TVPCheckAndSendDumps(const std::string& dumpdir, const std::string& packageName, const std::string& versionStr);
bool TVPCheckStartupArg() {
	return false;
}

int TVPShowSimpleMessageBox(const ttstr& text, const ttstr& caption, const std::vector<ttstr>& vecButtons)
{
	std::vector<SDL_MessageBoxButtonData> sdlButtons;
    sdlButtons.reserve(vecButtons.size());

    for (size_t i = 0; i < vecButtons.size(); ++i) {
        SDL_MessageBoxButtonData btn = {0};
        btn.buttonID = static_cast<int>(i);

        if (i == 0) {
            btn.flags |= SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
        }
        if (i == vecButtons.size() - 1) {
            btn.flags |= SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
        }

        btn.text = vecButtons[i].AsNarrowStdString().c_str();
        sdlButtons.push_back(btn);
    }

    SDL_MessageBoxData msgboxData = {
            SDL_MESSAGEBOX_INFORMATION,
            nullptr,
            caption.AsNarrowStdString().c_str(),
            text.AsStdString().c_str(),
            static_cast<int>(vecButtons.size()),
            vecButtons.empty() ? nullptr : sdlButtons.data(),
            nullptr
    };

    if (vecButtons.empty()) {
        SDL_MessageBoxButtonData defaultButton = {
                SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,
                0,
                "确定"
        };
        msgboxData.buttons = &defaultButton;
        msgboxData.numbuttons = 1;
    }

    int buttonid = -1;
    if (SDL_ShowMessageBox(&msgboxData, &buttonid) < 0) {
        SDL_Log("SDL_ShowMessageBox failed: %s",  SDL_GetError());
        return -1;
    }
    return buttonid;
}

int TVPShowSimpleMessageBox(const char* pszText, const char* pszTitle, unsigned int nButton, const char** btnText) {
	std::vector<ttstr> vecButtons;
    for (unsigned int i = 0; i < nButton; ++i) {
        vecButtons.emplace_back(btnText[i]);
    }
    return TVPShowSimpleMessageBox(pszText, pszTitle, vecButtons);
}

std::vector<std::string> TVPGetDriverPath() {
	return { "/" };
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
    if(folder.IsEmpty())
        return true;

    if(TVPCheckExistentLocalFolder(folder))
        return true; // already created

    const tjs_char *p = folder.c_str();
    tjs_int i = folder.GetLen() - 1;

    if(p[i] == TJS_W(':'))
        return true;

    while(i >= 0 && (p[i] == TJS_W('/') || p[i] == TJS_W('\\')))
        i--;

    if(i >= 0 && p[i] == TJS_W(':'))
        return true;

    for(; i >= 0; i--) {
        if(p[i] == TJS_W(':') || p[i] == TJS_W('/') || p[i] == TJS_W('\\'))
            break;
    }

    ttstr parent(p, i + 1);
    if(!TVPCreateFolders(parent))
        return false;

    return !std::filesystem::create_directory(folder.AsStdString().c_str());

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
	FILE *handle = fopen(filepath.AsStdString().c_str(), "wb");
    if(handle) {
        bool ret = fwrite(data, 1, len, handle) == len;
        fclose(handle);
        return ret;
    }
    return false;
}

std::string TVPGetCurrentLanguage() {
	const char *lang_env = std::getenv("LANG");
    if(!lang_env) {
        lang_env = std::getenv("LC_ALL");
        if(!lang_env) {
            lang_env = std::getenv("LC_MESSAGES");
            if(!lang_env) {
                return "en_US";
            }
        }
    }

    std::string locale(lang_env);
    size_t dot_pos = locale.find('.');
    if(dot_pos != std::string::npos) {
        locale = locale.substr(0, dot_pos);
    }

    size_t underscore_pos = locale.find('_');
    if(underscore_pos != std::string::npos) {
        std::string language = locale.substr(0, underscore_pos);
        std::string country = locale.substr(underscore_pos + 1);

        for(char &c : country) {
            if(c >= 'A' && c <= 'Z') {
                c += 'a' - 'A';
            }
        }
        return language + "_" + country;
    }

    return locale;
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

void TVPProcessInputEvents() {}
void TVPShowIME(int x, int y, int w, int h) {}
void TVPHideIME() {}

void TVPRelinquishCPU() { sched_yield(); }

tjs_uint32 TVPGetRoughTickCount32()
{
	tjs_uint32 uptime = 0;
    struct timespec on;
    if(clock_gettime(CLOCK_MONOTONIC, &on) == 0)
        uptime = on.tv_sec * 1000 + on.tv_nsec / 1000000;
    return uptime;
}

void TVPPrintLog(const char* str) {
	printf("%s", str);
}

bool TVP_stat(const tjs_char* name, tTVP_stat& s) {
	tTJSNarrowStringHolder holder(name);
	return TVP_stat(holder, s);
}

bool TVP_stat(const char* name, tTVP_stat& s) {
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

bool TVP_utime(const char* name, time_t modtime) {
	timeval mt[2];
    mt[0].tv_sec = modtime;
    mt[0].tv_usec = 0;
    mt[1].tv_sec = modtime;
    mt[1].tv_usec = 0;
    return utimes(name, mt) == 0;
}

void TVPSendToOtherApp(const std::string& filename) {

}

void TVPListDir(const std::string& folder, std::function<void(const std::string&, int)> cb) {
	DIR* dirp;
	struct dirent* direntp;
	tTVP_stat stat_buf;
	if ((dirp = opendir(folder.c_str())))
	{
		while ((direntp = readdir(dirp)) != NULL)
		{
			std::string fullpath = folder + "/" + direntp->d_name;
			if (!TVP_stat(fullpath.c_str(), stat_buf))
				continue;
			cb(direntp->d_name, stat_buf.tvp_mode);
		}
		closedir(dirp);
	}
}
void TVPGetLocalFileListAt(const ttstr& name, const std::function<void(const ttstr&, tTVPLocalFileInfo*)>& cb) {
	DIR* dirp;
	struct dirent* direntp;
	tTVP_stat stat_buf;
	std::string folder(name.AsNarrowStdString());
	if ((dirp = opendir(folder.c_str())))
	{
		while ((direntp = readdir(dirp)) != NULL)
		{
			std::string fullpath = folder + "/" + direntp->d_name;
			if (!TVP_stat(fullpath.c_str(), stat_buf))
				continue;
			ttstr file(direntp->d_name);
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
			info.NativeName = direntp->d_name;
			info.Mode = stat_buf.tvp_mode;
			info.Size = stat_buf.tvp_size;
			info.AccessTime = stat_buf.tvp_atime;
			info.ModifyTime = stat_buf.tvp_mtime;
			info.CreationTime = stat_buf.tvp_ctime;
			cb(file, &info);
		}
		closedir(dirp);
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
	// if the name is an OS's native expression, change it according
    // with the TVP storage system naming rule.
    tjs_int namelen = name.GetLen();
    if(namelen == 0)
        return;
    if(namelen >= 1) {
        if(name[0] == TJS_W('/')) {
            name = ttstr(TJS_W("file://.")) + name;
            return;
        }
    }
}
