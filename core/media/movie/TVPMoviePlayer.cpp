#include "tjsCommHead.h"

#include "TVPMoviePlayer.h"

#include <algorithm>
#include <cmath>

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

// 送声卡的一块有多大（毫秒）。内核声卡按「块数」分配缓冲，解码器给的帧长短
// 不一，统一成固定块数才有稳定的余量（8 块 ≈ 400ms）。
constexpr int kAudioChunkMs = 50;

// 起播前先攒几块（约 150ms）再打开声卡。设备一开就会按实时速度抽数据，
// 解码线程还在啃首个关键帧的时候就只能放静音 —— 听感就是开头口吃。
constexpr int kAudioPrerollBlocks = 3;

// 待解码音频包的上限。只有声卡完全不动才会堆到这里，届时丢最旧的包，
// 不能反过来把解复用/视频解码卡住。
constexpr size_t kMaxQueuedAudioPackets = 256;

// 解码线程在无事可做时的轮询间隔（毫秒）。
constexpr unsigned kIdleSleepMs = 2;

// 有视频流却迟迟解不出第一帧时的容忍时长。解码器不报错、也不给画面
// （不支持的 profile / 缺参考帧之类），再等下去就是挂着黑屏。
constexpr tjs_uint64 kNoFrameTimeoutMs = 5000;

// 允许画面落后时钟多少毫秒。这个窗口内的迟到帧按顺序放（60Hz 下每帧本来
// 就占两拍，追得回来），超过就跳到最新帧重新对齐，免得一路拖下去。
constexpr double kMaxLagMs = 250.0;

// 播完之后最多再等多久让声卡把尾巴放完。等太久会把游戏卡在「影片结束了
// 但场景还没切」的状态，一声尾音不值这个价。
constexpr tjs_uint64 kEndAudioWaitMs = 1000;



double ClampPosition(double value)
{
    return value < 0.0 ? 0.0 : value;
}

} // namespace

tTVPMoviePlayer::tTVPMoviePlayer()
  : tTVPThread("TVPMoviePlayer")
{
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

    audioTailDrained_ = false;
    {
        tTJSUniqueLock lock(audioQueueLock_);
        audioPackets_.clear();
    }

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
    unsupported_ = false;
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
            {
                fps_ = streamFps;
                fpsKnown_ = true;
            }

            aspect_ = stream.aspect > 0.0f
                          ? stream.aspect
                          : (stream.height > 0 ? float(stream.width) / float(stream.height)
                                               : 0.0f);
        }
        else
        {
            // 有视频流却没人能解：整部影片按失败处理，交给上层立刻结束。
            unsupported_ = true;
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

    if (unsupported_)
        return false;

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
    if (!playing_.load())
        playStartTick_ = TVPGetTickCount();

    if (demuxer_ == nullptr)
        return;

    if (!playing_.exchange(true))
    {
        // 从暂停恢复：重设时钟锚点，避免暂停时长被计入播放位置。
        tTJSUniqueLock lock(clockLock_);
        baseTick_ = TVPGetTickCount();
    }

    // 不在这里直接开设备：先让解码线程攒几块，否则开头必定欠载。
    audioStarted_ = false;
    audioQueuedBlocks_ = 0;
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
        sound_->Pause();
        FlushAudio();
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
    // 有视频流但一直出不了帧：不能把画面停在黑屏上，直接结束。
    if (playing_.load(std::memory_order_relaxed) && videoStreamCount_ > 0 &&
        !firstFrameSeen_.load(std::memory_order_relaxed) && playStartTick_ != 0 &&
        TVPGetTickCount() - playStartTick_ > kNoFrameTimeoutMs)
    {

        ended_ = true;
        playing_ = false;
    }

    const double now = Position();
    bool advanced = false;

    {
        tTJSUniqueLock lock(pictureLock_);

        if (pending_.empty())
            return false;

        // 落后太多才跳帧：引擎偶尔渲染晚几十毫秒，那些帧到了下一拍就能补上，
        // 按顺序放反而更顺；只有累计落后超过窗口（真跟不上了）才跳到最新一帧。
        if (now - pending_.front().pts > kMaxLagMs)
        {
            while (pending_.size() > 1 && pending_[1].pts <= now)
            {
                pending_.pop_front();
                ++dropped_;
            }
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

        // 音频按声卡的空位节奏逐包解码；积压满了就只停音频，不停视频。
        PumpAudioDecoder();

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
            QueueAudioPacket(packet);
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
        const bool firstPicture = !firstFrameSeen_.load();
        pending_.push_back(std::move(picture));
        firstFrameSeen_ = true;

        // 首帧到位才让时钟起跑：Play() 到首帧解出来之间通常要一两百毫秒，
        // 这段算进去的话开头每帧都是「迟到」，画面会先顿一下再追。
        if (firstPicture)
        {
            tTJSUniqueLock clockLock(clockLock_);
            if (basePosition_ <= 0.0)
            {
                basePosition_ = 0.0;
                baseTick_ = TVPGetTickCount();
            }
        }

        // 容器没声明帧率时，用相邻两帧的 pts 间隔估一个；估出来之前
        // fps_ 只是个占位值，帧号/循环区间都会算偏。
        if (!fpsKnown_ && !fpsForced_)
        {
            if (fpsLastPts_ != TVPMovieNoTime)
            {
                const double delta = picture.pts - fpsLastPts_;
                if (delta > 0.5 && delta < 200.0)
                {
                    fpsSampleSum_ += delta;
                    if (++fpsSampleCount_ >= 15)
                    {
                        fps_ = TVPMovieMsPerSecond * fpsSampleCount_ / fpsSampleSum_;
                        fpsKnown_ = true;

                    }
                }
            }
            fpsLastPts_ = picture.pts;
        }

    }

    // 循环区间：越过结束帧就把下一轮定位排进队列，由解码线程处理。
    if (loopEnd_ > 0 && fps_ > 0.0)
    {
        const double endMs = loopEnd_ * TVPMovieMsPerSecond / fps_;
        if (frame.pts >= endMs)
            pendingSeek_.store(fps_ > 0.0 ? loopBegin_ * TVPMovieMsPerSecond / fps_ : 0.0);
    }
}

bool tTVPMoviePlayer::FeedAudioData(const uint8_t* data, int size, double pts)
{
    if (rate_ != 1.0 || sound_ == nullptr || audioDecoder_ == nullptr)
        return false; // 变速/无声卡时不消耗音频帧

    tTVPMoviePacket packet;
    packet.stream = audioStreamId_;
    packet.data = data;
    packet.size = size;
    packet.pts = pts;

    if (!audioDecoder_->Feed(packet))
    {
        tTVPMovieFrame frame;
        while (!GetTerminated() && audioDecoder_->Receive(frame))
            AppendAudio(frame);

        if (!audioDecoder_->Feed(packet))
            return false;
    }

    tTVPMovieFrame frame;
    while (!GetTerminated() && audioDecoder_->Receive(frame))
        AppendAudio(frame);
    return true;
}

//---------------------------------------------------------------------------
// 音频：压缩包入队 → 按声卡空位逐包解码
//---------------------------------------------------------------------------

void tTVPMoviePlayer::QueueAudioPacket(const tTVPMoviePacket& packet)
{
    if (packet.data == nullptr || packet.size <= 0)
        return;

    tQueuedAudio item;
    item.data.assign(packet.data, packet.data + packet.size);
    item.pts = packet.pts;

    tTJSUniqueLock lock(audioQueueLock_);
    // 队列只会在声卡完全不动时堆起来；真堆起来了丢最旧的压缩包，
    // 绝不能反过来把解码线程（视频）卡住。
    if (audioPackets_.size() >= kMaxQueuedAudioPackets)
        audioPackets_.pop_front();
    audioPackets_.push_back(std::move(item));
}

void tTVPMoviePlayer::PumpAudioDecoder()
{
    // 声卡积压已经到顶就先不解，包留在队列里；视频继续跑。
    const size_t limit = AudioBacklogBytesLimit();
    if (sound_ != nullptr && limit > 0 && audioBacklogBytes_ >= limit)
        return;

    tQueuedAudio item;
    {
        tTJSUniqueLock lock(audioQueueLock_);
        if (audioPackets_.empty())
            return;
        item = std::move(audioPackets_.front());
        audioPackets_.pop_front();
    }

    FeedAudioData(item.data.data(), int(item.data.size()), item.pts);
}

bool tTVPMoviePlayer::AudioQueueEmpty() const
{
    tTJSUniqueLock lock(audioQueueLock_);
    return audioPackets_.empty();
}

void tTVPMoviePlayer::DrainAudioDecoder()
{
    if (audioDecoder_ == nullptr)
        return;

    audioDecoder_->Drain();
    tTVPMovieFrame frame;
    while (!GetTerminated() && audioDecoder_->Receive(frame))
        AppendAudio(frame);

    // 把拼接缓冲里剩下的尾巴也交出去。
    tTJSUniqueLock lock(audioLock_);
    FlushAudioStage();
    DrainAudioBacklogLocked();
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

    // 目标是约 1 秒的积压，由解码线程的节流维持；这里只在声卡彻底不消费
    // （设备丢了之类）时兜底，把积压压到 4 秒以内。
    // 注意不能按目标值丢：解码线程本来就贴着上限跑，按上限丢等于每帧都扔掉
    // 一段还没播出去的音频，听感就是持续的口吃。
    const size_t hardLimit = AudioBacklogBytesLimit() * 4;

    tTJSUniqueLock lock(audioLock_);

    // 拼成 50ms 一块再入队：解码器给的帧可能只有 5.8ms，直接往下塞的话
    // 声卡那 8 个缓冲合起来也就几十毫秒，开头很容易欠载成口吃。
    const uint8_t* src = static_cast<const uint8_t*>(frame.samples[0]);
    const size_t chunk = AudioChunkBytes();
    audioStage_.insert(audioStage_.end(), src, src + bytes);

    while (audioStage_.size() >= chunk)
    {
        audioBacklog_.emplace_back(audioStage_.begin(), audioStage_.begin() + chunk);
        audioStage_.erase(audioStage_.begin(), audioStage_.begin() + chunk);
        audioBacklogBytes_ += chunk;
    }

    while (audioBacklogBytes_ > hardLimit && audioBacklog_.size() > 1)
    {
        audioBacklogBytes_ -= audioBacklog_.front().size();
        audioDroppedBytes_ += audioBacklog_.front().size();
        audioBacklog_.pop_front();
    }

    DrainAudioBacklogLocked();

}

size_t tTVPMoviePlayer::AudioChunkBytes() const
{
    const size_t bytesPerSecond =
        size_t(std::max(audioRate_, 1)) * size_t(std::max(audioChannels_, 1)) * sizeof(int16_t);
    return std::max<size_t>(bytesPerSecond * kAudioChunkMs / TVPMovieMsPerSecond,
                            size_t(1024));
}

void tTVPMoviePlayer::FlushAudioStage()
{
    if (audioStage_.empty())
        return;

    audioBacklog_.emplace_back(audioStage_.begin(), audioStage_.end());
    audioBacklogBytes_ += audioStage_.size();
    audioStage_.clear();
}

size_t tTVPMoviePlayer::AudioBacklogBytesLimit() const
{
    return size_t(audioRate_) * size_t(audioChannels_) * sizeof(int16_t);
}

void tTVPMoviePlayer::FlushAudio()
{
    tTJSUniqueLock lock(audioLock_);

    audioStage_.clear();
    audioQueuedBlocks_ = 0;
    audioStarted_ = false;

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
        ++audioQueuedBlocks_;

        audioBacklogBytes_ -= block.size();
        audioBacklog_.pop_front();
    }

    // 攒够门限再开设备；影片特别短、攒不满时由 HandleEnd 兜底开。
    if (!audioStarted_ && playing_.load(std::memory_order_relaxed) &&
        audioQueuedBlocks_ >= kAudioPrerollBlocks &&
        (firstFrameSeen_.load(std::memory_order_relaxed) || drained_))
    {
        sound_->Play();
        audioStarted_ = true;
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
    // 音频的收尾由 DrainAudioQueue/DrainAudioDecoder 在 EOF 时统一处理。
}

bool tTVPMoviePlayer::HandleEnd()
{
    // 循环：回卷到起点，重新开始一轮解码。
    if (loopBegin_ >= 0)
    {


        const double beginMs = fps_ > 0.0 ? loopBegin_ * TVPMovieMsPerSecond / fps_ : 0.0;
        PerformSeek(beginMs, false);
        ended_ = false;
        drained_ = false;
        return true;
    }

    // 收尾：边等声卡放，边把还没解码的音频包喂完。声卡队列里通常还排着
    // 几百毫秒，立刻结束会把结尾切掉一截（ED/OP 的最后一小节）。
    if (sound_ != nullptr)
    {
        const bool queueEmpty = AudioQueueEmpty();
        if (!queueEmpty)
        {
            PumpAudioDecoder();
        }
        else if (!audioTailDrained_)
        {
            audioTailDrained_ = true;
            DrainAudioDecoder(); // 队列空了，把解码器内部的延迟帧冲出来
        }

        {
            tTJSUniqueLock lock(audioLock_);
            FlushAudioStage();
            DrainAudioBacklogLocked();

            // 很短的片子可能攒不满起播门槛，收尾时兜底把设备开起来。
            if (!audioStarted_ && playing_.load(std::memory_order_relaxed))
            {
                sound_->Play();
                audioStarted_ = true;
            }
        }

        // 设备没在放（被别的流抢走、或本来就暂停）就别等了，队列不会动。
        const bool audioPending =
            sound_->IsPlaying() &&
            (!queueEmpty || !audioBacklog_.empty() || sound_->GetCurrentPlaySamples() > 0);
        if (audioPending)
        {
            if (endWaitStart_ == 0)
                endWaitStart_ = TVPGetTickCount();
            if (TVPGetTickCount() - endWaitStart_ < kEndAudioWaitMs)
            {
                Sleep(kIdleSleepMs);
                return true;
            }
        }
    }

    if (!ended_.load())
    {
        ended_ = true;

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
    endWaitStart_ = 0;
    audioTailDrained_ = false;
    {
        tTJSUniqueLock lock(audioQueueLock_);
        audioPackets_.clear();
    }
    if (demuxer_ == nullptr)
        return;


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
