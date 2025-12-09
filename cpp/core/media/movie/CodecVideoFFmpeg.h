#pragma once
#include "KRStreamInfo.h"
#include "CodecVideo.h"
#include "KRMovieDef.h"

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
}

NS_KRMOVIE_BEGIN

class CDVDVideoCodecFFmpeg : public CDVDVideoCodec
{
public:
    CDVDVideoCodecFFmpeg();
    virtual ~CDVDVideoCodecFFmpeg();
    virtual bool Open(CDVDStreamInfo& hints) override;
    virtual bool AddData(const DemuxPacket& packet) override;
    virtual void Reset() override;
    virtual bool GetPicture(DVDVideoPicture* pDvdVideoPicture) override;
    virtual void SetDropState(bool bDrop) override;
    virtual unsigned GetConvergeCount() override;
    virtual bool GetCodecStats(double& pts, int& droppedFrames) override;

protected:
    bool SetPictureParams(DVDVideoPicture* pVideoPicture);
    void Dispose();
    static enum AVPixelFormat GetFormat(struct AVCodecContext* avctx, const AVPixelFormat* fmt);

    AVFrame* m_pFrame;
    AVCodecContext* m_pCodecContext;

    int m_iLastKeyframe;
    double m_dts;
    bool m_started;
    double m_decoderPts;
    int m_droppedFrames;

    struct CDropControl
    {
        CDropControl();
        void Reset(bool init);
        void Process(int64_t pts, bool drop);

        int64_t m_lastPTS;
        int64_t m_diffPTS;
        int m_count;
        enum
        {
            INIT,
            VALID
        } m_state;
    } m_dropCtrl;
};

NS_KRMOVIE_END
