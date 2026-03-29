#include "tjsCommHead.h"

#include <algorithm>
#include <string>
#include <vector>
#include <filesystem>
#include <assert.h>
#include <SDL2/SDL.h>
#ifdef _KRKRSDL3_OHOS
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#endif

#include "tjsError.h"
#include "tjsDebug.h"

#include "TVPApplication.h"
#include "TVPSystem.h"
#include "TVPDebug.h"
#include "TVPMsg.h"
#include "TVPScript.h"
#include "TVPPlugin.h"

#include "TVPConfig.h"
#include "Exception.h"
#include "SystemControl.h"
#include "WaveIntf.h"
#include "GraphicsLoadThread.h"
#include "Platform.h"
#include "TVPEvent.h"
#include <thread>

#include "TVPStorage.h"
extern "C"
{
#include <libavutil/avstring.h>
}
#include "TVPColor.h"
#include "TVPFont.h"
#include "TVPTimer.h"

tTVPApplication* Application = new tTVPApplication;
std::thread::id TVPMainThreadID;
static tTJSCriticalSection _NoMemCallBackCS;
static void* _reservedMem = malloc(1024 * 1024 * 4); // 4M reserved mem
static bool _project_startup = false;
tTJS* TVPAppScriptEngine;

//---------------------------------------------------------------------------
// TVPResetApplicationForReentry - reset application state for OHOS re-entry
//---------------------------------------------------------------------------
#ifdef _KRKRSDL3_OHOS
void TVPResetApplicationForReentry()
{
    _project_startup = false;
    TVPAppScriptEngine = nullptr;
    if (!_reservedMem)
        _reservedMem = malloc(1024 * 1024 * 4);
    if (!Application)
        Application = new tTVPApplication;
}
#endif
//---------------------------------------------------------------------------

static void _do_compact()
{
    TVPDeliverCompactEvent(TVP_COMPACT_LEVEL_MAX);
}

static void _no_memory_cb()
{
    tTJSCSH lock(_NoMemCallBackCS);
    free(_reservedMem);
    if (TVPMainThreadID == std::this_thread::get_id())
    {
        _do_compact();
    }
    else
    {
        Application->PostUserMessage(_do_compact);
    }
    _reservedMem = realloc(0, 1024 * 1024 * 4);
}

static std::string _title, _msg, _retry, _cancel;
static tTJSCriticalSection _cs;
typedef void* F_alloc_t(void*, size_t);
static void* __do_alloc_func(F_alloc_t* f, void* p, size_t c)
{
    void* ptr = f(p, c);

    if (!ptr)
    {
        _no_memory_cb();
        ptr = f(p, c);
        if (!ptr)
        {
            tTJSCSH lock(_cs);
            const char* btns[2] = {_retry.c_str(), _cancel.c_str()};
            while (!ptr && TVPShowSimpleMessageBox(_msg.c_str(), _title.c_str(), 2, btns) == 0)
            {
                ptr = f(p, c);
            }
            // TVPExitApplication(-1);
        }
    }
    return ptr;
}

ttstr TVPGetErrorDialogTitle()
{
    const ttstr& title = Application->GetTitle();
    if (title.IsEmpty())
    {
        return TVPGetPackageVersionString() + " Error";
    }
    else
    {
        return ttstr(TVPGetPackageVersionString()) + " " + title;
    }
}

bool IsInMainThread()
{
    return std::this_thread::get_id() == TVPMainThreadID;
}

ttstr ExePath()
{
    return TVPNativeProjectDir;
}

bool TVPCheckAbout();
bool TVPCheckPrintDataPath();
void TVPOnError();
void TVPLockSoundMixer();
void TVPUnlockSoundMixer();

static bool _warnLowMem = true;
#ifndef _KRKRSDL3_OHOS
void TVPCheckMemory()
{
#if defined(_DEBUG)
    if (_warnLowMem)
    {
        tjs_int freeMem = TVPGetSystemFreeMemory();
        if (freeMem < 24)
        {
            char buf[256];
            sprintf(buf,
                    "Insufficient memory (%dMB available)\nYou can diable this notice in global "
                    "preference.",
                    freeMem);
            const char* btn = "OK";
            TVPShowSimpleMessageBox(buf, "No Memory Warning", 1, &btn);
            _warnLowMem = false;
        }
    }
#endif
}

int TVPShowSimpleMessageBox(const ttstr& text, const ttstr& caption)
{
    std::vector<ttstr> normal;
    normal.emplace_back("OK");
    return TVPShowSimpleMessageBox(text, caption, normal);
}

int TVPShowSimpleMessageBoxYesNo(const ttstr& text, const ttstr& caption)
{
    std::vector<ttstr> normal;
    normal.emplace_back("Yes");
    normal.emplace_back("No");
    return TVPShowSimpleMessageBox(text, caption, normal);
}
#endif // !_KRKRSDL3_OHOS

tTVPApplication::tTVPApplication()
  : is_attach_console_(false),
    tarminate_(false),
    application_activating_(true),
    image_load_thread_(NULL),
    has_map_report_process_(false)
{
}
tTVPApplication::~tTVPApplication()
{
    delete image_load_thread_;
}

extern void TVPLoadPluigins(void);
bool tTVPApplication::StartApplication(int argc, char* argv[])
{
    SetCommandlineArguments(argc, argv);

    TVPTerminateCode = 0;
    _retry = "retry";
    _cancel = "cancel";
    _msg = "内存不足";
    _title = "发生异常";

#ifdef _KRKRSDL3_LIB
    TVPNativeProjectDir = std::string(argv[1]);
#else
    size_t lastSlash = std::string(argv[0]).find_last_of("/\\");
    if (lastSlash != std::string::npos)
    {
        TVPNativeProjectDir = std::string(argv[0]).substr(0, lastSlash + 1) + "Res";
    }
#endif

    CheckConsole();

    // try starting the program!
    try
    {

        // If argv[1] is a directory, auto-detect data.xp3 (or first *.xp3) inside it
        {
            std::string arg1 = argv[1];
            // normalize separators
            for (auto& c : arg1) if (c == '\\') c = '/';
            // strip trailing slash
            while (!arg1.empty() && arg1.back() == '/') arg1.pop_back();
            // check if it's a directory
            bool isDir = false;
#ifdef _KRKRSDL3_OHOS
            {
                struct stat st;
                int rc = stat(arg1.c_str(), &st);
                SDL_Log("[krkr2-diag] stat('%s') rc=%d mode=0%o isdir=%d",
                        arg1.c_str(), rc, rc == 0 ? st.st_mode : 0,
                        rc == 0 ? S_ISDIR(st.st_mode) : -1);
                if (rc == 0 && S_ISDIR(st.st_mode))
                    isDir = true;
            }
#else
            {
                std::error_code ec;
                isDir = std::filesystem::is_directory(
                    std::filesystem::u8path(arg1), ec);
            }
#endif
            SDL_Log("[krkr2-diag] arg1='%s' isDir=%d", arg1.c_str(), isDir);
            if (isDir) {
                // look for data.xp3 first, then any *.xp3
                std::string found;
                std::string candidate = arg1 + "/data.xp3";
#ifdef _KRKRSDL3_OHOS
                {
                    struct stat st;
                    int rc = stat(candidate.c_str(), &st);
                    SDL_Log("[krkr2-diag] stat('%s') rc=%d errno=%d isreg=%d",
                            candidate.c_str(), rc, rc != 0 ? errno : 0,
                            rc == 0 ? S_ISREG(st.st_mode) : -1);
                    if (rc == 0 && S_ISREG(st.st_mode))
                        found = candidate;
                }
                if (found.empty()) {
                    DIR* dir = opendir(arg1.c_str());
                    SDL_Log("[krkr2-diag] opendir('%s') = %p", arg1.c_str(), (void*)dir);
                    if (dir) {
                        struct dirent* ent;
                        int count = 0;
                        while ((ent = readdir(dir)) != nullptr) {
                            std::string fn(ent->d_name);
                            SDL_Log("[krkr2-diag]   entry[%d]: '%s'", count, fn.c_str());
                            if (fn.size() > 4 && fn.substr(fn.size() - 4) == ".xp3") {
                                found = arg1 + "/" + fn;
                                break;
                            }
                            count++;
                        }
                        closedir(dir);
                    }
                }
#else
                {
                    std::error_code ec;
                    if (std::filesystem::is_regular_file(
                            std::filesystem::u8path(candidate), ec))
                        found = candidate;
                }
                if (found.empty()) {
                    std::error_code ec;
                    for (auto& entry : std::filesystem::directory_iterator(
                             std::filesystem::u8path(arg1), ec)) {
                        if (entry.path().extension() == ".xp3") {
                            found = entry.path().u8string();
                            break;
                        }
                    }
                }
#endif
                SDL_Log("[krkr2-diag] XP3 found='%s'", found.c_str());
                if (!found.empty())
                    TVPProjectDir = TVPNormalizeStorageName(found.c_str());
                else
                    TVPProjectDir = TVPNormalizeStorageName(argv[1]);
            } else {
                TVPProjectDir = TVPNormalizeStorageName(argv[1]);
            }
            SDL_Log("[krkr2-diag] TVPProjectDir='%s'", TVPProjectDir.AsStdString().c_str());
        }

#ifdef _KRKRSDL3_OHOS
        // [OHOS] Early auto-path registration.
        // TVPBeforeSystemInit (which appends '>' to TVPProjectDir) runs inside
        // TVPSystemInit, far AFTER TVPInitFontNames / TVPInitializeBaseSystems.
        // Those earlier functions may trigger TVPRebuildAutoPathTable while
        // TVPAutoPathList is still empty, giving "Total 0 file(s) found".
        //
        // IMPORTANT: Do NOT modify TVPProjectDir here — TVPGetAppPath() uses a
        // static cache of TVPExtractStoragePath(TVPProjectDir) on first call.
        // If TVPProjectDir already has '>', the extracted path includes '>' and
        // GetLocalName fails ("Cannot get local name from ...xp3>").
        //
        // Instead, only register the auto-path entries so the archive contents
        // become discoverable.  TVPBeforeSystemInit will handle TVPProjectDir.
        try {
            SDL_Log("[krkr2-diag] Early auto-path registration starting");
            bool isArchive = TVPIsExistentStorageNoSearchNoNormalize(TVPProjectDir);
            SDL_Log("[krkr2-diag] TVPIsExistentStorageNoSearchNoNormalize=%d", isArchive);
            if (isArchive)
            {
                // Archive file (e.g. data.xp3) — register it with '>' delimiter
                ttstr arcAutoPath = TVPProjectDir + TVPArchiveDelimiter;
                TVPAddAutoPath(arcAutoPath);
                // Register all top-level subdirectories inside the archive
                // (e.g. data.xp3>main/, data.xp3>system/) so that files like
                // Config.tjs (stored as main/Config.tjs) are findable by name.
                TVPAddArchiveSubDirAutoPath(TVPNormalizeStorageName(arcAutoPath));
                SDL_Log("[krkr2-diag] Registered archive auto-path: '%s'",
                        arcAutoPath.AsStdString().c_str());
                // Also register the parent directory so files sitting next to
                // the archive (e.g. Config.tjs, patch.xp3) are accessible
                ttstr parentDir = TVPExtractStoragePath(TVPProjectDir);
                if (!parentDir.IsEmpty()) {
                    TVPAddAutoPath(parentDir);
                    SDL_Log("[krkr2-diag] Registered parent dir auto-path: '%s'",
                            parentDir.AsStdString().c_str());
                    // Scan the parent directory for other XP3 archives
                    // (e.g. when patch.xp3 is passed but data.xp3 has startup.tjs)
                    static const char* xp3Names[] = {
                        "data.xp3", "run.xp3", "plugin.xp3",
                        "patch.xp3", "patch2.xp3", "patch3.xp3",
                        "patch4.xp3", "patch5.xp3", nullptr
                    };
                    for (int i = 0; xp3Names[i]; ++i) {
                        ttstr siblingArc = parentDir + xp3Names[i];
                        // Skip the archive we already registered
                        if (siblingArc == TVPProjectDir) continue;
                        if (TVPIsExistentStorageNoSearchNoNormalize(siblingArc)) {
                            ttstr siblingAutoPath = siblingArc + TVPArchiveDelimiter;
                            TVPAddAutoPath(siblingAutoPath);
                            TVPAddArchiveSubDirAutoPath(TVPNormalizeStorageName(siblingAutoPath));
                            SDL_Log("[krkr2-diag] Auto-discovered sibling XP3: '%s'",
                                    siblingAutoPath.AsStdString().c_str());
                        }
                    }
                }
            }
            else
            {
                // Directory — register it, and auto-discover XP3 archives inside
                ttstr dirPath = TVPProjectDir;
                if (dirPath.GetLastChar() != TJS_N('/'))
                    dirPath += TJS_N("/");
                TVPAddAutoPath(dirPath);
                static const char* xp3Names[] = {
                    "data.xp3", "run.xp3", "plugin.xp3",
                    "patch.xp3", "patch2.xp3", "patch3.xp3", nullptr
                };
                for (int i = 0; xp3Names[i]; ++i) {
                    ttstr arcPath = dirPath + xp3Names[i];
                    if (TVPIsExistentStorageNoSearchNoNormalize(arcPath)) {
                        ttstr arcAutoPath = arcPath + TVPArchiveDelimiter;
                        TVPAddAutoPath(arcAutoPath);
                        SDL_Log("[krkr2-diag] Auto-discovered XP3: '%s'",
                                arcAutoPath.AsStdString().c_str());
                    }
                }
                SDL_Log("[krkr2-diag] Registered dir auto-path: '%s'",
                        dirPath.AsStdString().c_str());
            }
        } catch (const std::exception& e) {
            SDL_Log("[krkr2-diag] Early auto-path exception: %s", e.what());
        } catch (...) {
            SDL_Log("[krkr2-diag] Early auto-path unknown exception");
        }
#endif

#ifdef _KRKRSDL3_OHOS
        SDL_Log("[krkr2-diag] >>> Step 1: TVPInitScriptEngine");
#endif
        TVPInitScriptEngine();
#ifdef _KRKRSDL3_OHOS
        SDL_Log("[krkr2-diag] >>> Step 2: TVPInitFontNames");
#endif
        TVPInitFontNames();

        // banner
        TVPAddImportantLog(
            TVPFormatMessage(TVPProgramStartedOn, TVPGetOSName(), TVPGetPlatformName()));

        // TVPInitializeBaseSystems
#ifdef _KRKRSDL3_OHOS
        SDL_Log("[krkr2-diag] >>> Step 3: TVPInitializeBaseSystems");
#endif
        TVPInitializeBaseSystems();

#ifdef _KRKRSDL3_OHOS
        SDL_Log("[krkr2-diag] >>> Step 4: Initialize");
#endif
        Initialize();

        if (TVPCheckPrintDataPath())
            return true;
        if (TVPExecuteUserConfig())
            return true;

        image_load_thread_ = new tTVPAsyncImageLoader();

#ifdef _KRKRSDL3_OHOS
        SDL_Log("[krkr2-diag] >>> Step 5: TVPLoadPluigins");
#endif
        TVPLoadPluigins(); // load plugin module *.tpm
#ifdef _KRKRSDL3_OHOS
        SDL_Log("[krkr2-diag] >>> Step 6: TVPSystemInit");
#endif
        TVPSystemInit();

        if (TVPCheckAbout())
            return true; // version information dialog box;

        SetTitle(TVPKirikiri.operator const tjs_char*());
        TVPSystemControl = new tTVPSystemControl();
        // Check digitizer
        CheckDigitizer();

        // start image load thread
        image_load_thread_->Resume();

#ifdef _KRKRSDL3_OHOS
        SDL_Log("[krkr2-diag] >>> Step 7: TVPInitializeStartupScript");
#endif
        TVPInitializeStartupScript();
        _project_startup = true;
    }
    catch (const EAbort&)
    {
        // nothing to do
    }
#ifdef _KRKRSDL3_OHOS
    catch (const eTJS& e)
    {
        SDL_Log("[krkr2-diag] eTJS exception: %s", e.GetMessage().AsStdString().c_str());
        throw; // re-throw for runner_main to handle
    }
    catch (const Exception& e)
    {
        SDL_Log("[krkr2-diag] Exception: %s", e.what().AsStdString().c_str());
        throw;
    }
#endif

    return true;
}
/**
 * コンソールからの起動か確認し、コンソールからの起動の場合は、標準出力を割り当てる
 */
void tTVPApplication::CheckConsole()
{
#ifdef TVP_LOG_TO_COMMANDLINE_CONSOLE
    if (has_map_report_process_)
        return; // 書き出し用子プロセスして起動されていた時はコンソール接続しない
    HANDLE hin = ::GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hout = ::GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE herr = ::GetStdHandle(STD_ERROR_HANDLE);

    DWORD curProcId = ::GetCurrentProcessId();
    DWORD processList[256];
    DWORD count = ::GetConsoleProcessList(processList, 256);
    bool thisProcHasConsole = false;
    for (DWORD i = 0; i < count; i++)
    {
        if (processList[i] == curProcId)
        {
            thisProcHasConsole = true;
            break;
        }
    }
    bool attachedConsole = true;
    if (thisProcHasConsole == false)
    {
        attachedConsole = ::AttachConsole(ATTACH_PARENT_PROCESS) != 0;
    }

    if ((hin == 0 || hout == 0 || herr == 0) && attachedConsole)
    {
        wchar_t console[256];
        ::GetConsoleTitle(console, 256);
        console_title_ = std::wstring(console);
        // 元のハンドルを再割り当て
        if (hin)
            ::SetStdHandle(STD_INPUT_HANDLE, hin);
        if (hout)
            ::SetStdHandle(STD_OUTPUT_HANDLE, hout);
        if (herr)
            ::SetStdHandle(STD_ERROR_HANDLE, herr);
    }
    is_attach_console_ = attachedConsole;
#endif
}

void tTVPApplication::CloseConsole()
{
}
void TVPConsoleLog(const ttstr& mes, bool important);
void tTVPApplication::PrintConsole(const ttstr& mes, bool important)
{
    TVPConsoleLog(mes, important);
}

void tTVPApplication::ShowException(const ttstr& e)
{
    TVPShowSimpleMessageBox(e, TVPGetErrorDialogTitle());
    TVPSystemUninit();
    TVPExitApplication(0);
}
void tTVPApplication::Run()
{
    try
    {
        if (TVPTerminated)
        {
            TVPSystemUninit();
            TVPExitApplication(TVPTerminateCode);
        }
        ProcessMessages();
        if (TVPSystemControl)
            TVPSystemControl->SystemWatchTimerTimer();
    }
    catch (const EAbort&)
    {
        // nothing to do
    }
}

void tTVPApplication::ProcessMessages()
{
    std::vector<std::tuple<void*, int, tMsg>> lstUserMsg;
    {
        std::lock_guard<std::mutex> cs(m_msgQueueLock);
        m_lstUserMsg.swap(lstUserMsg);
    }
    for (std::tuple<void*, int, tMsg>& it : lstUserMsg)
    {
        std::get<2>(it)();
    }
    TVPTimer::ProgressAllTimer();
}

void tTVPApplication::SetTitle(const ttstr& caption)
{
    title_ = caption;
}

void tTVPApplication::Terminate()
{
    tarminate_ = true;
    TVPTerminated = true;
}

void tTVPApplication::CheckDigitizer()
{
}

void tTVPApplication::PostUserMessage(const std::function<void()>& func, void* host, int msg)
{
    std::lock_guard<std::mutex> cs(m_msgQueueLock);
    m_lstUserMsg.emplace_back(host, msg, func);
}

void tTVPApplication::FilterUserMessage(
    const std::function<void(std::vector<std::tuple<void*, int, tMsg>>&)>& func)
{
    std::lock_guard<std::mutex> cs(m_msgQueueLock);
    func(m_lstUserMsg);
}

void tTVPApplication::OnActivate()
{
    application_activating_ = true;
    if (!_project_startup)
        return;

    //	TVPRestoreFullScreenWindowAtActivation();
    TVPResetVolumeToAllSoundBuffer();
    TVPUnlockSoundMixer();

    // trigger System.onActivate event
    TVPPostApplicationActivateEvent();
    for (auto& it : m_activeEvents)
    {
        it.second(it.first, eTVPActiveEvent::onActive);
    }
}
void tTVPApplication::OnDeactivate()
{
    application_activating_ = false;
    if (!_project_startup)
        return;

    //	TVPMinimizeFullScreenWindowAtInactivation();

    // fire compact event
    TVPDeliverCompactEvent(TVP_COMPACT_LEVEL_DEACTIVATE);

    // set sound volume
    TVPResetVolumeToAllSoundBuffer();
    TVPLockSoundMixer();

    // trigger System.onDeactivate event
    TVPPostApplicationDeactivateEvent();
    for (auto& it : m_activeEvents)
    {
        it.second(it.first, eTVPActiveEvent::onDeactive);
    }
}

void tTVPApplication::OnExit()
{
    TVPUninitScriptEngine();

    if (TVPSystemControl)
        delete TVPSystemControl;
    TVPSystemControl = NULL;

    CloseConsole();
}

void tTVPApplication::OnLowMemory()
{
    if (!_project_startup)
        return;
    TVPDeliverCompactEvent(TVP_COMPACT_LEVEL_MAX);
}

bool tTVPApplication::GetNotMinimizing() const
{
    return !application_activating_;
}

void tTVPApplication::LoadImageRequest(class iTJSDispatch2* owner,
                                       class tTJSNI_Bitmap* bmp,
                                       const ttstr& name)
{
    if (image_load_thread_)
    {
        image_load_thread_->LoadRequest(owner, bmp, name);
    }
}

void tTVPApplication::RegisterActiveEvent(
    void* host, const std::function<void(void*, eTVPActiveEvent)>& func /*empty = unregister*/)
{
    if (func)
        m_activeEvents.emplace(host, func);
    else
        m_activeEvents.erase(host);
}

void TVPInitWindowOptions()
{
}
