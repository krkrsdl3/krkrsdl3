// WASM video player 鈥?browser <video> element based
// Overlay mode: <video> element positioned over canvas (self-displayed)
// Layer mode:  captures frames via canvas drawImage 鈫?GetFrontBuffer

#include "tjsCommHead.h"
#include "PlatformVideo.h"
#include "Platform.h"
#include "TVPSystem.h"
#include "TVPStorage.h"
#include "TVPDebug.h"
#include "LayerBitmap.h"
#include "tjsNativeVideoOverlay.h"
#include "TVPEvent.h"

#include <emscripten.h>
#include <emscripten/html5.h>
#include <unistd.h>
#include <thread>
#include <vector>
#include <atomic>
#include <cstring>
#include <string>

// ============================================================
// JS helpers
// ============================================================
EM_ASYNC_JS(int, wasmCreateVideo, (const char* mime, int dataAddr, int dataLen), {
    try {
        var src = HEAPU8.slice(dataAddr, dataAddr + dataLen);
        var copy = new Uint8Array(src.length); copy.set(src);
        var blob = new Blob([copy], {type:UTF8ToString(mime)});
        var url = URL.createObjectURL(blob);
        var el = document.getElementById('wasm-video');
        el.src = url;
        el.style.pointerEvents = 'none';
        el.addEventListener('play', function once(){ el.muted = false; }, {once:true});
        el.addEventListener('canplay', function(){ el.muted = false; }, {once:true});
        el.load();
        return 0;
    } catch(e) { console.error('[video] create:', e.message); return -1; }
});

EM_JS(void, wasmVideoDump, (int vid), {
    try {
        var el = document.getElementById('wasm-video');
        if (!el) { console.log('[video] dump: null'); return; }
        var s = el.style;
        console.log('[video] dump:',
            'inDOM=' + (el.parentNode ? 1 : 0),
            'display=' + s.display,
            'zIndex=' + s.zIndex,
            'pos=' + s.position,
            'rect=' + s.left + ',' + s.top + ' ' + s.width + 'x' + s.height,
            'readyState=' + el.readyState,
            'networkState=' + el.networkState,
            'paused=' + el.paused,
            'ended=' + el.ended,
            'error=' + (el.error ? el.error.code + ':' + el.error.message : 'null'),
            'videoWidth=' + el.videoWidth,
            'videoHeight=' + el.videoHeight,
            'duration=' + el.duration,
            'currentTime=' + el.currentTime,
            'src=' + (el.src ? el.src.substring(0, 50) : 'null')
        );
    } catch(e) { console.log('[video] dump error:', e); }
});

EM_JS(int, wasmVideoEvents, (int vid), {
    try { var el = document.getElementById('wasm-video'); return el ? el.__events || 0 : -1; } catch(e) { return -1; }
});
EM_JS(void, wasmVideoPlay, (int id), { try { var e = document.getElementById('wasm-video'); if (e) { if (e.readyState === 0) e.load(); e.play().catch(function(){}); } } catch(e) {} });
EM_JS(void, wasmVideoPause, (int id), { try { var e = document.getElementById('wasm-video'); if (e) e.pause(); } catch(e) {} });
EM_JS(void, wasmVideoStop, (int id), { try { var e = document.getElementById('wasm-video'); if (e) { e.pause(); e.currentTime = 0; } } catch(e) {} });
EM_JS(double, wasmVideoDur, (int id), { try { return document.getElementById('wasm-video') ? document.getElementById('wasm-video').duration : 0; } catch(e) { return 0; } });
EM_JS(double, wasmVideoTime, (int id), { try { return document.getElementById('wasm-video') ? document.getElementById('wasm-video').currentTime : 0; } catch(e) { return 0; } });
EM_JS(void, wasmVideoSeek, (int id, double t), { try { var e = document.getElementById('wasm-video'); if (e) e.currentTime = t; } catch(e) {} });
EM_JS(int, wasmVideoEnded, (int id), { try { var e = document.getElementById('wasm-video'); return e ? (e.ended ? 1 : 0) : 0; } catch(e) { return 0; } });
EM_JS(int, wasmVideoWidth, (int id), { try { var e = document.getElementById('wasm-video'); return e ? (e.videoWidth || 0) : 0; } catch(e) { return 0; } });
EM_JS(int, wasmVideoHeight, (int id), { try { var e = document.getElementById('wasm-video'); return e ? (e.videoHeight || 0) : 0; } catch(e) { return 0; } });
EM_JS(int, wasmVideoReady, (int id), { try { var e = document.getElementById('wasm-video'); return e ? (e.readyState || 0) : 0; } catch(e) { return 0; } });
EM_JS(void, wasmShowOverlay, (int vid, int show, int l, int t, int r, int b), {
    try {
        var el = document.getElementById('wasm-video-wrap');
        if (!el) return;
        el.style.display = show ? 'block' : 'none';
    } catch(e) {}
});
EM_JS(int, wasmCaptureFrame, (int id, int buf, int size), {
    try {
        var el = document.getElementById('wasm-video'); if (!el || !el.videoWidth) return 0;
        if (!window.__vctx) { window.__vc = document.createElement('canvas'); window.__vctx = window.__vc.getContext('2d'); }
        var w = el.videoWidth, h = el.videoHeight, n = w * h * 4;
        if (n > size) return 0;
        window.__vc.width = w; window.__vc.height = h;
        window.__vctx.drawImage(el, 0, 0, w, h);
        var d = new Uint8Array(Module.HEAPU8.buffer, buf, n);
        d.set(window.__vctx.getImageData(0, 0, w, h).data);
        return 1;
    } catch(e) { console.log('[video] capture:', e); return 0; }
});

// ============================================================
// WasmVideoPlayer
// ============================================================
class WasmVideoPlayer : public OverlayVideoPlayer, public LayerVideoPlayer,
                        public tTVPContinuousEventCallbackIntf
{
    uint32_t ref = 1;
    tTJSNI_VideoOverlay* m_cb = nullptr;

    uint8_t* streamData = nullptr;
    size_t streamSize = 0;
    int videoId = -1;
    int vW = 0, vH = 0;
    double duration = 0;
    std::vector<uint8_t> rgbaScratch;

    bool overlayVisible = false;
    bool hasEnded = false;
    bool _metadataReady = false;
    int _lastL = 0, _lastT = 0, _lastR = 0, _lastB = 0;

public:
    WasmVideoPlayer() { TVPAddLog(ttstr(TJS_N("[wasm_video] constructed"))); }
    ~WasmVideoPlayer()
    {
        TVPRemoveContinuousEventHook(this);
        Close();
        TVPAddLog(ttstr(TJS_N("[wasm_video] destroyed")));
    }

    void Close()
    {
        if (videoId >= 0) { wasmVideoStop(0); wasmShowOverlay(0, 0, 0, 0, 0, 0); }
        delete[] streamData; streamData = nullptr;
        overlayVisible = false;
    }

    // 鈹€鈹€ OverlayVideoPlayer 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    void SetWindow(class tTJSNI_Window* w) override
    {
        TVPAddLog(ttstr(TJS_N("[wasm_video] SetWindow")));
    }
    void SetMessageDrainWindow(void* w) override
    {
        m_cb = (tTJSNI_VideoOverlay*)w;
        TVPAddLog(ttstr(TJS_N("[wasm_video] SetMessageDrainWindow cb=")) + ttstr((int64_t)(intptr_t)w));
    }

    void SetRect(int l, int t, int r, int b) override
    {
        _lastL = l; _lastT = t; _lastR = r; _lastB = b;
        TVPAddLog(ttstr(TJS_N("[wasm_video] SetRect ")) + ttstr(l) + ttstr(TJS_N(",")) + ttstr(t) +
                  ttstr(TJS_N(",")) + ttstr(r) + ttstr(TJS_N(",")) + ttstr(b));
        if (overlayVisible && videoId >= 0)
            wasmShowOverlay(videoId, 1, l, t, r, b);
    }

    void SetVisible(bool v) override
    {
        TVPAddLog(ttstr(TJS_N("[wasm_video] SetVisible ")) + ttstr((int)v));
        overlayVisible = v;
        if (videoId >= 0)
            wasmShowOverlay(videoId, v ? 1 : 0, _lastL, _lastT, _lastR, _lastB);
    }

    bool OpenStream(TJS::tTJSBinaryStream* stream, const ttstr& streamname) override
    {
        TVPAddLog(ttstr(TJS_N("[wasm_video] OpenStream ")) + streamname);
        if (!stream) { TVPAddLog(ttstr(TJS_N("[wasm_video]   ERROR: null stream"))); return false; }
        streamSize = (size_t)stream->GetSize();
        TVPAddLog(ttstr(TJS_N("[wasm_video]   streamSize=")) + ttstr((int)streamSize));
        if (streamSize == 0 || streamSize > 500 * 1024 * 1024) {
            TVPAddLog(ttstr(TJS_N("[wasm_video]   ERROR: bad size")));
            return false;
        }
        streamData = new uint8_t[streamSize];
        if (!streamData) { TVPAddLog(ttstr(TJS_N("[wasm_video]   ERROR: alloc failed"))); return false; }
        stream->SetPosition(0);
        size_t total = 0;
        while (total < streamSize) {
            tjs_uint n = stream->Read(streamData + total, (tjs_uint)(streamSize - total));
            if (n <= 0) break;
            total += n;
        }
        if (total != streamSize) {
            TVPAddLog(ttstr(TJS_N("[wasm_video]   ERROR: read only ")) + ttstr((int)total) + ttstr(TJS_N("/")) + ttstr((int)streamSize));
            delete[] streamData; streamData = nullptr; return false;
        }
        TVPAddLog(ttstr(TJS_N("[wasm_video]   read OK, ")) + ttstr((int)streamSize) + ttstr(TJS_N(" bytes")));

        ttstr ext = TVPExtractStorageExt(streamname);
        ext.ToLowerCase();
        const char* mime = "video/mp4";
        if (ext == TJS_N(".webm")) mime = "video/webm";
        else if (ext == TJS_N(".ogv")) mime = "video/ogg";
        else if (ext == TJS_N(".mpg") || ext == TJS_N(".mpeg")) mime = "video/mpeg";
        TVPAddLog(ttstr(TJS_N("[wasm_video]   mime=")) + ttstr(mime));
        TVPAddLog(ttstr(TJS_N("[wasm_video]   streaming ")) + ttstr((int)streamSize) + TJS_N(" bytes to video"));

        // Pass data directly — EM_ASYNC_JS copies from HEAP to regular ArrayBuffer in JS
        videoId = wasmCreateVideo(mime, (int)(uintptr_t)streamData, (int)streamSize);
        if (videoId < 0) {
            TVPAddLog(ttstr(TJS_N("[wasm_video]   ERROR: createVideo failed")));
            delete[] streamData; streamData = nullptr; return false;
        }
        TVPAddLog(ttstr(TJS_N("[wasm_video]   videoId=")) + ttstr(videoId));

        // Don't block — metadata/error arrive via browser event loop which
        // can't run while emscripten_sleep blocks the main thread.
        // Just return true and check in OnContinuousCallback.
        TVPAddLog(ttstr(TJS_N("[wasm_video]   opened (async metadata)")));
        return true;
    }

    // 鈹€鈹€ iTVPVideoOverlay 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    void AddRef() override { ref++; }
    void Release() override { if (--ref == 0) delete this; }

    void Play() override
    {
        if (videoId < 0) return;
        TVPAddLog(ttstr(TJS_N("[wasm_video] Play")));
        TVPAddContinuousEventHook(this);
        if (wasmVideoEnded(videoId)) wasmVideoSeek(videoId, 0);
        wasmVideoPlay(videoId);
        if (overlayVisible) wasmShowOverlay(videoId, 1, _lastL, _lastT, _lastR, _lastB);
        hasEnded = false;
    }

    void Stop() override
    {
        TVPAddLog(ttstr(TJS_N("[wasm_video] Stop")));
        if (videoId >= 0) { wasmVideoStop(videoId); wasmShowOverlay(videoId, 0, 0, 0, 0, 0); }
        overlayVisible = false;
    }

    void Pause() override
    {
        TVPAddLog(ttstr(TJS_N("[wasm_video] Pause")));
        if (videoId >= 0) wasmVideoPause(videoId);
    }

    void SetPosition(uint64_t tick) override
    {
        if (videoId >= 0) wasmVideoSeek(videoId, tick / 1000.0);
    }
    void GetPosition(uint64_t* tick) override
    {
        *tick = videoId >= 0 ? (uint64_t)(wasmVideoTime(videoId) * 1000.0) : 0;
    }
    void GetStatus(tTVPVideoStatus* s) override
    {
        if (!s) return;
        if (videoId < 0 || hasEnded) { *s = vsStopped; return; }
        if (wasmVideoEnded(videoId)) { *s = vsEnded; return; }
        *s = vsPlaying;
    }
    void Rewind() override { if (videoId >= 0) wasmVideoSeek(videoId, 0); hasEnded = false; }
    void SetFrame(int f) override { if (videoId >= 0) wasmVideoSeek(videoId, f / 30.0); }
    void GetFrame(int* f) override { *f = videoId >= 0 ? (int)(wasmVideoTime(videoId) * 30.0) : 0; }
    void GetFPS(double* f) override { *f = 30.0; }
    void GetNumberOfFrame(int* n) override { *n = (int)(duration * 30.0); }
    void GetTotalTime(int64_t* t) override { *t = (int64_t)(duration * 1000.0); }
    void GetVideoSize(long* w, long* h) override { if (w) *w = vW; if (h) *h = vH; }

    tTVPBaseTexture* GetFrontBuffer() override
    {
        if (videoId < 0 || vW <= 0) return nullptr;
        int ok = wasmCaptureFrame(videoId, (int)(uintptr_t)rgbaScratch.data(), (int)rgbaScratch.size());
        if (!ok) return nullptr;
        return nullptr; // layer mode: caller should manage texture output
    }

    void SetVideoBuffer(tTVPBaseTexture* b1, tTVPBaseTexture* b2, long size) override {}

    void OnContinuousCallback(tjs_uint64 tick) override
    {
        if (videoId < 0) return;

        static int _dumpCtr = 0;
        if (++_dumpCtr % 120 == 1) wasmVideoDump(videoId);
        int ev = wasmVideoEvents(videoId);
        int rs = wasmVideoReady(videoId);

        // Check if metadata just arrived
        if (!_metadataReady && rs >= 1) {
            vW = wasmVideoWidth(videoId);
            vH = wasmVideoHeight(videoId);
            duration = wasmVideoDur(videoId);
            _metadataReady = true;
            if (vW > 0) rgbaScratch.resize(vW * vH * 4);
            TVPAddLog(ttstr(TJS_N("[wasm_video] metadata ready ")) + ttstr(vW) + ttstr(TJS_N("x")) + ttstr(vH));
        }

        // Check for decode error
        if (ev & 8 && !hasEnded) {
            hasEnded = true;
            TVPAddLog(ttstr(TJS_N("[wasm_video] browser decode error, posting EC_COMPLETE")));
            if (m_cb) { NativeEvent ev(WM_GRAPHNOTIFY); ev.WParam = EC_COMPLETE; ev.LParam = 0; m_cb->PostEvent(ev); }
            if (overlayVisible) { wasmShowOverlay(videoId, 0, 0, 0, 0, 0); overlayVisible = false; }
            return;
        }

        if (wasmVideoEnded(videoId) && !hasEnded) {
            hasEnded = true;
            TVPAddLog(ttstr(TJS_N("[wasm_video] ended, posting EC_COMPLETE")));
            if (m_cb) { NativeEvent ev(WM_GRAPHNOTIFY); ev.WParam = EC_COMPLETE; ev.LParam = 0; m_cb->PostEvent(ev); }
            if (overlayVisible) { wasmShowOverlay(videoId, 0, 0, 0, 0, 0); overlayVisible = false; }
        }
        else if (!hasEnded && m_cb && rs >= 1) {
            NativeEvent ev(WM_GRAPHNOTIFY);
            ev.WParam = EC_UPDATE;
            ev.LParam = (int)(wasmVideoTime(videoId) * 30.0);
            m_cb->WndProc(ev);
        }
    }

    void SetStopFrame(int) override {}
    void GetStopFrame(int* f) override { *f = 0; }
    void SetDefaultStopFrame() override {}
    void SetPlayRate(double) override {}
    void GetPlayRate(double* r) override { *r = 1.0; }
    void SetAudioBalance(long) override {}
    void GetAudioBalance(long* b) override { *b = 0; }
    void SetAudioVolume(long) override {}
    void GetAudioVolume(long* v) override { *v = 100; }
    void GetNumberOfAudioStream(unsigned long* c) override { *c = 1; }
    void SelectAudioStream(unsigned long) override {}
    void GetEnableAudioStreamNum(long* n) override { *n = 0; }
    void DisableAudioStream() override {}
    void GetNumberOfVideoStream(unsigned long* c) override { *c = 1; }
    void SelectVideoStream(unsigned long) override {}
    void GetEnableVideoStreamNum(long* n) override { *n = 1; }
    void SetLoopSegement(int, int) override {}
    void SetMixingBitmap(class tTVPBaseTexture* dest, float alpha) override {}
    void ResetMixingBitmap() override {}
    void SetMixingMovieAlpha(float) override {}
    void GetMixingMovieAlpha(float* a) override { *a = 1.0f; }
    void SetMixingMovieBGColor(unsigned long) override {}
    void GetMixingMovieBGColor(unsigned long* c) override { *c = 0xFF000000; }
    void PresentVideoImage() override {}

    void GetContrastRangeMin(float* v) override { if (v) *v = -1.0f; }
    void GetContrastRangeMax(float* v) override { if (v) *v = 1.0f; }
    void GetContrastDefaultValue(float* v) override { if (v) *v = 0.0f; }
    void GetContrastStepSize(float* v) override { if (v) *v = 0.01f; }
    void GetContrast(float* v) override { if (v) *v = 0.0f; }
    void SetContrast(float) override {}
    void GetBrightnessRangeMin(float* v) override { if (v) *v = -1.0f; }
    void GetBrightnessRangeMax(float* v) override { if (v) *v = 1.0f; }
    void GetBrightnessDefaultValue(float* v) override { if (v) *v = 0.0f; }
    void GetBrightnessStepSize(float* v) override { if (v) *v = 0.01f; }
    void GetBrightness(float* v) override { if (v) *v = 0.0f; }
    void SetBrightness(float) override {}
    void GetHueRangeMin(float* v) override { if (v) *v = -180.0f; }
    void GetHueRangeMax(float* v) override { if (v) *v = 180.0f; }
    void GetHueDefaultValue(float* v) override { if (v) *v = 0.0f; }
    void GetHueStepSize(float* v) override { if (v) *v = 1.0f; }
    void GetHue(float* v) override { if (v) *v = 0.0f; }
    void SetHue(float) override {}
    void GetSaturationRangeMin(float* v) override { if (v) *v = -1.0f; }
    void GetSaturationRangeMax(float* v) override { if (v) *v = 1.0f; }
    void GetSaturationDefaultValue(float* v) override { if (v) *v = 0.0f; }
    void GetSaturationStepSize(float* v) override { if (v) *v = 0.01f; }
    void GetSaturation(float* v) override { if (v) *v = 0.0f; }
    void SetSaturation(float) override {}
};

OverlayVideoPlayer* CreateOverlayVideoPlayer() { return new WasmVideoPlayer(); }
LayerVideoPlayer*    CreateLayerVideoPlayer()    { return new WasmVideoPlayer(); }




