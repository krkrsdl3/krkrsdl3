#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <comdef.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <cstring>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")

#include "tjsCommHead.h"
#include "Platform.h"
#include "PlatformMutex.h"
#include "PlatformVideo.h"
#include "TVPEvent.h"
#include "TVPStorage.h"
#include "LayerBitmap.h"
#include "TVPCompositor.h"

// --- UTF-8 to UTF-16 helper ---
static std::wstring to_wide(const ttstr& s)
{
    const char* src = s.c_str();
    int len = MultiByteToWideChar(CP_UTF8, 0, src, -1, nullptr, 0);
    if (len <= 0) return std::wstring();
    std::wstring w(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, src, -1, &w[0], len);
    return w;
}

// --- NV12 to RGBA ---
// stride 可能为负（bottom-up 存储），此时首行在缓冲区末尾
static void NV12ToRGBA(const uint8_t* src, int stride, UINT32 width, UINT32 height,
                       uint8_t* dst, UINT32 dstStride)
{
    const int absStride = stride < 0 ? -stride : stride;
    const uint8_t* y_plane = src;
    const uint8_t* uv_plane = src + absStride * (int)height;
    for (UINT32 y = 0; y < height; y++) {
        UINT32 yRow = stride < 0 ? height - 1 - y : y;
        UINT32 uvRow = stride < 0 ? height / 2 - 1 - y / 2 : y / 2;
        const uint8_t* y_row = y_plane + yRow * absStride;
        const uint8_t* uv_row = uv_plane + uvRow * absStride;
        uint8_t* dst_row = dst + y * dstStride;
        for (UINT32 x = 0; x < width; x++) {
            int Y = y_row[x];
            int U = uv_row[(x / 2) * 2] - 128;
            int V = uv_row[(x / 2) * 2 + 1] - 128;
            int R = Y + (int)(1.402f * V);
            int G = Y - (int)(0.344f * U + 0.714f * V);
            int B = Y + (int)(1.772f * U);
            dst_row[x * 4 + 0] = (uint8_t)std::max(0, std::min(255, R));
            dst_row[x * 4 + 1] = (uint8_t)std::max(0, std::min(255, G));
            dst_row[x * 4 + 2] = (uint8_t)std::max(0, std::min(255, B));
            dst_row[x * 4 + 3] = 255;
        }
    }
}

// --- IMFByteStream wrapper around tTJSBinaryStream ---
// Media Foundation 的 MP4 解析会用 BeginRead/EndRead 异步读；
// 同步 Read 只用于嗅探阶段。
class MFByteStream : public IMFByteStream
{
    LONG _ref = 1;
    tTJSBinaryStream* _stream;
    tTJSCriticalSection _lock;

    class AsyncResult : public IMFAsyncResult
    {
        LONG ref = 1;
        IUnknown* state = nullptr;
        HRESULT status = S_OK;

    public:
        ULONG bytesRead = 0;

        explicit AsyncResult(IUnknown* s) : state(s)
        {
            if (state)
                state->AddRef();
        }
        ~AsyncResult()
        {
            if (state)
                state->Release();
        }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
        {
            if (riid == IID_IUnknown || riid == IID_IMFAsyncResult)
            {
                *ppv = static_cast<IMFAsyncResult*>(this);
                AddRef();
                return S_OK;
            }
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref); }
        ULONG STDMETHODCALLTYPE Release() override
        { LONG r = InterlockedDecrement(&ref); if (r == 0) delete this; return r; }

        HRESULT STDMETHODCALLTYPE GetState(IUnknown** pp) override
        {
            if (!pp)
                return E_POINTER;
            *pp = state;
            if (state)
                state->AddRef();
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE GetStatus() override { return status; }
        HRESULT STDMETHODCALLTYPE SetStatus(HRESULT hr) override { status = hr; return S_OK; }
        HRESULT STDMETHODCALLTYPE GetObject(IUnknown** pp) override
        {
            if (pp)
                *pp = nullptr;
            return E_NOTIMPL;
        }
        IUnknown* STDMETHODCALLTYPE GetStateNoAddRef() override { return state; }
    };

    class ReadTask : public IMFAsyncCallback
    {
        LONG ref = 1;
        MFByteStream* owner;
        BYTE* buffer;
        ULONG size;
        IMFAsyncCallback* callback;
        IUnknown* state;

    public:
        ReadTask(MFByteStream* o, BYTE* b, ULONG s, IMFAsyncCallback* cb, IUnknown* st)
            : owner(o), buffer(b), size(s), callback(cb), state(st)
        {
            owner->AddRef();
            callback->AddRef();
            if (state)
                state->AddRef();
        }
        ~ReadTask()
        {
            owner->Release();
            callback->Release();
            if (state)
                state->Release();
        }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
        {
            if (riid == IID_IUnknown || riid == IID_IMFAsyncCallback)
            {
                *ppv = static_cast<IMFAsyncCallback*>(this);
                AddRef();
                return S_OK;
            }
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref); }
        ULONG STDMETHODCALLTYPE Release() override
        { LONG r = InterlockedDecrement(&ref); if (r == 0) delete this; return r; }

        HRESULT STDMETHODCALLTYPE GetParameters(DWORD* flags, DWORD* queue) override
        {
            if (flags)
                *flags = 0;
            if (queue)
                *queue = MFASYNC_CALLBACK_QUEUE_STANDARD;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE Invoke(IMFAsyncResult*) override
        {
            AsyncResult* result = new (std::nothrow) AsyncResult(state);
            if (!result)
                return E_OUTOFMEMORY;
                result->SetStatus(owner->ReadSync(buffer, size, &result->bytesRead));
            HRESULT hr = callback->Invoke(result);
            result->Release();
            return hr;
        }
    };

public:
    MFByteStream(tTJSBinaryStream* s) : _stream(s) { _stream->SetPosition(0); }

    HRESULT ReadSync(BYTE* buf, ULONG len, ULONG* read)
    {
        tTJSCriticalSectionHolder lk(_lock);
        *read = (ULONG)_stream->Read(buf, len);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == IID_IUnknown || riid == IID_IMFByteStream) {
            *ppv = static_cast<IMFByteStream*>(this); AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&_ref); }
    ULONG STDMETHODCALLTYPE Release() override
    { LONG r = InterlockedDecrement(&_ref); if (r == 0) delete this; return r; }

    HRESULT STDMETHODCALLTYPE GetCapabilities(DWORD* caps) override
    {
        *caps = MFBYTESTREAM_IS_READABLE | MFBYTESTREAM_IS_SEEKABLE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetLength(QWORD* len) override
    {
        *len = (QWORD)_stream->GetSize();
        return *len == 0 ? E_FAIL : S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetLength(QWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetCurrentPosition(QWORD* pos) override
    {
        tTJSCriticalSectionHolder lk(_lock);
        *pos = _stream->GetPosition();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetCurrentPosition(QWORD pos) override
    {
        tTJSCriticalSectionHolder lk(_lock);
        _stream->SetPosition((tjs_uint64)pos);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE IsEndOfStream(BOOL* eos) override
    {
        tTJSCriticalSectionHolder lk(_lock);
        *eos = (_stream->GetPosition() >= _stream->GetSize()) ? TRUE : FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Read(BYTE* buf, ULONG len, ULONG* read) override
    {
        return ReadSync(buf, len, read);
    }
    HRESULT STDMETHODCALLTYPE BeginRead(BYTE* buf, ULONG len, IMFAsyncCallback* callback,
                                        IUnknown* state) override
    {
        if (!callback)
            return E_INVALIDARG;
        ReadTask* task = new (std::nothrow) ReadTask(this, buf, len, callback, state);
        if (!task)
            return E_OUTOFMEMORY;
        HRESULT hr = MFPutWorkItem(MFASYNC_CALLBACK_QUEUE_STANDARD, task, nullptr);
        task->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE EndRead(IMFAsyncResult* result, ULONG* read) override
    {
        if (!result)
            return E_INVALIDARG;
        AsyncResult* r = static_cast<AsyncResult*>(result);
        if (read)
            *read = r->bytesRead;
        return r->GetStatus();
    }
    HRESULT STDMETHODCALLTYPE Write(const BYTE*, ULONG, ULONG*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE BeginWrite(const BYTE*, ULONG, IMFAsyncCallback*, IUnknown*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EndWrite(IMFAsyncResult*, ULONG*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Seek(MFBYTESTREAM_SEEK_ORIGIN origin, LONGLONG offset, DWORD, QWORD* newPos) override
    {
        tTJSCriticalSectionHolder lk(_lock);
        tjs_uint64 pos = 0;
        if (origin == msoBegin)
            pos = (tjs_uint64)offset;
        else if (origin == msoCurrent)
        {
            pos = _stream->GetPosition() + offset;
            if (offset < 0 && pos < (tjs_uint64)(-offset))
                pos = 0;
        }
        else
            return E_INVALIDARG;
        _stream->SetPosition(pos);
        if (newPos)
            *newPos = (QWORD)pos;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Flush() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }
};

// --- WinVideoPlayer --- concrete class for both overlay and layer modes ---
class WinVideoPlayer : public OverlayVideoPlayer, public LayerVideoPlayer,
                      public tTVPContinuousEventCallbackIntf
{
    uint32_t ref = 1;
    IMFSourceReader* reader = nullptr;
    IMFMediaType* videoType = nullptr;
    DWORD videoStreamIdx = (DWORD)-1;
    DWORD audioStreamIdx = (DWORD)-1;
    iTVPSoundBuffer* sound = nullptr;
    long audioVolume = 100000;
    long audioBalance = 0;
    bool mfStarted = false;
    UINT32 vWidth = 0, vHeight = 0;
    UINT32 vFrameRateNum = 30, vFrameRateDen = 1;
    int64_t totalDuration = 0;
    int totalFrames = 0;
    std::atomic<bool> playing{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> ended{false};
    std::atomic<int64_t> seekTarget{-1};
    std::thread* decodeThread = nullptr;
    std::atomic<bool> running{false};

    TVPVideoEventCallback cb = nullptr;
    void* cbctx = nullptr;
    std::chrono::steady_clock::time_point paceWall;
    int64_t pacePts = -1; // 100ns
    int64_t lastPts = 0;  // 最近写入帧的 pts（100ns），环空时 GetPosition 仍可用
    std::atomic<int> pendingFrame{0};
    std::atomic<bool> notifyEnabled{true};
    bool continuousHook = false;

    // overlay / mixer 模式：把帧画到合成器的 overlay sprite 上（与 FFmpeg 后端一致）
    bool layerMode = false;
    bool overlayVisible = false;
    TVPSprite* overlaySprite = nullptr;

    static const int MAX_FRAMES = 3;
    struct VideoFrame
    {
        uint8_t* rgba = nullptr;
        int w = 0, h = 0;
        int64_t pts = 0;
    };
    VideoFrame frames[MAX_FRAMES];
    int writeIdx = 0, readIdx = 0, frameCount = 0;
    tTJSCriticalSection frameMtx;
    tTVPBaseTexture* bmp[2] = {};
    long bmpSize = 0;
    std::vector<uint8_t> rgbaScratch;

public:
    WinVideoPlayer(TVPVideoEventCallback callback, void* ctx) : cb(callback), cbctx(ctx) {}
    ~WinVideoPlayer() { Close(); }

    // EC_UPDATE 必须在主线程派发（tjsNativeVideoOverlay 会直接操作图层）
    void OnContinuousCallback(tjs_uint64) override
    {
        if (!notifyEnabled)
            return;
        if (layerMode)
        {
            bool hasFrame;
            {
                tTJSCriticalSectionHolder lk(frameMtx);
                hasFrame = frameCount > 0;
            }
            if (hasFrame && cb)
                cb(cbctx, WM_GRAPHNOTIFY, EC_UPDATE, (uintptr_t)pendingFrame.load());
        }
        else
            PresentOverlayFrame();
    }

    // overlay / mixer 模式：取队首帧更新合成器 sprite，整窗按比例 letterbox 显示
    void PresentOverlayFrame()
    {
        tTJSCriticalSectionHolder lk(frameMtx);
        if (frameCount <= 0)
            return;
        VideoFrame& f = frames[readIdx];
        if (!f.rgba)
            return;
        if (!overlaySprite)
        {
            overlaySprite = new TVPSprite;
            overlaySprite->type = 2;
            overlaySprite->width = f.w;
            overlaySprite->height = f.h;
            krkrsdl3::TVPCreateTexture(*overlaySprite);
            krkrsdl3::TVPJoinTexture(overlaySprite);
        }
        krkrsdl3::TVPUpdateTexture(overlaySprite, f.rgba, f.w, f.h, f.w * 4);
        overlaySprite->isVisible = overlayVisible;
        readIdx = (readIdx + 1) % MAX_FRAMES;
        frameCount--;
    }

    void PostEnded()
    {
        if (notifyEnabled && cb)
            cb(cbctx, WM_GRAPHNOTIFY, EC_COMPLETE, 0);
    }

    // 按 pts 控制帧输出节奏；暂停期间不推进时间基准
    void PaceTo(int64_t pts)
    {
        auto now = std::chrono::steady_clock::now();
        if (pacePts < 0 || pts < pacePts)
        {
            pacePts = pts;
            paceWall = now;
            return;
        }
        auto due = paceWall + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                 std::chrono::nanoseconds((pts - pacePts) * 100));
        while (running)
        {
            if (seekTarget >= 0)
                return;
            if (paused || !playing)
            {
                pacePts = pts;
                paceWall = std::chrono::steady_clock::now();
                return;
            }
            now = std::chrono::steady_clock::now();
            if (now >= due)
                break;
            auto left = due - now;
            std::this_thread::sleep_for(std::min<decltype(left)>(
                left, std::chrono::milliseconds(5)));
        }
        if (std::chrono::steady_clock::now() - due > std::chrono::seconds(1))
            due = std::chrono::steady_clock::now();
        pacePts = pts;
        paceWall = due;
    }

    bool OpenStream(TJS::tTJSBinaryStream* stream, const ttstr& streamname) override
    {
        // 1. 初始化 Media Foundation
        HRESULT hr = MFStartup(MF_VERSION);
        if (FAILED(hr))
        {
            TVPConsoleLog("MFStartup failed: 0x%08X", hr);
            return false;
        }
        mfStarted = true;

        // 3. 用 TJS 流构造 IMFByteStream
        MFByteStream* bs = new (std::nothrow) MFByteStream(stream);
        if (!bs)
            return false;
        bs->AddRef();

        // 4. 创建 SourceReader
        hr = MFCreateSourceReaderFromByteStream(bs, nullptr, &reader);
        bs->Release();

        if (FAILED(hr))
        {
            TVPConsoleLog("MFCreateSourceReaderFromByteStream failed: 0x%08X", hr);
            return false;
        }

        // 5. 枚举流
        for (DWORD idx = 0; idx < 16; idx++)
        {
            IMFMediaType* mt = nullptr;
            hr = reader->GetNativeMediaType(idx, 0, &mt);
            if (hr == MF_E_INVALIDSTREAMNUMBER)
                break;
            if (FAILED(hr))
                break;
            GUID major = GUID_NULL;
            mt->GetGUID(MF_MT_MAJOR_TYPE, &major);
            if (major == MFMediaType_Audio && audioStreamIdx == (DWORD)-1)
            {
                // 音频统一解码成 16bit PCM，交给引擎的 sound buffer 播放
                UINT32 rate = 0, channels = 0;
                rate = MFGetAttributeUINT32(mt, MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
                channels = MFGetAttributeUINT32(mt, MF_MT_AUDIO_NUM_CHANNELS, 0);
                if (rate && channels)
                {
                    IMFMediaType* pcm = nullptr;
                    MFCreateMediaType(&pcm);
                    pcm->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
                    pcm->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
                    pcm->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
                    pcm->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
                    pcm->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
                    pcm->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, channels * 2);
                    pcm->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, rate * channels * 2);
                    hr = reader->SetCurrentMediaType(idx, nullptr, pcm);
                    pcm->Release();
                    if (SUCCEEDED(hr))
                    {
                        audioStreamIdx = idx;
                        tTVPWaveFormat fmt = {};
                        fmt.SamplesPerSec = rate;
                        fmt.Channels = channels;
                        fmt.BitsPerSample = 16;
                        fmt.BytesPerSample = 2;
                        fmt.TotalSamples = 0;
                        fmt.TotalTime = 0;
                        fmt.SpeakerConfig = 0;
                        fmt.IsFloat = false;
                        fmt.Seekable = false;
                        TVPInitDirectSound();
                        sound = TVPCreateSoundBuffer(fmt, 32);
                        if (sound)
                        {
                            sound->SetVolume(audioVolume / 100000.0f);
                            sound->SetPan(audioBalance / 100000.0f);
                            TVPConsoleLog("audio: %u Hz %u ch", rate, channels);
                        }
                        else
                            TVPConsoleLog("audio: TVPCreateSoundBuffer failed");
                    }
                    else
                        TVPConsoleLog("SetCurrentMediaType(PCM) failed: 0x%08X", hr);
                }
            }
            else if (major == MFMediaType_Video && videoStreamIdx == (DWORD)-1)
            {
                IMFMediaType* out = nullptr;
                MFCreateMediaType(&out);
                out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
                hr = reader->SetCurrentMediaType(idx, nullptr, out);
                out->Release();
                if (FAILED(hr))
                {
                    TVPConsoleLog("SetCurrentMediaType(NV12) failed: 0x%08X", hr);
                    mt->Release();
                    continue;
                }
                hr = reader->GetCurrentMediaType(idx, &videoType);
                if (FAILED(hr) || !videoType)
                {
                    mt->Release();
                    continue;
                }
                MFGetAttributeSize(videoType, MF_MT_FRAME_SIZE, &vWidth, &vHeight);
                UINT32 num = 0, den = 0;
                if (SUCCEEDED(MFGetAttributeRatio(videoType, MF_MT_FRAME_RATE, &num, &den)) &&
                    num && den)
                {
                    vFrameRateNum = num;
                    vFrameRateDen = den;
                }
                videoStreamIdx = idx;
                if (vWidth > 0 && vHeight > 0)
                    rgbaScratch.resize(vWidth * vHeight * 4);
            }
            mt->Release();
        }
        if (videoStreamIdx == (DWORD)-1)
            return false;

        // 6. 读取总时长（100ns）
        {
            PROPVARIANT var;
            PropVariantInit(&var);
            if (SUCCEEDED(reader->GetPresentationAttribute(
                    MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &var)) &&
                var.vt == VT_UI8)
            {
                totalDuration = (int64_t)var.uhVal.QuadPart;
            }
            PropVariantClear(&var);
        }
        if (vFrameRateNum && totalDuration > 0)
            totalFrames = (int)((double)totalDuration / 10000000.0 * vFrameRateNum / vFrameRateDen);

        running = true;
        decodeThread = new std::thread(&WinVideoPlayer::DecodeThreadProc, this);
        return true;
    }

    void Close()
    {
        notifyEnabled = false;
        if (continuousHook)
        {
            TVPRemoveContinuousEventHook(this);
            continuousHook = false;
        }
        running = false;
        playing = false;
        paused = false;
        if (decodeThread)
        {
            decodeThread->join();
            delete decodeThread;
            decodeThread = nullptr;
        }
        if (overlaySprite)
        {
            if (overlaySprite->texture)
            {
                krkrsdl3::TVPDepartTexture(overlaySprite);
                krkrsdl3::TVPDestroyTexture(overlaySprite);
            }
            delete overlaySprite;
            overlaySprite = nullptr;
        }
        {
            tTJSCriticalSectionHolder lk(frameMtx);
            for (int i = 0; i < MAX_FRAMES; i++)
            {
                delete[] frames[i].rgba;
                frames[i].rgba = nullptr;
            }
            frameCount = 0;
        }
        if (videoType)
        {
            videoType->Release();
            videoType = nullptr;
        }
        if (reader)
        {
            reader->Release();
            reader = nullptr;
        }
        if (sound)
        {
            sound->Stop();
            delete sound;
            sound = nullptr;
        }
        if (mfStarted)
        {
            MFShutdown();
            mfStarted = false;
        }
    }

    // 音频按 sound buffer 余量节流：一次补若干块，避免视频节拍导致欠载
    void PumpAudio()
    {
        if (!sound || audioStreamIdx == (DWORD)-1 || !running || !playing || paused)
            return;
        for (int i = 0; i < 8 && sound->IsBufferValid(); ++i)
        {
            DWORD flags = 0;
            LONGLONG ts = 0;
            IMFSample* s = nullptr;
            HRESULT hr = reader->ReadSample(audioStreamIdx, 0, nullptr, &flags, &ts, &s);
            if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM))
            {
                if (s)
                    s->Release();
                break;
            }
            if (!s)
                break;
            IMFMediaBuffer* b = nullptr;
            if (SUCCEEDED(s->ConvertToContiguousBuffer(&b)))
            {
                BYTE* p = nullptr;
                DWORD len = 0;
                if (SUCCEEDED(b->Lock(&p, nullptr, &len)) && p && len)
                {
                    sound->AppendBuffer(p, len);
                    b->Unlock();
                }
                b->Release();
            }
            s->Release();
        }
        if (!sound->IsPlaying())
            sound->Play();
    }

    void DecodeThreadProc()
    {
        if (!reader)
            return;
        while (running)
        {
            int64_t seek = seekTarget.exchange(-1);
            if (seek >= 0)
            {
                PROPVARIANT var;
                var.vt = VT_I8;
                var.hVal.QuadPart = seek;
                reader->SetCurrentPosition(GUID_NULL, var);
                {
                    tTJSCriticalSectionHolder lk(frameMtx);
                    frameCount = 0;
                }
                if (sound)
                    sound->Reset();
                ended = false;
                pacePts = -1;
                paceWall = std::chrono::steady_clock::now();
            }
            if (paused || (!playing && ended))
            {
                pacePts = -1; // 暂停/停止期间不走时，恢复后不快进
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            if (!playing)
            {
                pacePts = -1;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            PumpAudio();

            DWORD flags = 0;
            LONGLONG ts = 0;
            IMFSample* sample = nullptr;
            HRESULT hr = reader->ReadSample(videoStreamIdx, 0, nullptr, &flags, &ts, &sample);
            if (hr == MF_E_END_OF_STREAM || (flags & MF_SOURCE_READERF_ENDOFSTREAM))
            {
                if (sample)
                    sample->Release();
                if (!ended.exchange(true))
                {
                    playing = false;
                    PostEnded();
                }
                continue;
            }
            if (FAILED(hr))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            if (!sample)
                continue;

            // 按 pts 等待展示时刻；期间发生 seek 则丢弃这一帧
            PaceTo(ts);
            if (!running)
            {
                sample->Release();
                break;
            }
            if (seekTarget >= 0)
            {
                sample->Release();
                continue;
            }

            IMFMediaBuffer* buf = nullptr;
            if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf)))
            {
                BYTE* data = nullptr;
                DWORD len = 0;
                if (SUCCEEDED(buf->Lock(&data, nullptr, &len)) && data && vWidth)
                {
                    int stride = (int)(INT32)MFGetAttributeUINT32(
                        videoType, MF_MT_DEFAULT_STRIDE, (UINT32)vWidth);
                    if (!stride)
                        stride = (int)vWidth;
                    NV12ToRGBA(data, stride, vWidth, vHeight, rgbaScratch.data(), vWidth * 4);
                    buf->Unlock();

                    tTJSCriticalSectionHolder lk(frameMtx);
                    if (frameCount >= MAX_FRAMES)
                    {
                        readIdx = (readIdx + 1) % MAX_FRAMES;
                        frameCount--;
                    }
                    VideoFrame& f = frames[writeIdx];
                    if (!f.rgba)
                        f.rgba = new uint8_t[vWidth * vHeight * 4];
                    memcpy(f.rgba, rgbaScratch.data(), vWidth * vHeight * 4);
                    f.w = vWidth;
                    f.h = vHeight;
                    f.pts = ts;
                    lastPts = ts;
                    writeIdx = (writeIdx + 1) % MAX_FRAMES;
                    frameCount++;
                }
                buf->Release();
            }
            if (sample)
                sample->Release();

            if (vFrameRateNum && vFrameRateDen)
                pendingFrame = (int)(ts * vFrameRateNum / (10000000LL * vFrameRateDen));
        }
    }

    void AddRef() override { ref++; }
    void Release() override
    {
        if (--ref == 0)
            delete this;
    }

    void SetWindow(class tTJSNI_Window*) override {}
    void SetMessageDrainWindow(void*) override {}
    void SetRect(int, int, int, int) override {}
    void SetVisible(bool b) override
    {
        if (layerMode)
            return; // 图层模式由 tTJSNI_VideoOverlay 自己控制可见性
        overlayVisible = b;
        if (overlaySprite)
            overlaySprite->isVisible = b;
    }

    void Play() override
    {
        if (!continuousHook)
        {
            TVPAddContinuousEventHook(this);
            continuousHook = true;
        }
        if (ended)
        {
            seekTarget = 0;
            ended = false;
        }
        overlayVisible = true;
        if (overlaySprite)
            overlaySprite->isVisible = true;
        if (sound)
            sound->Play();
        playing = true;
        paused = false;
    }
    void Stop() override
    {
        playing = false;
        paused = false;
        ended = true;
        seekTarget = 0;
        overlayVisible = false;
        if (overlaySprite)
            overlaySprite->isVisible = false;
        if (sound)
            sound->Stop();
    }
    void Pause() override
    {
        paused = true;
        if (sound)
            sound->Pause();
    }

    void SetPosition(uint64_t tick) override { seekTarget = (int64_t)tick * 10000; }
    void GetPosition(uint64_t* tick) override
    {
        if (!tick)
            return;
        tTJSCriticalSectionHolder lk(frameMtx);
        *tick = lastPts / 10000;
    }

    void GetStatus(tTVPVideoStatus* s) override
    {
        if (!s)
            return;
        if (ended)
            *s = vsEnded;
        else if (paused)
            *s = vsPaused;
        else if (playing)
            *s = vsPlaying;
        else
            *s = vsStopped;
    }

    void Rewind() override
    {
        seekTarget = 0;
        ended = false;
    }

    void SetFrame(int f) override
    {
        if (vFrameRateNum && vFrameRateDen)
        {
            double sec = (double)f * vFrameRateDen / vFrameRateNum;
            seekTarget = (int64_t)(sec * 10000000.0);
        }
    }

    void GetFrame(int* f) override
    {
        if (!f)
            return;
        uint64_t pos = 0;
        GetPosition(&pos);
        if (vFrameRateNum && vFrameRateDen && pos > 0)
            *f = (int)((double)pos / 1000.0 * vFrameRateNum / vFrameRateDen);
        else
            *f = 0;
    }

    void GetFPS(double* fps) override
    {
        if (fps)
            *fps = vFrameRateNum / (double)vFrameRateDen;
    }
    void GetNumberOfFrame(int* n) override
    {
        if (n)
            *n = totalFrames;
    }
    void GetTotalTime(int64_t* t) override
    {
        if (t)
            *t = totalDuration / 10000;
    }
    void GetVideoSize(long* w, long* h) override
    {
        if (w)
            *w = vWidth;
        if (h)
            *h = vHeight;
    }

    tTVPBaseTexture* GetFrontBuffer() override
    {
        tTJSCriticalSectionHolder lk(frameMtx);
        if (frameCount <= 0)
            return nullptr;
        VideoFrame& f = frames[readIdx];
        tTVPBaseTexture* target = bmp[readIdx % 2];
        if (!f.rgba || !target)
            return nullptr;
        target->Update(f.rgba, f.w * 4, 0, 0, f.w, f.h);
        readIdx = (readIdx + 1) % MAX_FRAMES;
        frameCount--;
        return target;
    }

    void SetVideoBuffer(tTVPBaseTexture* b1, tTVPBaseTexture* b2, long size) override
    {
        layerMode = true;
        bmp[0] = b1;
        bmp[1] = b2;
        bmpSize = size;
    }
    void SetStopFrame(int) override {}
    void GetStopFrame(int* f) override
    {
        if (f)
            *f = 0;
    }
    void SetDefaultStopFrame() override {}
    void SetPlayRate(double) override {}
    void GetPlayRate(double* r) override
    {
        if (r)
            *r = 1.0;
    }
    void SetAudioBalance(long balance) override
    {
        audioBalance = balance;
        if (sound)
            sound->SetPan(balance / 100000.0f);
    }
    void GetAudioBalance(long* b) override
    {
        if (b)
            *b = sound ? (long)(sound->GetPan() * 100000) : audioBalance;
    }
    void SetAudioVolume(long volume) override
    {
        audioVolume = volume;
        if (sound)
            sound->SetVolume(volume / 100000.0f);
    }
    void GetAudioVolume(long* v) override
    {
        if (v)
            *v = sound ? (long)(sound->GetVolume() * 100000) : audioVolume;
    }
    void GetNumberOfAudioStream(unsigned long* c) override
    {
        if (c)
            *c = (audioStreamIdx != (DWORD)-1 && sound) ? 1 : 0;
    }
    void SelectAudioStream(unsigned long) override {}
    void GetEnableAudioStreamNum(long* n) override
    {
        if (n)
            *n = (audioStreamIdx != (DWORD)-1 && sound) ? 0 : -1;
    }
    void DisableAudioStream() override {}
    void GetNumberOfVideoStream(unsigned long* c) override
    {
        if (c)
            *c = 1;
    }
    void SelectVideoStream(unsigned long) override {}
    void GetEnableVideoStreamNum(long* n) override
    {
        if (n)
            *n = 1;
    }
    void SetLoopSegement(int, int) override {}
    void SetMixingBitmap(class tTVPBaseTexture*, float) override {}
    void ResetMixingBitmap() override {}
    void SetMixingMovieAlpha(float) override {}
    void GetMixingMovieAlpha(float* a) override
    {
        if (a)
            *a = 1.0f;
    }
    void SetMixingMovieBGColor(unsigned long) override {}
    void GetMixingMovieBGColor(unsigned long* c) override
    {
        if (c)
            *c = 0xFF000000;
    }
    void PresentVideoImage() override {}
    void GetContrastRangeMin(float* v) override
    {
        if (v)
            *v = -1.0f;
    }
    void GetContrastRangeMax(float* v) override
    {
        if (v)
            *v = 1.0f;
    }
    void GetContrastDefaultValue(float* v) override
    {
        if (v)
            *v = 0.0f;
    }
    void GetContrastStepSize(float* v) override
    {
        if (v)
            *v = 0.01f;
    }
    void GetContrast(float* v) override
    {
        if (v)
            *v = 0.0f;
    }
    void SetContrast(float) override {}
    void GetBrightnessRangeMin(float* v) override
    {
        if (v)
            *v = -1.0f;
    }
    void GetBrightnessRangeMax(float* v) override
    {
        if (v)
            *v = 1.0f;
    }
    void GetBrightnessDefaultValue(float* v) override
    {
        if (v)
            *v = 0.0f;
    }
    void GetBrightnessStepSize(float* v) override
    {
        if (v)
            *v = 0.01f;
    }
    void GetBrightness(float* v) override
    {
        if (v)
            *v = 0.0f;
    }
    void SetBrightness(float) override {}
    void GetHueRangeMin(float* v) override
    {
        if (v)
            *v = -180.0f;
    }
    void GetHueRangeMax(float* v) override
    {
        if (v)
            *v = 180.0f;
    }
    void GetHueDefaultValue(float* v) override
    {
        if (v)
            *v = 0.0f;
    }
    void GetHueStepSize(float* v) override
    {
        if (v)
            *v = 1.0f;
    }
    void GetHue(float* v) override
    {
        if (v)
            *v = 0.0f;
    }
    void SetHue(float) override {}
    void GetSaturationRangeMin(float* v) override
    {
        if (v)
            *v = -1.0f;
    }
    void GetSaturationRangeMax(float* v) override
    {
        if (v)
            *v = 1.0f;
    }
    void GetSaturationDefaultValue(float* v) override
    {
        if (v)
            *v = 0.0f;
    }
    void GetSaturationStepSize(float* v) override
    {
        if (v)
            *v = 0.01f;
    }
    void GetSaturation(float* v) override
    {
        if (v)
            *v = 0.0f;
    }
    void SetSaturation(float) override {}
};

// --- Factory functions ---
OverlayVideoPlayer* CreateOverlayVideoPlayer(TVPVideoEventCallback cb, void* cbctx) { return new WinVideoPlayer(cb, cbctx); }
LayerVideoPlayer*    CreateLayerVideoPlayer(TVPVideoEventCallback cb, void* cbctx)    { return new WinVideoPlayer(cb, cbctx); }
