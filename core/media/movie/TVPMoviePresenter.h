#pragma once
//---------------------------------------------------------------------------
// TVPMoviePresenter.h — iTVPVideoOverlay 的唯一实现
//
// 三种挂载方式共用一套代码：
//   Overlay —— 自己持有 TVPSprite，把帧直接铺到合成器上；
//   Layer   —— 把帧写进调用方给的双缓冲纹理，由图层系统取走；
//   Mixer   —— 与 Overlay 同路（旧实现即为同一后端）。
//
// 逐帧驱动走内核的连续事件钩子，完成通知走 iTVPVideoCallback，
// 本模块不再自己造定时器或消息循环。
//---------------------------------------------------------------------------

#include <atomic>

#include "PlatformVideo.h"
#include "PlatformView.h"
#include "TVPEvent.h"
#include "TVPMoviePlayer.h"
#include "krmovie.h"

namespace krkrsdl3
{

class tTVPMoviePresenter : public iTVPVideoOverlay, public tTVPContinuousEventCallbackIntf
{
public:
    enum class tMode
    {
        Overlay,
        Layer,
        Mixer,
    };

    explicit tTVPMoviePresenter(tMode mode);
    ~tTVPMoviePresenter() override;

    // 打开影片。失败时对象仍然可用：首次 tick 会立刻回报 EC_COMPLETE，
    // 调用方不必处理空指针。
    void Open(iTVPVideoCallback* callback,
              TJS::tTJSBinaryStream* stream,
              const tjs_char* name,
              const tjs_char* type,
              uint64_t size);

    // -- iTVPVideoOverlay --------------------------------------------------
    void AddRef() override { ++refCount_; }
    void Release() override;

    void SetWindow(class tTJSNI_Window* window) override;
    void SetMessageDrainWindow(void* window) override;
    void SetRect(int left, int top, int right, int bottom) override;
    void SetVisible(bool visible) override;
    void Play() override;
    void Stop() override;
    void Pause() override;
    void SetPosition(uint64_t tick) override;
    void GetPosition(uint64_t* tick) override;
    void GetStatus(tTVPVideoStatus* status) override;

    void Rewind() override;
    void SetFrame(int frame) override;
    void GetFrame(int* frame) override;
    void GetFPS(double* fps) override;
    void GetNumberOfFrame(int* count) override;
    void GetTotalTime(int64_t* total) override;

    void GetVideoSize(long* width, long* height) override;
    tTVPBaseTexture* GetFrontBuffer() override;
    void SetVideoBuffer(tTVPBaseTexture* buff1, tTVPBaseTexture* buff2, long size) override;

    void SetStopFrame(int frame) override;
    void GetStopFrame(int* frame) override;
    void SetDefaultStopFrame() override;

    void SetPlayRate(double rate) override;
    void GetPlayRate(double* rate) override;

    void SetAudioBalance(long balance) override;
    void GetAudioBalance(long* balance) override;
    void SetAudioVolume(long volume) override;
    void GetAudioVolume(long* volume) override;

    void GetNumberOfAudioStream(unsigned long* count) override;
    void SelectAudioStream(unsigned long index) override;
    void GetEnableAudioStreamNum(long* index) override;
    void DisableAudioStream(void) override;

    void GetNumberOfVideoStream(unsigned long* count) override;
    void SelectVideoStream(unsigned long index) override;
    void GetEnableVideoStreamNum(long* index) override;

    void SetMixingBitmap(class tTVPBaseTexture* dest, float alpha) override;
    void ResetMixingBitmap() override;

    void SetMixingMovieAlpha(float alpha) override;
    void GetMixingMovieAlpha(float* alpha) override;
    void SetMixingMovieBGColor(unsigned long color) override;
    void GetMixingMovieBGColor(unsigned long* color) override;

    void PresentVideoImage() override;

    void GetContrastRangeMin(float* value) override;
    void GetContrastRangeMax(float* value) override;
    void GetContrastDefaultValue(float* value) override;
    void GetContrastStepSize(float* value) override;
    void GetContrast(float* value) override;
    void SetContrast(float value) override;

    void GetBrightnessRangeMin(float* value) override;
    void GetBrightnessRangeMax(float* value) override;
    void GetBrightnessDefaultValue(float* value) override;
    void GetBrightnessStepSize(float* value) override;
    void GetBrightness(float* value) override;
    void SetBrightness(float value) override;

    void GetHueRangeMin(float* value) override;
    void GetHueRangeMax(float* value) override;
    void GetHueDefaultValue(float* value) override;
    void GetHueStepSize(float* value) override;
    void GetHue(float* value) override;
    void SetHue(float value) override;

    void GetSaturationRangeMin(float* value) override;
    void GetSaturationRangeMax(float* value) override;
    void GetSaturationDefaultValue(float* value) override;
    void GetSaturationStepSize(float* value) override;
    void GetSaturation(float* value) override;
    void SetSaturation(float value) override;

    void SetLoopSegement(int beginFrame, int endFrame) override;

    // -- tTVPContinuousEventCallbackIntf -----------------------------------
    void OnContinuousCallback(tjs_uint64 tick) override;

private:
    // 丢掉整个播放器重建：解复用器、解码器、帧环、声卡缓冲全部重新来过。
    // 缓存复用的 overlay 重播时靠它保证「上一次的解码数据一点都不剩」。
    void RestartPlayback();

    void TickOverlay();
    void TickLayer();
    void UploadSprite();
    // sprite 的可见性 = 调用方要求可见 && 前台确实有帧。
    // 少了后半句，定位/重开时会先闪一下上一轮的残留纹理。
    void ApplyVisibility();
    void PostEvent(bool complete);
    void DestroySprite();

    tMode mode_;
    std::atomic<uint32_t> refCount_{1};

    tTVPMoviePlayer* player_ = nullptr;
    iTVPVideoCallback* callback_ = nullptr;

    // 重建播放器需要原始文件名（Open 时记下）。
    ttstr file_;
    bool hasFile_ = false;

    // 重建后要恢复的播放参数。
    int loopBegin_ = -1;
    int loopEnd_ = -1;

    bool visible_ = false;
    bool opened_ = false;
    bool completePosted_ = false;

    // Overlay 模式的合成器精灵
    TVPSprite* sprite_ = nullptr;
    int spriteWidth_ = 0;
    int spriteHeight_ = 0;

    // Layer 模式的双缓冲
    tTVPBaseTexture* buffers_[2] = {nullptr, nullptr};
    int bufferIndex_ = 0;
    bool layerFrameValid_ = false;
};

} // namespace krkrsdl3
