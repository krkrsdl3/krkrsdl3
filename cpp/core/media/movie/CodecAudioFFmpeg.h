#pragma once
#include "KRMovieDef.h"
#include "CodecAudio.h"

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
}

NS_KRMOVIE_BEGIN

class CDVDAudioCodecFFmpeg : public CDVDAudioCodec
{
public:
    CDVDAudioCodecFFmpeg();
    virtual ~CDVDAudioCodecFFmpeg();
    virtual bool Open(CDVDStreamInfo& hints);
    virtual void Dispose();
    virtual bool AddData(const DemuxPacket& packet) override;
    virtual void GetData(DVDAudioFrame& frame) override;
    virtual void Reset();

protected:
    int GetData(uint8_t** dst);
    AVCodecContext* m_pCodecContext;
    AVFrame* m_pFrame;
};

NS_KRMOVIE_END
