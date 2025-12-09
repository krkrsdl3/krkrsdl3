#pragma once
#include "TVPThread.h"
#include "IVideoPlayer.h"
#include "CodecVideo.h"
#include <deque>
#include <list>
#include "MessageQueue.h"
#include "TVPTimer.h"

NS_KRMOVIE_BEGIN

class CDemuxStreamVideo;
class CRenderManager;
#define VIDEO_PICTURE_QUEUE_SIZE 1

#define EOS_ABORT 1
#define EOS_DROPPED 2
#define EOS_VERYLATE 4
#define EOS_BUFFER_LEVEL 8

#define NUM_BUFFERS 6

class CDroppingStats
{
public:
    void Reset();
    void AddOutputDropGain(double pts, int frames);
    struct CGain
    {
        int frames;
        double pts;
    };
    std::deque<CGain> m_gain;
    double m_totalGain;
    double m_lastPts;
};

class IRenderMsg
{
    friend class CRenderManager;

protected:
    virtual void VideoParamsChange() = 0;
    virtual void GetDebugInfo(std::string& audio, std::string& video, std::string& general) = 0;
    virtual void UpdateClockSync(bool enabled) = 0;

    virtual CBaseRenderer* CreateRenderer() = 0;
};

class CDVDClock;
class CVideoPlayerVideo : public tTVPThread, public IDVDStreamPlayerVideo, public IRenderMsg
{
public:
    CVideoPlayerVideo(CDVDClock* pClock, CDVDMessageQueue& parent, CBaseRenderer* renderer);
    virtual ~CVideoPlayerVideo();

    bool OpenStream(CDVDStreamInfo& hint);
    void CloseStream(bool bWaitForBuffers);

    void FrameMove();
    // IRenderMsg
    virtual void VideoParamsChange() override;
    virtual void GetDebugInfo(std::string& audio,
                              std::string& video,
                              std::string& general) override;
    virtual void UpdateClockSync(bool enabled) override;
    virtual CBaseRenderer* CreateRenderer() override { return m_pRenderer; }

    void Flush(bool sync);
    bool AcceptsData();
    bool HasData() const { return m_messageQueue.GetDataSize() > 0; }
    int GetLevel() { return m_messageQueue.GetLevel(); }
    bool IsInited() const { return m_messageQueue.IsInited(); }
    void SendMessage(CDVDMsg* pMsg, int priority = 0) { m_messageQueue.Put(pMsg, priority); }
    void FlushMessages() { m_messageQueue.Flush(); }

    void EnableSubtitle(bool bEnable) { m_bRenderSubs = bEnable; }
    bool IsSubtitleEnabled() { return m_bRenderSubs; }
    double GetSubtitleDelay() { return m_iSubtitleDelay; }
    void SetSubtitleDelay(double delay) { m_iSubtitleDelay = delay; }
    bool IsStalled() const override { return m_stalled; }
    bool IsRewindStalled() const override { return m_rewindStalled; }
    double GetCurrentPts();
    double GetOutputDelay(); /* returns the expected delay, from that a packet is put in queue */
    int GetDecoderFreeSpace() { return 0; }
    std::string GetPlayerInfo();
    int GetVideoBitrate();
    std::string GetStereoMode();
    void SetSpeed(int iSpeed);

    CDVDClock* m_pClock;

    virtual float GetAspectRatio();
    virtual bool Configure();

protected:
    virtual void OnExit();
    virtual void Execute();
    bool ProcessDecoderOutput(double& frametime, double& pts);

    int OutputPicture(const DVDVideoPicture* src, double pts);
    void OpenStream(CDVDStreamInfo& hint, CDVDVideoCodec* codec);

    void ResetFrameRateCalc();
    void CalcFrameRate();
    int CalcDropRequirement(double pts);

    double m_iSubtitleDelay;

    int m_iLateFrames;
    int m_iDroppedFrames;
    int m_iDroppedRequest;

    double m_fFrameRate = 25; // framerate of the video currently playing
    bool m_bAllowDrop;        // we can't drop frames until we've calculated the framerate

    bool m_bFpsInvalid; // needed to ignore fps (e.g. dvd stills)
    bool m_bRenderSubs;
    float m_fForcedAspectRatio;
    int m_speed;
    std::atomic_bool m_stalled;
    std::atomic_bool m_rewindStalled;
    bool m_paused;
    IDVDStreamPlayer::ESyncState m_syncState;
    std::atomic_bool m_bAbortOutput;

    CDVDMessageQueue m_messageQueue;
    CDVDMessageQueue& m_messageParent;
    CDVDStreamInfo m_hints;
    CDVDVideoCodec* m_pVideoCodec;
    DVDVideoPicture* m_pTempOverlayPicture;
    std::list<DVDMessageListItem> m_packets;
    CDroppingStats m_droppingStats;
    DVDVideoPicture m_picture;
    // --------------------------------- Render Data--------------------
    // Functions called from render thread
    void FrameWait(int ms);
    bool HasFrame();
    void Render(bool clear, uint32_t flags = 0, uint32_t alpha = 255, bool gui = true);
    bool IsGuiLayer();
    bool IsVideoLayer();
    void TriggerUpdateResolution(float fps, int width, int flags);
    void SetViewMode(int iViewMode);
    void PreInit();
    void UnInit();
    bool Flush();
    bool IsConfigured();
    void ToggleDebug();

    int GetSkippedFrames() { return m_QueueSkip; }

    bool Configure(DVDVideoPicture& picture,
                   float fps,
                   unsigned flags,
                   unsigned int orientation,
                   int buffers = 0);

    int AddVideoPicture(DVDVideoPicture& picture);

    void FlipPage(volatile std::atomic_bool& bStop,
                  double pts /*, EINTERLACEMETHOD deintMethod, EFIELDSYNC sync*/,
                  bool wait);

    int WaitForBuffer(volatile std::atomic_bool& bStop, int timeout = 100);

    bool GetStats(int& lateframes, double& pts, int& queued, int& discard);

    void DiscardBuffer();

    void SetDelay(int delay) { m_videoDelay = delay; };
    int GetDelay() { return m_videoDelay; };

    CBaseRenderer* GetRenderer() { return m_pRenderer; }

protected:
    void PrepareNextRender();
    void DeleteRenderer();

    void UpdateDisplayLatency();
    void CheckEnableClockSync();

    CBaseRenderer* m_pRenderer;
    std::recursive_mutex m_statelock;
    std::recursive_mutex m_presentlock;
    std::recursive_mutex m_datalock;
    bool m_bTriggerUpdateResolution;
    bool m_bRenderGUI;
    int m_waitForBufferCount;
    int m_rendermethod;
    bool m_renderDebug;
    TVPElapsedTimer m_debugTimer;

    enum EPRESENTSTEP
    {
        PRESENT_IDLE = 0,
        PRESENT_FLIP,
        PRESENT_FRAME,
        PRESENT_FRAME2,
        PRESENT_READY
    };

    enum EPRESENTMETHOD
    {
        PRESENT_METHOD_SINGLE = 0,
        PRESENT_METHOD_BLEND,
        PRESENT_METHOD_WEAVE,
        PRESENT_METHOD_BOB,
    };

    enum ERENDERSTATE
    {
        STATE_UNCONFIGURED = 0,
        STATE_CONFIGURING,
        STATE_CONFIGURED,
    };
    ERENDERSTATE m_renderState;
    std::recursive_mutex m_stateMutex;
    std::condition_variable_any m_stateEvent;

    double m_displayLatency;
    std::atomic_int m_videoDelay;

    int m_QueueSize;
    int m_QueueSkip;

    struct SPresent
    {
        double pts;
        EPRESENTMETHOD presentmethod;
    } m_Queue[NUM_BUFFERS];

    std::deque<int> m_free;
    std::deque<int> m_queued;
    std::deque<int> m_discard;

    unsigned int m_width, m_height, m_dwidth, m_dheight;
    unsigned int m_flags;
    float m_fps;
    unsigned int m_extended_format;
    unsigned int m_orientation;
    int m_NumberBuffers;

    int m_lateframes;
    double m_presentpts;
    EPRESENTSTEP m_presentstep;
    bool m_forceNext;
    int m_presentsource;
    std::condition_variable_any m_presentevent;
    tTVPThreadEvent m_flushEvent;

    struct CClockSync
    {
        void Reset();
        double m_error;
        int m_errCount;
        double m_syncOffset;
        bool m_enabled;
    } m_clockSync;
};

NS_KRMOVIE_END
