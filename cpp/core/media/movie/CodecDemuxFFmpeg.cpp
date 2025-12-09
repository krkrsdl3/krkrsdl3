#include "tjsCommHead.h"
#include "CodecDemuxFFmpeg.h"

extern "C"
{
#include "libavutil/opt.h"
}

#include "VideoReferenceClock.h"
#include "InputStream.h"

NS_KRMOVIE_BEGIN

#define FFMPEG_FILE_BUFFER_SIZE 4096 // default reading size for ffmpeg

/**
 * CDVDMsgDemuxerPacket --- DEMUXER_PACKET
 */
void DemuxPacket::Free(DemuxPacket* pPacket)
{
    if (pPacket)
    {
        try
        {
            if (pPacket->pData)
                TJSAlignedDealloc(pPacket->pData);
            delete pPacket;
        }
        catch (...)
        {
        }
    }
}

DemuxPacket* DemuxPacket::Allocate(int iDataSize)
{
    DemuxPacket* pPacket = new DemuxPacket;
    if (!pPacket)
        return nullptr;

    try
    {
        memset(pPacket, 0, sizeof(DemuxPacket));

        if (iDataSize > 0)
        {
            // need to allocate a few bytes more.
            // From avcodec.h (ffmpeg)
            /**
             * Required number of additionally allocated bytes at the end of the input bitstream for
             * decoding. this is mainly needed because some optimized bitstream readers read 32 or
             * 64 bit at once and could read over the end<br> Note, if the first 23 bits of the
             * additional bytes are not 0 then damaged MPEG bitstreams could cause overread and
             * segfault
             */
            pPacket->pData = (uint8_t*)TJSAlignedAlloc(iDataSize + AV_INPUT_BUFFER_PADDING_SIZE, 4);
            if (!pPacket->pData)
            {
                Free(pPacket);
                return NULL;
            }

            // reset the last 8 bytes to 0;
            memset(pPacket->pData + iDataSize, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        }

        // setup defaults
        pPacket->dts = DVD_NOPTS_VALUE;
        pPacket->pts = DVD_NOPTS_VALUE;
        pPacket->iStreamId = -1;
        pPacket->dispTime = 0;
    }
    catch (...)
    {
        Free(pPacket);
        pPacket = nullptr;
    }
    return pPacket;
}

static int interrupt_cb(void* ctx)
{
    CDVDDemuxFFmpeg* demuxer = static_cast<CDVDDemuxFFmpeg*>(ctx);
    if (demuxer && demuxer->Aborted())
        return 1;
    return 0;
}

static int dvd_file_read(void* h, uint8_t* buf, int size)
{
    if (interrupt_cb(h))
        return AVERROR_EXIT;

    InputStream* pInputStream = static_cast<CDVDDemuxFFmpeg*>(h)->m_pInput;
    int len = pInputStream->Read(buf, size);
    if (len == 0)
        return AVERROR_EOF;
    else
        return len;
}

static int64_t dvd_file_seek(void* h, int64_t pos, int whence)
{
    if (interrupt_cb(h))
        return AVERROR_EXIT;

    InputStream* pInputStream = static_cast<CDVDDemuxFFmpeg*>(h)->m_pInput;
    if (whence == AVSEEK_SIZE)
    {
        return pInputStream->GetLength();
    }
    else
    {
        return pInputStream->Seek(pos, whence & ~AVSEEK_FORCE);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////

CDVDDemuxFFmpeg::CDVDDemuxFFmpeg() : IDemux()
{
    m_pFormatContext = NULL;
    m_ioContext = NULL;
    m_currentPts = DVD_NOPTS_VALUE;
    m_bMatroska = false;
    m_bAVI = false;
    m_speed = DVD_PLAYSPEED_NORMAL;
    m_program = UINT_MAX;
    m_pkt.result = -1;
    m_seekStream = -1;
    memset(&m_pkt.pkt, 0, sizeof(AVPacket));
    m_checkTransportStream = false;
}

CDVDDemuxFFmpeg::~CDVDDemuxFFmpeg()
{
    Dispose();
}

bool CDVDDemuxFFmpeg::Aborted()
{
    if (m_timeout.IsTimePast())
        return true;
    return false;
}

bool CDVDDemuxFFmpeg::Open(InputStream* pInput, bool fileinfo)
{
    if (!pInput)
        return false;

    const std::string& strFile = pInput->GetFileName();
    m_pInput = pInput;

    m_currentPts = DVD_NOPTS_VALUE;
    m_speed = DVD_PLAYSPEED_NORMAL;
    m_program = UINT_MAX;

    // open the demuxer
    m_pFormatContext = avformat_alloc_context();
    const AVIOInterruptCB int_cb = {interrupt_cb, this};
    m_pFormatContext->interrupt_callback = int_cb;

    // try to abort after 30 seconds
    m_timeout.Set(30000);
    unsigned char* buffer = (unsigned char*)av_malloc(FFMPEG_FILE_BUFFER_SIZE);
    m_ioContext = avio_alloc_context(buffer, FFMPEG_FILE_BUFFER_SIZE, 0, this, dvd_file_read, NULL,
                                     dvd_file_seek);

    const AVInputFormat* iformat = NULL;
    // let ffmpeg decide which demuxer we have to open
    av_probe_input_buffer(m_ioContext, &iformat, strFile.c_str(), NULL, 0, 0);

    if (!iformat)
        return false;

    m_pFormatContext->pb = m_ioContext;

    AVDictionary* options = NULL;
    if (iformat->name && (strcmp(iformat->name, "mp3") == 0 || strcmp(iformat->name, "mp2") == 0))
    {
        av_dict_set(&options, "usetoc", "0", 0);
    }

    if (avformat_open_input(&m_pFormatContext, strFile.c_str(), iformat, &options) < 0)
    {
        Dispose();
        av_dict_free(&options);
        return false;
    }
    av_dict_free(&options);

    m_pFormatContext->fps_probe_size = 0;

    // analyse very short to speed up mjpeg playback start
    if (iformat->name && (strcmp(iformat->name, "mjpeg") == 0) && m_ioContext->seekable == 0)
        av_opt_set_int(m_pFormatContext, "analyzeduration", 500000, 0);

    // increase probesize for mpegts streams only
    if (iformat->name && strcmp(iformat->name, "mpegts") == 0)
        av_opt_set_int(m_pFormatContext, "probesize", 10000000, 0); // double ffmpeg default

    // don't re-open mpegts streams with hevc encoding as the params are not correctly detected
    // again
    bool skipCreateStreams = false;
    if (iformat->name && (strcmp(iformat->name, "mpegts") == 0) && !fileinfo &&
        m_pFormatContext->nb_streams > 0 && m_pFormatContext->streams != nullptr &&
        m_pFormatContext->streams[0]->codecpar->codec_id != AV_CODEC_ID_HEVC)
    {
        av_opt_set_int(m_pFormatContext, "analyzeduration", 500000, 0);
        m_checkTransportStream = true;
        skipCreateStreams = true;
    }

    // we need to know if this is matroska or avi later
    m_bMatroska =
        strncmp(m_pFormatContext->iformat->name, "matroska", 8) == 0; // for "matroska.webm"
    m_bAVI = strcmp(m_pFormatContext->iformat->name, "avi") == 0;

    int iErr = avformat_find_stream_info(m_pFormatContext, NULL);
    if (iErr < 0)
    {
        if ((m_pFormatContext->nb_streams == 1 &&
             m_pFormatContext->streams[0]->codecpar->codec_id == AV_CODEC_ID_AC3) ||
            m_checkTransportStream)
        {
            // special case, our codecs can still handle it.
        }
        else
        {
            Dispose();
            return false;
        }
    }

    // print some extra information
    av_dump_format(m_pFormatContext, 0, strFile.c_str(), 0);

    if (m_checkTransportStream)
    {
        // make sure we start video with an i-frame
        ResetVideoStreams();
    }

    // reset any timeout
    m_timeout.SetInfinite();

    // if format can be nonblocking, let's use that
    m_pFormatContext->flags |= AVFMT_FLAG_NONBLOCK;

    // allow IsProgramChange to return true
    if (skipCreateStreams && GetNrOfStreams() == 0)
        m_program = 0;

    m_startTime = 0;
    m_seekStream = -1;
    return true;
}

void CDVDDemuxFFmpeg::Dispose()
{
    m_pkt.result = -1;
    av_packet_unref(&m_pkt.pkt);

    if (m_pFormatContext)
    {
        if (m_ioContext && m_pFormatContext->pb && m_pFormatContext->pb != m_ioContext)
        {
            m_ioContext = m_pFormatContext->pb;
        }
        avformat_close_input(&m_pFormatContext);
    }

    if (m_ioContext)
    {
        av_free(m_ioContext->buffer);
        av_free(m_ioContext);
    }

    m_ioContext = NULL;
    m_pFormatContext = NULL;
    m_speed = DVD_PLAYSPEED_NORMAL;
}

void CDVDDemuxFFmpeg::Reset()
{
    InputStream* pInputStream = m_pInput;
    Dispose();
    Open(pInputStream, true);
    if (pInputStream)
        delete pInputStream;
}

void CDVDDemuxFFmpeg::Flush()
{
    if (m_pFormatContext)
    {
        if (m_pFormatContext->pb)
            avio_flush(m_pFormatContext->pb);
    }

    m_currentPts = DVD_NOPTS_VALUE;

    m_pkt.result = -1;
    av_packet_unref(&m_pkt.pkt);
}

void CDVDDemuxFFmpeg::Abort()
{
    m_timeout.SetExpired();
}

void CDVDDemuxFFmpeg::SetSpeed(int iSpeed)
{
    if (!m_pFormatContext)
        return;

    if (m_speed == iSpeed)
        return;

    if (m_speed != DVD_PLAYSPEED_PAUSE && iSpeed == DVD_PLAYSPEED_PAUSE)
    {
        av_read_pause(m_pFormatContext);
    }
    else if (m_speed == DVD_PLAYSPEED_PAUSE && iSpeed != DVD_PLAYSPEED_PAUSE)
    {
        av_read_play(m_pFormatContext);
    }
    m_speed = iSpeed;

    AVDiscard discard = AVDISCARD_NONE;
    if (m_speed > 4 * DVD_PLAYSPEED_NORMAL)
        discard = AVDISCARD_NONKEY;
    else if (m_speed > 2 * DVD_PLAYSPEED_NORMAL)
        discard = AVDISCARD_BIDIR;
    else if (m_speed < DVD_PLAYSPEED_PAUSE)
        discard = AVDISCARD_NONKEY;

    for (unsigned int i = 0; i < m_pFormatContext->nb_streams; i++)
    {
        if (m_pFormatContext->streams[i])
        {
            if (m_pFormatContext->streams[i]->discard != AVDISCARD_ALL)
                m_pFormatContext->streams[i]->discard = discard;
        }
    }
}

double CDVDDemuxFFmpeg::ConvertTimestamp(int64_t pts, int den, int num)
{
    if (pts == (int64_t)AV_NOPTS_VALUE)
        return DVD_NOPTS_VALUE;

    // do calculations in floats as they can easily overflow otherwise
    // we don't care for having a completly exact timestamp anyway
    double timestamp = (double)pts * num / den;
    double starttime = 0.0f;

    if (m_checkTransportStream)
        starttime = m_startTime;

    if (timestamp > starttime)
        timestamp -= starttime;
    // allow for largest possible difference in pts and dts for a single packet
    else if (timestamp + 0.5f > starttime)
        timestamp = 0;

    return timestamp * DVD_TIME_BASE;
}

DemuxPacket* CDVDDemuxFFmpeg::Read()
{
    DemuxPacket* pPacket = NULL;

    bool bReturnEmpty = false;
    {
        std::unique_lock<std::recursive_mutex> lock(m_critSection); // open lock scope
        if (m_pFormatContext)
        {
            // assume we are not eof
            if (m_pFormatContext->pb)
                m_pFormatContext->pb->eof_reached = 0;

            // check for saved packet after a program change
            if (m_pkt.result < 0)
            {
                // keep track if ffmpeg doesn't always set these
                m_pkt.pkt.size = 0;
                m_pkt.pkt.data = NULL;

                // timeout reads after 100ms
                m_timeout.Set(20000);
                m_pkt.result = av_read_frame(m_pFormatContext, &m_pkt.pkt);
                m_timeout.SetInfinite();
            }

            if (m_pkt.result == AVERROR(EINTR) || m_pkt.result == AVERROR(EAGAIN))
            {
                // timeout, probably no real error, return empty packet
                bReturnEmpty = true;
            }
            else if (m_pkt.result == AVERROR_EOF)
            {
                return nullptr;
            }
            else if (m_pkt.result < 0)
            {
                Flush();
            }
            else if (IsProgramChange())
            {
                pPacket = DemuxPacket::Allocate(0);
                pPacket->iStreamId = DMX_SPECIALID_STREAMCHANGE;

                return pPacket;
            }
            // check size and stream index for being in a valid range
            else if (m_pkt.pkt.size < 0 || m_pkt.pkt.stream_index < 0 ||
                     m_pkt.pkt.stream_index >= (int)m_pFormatContext->nb_streams)
            {
                // XXX, in some cases ffmpeg returns a negative packet size
                if (m_pFormatContext->pb && !m_pFormatContext->pb->eof_reached)
                {
                    bReturnEmpty = true;
                    Flush();
                }

                m_pkt.result = -1;
                av_packet_unref(&m_pkt.pkt);
            }
            else
            {
                AVStream* stream = m_pFormatContext->streams[m_pkt.pkt.stream_index];

                if (IsTransportStreamReady())
                {
                    if (m_program != UINT_MAX)
                    {
                        /* check so packet belongs to selected program */
                        for (unsigned int i = 0;
                             i < m_pFormatContext->programs[m_program]->nb_stream_indexes; i++)
                        {
                            if (m_pkt.pkt.stream_index ==
                                (int)m_pFormatContext->programs[m_program]->stream_index[i])
                            {
                                pPacket = DemuxPacket::Allocate(m_pkt.pkt.size);
                                break;
                            }
                        }

                        if (!pPacket)
                            bReturnEmpty = true;
                    }
                    else
                        pPacket = DemuxPacket::Allocate(m_pkt.pkt.size);
                }
                else
                    bReturnEmpty = true;

                if (pPacket)
                {
                    if (m_bMatroska && stream->codecpar &&
                        stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                    { // matroska can store different timestamps
                        // for different formats, for native stored
                        // stuff it is pts, but for ms compatibility
                        // tracks, it is really dts. sadly ffmpeg
                        // sets these two timestamps equal all the
                        // time, so we select it here instead
                        if (stream->codecpar->codec_tag == 0)
                            m_pkt.pkt.dts = AV_NOPTS_VALUE;
                        else
                            m_pkt.pkt.pts = AV_NOPTS_VALUE;
                    }

                    if (m_bAVI && stream->codecpar &&
                        stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                    {
                        // AVI's always have borked pts, specially if m_pFormatContext->flags
                        // includes AVFMT_FLAG_GENPTS so always use dts
                        m_pkt.pkt.pts = AV_NOPTS_VALUE;
                    }

                    // copy contents into our own packet
                    pPacket->iSize = m_pkt.pkt.size;

                    // maybe we can avoid a memcpy here by detecting where pkt.destruct is pointing
                    // too?
                    if (m_pkt.pkt.data)
                        memcpy(pPacket->pData, m_pkt.pkt.data, pPacket->iSize);

                    pPacket->pts = ConvertTimestamp(m_pkt.pkt.pts, stream->time_base.den,
                                                    stream->time_base.num);
                    pPacket->dts = ConvertTimestamp(m_pkt.pkt.dts, stream->time_base.den,
                                                    stream->time_base.num);
                    pPacket->duration = DVD_SEC_TO_TIME(
                        (double)m_pkt.pkt.duration * stream->time_base.num / stream->time_base.den);

                    // used to guess streamlength
                    if (pPacket->dts != DVD_NOPTS_VALUE &&
                        (pPacket->dts > m_currentPts || m_currentPts == DVD_NOPTS_VALUE))
                        m_currentPts = pPacket->dts;

                    // check if stream has passed full duration, needed for live streams
                    bool bAllowDurationExt =
                        (stream->codecpar && (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ||
                                              stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO));
                    if (bAllowDurationExt && m_pkt.pkt.dts != (int64_t)AV_NOPTS_VALUE)
                    {
                        int64_t duration;
                        duration = m_pkt.pkt.dts;
                        if (stream->start_time != (int64_t)AV_NOPTS_VALUE)
                            duration -= stream->start_time;

                        if (duration > stream->duration)
                        {
                            stream->duration = duration;
                            duration = av_rescale_rnd(stream->duration,
                                                      (int64_t)stream->time_base.num * AV_TIME_BASE,
                                                      stream->time_base.den, AV_ROUND_NEAR_INF);
                            if ((m_pFormatContext->duration == (int64_t)AV_NOPTS_VALUE) ||
                                (m_pFormatContext->duration != (int64_t)AV_NOPTS_VALUE &&
                                 duration > m_pFormatContext->duration))
                                m_pFormatContext->duration = duration;
                        }
                    }

                    // store internal id until we know the continuous id presented to player
                    // the stream might not have been created yet
                    pPacket->iStreamId = m_pkt.pkt.stream_index;
                }
                m_pkt.result = -1;
                av_packet_unref(&m_pkt.pkt);
            }
        }
    } // end of lock scope
    if (bReturnEmpty && !pPacket)
        pPacket = DemuxPacket::Allocate(0);

    if (!pPacket)
        return nullptr;

    // check streams, can we make this a bit more simple?
    if (pPacket && pPacket->iStreamId >= 0)
    {
        AVStream* stream = GetStream(pPacket->iStreamId);
        if (!stream)
        {
            DemuxPacket::Free(pPacket);
            return NULL;
        }

        pPacket->iStreamId = stream->index;
    }
    return pPacket;
}

bool CDVDDemuxFFmpeg::SeekTime(int time, bool backwords, double* startpts)
{
    bool hitEnd = false;

    if (!m_pInput)
        return false;

    if (time < 0)
    {
        time = 0;
        hitEnd = true;
    }

    m_pkt.result = -1;
    av_packet_unref(&m_pkt.pkt);

    int64_t seek_pts = (int64_t)time * (AV_TIME_BASE / 1000);

    int ret;
    {
        std::unique_lock<std::recursive_mutex> lock(m_critSection);
        ret = av_seek_frame(m_pFormatContext, -1, seek_pts, backwords ? AVSEEK_FLAG_BACKWARD : 0);

        // demuxer can return failure, if seeking behind eof
        if (ret < 0 && m_pFormatContext->duration && seek_pts >= (m_pFormatContext->duration))
        {
        }
        else if (ret < 0)
            ret = 0;

        if (ret >= 0)
        {
            m_currentPts = DVD_NOPTS_VALUE;
        }
    }

    if (startpts)
        *startpts = DVD_MSEC_TO_TIME(time);

    if (ret >= 0)
    {
        if (!hitEnd)
            return true;
        else
            return false;
    }
    else
        return false;
}

int CDVDDemuxFFmpeg::GetStreamLength()
{
    if (!m_pFormatContext)
        return 0;

    if (m_pFormatContext->duration < 0)
        return 0;

    return (int)(m_pFormatContext->duration / (AV_TIME_BASE / 1000));
}

/**
 * @brief Finds stream based on unique id
 */
AVStream* CDVDDemuxFFmpeg::GetStream(int iStreamId) const
{
    if (iStreamId < m_pFormatContext->nb_streams)
    {
        return m_pFormatContext->streams[iStreamId];
    }

    return nullptr;
}

std::vector<AVStream*> CDVDDemuxFFmpeg::GetStreams() const
{
    std::vector<AVStream*> streams;

    for (size_t i = 0; i < m_pFormatContext->nb_streams; i++)
    {
        streams.push_back(m_pFormatContext->streams[i]);
    }
    return streams;
}

int CDVDDemuxFFmpeg::GetNrOfStreams() const
{
    return m_pFormatContext->nb_streams;
}

std::string CDVDDemuxFFmpeg::GetStreamCodecName(int iStreamId)
{
    AVStream* stream = GetStream(iStreamId);
    std::string strName;
    if (stream)
    {
#ifdef FF_PROFILE_DTS_HD_MA
        /* use profile to determine the DTS type */
        if (stream->codecpar->codec_id == AV_CODEC_ID_DTS)
        {
            if (stream->codecpar->profile == FF_PROFILE_DTS_HD_MA)
                strName = "dtshd_ma";
            else if (stream->codecpar->profile == FF_PROFILE_DTS_HD_HRA)
                strName = "dtshd_hra";
            else
                strName = "dca";

            return strName;
        }
#endif

        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (codec)
            strName = codec->name;
    }
    return strName;
}

bool CDVDDemuxFFmpeg::IsProgramChange()
{
    if (m_program == UINT_MAX)
        return false;

    if (m_program == 0 && !m_pFormatContext->nb_programs)
        return false;

    if (m_pFormatContext->programs[m_program]->nb_stream_indexes != m_pFormatContext->nb_streams)
        return true;

    if (m_program >= m_pFormatContext->nb_programs)
        return true;

    for (unsigned int i = 0; i < m_pFormatContext->programs[m_program]->nb_stream_indexes; i++)
    {
        int idx = m_pFormatContext->programs[m_program]->stream_index[i];
        AVStream* stream = GetStream(idx);
        if (!stream)
            return true;
        if (m_pFormatContext->streams[idx]->codecpar->codec_id != stream->codecpar->codec_id)
            return true;
    }
    return false;
}

TRANSPORT_STREAM_STATE CDVDDemuxFFmpeg::TransportStreamAudioState()
{
    AVStream* st = nullptr;
    bool hasAudio = false;

    if (m_program != UINT_MAX)
    {
        for (unsigned int i = 0; i < m_pFormatContext->programs[m_program]->nb_stream_indexes; i++)
        {
            int idx = m_pFormatContext->programs[m_program]->stream_index[i];
            st = m_pFormatContext->streams[idx];
            if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            {
                if (idx == m_pkt.pkt.stream_index && m_pkt.pkt.dts != AV_NOPTS_VALUE)
                {
                    if (!m_startTime)
                    {
                        m_startTime =
                            av_rescale(m_pkt.pkt.dts, st->time_base.num, st->time_base.den) -
                            0.000001;
                        m_seekStream = idx;
                    }
                    return TRANSPORT_STREAM_STATE::READY;
                }
                hasAudio = true;
            }
        }
    }
    else
    {
        for (unsigned int i = 0; i < m_pFormatContext->nb_streams; i++)
        {
            st = m_pFormatContext->streams[i];
            if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            {
                if (static_cast<int>(i) == m_pkt.pkt.stream_index &&
                    m_pkt.pkt.dts != AV_NOPTS_VALUE)
                {
                    if (!m_startTime)
                    {
                        m_startTime =
                            av_rescale(m_pkt.pkt.dts, st->time_base.num, st->time_base.den) -
                            0.000001;
                        m_seekStream = i;
                    }
                    return TRANSPORT_STREAM_STATE::READY;
                }
                hasAudio = true;
            }
        }
    }
    if (hasAudio && m_startTime)
        return TRANSPORT_STREAM_STATE::READY;

    return (hasAudio) ? TRANSPORT_STREAM_STATE::NOTREADY : TRANSPORT_STREAM_STATE::NONE;
}

TRANSPORT_STREAM_STATE CDVDDemuxFFmpeg::TransportStreamVideoState()
{
    AVStream* st = nullptr;
    bool hasVideo = false;

    if (m_program == 0 && !m_pFormatContext->nb_programs)
        return TRANSPORT_STREAM_STATE::NONE;

    if (m_program != UINT_MAX)
    {
        for (unsigned int i = 0; i < m_pFormatContext->programs[m_program]->nb_stream_indexes; i++)
        {
            int idx = m_pFormatContext->programs[m_program]->stream_index[i];
            st = m_pFormatContext->streams[idx];
            if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            {
                if (idx == m_pkt.pkt.stream_index && m_pkt.pkt.dts != AV_NOPTS_VALUE &&
                    st->codecpar->extradata)
                {
                    if (!m_startTime)
                    {
                        m_startTime =
                            av_rescale(m_pkt.pkt.dts, st->time_base.num, st->time_base.den) -
                            0.000001;
                        m_seekStream = idx;
                    }
                    return TRANSPORT_STREAM_STATE::READY;
                }
                hasVideo = true;
            }
        }
    }
    else
    {
        for (unsigned int i = 0; i < m_pFormatContext->nb_streams; i++)
        {
            st = m_pFormatContext->streams[i];
            if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            {
                if (static_cast<int>(i) == m_pkt.pkt.stream_index &&
                    m_pkt.pkt.dts != AV_NOPTS_VALUE && st->codecpar->extradata)
                {
                    if (!m_startTime)
                    {
                        m_startTime =
                            av_rescale(m_pkt.pkt.dts, st->time_base.num, st->time_base.den) -
                            0.000001;
                        m_seekStream = i;
                    }
                    return TRANSPORT_STREAM_STATE::READY;
                }
                hasVideo = true;
            }
        }
    }
    if (hasVideo && m_startTime)
        return TRANSPORT_STREAM_STATE::READY;

    return (hasVideo) ? TRANSPORT_STREAM_STATE::NOTREADY : TRANSPORT_STREAM_STATE::NONE;
}

bool CDVDDemuxFFmpeg::IsTransportStreamReady()
{
    if (!m_checkTransportStream)
        return true;

    if (m_program == 0 && !m_pFormatContext->nb_programs)
        return false;

    TRANSPORT_STREAM_STATE state = TransportStreamVideoState();
    if (state == TRANSPORT_STREAM_STATE::NONE)
        state = TransportStreamAudioState();

    return state == TRANSPORT_STREAM_STATE::READY;
}

void CDVDDemuxFFmpeg::ResetVideoStreams()
{
    AVStream* st;
    for (unsigned int i = 0; i < m_pFormatContext->nb_streams; i++)
    {
        st = m_pFormatContext->streams[i];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            av_freep(&st->codecpar->extradata);
            st->codecpar->extradata_size = 0;
            st->codecpar->width = 0;
        }
    }
}

NS_KRMOVIE_END
