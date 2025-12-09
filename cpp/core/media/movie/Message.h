#pragma once
#include "KRMovieDef.h"
#include <atomic>
#include <assert.h>

NS_KRMOVIE_BEGIN

class CDVDMsg
{
public:
    enum Message
    {
        NONE = 1000,

        // messages used in the whole system

        GENERAL_RESYNC, //
        GENERAL_FLUSH, // flush all buffers
        GENERAL_RESET, // reset codecs for new data
        GENERAL_PAUSE,
        GENERAL_STREAMCHANGE, //
        GENERAL_SYNCHRONIZE, //
        GENERAL_GUI_ACTION, // gui action of some sort
        GENERAL_EOF, // eof of stream

        // player core related messages (cVideoPlayer.cpp)

        PLAYER_SET_AUDIOSTREAM, //
        PLAYER_SET_VIDEOSTREAM, //
        PLAYER_SET_SUBTITLESTREAM, //
        PLAYER_SET_SUBTITLESTREAM_VISIBLE, //
        PLAYER_SET_STATE, // restore the VideoPlayer to a certain state
        PLAYER_SET_RECORD, // set record state
        PLAYER_SEEK, //
        PLAYER_SEEK_CHAPTER, //
        PLAYER_SETSPEED, // set the playback speed

        PLAYER_CHANNEL_NEXT, // switches to next playback channel
        PLAYER_CHANNEL_PREV, // switches to previous playback channel
        PLAYER_CHANNEL_PREVIEW_NEXT, // switches to next channel preview (does not switch the channel)
        PLAYER_CHANNEL_PREVIEW_PREV, // switches to previous channel preview (does not switch the channel)
        PLAYER_CHANNEL_SELECT_NUMBER, // switches to the channel with the provided channel number
        PLAYER_CHANNEL_SELECT, // switches to the provided channel
        PLAYER_STARTED, // sent whenever a sub player has finished it's first frame after open
        PLAYER_AVCHANGE, // signal a change in audio or video parameters

        // demuxer related messages

        DEMUXER_PACKET, // data packet
        DEMUXER_RESET, // reset the demuxer

        // video related messages

        VIDEO_SET_ASPECT, // set aspectratio of video
        VIDEO_DRAIN, // wait for decoder to output last frame

        // audio related messages

        AUDIO_SILENCE,

        // subtitle related messages
        SUBTITLE_CLUTCHANGE,
        SUBTITLE_ADDFILE
    };

    CDVDMsg(Message msg) : m_refs(1) { m_message = msg; }

    virtual ~CDVDMsg() {}

    CDVDMsg& operator=(const CDVDMsg&) = delete;

    virtual CDVDMsg* AddRef()
    {
        m_refs++;
        return (CDVDMsg*)this;
    }

    virtual long Release()
    {
        intptr_t count = --m_refs;
        assert(count >= 0);
        if (count == 0)
            delete (CDVDMsg*)this;
        return count;
    }

    inline bool IsType(Message msg) { return (m_message == msg); }

    inline Message GetMessageType() { return m_message; }

    long GetNrOfReferences() { return m_refs; }

protected:
    std::atomic_intptr_t m_refs;

private:
    Message m_message;
    
};

#define SYNCSOURCE_AUDIO 0x01
#define SYNCSOURCE_VIDEO 0x02
#define SYNCSOURCE_PLAYER 0x04
#define SYNCSOURCE_ANY 0x08

class CDVDMsgGeneralSynchronizePriv;
class CDVDMsgGeneralSynchronize : public CDVDMsg
{
public:
    CDVDMsgGeneralSynchronize(unsigned int timeout, unsigned int sources);
    ~CDVDMsgGeneralSynchronize();
    virtual long Release();

    // waits until all threads waiting, released the object
    // if abort is set somehow
    bool Wait(unsigned int ms, unsigned int source);
    void Wait(std::atomic<bool>& abort, unsigned int source);

private:
    class CDVDMsgGeneralSynchronizePriv* m_p;
};

template<typename T>
class CDVDMsgType : public CDVDMsg
{
public:
    CDVDMsgType(Message type, const T& value) : CDVDMsg(type), m_value(value) {}
    operator T() { return m_value; }
    T m_value;
};

typedef CDVDMsgType<bool> CDVDMsgBool;
typedef CDVDMsgType<int> CDVDMsgInt;
typedef CDVDMsgType<double> CDVDMsgDouble;

class CDVDMsgPlayerSetAudioStream : public CDVDMsg
{
public:
    CDVDMsgPlayerSetAudioStream(int streamId) : CDVDMsg(PLAYER_SET_AUDIOSTREAM)
    {
        m_streamId = streamId;
    }
    int GetStreamId() { return m_streamId; }

private:
    int m_streamId;
};

class CDVDMsgPlayerSetVideoStream : public CDVDMsg
{
public:
    CDVDMsgPlayerSetVideoStream(int streamId) : CDVDMsg(PLAYER_SET_VIDEOSTREAM)
    {
        m_streamId = streamId;
    }
    int GetStreamId() const { return m_streamId; }

private:
    int m_streamId;
};

class CDVDMsgPlayerSeek : public CDVDMsg
{
public:
    struct CMode
    {
        int time = 0;
        bool relative = false;
        bool backward = false;
        bool flush = true;
        bool accurate = true;
        bool sync = true;
        bool restore = true;
        bool trickplay = false;
    };

    CDVDMsgPlayerSeek(CDVDMsgPlayerSeek::CMode mode) : CDVDMsg(PLAYER_SEEK), m_mode(mode) {}
    int GetTime() { return m_mode.time; }
    bool GetRelative() { return m_mode.relative; }
    bool GetBackward() { return m_mode.backward; }
    bool GetFlush() { return m_mode.flush; }
    bool GetAccurate() { return m_mode.accurate; }
    bool GetRestore() { return m_mode.restore; }
    bool GetTrickPlay() { return m_mode.trickplay; }
    bool GetSync() { return m_mode.sync; }

private:
    CMode m_mode;
};

struct DemuxPacket;
class CDVDMsgDemuxerPacket : public CDVDMsg
{
public:
    CDVDMsgDemuxerPacket(DemuxPacket* packet, bool drop = false);
    virtual ~CDVDMsgDemuxerPacket();
    DemuxPacket* GetPacket() { return m_packet; }
    unsigned int GetPacketSize();
    bool GetPacketDrop() { return m_drop; }
    DemuxPacket* m_packet;
    bool m_drop;
};

NS_KRMOVIE_END
