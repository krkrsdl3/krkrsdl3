//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Thread base class
//---------------------------------------------------------------------------
#ifndef TVPThreadH
#define TVPThreadH
#include "tjsNative.h"

#include <condition_variable>
#include <functional>
#include <SDL2/SDL_thread.h>

//---------------------------------------------------------------------------
// tTVPThreadPriority
//---------------------------------------------------------------------------
enum tTVPThreadPriority
{
    ttpIdle,
    ttpLowest,
    ttpLower,
    ttpNormal,
    ttpHigher,
    ttpHighest,
    ttpTimeCritical
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPThread
//---------------------------------------------------------------------------
class tTVPThread
{
    bool Terminated;
    bool Suspended;
    SDL_Thread* Handle;
    std::mutex _mutex;
    std::condition_variable _cond;

    static int StartProc(void* arg);

public:
    tTVPThread();
    virtual ~tTVPThread();

    bool GetTerminated() const { return Terminated; }
    bool IsRunning() { return !Suspended; }
    void Terminate();
    void StopThread();
    void Sleep(unsigned int milliseconds);
    bool IsCurrentThread();

protected:
    virtual void Execute() = 0;
    virtual void OnExit(){};

public:
    void WaitFor();

    tTVPThreadPriority GetPriority();
    void SetPriority(tTVPThreadPriority pri);

    void Resume();
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPThreadEvent
//---------------------------------------------------------------------------
class tTVPThreadEvent
{
    std::condition_variable Handle;
    std::mutex Mutex;

public:
    void Set();
    bool WaitFor(tjs_uint timeout);
};

/*[*/
const tjs_int TVPMaxThreadNum = 8;
typedef const std::function<void(int)>& TVP_THREAD_TASK_FUNC;
/*]*/

tjs_int TVPGetProcessorNum();
tjs_int TVPGetThreadNum();
void TVPExecThreadTask(int numThreads, TVP_THREAD_TASK_FUNC func);

//---------------------------------------------------------------------------
void TVPAddOnThreadExitEvent(const std::function<void()>& ev);
void TVPOnThreadExited();
//---------------------------------------------------------------------------

#endif
