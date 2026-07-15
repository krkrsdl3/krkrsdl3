//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Thread base class — pthread 实现（用于 WASM / Linux / Unix）
//---------------------------------------------------------------------------

#include "tjsCommHead.h"
#include "Platform.h"
#include "PlatformThread.h"
#include "PlatformMutex.h"

#include "TVPMsg.h"
#include "TVPDebug.h"

#include <pthread.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <functional>

#include <time.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#include <emscripten/threading.h>
#endif

// ── 线程数量追踪 ──────────────────────────────────────────────────
#include <atomic>
static std::atomic<int> _tvpThreadCount{0};
static std::atomic<int> _tvpThreadTotalCreated{0};

extern "C" {
int EMSCRIPTEN_KEEPALIVE wasmGetThreadCount() { return _tvpThreadCount.load(); }
int EMSCRIPTEN_KEEPALIVE wasmGetThreadTotalCreated() { return _tvpThreadTotalCreated.load(); }
}

// 在 pthread worker 中使用的非 ASYNCIFY 睡眠函数
static void do_sleep_ms(uint32_t ms)
{
    emscripten_sleep(ms);
}

//---------------------------------------------------------------------------
// tTVPThread
//---------------------------------------------------------------------------
struct TVPThreadImpl
{
    pthread_t thread;
    std::mutex* mutex;
    std::condition_variable* cond;
    bool thread_created;
};

#define THR_IMPL ((TVPThreadImpl*)_impl)

tTVPThread::tTVPThread()
{
    Terminated = false;
    Suspended = true;

    _impl = new TVPThreadImpl;
    THR_IMPL->mutex = new std::mutex();
    THR_IMPL->cond = new std::condition_variable();
    THR_IMPL->thread_created = false;

    if (pthread_create(&THR_IMPL->thread, nullptr, (void* (*)(void*))StartProc, this) != 0)
    {
        TVPAddLog(ttstr(TJS_N("WARNING: pthread_create failed")));
        THR_IMPL->thread_created = false;
    }
    else
    {
        THR_IMPL->thread_created = true;
        _tvpThreadCount.fetch_add(1, std::memory_order_relaxed);
        _tvpThreadTotalCreated.fetch_add(1, std::memory_order_relaxed);
    }
}

tTVPThread::~tTVPThread()
{
    if (!Terminated)
        Terminate();
    if (THR_IMPL->thread_created)
        _tvpThreadCount.fetch_sub(1, std::memory_order_relaxed);
    delete THR_IMPL->cond;
    delete THR_IMPL->mutex;
    delete THR_IMPL;
    _impl = nullptr;
}

void tTVPThread::Terminate()
{
    Terminated = true;
}

void tTVPThread::StopThread()
{
    Terminated = true;
    if (THR_IMPL->thread_created)
    {
        THR_IMPL->cond->notify_all();
        pthread_join(THR_IMPL->thread, nullptr);
    }
}

void tTVPThread::Sleep(unsigned int milliseconds)
{
    if (IsCurrentThread())
    {
        std::unique_lock<std::mutex> lk(*THR_IMPL->mutex);
        THR_IMPL->cond->wait_for(lk, std::chrono::milliseconds(milliseconds));
    }
    else
    {
        do_sleep_ms(milliseconds);
    }
}

bool tTVPThread::IsCurrentThread()
{
    if (!THR_IMPL->thread_created) return true;
    return pthread_equal(pthread_self(), THR_IMPL->thread);
}

int tTVPThread::StartProc(void* arg)
{
    tTVPThread* _this = static_cast<tTVPThread*>(arg);
    auto impl = (TVPThreadImpl*)_this->_impl;
    if (!impl) return -1;

    if (_this->Suspended)
    {
        std::unique_lock<std::mutex> lk(*impl->mutex);
        impl->cond->wait(lk);
    }
    _this->Execute();
    _this->OnExit();
    _this->Terminated = false;
    TVPOnThreadExited();

    return 0;
}

void tTVPThread::WaitFor()
{
    if (THR_IMPL->thread_created)
        pthread_join(THR_IMPL->thread, nullptr);
}

tTVPThreadPriority tTVPThread::GetPriority()
{
    return ttpNormal;
}

void tTVPThread::SetPriority(tTVPThreadPriority pri)
{
}

void tTVPThread::Resume()
{
    Suspended = false;
    if (THR_IMPL->thread_created)
        THR_IMPL->cond->notify_one();
}

//---------------------------------------------------------------------------
// tTVPThreadEvent
//---------------------------------------------------------------------------
#define EVT_IMPL ((TVPThreadEventImpl*)_impl)
struct TVPThreadEventImpl
{
    std::condition_variable* cond;
    std::mutex* mutex;
};

tTVPThreadEvent::tTVPThreadEvent()
{
    _impl = new TVPThreadEventImpl;
    EVT_IMPL->cond = new std::condition_variable();
    EVT_IMPL->mutex = new std::mutex();
}

tTVPThreadEvent::~tTVPThreadEvent()
{
    delete EVT_IMPL->cond;
    delete EVT_IMPL->mutex;
    delete EVT_IMPL;
}

void tTVPThreadEvent::Set()
{
    std::unique_lock<std::mutex> lk(*EVT_IMPL->mutex);
    EVT_IMPL->cond->notify_one();
}

bool tTVPThreadEvent::WaitFor(int timeout)
{
    std::unique_lock<std::mutex> lk(*EVT_IMPL->mutex);
    if (timeout != 0)
    {
        return EVT_IMPL->cond->wait_for(lk, std::chrono::milliseconds(timeout))
               != std::cv_status::timeout;
    }
    else
    {
        EVT_IMPL->cond->wait(lk);
        return true;
    }
}

//---------------------------------------------------------------------------
// 平台无关部分
//---------------------------------------------------------------------------
int TVPDrawThreadNum = 1;

static std::vector<tjs_int> TVPProcesserIdList;
static tjs_int TVPThreadTaskNum, TVPThreadTaskCount;

static tjs_int GetProcesserNum(void)
{
    static tjs_int processor_num = 0;
    if (!processor_num)
    {
        processor_num = (tjs_int)std::thread::hardware_concurrency();
        tjs_char tmp[34];
        TVPAddLog(ttstr(TJS_N("Detected CPU core(s): ")) + TJS_tTVInt_to_str(processor_num, tmp));
    }
    return processor_num;
}

int TVPGetThreadNum(void)
{
    tjs_int threadNum = TVPDrawThreadNum ? TVPDrawThreadNum : GetProcesserNum();
    threadNum = std::min(threadNum, TVPMaxThreadNum);
    return threadNum;
}

void TVPExecThreadTask(int numThreads, TVP_THREAD_TASK_FUNC func)
{
    if (numThreads == 1) { func(0); return; }
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < numThreads; ++i) func(i);
}

std::vector<std::function<void()>> _OnThreadExitedEvents;

void TVPOnThreadExited()
{
    for (const auto& ev : _OnThreadExitedEvents) ev();
}

void TVPAddOnThreadExitEvent(const std::function<void()>& ev)
{
    _OnThreadExitedEvents.emplace_back(ev);
}

bool TVPIsInMainThread()
{
#if defined(__EMSCRIPTEN__)
    return emscripten_is_main_browser_thread();
#else
    static pthread_t main_tid = pthread_self();
    return pthread_equal(pthread_self(), main_tid);
#endif
}

uint64_t TVPGetCurrentThreadID()
{
#if defined(__EMSCRIPTEN__)
    return (uint64_t)pthread_self();
#else
    return (uint64_t)pthread_self();
#endif
}

void TVPSleepFor(uint32_t ms)
{
#ifdef __EMSCRIPTEN__
    emscripten_sleep(ms);
#else
    do_sleep_ms(ms);
#endif
}

//---------------------------------------------------------------------------
// tTJSCriticalSection
//---------------------------------------------------------------------------
struct tTJSCriticalSectionImpl
{
    std::mutex _mutex;
    std::thread::id _tid;

    bool lock()
    {
        std::thread::id id = std::this_thread::get_id();
        if (_tid == id) return false;
        _mutex.lock();
        _tid = id;
        return true;
    }
    void unlock()
    {
        _tid = std::thread::id();
        _mutex.unlock();
    }
};

bool tTJSCriticalSection::lock() { return _impl->lock(); }
void tTJSCriticalSection::unlock() { _impl->unlock(); }
tTJSCriticalSection::tTJSCriticalSection() { _impl = new tTJSCriticalSectionImpl; }
tTJSCriticalSection::~tTJSCriticalSection() { delete _impl; }

tTJSCriticalSectionHolder::tTJSCriticalSectionHolder(tTJSCriticalSection& cs)
{
    if (cs.lock()) _cs = &cs; else _cs = nullptr;
}
tTJSCriticalSectionHolder::~tTJSCriticalSectionHolder() { if (_cs) _cs->unlock(); }

tTJSUniqueLock::tTJSUniqueLock(tTJSCriticalSection& cs) : owns(true)
{
    if (cs.lock()) _cs = &cs; else _cs = nullptr;
}
tTJSUniqueLock::~tTJSUniqueLock() { if (owns && _cs) _cs->unlock(); }
void tTJSUniqueLock::unlock() { if (owns) { owns = false; _cs->unlock(); } }
void tTJSUniqueLock::lock() { if (!owns) { owns = true; _cs->lock(); } }

//---------------------------------------------------------------------------
// tTVPCondition
//---------------------------------------------------------------------------
tTVPCondition::tTVPCondition() : _impl(new std::condition_variable()) {}
tTVPCondition::~tTVPCondition() { delete (std::condition_variable*)_impl; }
void tTVPCondition::notify_one() { ((std::condition_variable*)_impl)->notify_one(); }
void tTVPCondition::notify_all() { ((std::condition_variable*)_impl)->notify_all(); }
void tTVPCondition::Wait(tTJSCriticalSection& cs)
{
    std::unique_lock<std::mutex> lk(cs._impl->_mutex, std::adopt_lock);
    ((std::condition_variable*)_impl)->wait(lk);
    lk.release();
}
bool tTVPCondition::WaitFor(tTJSCriticalSection& cs, unsigned int ms)
{
    std::unique_lock<std::mutex> lk(cs._impl->_mutex, std::adopt_lock);
    auto result = ((std::condition_variable*)_impl)->wait_for(lk, std::chrono::milliseconds(ms));
    lk.release();
    return result != std::cv_status::timeout;
}

//---------------------------------------------------------------------------
// tTJSSpinLock
//---------------------------------------------------------------------------
#if defined(__EMSCRIPTEN__)
tTJSSpinLock::tTJSSpinLock() : splock(0) {}
void tTJSSpinLock::lock() {
    while (__atomic_test_and_set(&splock, __ATOMIC_ACQUIRE)) {
        emscripten_futex_wait(&splock, 1, 100);
    }
}
void tTJSSpinLock::unlock() {
    __atomic_clear(&splock, __ATOMIC_RELEASE);
    emscripten_futex_wake(&splock, 1);
}
#else
tTJSSpinLock::tTJSSpinLock() { splock = 0; }
void tTJSSpinLock::lock() {
    while (__sync_lock_test_and_set(&splock, 1)) { /* spin */ }
}
void tTJSSpinLock::unlock() {
    __sync_lock_release(&splock);
}
#endif
tTJSSpinLockHolder::tTJSSpinLockHolder(tTJSSpinLock& lock) : Lock(nullptr) { lock.lock(); Lock = &lock; }
tTJSSpinLockHolder::~tTJSSpinLockHolder() { if (Lock) Lock->unlock(); }
//---------------------------------------------------------------------------
