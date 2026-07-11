//---------------------------------------------------------------------------
// krnull.h — Null / stub video player implementations
//
// When the platform-specific video backend fails to open a stream,
// these stub players are returned instead of nullptr. They post
// EC_COMPLETE immediately on Play() so the engine can continue
// without hanging.
//---------------------------------------------------------------------------

#pragma once

#include "PlatformVideo.h"
#include "tjsNativeVideoOverlay.h"

// ─── NullOverlayPlayer ──────────────────────────────────────
class NullOverlayPlayer : public iTVPVideoOverlay
{
    tTJSNI_VideoOverlay* m_cb;
    uint32_t ref = 1;

public:
    NullOverlayPlayer(tTJSNI_VideoOverlay* cb) : m_cb(cb) {}

    void AddRef() override { ref++; }
    void Release() override
    {
        if (--ref == 0)
            delete this;
    }

    void SetVisible(bool) override {}
    void SetRect(int, int, int, int) override {}

    void Play() override
    {
        if (m_cb)
        {
            NativeEvent ev(WM_GRAPHNOTIFY);
            ev.WParam = EC_COMPLETE;
            ev.LParam = 0;
            m_cb->PostEvent(ev);
        }
    }
    void Stop() override {}
    void Pause() override {}
    void SetPosition(uint64_t) override {}
    void GetPosition(uint64_t* t) override { *t = 0; }
    void GetStatus(tTVPVideoStatus* s) override { *s = vsStopped; }
    void Rewind() override {}
    void SetFrame(int) override {}
    void GetFrame(int* f) override { *f = 0; }
    void GetFPS(double* f) override { *f = 0; }
    void GetNumberOfFrame(int* f) override { *f = 0; }
    void GetTotalTime(int64_t* t) override { *t = 0; }
    void GetVideoSize(long* w, long* h) override
    {
        *w = 0;
        *h = 0;
    }
    void SetWindow(class tTJSNI_Window*) override {}
    void SetMessageDrainWindow(void*) override {}

    tTVPBaseTexture* GetFrontBuffer() override { return nullptr; }
    void SetVideoBuffer(tTVPBaseTexture*, tTVPBaseTexture*, long) override {}

    void SetStopFrame(int) override {}
    void GetStopFrame(int* f) override { *f = 0; }
    void SetDefaultStopFrame() override {}

    void SetPlayRate(double) override {}
    void GetPlayRate(double* r) override { *r = 1.0; }
    void SetAudioBalance(long) override {}
    void GetAudioBalance(long* b) override { *b = 0; }
    void SetAudioVolume(long) override {}
    void GetAudioVolume(long* v) override { *v = 100; }

    void GetNumberOfAudioStream(unsigned long* c) override { *c = 0; }
    void SelectAudioStream(unsigned long) override {}
    void GetEnableAudioStreamNum(long* n) override { *n = -1; }
    void DisableAudioStream() override {}

    void GetNumberOfVideoStream(unsigned long* c) override { *c = 1; }
    void SelectVideoStream(unsigned long) override {}
    void GetEnableVideoStreamNum(long* n) override { *n = 1; }

    void SetLoopSegement(int, int) override {}

    void SetMixingBitmap(class tTVPBaseTexture*, float) override {}
    void ResetMixingBitmap() override {}
    void SetMixingMovieAlpha(float) override {}
    void GetMixingMovieAlpha(float* a) override { *a = 1.0f; }
    void SetMixingMovieBGColor(unsigned long) override {}
    void GetMixingMovieBGColor(unsigned long* c) override { *c = 0xFF000000; }
    void PresentVideoImage() override {}

    void GetContrastRangeMin(float*) override {}
    void GetContrastRangeMax(float*) override {}
    void GetContrastDefaultValue(float*) override {}
    void GetContrastStepSize(float*) override {}
    void GetContrast(float*) override {}
    void SetContrast(float) override {}
    void GetBrightnessRangeMin(float*) override {}
    void GetBrightnessRangeMax(float*) override {}
    void GetBrightnessDefaultValue(float*) override {}
    void GetBrightnessStepSize(float*) override {}
    void GetBrightness(float*) override {}
    void SetBrightness(float) override {}
    void GetHueRangeMin(float*) override {}
    void GetHueRangeMax(float*) override {}
    void GetHueDefaultValue(float*) override {}
    void GetHueStepSize(float*) override {}
    void GetHue(float*) override {}
    void SetHue(float) override {}
    void GetSaturationRangeMin(float*) override {}
    void GetSaturationRangeMax(float*) override {}
    void GetSaturationDefaultValue(float*) override {}
    void GetSaturationStepSize(float*) override {}
    void GetSaturation(float*) override {}
    void SetSaturation(float) override {}
};

// ─── NullLayerPlayer ────────────────────────────────────────
class NullLayerPlayer : public iTVPVideoOverlay, public tTVPContinuousEventCallbackIntf
{
    tTJSNI_VideoOverlay* m_cb;
    uint32_t ref = 1;

public:
    NullLayerPlayer(tTJSNI_VideoOverlay* cb) : m_cb(cb) {}
    ~NullLayerPlayer() { TVPRemoveContinuousEventHook(this); }

    void AddRef() override { ref++; }
    void Release() override
    {
        if (--ref == 0)
            delete this;
    }

    void SetVisible(bool) override {}
    void SetRect(int, int, int, int) override {}

    virtual void OnContinuousCallback(tjs_uint64 tick) override
    {
        if (m_cb)
        {
            NativeEvent ev(WM_GRAPHNOTIFY);
            ev.WParam = EC_UPDATE;
            int frame;
            GetFrame(&frame);
            ev.LParam = frame;
            m_cb->WndProc(ev);
        }
    }
    void Play() override
    {
        TVPAddContinuousEventHook(this);
    }
    void Stop() override {}
    void Pause() override {}
    void SetPosition(uint64_t) override {}
    void GetPosition(uint64_t* t) override { *t = 0; }
    void GetStatus(tTVPVideoStatus* s) override { *s = vsPlaying; }
    void Rewind() override {}
    void SetFrame(int) override {}
    void GetFrame(int* f) override { *f = 0; }
    void GetFPS(double* f) override { *f = 0; }
    void GetNumberOfFrame(int* f) override { *f = 0; }
    void GetTotalTime(int64_t* t) override { *t = 0; }
    void GetVideoSize(long* w, long* h) override
    {
        *w = 1;
        *h = 1;
    }
    void SetWindow(class tTJSNI_Window*) override {}
    void SetMessageDrainWindow(void*) override {}


    tTVPBaseTexture* GetFrontBuffer() override
    {
        if (m_cb)
        {
            NativeEvent ev(WM_GRAPHNOTIFY);
            ev.WParam = EC_COMPLETE;
            ev.LParam = 0;
            m_cb->PostEvent(ev);
        }
        return nullptr;
    }
    void SetVideoBuffer(tTVPBaseTexture*, tTVPBaseTexture*, long) override {}

    void SetStopFrame(int) override {}
    void GetStopFrame(int* f) override { *f = 0; }
    void SetDefaultStopFrame() override {}

    void SetPlayRate(double) override {}
    void GetPlayRate(double* r) override { *r = 1.0; }
    void SetAudioBalance(long) override {}
    void GetAudioBalance(long* b) override { *b = 0; }
    void SetAudioVolume(long) override {}
    void GetAudioVolume(long* v) override { *v = 100; }

    void GetNumberOfAudioStream(unsigned long* c) override { *c = 0; }
    void SelectAudioStream(unsigned long) override {}
    void GetEnableAudioStreamNum(long* n) override { *n = -1; }
    void DisableAudioStream() override {}

    void GetNumberOfVideoStream(unsigned long* c) override { *c = 1; }
    void SelectVideoStream(unsigned long) override {}
    void GetEnableVideoStreamNum(long* n) override { *n = 1; }

    void SetLoopSegement(int, int) override {}

    void SetMixingBitmap(class tTVPBaseTexture*, float) override {}
    void ResetMixingBitmap() override {}
    void SetMixingMovieAlpha(float) override {}
    void GetMixingMovieAlpha(float* a) override { *a = 1.0f; }
    void SetMixingMovieBGColor(unsigned long) override {}
    void GetMixingMovieBGColor(unsigned long* c) override { *c = 0xFF000000; }
    void PresentVideoImage() override {}

    void GetContrastRangeMin(float*) override {}
    void GetContrastRangeMax(float*) override {}
    void GetContrastDefaultValue(float*) override {}
    void GetContrastStepSize(float*) override {}
    void GetContrast(float*) override {}
    void SetContrast(float) override {}
    void GetBrightnessRangeMin(float*) override {}
    void GetBrightnessRangeMax(float*) override {}
    void GetBrightnessDefaultValue(float*) override {}
    void GetBrightnessStepSize(float*) override {}
    void GetBrightness(float*) override {}
    void SetBrightness(float) override {}
    void GetHueRangeMin(float*) override {}
    void GetHueRangeMax(float*) override {}
    void GetHueDefaultValue(float*) override {}
    void GetHueStepSize(float*) override {}
    void GetHue(float*) override {}
    void SetHue(float) override {}
    void GetSaturationRangeMin(float*) override {}
    void GetSaturationRangeMax(float*) override {}
    void GetSaturationDefaultValue(float*) override {}
    void GetSaturationStepSize(float*) override {}
    void GetSaturation(float*) override {}
    void SetSaturation(float) override {}
};
