//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Thread base class
//---------------------------------------------------------------------------

#include "tjsCommHead.h"
#include "TVPThread.h"

#include <thread>
#include <mutex>
#include "TVPMsg.h"
#include "TVPDebug.h"

//---------------------------------------------------------------------------
// tTVPThread : a wrapper class for thread
//---------------------------------------------------------------------------
tTVPThread::tTVPThread()
{
    Terminated = false;
    Suspended = true;

    Handle = SDL_CreateThread(StartProc, "TVPThread", this);
    if (!Handle)
    {
        TVPThrowInternalError;
    }
}
//---------------------------------------------------------------------------
tTVPThread::~tTVPThread()
{
    // Ensure the thread is fully stopped and joined before destroying
    // mutex/condvar members.  Previous code only called Terminate() which
    // set a flag but never joined — leaving a dangling thread on re-entry.
    StopThread();
}
//---------------------------------------------------------------------------
void tTVPThread::Terminate()
{
    std::lock_guard<std::mutex> lk(_mutex);
    Terminated = true;
}
void tTVPThread::StopThread()
{
    SDL_Thread* h = nullptr;
    {
        std::lock_guard<std::mutex> lk(_mutex);
        Terminated = true;
        Suspended = false;          // wake predicate in StartProc
        h = (SDL_Thread*)Handle;
        Handle = nullptr;           // prevent double-join
    }
    _cond.notify_all();
    if (h)
    {
        SDL_WaitThread(h, nullptr);
    }
}
//---------------------------------------------------------------------------
void tTVPThread::Sleep(unsigned int milliseconds)
{
    if (IsCurrentThread())
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _cond.wait_for(lock, std::chrono::milliseconds(milliseconds));
    }
    else
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
}
//---------------------------------------------------------------------------
bool tTVPThread::IsCurrentThread()
{
    SDL_Thread* h = nullptr;
    {
        std::lock_guard<std::mutex> lk(_mutex);
        h = (SDL_Thread*)Handle;
    }
    if (!h) return false;
    return (SDL_ThreadID() == SDL_GetThreadID(h));
}
//---------------------------------------------------------------------------
int tTVPThread::StartProc(void* arg)
{
    tTVPThread* _this = static_cast<tTVPThread*>(arg);

    // Wait until Resume() or StopThread() using a predicate-based wait
    // to be immune to spurious wakeups.
    {
        std::unique_lock<std::mutex> lk(_this->_mutex);
        _this->_cond.wait(lk, [_this]{
            return !_this->Suspended || _this->Terminated;
        });
    }

    // If terminated while still suspended, exit without calling Execute()
    if (_this->Terminated)
    {
        TVPOnThreadExited();
        return 0;
    }

    _this->Execute();
    _this->OnExit();
    _this->Terminated = false;
    TVPOnThreadExited();

    return 0;
}
//---------------------------------------------------------------------------
void tTVPThread::WaitFor()
{
    SDL_Thread* h = nullptr;
    {
        std::lock_guard<std::mutex> lk(_mutex);
        h = (SDL_Thread*)Handle;
        Handle = nullptr;
    }
    if (h)
    {
        SDL_WaitThread(h, nullptr);
    }
}
//---------------------------------------------------------------------------
tTVPThreadPriority tTVPThread::GetPriority()
{
    // do nothing
    return ttpNormal;
}
//---------------------------------------------------------------------------
void tTVPThread::SetPriority(tTVPThreadPriority pri)
{
    // do nothing
}
//---------------------------------------------------------------------------
void tTVPThread::Resume()
{
    {
        std::lock_guard<std::mutex> lk(_mutex);
        Suspended = false;
    }
    _cond.notify_one();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPThreadEvent
//---------------------------------------------------------------------------
void tTVPThreadEvent::Set()
{
    std::unique_lock<std::mutex> lk(Mutex);
    Handle.notify_one();
}
//---------------------------------------------------------------------------
bool tTVPThreadEvent::WaitFor(tjs_uint timeout)
{
    // wait for event;
    // returns true if the event is set, otherwise (when timed out) returns false.

    std::unique_lock<std::mutex> lk(Mutex);
    if (timeout != 0)
    {
        return Handle.wait_for(lk, std::chrono::milliseconds(timeout)) != std::cv_status::timeout;
    }
    else
    {
        Handle.wait(lk);
        return true;
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
tjs_int TVPDrawThreadNum = 1;

static std::vector<tjs_int> TVPProcesserIdList;
static tjs_int TVPThreadTaskNum, TVPThreadTaskCount;

//---------------------------------------------------------------------------
static tjs_int GetProcesserNum(void)
{
    static tjs_int processor_num = 0;
    if (!processor_num)
    {
        processor_num = std::thread::hardware_concurrency();
        tjs_char tmp[34];
        TVPAddLog(ttstr(TJS_N("Detected CPU core(s): ")) + TJS_tTVInt_to_str(processor_num, tmp));
    }
    return processor_num;
}

tjs_int TVPGetProcessorNum(void)
{
    return GetProcesserNum();
}

//---------------------------------------------------------------------------
tjs_int TVPGetThreadNum(void)
{
    tjs_int threadNum = TVPDrawThreadNum ? TVPDrawThreadNum : GetProcesserNum();
    threadNum = std::min(threadNum, TVPMaxThreadNum);
    return threadNum;
}

//---------------------------------------------------------------------------
void TVPExecThreadTask(int numThreads, TVP_THREAD_TASK_FUNC func)
{
    if (numThreads == 1)
    {
        func(0);
        return;
    }

#pragma omp parallel for schedule(static)
    for (int i = 0; i < numThreads; ++i)
        func(i);
}
//---------------------------------------------------------------------------

// xp3filter cleaner
std::vector<std::function<void()>> _OnThreadExitedEvents;

void TVPOnThreadExited()
{
    for (const auto& ev : _OnThreadExitedEvents)
    {
        ev();
    }
}

void TVPAddOnThreadExitEvent(const std::function<void()>& ev)
{
    _OnThreadExitedEvents.emplace_back(ev);
}
