#include "tjsCommHead.h"
#include "CodecAudioFFmpeg.h"

#include "KRStreamInfo.h"
#include "VideoReferenceClock.h"
#include "Message.h"

NS_KRMOVIE_BEGIN

CDVDAudioCodecFFmpeg::CDVDAudioCodecFFmpeg() : CDVDAudioCodec()
{
    m_pCodecContext = NULL;
    m_pFrame = NULL;
}

CDVDAudioCodecFFmpeg::~CDVDAudioCodecFFmpeg()
{
    Dispose();
}

bool CDVDAudioCodecFFmpeg::Open(CDVDStreamInfo& hints)
{
    const AVCodec* pCodec = NULL;

    if (hints.codec == AV_CODEC_ID_DTS)
        pCodec = avcodec_find_decoder_by_name("dcadec");

    if (!pCodec)
        pCodec = avcodec_find_decoder(hints.codec);

    if (!pCodec)
    {
        return false;
    }

    m_pCodecContext = avcodec_alloc_context3(pCodec);
    if (!m_pCodecContext)
        return false;

    m_pCodecContext->debug = 0;
    m_pCodecContext->workaround_bugs = 1;

    m_pCodecContext->ch_layout.nb_channels = hints.channels;
    m_pCodecContext->sample_rate = hints.samplerate;
    m_pCodecContext->block_align = hints.blockalign;
    m_pCodecContext->bit_rate = hints.bitrate;
    m_pCodecContext->bits_per_coded_sample = hints.bitspersample;

    if (m_pCodecContext->bits_per_coded_sample == 0)
        m_pCodecContext->bits_per_coded_sample = 16;

    if (hints.extradata && hints.extrasize > 0)
    {
        m_pCodecContext->extradata =
            (uint8_t*)av_mallocz(hints.extrasize + AV_INPUT_BUFFER_PADDING_SIZE);
        if (m_pCodecContext->extradata)
        {
            m_pCodecContext->extradata_size = hints.extrasize;
            memcpy(m_pCodecContext->extradata, hints.extradata, hints.extrasize);
        }
    }

    if (avcodec_open2(m_pCodecContext, pCodec, NULL) < 0)
    {
        Dispose();
        return false;
    }

    m_pFrame = av_frame_alloc();
    if (!m_pFrame)
    {
        Dispose();
        return false;
    }

    return true;
}

void CDVDAudioCodecFFmpeg::Dispose()
{
    av_frame_free(&m_pFrame);
    avcodec_close(m_pCodecContext);
}

bool CDVDAudioCodecFFmpeg::AddData(const DemuxPacket& packet)
{
    if (!m_pCodecContext)
        return false;

    AVPacket* avpkt = av_packet_alloc();
    if (!avpkt)
    {
        return false;
    }

    avpkt->data = packet.pData;
    avpkt->size = packet.iSize;
    avpkt->dts = (packet.dts == DVD_NOPTS_VALUE)
                     ? AV_NOPTS_VALUE
                     : static_cast<int64_t>(packet.dts / DVD_TIME_BASE * AV_TIME_BASE);
    avpkt->pts = (packet.pts == DVD_NOPTS_VALUE)
                     ? AV_NOPTS_VALUE
                     : static_cast<int64_t>(packet.pts / DVD_TIME_BASE * AV_TIME_BASE);

    int ret = avcodec_send_packet(m_pCodecContext, avpkt);

    av_packet_free(&avpkt);

    // try again
    if (ret == AVERROR(EAGAIN))
    {
        return false;
    }

    return true;
}

#define AE_IS_PLANAR(x) ((x) >= AV_SAMPLE_FMT_U8P && (x) <= AV_SAMPLE_FMT_DBLP)
static const unsigned int DataFormatToBits(const enum AVSampleFormat dataFormat)
{
    if (dataFormat < 0 || dataFormat >= AV_SAMPLE_FMT_NB)
        return 0;

    static const unsigned int formats[AV_SAMPLE_FMT_NB] = {
        8, /* U8     */

        16,                  /* S16  */
        32,                  /* S32  */
        sizeof(float) << 3,  /* FLT  */
        sizeof(double) << 3, /* DBL  */

        8,                   /* U8P */
        16,                  /* S16P */
        32,                  /* S32P */
        sizeof(float) << 3,  /* FLTP */
        sizeof(double) << 3, /* DBLP */
        64,                  /* S64, */
        64,                  /* S64P */
    };

    return formats[dataFormat];
}

void CDVDAudioCodecFFmpeg::GetData(DVDAudioFrame& frame)
{
    frame.nb_frames = 0;
    uint8_t* data[16]{};
    int bytes = GetData(data);
    if (!bytes)
    {
        return;
    }
    frame.m_dataFormat = m_pCodecContext->sample_fmt;
    frame.m_channelCount = m_pCodecContext->ch_layout.nb_channels;
    frame.framesize = (DataFormatToBits(frame.m_dataFormat) >> 3) * frame.m_channelCount;
    if (frame.framesize == 0)
        return;
    frame.nb_frames = bytes / frame.framesize;
    frame.planes = AE_IS_PLANAR(frame.m_dataFormat) ? frame.m_channelCount : 1;
    for (unsigned int i = 0; i < frame.planes; i++)
        frame.data[i] = data[i];
    frame.bits_per_sample = DataFormatToBits(frame.m_dataFormat);
    frame.m_sampleRate = m_pCodecContext->sample_rate;

    if (frame.m_sampleRate)
        frame.duration = ((double)frame.nb_frames * DVD_TIME_BASE) / frame.m_sampleRate;
    else
        frame.duration = 0.0;

    int64_t bpts = m_pFrame->best_effort_timestamp;
    if (bpts != AV_NOPTS_VALUE)
        frame.pts = (double)bpts * DVD_TIME_BASE / AV_TIME_BASE;
    else
        frame.pts = DVD_NOPTS_VALUE;
}

int CDVDAudioCodecFFmpeg::GetData(uint8_t** dst)
{
    int ret = avcodec_receive_frame(m_pCodecContext, m_pFrame);
    if (!ret)
    {
        int channels = m_pFrame->ch_layout.nb_channels;
        int planes = av_sample_fmt_is_planar(m_pCodecContext->sample_fmt) ? channels : 1;

        for (int i = 0; i < planes; i++)
            dst[i] = m_pFrame->extended_data[i];

        return m_pFrame->nb_samples * channels *
               av_get_bytes_per_sample(m_pCodecContext->sample_fmt);
    }

    return 0;
}

void CDVDAudioCodecFFmpeg::Reset()
{
    if (m_pCodecContext)
        avcodec_flush_buffers(m_pCodecContext);
}

NS_KRMOVIE_END
