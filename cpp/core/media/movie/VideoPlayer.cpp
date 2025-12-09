#include "tjsCommHead.h"
#include "VideoPlayer.h"

#include "CodecDemuxFFmpeg.h"
#include "InputStream.h"
#include "TVPApplication.h"
#include "VideoPlayerAudio.h"
#include "VideoPlayerVideo.h"
#include "WaveMixer.h"
#include "krmovie.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>

NS_KRMOVIE_BEGIN

#define STREAM_SOURCE_MASK(a) ((a) & 0xf00)

void CSelectionStreams::Clear(AVMediaType type)
{
    std::lock_guard<std::recursive_mutex> lock(m_section);
    auto new_end =
        std::remove_if(m_Streams.begin(), m_Streams.end(), [type](const SelectionStream& stream)
                       { return (type == AVMEDIA_TYPE_UNKNOWN || stream.type == type); });
    m_Streams.erase(new_end, m_Streams.end());
}

int CSelectionStreams::IndexOf(AVMediaType type, int id)
{
    std::lock_guard<std::recursive_mutex> lock(m_section);
    int count = -1;
    for (size_t i = 0; i < m_Streams.size(); i++)
    {
        if (m_Streams[i].type != type)
            continue;
        count++;
        if (id < 0)
            continue;
        if (m_Streams[i].id == id)
            return m_Streams[i].type_index;
    }
    if (id < 0)
        return count;
    else
        return -1;
}

SelectionStream& CSelectionStreams::Get(AVMediaType type, int index)
{
    std::lock_guard<std::recursive_mutex> lock(m_section);
    int count = -1;
    for (size_t i = 0; i < m_Streams.size(); i++)
    {
        if (m_Streams[i].type != type)
            continue;
        count++;
        if (count == index)
            return m_Streams[i];
    }
    return m_invalid;
}

std::vector<SelectionStream> CSelectionStreams::Get(AVMediaType type)
{
    std::vector<SelectionStream> streams;
    std::copy_if(m_Streams.begin(), m_Streams.end(), std::back_inserter(streams),
                 [type](const SelectionStream& stream) { return stream.type == type; });
    return streams;
}

bool CSelectionStreams::Get(AVMediaType type, int flag, SelectionStream& out)
{
    std::lock_guard<std::recursive_mutex> lock(m_section);
    for (size_t i = 0; i < m_Streams.size(); i++)
    {
        if (m_Streams[i].type != type)
            continue;
        if ((m_Streams[i].flags & flag) != flag)
            continue;
        out = m_Streams[i];
        return true;
    }
    return false;
}

void CSelectionStreams::Update(SelectionStream& s)
{
    std::lock_guard<std::recursive_mutex> lock(m_section);
    int index = IndexOf(s.type, s.id);
    if (index >= 0)
    {
        SelectionStream& o = Get(s.type, index);
        s.type_index = o.type_index;
        o = s;
    }
    else
    {
        s.type_index = Count(s.type);
        m_Streams.push_back(s);
    }
}

void CSelectionStreams::Update(InputStream* input, IDemux* demuxer, const std::string& filename2)
{
    if (demuxer)
    {
        const std::string& filename = input->GetFileName();

        for (auto stream : demuxer->GetStreams())
        {
            /* skip streams with no type */
            if (stream->codecpar->codec_type < AVMEDIA_TYPE_VIDEO ||
                stream->codecpar->codec_type > AVMEDIA_TYPE_DATA)
                continue;

            SelectionStream s;
            s.type = stream->codecpar->codec_type;
            s.id = stream->index;
            s.flags = stream->disposition;
            s.filename = filename;
            s.codec = demuxer->GetStreamCodecName(stream->index);
            s.channels = 0; // Default to 0. Overwrite if STREAM_AUDIO below.
            if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            {
                s.width = stream->codecpar->width;
                s.height = stream->codecpar->height;
            }
            if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            {
                std::string type;
                s.name = "";
                s.channels = stream->codecpar->ch_layout.nb_channels;
            }
            Update(s);
        }
    }
}

BasePlayer::BasePlayer(CBaseRenderer* renderer)
  : m_CurrentAudio(AVMEDIA_TYPE_AUDIO, VideoPlayer_AUDIO),
    m_CurrentVideo(AVMEDIA_TYPE_VIDEO, VideoPlayer_VIDEO),
    m_messenger("player"),
    m_pRenderer(renderer)
{
    TVPInitDirectSound(); // to avoid initialize in other thread
    m_playSpeed = DVD_PLAYSPEED_NORMAL;
    m_newPlaySpeed = DVD_PLAYSPEED_NORMAL;
    m_streamPlayerSpeed = DVD_PLAYSPEED_NORMAL;
    m_caching = CACHESTATE_DONE;
    memset(&m_SpeedState, 0, sizeof(m_SpeedState));
}

BasePlayer::~BasePlayer()
{
    CloseInputStream();
    DestroyPlayers();
    ::Application->RegisterActiveEvent(this, nullptr);
}

void BasePlayer::Play()
{
    m_bStopStatus = false;

    Resume();

    if (GetSpeed() == 0)
        SetSpeed(1 /*m_newPlaySpeed*/);
}

void BasePlayer::Stop()
{
    m_bStopStatus = true;
    // pause and rewind
    SetSpeed(0);
    SeekTime(0);
}

void BasePlayer::Pause()
{
    if (GetSpeed() != 0)
        SetSpeed(0);
}

void BasePlayer::GetVideoSize(long* width, long* height)
{
    std::lock_guard<std::recursive_mutex> lock(m_SelectionStreams.m_section);
    int streamId = GetVideoStream();

    if (streamId < 0)
    {
        *width = 0;
        *height = 0;
        return;
    }

    SelectionStream& s = m_SelectionStreams.Get(AVMEDIA_TYPE_VIDEO, streamId);

    *width = s.width;
    *height = s.height;
}

void BasePlayer::SetLoopSegement(int beginFrame, unsigned int endFrame)
{
    m_iLoopSegmentBegin = beginFrame;
    m_iLoopSegmentEnd = endFrame;
}

void BasePlayer::OnDeactive()
{
    m_origSpeed = GetSpeed();
    m_clock.Pause(true);
}

void BasePlayer::OnActive()
{
    m_clock.Pause(false);
    SetSpeed(m_origSpeed);
}

bool BasePlayer::OpenFromStream(tTJSBinaryStream* stream,
                                const tjs_char* streamname,
                                const tjs_char* type,
                                uint64_t size)
{
    if (IsRunning())
        CloseInputStream();

    TJS::tTJSNarrowStringHolder holder(streamname);
    std::string filename = holder.operator const tjs_nchar*();

    m_bAbortRequest = false;
    SetPlaySpeed(DVD_PLAYSPEED_NORMAL);

    m_State.Clear();
    memset(&m_SpeedState, 0, sizeof(m_SpeedState));
    m_offset_pts = 0;
    m_CurrentAudio.lastdts = DVD_NOPTS_VALUE;
    m_CurrentVideo.lastdts = DVD_NOPTS_VALUE;

    m_CurrentVideo.Clear();
    m_CurrentAudio.Clear();

    m_messenger.Init();

    m_playSpeed = DVD_PLAYSPEED_PAUSE; // pause at begin
    m_newPlaySpeed = DVD_PLAYSPEED_PAUSE;
    m_streamPlayerSpeed = DVD_PLAYSPEED_PAUSE;

    OpenInputStream(stream, filename);
    OpenDemuxStream();
    CreatePlayers();
    OpenDefaultStreams();

    UpdatePlayState(0);

    SetCaching(CACHESTATE_FLUSH);

    if (GetTerminated() || m_bAbortRequest)
        return false;

    // ready to play
    m_VideoPlayerVideo->Configure();
    return true;
}

bool BasePlayer::CloseInputStream()
{
    m_bAbortRequest = true;

    // tell demuxer to abort
    if (m_pDemuxer)
        m_pDemuxer->Abort();

    StopThread();

    SAFE_RELEASE(m_pInputStream);

    m_HasVideo = false;
    m_HasAudio = false;

    return true;
}

bool BasePlayer::IsPlaying() const
{
    return !GetTerminated();
}

bool BasePlayer::CanSeek()
{
    std::unique_lock<std::recursive_mutex> lock(m_StateSection);
    return m_State.canseek;
}

void BasePlayer::OnExit()
{
    SetCaching(CACHESTATE_DONE);

    CloseStream(m_CurrentAudio, !m_bAbortRequest);
    CloseStream(m_CurrentVideo, !m_bAbortRequest);

    // destroy objects
    SAFE_DELETE(m_pDemuxer);
    SAFE_RELEASE(m_pInputStream);

    // clean up all selection streams
    m_SelectionStreams.Clear(AVMEDIA_TYPE_UNKNOWN);

    m_messenger.End();
    m_bStopStatus = true;
    m_ready.notify_all();
}

bool BasePlayer::IsValidStream(CCurrentStream& stream)
{
    if (stream.id < 0)
        return true; // we consider non selected as valid

    AVStream* st = m_pDemuxer->GetStream(stream.id);
    if (st == nullptr)
        return false;
    if (st->codecpar->codec_type != stream.type)
        return false;

    return true;
}

bool BasePlayer::IsBetterStream(CCurrentStream& current, AVStream* stream)
{
    if (stream->index == current.id)
        return false;

    if (stream->codecpar->codec_type != current.type)
        return false;
    if (current.id < 0)
        return true;

    return false;
}

void BasePlayer::CheckBetterStream(CCurrentStream& current, AVStream* stream)
{
    IDVDStreamPlayer* player = GetStreamPlayer(current.player);
    if (!IsValidStream(current) && (player == NULL || player->IsStalled()))
        CloseStream(current, true);

    if (IsBetterStream(current, stream))
        OpenStream(current, stream->index);
}

void BasePlayer::CheckStreamChanges(CCurrentStream& current, AVStream* stream)
{
    if (current.stream != (void*)stream)
    {
        /* check so that dmuxer hints or extra data hasn't changed */
        /* if they have, reopen stream */

        if (current.hint != CDVDStreamInfo(*stream, true))
        {
            m_SelectionStreams.Clear(AVMEDIA_TYPE_UNKNOWN);
            m_SelectionStreams.Update(m_pInputStream, m_pDemuxer);
            OpenDefaultStreams(false);
        }

        current.stream = (void*)stream;
    }
}

void BasePlayer::Execute()
{

    while (!m_bAbortRequest)
    {
        // handle messages send to this thread, like seek or demuxer reset requests
        HandleMessages();

        if (m_bAbortRequest)
            break;

        // should we open a new demuxer?
        if (!m_pDemuxer)
        {
            if (OpenDemuxStream() == false)
            {
                m_bAbortRequest = true;
                break;
            }

            // on channel switch we don't want to close stream players at this
            // time. we'll get the stream change event later
            OpenDefaultStreams();

            //			UpdateApplication(0);
            UpdatePlayState(0);
        }

        // handle eventual seeks due to playspeed
        HandlePlaySpeed();

        // update player state
        UpdatePlayState(200);

        // if the queues are full, no need to read more
        if ((!m_VideoPlayerAudio->AcceptsData() && m_CurrentAudio.id >= 0) ||
            (!m_VideoPlayerVideo->AcceptsData() && m_CurrentVideo.id >= 0))
        {
            Sleep(10);
            continue;
        }

        // always yield to players if they have data levels > 50 percent
        if ((m_VideoPlayerAudio->GetLevel() > 50 || m_CurrentAudio.id < 0) &&
            (m_VideoPlayerVideo->GetLevel() > 50 || m_CurrentVideo.id < 0))
            Sleep(0);

        DemuxPacket* pPacket = NULL;
        AVStream* pStream = NULL;
        ReadPacket(pPacket, pStream);
        if (pPacket && !pStream)
        {
            /* probably a empty packet, just free it and move on */
            DemuxPacket::Free(pPacket);
            continue;
        }

        if (!pPacket)
        {
            // when paused, demuxer could be be returning empty
            if (m_playSpeed == DVD_PLAYSPEED_PAUSE)
                continue;

            if (m_iLoopSegmentBegin != -1)
            { // process loop info
                double start = DVD_NOPTS_VALUE;
                if (m_pDemuxer &&
                    m_pDemuxer->SeekTime(m_iLoopSegmentBegin / GetFPS() * DVD_PLAYSPEED_NORMAL,
                                         true, &start))
                {
                    continue;
                }
            }

            if (m_CurrentAudio.inited)
                m_VideoPlayerAudio->SendMessage(new CDVDMsg(CDVDMsg::GENERAL_EOF));
            if (m_CurrentVideo.inited)
                m_VideoPlayerVideo->SendMessage(new CDVDMsg(CDVDMsg::GENERAL_EOF));

            m_CurrentAudio.inited = false;
            m_CurrentVideo.inited = false;

            // if we are caching, start playing it again
            SetCaching(CACHESTATE_DONE);

            // while players are still playing, keep going to allow seekbacks
            if (m_VideoPlayerVideo->HasData() || m_VideoPlayerAudio->HasData())
            {
                Sleep(100);
                continue;
            }
            // TODO process loop info
            SetSpeed(0);
            SeekTime(0); // rewind
            if (m_callback)
                m_callback(KRMovieEvent::Ended, nullptr);
            continue;
        }

        // it's a valid data packet, reset error counter
        m_errorCount = 0;

        // see if we can find something better to play
        CheckBetterStream(m_CurrentAudio, pStream);
        CheckBetterStream(m_CurrentVideo, pStream);

        // demux video stream

        // process the packet
        ProcessPacket(pStream, pPacket);
    }
}

void BasePlayer::CreatePlayers()
{
    if (m_players_created)
        return;

    {
        m_VideoPlayerVideo = new CVideoPlayerVideo(&m_clock, m_messenger, m_pRenderer);
        m_VideoPlayerAudio = new CVideoPlayerAudio(&m_clock, m_messenger);
    }
    m_players_created = true;
}

void BasePlayer::DestroyPlayers()
{
    if (!m_players_created)
        return;

    delete m_VideoPlayerVideo;
    delete m_VideoPlayerAudio;

    m_players_created = false;
}

bool BasePlayer::OpenStream(CCurrentStream& current, int iStream, bool reset /*= true*/)
{
    AVStream* stream = NULL;
    CDVDStreamInfo hint;

    if (!m_pDemuxer)
        return false;
    stream = m_pDemuxer->GetStream(iStream);
    if (!stream)
        return false;
    hint.Assign(*stream, true);

    bool res;
    switch (current.type)
    {
        case AVMEDIA_TYPE_AUDIO:
            res = OpenAudioStream(hint, reset);
            break;
        case AVMEDIA_TYPE_VIDEO:
            res = OpenVideoStream(hint, reset);
            break;
        default:
            res = false;
            break;
    }

    if (res)
    {
        current.id = iStream;
        current.hint = hint;
        current.stream = (void*)stream;
        current.lastdts = DVD_NOPTS_VALUE;
        if (current.avsync != CCurrentStream::AV_SYNC_FORCE)
            current.avsync = CCurrentStream::AV_SYNC_CHECK;
    }

    return res;
}

bool BasePlayer::OpenAudioStream(CDVDStreamInfo& hint, bool reset)
{
    IDVDStreamPlayer* player = GetStreamPlayer(m_CurrentAudio.player);
    if (player == nullptr)
        return false;

    if (m_CurrentAudio.id < 0 || m_CurrentAudio.hint != hint)
    {
        if (!player->OpenStream(hint))
            return false;

        static_cast<IDVDStreamPlayerAudio*>(player)->SetSpeed(m_streamPlayerSpeed);
        m_CurrentAudio.syncState = IDVDStreamPlayer::SYNC_STARTING;
        m_CurrentAudio.packets = 0;
    }
    else if (reset)
        player->SendMessage(new CDVDMsg(CDVDMsg::GENERAL_RESET), 0);

    m_HasAudio = true;

    return true;
}

bool BasePlayer::OpenVideoStream(CDVDStreamInfo& hint, bool reset)
{
    SelectionStream& s = m_SelectionStreams.Get(AVMEDIA_TYPE_VIDEO, 0);

    IDVDStreamPlayer* player = GetStreamPlayer(m_CurrentVideo.player);
    if (player == nullptr)
        return false;

    if (m_CurrentVideo.id < 0 || m_CurrentVideo.hint != hint)
    {
        if (!player->OpenStream(hint))
            return false;

        s.stereo_mode = static_cast<IDVDStreamPlayerVideo*>(player)->GetStereoMode();
        if (s.stereo_mode == "mono")
            s.stereo_mode = "";

        static_cast<IDVDStreamPlayerVideo*>(player)->SetSpeed(m_streamPlayerSpeed);
        m_CurrentVideo.syncState = IDVDStreamPlayer::SYNC_STARTING;
        m_CurrentVideo.packets = 0;
    }
    else if (reset)
        player->SendMessage(new CDVDMsg(CDVDMsg::GENERAL_RESET), 0);

    m_HasVideo = true;

    return true;
}

bool BasePlayer::CloseStream(CCurrentStream& current, bool bWaitForBuffers)
{
    if (current.id < 0)
        return false;

    if (bWaitForBuffers)
        SetCaching(CACHESTATE_DONE);

    IDVDStreamPlayer* player = GetStreamPlayer(current.player);
    if (player)
    {
        if ((current.type == AVMEDIA_TYPE_AUDIO &&
             current.syncState != IDVDStreamPlayer::SYNC_INSYNC) ||
            (current.type == AVMEDIA_TYPE_VIDEO &&
             current.syncState != IDVDStreamPlayer::SYNC_INSYNC))
            bWaitForBuffers = false;
        player->CloseStream(bWaitForBuffers);
    }

    current.Clear();
    return true;
}

bool BasePlayer::CheckIsCurrent(CCurrentStream& current, AVStream* stream, DemuxPacket* pkg)
{
    if (current.id == pkg->iStreamId && current.type == stream->codecpar->codec_type)
        return true;
    else
        return false;
}

void BasePlayer::ProcessPacket(AVStream* pStream, DemuxPacket* pPacket)
{
    // process packet if it belongs to selected stream.
    // for dvd's don't allow automatic opening of streams*/

    if (CheckIsCurrent(m_CurrentAudio, pStream, pPacket))
        ProcessAudioData(pStream, pPacket);
    else if (CheckIsCurrent(m_CurrentVideo, pStream, pPacket))
        ProcessVideoData(pStream, pPacket);
    else
    {
        DemuxPacket::Free(pPacket); // free it since we won't do anything with it
    }
}

void BasePlayer::ProcessAudioData(AVStream* pStream, DemuxPacket* pPacket)
{
    CheckStreamChanges(m_CurrentAudio, pStream);

    bool checkcont = CheckContinuity(m_CurrentAudio, pPacket);
    UpdateTimestamps(m_CurrentAudio, pPacket);

    if (checkcont && (m_CurrentAudio.avsync == CCurrentStream::AV_SYNC_CHECK))
        m_CurrentAudio.avsync = CCurrentStream::AV_SYNC_NONE;

    bool drop = false;
    if (CheckPlayerInit(m_CurrentAudio))
        drop = true;

    m_VideoPlayerAudio->SendMessage(new CDVDMsgDemuxerPacket(pPacket, drop));
    m_CurrentAudio.packets++;
}

void BasePlayer::ProcessVideoData(AVStream* pStream, DemuxPacket* pPacket)
{
    CheckStreamChanges(m_CurrentVideo, pStream);
    bool checkcont = false;

    if (pPacket->iSize != 4) // don't check the EOF_SEQUENCE of stillframes
    {
        checkcont = CheckContinuity(m_CurrentVideo, pPacket);
        UpdateTimestamps(m_CurrentVideo, pPacket);
    }
    if (checkcont && (m_CurrentVideo.avsync == CCurrentStream::AV_SYNC_CHECK))
        m_CurrentVideo.avsync = CCurrentStream::AV_SYNC_NONE;

    bool drop = false;
    if (CheckPlayerInit(m_CurrentVideo))
        drop = true;

    m_VideoPlayerVideo->SendMessage(new CDVDMsgDemuxerPacket(pPacket, drop));
    m_CurrentVideo.packets++;
}

void BasePlayer::SetPlaySpeed(int speed)
{
    if (IsPlaying())
        m_messenger.Put(new CDVDMsgInt(CDVDMsg::PLAYER_SETSPEED, speed));
    else
    {
        m_playSpeed = speed;
        m_newPlaySpeed = speed;
        m_streamPlayerSpeed = speed;
    }
}

void BasePlayer::FlushBuffers(bool queued, double pts, bool accurate, bool sync)
{
    double startpts;
    if (accurate)
        startpts = pts;
    else
        startpts = DVD_NOPTS_VALUE;

    if (sync)
    {
        m_CurrentAudio.inited = false;
        m_CurrentAudio.avsync = CCurrentStream::AV_SYNC_FORCE;
        m_CurrentVideo.inited = false;
        m_CurrentVideo.avsync = CCurrentStream::AV_SYNC_FORCE;
    }

    m_CurrentAudio.dts = DVD_NOPTS_VALUE;
    m_CurrentAudio.startpts = startpts;
    m_CurrentAudio.packets = 0;

    m_CurrentVideo.dts = DVD_NOPTS_VALUE;
    m_CurrentVideo.startpts = startpts;
    m_CurrentVideo.packets = 0;

    if (queued)
    {
        m_VideoPlayerAudio->SendMessage(new CDVDMsg(CDVDMsg::GENERAL_RESET));
        m_VideoPlayerVideo->SendMessage(new CDVDMsg(CDVDMsg::GENERAL_RESET));

        int sources = 0;
        if (m_VideoPlayerVideo->IsInited())
            sources |= SYNCSOURCE_VIDEO;
        if (m_VideoPlayerAudio->IsInited())
            sources |= SYNCSOURCE_AUDIO;

        CDVDMsgGeneralSynchronize* msg = new CDVDMsgGeneralSynchronize(10 * 1000, sources);
        m_VideoPlayerAudio->SendMessage(msg->AddRef(), 1);
        m_VideoPlayerVideo->SendMessage(msg->AddRef(), 1);
        msg->Wait(GetTerminated(), 0);
        msg->Release();
    }
    else
    {
        m_VideoPlayerAudio->Flush(sync);
        m_VideoPlayerVideo->Flush(sync);

        if (m_playSpeed == DVD_PLAYSPEED_NORMAL || m_playSpeed == DVD_PLAYSPEED_PAUSE)
        {
            // make sure players are properly flushed, should put them in stalled state
            int sources = 0;
            if (m_VideoPlayerVideo->IsInited())
                sources |= SYNCSOURCE_VIDEO;
            if (m_VideoPlayerAudio->IsInited())
                sources |= SYNCSOURCE_AUDIO;
            CDVDMsgGeneralSynchronize* msg = new CDVDMsgGeneralSynchronize(1000, sources);
            m_VideoPlayerAudio->SendMessage(msg->AddRef(), 1);
            m_VideoPlayerVideo->SendMessage(msg->AddRef(), 1);
            msg->Wait(GetTerminated(), 0);
            msg->Release();

            // purge any pending PLAYER_STARTED messages
            m_messenger.Flush(CDVDMsg::PLAYER_STARTED);

            // we should now wait for init cache
            SetCaching(CACHESTATE_FLUSH);
            if (sync)
            {
                m_CurrentAudio.syncState = IDVDStreamPlayer::SYNC_STARTING;
                m_CurrentVideo.syncState = IDVDStreamPlayer::SYNC_STARTING;
            }
        }

        if (pts != DVD_NOPTS_VALUE && sync)
            m_clock.Discontinuity(pts);
        UpdatePlayState(0);
    }
}

void BasePlayer::HandleMessages()
{
    CDVDMsg* pMsg;

    while (m_messenger.Get(&pMsg, 0) == MSGQ_OK)
    {

        if (pMsg->IsType(CDVDMsg::PLAYER_SEEK) &&
            m_messenger.GetPacketCount(CDVDMsg::PLAYER_SEEK) == 0 &&
            m_messenger.GetPacketCount(CDVDMsg::PLAYER_SEEK_CHAPTER) == 0)
        {
            CDVDMsgPlayerSeek& msg(*((CDVDMsgPlayerSeek*)pMsg));

            if (!m_State.canseek)
            {
                pMsg->Release();
                continue;
            }

            // skip seeks if player has not finished the last seek
            if (m_CurrentVideo.id >= 0 && m_CurrentVideo.syncState != IDVDStreamPlayer::SYNC_INSYNC)
            {
                double now = m_clock.GetAbsoluteClock();
                if (m_playSpeed == DVD_PLAYSPEED_NORMAL &&
                    DVD_TIME_TO_MSEC(now - m_State.lastSeek) < 2000 && !msg.GetAccurate())
                {
                    pMsg->Release();
                    continue;
                }
            }

            if (!msg.GetTrickPlay())
            {
                if (msg.GetFlush())
                    SetCaching(CACHESTATE_FLUSH);
            }

            double start = DVD_NOPTS_VALUE;

            int time = msg.GetTime();
            if (msg.GetRelative())
                time = GetTime() + time;

            time -= DVD_TIME_TO_MSEC(m_State.time_offset);

            if (m_pDemuxer && m_pDemuxer->SeekTime(time, msg.GetBackward(), &start))
            {
                // dts after successful seek
                if (start == DVD_NOPTS_VALUE)
                    start = DVD_MSEC_TO_TIME(time) - m_State.time_offset;

                m_State.dts = start;
                m_State.lastSeek = m_clock.GetAbsoluteClock();

                FlushBuffers(!msg.GetFlush(), start, msg.GetAccurate(), msg.GetSync());
            }
            else if (m_pDemuxer)
            {
                if (start == DVD_NOPTS_VALUE)
                    start = DVD_MSEC_TO_TIME(time) - m_State.time_offset;

                m_State.dts = start;

                FlushBuffers(false, start, false, true);
                if (m_playSpeed != DVD_PLAYSPEED_PAUSE)
                {
                    SetPlaySpeed(DVD_PLAYSPEED_NORMAL);
                }
            }
        }
        else if (pMsg->IsType(CDVDMsg::DEMUXER_RESET))
        {
            m_CurrentAudio.stream = NULL;
            m_CurrentVideo.stream = NULL;

            if (m_pDemuxer)
                m_pDemuxer->Reset();
        }
        else if (pMsg->IsType(CDVDMsg::PLAYER_SET_AUDIOSTREAM))
        {
            CDVDMsgPlayerSetAudioStream* pMsg2 = (CDVDMsgPlayerSetAudioStream*)pMsg;

            SelectionStream& st = m_SelectionStreams.Get(AVMEDIA_TYPE_AUDIO, pMsg2->GetStreamId());

            CloseStream(m_CurrentAudio, false);
            OpenStream(m_CurrentAudio, st.demuxerId, st.id);

            CDVDMsgPlayerSeek::CMode mode;
            mode.time = (int)GetTime();
            mode.backward = true;
            mode.flush = true;
            mode.accurate = true;
            mode.trickplay = true;
            mode.sync = true;
            m_messenger.Put(new CDVDMsgPlayerSeek(mode));
        }
        else if (pMsg->IsType(CDVDMsg::PLAYER_SET_VIDEOSTREAM))
        {
            CDVDMsgPlayerSetVideoStream* pMsg2 = (CDVDMsgPlayerSetVideoStream*)pMsg;

            SelectionStream& st = m_SelectionStreams.Get(AVMEDIA_TYPE_VIDEO, pMsg2->GetStreamId());

            CloseStream(m_CurrentVideo, false);
            OpenStream(m_CurrentVideo, st.demuxerId, st.id);
            CDVDMsgPlayerSeek::CMode mode;
            mode.time = (int)GetTime();
            mode.backward = true;
            mode.flush = true;
            mode.accurate = true;
            mode.trickplay = true;
            mode.sync = true;
            m_messenger.Put(new CDVDMsgPlayerSeek(mode));
        }
        else if (pMsg->IsType(CDVDMsg::GENERAL_FLUSH))
        {
            FlushBuffers(false);
        }
        else if (pMsg->IsType(CDVDMsg::PLAYER_SETSPEED))
        {
            int speed = static_cast<CDVDMsgInt*>(pMsg)->m_value;

            if (m_State.timestamp > 0)
            {
                double offset;
                offset = m_clock.GetAbsoluteClock() - m_State.timestamp;
                offset *= m_playSpeed / DVD_PLAYSPEED_NORMAL;
                offset = DVD_TIME_TO_MSEC(offset);
                if (offset > 1000)
                    offset = 1000;
                if (offset < -1000)
                    offset = -1000;
                m_State.time += offset;
                m_State.timestamp = m_clock.GetAbsoluteClock();
            }

            // do a seek after rewind, clock is not in sync with current pts
            if ((speed == DVD_PLAYSPEED_NORMAL) && (m_playSpeed != DVD_PLAYSPEED_NORMAL) &&
                (m_playSpeed != DVD_PLAYSPEED_PAUSE)
                /* && !m_omxplayer_mode*/)
            {
                int64_t iTime = (int64_t)DVD_TIME_TO_MSEC(m_clock.GetClock() + m_State.time_offset);
                if (m_State.time != DVD_NOPTS_VALUE)
                    iTime = m_State.time;

                CDVDMsgPlayerSeek::CMode mode;
                mode.time = iTime;
                mode.backward = m_playSpeed < 0;
                mode.flush = true;
                mode.accurate = false;
                mode.trickplay = true;
                mode.sync = true;
                m_messenger.Put(new CDVDMsgPlayerSeek(mode));
            }
            m_playSpeed = speed;
            m_newPlaySpeed = speed;
            m_caching = CACHESTATE_DONE;
            m_clock.SetSpeed(speed);
            m_VideoPlayerAudio->SetSpeed(speed);
            m_VideoPlayerVideo->SetSpeed(speed);
            m_streamPlayerSpeed = speed;
            if (m_pDemuxer)
                m_pDemuxer->SetSpeed(speed);
        }
        else if (pMsg->IsType(CDVDMsg::PLAYER_STARTED))
        {
            SStartMsg& msg = ((CDVDMsgType<SStartMsg>*)pMsg)->m_value;
            if (msg.player == VideoPlayer_AUDIO)
            {
                m_CurrentAudio.syncState = IDVDStreamPlayer::SYNC_WAITSYNC;
                m_CurrentAudio.cachetime = msg.cachetime;
                m_CurrentAudio.cachetotal = msg.cachetotal;
                m_CurrentAudio.starttime = msg.timestamp;
            }
            if (msg.player == VideoPlayer_VIDEO)
            {
                m_CurrentVideo.syncState = IDVDStreamPlayer::SYNC_WAITSYNC;
                m_CurrentVideo.cachetime = msg.cachetime;
                m_CurrentVideo.cachetotal = msg.cachetotal;
                m_CurrentVideo.starttime = msg.timestamp;
            }
        }
        else if (pMsg->IsType(CDVDMsg::GENERAL_SYNCHRONIZE))
        {
            ((CDVDMsgGeneralSynchronize*)pMsg)->Wait(100, SYNCSOURCE_PLAYER);
        }
        else if (pMsg->IsType(CDVDMsg::PLAYER_AVCHANGE))
        {
            UpdateStreamInfos();
        }

        pMsg->Release();
    }
}

void BasePlayer::SetCaching(ECacheState state)
{
    if (state == CACHESTATE_FLUSH)
    {
        double level, delay, offset;
        if (GetCachingTimes(level, delay, offset))
            state = CACHESTATE_FULL;
        else
            state = CACHESTATE_INIT;
    }

    if (m_caching == state)
        return;

    if (state == CACHESTATE_FULL || state == CACHESTATE_INIT)
    {
        m_clock.SetSpeed(DVD_PLAYSPEED_PAUSE);
        m_VideoPlayerAudio->SetSpeed(DVD_PLAYSPEED_PAUSE);
        m_VideoPlayerVideo->SetSpeed(DVD_PLAYSPEED_PAUSE);
        m_streamPlayerSpeed = DVD_PLAYSPEED_PAUSE;

        m_cachingTimer.Set(5000);
    }

    if (state == CACHESTATE_PLAY || (state == CACHESTATE_DONE && m_caching != CACHESTATE_PLAY))
    {
        m_clock.SetSpeed(m_playSpeed);
        m_VideoPlayerAudio->SetSpeed(m_playSpeed);
        m_VideoPlayerVideo->SetSpeed(m_playSpeed);
        m_streamPlayerSpeed = m_playSpeed;
    }
    m_caching = state;
}

double BasePlayer::GetQueueTime()
{
    int a = m_VideoPlayerAudio->GetLevel();
    int v = m_VideoPlayerVideo->GetLevel();
    return std::max(a, v) * 8000.0 / 100;
}

bool BasePlayer::GetCachingTimes(double& level, double& delay, double& offset)
{
    return false;
}

void BasePlayer::SetSpeed(double speed)
{
    if (!CanSeek())
        return;

    m_newPlaySpeed = speed * DVD_PLAYSPEED_NORMAL;
    SetPlaySpeed(speed * DVD_PLAYSPEED_NORMAL);
}

double BasePlayer::GetSpeed()
{
    if (m_playSpeed != m_newPlaySpeed)
        return (double)m_newPlaySpeed / DVD_PLAYSPEED_NORMAL;

    return (double)m_playSpeed / DVD_PLAYSPEED_NORMAL;
}

void BasePlayer::SeekTime(int64_t iTime)
{
    int seekOffset = (int)(iTime - GetTime());

    CDVDMsgPlayerSeek::CMode mode;
    mode.time = (int)iTime;
    mode.backward = true;
    mode.flush = true;
    mode.accurate = true;
    mode.trickplay = false;
    mode.sync = true;

    m_messenger.Put(new CDVDMsgPlayerSeek(mode));
    SynchronizeDemuxer();
    OnPlayBackSeek((int)iTime, seekOffset);
}

// return the time in milliseconds
int64_t BasePlayer::GetTime()
{
    std::unique_lock<std::recursive_mutex> lock(m_StateSection);
    double offset = 0;
    const double limit = DVD_MSEC_TO_TIME(500);
    if (m_State.timestamp > 0)
    {
        offset = m_clock.GetAbsoluteClock() - m_State.timestamp;
        offset *= m_playSpeed / DVD_PLAYSPEED_NORMAL;
        if (offset > limit)
            offset = limit;
        if (offset < -limit)
            offset = -limit;
    }
    return llrint(m_State.time + DVD_TIME_TO_MSEC(offset));
}

int BasePlayer::GetCurrentFrame()
{
    // TODO accuracy
    return GetTime() * GetFPS() / DVD_PLAYSPEED_NORMAL;
}

int BasePlayer::GetVideoStream()
{
    return m_SelectionStreams.IndexOf(AVMEDIA_TYPE_VIDEO, m_CurrentVideo.id);
}

int BasePlayer::GetVideoStreamCount()
{
    return m_SelectionStreams.Count(AVMEDIA_TYPE_VIDEO);
}

int BasePlayer::GetAudioStreamCount()
{
    return m_SelectionStreams.Count(AVMEDIA_TYPE_AUDIO);
}

int BasePlayer::GetAudioStream()
{
    return m_SelectionStreams.IndexOf(AVMEDIA_TYPE_AUDIO, m_CurrentAudio.id);
}

void BasePlayer::HandlePlaySpeed()
{
    if (m_caching == CACHESTATE_FULL)
    {
        double level, delay, offset;
        if (GetCachingTimes(level, delay, offset))
        {
            if (level < 0.0)
            {
                //	CGUIDialogKaiToast::QueueNotification(g_localizeStrings.Get(21454),
                // g_localizeStrings.Get(21455));
                SetCaching(CACHESTATE_INIT);
            }
            if (level >= 1.0)
                SetCaching(CACHESTATE_INIT);
        }
        else
        {
            if ((!m_VideoPlayerAudio->AcceptsData() && m_CurrentAudio.id >= 0) ||
                (!m_VideoPlayerVideo->AcceptsData() && m_CurrentVideo.id >= 0))
                SetCaching(CACHESTATE_INIT);
        }
    }

    if (m_caching == CACHESTATE_INIT)
    {
        // if all enabled streams have been inited we are done
        if ((m_CurrentVideo.id >= 0 || m_CurrentAudio.id >= 0) &&
            (m_CurrentVideo.id < 0 ||
             m_CurrentVideo.syncState != IDVDStreamPlayer::SYNC_STARTING) &&
            (m_CurrentAudio.id < 0 || m_CurrentAudio.syncState != IDVDStreamPlayer::SYNC_STARTING))
            SetCaching(CACHESTATE_PLAY);

        // handle exceptions
        if (m_CurrentAudio.id >= 0 && m_CurrentVideo.id >= 0)
        {
            if ((!m_VideoPlayerAudio->AcceptsData() || !m_VideoPlayerVideo->AcceptsData()) &&
                m_cachingTimer.IsTimePast())
            {
                SetCaching(CACHESTATE_DONE);
            }
        }
    }

    if (m_caching == CACHESTATE_PLAY)
    {
        // if all enabled streams have started playing we are done
        if ((m_CurrentVideo.id < 0 || !m_VideoPlayerVideo->IsStalled()) &&
            (m_CurrentAudio.id < 0 || !m_VideoPlayerAudio->IsStalled()))
            SetCaching(CACHESTATE_DONE);
    }

    if (m_caching == CACHESTATE_DONE)
    {
        if (m_playSpeed == DVD_PLAYSPEED_NORMAL /*&& !isInMenu*/)
        {
            // take action is audio or video stream is stalled
            if (((m_VideoPlayerAudio->IsStalled() && m_CurrentAudio.inited) ||
                 (m_VideoPlayerVideo->IsStalled() && m_CurrentVideo.inited)) &&
                m_syncTimer.IsTimePast())
            {
                {
                    // start caching if audio and video have run dry
                    if (m_VideoPlayerAudio->GetLevel() <= 50 &&
                        m_VideoPlayerVideo->GetLevel() <= 50)
                    {
                        SetCaching(CACHESTATE_FULL);
                    }
                    else if (m_CurrentAudio.id >= 0 && m_CurrentAudio.inited &&
                             m_CurrentAudio.syncState == IDVDStreamPlayer::SYNC_INSYNC &&
                             m_VideoPlayerAudio->GetLevel() == 0)
                    {
                        FlushBuffers(false);
                        CDVDMsgPlayerSeek::CMode mode;
                        mode.time = (int)GetTime();
                        mode.backward = false;
                        mode.flush = true;
                        mode.accurate = true;
                        mode.sync = true;
                        m_messenger.Put(new CDVDMsgPlayerSeek(mode));
                    }
                }
            }
        }
    }

    // sync streams to clock
    if ((m_CurrentVideo.syncState == IDVDStreamPlayer::SYNC_WAITSYNC) ||
        (m_CurrentAudio.syncState == IDVDStreamPlayer::SYNC_WAITSYNC))
    {
        unsigned int threshold = 20;

        bool video = m_CurrentVideo.id < 0 ||
                     (m_CurrentVideo.syncState == IDVDStreamPlayer::SYNC_WAITSYNC) ||
                     (m_CurrentVideo.packets == 0 && m_CurrentAudio.packets > threshold);
        bool audio = m_CurrentAudio.id < 0 ||
                     (m_CurrentAudio.syncState == IDVDStreamPlayer::SYNC_WAITSYNC) ||
                     (m_CurrentAudio.packets == 0 && m_CurrentVideo.packets > threshold);

        if (m_CurrentAudio.syncState == IDVDStreamPlayer::SYNC_WAITSYNC &&
            m_CurrentAudio.avsync == CCurrentStream::AV_SYNC_CONT)
        {
            m_CurrentAudio.syncState = IDVDStreamPlayer::SYNC_INSYNC;
            m_CurrentAudio.avsync = CCurrentStream::AV_SYNC_NONE;
            m_VideoPlayerAudio->SendMessage(
                new CDVDMsgDouble(CDVDMsg::GENERAL_RESYNC, m_clock.GetClock()), 1);
        }
        else if (m_CurrentVideo.syncState == IDVDStreamPlayer::SYNC_WAITSYNC &&
                 m_CurrentVideo.avsync == CCurrentStream::AV_SYNC_CONT)
        {
            m_CurrentVideo.syncState = IDVDStreamPlayer::SYNC_INSYNC;
            m_CurrentVideo.avsync = CCurrentStream::AV_SYNC_NONE;
            m_VideoPlayerVideo->SendMessage(
                new CDVDMsgDouble(CDVDMsg::GENERAL_RESYNC, m_clock.GetClock()), 1);
        }
        else if (video && audio)
        {
            double clock = 0;

            if (m_CurrentVideo.starttime != DVD_NOPTS_VALUE && m_CurrentVideo.packets > 0 &&
                m_playSpeed == DVD_PLAYSPEED_PAUSE)
            {
                clock = m_CurrentVideo.starttime;
            }
            else if (m_CurrentAudio.starttime != DVD_NOPTS_VALUE && m_CurrentAudio.packets > 0)
            {
                clock = m_CurrentAudio.starttime - m_CurrentAudio.cachetime;
                if (m_CurrentVideo.starttime != DVD_NOPTS_VALUE && (m_CurrentVideo.packets > 0) &&
                    m_CurrentVideo.starttime - m_CurrentVideo.cachetotal < clock)
                {
                    clock = m_CurrentVideo.starttime - m_CurrentVideo.cachetotal;
                }
            }
            else if (m_CurrentVideo.starttime != DVD_NOPTS_VALUE && m_CurrentVideo.packets > 0)
            {
                clock = m_CurrentVideo.starttime - m_CurrentVideo.cachetotal;
            }
            m_clock.Discontinuity(clock);
            m_CurrentAudio.syncState = IDVDStreamPlayer::SYNC_INSYNC;
            m_CurrentAudio.avsync = CCurrentStream::AV_SYNC_NONE;
            m_CurrentVideo.syncState = IDVDStreamPlayer::SYNC_INSYNC;
            m_CurrentVideo.avsync = CCurrentStream::AV_SYNC_NONE;
            m_VideoPlayerAudio->SendMessage(new CDVDMsgDouble(CDVDMsg::GENERAL_RESYNC, clock), 1);
            m_VideoPlayerVideo->SendMessage(new CDVDMsgDouble(CDVDMsg::GENERAL_RESYNC, clock), 1);
            SetCaching(CACHESTATE_DONE);

            m_syncTimer.Set(3000);
        }
        else
        {
            // exceptions for which stream players won't start properly
            // 1. videoplayer has not detected a keyframe within lenght of demux buffers
            if (m_CurrentAudio.id >= 0 && m_CurrentVideo.id >= 0 &&
                !m_VideoPlayerAudio->AcceptsData() && m_VideoPlayerVideo->IsStalled())
            {
                FlushBuffers(false);
            }
        }
    }

    // handle ff/rw
    if (m_playSpeed != DVD_PLAYSPEED_NORMAL && m_playSpeed != DVD_PLAYSPEED_PAUSE)
    {
        // 		if (isInMenu)
        // 		{
        // 			// this can't be done in menu
        // 			SetPlaySpeed(DVD_PLAYSPEED_NORMAL);
        //
        // 		} else
        {
            bool check = true;

            // only check if we have video
            if (m_CurrentVideo.id < 0 || m_CurrentVideo.syncState != IDVDStreamPlayer::SYNC_INSYNC)
                check = false;
            // video message queue either initiated or already seen eof
            else if (m_CurrentVideo.inited == false && m_playSpeed >= 0)
                check = false;
            // don't check if time has not advanced since last check
            else if (m_SpeedState.lasttime == GetTime())
                check = false;
            // skip if frame at screen has no valid timestamp
            else if (m_VideoPlayerVideo->GetCurrentPts() == DVD_NOPTS_VALUE)
                check = false;
            // skip if frame on screen has not changed
            else if (m_SpeedState.lastpts == m_VideoPlayerVideo->GetCurrentPts() &&
                     (m_SpeedState.lastpts > m_State.dts || m_playSpeed > 0))
                check = false;

            if (check)
            {
                m_SpeedState.lastpts = m_VideoPlayerVideo->GetCurrentPts();
                m_SpeedState.lasttime = GetTime();
                m_SpeedState.lastabstime = m_clock.GetAbsoluteClock();
                // check how much off clock video is when ff/rw:ing
                // a problem here is that seeking isn't very accurate
                // and since the clock will be resynced after seek
                // we might actually not really be playing at the wanted
                // speed. we'd need to have some way to not resync the clock
                // after a seek to remember timing. still need to handle
                // discontinuities somehow

                double error;
                error = m_clock.GetClock() - m_SpeedState.lastpts;
                error *= m_playSpeed / abs(m_playSpeed);

                // allow a bigger error when going ff, the faster we go
                // the the bigger is the error we allow
                if (m_playSpeed > DVD_PLAYSPEED_NORMAL)
                {
                    int errorwin = m_playSpeed / DVD_PLAYSPEED_NORMAL;
                    if (errorwin > 8)
                        errorwin = 8;
                    error /= errorwin;
                }

                if (error > DVD_MSEC_TO_TIME(1000))
                {
                    error = (int)DVD_TIME_TO_MSEC(m_clock.GetClock()) - m_SpeedState.lastseekpts;

                    if (std::abs(error) > 1000 ||
                        (m_VideoPlayerVideo->IsRewindStalled() && std::abs(error) > 100))
                    {
                        m_SpeedState.lastseekpts = (int)DVD_TIME_TO_MSEC(m_clock.GetClock());
                        int direction = (m_playSpeed > 0) ? 1 : -1;
                        int iTime = DVD_TIME_TO_MSEC(m_clock.GetClock() + m_State.time_offset +
                                                     1000000.0 * direction);
                        CDVDMsgPlayerSeek::CMode mode;
                        mode.time = iTime;
                        mode.backward = (GetPlaySpeed() < 0);
                        mode.flush = true;
                        mode.accurate = false;
                        mode.restore = false;
                        mode.trickplay = true;
                        mode.sync = false;
                        m_messenger.Put(new CDVDMsgPlayerSeek(mode));
                    }
                }
            }
        }
    }
}

void BasePlayer::SynchronizeDemuxer()
{
    if (IsCurrentThread())
        return;
    if (!m_messenger.IsInited())
        return;

    CDVDMsgGeneralSynchronize* message = new CDVDMsgGeneralSynchronize(500, SYNCSOURCE_PLAYER);
    m_messenger.Put(message->AddRef());
    message->Wait(GetTerminated(), 0);
    message->Release();
}

static void UpdateLimits(double& minimum, double& maximum, double dts)
{
    if (dts == DVD_NOPTS_VALUE)
        return;
    if (minimum == DVD_NOPTS_VALUE || minimum > dts)
        minimum = dts;
    if (maximum == DVD_NOPTS_VALUE || maximum < dts)
        maximum = dts;
}

bool BasePlayer::CheckContinuity(CCurrentStream& current, DemuxPacket* pPacket)
{
    if (m_playSpeed < DVD_PLAYSPEED_PAUSE)
        return false;

    if (pPacket->dts == DVD_NOPTS_VALUE || current.dts == DVD_NOPTS_VALUE)
        return false;

    double mindts = DVD_NOPTS_VALUE, maxdts = DVD_NOPTS_VALUE;
    UpdateLimits(mindts, maxdts, m_CurrentAudio.dts);
    UpdateLimits(mindts, maxdts, m_CurrentVideo.dts);
    UpdateLimits(mindts, maxdts, m_CurrentAudio.dts_end());
    UpdateLimits(mindts, maxdts, m_CurrentVideo.dts_end());

    /* if we don't have max and min, we can't do anything more */
    if (mindts == DVD_NOPTS_VALUE || maxdts == DVD_NOPTS_VALUE)
        return false;

    double correction = 0.0;
    if (pPacket->dts > maxdts + DVD_MSEC_TO_TIME(1000))
    {
        correction = pPacket->dts - maxdts;
    }

    /* if it's large scale jump, correct for it after having confirmed the jump */
    if (pPacket->dts + DVD_MSEC_TO_TIME(500) < current.dts_end())
    {
        correction = pPacket->dts - current.dts_end();
    }

    double lastdts = pPacket->dts;
    if (correction != 0.0)
    {
        // we want the dts values of two streams to close, or for one to be invalid (e.g. from a
        // missing audio stream)
        double this_dts = pPacket->dts;
        double that_dts =
            current.type == AVMEDIA_TYPE_AUDIO ? m_CurrentVideo.lastdts : m_CurrentAudio.lastdts;

        if (m_CurrentAudio.id == -1 || m_CurrentVideo.id == -1 ||
            current.lastdts == DVD_NOPTS_VALUE ||
            fabs(this_dts - that_dts) < DVD_MSEC_TO_TIME(1000))
        {
            m_offset_pts += correction;
            UpdateCorrection(pPacket, correction);
            lastdts = pPacket->dts;
        }
        else
        {
            // not sure yet - flags the packets as unknown until we get confirmation on another
            // audio/video packet
            pPacket->dts = DVD_NOPTS_VALUE;
            pPacket->pts = DVD_NOPTS_VALUE;
        }
    }
    else
    {
        if (current.avsync == CCurrentStream::AV_SYNC_CHECK)
            current.avsync = CCurrentStream::AV_SYNC_CONT;
    }
    current.lastdts = lastdts;
    return true;
}

bool BasePlayer::CheckPlayerInit(CCurrentStream& current)
{
    if (current.inited)
        return false;

    if (current.startpts != DVD_NOPTS_VALUE)
    {
        if (current.dts == DVD_NOPTS_VALUE)
        {
            return true;
        }

        if ((current.startpts - current.dts) > DVD_SEC_TO_TIME(20))
        {
            if (m_CurrentAudio.startpts != DVD_NOPTS_VALUE)
                m_CurrentAudio.startpts = current.dts;
            if (m_CurrentVideo.startpts != DVD_NOPTS_VALUE)
                m_CurrentVideo.startpts = current.dts;
        }

        if (current.dts < current.startpts)
        {
            return true;
        }
    }

    if (current.dts != DVD_NOPTS_VALUE)
    {
        current.inited = true;
        current.startpts = current.dts;
    }
    return false;
}

void BasePlayer::UpdateCorrection(DemuxPacket* pkt, double correction)
{
    if (pkt->dts != DVD_NOPTS_VALUE)
        pkt->dts -= correction;
    if (pkt->pts != DVD_NOPTS_VALUE)
        pkt->pts -= correction;
}

void BasePlayer::UpdateTimestamps(CCurrentStream& current, DemuxPacket* pPacket)
{
    double dts = current.dts;
    /* update stored values */
    if (pPacket->dts != DVD_NOPTS_VALUE)
        dts = pPacket->dts;
    else if (pPacket->pts != DVD_NOPTS_VALUE)
        dts = pPacket->pts;

    /* calculate some average duration */
    if (pPacket->duration != DVD_NOPTS_VALUE)
        current.dur = pPacket->duration;
    else if (dts != DVD_NOPTS_VALUE && current.dts != DVD_NOPTS_VALUE)
        current.dur = 0.1 * (current.dur * 9 + (dts - current.dts));

    current.dts = dts;

    current.dispTime = pPacket->dispTime;
}

IDVDStreamPlayer* BasePlayer::GetStreamPlayer(unsigned int target)
{
    if (target == VideoPlayer_AUDIO)
        return m_VideoPlayerAudio;
    if (target == VideoPlayer_VIDEO)
        return m_VideoPlayerVideo;
    return NULL;
}

void BasePlayer::SendPlayerMessage(CDVDMsg* pMsg, unsigned int target)
{
    IDVDStreamPlayer* player = GetStreamPlayer(target);
    if (player)
        player->SendMessage(pMsg, 0);
}

bool BasePlayer::ReadPacket(DemuxPacket*& packet, AVStream*& stream)
{
    // check if we should read from subtitle demuxer
    // read a data frame from stream.
    if (m_pDemuxer)
        packet = m_pDemuxer->Read();

    if (packet)
    {
        // stream changed, update and open defaults
        if (packet->iStreamId == DMX_SPECIALID_STREAMCHANGE)
        {
            m_SelectionStreams.Clear(AVMEDIA_TYPE_UNKNOWN);
            m_SelectionStreams.Update(m_pInputStream, m_pDemuxer);
            OpenDefaultStreams(false);

            // reevaluate HasVideo/Audio, we may have switched from/to a radio channel
            if (m_CurrentVideo.id < 0)
                m_HasVideo = false;
            if (m_CurrentAudio.id < 0)
                m_HasAudio = false;

            return true;
        }

        UpdateCorrection(packet, m_offset_pts);

        if (packet->iStreamId < 0)
            return true;

        if (m_pDemuxer)
        {
            stream = m_pDemuxer->GetStream(packet->iStreamId);
            if (!stream)
            {
                return false;
            }
        }
        return true;
    }
    return false;
}

void BasePlayer::OpenInputStream(tTJSBinaryStream* stream, const std::string& filename)
{
    SAFE_RELEASE(m_pInputStream);

    m_pInputStream = new InputStream(stream, filename);

    m_clock.Reset();
    m_errorCount = 0;
    m_ChannelEntryTimeOut.SetInfinite();
}

bool BasePlayer::OpenDemuxStream()
{
    CloseDemuxer();

    CDVDDemuxFFmpeg* demuxer = new CDVDDemuxFFmpeg;
    demuxer->Open(m_pInputStream);
    m_pDemuxer = demuxer;

    m_SelectionStreams.Clear(AVMEDIA_TYPE_UNKNOWN);
    m_SelectionStreams.Update(m_pInputStream, m_pDemuxer);

    m_offset_pts = 0;

    return true;
}

void BasePlayer::CloseDemuxer()
{
    delete m_pDemuxer;
    m_pDemuxer = nullptr;
    m_SelectionStreams.Clear(AVMEDIA_TYPE_UNKNOWN);
}

static int GetCodecPriority(const std::string& codec)
{
    /*
     * Technically flac, truehd, and dtshd_ma are equivalently good as they're all lossless.
     * However, ffmpeg can't decode dtshd_ma losslessy yet.
     */
    if (codec == "flac") // Lossless FLAC
        return 7;
    if (codec == "truehd") // Dolby TrueHD
        return 6;
    if (codec == "dtshd_ma") // DTS-HD Master Audio (previously known as DTS++)
        return 5;
    if (codec == "dtshd_hra") // DTS-HD High Resolution Audio
        return 4;
    if (codec == "eac3") // Dolby Digital Plus
        return 3;
    if (codec == "dca") // DTS
        return 2;
    if (codec == "ac3") // Dolby Digital
        return 1;
    return 0;
}

#define PREDICATE_RETURN(lh, rh) \
    do \
    { \
        if ((lh) != (rh)) \
            return (lh) > (rh); \
    } while (0)

static bool PredicateAudioPriority(const SelectionStream& lh, const SelectionStream& rh)
{
    PREDICATE_RETURN(lh.channels, rh.channels);

    PREDICATE_RETURN(GetCodecPriority(lh.codec), GetCodecPriority(rh.codec));

    PREDICATE_RETURN(lh.flags & 0x0001, rh.flags & 0x0001);

    return false;
}

static bool PredicateVideoPriority(const SelectionStream& lh, const SelectionStream& rh)
{
    PREDICATE_RETURN(lh.flags & 0x0001, rh.flags & 0x0001);
    return false;
}

void BasePlayer::OpenDefaultStreams(bool reset)
{
    bool valid;

    // open video stream
    valid = false;

    for (const auto& stream : m_SelectionStreams.Get(AVMEDIA_TYPE_VIDEO, PredicateVideoPriority))
    {
        if (OpenStream(m_CurrentVideo, stream.id, reset))
        {
            valid = true;
            break;
        }
    }
    if (!valid)
    {
        CloseStream(m_CurrentVideo, true);
    }

    // open audio stream
    valid = false;
    //	if (!m_PlayerOptions.video_only)
    {
        for (const auto& stream :
             m_SelectionStreams.Get(AVMEDIA_TYPE_AUDIO, PredicateAudioPriority))
        {
            if (OpenStream(m_CurrentAudio, stream.id, reset))
            {
                valid = true;
                break;
            }
        }
    }

    if (!valid)
    {
        CloseStream(m_CurrentAudio, true);
    }
}

void BasePlayer::UpdatePlayState(double timeout)
{
    if (m_State.timestamp != 0 &&
        m_State.timestamp + DVD_MSEC_TO_TIME(timeout) > m_clock.GetAbsoluteClock())
        return;

    SPlayerState state(m_State);

    state.dts = DVD_NOPTS_VALUE;
    if (m_CurrentVideo.dts != DVD_NOPTS_VALUE)
        state.dts = m_CurrentVideo.dts;
    else if (m_CurrentAudio.dts != DVD_NOPTS_VALUE)
        state.dts = m_CurrentAudio.dts;
    else if (m_CurrentVideo.startpts != DVD_NOPTS_VALUE)
        state.dts = m_CurrentVideo.startpts;
    else if (m_CurrentAudio.startpts != DVD_NOPTS_VALUE)
        state.dts = m_CurrentAudio.startpts;

    if (m_pDemuxer)
    {
        state.time = DVD_TIME_TO_MSEC(m_clock.GetClock());
        state.time_total = m_pDemuxer->GetStreamLength();
    }

    state.canpause = true;
    state.canseek = true;
    state.isInMenu = false;
    state.hasMenu = false;

    if (m_pInputStream)
    {
        state.time_offset = 0;
        state.canpause = true;
        state.canseek = true;
    }

    if (state.time_total <= 0)
        state.canseek = false;

    if (m_caching > CACHESTATE_DONE && m_caching < CACHESTATE_PLAY)
        state.caching = true;
    else
        state.caching = false;

    double level, delay, offset;
    if (GetCachingTimes(level, delay, offset))
    {
        state.cache_delay = std::max(0.0, delay);
        state.cache_level = std::max(0.0, std::min(1.0, level));
        state.cache_offset = offset;
    }
    else
    {
        state.cache_delay = 0.0;
        state.cache_level = std::min(1.0, GetQueueTime() / 8000.0);
        state.cache_offset = GetQueueTime() / state.time_total;
    }
    state.cache_bytes = 0;

    state.timestamp = m_clock.GetAbsoluteClock();

    std::unique_lock<std::recursive_mutex> lock(m_StateSection);
    m_State = state;
}

void BasePlayer::UpdateStreamInfos()
{
    if (!m_pDemuxer)
        return;

    std::lock_guard<std::recursive_mutex> lock(m_SelectionStreams.m_section);
    int streamId;
    std::string retVal;

    // video
    streamId = GetVideoStream();

    if (streamId >= 0 && streamId < GetVideoStreamCount())
    {
        SelectionStream& s = m_SelectionStreams.Get(AVMEDIA_TYPE_VIDEO, streamId);
        s.aspect_ratio = m_VideoPlayerVideo->GetAspectRatio();
        AVStream* stream = m_pDemuxer->GetStream(m_CurrentVideo.id);
        if (stream && stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            s.width = stream->codecpar->width;
            s.height = stream->codecpar->height;
            s.stereo_mode = m_VideoPlayerVideo->GetStereoMode();
            if (s.stereo_mode == "mono")
                s.stereo_mode = "";
        }
    }

    // audio
    streamId = GetAudioStream();

    if (streamId >= 0 && streamId < GetAudioStreamCount())
    {
        SelectionStream& s = m_SelectionStreams.Get(AVMEDIA_TYPE_AUDIO, streamId);
        AVStream* stream = m_pDemuxer->GetStream(m_CurrentAudio.id);
        if (stream && stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            s.codec = m_pDemuxer->GetStreamCodecName(stream->index);
        }
    }
}

NS_KRMOVIE_END
