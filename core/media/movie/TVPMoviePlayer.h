#pragma once
//---------------------------------------------------------------------------
// TVPMoviePlayer.h — 影片播放核心
//
// 线程模型（固定 2 个线程，不随影片并发数增加）：
//   1. 主线程：内核连续事件钩子每次回调时 Advance() 取一帧、上传纹理；
//   2. 解码线程（本类自身，继承 tTVPThread）：解复用 + 视频解码 + 音频解码。
// 另外声卡回调线程属于内核（iTVPSoundBuffer），解码器内部的并行度由
// TVPMovieThreadBudget() 决定（wasm 上为 1）。
//
// 「音频绝不阻塞解码线程」是这套模型成立的前提：声卡塞不下时音频进有界
// 积压队列，解码线程继续解视频，否则帧环会被抽空、随后成片补帧丢弃。
//---------------------------------------------------------------------------

#include <atomic>
#include <deque>
#include <vector>

#include "PlatformAudio.h"
#include "PlatformMutex.h"
#include "PlatformThread.h"
#include "TVPMovieDemuxer.h"
#include "TVPMovieStream.h"

namespace TJS
{
class tTJSBinaryStream;
}

namespace krkrsdl3
{

class tTVPMovieIO;
class iTVPMovieDecoder;

class tTVPMoviePlayer : public tTVPThread
{
public:
    tTVPMoviePlayer();
    ~tTVPMoviePlayer() override;

    tTVPMoviePlayer(const tTVPMoviePlayer&) = delete;
    tTVPMoviePlayer& operator=(const tTVPMoviePlayer&) = delete;

    // 打开影片。成功返回 true；失败时可继续用 Ended() 判断当「空影片」。
    // 接管 stream 的所有权，无论成败。
    bool Open(TJS::tTJSBinaryStream* stream, const ttstr& name);
    void Close();

    // -- 播放控制 ----------------------------------------------------------
    void Play();
    void Pause();
    bool IsPlaying() const { return playing_.load(std::memory_order_relaxed); }
    void SetRate(double rate);
    double GetRate() const { return rate_; }

    // -- 时间轴（毫秒）-----------------------------------------------------
    void Seek(double positionMs);
    double Position() const;
    double Duration() const { return duration_; }

    // -- 视频信息 ----------------------------------------------------------
    double Fps() const { return fps_; }
    void SetFps(double fps);
    int FrameIndex() const;
    int FrameCount() const;
    void GetVideoSize(int* width, int* height) const;
    double Aspect() const { return aspect_; }

    // -- 流选择 ------------------------------------------------------------
    int VideoStreamCount() const { return videoStreamCount_; }
    int AudioStreamCount() const { return audioStreamCount_; }
    int AudioStream() const { return audioStreamIndex_; }
    void SelectAudioStream(int index);

    // -- 音频输出 ----------------------------------------------------------
    iTVPSoundBuffer* SoundBuffer() const { return sound_; }
    void SetVolume(float volume);
    float GetVolume() const;
    void SetPan(float pan);
    float GetPan() const;

    // -- 循环（帧号；begin < 0 表示不循环）---------------------------------
    void SetLoopSegment(int beginFrame, int endFrame);

    // -- 主线程拉帧 --------------------------------------------------------
    // 有帧到点就把它挪到前台并返回 true。调用方据此更新纹理。
    bool Advance();
    // 前台帧像素（RGBA32）；无帧时返回 nullptr。
    const uint8_t* FrontPixels(int* width, int* height) const;
    // 前台是否有可用帧。定位/重开之后、新帧解出来之前为 false ——
    // 调用方据此决定要不要把画面藏起来（否则会闪现上一次的残留）。
    bool HasFrontFrame() const;
    // 期间有新帧产生（用于 layer 模式的 EC_UPDATE 通知）。
    bool TakeFrameFlag();

    // 播放到结尾且前台帧已消费完。
    bool Finished() const;

protected:
    void Execute() override;

private:
    struct tPicture
    {
        std::vector<uint8_t> rgba;
        int width = 0;
        int height = 0;
        double pts = 0.0;
    };

    // 解复用/流选择
    bool OpenStreams();
    int PickStream(tTVPStreamKind kind) const;
    // 释放后端对象（不动线程）。
    void ReleaseResources();

    // 音频：压缩包先入队，按声卡的空位节奏逐包解码，绝不阻塞视频。
    void QueueAudioPacket(const tTVPMoviePacket& packet);
    void PumpAudioDecoder();
    bool AudioQueueEmpty() const;
    bool FeedAudioData(const uint8_t* data, int size, double pts);
    void DrainAudioDecoder();

    // 解码线程内部
    void DecodeLoop();
    void FeedVideo(const tTVPMoviePacket& packet);
    void PushPicture(tTVPMovieFrame& frame);
    void DrainDecoders();
    // flushAudio=false 用于循环回卷：上一轮的尾巴还要接着放，不能丢。
    void PerformSeek(double positionMs, bool flushAudio = true);
    bool HandleEnd();

    // 音频输出
    bool OpenAudioOutput();
    void CloseAudioOutput();
    void AppendAudio(tTVPMovieFrame& frame);
    // 把解码器吐出来的小块拼成整块再送声卡（见 AppendAudio 注释）。
    void FlushAudioStage();
    size_t AudioChunkBytes() const;
    // 把积压的音频块尽量塞进声卡；不阻塞，塞不下就留着。
    void DrainAudioBacklog();
    // 调用方需已持有 audioLock_。
    void DrainAudioBacklogLocked();
    // 丢光声卡里排着的声音 + 未投递的积压（定位/重播用）。
    void FlushAudio();
    // 积压上限（约 1 秒），同时也是纯音频流的节流阈值。
    size_t AudioBacklogBytesLimit() const;

    // 时钟
    double ClockNow() const;

    // -- 后端对象 ----------------------------------------------------------
    tTVPMovieIO* io_ = nullptr;
    iTVPMovieDemuxer* demuxer_ = nullptr;
    iTVPMovieDecoder* videoDecoder_ = nullptr;
    iTVPMovieDecoder* audioDecoder_ = nullptr;

    std::vector<tTVPMovieStream> streams_;
    int videoStreamId_ = -1;
    // 有视频流但没有任何解码器接得住：按「不支持的格式」直接结束，
    // 不能挂着黑屏。
    bool unsupported_ = false;
    // 播放起点，用于「解码器不报错也永远不出帧」的超时判定。
    tjs_uint64 playStartTick_ = 0;
    // 播完后等音频尾巴的起始时刻（0 表示还没开始等）。
    tjs_uint64 endWaitStart_ = 0;
    std::atomic<bool> firstFrameSeen_{false};
    int audioStreamId_ = -1;
    int videoStreamCount_ = 0;
    int audioStreamCount_ = 0;
    int audioStreamIndex_ = 0;

    // -- 视频参数 ----------------------------------------------------------
    double duration_ = 0.0;
    double fps_ = 25.0;
    bool fpsForced_ = false;
    // 容器没给帧率时，用解出来的前几帧 pts 间隔估一个（ASF/WMV 常见）。
    bool fpsKnown_ = false;
    double fpsSampleSum_ = 0.0;
    int fpsSampleCount_ = 0;
    double fpsLastPts_ = TVPMovieNoTime;
    double aspect_ = 0.0;
    int codedWidth_ = 0;
    int codedHeight_ = 0;

    // -- 帧环 --------------------------------------------------------------
    mutable tTJSCriticalSection pictureLock_;
    tTVPCondition pictureCond_;
    std::deque<tPicture> pending_;
    tPicture front_;
    bool frontValid_ = false;
    std::atomic<bool> newFrameFlag_{false};
    int dropped_ = 0; // 统计用：被丢弃的过期帧数

    // -- 时钟 --------------------------------------------------------------
    mutable tTJSCriticalSection clockLock_;
    double basePosition_ = 0.0; // 锚点媒体时间（毫秒）
    tjs_uint64 baseTick_ = 0;   // 锚点对应的内核 tick
    double rate_ = 1.0;

    // -- 状态 --------------------------------------------------------------
    std::atomic<bool> playing_{false};
    std::atomic<bool> ended_{false};
    std::atomic<bool> drained_{false};
    std::atomic<double> pendingSeek_{TVPMovieNoTime};
    // 解码线程每真正应用完一次定位就 +1。Seek() 靠它把「清空解码状态」
    // 变成同步语义：返回时 demuxer / 解码器 / 声卡一定已经是干净的。
    std::atomic<unsigned> seekDone_{0};

    // -- 音频 --------------------------------------------------------------
    iTVPSoundBuffer* sound_ = nullptr;
    int audioRate_ = 0;
    int audioChannels_ = 0;
    float volume_ = 1.0f;
    float pan_ = 0.0f;

    // 声卡暂时塞不下时先攒在这里。解码线程绝不为此等待 —— 一等待就等于
    // 把视频解码一起停掉，帧环被抽空后会出现成片的补帧丢弃。
    // audioLock_ 同时保证「投递」与「清空」不会交错：否则定位时刚 Reset
    // 完，一个旧块又被塞回声卡，听起来就是一声杂音。
    mutable tTJSCriticalSection audioLock_;
    std::deque<std::vector<uint8_t>> audioBacklog_;
    // 拼接中的音频：解码器给的帧长短不一，先攒成固定大小再送声卡。
    std::vector<uint8_t> audioStage_;
    // 音频压缩包队列（解复用线程投递，解码按声卡空位逐包消费）
    struct tQueuedAudio
    {
        std::vector<uint8_t> data;
        double pts = TVPMovieNoTime;
    };
    mutable tTJSCriticalSection audioQueueLock_;
    std::deque<tQueuedAudio> audioPackets_;
    // 收尾时解码器只冲一次，避免反复 Drain。
    bool audioTailDrained_ = false;
    // 起播门槛：攒够几块才开设备，否则开头必欠载（详情见 DrainAudioBacklogLocked）。
    int audioQueuedBlocks_ = 0;
    bool audioStarted_ = false;
    size_t audioBacklogBytes_ = 0;
    uint64_t audioDroppedBytes_ = 0;

    // -- 循环 --------------------------------------------------------------
    int loopBegin_ = -1;
    int loopEnd_ = -1;

    // tTVPThread 的线程只能等待一次，重复 StopThread 会二次 join。
    bool threadStopped_ = false;
};

} // namespace krkrsdl3
