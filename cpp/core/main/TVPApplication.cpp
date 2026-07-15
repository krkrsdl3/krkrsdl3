#include "tjsCommHead.h"

#include <algorithm>
#include <string>
#include <vector>

#include "tjsError.h"
#include "tjsDebug.h"

#include "TVPApplication.h"
#include "TVPSystem.h"
#include "TVPDebug.h"
#include "TVPMsg.h"
#include "TVPScript.h"
#include "TVPPlugin.h"

#include "Exception.h"
#include "SystemControl.h"
#include "WaveIntf.h"
#include "GraphicsLoadThread.h"
#include "Platform.h"
#include "PlatformView.h"
#include "TVPEvent.h"

#include "TVPStorage.h"
#include "TVPColor.h"
#include "TVPFont.h"
#include "TVPTimer.h"

tTVPApplication* Application = new tTVPApplication;
static tTJSCriticalSection _NoMemCallBackCS;
static void* _reservedMem = malloc(1024 * 1024 * 4); // 4M reserved mem
static bool _project_startup = false;

ttstr TVPGetErrorDialogTitle()
{
    const ttstr& title = TVPGetWindowTitle();
    if (title.IsEmpty())
    {
        return TVPGetPackageVersionString() + " Error";
    }
    else
    {
        return ttstr(TVPGetPackageVersionString()) + " " + title;
    }
}

void TVPOnError();
void TVPLockSoundMixer();
void TVPUnlockSoundMixer();

static bool _warnLowMem = true;
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

bool tTVPApplication::StartApplication()
{
    TVPTerminateCode = 0;

    // try starting the program!
    try
    {
        TVPInitScriptEngine();
        TVPInitFontNames();

        // banner
        TVPAddImportantLog(
            TVPFormatMessage(TVPProgramStartedOn, TVPGetOSName(), TVPGetPlatformName()));

        // TVPInitializeBaseSystems
        TVPInitializeBaseSystems();
        
        image_load_thread_ = new tTVPAsyncImageLoader();

        TVPLoadPluigins(); // load plugin module *.tpm
        TVPSystemInit();

        TVPSetWindowTitle(TVPKirikiri.operator const tjs_char*());
        TVPSystemControl = new tTVPSystemControl();

        // start image load thread
        image_load_thread_->Resume();

        TVPInitializeStartupScript();
        _project_startup = true;
    }
    catch (const EAbort&)
    {
        // nothing to do
    }

    return true;
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
        tTJSCSH cs(m_msgQueueLock);
        m_lstUserMsg.swap(lstUserMsg);
    }
    for (std::tuple<void*, int, tMsg>& it : lstUserMsg)
    {
        std::get<2>(it)();
    }
    TVPTimer::ProgressAllTimer();
}

void tTVPApplication::Terminate()
{
    tarminate_ = true;
    TVPTerminated = true;
}

void tTVPApplication::PostUserMessage(const std::function<void()>& func, void* host, int msg)
{
    tTJSCSH cs(m_msgQueueLock);
    m_lstUserMsg.emplace_back(host, msg, func);
}

void tTVPApplication::FilterUserMessage(
    const std::function<void(std::vector<std::tuple<void*, int, tMsg>>&)>& func)
{
    tTJSCSH cs(m_msgQueueLock);
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
