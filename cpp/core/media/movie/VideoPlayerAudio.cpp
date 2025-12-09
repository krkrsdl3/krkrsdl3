#include "tjsCommHead.h"
#include "VideoPlayerAudio.h"

#include "WaveMixer.h"
#include "CodecAudioFFmpeg.h"

NS_KRMOVIE_BEGIN
// allow audio for slow and fast speeds (but not rewind/fastforward)
#define ALLOW_AUDIO(speed) \
    ((speed) > 5 * DVD_PLAYSPEED_NORMAL / 10 && (speed) <= 15 * DVD_PLAYSPEED_NORMAL / 10)

class CDVDMsgAudioCodecChange : public CDVDMsg
{
public:
    CDVDMsgAudioCodecChange(const CDVDStreamInfo& hints, CDVDAudioCodec* codec)
      : CDVDMsg(GENERAL_STREAMCHANGE),
        m_codec(codec),
        m_hints(hints)
    {
    }
    ~CDVDMsgAudioCodecChange() { delete m_codec; }
    CDVDAudioCodec* m_codec;
    CDVDStreamInfo m_hints;
};

CVideoPlayerAudio::CVideoPlayerAudio(CDVDClock* pClock, CDVDMessageQueue& parent)
  : tTVPThread(),
    IDVDStreamPlayerAudio(),
    m_messageQueue("audio"),
    m_messageParent(parent)
{
    m_pClock = pClock;
    m_pAudioCodec = NULL;
    m_audioClock = 0;
    m_speed = DVD_PLAYSPEED_NORMAL;
    m_stalled = true;
    m_paused = false;
    m_syncState = IDVDStreamPlayer::SYNC_STARTING;
    m_silence = false;
    m_synctype = SYNC_DISCON;
    m_setsynctype = SYNC_DISCON;
    m_prevsynctype = -1;
    m_prevskipped = false;
    m_maxspeedadjust = 0.0;

    m_messageQueue.SetMaxDataSize(6 * 1024 * 1024);
    m_messageQueue.SetMaxTimeSize(8.0);
}

CVideoPlayerAudio::~CVideoPlayerAudio()
{
    if (swr_ctx)
    {
        swr_free(&swr_ctx);
    }
    if (audio_buf)
    {
        av_freep(&audio_buf);
    }
    if (m_soundDevice)
    {
        delete m_soundDevice;
        m_soundDevice = nullptr;
    }

    StopThread();

    // close the stream, and don't wait for the audio to be finished
    // CloseStream(true);
}

bool CVideoPlayerAudio::OpenStream(CDVDStreamInfo& hints)
{
    bool allowpassthrough = true;
    if (hints.realtime)
        allowpassthrough = false;
    CDVDAudioCodec* codec = new CDVDAudioCodecFFmpeg();
    if (!codec->Open(hints))
    {
        delete codec;
        return false;
    }

    if (m_messageQueue.IsInited())
        m_messageQueue.Put(new CDVDMsgAudioCodecChange(hints, codec), 0);
    else
    {
        OpenStream(hints, codec);
        m_messageQueue.Init();
        Resume();
    }
    return true;
}

void CVideoPlayerAudio::OpenStream(CDVDStreamInfo& hints, CDVDAudioCodec* codec)
{
    SAFE_DELETE(m_pAudioCodec);
    m_pAudioCodec = codec;

    /* store our stream hints */
    m_streaminfo = hints;

    /* check if we only just got sample rate, in which case the previous call
     * to CreateAudioCodec() couldn't have started passthrough */
    if (hints.samplerate != m_streaminfo.samplerate)
        SwitchCodecIfNeeded();

    m_audioClock = 0;
    m_stalled = m_messageQueue.GetPacketCount(CDVDMsg::DEMUXER_PACKET) == 0;

    m_synctype = SYNC_DISCON;
    m_setsynctype = SYNC_DISCON;
    if (hints.realtime)
        m_setsynctype = SYNC_RESAMPLE;

    m_prevsynctype = -1;

    m_prevskipped = false;
    m_silence = false;

    m_maxspeedadjust = 5.0;

    m_messageParent.Put(new CDVDMsg(CDVDMsg::PLAYER_AVCHANGE));
    m_syncState = IDVDStreamPlayer::SYNC_STARTING;
}

void CVideoPlayerAudio::CloseStream(bool bWaitForBuffers)
{
    bool bWait = bWaitForBuffers && m_speed > 0 && m_soundDevice->IsPlaying();

    // wait until buffers are empty
    if (bWait)
        m_messageQueue.WaitUntilEmpty();

    // send abort message to the audio queue
    m_messageQueue.Abort();

    // shut down the adio_decode thread and wait for it
    StopThread(); // will set this->m_bStop to true

    if (!bWait)
    {
        Flush();
    }

    Destroy();

    // uninit queue
    m_messageQueue.End();

    if (m_pAudioCodec)
    {
        m_pAudioCodec->Dispose();
        delete m_pAudioCodec;
        m_pAudioCodec = NULL;
    }
}

void CVideoPlayerAudio::UpdatePlayerInfo()
{
    SInfo info;
    info.pts = GetPlayingPts();
    info.passthrough = false;

    {
        std::unique_lock<std::recursive_mutex> lock(m_info_section);
        m_info = info;
    }
}

void CVideoPlayerAudio::Execute()
{
    DVDAudioFrame audioframe;

    while (!GetTerminated())
    {
        CDVDMsg* pMsg;
        int timeout = 1000;

        // read next packet and return -1 on error
        int priority = 1;
        // Do we want a new audio frame?
        if (m_syncState == IDVDStreamPlayer::SYNC_STARTING || /* when not started */
            ALLOW_AUDIO(m_speed) ||                           /* when playing normally */
            m_speed < DVD_PLAYSPEED_PAUSE ||                  /* when rewinding */
            (m_speed > DVD_PLAYSPEED_NORMAL &&
             m_audioClock < m_pClock->GetClock())) /* when behind clock in ff */
            priority = 0;

        if (m_syncState == IDVDStreamPlayer::SYNC_WAITSYNC)
            priority = 1;

        if (m_paused)
            priority = 1;

        MsgQueueReturnCode ret = m_messageQueue.Get(&pMsg, timeout, priority);

        if (MSGQ_IS_ERROR(ret))
        {
            break;
        }
        else if (ret == MSGQ_TIMEOUT)
        {
            // Flush as the audio output may keep looping if we don't
            if (ALLOW_AUDIO(m_speed) && !m_stalled && m_syncState == IDVDStreamPlayer::SYNC_INSYNC)
            {
                // while AE sync is active, we still have time to fill buffers
                if (m_syncTimer.IsTimePast())
                {
                    m_stalled = true;
                }
            }
            if (timeout == 0)
                Sleep(10);
            continue;
        }

        // handle messages
        if (pMsg->IsType(CDVDMsg::GENERAL_SYNCHRONIZE))
        {
            if (((CDVDMsgGeneralSynchronize*)pMsg)->Wait(100, SYNCSOURCE_AUDIO))
                ;
            else
                m_messageQueue.Put(pMsg->AddRef(),
                                   1); // push back as prio message, to process other prio messages
        }
        else if (pMsg->IsType(CDVDMsg::GENERAL_RESYNC))
        { // player asked us to set internal clock
            double pts = static_cast<CDVDMsgDouble*>(pMsg)->m_value;

            m_audioClock = pts;
            if (m_speed != DVD_PLAYSPEED_PAUSE)
                m_soundDevice->Play();
            m_syncState = IDVDStreamPlayer::SYNC_INSYNC;
            m_syncTimer.Set(3000);
        }
        else if (pMsg->IsType(CDVDMsg::GENERAL_RESET))
        {
            if (m_pAudioCodec)
                m_pAudioCodec->Reset();
            Flush();
            m_stalled = true;
            m_audioClock = 0;
            m_syncState = IDVDStreamPlayer::SYNC_STARTING;
        }
        else if (pMsg->IsType(CDVDMsg::GENERAL_FLUSH))
        {
            bool sync = static_cast<CDVDMsgBool*>(pMsg)->m_value;
            Flush();
            m_stalled = true;
            m_audioClock = 0;

            if (sync)
            {
                m_syncState = IDVDStreamPlayer::SYNC_STARTING;
                Pause();
            }

            if (m_pAudioCodec)
                m_pAudioCodec->Reset();
        }
        else if (pMsg->IsType(CDVDMsg::GENERAL_EOF))
        {
        }
        else if (pMsg->IsType(CDVDMsg::PLAYER_SETSPEED))
        {
            double speed = static_cast<CDVDMsgInt*>(pMsg)->m_value;

            if (ALLOW_AUDIO(speed))
            {
                if (speed != m_speed)
                {
                    if (m_syncState == IDVDStreamPlayer::SYNC_INSYNC)
                        m_soundDevice->Play();
                }
            }
            else
            {
                Pause();
            }
            m_speed = speed;
        }
        else if (pMsg->IsType(CDVDMsg::AUDIO_SILENCE))
        {
            m_silence = static_cast<CDVDMsgBool*>(pMsg)->m_value;
        }
        else if (pMsg->IsType(CDVDMsg::GENERAL_STREAMCHANGE))
        {
            CDVDMsgAudioCodecChange* msg(static_cast<CDVDMsgAudioCodecChange*>(pMsg));
            OpenStream(msg->m_hints, msg->m_codec);
            msg->m_codec = NULL;
        }
        else if (pMsg->IsType(CDVDMsg::GENERAL_PAUSE))
        {
            m_paused = static_cast<CDVDMsgBool*>(pMsg)->m_value;
        }
        else if (pMsg->IsType(CDVDMsg::DEMUXER_PACKET))
        {
            DemuxPacket* pPacket = ((CDVDMsgDemuxerPacket*)pMsg)->GetPacket();
            bool bPacketDrop = ((CDVDMsgDemuxerPacket*)pMsg)->GetPacketDrop();

            if (bPacketDrop)
            {
                if (m_syncState != IDVDStreamPlayer::SYNC_STARTING)
                {
                    audioframe.nb_frames = 0;
                }
                m_syncState = IDVDStreamPlayer::SYNC_STARTING;
                continue;
            }

            if (!m_pAudioCodec->AddData(*pPacket))
            {
                pMsg->AddRef();
                m_messageQueue.Put(pMsg, 0, false);
            }

            UpdatePlayerInfo();

            ProcessDecoderOutput(audioframe);
        }

        pMsg->Release();
    }
}

void CVideoPlayerAudio::SetSyncType(bool passthrough)
{
    // set the synctype from the gui
    m_synctype = m_setsynctype;

    // if SetMaxSpeedAdjust returns false, it means no video is played and we need to use clock
    // feedback
    double maxspeedadjust = 0.0;
    if (m_synctype == SYNC_RESAMPLE)
        maxspeedadjust = m_maxspeedadjust;

    if (m_synctype != m_prevsynctype)
    {
        const char* synctypes[] = {"clock feedback", "resample", "invalid"};
        int synctype = (m_synctype >= 0 && m_synctype <= 1) ? m_synctype : 2;
        m_prevsynctype = m_synctype;
    }
}

bool CVideoPlayerAudio::ProcessDecoderOutput(DVDAudioFrame& audioframe)
{
    m_pAudioCodec->GetData(audioframe);

    if (audioframe.nb_frames == 0)
        return false;

    audioframe.hasTimestamp = true;
    if (audioframe.pts == DVD_NOPTS_VALUE)
    {
        audioframe.pts = m_audioClock;
        audioframe.hasTimestamp = false;
    }
    else
    {
        m_audioClock = audioframe.pts;
    }

    if (audioframe.m_sampleRate && m_streaminfo.samplerate != (int)audioframe.m_sampleRate)
    {
        // The sample rate has changed or we just got it for the first time
        // for this stream. See if we should enable/disable passthrough due
        // to it.
        m_streaminfo.samplerate = audioframe.m_sampleRate;
        if (SwitchCodecIfNeeded())
        {
            audioframe.nb_frames = 0;
            return false;
        }
    }

    // demuxer reads metatags that influence channel layout
    if (m_streaminfo.codec == AV_CODEC_ID_FLAC && m_streaminfo.channels)
        audioframe.m_channelCount = m_streaminfo.channels;

    // we have succesfully decoded an audio frame, setup renderer to match
    if (!IsValidFormat(audioframe))
    {

        Destroy();

        Create(audioframe, m_streaminfo.codec, m_setsynctype == SYNC_RESAMPLE);

        if (m_syncState == IDVDStreamPlayer::SYNC_INSYNC)
            m_soundDevice->Play();

        m_streaminfo.channels = audioframe.m_channelCount;

        m_messageParent.Put(new CDVDMsg(CDVDMsg::PLAYER_AVCHANGE));
    }

    // Zero out the frame data if we are supposed to silence the audio
    if (m_silence)
    {
        int size = audioframe.nb_frames * audioframe.framesize / audioframe.planes;
        for (unsigned int i = 0; i < audioframe.planes; i++)
            memset(audioframe.data[i], 0, size);
    }

    SetSyncType(false);

    AddPackets(audioframe);

    // guess next pts
    m_audioClock += audioframe.duration;

    // signal to our parent that we have initialized
    if (m_syncState == IDVDStreamPlayer::SYNC_STARTING)
    {
        double cachetotal = DVD_SEC_TO_TIME(m_soundDevice->GetLatencySeconds());
        {
            m_syncState = IDVDStreamPlayer::SYNC_WAITSYNC;

            m_stalled = false;
            SStartMsg msg;
            msg.player = VideoPlayer_AUDIO;
            msg.cachetotal = cachetotal;
            msg.cachetime = 0;
            msg.timestamp = audioframe.hasTimestamp ? audioframe.pts : DVD_NOPTS_VALUE;
            m_messageParent.Put(new CDVDMsgType<SStartMsg>(CDVDMsg::PLAYER_STARTED, msg));
        }
    }

    return true;
}

void CVideoPlayerAudio::OnExit()
{
}

void CVideoPlayerAudio::SetSpeed(int speed)
{
    if (m_messageQueue.IsInited())
        m_messageQueue.Put(new CDVDMsgInt(CDVDMsg::PLAYER_SETSPEED, speed), 1);
    else
        m_speed = speed;
}

void CVideoPlayerAudio::Flush(bool sync)
{
    m_messageQueue.Flush();
    m_messageQueue.Put(new CDVDMsgBool(CDVDMsg::GENERAL_FLUSH, sync), 1);

    AbortAddPackets();
}

bool CVideoPlayerAudio::AcceptsData()
{
    bool full = m_messageQueue.IsFull();
    return !full;
}

bool CVideoPlayerAudio::SwitchCodecIfNeeded()
{
    return false;
}

std::string CVideoPlayerAudio::GetPlayerInfo()
{
    std::unique_lock<std::recursive_mutex> lock(m_info_section);
    return m_info.info;
}

int CVideoPlayerAudio::GetAudioBitrate()
{
    return 0;
}

int CVideoPlayerAudio::GetAudioChannels()
{
    return m_streaminfo.channels;
}

bool CVideoPlayerAudio::IsPassthrough()
{
    std::unique_lock<std::recursive_mutex> lock(m_info_section);
    return m_info.passthrough;
}

bool CVideoPlayerAudio::Create(const DVDAudioFrame& audioframe, AVCodecID codec, bool needresampler)
{
    tTVPWaveFormat format;
    format.SamplesPerSec = audioframe.m_sampleRate;
    format.Channels = audioframe.m_channelCount;
    format.BitsPerSample = 0;
    swr_tgtFormat = audioframe.m_dataFormat;
    switch (audioframe.m_dataFormat)
    {
        case AV_SAMPLE_FMT_U8:
            format.BitsPerSample = 8;
            break;
        case AV_SAMPLE_FMT_S16:
            format.BitsPerSample = 16;
            break;
        default:
        {
            AVSampleFormat srcFormat = swr_tgtFormat;
            switch (swr_tgtFormat)
            {
                case AV_SAMPLE_FMT_S16P:
                case AV_SAMPLE_FMT_S32P:
                case AV_SAMPLE_FMT_DBLP:
                case AV_SAMPLE_FMT_FLTP:
                    src_buffer_count = format.Channels;
                    break;
                default:
                    src_buffer_count = 1;
                    break;
            }
            int bitsPerSample = 0;
            switch (swr_tgtFormat)
            {
                case AV_SAMPLE_FMT_U8:
                case AV_SAMPLE_FMT_U8P:
                    swr_tgtFormat = AV_SAMPLE_FMT_U8;
                    bitsPerSample = 8;
                    break;
                default:
                    swr_tgtFormat = AV_SAMPLE_FMT_S16;
                    bitsPerSample = 16;
                    break;
            }
            AVChannelLayout layout;
            av_channel_layout_default(&layout, format.Channels);
            swr_alloc_set_opts2(&swr_ctx, &layout, swr_tgtFormat, format.SamplesPerSec, &layout,
                                srcFormat, format.SamplesPerSec, 0, NULL);

            tgt_frameSize = av_get_bytes_per_sample(swr_tgtFormat) * format.Channels;
            int result = swr_init(swr_ctx);
            assert(swr_ctx && result >= 0);
            format.BitsPerSample = bitsPerSample;
            break;
        }
    }
    format.BytesPerSample = format.BitsPerSample / 8;
    format.TotalSamples = 0;
    format.TotalTime = 0;
    format.SpeakerConfig = 0;
    format.IsFloat = false;
    format.Seekable = false;
    m_soundDevice = TVPCreateSoundBuffer(format, 8);
    if (!m_soundDevice)
        return false;

    return true;
}

void CVideoPlayerAudio::Destroy()
{
    if (swr_ctx)
    {
        swr_free(&swr_ctx);
    }
    if (audio_buf)
    {
        av_freep(&audio_buf);
    }
    if (m_soundDevice)
    {
        delete m_soundDevice;
        m_soundDevice = nullptr;
    }

    m_playingPts = DVD_NOPTS_VALUE;
}

unsigned int CVideoPlayerAudio::AddPackets(const DVDAudioFrame& audioframe)
{
    m_bAbort = false;

    if (!m_soundDevice)
        return 0;

    m_syncErrorTime = 0;
    m_syncError = 0.0;

    // Calculate a timeout when this definitely should be done
    double timeout;
    timeout = DVD_SEC_TO_TIME((double)m_soundDevice->GetLatencySeconds()) + audioframe.duration;
    timeout += DVD_SEC_TO_TIME(1.0);
    timeout += m_pClock->GetAbsoluteClock();

    unsigned int total = audioframe.nb_frames;
    unsigned int frames = audioframe.nb_frames;
    unsigned int offset = 0;
    do
    {
        double pts = (offset == 0) ? audioframe.pts / DVD_TIME_BASE * 1000 : 0.0;
        unsigned int copied = 0;
        do
        {
            _timer.Set(1000);
            while (!m_soundDevice->IsBufferValid())
            {
                std::unique_lock<std::mutex> lk(_mutex);
                _cond.wait_for(lk, std::chrono::milliseconds(10));
                if (_timer.IsTimePast())
                {
                    copied = -1;
                    break;
                }
            }
            if (copied == -1)
            {
                copied = 0;
                break;
            }

            if (swr_ctx)
            {
                uint32_t srcoff = offset * (audioframe.m_frameSize / src_buffer_count);
                const uint8_t* in[8];
                for (unsigned int i = 0; i < src_buffer_count; ++i)
                {
                    in[i] = audioframe.data[i] + srcoff;
                }
                int out_count = frames + 256;
                int out_size = av_samples_get_buffer_size(NULL, audioframe.m_channelCount,
                                                          out_count, swr_tgtFormat, 0);
                av_fast_malloc(&audio_buf, &audio_buf_size, out_size);
                int len2 = swr_convert(swr_ctx, &audio_buf, out_count, in, frames);
                if (len2 == out_count)
                {
                    av_log(NULL, AV_LOG_WARNING, "audio buffer is probably too small\n");
                    swr_init(swr_ctx);
                }
                m_soundDevice->AppendBuffer(audio_buf, len2 * tgt_frameSize);
            }
            else
            {
                m_soundDevice->AppendBuffer(audioframe.data[0] + offset * audioframe.m_frameSize,
                                     frames * audioframe.m_frameSize);
            }

            if (!m_soundDevice->IsPlaying())
            { // out of buffer
                m_soundDevice->Play();
            }
            copied = frames;
            break;
        } while (false);
        offset += copied;
        frames -= copied;
        if (frames <= 0)
            break;

        if (copied == 0 && timeout < m_pClock->GetAbsoluteClock())
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (!m_bAbort);

    m_playingPts = audioframe.pts + audioframe.duration;
    m_timeOfPts = m_pClock->GetAbsoluteClock();

    return total - frames;
}

void CVideoPlayerAudio::Pause()
{
    if (m_soundDevice)
        m_soundDevice->Pause();
    m_playingPts = DVD_NOPTS_VALUE;
}

void CVideoPlayerAudio::Flush()
{
    m_bAbort = true;

    if (m_soundDevice)
    {
        m_soundDevice->Reset();
    }
    m_playingPts = DVD_NOPTS_VALUE;
    m_syncError = 0.0;
    m_syncErrorTime = 0;
}

void CVideoPlayerAudio::AbortAddPackets()
{
    m_bAbort = true;
}

bool CVideoPlayerAudio::IsValidFormat(const DVDAudioFrame& audioframe)
{
    if (!m_soundDevice)
        return false;

    tTVPWaveFormat format;
    format.SamplesPerSec = audioframe.m_sampleRate;
    format.Channels = audioframe.m_channelCount;
    format.BitsPerSample = 0;
    switch (audioframe.m_dataFormat)
    {
        case AV_SAMPLE_FMT_U8:
        case AV_SAMPLE_FMT_U8P:
            format.BitsPerSample = 8;
            break;
        default:
            format.BitsPerSample = 16;
            break;
    }
    return m_soundDevice->IsValidFormat(format);
}

double CVideoPlayerAudio::GetPlayingPts()
{
    if (m_playingPts == DVD_NOPTS_VALUE)
        return 0.0;

    double now = m_pClock->GetAbsoluteClock();
    double diff = now - m_timeOfPts;
    double cache = 1.0;
    double played = 0.0;

    if (diff < cache)
        played = diff;
    else
        played = cache;

    m_timeOfPts = now;
    m_playingPts += played;
    return m_playingPts;
}

double CVideoPlayerAudio::GetSyncError()
{
    return m_syncError;
}

void CVideoPlayerAudio::SetSyncErrorCorrection(double correction)
{
    m_syncError += correction;
}

NS_KRMOVIE_END
