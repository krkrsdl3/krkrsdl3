#include "tjsCommHead.h"
#include "CodecVideoFFmpeg.h"

extern "C"
{
#include "libavutil/pixdesc.h"
}
#include "VideoReferenceClock.h"
#include "TVPThread.h"
#include "Message.h"

NS_KRMOVIE_BEGIN

enum DecoderState
{
    STATE_NONE,
    STATE_SW_SINGLE,
    STATE_HW_SINGLE,
    STATE_HW_FAILED,
    STATE_SW_MULTI
};

enum EFilterFlags
{
    FILTER_NONE = 0x0,
    FILTER_DEINTERLACE_YADIF = 0x1,    //< use first deinterlace mode
    FILTER_DEINTERLACE_ANY = 0xf,      //< use any deinterlace mode
    FILTER_DEINTERLACE_FLAGGED = 0x10, //< only deinterlace flagged frames
    FILTER_DEINTERLACE_HALFED = 0x20,  //< do half rate deinterlacing
    FILTER_ROTATE = 0x40,              //< rotate image according to the codec hints
};

CDVDVideoCodecFFmpeg::CDropControl::CDropControl()
{
    Reset(true);
}

void CDVDVideoCodecFFmpeg::CDropControl::Reset(bool init)
{
    m_lastPTS = AV_NOPTS_VALUE;

    if (init || m_state != VALID)
    {
        m_count = 0;
        m_diffPTS = 0;
        m_state = INIT;
    }
}

void CDVDVideoCodecFFmpeg::CDropControl::Process(int64_t pts, bool drop)
{
    if (m_state == INIT)
    {
        if (pts != AV_NOPTS_VALUE && m_lastPTS != AV_NOPTS_VALUE)
        {
            m_diffPTS += pts - m_lastPTS;
            m_count++;
        }
        if (m_count > 10)
        {
            m_diffPTS = m_diffPTS / m_count;
            if (m_diffPTS > 0)
            {
                m_state = CDropControl::VALID;
                m_count = 0;
            }
        }
    }
    else if (m_state == VALID && !drop)
    {
        if (std::abs(pts - m_lastPTS - m_diffPTS) > m_diffPTS * 0.2)
        {
            m_count++;
            if (m_count > 5)
            {
                Reset(true);
            }
        }
        else
            m_count = 0;
    }
    m_lastPTS = pts;
}

enum AVPixelFormat CDVDVideoCodecFFmpeg::GetFormat(struct AVCodecContext* avctx,
                                                   const AVPixelFormat* fmt)
{
    CDVDVideoCodecFFmpeg* ctx = (CDVDVideoCodecFFmpeg*)avctx->opaque;

    const char* pixFmtName = av_get_pix_fmt_name(*fmt);

    // if frame threading is enabled hw accel is not allowed
    // 2nd condition:
    // fix an ffmpeg issue here, it calls us with an invalid profile
    // then a 2nd call with a valid one
    AVPixelFormat defaultFmt = avcodec_default_get_format(avctx, fmt);
    pixFmtName = av_get_pix_fmt_name(defaultFmt);
    return defaultFmt;
}

CDVDVideoCodecFFmpeg::CDVDVideoCodecFFmpeg() : CDVDVideoCodec()
{
    m_pCodecContext = nullptr;
    m_pFrame = nullptr;
    m_iLastKeyframe = 0;
    m_dts = DVD_NOPTS_VALUE;
    m_started = false;
    m_decoderPts = DVD_NOPTS_VALUE;
    m_droppedFrames = 0;
}

CDVDVideoCodecFFmpeg::~CDVDVideoCodecFFmpeg()
{
    Dispose();
}

bool CDVDVideoCodecFFmpeg::Open(CDVDStreamInfo& hints)
{
    const AVCodec* pCodec;

    pCodec = avcodec_find_decoder(hints.codec);

    if (pCodec == NULL)
        return false;

    m_pCodecContext = avcodec_alloc_context3(pCodec);
    if (!m_pCodecContext)
        return false;

    m_pCodecContext->opaque = (void*)this;
    m_pCodecContext->debug = 0;
    m_pCodecContext->workaround_bugs = FF_BUG_AUTODETECT;
    m_pCodecContext->get_format = GetFormat;
    m_pCodecContext->codec_tag = hints.codec_tag;

    int num_threads = TVPGetProcessorNum() * 3 / 2;
    num_threads = std::max(1, std::min(num_threads, 16));
    m_pCodecContext->thread_count = num_threads;

    // if we don't do this, then some codecs seem to fail.
    m_pCodecContext->coded_height = hints.height;
    m_pCodecContext->coded_width = hints.width;
    m_pCodecContext->bits_per_coded_sample = hints.bitsperpixel;

    if (hints.extradata && hints.extrasize > 0)
    {
        m_pCodecContext->extradata_size = hints.extrasize;
        m_pCodecContext->extradata =
            (uint8_t*)av_mallocz(hints.extrasize + AV_INPUT_BUFFER_PADDING_SIZE);
        memcpy(m_pCodecContext->extradata, hints.extradata, hints.extrasize);
    }

    while (avcodec_open2(m_pCodecContext, pCodec, nullptr) < 0)
    {
        // trying set lowres to 0
        if (pCodec->max_lowres < m_pCodecContext->lowres)
        {
            m_pCodecContext->lowres = pCodec->max_lowres;
            if (avcodec_open2(m_pCodecContext, pCodec, nullptr) >= 0)
                break;
        }
        avcodec_close(m_pCodecContext);
        av_free(m_pCodecContext);
        return false;
    }

    m_pFrame = av_frame_alloc();
    if (!m_pFrame)
    {
        avcodec_close(m_pCodecContext);
        av_free(m_pCodecContext);
        return false;
    }

    m_dropCtrl.Reset(true);
    return true;
}

void CDVDVideoCodecFFmpeg::Dispose()
{
    av_frame_free(&m_pFrame);
    avcodec_close(m_pCodecContext);
    av_free(m_pCodecContext);
}

void CDVDVideoCodecFFmpeg::SetDropState(bool bDrop)
{
    if (m_pCodecContext)
    {
        // i don't know exactly how high this should be set
        // couldn't find any good docs on it. think it varies
        // from codec to codec on what it does

        //  2 seem to be to high.. it causes video to be ruined on following images
        if (bDrop)
        {
            m_pCodecContext->skip_frame = AVDISCARD_NONREF;
            m_pCodecContext->skip_idct = AVDISCARD_NONREF;
            m_pCodecContext->skip_loop_filter = AVDISCARD_NONREF;
        }
        else
        {
            m_pCodecContext->skip_frame = AVDISCARD_DEFAULT;
            m_pCodecContext->skip_idct = AVDISCARD_DEFAULT;
            m_pCodecContext->skip_loop_filter = AVDISCARD_DEFAULT;
        }
    }
}

union pts_union
{
    double pts_d;
    int64_t pts_i;
};

static int64_t pts_dtoi(double pts)
{
    pts_union u;
    u.pts_d = pts;
    return u.pts_i;
}

bool CDVDVideoCodecFFmpeg::AddData(const DemuxPacket& packet)
{
    if (!m_pCodecContext)
        return true;

    if (!packet.pData)
        return true;

    m_dts = packet.dts;

    AVPacket* avpkt = av_packet_alloc();
    if (!avpkt)
    {
        return false;
    }

    avpkt->data = packet.pData;
    avpkt->size = packet.iSize;
    avpkt->dts = (packet.dts == DVD_NOPTS_VALUE) ? AV_NOPTS_VALUE
                                                 : packet.dts / DVD_TIME_BASE * AV_TIME_BASE;
    avpkt->pts = (packet.pts == DVD_NOPTS_VALUE) ? AV_NOPTS_VALUE
                                                 : packet.pts / DVD_TIME_BASE * AV_TIME_BASE;
    avpkt->flags = AV_PKT_FLAG_KEY;
    int ret = avcodec_send_packet(m_pCodecContext, avpkt);

    av_packet_free(&avpkt);

    // try again
    if (ret == AVERROR(EAGAIN))
    {
        return false;
    }

    m_iLastKeyframe++;
    // put a limit on convergence count to avoid huge mem usage on streams without keyframes
    if (m_iLastKeyframe > 300)
        m_iLastKeyframe = 300;

    return true;
}

void CDVDVideoCodecFFmpeg::Reset()
{
    m_started = false;
    m_decoderPts = DVD_NOPTS_VALUE;
    m_droppedFrames = 0;
    m_iLastKeyframe = m_pCodecContext->has_b_frames;
    avcodec_flush_buffers(m_pCodecContext);

    m_dropCtrl.Reset(false);
}

bool CDVDVideoCodecFFmpeg::GetPicture(DVDVideoPicture* pDvdVideoPicture)
{
    av_frame_unref(m_pFrame);
    int ret = avcodec_receive_frame(m_pCodecContext, m_pFrame);

    if (m_iLastKeyframe < m_pCodecContext->has_b_frames + 2)
        m_iLastKeyframe = m_pCodecContext->has_b_frames + 2;

    if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN) || ret != 0)
    {
        return false;
    }

    // here we got a frame
    int64_t framePTS = m_pFrame->best_effort_timestamp;
    if (m_pCodecContext->skip_frame > AVDISCARD_DEFAULT)
    {
        if (m_dropCtrl.m_state == CDropControl::VALID && m_dropCtrl.m_lastPTS != AV_NOPTS_VALUE &&
            framePTS != AV_NOPTS_VALUE &&
            framePTS > (m_dropCtrl.m_lastPTS + m_dropCtrl.m_diffPTS * 1.5))
        {
            m_droppedFrames++;
        }
    }
    m_dropCtrl.Process(framePTS, m_pCodecContext->skip_frame > AVDISCARD_DEFAULT);

    if (m_pFrame->key_frame)
    {
        m_started = true;
        m_iLastKeyframe = m_pCodecContext->has_b_frames + 2;
    }

    if (!m_started)
    {
        av_frame_unref(m_pFrame);
        return false;
    }

    if (SetPictureParams(pDvdVideoPicture))
        return true;

    return false;
}

#define RINT(x) ((x) >= 0 ? ((int)((x) + 0.5)) : ((int)((x) - 0.5)))
bool CDVDVideoCodecFFmpeg::SetPictureParams(DVDVideoPicture* pDvdVideoPicture)
{
    while (m_pCodecContext->coded_width > 0 && m_pCodecContext->coded_height > 0)
    {
        int pitch = m_pFrame->linesize[0];
        if (pitch < m_pCodecContext->coded_width || pitch > m_pCodecContext->coded_width + 16)
            break;
        m_pFrame->width = m_pCodecContext->coded_width;
        m_pFrame->height = m_pCodecContext->coded_height;
        break;
    }

    if (!m_pFrame)
        return false;

    pDvdVideoPicture->iWidth = m_pFrame->width;
    pDvdVideoPicture->iHeight = m_pFrame->height;
    double aspect_ratio;

    /* use variable in the frame */
    AVRational pixel_aspect = m_pFrame->sample_aspect_ratio;

    if (pixel_aspect.num == 0)
        aspect_ratio = 0;
    else
        aspect_ratio = av_q2d(pixel_aspect) * pDvdVideoPicture->iWidth / pDvdVideoPicture->iHeight;

    if (aspect_ratio <= 0.0)
        aspect_ratio = (float)pDvdVideoPicture->iWidth / (float)pDvdVideoPicture->iHeight;

    /* XXX: we suppose the screen has a 1.0 pixel ratio */ // CDVDVideo will compensate it.
    pDvdVideoPicture->iDisplayHeight = pDvdVideoPicture->iHeight;
    pDvdVideoPicture->iDisplayWidth = ((int)RINT(pDvdVideoPicture->iHeight * aspect_ratio)) & -3;
    if (pDvdVideoPicture->iDisplayWidth > pDvdVideoPicture->iWidth)
    {
        pDvdVideoPicture->iDisplayWidth = pDvdVideoPicture->iWidth;
        pDvdVideoPicture->iDisplayHeight =
            ((int)RINT(pDvdVideoPicture->iWidth / aspect_ratio)) & -3;
    }

    pDvdVideoPicture->pts = DVD_NOPTS_VALUE;
    pDvdVideoPicture->iRepeatPicture = 0.5 * m_pFrame->repeat_pict;
    pDvdVideoPicture->iFlags = DVP_FLAG_ALLOCATED;
    pDvdVideoPicture->iFlags |= m_pFrame->interlaced_frame ? DVP_FLAG_INTERLACED : 0;
    pDvdVideoPicture->iFlags |= m_pFrame->top_field_first ? DVP_FLAG_TOP_FIELD_FIRST : 0;

    pDvdVideoPicture->chroma_position = m_pCodecContext->chroma_sample_location;
    pDvdVideoPicture->color_primaries = m_pCodecContext->color_primaries;
    pDvdVideoPicture->color_transfer = m_pCodecContext->color_trc;
    pDvdVideoPicture->color_matrix = m_pCodecContext->colorspace;
    if (m_pCodecContext->color_range == AVCOL_RANGE_JPEG ||
        m_pCodecContext->pix_fmt == AV_PIX_FMT_YUVJ420P)
        pDvdVideoPicture->color_range = 1;
    else
        pDvdVideoPicture->color_range = 0;

    pDvdVideoPicture->qp_table = nullptr;
    pDvdVideoPicture->qscale_type = 0;

    if (pDvdVideoPicture->iRepeatPicture)
        pDvdVideoPicture->dts = DVD_NOPTS_VALUE;
    else
        pDvdVideoPicture->dts = m_dts;

    m_dts = DVD_NOPTS_VALUE;

    int64_t bpts = m_pFrame->best_effort_timestamp;
    if (bpts != AV_NOPTS_VALUE)
    {
        pDvdVideoPicture->pts = (double)bpts * DVD_TIME_BASE / AV_TIME_BASE;
        if (pDvdVideoPicture->pts == m_decoderPts)
        {
            pDvdVideoPicture->iRepeatPicture = -0.5;
            pDvdVideoPicture->pts = DVD_NOPTS_VALUE;
            pDvdVideoPicture->dts = DVD_NOPTS_VALUE;
        }
    }
    else
        pDvdVideoPicture->pts = DVD_NOPTS_VALUE;

    if (pDvdVideoPicture->pts != DVD_NOPTS_VALUE)
        m_decoderPts = pDvdVideoPicture->pts;

    for (int i = 0; i < 4; i++)
        pDvdVideoPicture->data[i] = m_pFrame->data[i];
    for (int i = 0; i < 4; i++)
        pDvdVideoPicture->iLineSize[i] = m_pFrame->linesize[i];

    pDvdVideoPicture->iFlags |= pDvdVideoPicture->data[0] ? 0 : DVP_FLAG_DROPPED;
    pDvdVideoPicture->extended_format = 0;

    return true;
}

unsigned CDVDVideoCodecFFmpeg::GetConvergeCount()
{
    return m_iLastKeyframe;
}

bool CDVDVideoCodecFFmpeg::GetCodecStats(double& pts, int& droppedFrames)
{
    if (m_decoderPts != DVD_NOPTS_VALUE)
        pts = m_decoderPts;
    else
        pts = m_dts;

    if (m_droppedFrames)
        droppedFrames = m_droppedFrames;
    else
        droppedFrames = -1;
    m_droppedFrames = 0;

    return true;
}

NS_KRMOVIE_END
