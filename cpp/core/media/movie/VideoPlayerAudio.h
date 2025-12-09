#pragma once
#include "TVPThread.h"
#include "IVideoPlayer.h"
#include "CodecAudio.h"
#include "MessageQueue.h"
#include "TVPTimer.h"
#include "VideoReferenceClock.h"
extern "C"
{
#include "libavutil/crc.h"
#include <libswresample/swresample.h>
#include "libavcodec/avcodec.h"
}
#include <stdint.h>
#include <atomic>
#include <mutex>
#include "WaveMixer.h"

NS_KRMOVIE_BEGIN

// AV sync options
enum AVSync
{
    SYNC_DISCON = 0,
    SYNC_RESAMPLE
};

class CVideoPlayerAudio : public tTVPThread, public IDVDStreamPlayerAudio
{
public:
    CVideoPlayerAudio(CDVDClock* pClock, CDVDMessageQueue& parent);
    virtual ~CVideoPlayerAudio();

    bool OpenStream(CDVDStreamInfo& hints);
    void CloseStream(bool bWaitForBuffers);

    void SetSpeed(int speed);
    void Flush(bool sync);

    // waits until all available data has been rendered
    bool AcceptsData();
    bool HasData() const { return m_messageQueue.GetDataSize() > 0; }
    int GetLevel() { return m_messageQueue.GetLevel(); }
    bool IsInited() const { return m_messageQueue.IsInited(); }
    void SendMessage(CDVDMsg* pMsg, int priority = 0) { m_messageQueue.Put(pMsg, priority); }
    void FlushMessages() { m_messageQueue.Flush(); }
    float GetDynamicRangeAmplification() const { return 0.0f; }

    std::string GetPlayerInfo();
    int GetAudioBitrate();
    int GetAudioChannels();

    // holds stream information for current playing stream
    CDVDStreamInfo m_streaminfo;

    double GetCurrentPts()
    {
        std::unique_lock<std::recursive_mutex> lock(m_info_section);
        return m_info.pts;
    }

    bool IsStalled() const { return m_stalled; }
    bool IsPassthrough() override;
    iTVPSoundBuffer* GetSoundBuffer() { return m_soundDevice; }

protected:
    virtual void OnExit();
    virtual void Execute();

    void UpdatePlayerInfo();
    void OpenStream(CDVDStreamInfo& hints, CDVDAudioCodec* codec);
    //! Switch codec if needed. Called when the sample rate gotten from the
    //! codec changes, in which case we may want to switch passthrough on/off.
    bool SwitchCodecIfNeeded();

    CDVDMessageQueue m_messageQueue;
    CDVDMessageQueue& m_messageParent;

    double m_audioClock;

    CDVDClock* m_pClock;           // dvd master clock
    CDVDAudioCodec* m_pAudioCodec; // audio codec

    int m_speed;
    bool m_stalled;
    bool m_silence;
    bool m_paused;
    IDVDStreamPlayer::ESyncState m_syncState;
    TVPElapsedTimer m_syncTimer;

    bool ProcessDecoderOutput(DVDAudioFrame& audioframe);

    // SYNC_DISCON, SYNC_SKIPDUP, SYNC_RESAMPLE
    int m_synctype;
    int m_setsynctype;
    int m_prevsynctype; // so we can print to the log

    void SetSyncType(bool passthrough);

    bool m_prevskipped;
    double m_maxspeedadjust;

    struct SInfo
    {
        SInfo() : pts(DVD_NOPTS_VALUE), passthrough(false) {}

        std::string info;
        double pts;
        bool passthrough;
    };

    std::recursive_mutex m_info_section;
    SInfo m_info;

    void Pause();
    bool Create(const DVDAudioFrame& audioframe, AVCodecID codec, bool needresampler);
    bool IsValidFormat(const DVDAudioFrame& audioframe);
    void Destroy();
    unsigned int AddPackets(const DVDAudioFrame& audioframe);
    double GetPlayingPts();
    double GetSyncError();
    void SetSyncErrorCorrection(double correction);
    void Flush();
    void AbortAddPackets();

    iTVPSoundBuffer* m_soundDevice = nullptr;
    struct SwrContext* swr_ctx = nullptr;
    AVSampleFormat swr_tgtFormat;
    unsigned int src_buffer_count = 0;
    unsigned int tgt_frameSize = 0;
    uint8_t* audio_buf = nullptr;
    unsigned int audio_buf_size = 0;
    double m_playingPts = DVD_NOPTS_VALUE;
    double m_timeOfPts = 0;
    double m_syncError = 0;
    unsigned int m_syncErrorTime = 0;
    std::atomic_bool m_bAbort;
    std::mutex _mutex;
    std::condition_variable _cond;
    TVPElapsedTimer _timer;
};

NS_KRMOVIE_END
