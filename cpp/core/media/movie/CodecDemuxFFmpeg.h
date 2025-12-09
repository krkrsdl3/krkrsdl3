#pragma once
#include "CodecDemux.h"
#include <mutex>
#include "TVPTimer.h"

NS_KRMOVIE_BEGIN

enum class TRANSPORT_STREAM_STATE
{
    NONE,
    READY,
    NOTREADY,
};

class InputStream;

class CDVDDemuxFFmpeg : public IDemux
{
public:
    CDVDDemuxFFmpeg();
    virtual ~CDVDDemuxFFmpeg();

    bool Open(InputStream* pInput, bool fileinfo = false);
    void Dispose();
    virtual void Reset() override;
    virtual void Flush() override;
    virtual void Abort() override;
    virtual void SetSpeed(int iSpeed) override;
    virtual DemuxPacket* Read() override;
    virtual bool SeekTime(int time, bool backwords = false, double* startpts = NULL) override;
    virtual int GetStreamLength() override;
    virtual AVStream* GetStream(int iStreamId) const override;
    virtual std::vector<AVStream*> GetStreams() const override;
    virtual int GetNrOfStreams() const override;
    virtual std::string GetStreamCodecName(int iStreamId) override;

    bool Aborted();

    AVFormatContext* m_pFormatContext;
    InputStream* m_pInput = nullptr;

protected:
    TRANSPORT_STREAM_STATE TransportStreamAudioState();
    TRANSPORT_STREAM_STATE TransportStreamVideoState();
    bool IsTransportStreamReady();
    void ResetVideoStreams();

    double ConvertTimestamp(int64_t pts, int den, int num);
    bool IsProgramChange();

    std::recursive_mutex m_critSection;

    AVIOContext* m_ioContext;

    double m_currentPts; // used for stream length estimation
    bool m_bMatroska;
    bool m_bAVI;
    int m_speed;
    unsigned m_program;
    TVPElapsedTimer m_timeout;
    int m_seekStream;

    // Due to limitations of ffmpeg, we only can detect a program change
    // with a packet. This struct saves the packet for the next read and
    // signals STREAMCHANGE to player
    struct
    {
        AVPacket pkt; // packet ffmpeg returned
        int result;   // result from av_read_packet
    } m_pkt;

    bool m_checkTransportStream;
    double m_startTime = 0;
};

NS_KRMOVIE_END
