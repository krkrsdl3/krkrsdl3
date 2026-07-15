//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// WASM 原生音频后端 — AudioWorkletNode 独立音频线程 + 单共享环形缓冲。
//
//  C++ 侧：单共享环形缓冲，AppendBuffer 写入，IsBufferValid/GetRemainBuffers
//         查询缓冲状态。
//  JS 侧：AudioWorkletNode 在独立音频线程读取环形缓冲并输出。
//---------------------------------------------------------------------------

#include "tjsCommHead.h"
#include "PlatformAudio.h"
#include "PlatformMutex.h"
#include "PlatformThread.h"
#include "TVPSystem.h"
#include "TVPDebug.h"
#include "WaveIntf.h"

#include <emscripten/emscripten.h>
#include <emscripten/threading.h>
#include <atomic>
#include <cstring>
#include <algorithm>

//---------------------------------------------------------------------------
// JS: AudioContext + AudioWorkletNode
//---------------------------------------------------------------------------
EM_JS(void, wasmAudioInitJS, (int sampleRate, int channels), {
    if (typeof wasmAudioCtx !== "undefined" && wasmAudioCtx) {
        try { wasmAudioCtx.close(); } catch(e) {}
    }
    try {
        wasmAudioCtx = new (AudioContext || webkitAudioContext)({
            sampleRate: sampleRate
        });
        console.log("[wasm_audio] AudioContext created, sampleRate=" + wasmAudioCtx.sampleRate);

        var sps = wasmAudioCtx.createScriptProcessor(2048, 0, channels);
        sps.onaudioprocess = function(e) {
            var out = e.outputBuffer;
            var nCh = out.numberOfChannels;
            var nLen = out.length;

            var framesRead = _wasmAudioPullData(nLen);
            if (framesRead > 0) {
                var srcPtr = _wasmAudioGetReadPtr();
                var srcF32 = new Float32Array(Module.HEAPF32.buffer, srcPtr, framesRead * nCh);
                for (var c = 0; c < nCh; c++) {
                    var dst = out.getChannelData(c);
                    for (var i = 0; i < framesRead; i++) dst[i] = srcF32[i * nCh + c];
                }
            }
        };
        wasmAudioCtx.sps = sps;
        wasmAudioCtx.sps.connect(wasmAudioCtx.destination);
        console.log("[wasm_audio] ScriptProcessorNode connected");
    } catch(e) {
        console.log("[wasm_audio] init error:", e);
        wasmAudioCtx = null;
    }
});

EM_JS(void, wasmAudioResume, (), {
    if (typeof wasmAudioCtx !== "undefined" && wasmAudioCtx && wasmAudioCtx.state === "suspended")
        wasmAudioCtx.resume();
});

//---------------------------------------------------------------------------
// 全局环形缓冲区（单共享缓冲）
//---------------------------------------------------------------------------
#define RING_BUF_FRAMES (256 * 1024)

static float g_ringBuf[RING_BUF_FRAMES * 2];
static int g_ringChannels = 2;
static std::atomic<int> g_ringWritePos{0};
static std::atomic<int> g_ringReadPos{0};
static std::atomic<int> g_ringPending{0};
static float g_readBuf[4096 * 2];
static bool g_audioCtxNeeded = false;
static bool g_audioCtxDone = false;
static int g_sampleRate = 48000;

extern "C" {

int EMSCRIPTEN_KEEPALIVE wasmAudioPullData(int maxFrames)
{
    int avail = g_ringPending.load(std::memory_order_acquire);
    if (avail <= 0) return 0;
    int framesToRead = (maxFrames < avail) ? maxFrames : avail;
    int readPos = g_ringReadPos.load(std::memory_order_relaxed);
    int ch = g_ringChannels;
    for (int i = 0; i < framesToRead; i++) {
        int idx = (readPos + i) % RING_BUF_FRAMES;
        for (int c = 0; c < ch; c++)
            g_readBuf[i * ch + c] = g_ringBuf[idx * ch + c];
    }
    g_ringReadPos.store((readPos + framesToRead) % RING_BUF_FRAMES, std::memory_order_release);
    g_ringPending.fetch_sub(framesToRead, std::memory_order_release);
    return framesToRead;
}

int EMSCRIPTEN_KEEPALIVE wasmAudioGetReadPtr() { return (int)(uintptr_t)g_readBuf; }

// 为 AudioWorkletNode 导出的地址
int EMSCRIPTEN_KEEPALIVE wasmGetRingBufAddr() { return (int)(uintptr_t)g_ringBuf; }
int EMSCRIPTEN_KEEPALIVE wasmGetPendingAddr()  { return (int)(uintptr_t)&g_ringPending; }
int EMSCRIPTEN_KEEPALIVE wasmGetReadPosAddr()  { return (int)(uintptr_t)&g_ringReadPos; }
int EMSCRIPTEN_KEEPALIVE wasmGetChannelsAddr() { return (int)(uintptr_t)&g_ringChannels; }

} // extern "C"

//---------------------------------------------------------------------------
// tTVPSoundBufferWASM
//---------------------------------------------------------------------------
#ifndef TVP_WSB_ACCESS_FREQ
#define TVP_WSB_ACCESS_FREQ 8
#endif

class tTVPSoundBufferWASM : public iTVPSoundBuffer
{
public:
    bool _playing = false;
    float _volume = 1, _pan = 0;

    tTVPWaveFormat _format;
    int _frame_size = 0, _bytesPerSample = 0, _accessUnitSamples = 6000;

    tTVPSoundBufferWASM(tTVPWaveFormat& fmt, int bufcount)
      : _frame_size(fmt.BytesPerSample * fmt.Channels),
        _accessUnitSamples(fmt.SamplesPerSec / TVP_WSB_ACCESS_FREQ)
    {
        if (_accessUnitSamples < 1) _accessUnitSamples = 1;
        _format = fmt; _bytesPerSample = fmt.BytesPerSample;
    }

    virtual ~tTVPSoundBufferWASM() { Stop(); }

    virtual bool Init() override
    {
        if (_format.BitsPerSample != 16 && _format.BitsPerSample != 32) {
            TVPAddLog(ttstr(TJS_N("wasm_audio: unsupported bits: ")) + ttstr((int)_format.BitsPerSample));
            delete this; return false;
        }
        if (!g_audioCtxDone) {
            g_ringChannels = _format.Channels;
            g_sampleRate = _format.SamplesPerSec;
            g_audioCtxNeeded = true;
        }
        TVPAddLog(ttstr(TJS_N("wasm_audio: Init() freq=")) + ttstr((int)_format.SamplesPerSec)
                  + ttstr(TJS_N(" ch=")) + ttstr((int)_format.Channels)
                  + ttstr(TJS_N(" bits=")) + ttstr((int)_format.BitsPerSample));
        return true;
    }

    virtual void Release() override { delete this; }
    virtual void Play() override { if (_playing) return; wasmAudioResume(); _playing = true; }
    virtual void Pause() override { _playing = false; }

    virtual void Stop() override { _playing = false; }
    virtual void Reset() override {
        g_ringWritePos.store(0, std::memory_order_release);
        g_ringReadPos.store(0, std::memory_order_release);
        g_ringPending.store(0, std::memory_order_release);
    }

    virtual bool IsPlaying() override { return _playing; }
    virtual void SetVolume(float v) override { _volume = v; }
    virtual float GetVolume() override { return _volume; }
    virtual void SetPan(float v) override { _pan = v; }
    virtual float GetPan() override { return _pan; }

    virtual void AppendBuffer(const void* _inbuf, unsigned int inlen) override
    {
        if (inlen <= 0 || !_inbuf) return;
        int frames = inlen / _frame_size;
        if (frames <= 0) return;
        if (g_ringPending.load(std::memory_order_acquire) >= RING_BUF_FRAMES - 4096) return;

        const unsigned char* src = (const unsigned char*)_inbuf;
        int writePos = g_ringWritePos.load(std::memory_order_relaxed);
        int ch = _format.Channels;

        for (int i = 0; i < frames; i++) {
            int bufIdx = (writePos + i) % RING_BUF_FRAMES;
            for (int c = 0; c < ch; c++) {
                float s = 0;
                if (_bytesPerSample == 2)
                    s = ((const short*)src)[i * ch + c] * (1.0f / 32768.0f);
                else if (_bytesPerSample == 4)
                    s = ((const int*)src)[i * ch + c] * (1.0f / 2147483648.0f);
                g_ringBuf[bufIdx * ch + c] = s;
            }
        }
        g_ringWritePos.store((writePos + frames) % RING_BUF_FRAMES, std::memory_order_release);
        g_ringPending.fetch_add(frames, std::memory_order_release);
    }

    virtual bool IsBufferValid() override
    {
        int pend = g_ringPending.load(std::memory_order_acquire);
        return (pend > 0) || (pend < RING_BUF_FRAMES - 4096);
    }

    virtual bool IsValidFormat(tTVPWaveFormat& fmt) override
    {
        return _format.SamplesPerSec == fmt.SamplesPerSec &&
               _format.BitsPerSample == fmt.BitsPerSample &&
               _format.Channels == fmt.Channels;
    }

    virtual tjs_uint GetCurrentPlaySamples() override
    {
        return (tjs_uint)g_ringPending.load(std::memory_order_acquire);
    }

    virtual int GetRemainBuffers() override
    {
        int pend = g_ringPending.load(std::memory_order_acquire);
        if (pend <= 0) return 0;
        return pend / _accessUnitSamples;
    }

    virtual tjs_uint GetLatencySamples() override { return 0; }
    virtual float GetLatencySeconds() override { return 0; }
    virtual void SetPosition(float x, float y, float z) override {}
};

//---------------------------------------------------------------------------
// 全局初始化
//---------------------------------------------------------------------------
static bool g_devInited = false;

void TVPInitDirectSound(int freq)
{
    if (!g_devInited) { g_devInited = true; TVPAddLog(ttstr(TJS_N("wasm_audio: TVPInitDirectSound"))); }
}

void TVPUninitDirectSound() {}

iTVPSoundBuffer* TVPCreateSoundBuffer(tTVPWaveFormat& fmt, int bufcount)
{
    tTVPSoundBufferWASM* buf = new tTVPSoundBufferWASM(fmt, bufcount);
    return (buf && buf->Init()) ? buf : nullptr;
}

void wasmAudioTick()
{
    if (!g_audioCtxDone && g_audioCtxNeeded && emscripten_is_main_browser_thread()) {
        g_audioCtxDone = true;
        TVPAddLog(ttstr(TJS_N("wasm_audio: creating AudioContext...")));
        wasmAudioInitJS(g_sampleRate, g_ringChannels);
        TVPAddLog(ttstr(TJS_N("wasm_audio: AudioContext created")));
    }
    wasmAudioResume();
}
