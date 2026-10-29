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
#include "Platform.h"
#include "PlatformThread.h"
#include "PlatformMutex.h"

#include "TVPMsg.h"
#include "TVPDebug.h"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>
#if defined(__APPLE__)
#include <pthread/qos.h>
#include <dispatch/dispatch.h>
#include <sys/event.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#endif

//---------------------------------------------------------------------------
// tTVPThread : a wrapper class for thread
//---------------------------------------------------------------------------
#define THR_IMPL ((TVPThreadImpl*)_impl)
struct TVPThreadImpl
{
#if defined(__APPLE__)
    const char* name;
    int kq = -1;
#endif
    SDL_Thread* thread;
    SDL_Mutex* mutex;
    SDL_Condition* cond;
};

#if defined(__APPLE__)
static void TVPWakeThread(TVPThreadImpl* impl)
{
    if (impl->kq < 0)
        return;

    struct kevent ev;
    EV_SET(&ev, 2, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);
    kevent(impl->kq, &ev, 1, nullptr, 0, nullptr);
}
#endif

tTVPThread::tTVPThread(const char* name)
{
    Terminated = false;
    Suspended = true;

    _impl = new TVPThreadImpl;
#if defined(__APPLE__)
    THR_IMPL->name = name;
    THR_IMPL->kq = kqueue();
    if (THR_IMPL->kq >= 0)
    {
        struct kevent ev;
        EV_SET(&ev, 2, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);
        if (kevent(THR_IMPL->kq, &ev, 1, nullptr, 0, nullptr) < 0)
        {
            close(THR_IMPL->kq);
            THR_IMPL->kq = -1;
        }
    }
#endif
    THR_IMPL->mutex = SDL_CreateMutex();
    THR_IMPL->cond = SDL_CreateCondition();
    THR_IMPL->thread = SDL_CreateThread(StartProc, name, this);
    if (!THR_IMPL->thread)
    {
        TVPThrowInternalError;
    }
}
//---------------------------------------------------------------------------
tTVPThread::~tTVPThread()
{
    if (!Terminated)
        Terminate();
    SDL_DestroyCondition(THR_IMPL->cond);
    SDL_DestroyMutex(THR_IMPL->mutex);
#if defined(__APPLE__)
    if (THR_IMPL->kq >= 0) close(THR_IMPL->kq);
#endif
    delete THR_IMPL;
    _impl = NULL;
}
//---------------------------------------------------------------------------
void tTVPThread::Terminate()
{
    Terminated = true;
}
void tTVPThread::StopThread()
{
    Terminated = true;
    SDL_BroadcastCondition(THR_IMPL->cond);
#if defined(__APPLE__)
    // 让可能正在 kqueue 计时器上等待的线程尽快返回。
    TVPWakeThread(THR_IMPL);
#endif
    SDL_WaitThread(THR_IMPL->thread, nullptr);
}
//---------------------------------------------------------------------------
void tTVPThread::Sleep(unsigned int milliseconds)
{
    if (IsCurrentThread())
    {
#if defined(__APPLE__)
        // pthread_cond_timedwait 在 macOS 上实际会晚 90~110ms 才返回：系统会合并
        // 定时器，且默认线程 QoS 较低。这导致每 10ms 一次的轮询变成每 100ms 一次，
        // 视频播放循环直接慢了十倍。kqueue 的 NOTE_CRITICAL 计时器精度足够，
        // 配合 EVFILT_USER 还能保留 Resume() 主动唤醒的能力。
        if (THR_IMPL->kq >= 0)
        {
            struct kevent change, event;
            EV_SET(&change, 1, EVFILT_TIMER, EV_ADD | EV_ONESHOT,
                   NOTE_NSECONDS | NOTE_CRITICAL, (int64_t)milliseconds * 1000000, nullptr);
            int ret;
            do
            {
                ret = kevent(THR_IMPL->kq, &change, 1, &event, 1, nullptr);
            } while (ret < 0 && errno == EINTR);
            if (ret >= 0)
                return;
        }
#endif
        SDL_LockMutex(THR_IMPL->mutex);
        SDL_WaitConditionTimeout(THR_IMPL->cond, THR_IMPL->mutex, (int)milliseconds);
        SDL_UnlockMutex(THR_IMPL->mutex);
    }
    else
    {
        SDL_Delay(milliseconds);
    }
}
//---------------------------------------------------------------------------
bool tTVPThread::IsCurrentThread()
{
    return (SDL_GetCurrentThreadID() == SDL_GetThreadID(THR_IMPL->thread));
}
//---------------------------------------------------------------------------
int tTVPThread::StartProc(void* arg)
{
    tTVPThread* _this = static_cast<tTVPThread*>(arg);
    TVPThreadImpl* impl = (TVPThreadImpl*)_this->_impl;
    if (!impl)
        return 0;
#if defined(__APPLE__)
    // 实时媒体线程使用 user-interactive，其余使用 user-initiated；
    // 两者都低于主线程，避免线程被系统降频后轮询周期被拉长。
    {
        static const char* const realtime[] = {"TVPVideoPlayer", "TVPVideoPlayerVideo",
                                               "TVPVideoPlayerAudio", "TVPWaveSoundBufferThread",
                                               "TVPTimerThread"};
        qos_class_t qos = QOS_CLASS_USER_INITIATED;
        for (const char* n : realtime)
        {
            if (impl->name && !std::strcmp(impl->name, n))
                qos = QOS_CLASS_USER_INTERACTIVE;
        }
        pthread_set_qos_class_self_np(qos, 0);
    }
#endif

    // 等待恢复
    if (_this->Suspended)
    {
        SDL_LockMutex(impl->mutex);
        SDL_WaitCondition(impl->cond, impl->mutex);
        SDL_UnlockMutex(impl->mutex);
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
    SDL_WaitThread(THR_IMPL->thread, nullptr);
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
    Suspended = false;
    SDL_SignalCondition(THR_IMPL->cond);
#if defined(__APPLE__)
    // 唤醒正在 kqueue 上等待的 Sleep()。
    TVPWakeThread(THR_IMPL);
#endif
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPThreadEvent
//---------------------------------------------------------------------------
#define EVT_IMPL ((TVPThreadEventImpl*)_impl)
struct TVPThreadEventImpl
{
    SDL_Condition* cond;
    SDL_Mutex* mutex;
};

tTVPThreadEvent::tTVPThreadEvent()
{
    _impl = new TVPThreadEventImpl;
    EVT_IMPL->cond = SDL_CreateCondition();
    EVT_IMPL->mutex = SDL_CreateMutex();
}

tTVPThreadEvent::~tTVPThreadEvent()
{
    SDL_DestroyCondition(EVT_IMPL->cond);
    SDL_DestroyMutex(EVT_IMPL->mutex);
    delete EVT_IMPL;
}

void tTVPThreadEvent::Set()
{
    SDL_LockMutex(EVT_IMPL->mutex);
    SDL_SignalCondition(EVT_IMPL->cond);
    SDL_UnlockMutex(EVT_IMPL->mutex);
}
//---------------------------------------------------------------------------
bool tTVPThreadEvent::WaitFor(int timeout)
{
    // wait for event;
    // returns true if the event is set, otherwise (when timed out) returns false.

    SDL_LockMutex(EVT_IMPL->mutex);
    bool result;
    if (timeout != 0)
        result = SDL_WaitConditionTimeout(EVT_IMPL->cond, EVT_IMPL->mutex, (int)timeout);
    else
    {
        SDL_WaitCondition(EVT_IMPL->cond, EVT_IMPL->mutex);
        result = true;
    }
    SDL_UnlockMutex(EVT_IMPL->mutex);
    return result;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
int TVPDrawThreadNum = 1;

static std::vector<tjs_int> TVPProcesserIdList;
static tjs_int TVPThreadTaskNum, TVPThreadTaskCount;

//---------------------------------------------------------------------------
int TVPGetThreadNum(void)
{
    tjs_int threadNum = TVPDrawThreadNum ? TVPDrawThreadNum : TVPGetProcessorNum();
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

#if defined(__APPLE__)
    // 各分片写入互不重叠的行，用 GCD 在调用线程和线程池上并行执行。
    // 使用函数指针版本，避免依赖 Clang 的 blocks 扩展。
    dispatch_apply_f(
        (size_t)numThreads, DISPATCH_APPLY_AUTO, (void*)&func,
        [](void* context, size_t i)
        { (*static_cast<const std::function<void(int)>*>(context))((int)i); });
#else
#pragma omp parallel for schedule(static)
    for (int i = 0; i < numThreads; ++i)
        func(i);
#endif
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

uint64_t TVPGetCurrentThreadID()
{
    return SDL_GetCurrentThreadID();
}

void TVPSleepFor(uint32_t ms)
{
    SDL_Delay(ms);
}

//---------------------------------------------------------------------------
// tTJSCriticalSection
//---------------------------------------------------------------------------
struct tTJSCriticalSectionImpl
{
    SDL_Mutex* _mutex;
    SDL_ThreadID _tid;

    tTJSCriticalSectionImpl() : _mutex(SDL_CreateMutex()), _tid(0) {}
    ~tTJSCriticalSectionImpl() { SDL_DestroyMutex(_mutex); }

    bool lock()
    {
        SDL_ThreadID id = SDL_GetCurrentThreadID();
        if (_tid == id)
            return false;

        SDL_LockMutex(_mutex);
        _tid = id;
        return true;
    }
    void unlock()
    {
        _tid = 0;
        SDL_UnlockMutex(_mutex);
    }
};

bool tTJSCriticalSection::lock()
{
    return _impl->lock();
}
void tTJSCriticalSection::unlock()
{
    _impl->unlock();
}
tTJSCriticalSection::tTJSCriticalSection()
{
    _impl = new tTJSCriticalSectionImpl;
}
tTJSCriticalSection::~tTJSCriticalSection()
{
    delete _impl;
}

//---------------------------------------------------------------------------
// tTJSCriticalSectionHolder
//---------------------------------------------------------------------------
tTJSCriticalSectionHolder::tTJSCriticalSectionHolder(tTJSCriticalSection& cs)
{
    if (cs.lock())
    {
        _cs = &cs;
    }
    else
    {
        _cs = nullptr;
    }
}
tTJSCriticalSectionHolder::~tTJSCriticalSectionHolder()
{
    if (_cs)
        _cs->unlock();
}

//---------------------------------------------------------------------------
// tTJSUniqueLock
//---------------------------------------------------------------------------
tTJSUniqueLock::tTJSUniqueLock(tTJSCriticalSection& cs) : owns(true)
{
    if (cs.lock())
    {
        _cs = &cs;
    }
    else
    {
        _cs = nullptr;
    }
}
tTJSUniqueLock::~tTJSUniqueLock()
{
    if (owns && _cs)
        _cs->unlock();
}
void tTJSUniqueLock::unlock()
{
    if (owns)
    {
        owns = false;
        _cs->unlock();
    }
}
void tTJSUniqueLock::lock()
{
    if (!owns)
    {
        owns = true;
        _cs->lock();
    }
}

//---------------------------------------------------------------------------
// tTVPCondition
//---------------------------------------------------------------------------
tTVPCondition::tTVPCondition() : _impl(SDL_CreateCondition())
{
}
tTVPCondition::~tTVPCondition()
{
    SDL_DestroyCondition((SDL_Condition*)_impl);
}
void tTVPCondition::notify_one()
{
    SDL_SignalCondition((SDL_Condition*)_impl);
}
void tTVPCondition::notify_all()
{
    SDL_BroadcastCondition((SDL_Condition*)_impl);
}
void tTVPCondition::Wait(tTJSCriticalSection& cs)
{
    SDL_WaitCondition((SDL_Condition*)_impl, (SDL_Mutex*)cs._impl->_mutex);
}
bool tTVPCondition::WaitFor(tTJSCriticalSection& cs, unsigned int ms)
{
    return SDL_WaitConditionTimeout((SDL_Condition*)_impl, (SDL_Mutex*)cs._impl->_mutex, (int)ms);
}

//---------------------------------------------------------------------------
// tTJSSpinLock
//---------------------------------------------------------------------------
void tTJSSpinLock::lock()
{
    SDL_LockSpinlock(&splock);
}
void tTJSSpinLock::unlock()
{
    SDL_UnlockSpinlock(&splock);
}
tTJSSpinLock::tTJSSpinLock()
{
    unlock();
}

tTJSSpinLockHolder::tTJSSpinLockHolder(tTJSSpinLock& lock)
{
    lock.lock();
    Lock = &lock;
}
tTJSSpinLockHolder::~tTJSSpinLockHolder()
{
    if (Lock)
    {
        Lock->unlock();
    }
}
//---------------------------------------------------------------------------