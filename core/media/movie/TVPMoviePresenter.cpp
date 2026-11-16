#include "tjsCommHead.h"

#include "TVPMoviePresenter.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "LayerBitmap.h"
#include "NativeEventQueue.h"
#include "TVPStorage.h"
#include "PlatformView.h"
#include "TVPCompositor.h"

namespace krkrsdl3
{

namespace
{

// 音量/平衡在 iTVPVideoOverlay 里用 0..100000 的整数表示。
constexpr float kAudioScale = 100000.0f;

// 与播放器共用的诊断开关（KRKR_MOVIE_DIAG=1）。
bool TVPMovieDiagEnabled()
{
    static const bool enabled = []() {
        const char* value = std::getenv("KRKR_MOVIE_DIAG");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    return enabled;
}

} // namespace

tTVPMoviePresenter::tTVPMoviePresenter(tMode mode)
  : mode_(mode)
{
    player_ = new tTVPMoviePlayer();

    if (mode_ != tMode::Layer)
    {
        sprite_ = new TVPSprite;
        sprite_->isVisible = false;
        sprite_->type = 2;
        sprite_->xPos = 0;
        sprite_->yPos = 0;
    }

    TVPAddContinuousEventHook(this);
}

tTVPMoviePresenter::~tTVPMoviePresenter()
{
    TVPRemoveContinuousEventHook(this);

    delete player_;
    player_ = nullptr;

    DestroySprite();
}

void tTVPMoviePresenter::Release()
{
    if (--refCount_ == 0)
        delete this;
}

//---------------------------------------------------------------------------
// 打开
//---------------------------------------------------------------------------

void tTVPMoviePresenter::Open(iTVPVideoCallback* callback,
                              TJS::tTJSBinaryStream* stream,
                              const tjs_char* name,
                              const tjs_char* type,
                              uint64_t size)
{
    (void)type;
    (void)size;

    callback_ = callback;
    file_ = ttstr(name);
    hasFile_ = true;
    opened_ = player_->Open(stream, file_);
    completePosted_ = false;

    if (opened_ && (mode_ == tMode::Overlay || mode_ == tMode::Mixer))
        visible_ = true;
}

//---------------------------------------------------------------------------
// 逐帧驱动
//---------------------------------------------------------------------------

void tTVPMoviePresenter::OnContinuousCallback(tjs_uint64 tick)
{
    (void)tick;

    if (!opened_ && !player_->Finished())
        return;

    static int diagTicks = 0;
    const bool diag = TVPMovieDiagEnabled();
    if (diag && (++diagTicks % 30) == 0)
    {
        printf("[movie] tick n=%d pos=%.1f playing=%d finished=%d\n", diagTicks,
               player_->Position(), int(player_->IsPlaying()), int(player_->Finished()));
        fflush(stdout);
    }

    if (mode_ == tMode::Layer)
        TickLayer();
    else
        TickOverlay();

    if (!completePosted_ && player_->Finished())
    {
        completePosted_ = true;
        PostEvent(true);
    }
}

void tTVPMoviePresenter::TickOverlay()
{
    if (player_->Advance())
        UploadSprite();
    else
        ApplyVisibility(); // 定位后新帧还没到：这一段不要显示旧纹理
}

void tTVPMoviePresenter::ApplyVisibility()
{
    if (sprite_ == nullptr)
        return;

    const bool hasFrame = player_->HasFrontFrame();
    const bool show = visible_ && hasFrame;

    if (TVPMovieDiagEnabled() && show != sprite_->isVisible)
    {
        printf("[movie] overlay#%p vis=%d want=%d front=%d\n", (void*)this, int(show),
               int(visible_), int(hasFrame));
        fflush(stdout);
    }

    sprite_->isVisible = show;
}

void tTVPMoviePresenter::TickLayer()
{
    // 图层由调用方按需取帧，这里只负责通知「有新帧了」。
    if (player_->Advance())
        PostEvent(false);
}

void tTVPMoviePresenter::UploadSprite()
{
    if (sprite_ == nullptr)
        return;

    int width = 0;
    int height = 0;
    const uint8_t* pixels = player_->FrontPixels(&width, &height);
    if (pixels == nullptr || width <= 0 || height <= 0)
        return;

    if (sprite_->texture == nullptr || spriteWidth_ != width || spriteHeight_ != height)
    {
        if (sprite_->texture != nullptr)
        {
            sprite_->isVisible = false; // 旧纹理马上要被销毁，先别画
            TVPDepartTexture(sprite_);
            TVPDestroyTexture(sprite_);
        }
        sprite_->width = width;
        sprite_->height = height;
        TVPCreateTexture(*sprite_);
        TVPJoinTexture(sprite_);
        spriteWidth_ = width;
        spriteHeight_ = height;
    }

    TVPUpdateTexture(sprite_, const_cast<uint8_t*>(pixels), width, height, width * 4);
    ApplyVisibility();
}

void tTVPMoviePresenter::DestroySprite()
{
    if (sprite_ == nullptr)
        return;

    if (sprite_->texture != nullptr)
    {
        TVPDepartTexture(sprite_);
        TVPDestroyTexture(sprite_);
    }
    delete sprite_;
    sprite_ = nullptr;
}

void tTVPMoviePresenter::PostEvent(bool complete)
{
    if (callback_ == nullptr)
        return;

    NativeEvent event(WM_GRAPHNOTIFY);
    event.WParam = complete ? EC_COMPLETE : EC_UPDATE;
    event.LParam = complete ? 0 : static_cast<uintptr_t>(std::max(0, player_->FrameIndex()));

    if (complete)
        callback_->PostEvent(event);
    else
        callback_->WndProc(event);
}

//---------------------------------------------------------------------------
// 播放控制
//---------------------------------------------------------------------------

void tTVPMoviePresenter::SetWindow(class tTJSNI_Window* window)
{
    (void)window;
    // 逐帧驱动已在构造时挂到内核的连续事件钩子上。
}

void tTVPMoviePresenter::SetMessageDrainWindow(void* window)
{
    (void)window;
}

void tTVPMoviePresenter::SetRect(int left, int top, int right, int bottom)
{
    (void)left;
    (void)top;
    (void)right;
    (void)bottom;
}

void tTVPMoviePresenter::SetVisible(bool visible)
{
    visible_ = visible;
    ApplyVisibility();
}

void tTVPMoviePresenter::Play()
{
    player_->Play();
    visible_ = true;

    if (TVPMovieDiagEnabled())
    {
        printf("[movie] overlay#%p play want=1 front=%d -> show=%d\n", (void*)this,
               int(player_->HasFrontFrame()),
               int(visible_ && player_->HasFrontFrame()));
        fflush(stdout);
    }
    // 注意：这里不能直接把 sprite 点亮 —— 复用缓存的 overlay 时，
    // 纹理里还是上一次播放的最后一帧。
    ApplyVisibility();
}

void tTVPMoviePresenter::Pause()
{
    player_->Pause();
}

void tTVPMoviePresenter::Stop()
{
    player_->Pause();
    visible_ = false;
    ApplyVisibility();
}

void tTVPMoviePresenter::SetPosition(uint64_t tick)
{
    player_->Seek(static_cast<double>(tick));
    // 同步熄灯：定位后到新帧解出来之前，画面不该还停在上一次的位置。
    ApplyVisibility();
}

void tTVPMoviePresenter::GetPosition(uint64_t* tick)
{
    if (tick)
        *tick = static_cast<uint64_t>(player_->Position());
}

void tTVPMoviePresenter::GetStatus(tTVPVideoStatus* status)
{
    if (status == nullptr)
        return;

    if (!opened_)
        *status = vsStopped;
    else if (player_->Finished())
        *status = vsEnded;
    else if (player_->IsPlaying())
        *status = vsPlaying;
    else
        *status = vsPaused;
}

void tTVPMoviePresenter::Rewind()
{
    if (TVPMovieDiagEnabled())
        printf("[movie] overlay#%p rewind -> rebuild\n", (void*)this);

    RestartPlayback();
}

void tTVPMoviePresenter::RestartPlayback()
{
    if (!hasFile_)
    {
        player_->Seek(0.0);
        ApplyVisibility();
        return;
    }

    // 重开失败（文件被移走之类）就退回到普通定位，至少还能继续用。
    tTJSBinaryStream* stream = nullptr;
    try
    {
        stream = TVPCreateStream(file_);
    }
    catch (...)
    {
        stream = nullptr;
    }

    if (stream == nullptr)
    {
        player_->Seek(0.0);
        ApplyVisibility();
        return;
    }

    // 播放参数跨重建保留。
    const float volume = player_->GetVolume();
    const float pan = player_->GetPan();

    delete player_;
    player_ = new tTVPMoviePlayer();

    opened_ = player_->Open(stream, file_);
    player_->SetLoopSegment(loopBegin_, loopEnd_);
    player_->SetVolume(volume);
    player_->SetPan(pan);

    completePosted_ = false;
    layerFrameValid_ = false;
    bufferIndex_ = 0;

    // 新的播放器还没有任何帧：sprite 保持熄灯，等第一帧上传后由
    // ApplyVisibility() 点亮。
    ApplyVisibility();
}

void tTVPMoviePresenter::SetFrame(int frame)
{
    const double fps = player_->Fps();
    if (fps <= 0.0)
        return;
    player_->Seek(frame * TVPMovieMsPerSecond / fps);
}

void tTVPMoviePresenter::GetFrame(int* frame)
{
    if (frame)
        *frame = player_->FrameIndex();
}

void tTVPMoviePresenter::GetFPS(double* fps)
{
    if (fps)
        *fps = player_->Fps();
}

void tTVPMoviePresenter::GetNumberOfFrame(int* count)
{
    if (count)
        *count = player_->FrameCount();
}

void tTVPMoviePresenter::GetTotalTime(int64_t* total)
{
    if (total)
        *total = static_cast<int64_t>(player_->Duration());
}

void tTVPMoviePresenter::GetVideoSize(long* width, long* height)
{
    int w = 0;
    int h = 0;
    player_->GetVideoSize(&w, &h);
    if (width)
        *width = w;
    if (height)
        *height = h;
}

void tTVPMoviePresenter::SetPlayRate(double rate)
{
    player_->SetRate(rate);
}

void tTVPMoviePresenter::GetPlayRate(double* rate)
{
    if (rate)
        *rate = player_->GetRate();
}

//---------------------------------------------------------------------------
// 音频
//---------------------------------------------------------------------------

void tTVPMoviePresenter::SetAudioBalance(long balance)
{
    player_->SetPan(static_cast<float>(balance) / kAudioScale);
}

void tTVPMoviePresenter::GetAudioBalance(long* balance)
{
    if (balance)
        *balance = static_cast<long>(player_->GetPan() * kAudioScale);
}

void tTVPMoviePresenter::SetAudioVolume(long volume)
{
    player_->SetVolume(static_cast<float>(volume) / kAudioScale);
}

void tTVPMoviePresenter::GetAudioVolume(long* volume)
{
    if (volume)
        *volume = static_cast<long>(player_->GetVolume() * kAudioScale);
}

void tTVPMoviePresenter::GetNumberOfAudioStream(unsigned long* count)
{
    if (count)
        *count = static_cast<unsigned long>(player_->AudioStreamCount());
}

void tTVPMoviePresenter::SelectAudioStream(unsigned long index)
{
    player_->SelectAudioStream(static_cast<int>(index));
}

void tTVPMoviePresenter::GetEnableAudioStreamNum(long* index)
{
    if (index)
        *index = player_->AudioStream();
}

void tTVPMoviePresenter::DisableAudioStream(void)
{
    player_->SetVolume(0.0f);
}

//---------------------------------------------------------------------------
// 视频流
//---------------------------------------------------------------------------

void tTVPMoviePresenter::GetNumberOfVideoStream(unsigned long* count)
{
    if (count)
        *count = static_cast<unsigned long>(player_->VideoStreamCount());
}

void tTVPMoviePresenter::SelectVideoStream(unsigned long index)
{
    (void)index; // 多视频流切换需要重开解码器，当前不提供
}

void tTVPMoviePresenter::GetEnableVideoStreamNum(long* index)
{
    if (index)
        *index = 0;
}

//---------------------------------------------------------------------------
// 图层输出
//---------------------------------------------------------------------------

tTVPBaseTexture* tTVPMoviePresenter::GetFrontBuffer()
{
    if (buffers_[0] == nullptr || buffers_[1] == nullptr)
        return nullptr;

    int width = 0;
    int height = 0;
    const uint8_t* pixels = player_->FrontPixels(&width, &height);

    // 没有新帧时继续交出上一帧，避免图层被清空。
    if (pixels == nullptr || width <= 0 || height <= 0)
        return layerFrameValid_ ? buffers_[bufferIndex_] : nullptr;

    bufferIndex_ = 1 - bufferIndex_;
    tTVPBaseTexture* target = buffers_[bufferIndex_];
    target->Update(pixels, static_cast<unsigned int>(width) * 4, 0, 0, width, height);
    layerFrameValid_ = true;
    return target;
}

void tTVPMoviePresenter::SetVideoBuffer(tTVPBaseTexture* buff1, tTVPBaseTexture* buff2, long size)
{
    (void)size;
    buffers_[0] = buff1;
    buffers_[1] = buff2;
    bufferIndex_ = 0;
}

//---------------------------------------------------------------------------
// 停帧 / 混合 / 调色 —— 引擎接口要求实现，当前后端不提供这些能力
//---------------------------------------------------------------------------

void tTVPMoviePresenter::SetStopFrame(int frame)
{
    (void)frame;
}

void tTVPMoviePresenter::GetStopFrame(int* frame)
{
    if (frame)
        *frame = -1;
}

void tTVPMoviePresenter::SetDefaultStopFrame() {}

void tTVPMoviePresenter::SetMixingBitmap(class tTVPBaseTexture* dest, float alpha)
{
    (void)dest;
    (void)alpha;
}

void tTVPMoviePresenter::ResetMixingBitmap() {}

void tTVPMoviePresenter::SetMixingMovieAlpha(float alpha)
{
    (void)alpha;
}

void tTVPMoviePresenter::GetMixingMovieAlpha(float* alpha)
{
    if (alpha)
        *alpha = 1.0f;
}

void tTVPMoviePresenter::SetMixingMovieBGColor(unsigned long color)
{
    (void)color;
}

void tTVPMoviePresenter::GetMixingMovieBGColor(unsigned long* color)
{
    if (color)
        *color = 0xFF000000;
}

void tTVPMoviePresenter::PresentVideoImage() {}

void tTVPMoviePresenter::GetContrastRangeMin(float* value)
{
    if (value)
        *value = 0.0f;
}

void tTVPMoviePresenter::GetContrastRangeMax(float* value)
{
    if (value)
        *value = 0.0f;
}

void tTVPMoviePresenter::GetContrastDefaultValue(float* value)
{
    if (value)
        *value = 0.0f;
}

void tTVPMoviePresenter::GetContrastStepSize(float* value)
{
    if (value)
        *value = 0.0f;
}

void tTVPMoviePresenter::GetContrast(float* value)
{
    if (value)
        *value = 0.0f;
}

void tTVPMoviePresenter::SetContrast(float value)
{
    (void)value;
}

void tTVPMoviePresenter::GetBrightnessRangeMin(float* value)
{
    if (value)
        *value = 0.0f;
}

void tTVPMoviePresenter::GetBrightnessRangeMax(float* value)
{
    if (value)
        *value = 0.0f;
}

void tTVPMoviePresenter::GetBrightnessDefaultValue(float* value)
{
    if (value)
        *value = 0.0f;
}

void tTVPMoviePresenter::GetBrightnessStepSize(float* value)
{
    if (value)
        *value = 0.0f;
}

void tTVPMoviePresenter::GetBrightness(float* value)
{
    if (value)
        *value = 0.0f;
}

void tTVPMoviePresenter::SetBrightness(float value)
{
    (void)value;
}

void tTVPMoviePresenter::GetHueRangeMin(float* value)
{
    if (value)
        *value = 0.0f;
}

void tTVPMoviePresenter::GetHueRangeMax(float* value)
{
    if (value)
        *value = 0.0f;
}

void tTVPMoviePresenter::GetHueDefaultValue(float* value)
{
    if (value)
        *value = 0.0f;
}

void tTVPMoviePresenter::GetHueStepSize(float* value)
{
    if (value)
        *value = 0.0f;
}

void tTVPMoviePresenter::GetHue(float* value)
{
    if (value)
        *value = 0.0f;
}

void tTVPMoviePresenter::SetHue(float value)
{
    (void)value;
}

void tTVPMoviePresenter::GetSaturationRangeMin(float* value)
{
    if (value)
        *value = 0.0f;
}

void tTVPMoviePresenter::GetSaturationRangeMax(float* value)
{
    if (value)
        *value = 0.0f;
}

void tTVPMoviePresenter::GetSaturationDefaultValue(float* value)
{
    if (value)
        *value = 0.0f;
}

void tTVPMoviePresenter::GetSaturationStepSize(float* value)
{
    if (value)
        *value = 0.0f;
}

void tTVPMoviePresenter::GetSaturation(float* value)
{
    if (value)
        *value = 0.0f;
}

void tTVPMoviePresenter::SetSaturation(float value)
{
    (void)value;
}

void tTVPMoviePresenter::SetLoopSegement(int beginFrame, int endFrame)
{
    loopBegin_ = beginFrame;
    loopEnd_ = endFrame;
    player_->SetLoopSegment(beginFrame, endFrame);
}

} // namespace krkrsdl3
