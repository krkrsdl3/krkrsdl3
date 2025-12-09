#pragma once
#include "KRMovieDef.h"
#include "KRStreamInfo.h"

extern "C"
{
#include "libavcodec/avcodec.h"
}

NS_KRMOVIE_BEGIN

typedef struct stDVDAudioFrame
{
    uint8_t* data[16];
    double pts;
    bool hasTimestamp;
    double duration;
    unsigned int nb_frames;
    unsigned int framesize;
    unsigned int planes;

    enum AVSampleFormat m_dataFormat;
    unsigned int m_sampleRate;
    int m_channelCount;
    unsigned int m_frames;
    unsigned int m_frameSize;

    int bits_per_sample;
} DVDAudioFrame;

class CDVDAudioCodec
{
public:
    CDVDAudioCodec() {}
    virtual ~CDVDAudioCodec() {}

    /*
     * Open the decoder, returns true on success
     */
    virtual bool Open(CDVDStreamInfo& hints) = 0;

    /*
     * Dispose, Free all resources
     */
    virtual void Dispose() = 0;

    /*
     * returns false on error
     *
     */
    virtual bool AddData(const DemuxPacket& packet) = 0;

    /*
     * the data is valid until the next call
     */
    virtual void GetData(DVDAudioFrame& frame) = 0;

    /*
     * resets the decoder
     */
    virtual void Reset() = 0;
};

NS_KRMOVIE_END
