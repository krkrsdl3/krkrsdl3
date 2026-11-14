// OHOS thread — pthread (musl libc)
// Replaces sdl3 thread.cpp

#include "tjsCommHead.h"
#include "Platform.h"
#include "PlatformThread.h"
#include "PlatformMutex.h"
#include "TVPMsg.h"
#include "TVPDebug.h"

#include <pthread.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <vector>
#include <functional>
#include <algorithm>

// ─── tTVPThread ────────────────────────────────────────────────
#define THR_IMPL ((TVPThreadImpl*)_impl)
struct TVPThreadImpl { pthread_t thread; pthread_mutex_t mutex; pthread_cond_t cond; bool joined; };

tTVPThread::tTVPThread(const char* name)
{
    Terminated = false;
    Suspended = true;
    _impl = new TVPThreadImpl;
    THR_IMPL->joined = false;
    pthread_mutex_init(&THR_IMPL->mutex, nullptr);
    pthread_cond_init(&THR_IMPL->cond, nullptr);
    pthread_create(&THR_IMPL->thread, nullptr, (void*(*)(void*))StartProc, this);
    pthread_setname_np(THR_IMPL->thread, name);
}

tTVPThread::~tTVPThread()
{
    if (!Terminated) Terminate();
    if (!THR_IMPL->joined) {
        pthread_join(THR_IMPL->thread, nullptr);
        THR_IMPL->joined = true;
    }
    pthread_cond_destroy(&THR_IMPL->cond);
    pthread_mutex_destroy(&THR_IMPL->mutex);
    delete THR_IMPL;
    _impl = NULL;
}

void tTVPThread::Terminate() { Terminated = true; }
void tTVPThread::StopThread()
{
    Terminated = true;
    pthread_cond_broadcast(&THR_IMPL->cond);
    if (!THR_IMPL->joined) {
        pthread_join(THR_IMPL->thread, nullptr);
        THR_IMPL->joined = true;
    }
}

void tTVPThread::Sleep(unsigned int ms)
{
    if (IsCurrentThread()) {
        pthread_mutex_lock(&THR_IMPL->mutex);
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += ms / 1000;
        ts.tv_nsec += (ms % 1000) * 1000000;
        if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
        pthread_cond_timedwait(&THR_IMPL->cond, &THR_IMPL->mutex, &ts);
        pthread_mutex_unlock(&THR_IMPL->mutex);
    } else {
        usleep(ms * 1000);
    }
}

bool tTVPThread::IsCurrentThread() { return pthread_equal(pthread_self(), THR_IMPL->thread); }

int tTVPThread::StartProc(void* arg)
{
    tTVPThread* _this = static_cast<tTVPThread*>(arg);
    TVPThreadImpl* impl = (TVPThreadImpl*)_this->_impl;
    if (_this->Suspended) {
        pthread_mutex_lock(&impl->mutex);
        pthread_cond_wait(&impl->cond, &impl->mutex);
        pthread_mutex_unlock(&impl->mutex);
    }
    _this->Execute();
    _this->OnExit();
    _this->Terminated = false;
    TVPOnThreadExited();
    return 0;
}

void tTVPThread::WaitFor() {
    if (!THR_IMPL->joined) {
        pthread_join(THR_IMPL->thread, nullptr);
        THR_IMPL->joined = true;
    }
}
tTVPThreadPriority tTVPThread::GetPriority() { return ttpNormal; }
void tTVPThread::SetPriority(tTVPThreadPriority) {}

void tTVPThread::Resume()
{
    Suspended = false;
    pthread_cond_signal(&THR_IMPL->cond);
}

// ─── tTVPThreadEvent ───────────────────────────────────────────
#define EVT_IMPL ((TVPThreadEventImpl*)_impl)
struct TVPThreadEventImpl { pthread_cond_t cond; pthread_mutex_t mutex; };

tTVPThreadEvent::tTVPThreadEvent()
{
    _impl = new TVPThreadEventImpl;
    pthread_cond_init(&EVT_IMPL->cond, nullptr);
    pthread_mutex_init(&EVT_IMPL->mutex, nullptr);
}

tTVPThreadEvent::~tTVPThreadEvent()
{
    pthread_cond_destroy(&EVT_IMPL->cond);
    pthread_mutex_destroy(&EVT_IMPL->mutex);
    delete EVT_IMPL;
}

void tTVPThreadEvent::Set()
{
    pthread_mutex_lock(&EVT_IMPL->mutex);
    pthread_cond_signal(&EVT_IMPL->cond);
    pthread_mutex_unlock(&EVT_IMPL->mutex);
}

bool tTVPThreadEvent::WaitFor(int timeout)
{
    pthread_mutex_lock(&EVT_IMPL->mutex);
    bool result = true;
    if (timeout > 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout / 1000;
        ts.tv_nsec += (long long)(timeout % 1000) * 1000000LL;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec += ts.tv_nsec / 1000000000L;
            ts.tv_nsec %= 1000000000L;
        }
        result = pthread_cond_timedwait(&EVT_IMPL->cond, &EVT_IMPL->mutex, &ts) == 0;
    } else {
        // timeout == 0 或 INFINITE(-1)：无限等待（与 SDL 后端语义一致）
        pthread_cond_wait(&EVT_IMPL->cond, &EVT_IMPL->mutex);
    }
    pthread_mutex_unlock(&EVT_IMPL->mutex);
    return result;
}

// ─── Thread pool helpers ───────────────────────────────────────
int TVPDrawThreadNum = 1;
static std::vector<int> TVPProcesserIdList;
static int TVPThreadTaskNum, TVPThreadTaskCount;

int TVPGetThreadNum(void)
{
    int n = TVPDrawThreadNum ? TVPDrawThreadNum : TVPGetProcessorNum();
    return std::min(n, TVPMaxThreadNum);
}

void TVPExecThreadTask(int numThreads, TVP_THREAD_TASK_FUNC func)
{
    if (numThreads == 1) { func(0); return; }
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

uint64_t TVPGetCurrentThreadID() { return (uint64_t)pthread_self(); }
void TVPSleepFor(uint32_t ms) { usleep(ms * 1000); }

// ─── Critical section ──────────────────────────────────────────
struct tTJSCriticalSectionImpl
{
    pthread_mutex_t mutex;
    pthread_t tid;
    tTJSCriticalSectionImpl() : tid(0) { pthread_mutex_init(&mutex, nullptr); }
    ~tTJSCriticalSectionImpl() { pthread_mutex_destroy(&mutex); }
    bool lock()
    {
        pthread_t id = pthread_self();
        if (pthread_equal(tid, id)) return false;
        pthread_mutex_lock(&mutex);
        tid = id;
        return true;
    }
    void unlock() { tid = 0; pthread_mutex_unlock(&mutex); }
};

bool tTJSCriticalSection::lock() { return _impl->lock(); }
void tTJSCriticalSection::unlock() { _impl->unlock(); }
tTJSCriticalSection::tTJSCriticalSection() { _impl = new tTJSCriticalSectionImpl; }
tTJSCriticalSection::~tTJSCriticalSection() { delete _impl; }

tTJSCriticalSectionHolder::tTJSCriticalSectionHolder(tTJSCriticalSection& cs)
{ if (cs.lock()) _cs = &cs; else _cs = nullptr; }
tTJSCriticalSectionHolder::~tTJSCriticalSectionHolder() { if (_cs) _cs->unlock(); }

tTJSUniqueLock::tTJSUniqueLock(tTJSCriticalSection& cs) : owns(true)
{ if (cs.lock()) _cs = &cs; else _cs = nullptr; }
tTJSUniqueLock::~tTJSUniqueLock() { if (owns && _cs) _cs->unlock(); }
void tTJSUniqueLock::unlock() { if (owns) { owns = false; _cs->unlock(); } }
void tTJSUniqueLock::lock() { if (!owns) { owns = true; _cs->lock(); } }

// ─── Condition ─────────────────────────────────────────────────
tTVPCondition::tTVPCondition() { _impl = malloc(sizeof(pthread_cond_t)); pthread_cond_init((pthread_cond_t*)_impl, nullptr); }
tTVPCondition::~tTVPCondition() { pthread_cond_destroy((pthread_cond_t*)_impl); free(_impl); }
void tTVPCondition::notify_one() { pthread_cond_signal((pthread_cond_t*)_impl); }
void tTVPCondition::notify_all() { pthread_cond_broadcast((pthread_cond_t*)_impl); }
void tTVPCondition::Wait(tTJSCriticalSection& cs) { pthread_cond_wait((pthread_cond_t*)_impl, &cs._impl->mutex); }
bool tTVPCondition::WaitFor(tTJSCriticalSection& cs, unsigned int ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += (ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
    return pthread_cond_timedwait((pthread_cond_t*)_impl, &cs._impl->mutex, &ts) == 0;
}

// ─── SpinLock ──────────────────────────────────────────────────
void tTJSSpinLock::lock() { while (__sync_lock_test_and_set(&splock, 1)) {} }
void tTJSSpinLock::unlock() { __sync_lock_release(&splock); }
tTJSSpinLock::tTJSSpinLock() { unlock(); }
tTJSSpinLockHolder::tTJSSpinLockHolder(tTJSSpinLock& lock) { lock.lock(); Lock = &lock; }
tTJSSpinLockHolder::~tTJSSpinLockHolder() { if (Lock) Lock->unlock(); }
