#include "VideoReferenceClock.h"

#include "tjsCommHead.h"
#include "TickCount.h"

NS_KRMOVIE_BEGIN

CDVDClock::CDVDClock()
{
    std::unique_lock<std::recursive_mutex> lock(m_critSection);

    m_pauseClock = 0;
    m_bReset = true;
    m_paused = false;
    m_iDisc = 0;
    m_startClock = 0;
    m_frameTime = DVD_TIME_BASE / 60.0;
    m_speedAfterPause = 0;
    m_systemUsed = 1000;
    m_systemOffset = TVPGetRoughTickCount32();
}

CDVDClock::~CDVDClock()
{
}

double CDVDClock::GetClock()
{
    std::unique_lock<std::recursive_mutex> lock(m_critSection);
    return SystemToPlaying(TVPGetRoughTickCount32());
}

double CDVDClock::GetClock(double& absolute)
{
    std::unique_lock<std::recursive_mutex> lock(m_critSection);
    int64_t current = TVPGetRoughTickCount32();
    absolute = SystemToAbsolute(current);
    return SystemToPlaying(current);
}

double CDVDClock::GetAbsoluteClock()
{
    std::unique_lock<std::recursive_mutex> lock(m_critSection);
    return SystemToAbsolute(TVPGetRoughTickCount32());
}

void CDVDClock::SetSpeed(int iSpeed)
{
    std::unique_lock<std::recursive_mutex> lock(m_critSection);

    if (m_paused)
    {
        m_speedAfterPause = iSpeed;
        return;
    }

    if (iSpeed == DVD_PLAYSPEED_PAUSE)
    {
        if (!m_pauseClock)
            m_pauseClock = TVPGetRoughTickCount32();
        return;
    }

    int64_t current = TVPGetRoughTickCount32();
    int64_t newfreq = 1000 * DVD_PLAYSPEED_NORMAL / iSpeed;

    if (m_pauseClock)
    {
        m_startClock += current - m_pauseClock;
        m_pauseClock = 0;
    }

    m_startClock = current - (int64_t)((double)(current - m_startClock) * newfreq / m_systemUsed);
    m_systemUsed = newfreq;
}

double CDVDClock::GetClockSpeed()
{
    std::unique_lock<std::recursive_mutex> lock(m_critSection);
    return (double)1000 / m_systemUsed;
}

void CDVDClock::Discontinuity(double clock, double absolute)
{
    std::unique_lock<std::recursive_mutex> lock(m_critSection);
    m_startClock = AbsoluteToSystem(absolute);
    if (m_pauseClock)
        m_pauseClock = m_startClock;
    m_iDisc = clock;
    m_bReset = false;
}

int CDVDClock::UpdateFramerate(double fps, double* interval)
{
    if (fps == 0.0)
        return -1;

    m_frameTime = 1 / fps * DVD_TIME_BASE;

    return -1;
}

void CDVDClock::Pause(bool pause)
{
    std::unique_lock<std::recursive_mutex> lock(m_critSection);

    if (pause && !m_paused)
    {
        if (!m_pauseClock)
            m_speedAfterPause = 1000 * DVD_PLAYSPEED_NORMAL / m_systemUsed;
        else
            m_speedAfterPause = DVD_PLAYSPEED_PAUSE;

        SetSpeed(DVD_PLAYSPEED_PAUSE);
        m_paused = true;
    }
    else if (!pause && m_paused)
    {
        m_paused = false;
        SetSpeed(m_speedAfterPause);
    }
}

double CDVDClock::SystemToAbsolute(int64_t system)
{
    return DVD_TIME_BASE * (double)(system - m_systemOffset) / 1000;
}

int64_t CDVDClock::AbsoluteToSystem(double absolute)
{
    return (int64_t)(absolute / DVD_TIME_BASE * 1000) + m_systemOffset;
}

double CDVDClock::SystemToPlaying(int64_t system)
{
    int64_t current;

    if (m_bReset)
    {
        m_startClock = system;
        m_systemUsed = 1000;
        if (m_pauseClock)
            m_pauseClock = m_startClock;
        m_iDisc = 0;
        m_bReset = false;
    }

    if (m_pauseClock)
        current = m_pauseClock;
    else
        current = system;

    return DVD_TIME_BASE * (double)(current - m_startClock) / m_systemUsed + m_iDisc;
}

NS_KRMOVIE_END
