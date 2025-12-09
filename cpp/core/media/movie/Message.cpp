#include "tjsCommHead.h"
#include "Message.h"
#include "TVPTimer.h"
#include "VideoReferenceClock.h"
#include "CodecDemux.h"
#include <condition_variable>

extern "C"
{
#include "libavcodec/avcodec.h"
}

NS_KRMOVIE_BEGIN

class CDVDMsgGeneralSynchronizePriv
{
public:
    CDVDMsgGeneralSynchronizePriv(unsigned int timeout, unsigned int sources)
      : sources(sources),
        reached(0),
        timeout(timeout)
    {
    }
    unsigned int sources;
    unsigned int reached;
    std::recursive_mutex section;
    std::condition_variable_any condition;
    TVPElapsedTimer timeout;
};

/**
 * CDVDMsgGeneralSynchronize --- GENERAL_SYNCRONIZR
 */
CDVDMsgGeneralSynchronize::CDVDMsgGeneralSynchronize(unsigned int timeout, unsigned int sources)
  : CDVDMsg(GENERAL_SYNCHRONIZE),
    m_p(new CDVDMsgGeneralSynchronizePriv(timeout, sources))
{
}

CDVDMsgGeneralSynchronize::~CDVDMsgGeneralSynchronize()
{
    delete m_p;
}

bool CDVDMsgGeneralSynchronize::Wait(unsigned int milliseconds, unsigned int source)
{
    std::unique_lock<std::recursive_mutex> lock(m_p->section);

    TVPElapsedTimer timeout(milliseconds);

    m_p->reached |= (source & m_p->sources);
    if ((m_p->sources & SYNCSOURCE_ANY) && source)
        m_p->reached |= SYNCSOURCE_ANY;

    m_p->condition.notify_all();

    while (m_p->reached != m_p->sources)
    {
        milliseconds = std::min(m_p->timeout.MillisLeft(), timeout.MillisLeft());
        if (!milliseconds)
            milliseconds = 1;
        if (m_p->condition.wait_for(lock, std::chrono::milliseconds(milliseconds)) !=
            std::cv_status::timeout)
            continue;

        if (m_p->timeout.IsTimePast())
        {
            return true; // global timeout, we are done
        }
        if (timeout.IsTimePast())
        {
            return false; /* request timeout, should be retried */
        }
    }
    return true;
}

void CDVDMsgGeneralSynchronize::Wait(std::atomic<bool>& abort, unsigned int source)
{
    while (!Wait(100, source) && !abort)
        ;
}

long CDVDMsgGeneralSynchronize::Release()
{
    m_p->section.lock();
    intptr_t count = --m_refs;
    m_p->condition.notify_all();
    m_p->section.unlock();
    if (count == 0)
        delete this;
    return count;
}

CDVDMsgDemuxerPacket::CDVDMsgDemuxerPacket(DemuxPacket* packet, bool drop) : CDVDMsg(DEMUXER_PACKET)
{
    m_packet = packet;
    m_drop = drop;
}

CDVDMsgDemuxerPacket::~CDVDMsgDemuxerPacket()
{
    if (m_packet)
        DemuxPacket::Free(m_packet);
}

unsigned int CDVDMsgDemuxerPacket::GetPacketSize()
{
    if (m_packet)
        return m_packet->iSize;
    else
        return 0;
}

NS_KRMOVIE_END
