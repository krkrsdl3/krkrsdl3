// TODO : windowsAPI极其难用，不想搞了
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
#include <cstring>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")

#include "tjsCommHead.h"
#include "Platform.h"
#include "PlatformVideo.h"
#include "TVPStorage.h"
#include "LayerBitmap.h"

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
static void NV12ToRGBA(const uint8_t* src, UINT32 stride, UINT32 height,
                       uint8_t* dst, UINT32 width, UINT32 dstStride)
{
    const uint8_t* y_plane = src;
    const uint8_t* uv_plane = y_plane + stride * height;
    for (UINT32 y = 0; y < height; y++) {
        const uint8_t* y_row = y_plane + y * stride;
        const uint8_t* uv_row = uv_plane + (y / 2) * stride;
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
class MFByteStream : public IMFByteStream
{
    LONG _ref = 1;
    tTJSBinaryStream* _stream;
    bool _eos = false;
public:
    MFByteStream(tTJSBinaryStream* s) : _stream(s) { _stream->SetPosition(0); }

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
        if (*len == 0)
            return E_FAIL;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetLength(QWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetCurrentPosition(QWORD* pos) override { *pos = _stream->GetPosition(); return S_OK; }
    HRESULT STDMETHODCALLTYPE SetCurrentPosition(QWORD pos) override { _stream->SetPosition((tjs_uint64)pos); _eos = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE IsEndOfStream(BOOL* eos) override
    {
        if (!_stream)
        {
            *eos = TRUE;
            return S_OK;
        }
        *eos = (_stream->GetPosition() >= _stream->GetSize()) ? TRUE : FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Read(BYTE* buf, ULONG len, ULONG* read) override
    {
        ULONG n = (ULONG)_stream->Read(buf, len);
        if (read)
            *read = n;
        if (n == 0 && len > 0)
            _eos = true;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BeginRead(BYTE*, ULONG, IMFAsyncCallback*, IUnknown*) override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE EndRead(IMFAsyncResult*, ULONG*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Write(const BYTE*, ULONG, ULONG*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE BeginWrite(const BYTE*, ULONG, IMFAsyncCallback*, IUnknown*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EndWrite(IMFAsyncResult*, ULONG*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Seek(MFBYTESTREAM_SEEK_ORIGIN origin, LONGLONG offset, DWORD, QWORD* newPos) override
    {
        tjs_uint64 pos = 0;
        if (origin == msoBegin)
        {
            pos = (tjs_uint64)offset;
        }
        else if (origin == msoCurrent)
        {
            pos = _stream->GetPosition() + offset;
            if (offset < 0 && pos < (tjs_uint64)(-offset))
                pos = 0;
        }
        else
        {
            return E_INVALIDARG;
        }
        _stream->SetPosition(pos);
        _eos = false;
        if (newPos)
            *newPos = (QWORD)pos;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Flush() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }
};

// --- WinVideoPlayer --- concrete class for both overlay and layer modes ---
class WinVideoPlayer : public OverlayVideoPlayer, public LayerVideoPlayer
{
    uint32_t ref = 1;
    IMFSourceReader* reader = nullptr;
    IMFMediaType* videoType = nullptr;
    DWORD videoStreamIdx = (DWORD)-1;
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
    WinVideoPlayer() {}
    ~WinVideoPlayer() { Close(); }

    bool OpenStream(TJS::tTJSBinaryStream* stream, const ttstr&) override
    {
        // 1. 初始化 Media Foundation
        HRESULT hr = MFStartup(MF_VERSION);
        if (FAILED(hr))
        {
            TVPConsoleLog("MFStartup failed: 0x%08X", hr);
            return false;
        }
        mfStarted = true;

        // 3. 创建字节流
        MFByteStream* bs = new (std::nothrow) MFByteStream(stream);
        if (!bs)
        {
            return false;
        }
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
                continue;
            if (FAILED(hr))
                break;
            GUID major = GUID_NULL;
            mt->GetGUID(MF_MT_MAJOR_TYPE, &major);
            if (major == MFMediaType_Video && videoStreamIdx == (DWORD)-1)
            {
                IMFMediaType* out = nullptr;
                MFCreateMediaType(&out);
                out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
                reader->SetCurrentMediaType(idx, nullptr, out);
                out->Release();
                reader->GetCurrentMediaType(idx, &videoType);
                if (videoType)
                {
                    MFGetAttributeSize(videoType, MF_MT_FRAME_SIZE, &vWidth, &vHeight);
                    UINT32 num = 0, den = 0;
                    if (SUCCEEDED(MFGetAttributeRatio(videoType, MF_MT_FRAME_RATE, &num, &den)) &&
                        num && den)
                    {
                        vFrameRateNum = num;
                        vFrameRateDen = den;
                    }
                }
                videoStreamIdx = idx;
                if (vWidth > 0 && vHeight > 0)
                    rgbaScratch.resize(vWidth * vHeight * 4);
            }
            mt->Release();
        }
        if (videoStreamIdx == (DWORD)-1)
            return false;

        // Estimate total frames
        if (vFrameRateNum && totalDuration > 0)
            totalFrames = (int)((double)totalDuration / 10000000.0 * vFrameRateNum / vFrameRateDen);

        running = true;
        decodeThread = new std::thread(&WinVideoPlayer::DecodeThreadProc, this);
        return true;
    }

    void Close()
    {
        running = false;
        playing = false;
        paused = false;
        if (decodeThread)
        {
            decodeThread->join();
            delete decodeThread;
            decodeThread = nullptr;
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
        if (mfStarted)
        {
            MFShutdown();
            mfStarted = false;
        }
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
                ended = false;
            }
            if (paused || (!playing && ended))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            if (!playing)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            DWORD flags = 0;
            LONGLONG ts = 0;
            IMFSample* sample = nullptr;
            HRESULT hr = reader->ReadSample(videoStreamIdx, 0, nullptr, &flags, &ts, &sample);
            if (hr == MF_E_END_OF_STREAM || (flags & MF_SOURCE_READERF_ENDOFSTREAM))
            {
                ended = true;
                playing = false;
                if (sample)
                    sample->Release();
                continue;
            }
            if (FAILED(hr))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            if (!sample)
                continue;

            IMFMediaBuffer* buf = nullptr;
            if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf)))
            {
                BYTE* data = nullptr;
                DWORD len = 0;
                if (SUCCEEDED(buf->Lock(&data, nullptr, &len)) && data && vWidth)
                {
                    UINT32 stride = MFGetAttributeUINT32(videoType, MF_MT_DEFAULT_STRIDE, vWidth);
                    if (!stride)
                        stride = vWidth;
                    NV12ToRGBA(data, stride, vHeight, rgbaScratch.data(), vWidth, vWidth * 4);
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
                    writeIdx = (writeIdx + 1) % MAX_FRAMES;
                    frameCount++;
                }
                buf->Release();
            }
            if (sample)
                sample->Release();
        }
    }

    void AddRef() override { ref++; }
    void Release() override
    {
        if (--ref == 0)
            delete this;
    }
    void OnContinuousCallback(tjs_uint64) override {}

    void SetWindow(class tTJSNI_Window*) override {}
    void SetMessageDrainWindow(void*) override {}
    void SetRect(int, int, int, int) override {}
    void SetVisible(bool) override {}

    void Play() override
    {
        if (ended)
        {
            seekTarget = 0;
            ended = false;
        }
        playing = true;
        paused = false;
    }
    void Stop() override
    {
        playing = false;
        paused = false;
        ended = true;
        seekTarget = 0;
    }
    void Pause() override { paused = true; }

    void SetPosition(uint64_t tick) override { seekTarget = (int64_t)tick * 10000; }
    void GetPosition(uint64_t* tick) override
    {
        if (!tick)
            return;
        tTJSCriticalSectionHolder lk(frameMtx);
        if (frameCount > 0)
        {
            int last = (writeIdx == 0) ? MAX_FRAMES - 1 : writeIdx - 1;
            *tick = frames[last].pts / 10000;
        }
        else
            *tick = 0;
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
        if (frameCount > 0 && bmp[readIdx % 2])
        {
            VideoFrame& f = frames[readIdx];
            if (f.rgba)
            {
                bmp[readIdx % 2]->Update(f.rgba, f.w * 4, 0, 0, f.w, f.h);
                return bmp[readIdx % 2];
            }
        }
        return nullptr;
    }

    void SetVideoBuffer(tTVPBaseTexture* b1, tTVPBaseTexture* b2, long size) override
    {
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
    void SetAudioBalance(long) override {}
    void GetAudioBalance(long* b) override
    {
        if (b)
            *b = 0;
    }
    void SetAudioVolume(long) override {}
    void GetAudioVolume(long* v) override
    {
        if (v)
            *v = 100;
    }
    void GetNumberOfAudioStream(unsigned long* c) override
    {
        if (c)
            *c = 0;
    }
    void SelectAudioStream(unsigned long) override {}
    void GetEnableAudioStreamNum(long* n) override
    {
        if (n)
            *n = -1;
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
OverlayVideoPlayer* CreateOverlayVideoPlayer() { return new WinVideoPlayer(); }
LayerVideoPlayer*    CreateLayerVideoPlayer()    { return new WinVideoPlayer(); }
