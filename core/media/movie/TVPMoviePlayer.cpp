#include "tjsCommHead.h"

#include "TVPMoviePlayer.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include "Platform.h" // TVPGetTickCount
#include "TVPMovieBackend.h"
#include "TVPMovieDecoder.h"
#include "TVPMovieIO.h"
#include "TVPMoviePixels.h"

namespace krkrsdl3
{

namespace
{

// 帧环上限：够吸收一次解码抖动，又不至于把内存撑爆（1080p 一帧 8MB）。
constexpr int kMaxPendingPictures = 4;

// 音频缓冲段数：与内核其它音源保持一致的数量级。
constexpr int kAudioBufferCount = 8;

// 解码线程在无事可做时的轮询间隔（毫秒）。
constexpr unsigned kIdleSleepMs = 2;

// 环境变量 KRKR_MOVIE_DIAG=1 打开逐帧时序日志（排查卡顿用，默认零开销）。
// 诊断行前缀用的实例编号。
int TVPMovieNextInstanceId()
{
    static std::atomic<int> next{0};
    return ++next;
}

bool TVPMovieDiagEnabled()
{
    static const bool enabled = []() {
        const char* value = std::getenv("KRKR_MOVIE_DIAG");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    return enabled;
}

void TVPMovieDiag(const char* format, ...)
{
    if (!TVPMovieDiagEnabled())
        return;

    va_list args;
    va_start(args, format);
    std::vfprintf(stdout, format, args);
    va_end(args);
    std::fflush(stdout);
}

double ClampPosition(double value)
{
    return value < 0.0 ? 0.0 : value;
}

} // namespace

tTVPMoviePlayer::tTVPMoviePlayer()
  : tTVPThread("TVPMoviePlayer")
{
    instanceId_ = TVPMovieNextInstanceId();
    TVPRegisterMovieBackends();
    baseTick_ = TVPGetTickCount();
}

tTVPMoviePlayer::~tTVPMoviePlayer()
{
    Close();
}

//---------------------------------------------------------------------------
// 打开 / 关闭
//---------------------------------------------------------------------------

bool tTVPMoviePlayer::Open(TJS::tTJSBinaryStream* stream, const ttstr& name)
{
    // 一个播放器对象只服务一部影片：tTVPThread 的线程不能重启。
    if (demuxer_ != nullptr)
        return false;

    if (stream == nullptr)
        return false;

    // tjs_char 到 std::string 的转换沿用引擎既有的写法。
    io_ = new tTVPMovieIO(stream, std::string(name.c_str()));

    demuxer_ = tTVPMovieBackends::Get().CreateDemuxer(io_);
    if (demuxer_ == nullptr)
    {
        delete io_;
        io_ = nullptr;
        ended_ = true;
        return false;
    }

    streams_ = demuxer_->Streams();
    duration_ = demuxer_->Duration();
    TVPMovieDiag("[movie#%d] open demux=%s streams=%d duration=%.1fms file=%s\n", instanceId_,
                 demuxer_->Name(), int(streams_.size()), duration_, io_->Name().c_str());

    if (TVPMovieDiagEnabled())
    {
        for (const tTVPMovieStream& stream : streams_)
        {
            TVPMovieDiag("[movie#%d]   stream id=%d kind=%s codec=%s %dx%d %dHz %dch\n",
                         instanceId_, stream.id, TVPStreamKindName(stream.kind),
                         stream.codec.c_str(), stream.width, stream.height, stream.sampleRate,
                         stream.channels);
        }
    }

    if (!OpenStreams())
    {
        ReleaseResources();
        ended_ = true;
        return false;
    }

    ended_ = false;
    drained_ = false;
    playing_ = false;
    basePosition_ = 0.0;
    baseTick_ = TVPGetTickCount();

    // 解码线程只在真正有影片时启动；tTVPThread 的线程在构造时已挂起，
    // Resume() 之后才进入 Execute()。
    Resume();

    return true;
}

void tTVPMoviePlayer::Close()
{
    playing_ = false;

    if (io_)
        io_->Abort();
    if (demuxer_)
        demuxer_->Abort();

    // 线程只等一次：tTVPThread 的 join 不可重入。
    if (!threadStopped_)
    {
        threadStopped_ = true;
        StopThread();
    }

    {
        tTJSUniqueLock lock(pictureLock_);
        pending_.clear();
        front_.rgba.clear();
        front_.width = 0;
        front_.height = 0;
        frontValid_ = false;
        pictureCond_.notify_all();
    }

    ReleaseResources();
}

void tTVPMoviePlayer::ReleaseResources()
{
    CloseAudioOutput();

    delete videoDecoder_;
    videoDecoder_ = nullptr;
    delete audioDecoder_;
    audioDecoder_ = nullptr;
    delete demuxer_;
    demuxer_ = nullptr;
    delete io_;
    io_ = nullptr;

    streams_.clear();
    ended_ = true;
}

bool tTVPMoviePlayer::OpenStreams()
{
    videoStreamCount_ = 0;
    audioStreamCount_ = 0;
    videoStreamId_ = -1;
    audioStreamId_ = -1;

    for (size_t i = 0; i < streams_.size(); ++i)
    {
        const tTVPMovieStream& stream = streams_[i];
        if (stream.IsVideo())
            ++videoStreamCount_;
        else if (stream.IsAudio())
            ++audioStreamCount_;
    }

    const int videoIndex = PickStream(tTVPStreamKind::Video);
    if (videoIndex >= 0)
    {
        const tTVPMovieStream& stream = streams_[videoIndex];
        videoDecoder_ = tTVPMovieBackends::Get().CreateDecoder(stream);
        if (videoDecoder_)
        {
            videoStreamId_ = stream.id;
            codedWidth_ = stream.width;
            codedHeight_ = stream.height;

            const double streamFps = stream.Fps();
            if (!fpsForced_ && streamFps > 0.0)
                fps_ = streamFps;

            aspect_ = stream.aspect > 0.0f
                          ? stream.aspect
                          : (stream.height > 0 ? float(stream.width) / float(stream.height)
                                               : 0.0f);
        }
    }

    const int audioIndex = PickStream(tTVPStreamKind::Audio);
    if (audioIndex >= 0)
    {
        const tTVPMovieStream& stream = streams_[audioIndex];
        audioDecoder_ = tTVPMovieBackends::Get().CreateDecoder(stream);
        if (audioDecoder_)
        {
            audioStreamId_ = stream.id;
            audioRate_ = stream.sampleRate;
            audioChannels_ = stream.channels;
            if (!OpenAudioOutput())
            {
                // 声卡打不开就退化成静音播放，画面照常。
                delete audioDecoder_;
                audioDecoder_ = nullptr;
                audioStreamId_ = -1;
            }
        }
    }

    return videoDecoder_ != nullptr || audioDecoder_ != nullptr;
}

int tTVPMoviePlayer::PickStream(tTVPStreamKind kind) const
{
    int best = -1;
    int bestScore = -1;

    for (size_t i = 0; i < streams_.size(); ++i)
    {
        const tTVPMovieStream& stream = streams_[i];
        if (stream.kind != kind)
            continue;

        // 默认流优先；同等条件下取第一条。
        int score = (stream.disposition & tTVPMovieStream::DispositionDefault) ? 2 : 1;
        if (score > bestScore)
        {
            bestScore = score;
            best = static_cast<int>(i);
        }
    }
    return best;
}

//---------------------------------------------------------------------------
// 播放控制
//---------------------------------------------------------------------------

void tTVPMoviePlayer::Play()
{
    if (demuxer_ == nullptr)
        return;

    if (!playing_.exchange(true))
    {
        // 从暂停恢复：重设时钟锚点，避免暂停时长被计入播放位置。
        tTJSUniqueLock lock(clockLock_);
        baseTick_ = TVPGetTickCount();
    }

    if (sound_)
        sound_->Play();
}

void tTVPMoviePlayer::Pause()
{
    if (!playing_.exchange(false))
        return;

    {
        tTJSUniqueLock lock(clockLock_);
        basePosition_ = ClockNow();
        baseTick_ = TVPGetTickCount();
    }

    if (sound_)
    {
        const unsigned int queuedBefore = sound_->GetCurrentPlaySamples();
        sound_->Pause();
        FlushAudio();

        if (TVPMovieDiagEnabled())
        {
            printf("[movie#%d] pause queued=%u -> %u\n", instanceId_, queuedBefore,
                   sound_->GetCurrentPlaySamples());
            fflush(stdout);
        }
    }
}

void tTVPMoviePlayer::SetRate(double rate)
{
    if (rate <= 0.0)
        rate = 1.0;

    tTJSUniqueLock lock(clockLock_);
    basePosition_ = ClockNow();
    baseTick_ = TVPGetTickCount();
    rate_ = rate;

    if (demuxer_)
        demuxer_->SetRate(rate);

    // 变速时关掉声音：内核声卡按固定采样率播放，变速会变成变调。
    if (sound_)
    {
        if (rate == 1.0)
            sound_->Play();
        else
            sound_->Pause();
    }
}

void tTVPMoviePlayer::Seek(double positionMs)
{
    // 先置定位标记：解码线程从这一刻起不再投递音频/视频。
    pendingSeek_.store(ClampPosition(positionMs));

    // 前台帧必须同步作废：解码线程处理定位要花几毫秒，这段窗口里若还留着
    // 旧帧，画面就会闪一下上一次的内容（overlay 复用同一个 sprite 时尤其明显）。
    {
        tTJSUniqueLock lock(pictureLock_);
        frontValid_ = false;
        pending_.clear();
        pictureCond_.notify_all();
    }

    // 声音同理：声卡队列里排着的还是上一次/上一个位置的声音，必须现在就丢，
    // 不能等解码线程处理定位（那时 Play() 早把设备恢复、杂音已经出去了）。
    FlushAudio();

    // 等解码线程把这次定位真正做完（demuxer 归位、解码器 flush、声卡清空）。
    // 少了这一步，Rewind() 返回后立刻 Play()，解码线程可能还在跑旧位置的
    // 数据 —— 那就是「上一次的解码数据被重新投喂」。
    const unsigned before = seekDone_.load();
    for (int waited = 0; waited < 200; ++waited)
    {
        if (seekDone_.load() != before || GetTerminated() || demuxer_ == nullptr)
            break;
        TVPSleepFor(1);
    }
}

//---------------------------------------------------------------------------
// 时间轴
//---------------------------------------------------------------------------

double tTVPMoviePlayer::ClockNow() const
{
    if (!playing_.load(std::memory_order_relaxed))
        return basePosition_;

    const tjs_uint64 elapsed = TVPGetTickCount() - baseTick_;
    return basePosition_ + double(elapsed) * rate_;
}

double tTVPMoviePlayer::Position() const
{
    tTJSUniqueLock lock(clockLock_);
    return ClockNow();
}

void tTVPMoviePlayer::SetFps(double fps)
{
    if (fps <= 0.0)
        return;
    fps_ = fps;
    fpsForced_ = true;
}

int tTVPMoviePlayer::FrameIndex() const
{
    if (fps_ <= 0.0)
        return 0;
    return int(Position() * fps_ / TVPMovieMsPerSecond);
}

int tTVPMoviePlayer::FrameCount() const
{
    if (fps_ <= 0.0 || duration_ <= 0.0)
        return 0;
    return int(duration_ * fps_ / TVPMovieMsPerSecond);
}

void tTVPMoviePlayer::GetVideoSize(int* width, int* height) const
{
    if (width)
        *width = codedWidth_;
    if (height)
        *height = codedHeight_;
}

void tTVPMoviePlayer::SelectAudioStream(int index)
{
    // 只记录选择；重新打开音频解码器需要整段重开，代价远大于收益。
    if (index >= 0 && index < audioStreamCount_)
        audioStreamIndex_ = index;
}

//---------------------------------------------------------------------------
// 音频输出
//---------------------------------------------------------------------------

bool tTVPMoviePlayer::OpenAudioOutput()
{
    CloseAudioOutput();

    if (audioRate_ <= 0 || audioChannels_ <= 0)
        return false;
    TVPInitDirectSound();

    tTVPWaveFormat format;
    format.SamplesPerSec = audioRate_;
    format.Channels = audioChannels_;
    format.BitsPerSample = 16;
    format.BytesPerSample = 2;
    format.TotalSamples = 0;
    format.TotalTime = 0;
    format.SpeakerConfig = 0;
    format.IsFloat = false;
    format.Seekable = false;

    sound_ = TVPCreateSoundBuffer(format, kAudioBufferCount);
    TVPMovieDiag("[movie#%d] audio out %dHz %dch -> %s\n", instanceId_, audioRate_, audioChannels_,
                 sound_ ? "ok" : "FAILED");
    if (sound_ == nullptr)
        return false;

    sound_->SetVolume(volume_);
    sound_->SetPan(pan_);
    return true;
}

void tTVPMoviePlayer::CloseAudioOutput()
{
    if (sound_)
    {
        sound_->Stop();
        delete sound_;
        sound_ = nullptr;
    }
}

void tTVPMoviePlayer::SetVolume(float volume)
{
    volume_ = std::max(0.0f, std::min(1.0f, volume));
    if (sound_)
        sound_->SetVolume(volume_);
}

float tTVPMoviePlayer::GetVolume() const
{
    return sound_ ? sound_->GetVolume() : volume_;
}

void tTVPMoviePlayer::SetPan(float pan)
{
    pan_ = std::max(-1.0f, std::min(1.0f, pan));
    if (sound_)
        sound_->SetPan(pan_);
}

float tTVPMoviePlayer::GetPan() const
{
    return sound_ ? sound_->GetPan() : pan_;
}

//---------------------------------------------------------------------------
// 循环
//---------------------------------------------------------------------------

void tTVPMoviePlayer::SetLoopSegment(int beginFrame, int endFrame)
{
    loopBegin_ = beginFrame;
    loopEnd_ = endFrame;
}

//---------------------------------------------------------------------------
// 主线程拉帧
//---------------------------------------------------------------------------

bool tTVPMoviePlayer::Advance()
{
    const double now = Position();
    bool advanced = false;

    {
        tTJSUniqueLock lock(pictureLock_);

        if (pending_.empty())
            return false;

        // 只保留最新的一帧：落后的帧直接丢掉，避免慢机器上越拖越久。
        while (pending_.size() > 1 && pending_[1].pts <= now)
        {
            pending_.pop_front();
            ++dropped_;
        }

        if (pending_.front().pts <= now)
        {
            const double late = now - pending_.front().pts;
            front_.rgba.swap(pending_.front().rgba);
            front_.width = pending_.front().width;
            front_.height = pending_.front().height;
            front_.pts = pending_.front().pts;
            pending_.pop_front();
            frontValid_ = true;
            advanced = true;
            pictureCond_.notify_all();

            TVPMovieDiag("[movie#%d] show t=%.1f pts=%.1f late=%.1f ring=%d drop=%d\n", instanceId_,
                         now, front_.pts, late, int(pending_.size()), dropped_);
        }
    }

    if (advanced)
        newFrameFlag_.store(true, std::memory_order_relaxed);

    return advanced;
}

const uint8_t* tTVPMoviePlayer::FrontPixels(int* width, int* height) const
{
    tTJSUniqueLock lock(pictureLock_);
    if (!frontValid_ || front_.rgba.empty())
        return nullptr;

    if (width)
        *width = front_.width;
    if (height)
        *height = front_.height;
    return front_.rgba.data();
}

bool tTVPMoviePlayer::HasFrontFrame() const
{
    tTJSUniqueLock lock(pictureLock_);
    return frontValid_ && !front_.rgba.empty();
}

bool tTVPMoviePlayer::TakeFrameFlag()
{
    return newFrameFlag_.exchange(false, std::memory_order_relaxed);
}

bool tTVPMoviePlayer::Finished() const
{
    if (!ended_.load(std::memory_order_relaxed))
        return false;

    tTJSUniqueLock lock(pictureLock_);
    return pending_.empty();
}

//---------------------------------------------------------------------------
// 解码线程
//---------------------------------------------------------------------------

void tTVPMoviePlayer::Execute()
{
    if (GetTerminated() || demuxer_ == nullptr)
        return;

    DecodeLoop();
}

void tTVPMoviePlayer::DecodeLoop()
{
    drained_ = false;

    while (!GetTerminated())
    {
        // 声卡腾出空间就把积压的音频补上；不阻塞。
        DrainAudioBacklog();

        // 定位请求优先处理，并丢弃此前已经解出的帧。
        const double seekTo = pendingSeek_.exchange(TVPMovieNoTime);
        if (seekTo != TVPMovieNoTime)
        {
            PerformSeek(seekTo);
            continue;
        }

        if (!playing_.load(std::memory_order_relaxed))
        {
            Sleep(kIdleSleepMs);
            continue;
        }

        // 积压到顶：解码跑到声卡前面了（纯音频流没有帧环节流，这里就是
        // 唯一的刹车）。让出 CPU 等声卡消费，别一路解完再往外丢。
        if (audioBacklogBytes_ >= AudioBacklogBytesLimit())
        {
            Sleep(kIdleSleepMs);
            continue;
        }

        tTVPMoviePacket packet;
        if (!demuxer_->Read(packet))
        {
            if (demuxer_->Aborted() || GetTerminated())
                break;

            DrainDecoders();
            if (!HandleEnd())
            {
                while (!GetTerminated() && pendingSeek_.load() == TVPMovieNoTime)
                    Sleep(kIdleSleepMs);
                continue;
            }
            continue;
        }

        if (packet.Empty())
        {
            // 非阻塞读暂时没数据，稍后再试。
            Sleep(kIdleSleepMs);
            continue;
        }

        if (packet.stream == videoStreamId_ && videoDecoder_)
            FeedVideo(packet);
        else if (packet.stream == audioStreamId_ && audioDecoder_)
            FeedAudio(packet);
    }
}

void tTVPMoviePlayer::FeedVideo(const tTVPMoviePacket& packet)
{
    if (!videoDecoder_->Feed(packet))
    {
        // 解码器输入满：先把已有帧取空，再重试一次。
        tTVPMovieFrame frame;
        while (!GetTerminated() && videoDecoder_->Receive(frame))
            PushPicture(frame);

        if (!videoDecoder_->Feed(packet))
            return;
    }

    tTVPMovieFrame frame;
    while (!GetTerminated() && videoDecoder_->Receive(frame))
        PushPicture(frame);
}

void tTVPMoviePlayer::PushPicture(tTVPMovieFrame& frame)
{
    if (frame.kind != tTVPMovieFrame::tKind::Video)
        return;
    if (frame.width <= 0 || frame.height <= 0)
        return;

    // 等到帧环有空位或收到定位/退出请求。
    {
        tTJSUniqueLock lock(pictureLock_);
        while (!GetTerminated() && pending_.size() >= kMaxPendingPictures &&
               pendingSeek_.load() == TVPMovieNoTime && !ended_.load())
        {
            pictureCond_.WaitFor(pictureLock_, 10);
        }
    }

    if (GetTerminated() || pendingSeek_.load() != TVPMovieNoTime)
        return;

    tPicture picture;
    picture.width = frame.width;
    picture.height = frame.height;
    picture.pts = frame.pts;
    picture.rgba.resize(size_t(frame.width) * size_t(frame.height) * 4);

    if (!TVPConvertFrameToRGBA(frame, picture.rgba.data(), frame.width * 4))
        return;

    // 首帧决定实际显示尺寸与宽高比。
    if (codedWidth_ <= 0 || codedHeight_ <= 0)
    {
        codedWidth_ = frame.width;
        codedHeight_ = frame.height;
        if (frame.displayWidth > 0 && frame.displayHeight > 0 && frame.displayHeight != frame.height)
            aspect_ = double(frame.displayWidth) / double(frame.displayHeight);
    }

    {
        tTJSUniqueLock lock(pictureLock_);
        if (pending_.size() >= kMaxPendingPictures)
            return;
        pending_.push_back(std::move(picture));
        TVPMovieDiag("[movie#%d] push pts=%.1f ring=%d\n", instanceId_, frame.pts,
                     int(pending_.size()));
    }

    // 循环区间：越过结束帧就把下一轮定位排进队列，由解码线程处理。
    if (loopEnd_ > 0 && fps_ > 0.0)
    {
        const double endMs = loopEnd_ * TVPMovieMsPerSecond / fps_;
        if (frame.pts >= endMs)
            pendingSeek_.store(fps_ > 0.0 ? loopBegin_ * TVPMovieMsPerSecond / fps_ : 0.0);
    }
}

void tTVPMoviePlayer::FeedAudio(const tTVPMoviePacket& packet)
{
    if (rate_ != 1.0 || sound_ == nullptr)
        return; // 变速/无声卡时不消耗音频帧

    if (!audioDecoder_->Feed(packet))
    {
        tTVPMovieFrame frame;
        while (!GetTerminated() && audioDecoder_->Receive(frame))
            AppendAudio(frame);

        if (!audioDecoder_->Feed(packet))
            return;
    }

    tTVPMovieFrame frame;
    while (!GetTerminated() && audioDecoder_->Receive(frame))
        AppendAudio(frame);
}

void tTVPMoviePlayer::AppendAudio(tTVPMovieFrame& frame)
{
    if (frame.kind != tTVPMovieFrame::tKind::Audio || frame.sampleCount <= 0)
        return;
    if (sound_ == nullptr || frame.samples[0] == nullptr)
        return;

    // 正在定位：这段数据属于旧位置，直接丢，别让它挤进新的声卡队列。
    if (pendingSeek_.load() != TVPMovieNoTime)
        return;

    // 解码器契约：音频输出为 S16 交错、采样率与声道数等于流的声明值。
    const size_t bytes =
        size_t(frame.sampleCount) * size_t(frame.channels) * sizeof(int16_t);

    // 积压上限：约 1 秒的音频。超过说明声卡已经跟不上了，丢最旧的。
    const size_t limit = AudioBacklogBytesLimit();

    tTJSUniqueLock lock(audioLock_);

    audioBacklog_.emplace_back(static_cast<const uint8_t*>(frame.samples[0]),
                               static_cast<const uint8_t*>(frame.samples[0]) + bytes);
    audioBacklogBytes_ += bytes;

    while (audioBacklogBytes_ > limit && audioBacklog_.size() > 1)
    {
        audioBacklogBytes_ -= audioBacklog_.front().size();
        audioDroppedBytes_ += audioBacklog_.front().size();
        audioBacklog_.pop_front();
    }

    DrainAudioBacklogLocked();
}

size_t tTVPMoviePlayer::AudioBacklogBytesLimit() const
{
    return size_t(audioRate_) * size_t(audioChannels_) * sizeof(int16_t);
}

void tTVPMoviePlayer::FlushAudio()
{
    tTJSUniqueLock lock(audioLock_);

    if (!audioBacklog_.empty())
    {
        audioDroppedBytes_ += audioBacklogBytes_; // 统计口径：未播出的丢弃
        audioBacklog_.clear();
        audioBacklogBytes_ = 0;
    }

    if (sound_)
        sound_->Reset();
}

void tTVPMoviePlayer::DrainAudioBacklog()
{
    if (sound_ == nullptr)
        return;

    tTJSUniqueLock lock(audioLock_);
    DrainAudioBacklogLocked();
}

void tTVPMoviePlayer::DrainAudioBacklogLocked()
{
    if (sound_ == nullptr)
        return;

    while (!audioBacklog_.empty() && !GetTerminated() && sound_->IsBufferValid())
    {
        std::vector<uint8_t>& block = audioBacklog_.front();
        sound_->AppendBuffer(block.data(), static_cast<unsigned int>(block.size()));
        audioBacklogBytes_ -= block.size();
        audioBacklog_.pop_front();

        if (!sound_->IsPlaying() && playing_.load())
            sound_->Play();
    }
}

void tTVPMoviePlayer::DrainDecoders()
{
    if (drained_.exchange(true))
        return;

    if (videoDecoder_)
    {
        videoDecoder_->Drain();
        tTVPMovieFrame frame;
        while (!GetTerminated() && videoDecoder_->Receive(frame))
            PushPicture(frame);
    }
    if (audioDecoder_)
    {
        audioDecoder_->Drain();
        tTVPMovieFrame frame;
        while (!GetTerminated() && audioDecoder_->Receive(frame))
            AppendAudio(frame);
    }
}

bool tTVPMoviePlayer::HandleEnd()
{
    ended_ = true;
    TVPMovieDiag("[movie#%d] eof t=%.1f dropped=%d audioDrop=%lluKB backlog=%zu loop=%d\n",
                 instanceId_, Position(), dropped_,
                 (unsigned long long)(audioDroppedBytes_ / 1024), audioBacklog_.size(),
                 loopBegin_);

    if (loopBegin_ >= 0)
    {
        // 循环：回卷到起点，重新开始一轮解码。
        const double beginMs =
            fps_ > 0.0 ? loopBegin_ * TVPMovieMsPerSecond / fps_ : 0.0;
        PerformSeek(beginMs, false);
        ended_ = false;
        drained_ = false;
        return true;
    }

    // 等主线程把剩余帧消费完，再让解码线程挂起。
    while (!GetTerminated())
    {
        {
            tTJSUniqueLock lock(pictureLock_);
            if (pending_.empty())
                break;
            pictureCond_.WaitFor(pictureLock_, 10);
        }
        Sleep(kIdleSleepMs);
    }

    return false;
}

void tTVPMoviePlayer::PerformSeek(double positionMs, bool flushAudio)
{
    if (demuxer_ == nullptr)
        return;

    TVPMovieDiag("[movie#%d] seek -> %.1f (from %.1f, dropped=%d)\n", instanceId_, positionMs,
                 Position(), dropped_);
    demuxer_->Seek(positionMs);

    if (videoDecoder_)
        videoDecoder_->Flush();
    if (audioDecoder_)
        audioDecoder_->Flush();

    drained_ = false;
    ended_ = false;

    {
        tTJSUniqueLock lock(pictureLock_);
        pending_.clear();
        frontValid_ = false;
        pictureCond_.notify_all();
    }

    if (flushAudio)
        FlushAudio();

    tTJSUniqueLock lock(clockLock_);
    basePosition_ = ClampPosition(positionMs);
    baseTick_ = TVPGetTickCount();

    // 放最后：等在此之前的 seekDone_ 计数一旦变化，调用方就知道全部清完了。
    seekDone_.fetch_add(1);
}

} // namespace krkrsdl3
