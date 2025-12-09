#pragma once

#include "tjsNative.h"
#include "TVPEvent.h"

//---------------------------------------------------------------------------
// tTJSNI_BaseTimer
//---------------------------------------------------------------------------
class tTJSNI_BaseTimer : public tTJSNativeInstance {
    typedef tTJSNativeInstance inherited;

protected:
    iTJSDispatch2 *Owner;
    tTJSVariantClosure ActionOwner; // object to send action
    tjs_uint16 Counter; // serial number for event tag
    tjs_int Capacity; // max queue size for this timer object
    ttstr ActionName;
    tTVPAsyncTriggerMode Mode; // trigger mode

public:
    tTJSNI_BaseTimer();
    tjs_error TJS_INTF_METHOD Construct(tjs_int numparams, tTJSVariant **param,
                                        iTJSDispatch2 *tjs_obj);
    void TJS_INTF_METHOD Invalidate();

protected:
    void Fire(tjs_uint n);
    void CancelEvents();
    bool AreEventsInQueue();

public:
    tTJSVariantClosure GetActionOwnerNoAddRef() const { return ActionOwner; }
    ttstr &GetActionName() { return ActionName; }

    tjs_int GetCapacity() const { return Capacity; }
    void SetCapacity(tjs_int c) { Capacity = c; }

    tTVPAsyncTriggerMode GetMode() const { return Mode; }
    void SetMode(tTVPAsyncTriggerMode mode) { Mode = mode; }
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNI_Timer : Timer Native Instance
//---------------------------------------------------------------------------
class tTVPTimerThread;
class tTJSNI_Timer : public tTJSNI_BaseTimer {
    typedef tTJSNI_BaseTimer inherited;
    friend class tTVPTimerThread;

    tjs_uint64 Interval;
    tjs_uint64 NextTick;
    tjs_int PendingCount;
    bool Enabled;

public:
    tTJSNI_Timer();
    tjs_error TJS_INTF_METHOD Construct(tjs_int numparams, tTJSVariant **param,
                                        iTJSDispatch2 *tjs_obj);
    void TJS_INTF_METHOD Invalidate();

    void InternalSetInterval(tjs_uint64 n) { Interval = n; }
    void SetInterval(tjs_uint64 n);
    tjs_uint64 GetInterval() const;
    // { return Interval; }

    void ZeroPendingCount() { PendingCount = 0; }

    void SetNextTick(tjs_uint64 n) { NextTick = n; }
    tjs_uint64 GetNextTick() const;
    // { return NextTick; }
    // bcc 5.5.1 has an inliner bug of bad returning of int64...

    void InternalSetEnabled(bool b) { Enabled = b; }
    void SetEnabled(bool b);
    bool GetEnabled() const { return Enabled; }

    void Trigger(tjs_uint n);
    void FirePendingEventsAndClear();

private:
    void CancelTrigger();
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_Timer : TJS Timer class
//---------------------------------------------------------------------------
class tTJSNC_Timer : public tTJSNativeClass
{
	typedef tTJSNativeClass inherited;

public:
	tTJSNC_Timer();
	static tjs_uint32 ClassID;

protected:
	tTJSNativeInstance* CreateNativeInstance();
	/*
		implement this in each platform.
		this must return a proper instance of tTJSNI_Timer.
	*/
};
//---------------------------------------------------------------------------
extern tTJSNativeClass* TVPCreateNativeClass_Timer();
/*
	implement this in each platform.
	this must return a proper instance of tTJSNI_Timer.
	usually simple returns: new tTJSNC_Timer();
*/
//---------------------------------------------------------------------------