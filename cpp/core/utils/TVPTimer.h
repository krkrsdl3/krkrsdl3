//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Timer Object Interface
//---------------------------------------------------------------------------
#ifndef TimerIntfH
#define TimerIntfH

#include "tjsNative.h"
#include "TVPEvent.h"
#include "TickCount.h"


//---------------------------------------------------------------------------
// global variables
//---------------------------------------------------------------------------
extern bool TVPLimitTimerCapacity;
// limit timer capacity to one
//---------------------------------------------------------------------------

class TVPTimerEventIntarface {
public:
	virtual ~TVPTimerEventIntarface() {}
	virtual void Handle() = 0;
};

template<typename T>
class TVPTimerEvent : public TVPTimerEventIntarface {
	void (T::* handler_)();
	T* owner_;

public:
	TVPTimerEvent(T* owner, void (T::* Handler)()) : owner_(owner), handler_(Handler) {}
	void Handle() { (owner_->*handler_)(); }
};
struct tTVPTimerImpl;
class TVPTimer {
	TVPTimerEventIntarface* event_;
	int		interval_;
	bool	enabled_;

	void UpdateTimer();

	void FireEvent() {
		if (event_) {
			event_->Handle();
		}
	}

	friend struct tTVPTimerImpl;
	tTVPTimerImpl* impl_;

public:
	TVPTimer();
	~TVPTimer();

	template<typename T>
	void SetOnTimerHandler(T* owner, void (T::* Handler)()) {
		if (event_) delete event_;
		event_ = new TVPTimerEvent<T>(owner, Handler);
		UpdateTimer();
	}

	void SetInterval(int i) {
		if (interval_ != i) {
			interval_ = i;
			UpdateTimer();
		}
	}
	int GetInterval() const {
		return interval_;
	}
	void SetEnabled(bool b) {
		if (enabled_ != b) {
			enabled_ = b;
			UpdateTimer();
		}
	}
	bool GetEnable() const { return enabled_; }

	static void ProgressAllTimer();
};

//---------------------------------------------------------------------------
class TVPElapsedTimer {
    unsigned int startTime;
    unsigned int totalWaitTime;
    bool isInfiniteValue = false;

public:
    inline TVPElapsedTimer() : startTime(0), totalWaitTime(0) {}
    inline TVPElapsedTimer(unsigned int millisecondsIntoTheFuture) :
        startTime(TVPGetRoughTickCount32()),
        totalWaitTime(millisecondsIntoTheFuture) {}

    inline void Set(unsigned int millisecondsIntoTheFuture) {
        startTime = TVPGetRoughTickCount32();
        totalWaitTime = millisecondsIntoTheFuture;
    }
    inline bool IsTimePast() const {
        if(isInfiniteValue)
            return false;
        return (totalWaitTime == 0
                   ? true
                   : (TVPGetRoughTickCount32() - startTime) >= totalWaitTime);
    }

    inline unsigned int MillisLeft() const {
        if(isInfiniteValue)
            return UINT_MAX;
        if(totalWaitTime == 0)
            return 0;
        unsigned int timeWaitedAlready = (TVPGetRoughTickCount32() - startTime);
        return (timeWaitedAlready >= totalWaitTime)
            ? 0
            : (totalWaitTime - timeWaitedAlready);
    }

    inline void SetExpired() { totalWaitTime = 0; }
    inline void SetInfinite() { isInfiniteValue = true; }
    inline bool IsInfinite(void) const { return isInfiniteValue;
    }
    inline unsigned int GetInitialTimeoutValue(void) const {
        return totalWaitTime;
    }
    inline unsigned int GetStartTime(void) const { return startTime; }
};

//---------------------------------------------------------------------------
#endif