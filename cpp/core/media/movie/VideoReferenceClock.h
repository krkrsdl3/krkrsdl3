#pragma once
#include "KRMovieDef.h"
#include <mutex>

NS_KRMOVIE_BEGIN

#define DVD_TIME_BASE 1000000
#define DVD_NOPTS_VALUE 0xFFF0000000000000

#define DVD_TIME_TO_MSEC(x) ((int)((double)(x) * 1000 / DVD_TIME_BASE))
#define DVD_SEC_TO_TIME(x) ((double)(x) * DVD_TIME_BASE)
#define DVD_MSEC_TO_TIME(x) ((double)(x) * DVD_TIME_BASE / 1000)

#define DVD_PLAYSPEED_PAUSE 0 // frame stepping
#define DVD_PLAYSPEED_NORMAL 1000

class CDVDClock
{
public:
    CDVDClock();
    ~CDVDClock();

    double GetClock();
    double GetClock(double& absolute);
    double GetAbsoluteClock();
    void Reset() { m_bReset = true; }
    void SetSpeed(int iSpeed);
    double GetClockSpeed();
    void Discontinuity(double clock, double absolute);
    void Discontinuity(double clock = 0LL) { Discontinuity(clock, GetAbsoluteClock()); }
    int UpdateFramerate(double fps, double* interval = NULL);
    void Pause(bool pause);

protected:
    double SystemToAbsolute(int64_t system);
    int64_t AbsoluteToSystem(double absolute);
    double SystemToPlaying(int64_t system);

    std::recursive_mutex m_critSection;
    int64_t m_systemUsed;
    int64_t m_startClock;
    int64_t m_pauseClock;
    double m_iDisc;
    bool m_bReset;
    bool m_paused;
    int m_speedAfterPause;
    int64_t m_systemOffset;
    double m_frameTime;
};

NS_KRMOVIE_END
