#include "tjsNativeTimer.h"

#include "TVPSystem.h"
#include "PlatformThread.h"
#include "PlatformMutex.h"
#include "NativeEventQueue.h"
#include "TVPSettings.h"
#include "Platform.h"

#include <algorithm>

bool TVPLimitTimerCapacity = false;

#define TVP_DEFAULT_TIMER_CAPACITY 6

// the timer has sub-milliseconds precision by fixed-point real.
#define TVP_SUBMILLI_FRAC_BITS 16

//---------------------------------------------------------------------------
// tTJSNI_BaseTimer
//---------------------------------------------------------------------------
tTJSNI_BaseTimer::tTJSNI_BaseTimer()
{
    Owner = NULL;
    Counter = 0;
    Capacity = TVP_DEFAULT_TIMER_CAPACITY;
    ActionOwner.Object = ActionOwner.ObjThis = NULL;
    ActionName = TVPActionName;
    Mode = atmNormal;
}
//---------------------------------------------------------------------------
tjs_error tTJSNI_BaseTimer::Construct(tjs_int numparams,
                                      tTJSVariant** param,
                                      iTJSDispatch2* tjs_obj)
{
    if (numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    tjs_error hr = inherited::Construct(numparams, param, tjs_obj);
    if (TJS_FAILED(hr))
        return hr;

    if (numparams >= 2 && param[1]->Type() != tvtVoid)
        ActionName = *param[1]; // action function to be called

    ActionOwner = param[0]->AsObjectClosure();
    Owner = tjs_obj;

    return TJS_S_OK;
}
//---------------------------------------------------------------------------
void tTJSNI_BaseTimer::Invalidate()
{
    TVPCancelSourceEvents(Owner);
    Owner = NULL;

    ActionOwner.Release();
    ActionOwner.ObjThis = ActionOwner.Object = NULL;

    inherited::Invalidate();
}
//---------------------------------------------------------------------------
void tTJSNI_BaseTimer::Fire(tjs_uint n)
{
    if (!Owner)
        return;
    static ttstr eventname(TJS_N("onTimer"));

    tjs_int count = TVPCountEventsInQueue(Owner, Owner, eventname, 0);

    tjs_int cap = TVPLimitTimerCapacity ? 1 : (Capacity == 0 ? 65535 : Capacity);
    // 65536 should be considered as to be no-limit.

    tjs_int more = cap - count;

    if (more > 0)
    {
        if (n > (tjs_uint)more)
            n = more;
        if (Owner)
        {
            tjs_uint32 tag = 1 + ((tjs_uint32)Counter << 1);
            tjs_uint32 flags = TVP_EPT_POST | TVP_EPT_DISCARDABLE;
            switch (Mode)
            {
                case atmNormal:
                    flags |= TVP_EPT_NORMAL;
                    break;
                case atmExclusive:
                    flags |= TVP_EPT_EXCLUSIVE;
                    break;
                case atmAtIdle:
                    flags |= TVP_EPT_IDLE;
                    break;
            }
            while (n--)
            {
                TVPPostEvent(Owner, Owner, eventname, tag, flags, 0, NULL);
            }
        }

        Counter++;
    }
}
//---------------------------------------------------------------------------
void tTJSNI_BaseTimer::CancelEvents()
{
    // cancel all events
    if (Owner)
    {
        static ttstr eventname(TJS_N("onTimer"));
        TVPCancelEvents(Owner, Owner, eventname, 0);
    }
}
//---------------------------------------------------------------------------
bool tTJSNI_BaseTimer::AreEventsInQueue()
{
    // are events in event queue ?

    if (Owner)
    {
        static ttstr eventname(TJS_N("onTimer"));
        return TVPAreEventsInQueue(Owner, Owner, eventname, 0);
    }
    return 0;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------

// TVP Timer class gives ability of triggering event on punctual interval.
// a large quantity of event at once may easily cause freeze to system,
// so we must trigger only porocess-able quantity of the event.
#define TVP_LEAST_TIMER_INTERVAL 3
#define INFINITE 0xFFFFFFFF

//---------------------------------------------------------------------------
// tTVPTimerThread
//---------------------------------------------------------------------------
class tTVPTimerThread : public tTVPThread
{
    // thread for triggering punctual event.
    // normal Windows timer cannot call the timer callback routine at
    // too short interval ( roughly less than 50ms ).

    std::vector<tTJSNI_Timer*> List;
    std::vector<tTJSNI_Timer*> Pending; // timer object which has pending events
    bool PendingEventsAvailable;
    tTVPThreadEvent Event;

    NativeEventQueue<tTVPTimerThread> EventQueue;

public:
    tTJSCriticalSection TVPTimerCS;

    tTVPTimerThread();
    ~tTVPTimerThread();

protected:
    void Execute();

private:
    void Proc(NativeEvent& event);

    void AddItem(tTJSNI_Timer* item);
    bool RemoveItem(tTJSNI_Timer* item);
    void RemoveFromPendingItem(tTJSNI_Timer* item);
    void RegisterToPendingItem(tTJSNI_Timer* item);

public:
    void SetEnabled(tTJSNI_Timer* item, bool enabled); // managed by this class
    void SetInterval(tTJSNI_Timer* item,
                     tjs_uint64 interval); // managed by this class

public:
    static void Init();
    static void Uninit();

    static void Add(tTJSNI_Timer* item);
    static void Remove(tTJSNI_Timer* item);

    static void RemoveFromPending(tTJSNI_Timer* item);
    static void RegisterToPending(tTJSNI_Timer* item);

} static* TVPTimerThread = NULL;
//---------------------------------------------------------------------------
tTVPTimerThread::tTVPTimerThread() : tTVPThread(), EventQueue(this, &tTVPTimerThread::Proc)
{
    PendingEventsAvailable = false;
    SetPriority(TVPLimitTimerCapacity ? ttpNormal : ttpHighest);
    EventQueue.Allocate();
    Resume();
}
//---------------------------------------------------------------------------
tTVPTimerThread::~tTVPTimerThread()
{
    Terminate();
    Resume();
    Event.Set();
    WaitFor();
    EventQueue.Deallocate();
}
//---------------------------------------------------------------------------
void tTVPTimerThread::Execute()
{
    while (!GetTerminated())
    {
        tjs_uint64 step_next = (tjs_uint64)(tjs_int64)-1L; // invalid value
        tjs_uint64 curtick = TVPGetTickCount() << TVP_SUBMILLI_FRAC_BITS;
        tjs_uint32 sleeptime;

        { // thread-protected
            tTJSCriticalSectionHolder holder(TVPTimerCS);

            bool any_triggered = false;

            std::vector<tTJSNI_Timer*>::iterator i;
            for (i = List.begin(); i != List.end(); i++)
            {
                tTJSNI_Timer* item = *i;

                if (!item->GetEnabled() || item->GetInterval() == 0)
                    continue;

                if (item->GetNextTick() < curtick)
                {
                    tjs_uint n = static_cast<tjs_uint>((curtick - item->GetNextTick()) /
                                                       item->GetInterval());
                    n++;
                    if (n > 40)
                    {
                        // too large amount of event at once; discard rest
                        item->Trigger(1);
                        any_triggered = true;
                        item->SetNextTick(curtick + item->GetInterval());
                    }
                    else
                    {
                        item->Trigger(n);
                        any_triggered = true;
                        item->SetNextTick(item->GetNextTick() + n * item->GetInterval());
                    }
                }

                tjs_uint64 to_next = item->GetNextTick() - curtick;

                if (step_next == (tjs_uint64)(tjs_int64)-1L)
                {
                    step_next = to_next;
                }
                else
                {
                    if (step_next > to_next)
                        step_next = to_next;
                }
            }

            if (step_next != (tjs_uint64)(tjs_int64)-1L)
            {
                // too large step_next must be diminished to size of DWORD.
                if (step_next >= 0x80000000)
                    sleeptime = 0x7fffffff; // smaller value than step_next is OK
                else
                    sleeptime = static_cast<tjs_uint32>(step_next);
            }
            else
            {
                sleeptime = INFINITE;
            }

            if (List.size() == 0)
                sleeptime = INFINITE;

            if (any_triggered)
            {
                // triggered; post notification message to the UtilWindow
                if (!PendingEventsAvailable)
                {
                    PendingEventsAvailable = true;
                    EventQueue.PostEvent(NativeEvent(TVP_EV_TIMER_THREAD));
                }
            }

        } // end-of-thread-protected

        // now, sleeptime has sub-milliseconds precision but we need millisecond
        // precision time.
        if (sleeptime != INFINITE)
            sleeptime = (sleeptime >> TVP_SUBMILLI_FRAC_BITS) +
                        (sleeptime & ((1 << TVP_SUBMILLI_FRAC_BITS) - 1) ? 1 : 0); // round up

        // clamp to TVP_LEAST_TIMER_INTERVAL ...
        if (sleeptime != INFINITE && sleeptime < TVP_LEAST_TIMER_INTERVAL)
            sleeptime = TVP_LEAST_TIMER_INTERVAL;

        Event.WaitFor(sleeptime); // wait until sleeptime is elapsed or
        // Event->SetEvent() is executed.
    }
}
//---------------------------------------------------------------------------
// void __fastcall tTVPTimerThread::UtilWndProc(Messages::TMessage &Msg)
void tTVPTimerThread::Proc(NativeEvent& ev)
{
    // Window procedure of UtilWindow
    if (ev.Message == TVP_EV_TIMER_THREAD && !GetTerminated())
    {
        // pending events occur
        tTJSCriticalSectionHolder holder(TVPTimerCS); // protect the object

        std::vector<tTJSNI_Timer*>::iterator i;
        for (i = Pending.begin(); i != Pending.end(); i++)
        {
            tTJSNI_Timer* item = *i;
            item->FirePendingEventsAndClear();
        }

        Pending.clear();
        PendingEventsAvailable = false;
    }
    else
    {
        EventQueue.HandlerDefault(ev);
    }
}
//---------------------------------------------------------------------------
void tTVPTimerThread::AddItem(tTJSNI_Timer* item)
{
    tTJSCriticalSectionHolder holder(TVPTimerCS);

    if (std::find(List.begin(), List.end(), item) == List.end())
        List.push_back(item);
}
//---------------------------------------------------------------------------
bool tTVPTimerThread::RemoveItem(tTJSNI_Timer* item)
{
    tTJSCriticalSectionHolder holder(TVPTimerCS);

    std::vector<tTJSNI_Timer*>::iterator i;

    // remove from the List
    for (i = List.begin(); i != List.end(); /**/)
    {
        if (*i == item)
            i = List.erase(i);
        else
            i++;
    }

    // also remove from the Pending list
    RemoveFromPendingItem(item);

    return List.size() != 0;
}
//---------------------------------------------------------------------------
void tTVPTimerThread::RemoveFromPendingItem(tTJSNI_Timer* item)
{
    // remove item from pending list
    std::vector<tTJSNI_Timer*>::iterator i;
    for (i = Pending.begin(); i != Pending.end(); /**/)
    {
        if (*i == item)
            i = Pending.erase(i);
        else
            i++;
    }

    item->ZeroPendingCount();
}
//---------------------------------------------------------------------------
void tTVPTimerThread::RegisterToPendingItem(tTJSNI_Timer* item)
{
    // register item to the pending list
    Pending.push_back(item);
}
//---------------------------------------------------------------------------
void tTVPTimerThread::SetEnabled(tTJSNI_Timer* item, bool enabled)
{
    { // thread-protected
        tTJSCriticalSectionHolder holder(TVPTimerCS);

        item->InternalSetEnabled(enabled);
        if (enabled)
        {
            item->SetNextTick((TVPGetTickCount() << TVP_SUBMILLI_FRAC_BITS) + item->GetInterval());
        }
        else
        {
            item->CancelEvents();
            item->ZeroPendingCount();
        }
    } // end-of-thread-protected

    if (enabled)
        Event.Set();
}
//---------------------------------------------------------------------------
void tTVPTimerThread::SetInterval(tTJSNI_Timer* item, tjs_uint64 interval)
{
    { // thread-protected
        tTJSCriticalSectionHolder holder(TVPTimerCS);

        item->InternalSetInterval(interval);
        if (item->GetEnabled())
        {
            item->CancelEvents();
            item->ZeroPendingCount();
            item->SetNextTick((TVPGetTickCount() << TVP_SUBMILLI_FRAC_BITS) + item->GetInterval());
        }
    } // end-of-thread-protected

    if (item->GetEnabled())
        Event.Set();
}
//---------------------------------------------------------------------------
void tTVPTimerThread::Init()
{
    if (!TVPTimerThread)
    {
        TVPTimerThread = new tTVPTimerThread();
    }
}
//---------------------------------------------------------------------------
void tTVPTimerThread::Uninit()
{
    if (TVPTimerThread)
    {
        delete TVPTimerThread;
        TVPTimerThread = NULL;
    }
}
//---------------------------------------------------------------------------
static tTVPAtExit TVPTimerThreadUninitAtExit(TVP_ATEXIT_PRI_SHUTDOWN, tTVPTimerThread::Uninit);
//---------------------------------------------------------------------------
void tTVPTimerThread::Add(tTJSNI_Timer* item)
{
    // at this point, item->GetEnebled() must be false.

    Init();

    TVPTimerThread->AddItem(item);
}
//---------------------------------------------------------------------------
void tTVPTimerThread::Remove(tTJSNI_Timer* item)
{
    if (TVPTimerThread)
    {
        if (!TVPTimerThread->RemoveItem(item))
            Uninit();
    }
}
//---------------------------------------------------------------------------
void tTVPTimerThread::RemoveFromPending(tTJSNI_Timer* item)
{
    if (TVPTimerThread)
    {
        TVPTimerThread->RemoveFromPendingItem(item);
    }
}
//---------------------------------------------------------------------------
void tTVPTimerThread::RegisterToPending(tTJSNI_Timer* item)
{
    if (TVPTimerThread)
    {
        TVPTimerThread->RegisterToPendingItem(item);
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNI_Timer
//---------------------------------------------------------------------------
tTJSNI_Timer::tTJSNI_Timer()
{
    NextTick = 0;
    Interval = 1000;
    PendingCount = 0;
    Enabled = false;
}
//---------------------------------------------------------------------------
tjs_error tTJSNI_Timer::Construct(tjs_int numparams, tTJSVariant** param, iTJSDispatch2* tjs_obj)
{
    inherited::Construct(numparams, param, tjs_obj);

    tTVPTimerThread::Add(this);
    return TJS_S_OK;
}
//---------------------------------------------------------------------------
void tTJSNI_Timer::Invalidate()
{
    tTVPTimerThread::Remove(this);
    ZeroPendingCount();
    CancelEvents();
    inherited::Invalidate(); // this sets Owner = NULL
}
//---------------------------------------------------------------------------
void tTJSNI_Timer::SetInterval(tjs_uint64 n)
{
    TVPTimerThread->SetInterval(this, n);
}
//---------------------------------------------------------------------------
tjs_uint64 tTJSNI_Timer::GetInterval() const
{
    return Interval;
}
//---------------------------------------------------------------------------
tjs_uint64 tTJSNI_Timer::GetNextTick() const
{
    return NextTick;
}
//---------------------------------------------------------------------------
void tTJSNI_Timer::SetEnabled(bool b)
{
    TVPTimerThread->SetEnabled(this, b);
}
//---------------------------------------------------------------------------
void tTJSNI_Timer::Trigger(tjs_uint n)
{
    // this function is called by sub-thread.
    if (PendingCount == 0)
        tTVPTimerThread::RegisterToPending(this);
    PendingCount += n;
}
//---------------------------------------------------------------------------
void tTJSNI_Timer::FirePendingEventsAndClear()
{
    // fire all pending events and clear the pending event count
    if (PendingCount)
    {
        Fire(PendingCount);
        ZeroPendingCount();
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_Timer
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_Timer::ClassID = -1;
tTJSNC_Timer::tTJSNC_Timer() : inherited(TJS_N("Timer"))
{
    // registration of native members

    TJS_BEGIN_NATIVE_MEMBERS(Timer) // constructor
    TJS_DECL_EMPTY_FINALIZE_METHOD
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(/*var.name*/ _this, /*var.type*/ tTJSNI_Timer,
                                      /*TJS class name*/ Timer)
    {
        return TJS_S_OK;
    }
    TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/ Timer)
    //----------------------------------------------------------------------

    //-- methods

    //----------------------------------------------------------------------
    //----------------------------------------------------------------------

    //-- events

    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onTimer)
    {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Timer);

        tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
        if (obj.Object)
        {
            ttstr& actionname = _this->GetActionName();
            TVP_ACTION_INVOKE_BEGIN(0, "onTimer", objthis);
            TVP_ACTION_INVOKE_END_NAME(obj, actionname.IsEmpty() ? NULL : actionname.c_str(),
                                       actionname.IsEmpty() ? NULL : actionname.GetHint());
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onTimer)
    //----------------------------------------------------------------------

    //--properties

    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_PROP_DECL(interval){TJS_BEGIN_NATIVE_PROP_GETTER{
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Timer);
    /*
            bcc 5.5.1 has an inliner bug which cannot treat 64bit integer return
            value properly in some occasions.
            OK : tjs_uint64 interval = _this->GetInterval(); *result = (tjs_int64)interval;
            NG : *result = (tjs_int64)interval;
    */
    double interval = _this->GetInterval() * (1.0 / (1 << TVP_SUBMILLI_FRAC_BITS));
    *result = interval;
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Timer);
    double interval = (double)*param * (1 << TVP_SUBMILLI_FRAC_BITS);
    _this->SetInterval((tjs_int64)(interval + 0.5));
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(interval)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(enabled){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Timer);
*result = _this->GetEnabled();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Timer);
    _this->SetEnabled(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(enabled)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(capacity){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Timer);
*result = _this->GetCapacity();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Timer);
    _this->SetCapacity(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(capacity)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(mode){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Timer);
*result = (tjs_int)_this->GetMode();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_Timer);
    _this->SetMode((tTVPAsyncTriggerMode)(tjs_int)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(mode)
//----------------------------------------------------------------------

TJS_END_NATIVE_MEMBERS

tTJSVariant val;
if (TVPGetCommandLine(TJS_N("-laxtimer"), &val))
{
    ttstr str(val);
    if (str == TJS_N("yes"))
        TVPLimitTimerCapacity = true;
}
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_Timer
//---------------------------------------------------------------------------
tTJSNativeInstance* tTJSNC_Timer::CreateNativeInstance()
{
    return new tTJSNI_Timer();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPCreateNativeClass_Timer
//---------------------------------------------------------------------------
tTJSNativeClass* TVPCreateNativeClass_Timer()
{
    return new tTJSNC_Timer();
}
//---------------------------------------------------------------------------