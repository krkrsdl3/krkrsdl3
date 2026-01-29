#include "tjsCommHead.h"

#include <algorithm>
#include <string>
#include <vector>
#include <assert.h>

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
extern "C" {
#include <libavutil/avstring.h>
}
#include "TVPColor.h"
#include "TVPFont.h"
#include "TVPTimer.h"

tTVPApplication* Application = new tTVPApplication;
std::thread::id TVPMainThreadID;
static tTJSCriticalSection _NoMemCallBackCS;
static void *_reservedMem = malloc(1024 * 1024 * 4); // 4M reserved mem
static bool _project_startup = false;
tTJS *TVPAppScriptEngine;

static void _do_compact() {
	TVPDeliverCompactEvent(TVP_COMPACT_LEVEL_MAX);
}

static void _no_memory_cb() {
	tTJSCSH lock(_NoMemCallBackCS);
	free(_reservedMem);
	if (TVPMainThreadID == std::this_thread::get_id()) {
		_do_compact();
	} else {
		Application->PostUserMessage(_do_compact);
	}
	_reservedMem = realloc(0, 1024 * 1024 * 4);
}

static std::string _title, _msg, _retry, _cancel;
static tTJSCriticalSection _cs;
typedef void* F_alloc_t(void*, size_t);
static void* __do_alloc_func(F_alloc_t *f, void *p, size_t c) {
	void *ptr = f(p, c);

	if (!ptr) {
		_no_memory_cb();
		ptr = f(p, c);
		if (!ptr) {
			tTJSCSH lock(_cs);
			const char *btns[2] = { _retry.c_str(), _cancel.c_str() };
			while (!ptr && TVPShowSimpleMessageBox(_msg.c_str(), _title.c_str(), 2, btns) == 0) {
				ptr = f(p, c);
			}
			//TVPExitApplication(-1);
		}
	}
	return ptr;
}

ttstr TVPGetErrorDialogTitle() {
	const ttstr &title = Application->GetTitle();
	if (title.IsEmpty()) {
		return TVPGetPackageVersionString() + " Error";
	} else {
		return ttstr(TVPGetPackageVersionString()) + " " + title;
	}
}

bool IsInMainThread()
{ return std::this_thread::get_id() == TVPMainThreadID; }

ttstr ExePath() {
	return TVPNativeProjectDir;
}

bool TVPCheckAbout();
bool TVPCheckPrintDataPath();
void TVPOnError();
void TVPLockSoundMixer();
void TVPUnlockSoundMixer();

static bool _warnLowMem = true;
void TVPCheckMemory() {
#if defined(_DEBUG)
	if (_warnLowMem) {
		tjs_int freeMem = TVPGetSystemFreeMemory();
		if (freeMem < 24) {
			char buf[256];
			sprintf(buf, "Insufficient memory (%dMB available)\nYou can diable this notice in global preference.", freeMem);
			const char *btn = "OK";
			TVPShowSimpleMessageBox(buf, "No Memory Warning", 1, &btn);
			_warnLowMem = false;
		}
	}
#endif
}

int TVPShowSimpleMessageBox(const ttstr & text, const ttstr & caption) {
	std::vector<ttstr> normal; normal.emplace_back(GameSetting::msgbox_ok);
	return TVPShowSimpleMessageBox(text, caption, normal);
}

int TVPShowSimpleMessageBoxYesNo(const ttstr & text, const ttstr & caption) {
	std::vector<ttstr> normal;
	normal.emplace_back(GameSetting::msgbox_yes);
	normal.emplace_back(GameSetting::msgbox_no);
	return TVPShowSimpleMessageBox(text, caption, normal);
}

ttstr TVPGetMessageByLocale(const std::string &key) {
	return GameSetting::unkown;
}

tTVPApplication::tTVPApplication() : is_attach_console_(false), tarminate_(false), application_activating_(true)
	 , image_load_thread_(NULL), has_map_report_process_(false)
{
}
tTVPApplication::~tTVPApplication() {
	delete image_load_thread_;
}

extern void TVPLoadPluigins(void);
bool tTVPApplication::StartApplication(int argc, char* argv[])
{
    SetCommandlineArguments(argc, argv);

    TVPTerminateCode = 0;
    _retry = GameSetting::retry;
    _cancel = GameSetting::cancel;
    _msg = GameSetting::err_no_memory;
    _title = GameSetting::err_occured;

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
    try {

            TVPProjectDir = TVPNormalizeStorageName(argv[1]);

            TVPInitScriptEngine();
            TVPInitFontNames();

            // banner
            TVPAddImportantLog( TVPFormatMessage(TVPProgramStartedOn, TVPGetOSName(), TVPGetPlatformName()) );

            // TVPInitializeBaseSystems
            TVPInitializeBaseSystems();

            Initialize();

            if(TVPCheckPrintDataPath()) return true;
            if(TVPExecuteUserConfig()) return true;

            image_load_thread_ = new tTVPAsyncImageLoader();

            TVPLoadPluigins(); // load plugin module *.tpm
            TVPSystemInit();

            if(TVPCheckAbout()) return true; // version information dialog box;

            SetTitle(TVPKirikiri.operator const tjs_char *());
            TVPSystemControl = new tTVPSystemControl();
            // Check digitizer
            CheckDigitizer();

            // start image load thread
            image_load_thread_->Resume();

            TVPInitializeStartupScript();
            _project_startup = true;
    } catch( const EAbort & ) {
        // nothing to do
    }

    return true;
}
/**
 * コンソールからの起動か確認し、コンソールからの起動の場合は、標準出力を割り当てる
 */
void tTVPApplication::CheckConsole() {
#ifdef TVP_LOG_TO_COMMANDLINE_CONSOLE
	if( has_map_report_process_ ) return; // 書き出し用子プロセスして起動されていた時はコンソール接続しない
	HANDLE hin  = ::GetStdHandle(STD_INPUT_HANDLE);
	HANDLE hout = ::GetStdHandle(STD_OUTPUT_HANDLE);
	HANDLE herr = ::GetStdHandle(STD_ERROR_HANDLE);

	DWORD curProcId = ::GetCurrentProcessId();
	DWORD processList[256];
	DWORD count = ::GetConsoleProcessList( processList, 256 );
	bool thisProcHasConsole = false;
	for( DWORD i = 0; i < count; i++ ) {
		if( processList[i] == curProcId ) {
			thisProcHasConsole = true;
			break;
		}
	}
	bool attachedConsole = true;
	if( thisProcHasConsole == false ) {
		attachedConsole = ::AttachConsole(ATTACH_PARENT_PROCESS) != 0;
	}

	if( (hin==0||hout==0||herr==0) && attachedConsole ) {
		wchar_t console[256];
		::GetConsoleTitle( console, 256 );
		console_title_ = std::wstring( console );
		// 元のハンドルを再割り当て
		if (hin)  ::SetStdHandle(STD_INPUT_HANDLE, hin);
		if (hout) ::SetStdHandle(STD_OUTPUT_HANDLE, hout);
		if (herr) ::SetStdHandle(STD_ERROR_HANDLE, herr);
	}
	is_attach_console_ = attachedConsole;
#endif
}

void tTVPApplication::CloseConsole() {
}
void TVPConsoleLog(const ttstr &mes, bool important);
void tTVPApplication::PrintConsole(const ttstr &mes, bool important) {
	TVPConsoleLog(mes, important);
}

void tTVPApplication::ShowException(const ttstr& e) {
	TVPShowSimpleMessageBox(e, TVPGetErrorDialogTitle());
	TVPSystemUninit();
	TVPExitApplication(0);
}
void tTVPApplication::Run() {
	try {
		if (TVPTerminated) {
			TVPSystemUninit();
			TVPExitApplication(TVPTerminateCode);
		}
		ProcessMessages();
		if (TVPSystemControl) TVPSystemControl->SystemWatchTimerTimer();
	} catch (const EAbort &) {
		// nothing to do
	}
}

void tTVPApplication::ProcessMessages()
{
	std::vector<std::tuple<void*, int, tMsg> > lstUserMsg;
	{
		std::lock_guard<std::mutex> cs(m_msgQueueLock);
		m_lstUserMsg.swap(lstUserMsg);
	}
	for (std::tuple<void*, int, tMsg>& it : lstUserMsg) {
		std::get<2>(it)();
	}
	TVPTimer::ProgressAllTimer();
}

void tTVPApplication::SetTitle(const ttstr& caption) {
	title_ = caption;
}

void tTVPApplication::Terminate()
{
	tarminate_ = true;
	TVPTerminated = true;
}

void tTVPApplication::CheckDigitizer() {
	
}

void tTVPApplication::PostUserMessage(const std::function<void()> &func, void* host, int msg)
{
	std::lock_guard<std::mutex> cs(m_msgQueueLock);
	m_lstUserMsg.emplace_back(host, msg, func);
}

void tTVPApplication::FilterUserMessage(const std::function<void(std::vector<std::tuple<void*, int, tMsg> > &)> &func)
{
	std::lock_guard<std::mutex> cs(m_msgQueueLock);
	func(m_lstUserMsg);
}

void tTVPApplication::OnActivate()
{
	application_activating_ = true;
	if (!_project_startup) return;

//	TVPRestoreFullScreenWindowAtActivation();
	TVPResetVolumeToAllSoundBuffer();
	TVPUnlockSoundMixer();

	// trigger System.onActivate event
	TVPPostApplicationActivateEvent();
	for (auto & it : m_activeEvents) {
		it.second(it.first, eTVPActiveEvent::onActive);
	}
}
void tTVPApplication::OnDeactivate(  )
{
	application_activating_ = false;
	if (!_project_startup) return;

//	TVPMinimizeFullScreenWindowAtInactivation();
	
	// fire compact event
	TVPDeliverCompactEvent(TVP_COMPACT_LEVEL_DEACTIVATE);

	// set sound volume
	TVPResetVolumeToAllSoundBuffer();
	TVPLockSoundMixer();

	// trigger System.onDeactivate event
	TVPPostApplicationDeactivateEvent();
	for (auto & it : m_activeEvents) {
		it.second(it.first, eTVPActiveEvent::onDeactive);
	}
}

void tTVPApplication::OnExit()
{
	TVPUninitScriptEngine();

	if (TVPSystemControl) delete TVPSystemControl;
	TVPSystemControl = NULL;

	CloseConsole();
}

void tTVPApplication::OnLowMemory()
{
	if (!_project_startup) return;
	TVPDeliverCompactEvent(TVP_COMPACT_LEVEL_MAX);
}

bool tTVPApplication::GetNotMinimizing() const
{
	return !application_activating_;
}

void tTVPApplication::LoadImageRequest( class iTJSDispatch2 *owner, class tTJSNI_Bitmap* bmp, const ttstr &name ) {
	if( image_load_thread_ ) {
		image_load_thread_->LoadRequest( owner, bmp, name );
	}
}

void tTVPApplication::RegisterActiveEvent(void *host, const std::function<void(void*, eTVPActiveEvent)>& func/*empty = unregister*/)
{
	if (func)
		m_activeEvents.emplace(host, func);
	else
		m_activeEvents.erase(host);
}

void TVPInitWindowOptions() {
	
}
