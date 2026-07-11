// Android video player — NDK Media APIs (MediaCodec + MediaExtractor)
// Single AndroidVideoPlayer class for both overlay and layer modes.
// Uses memfd + AMediaExtractor_setDataSourceFd (no temp file on disk).

#include "tjsCommHead.h"
#include "PlatformVideo.h"
#include "Platform.h"
#include "TVPSystem.h"
#include "TVPStorage.h"
#include "LayerBitmap.h"

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>

#include <thread>
#include <vector>
#include <atomic>
#include <cstring>
#include <unistd.h>
#include <sys/mman.h>
#include <android/sharedmem.h>

// ============================================================
// NV12 / YUV420 Semi-Planar -> RGBA
// ============================================================
static void NV12ToRGBA(const uint8_t* src, int srcStride, int srcSliceHeight,
                       uint8_t* dst, int width, int height)
{
    const uint8_t* y_plane = src;
    const uint8_t* uv_plane = src + srcStride * srcSliceHeight;
    for (int y = 0; y < height; y++) {
        const uint8_t* y_row = y_plane + y * srcStride;
        const uint8_t* uv_row = uv_plane + (y / 2) * srcStride;
        uint8_t* dst_row = dst + y * width * 4;
        for (int x = 0; x < width; x++) {
            int Y = y_row[x];
            int U = uv_row[x / 2 * 2] - 128;
            int V = uv_row[x / 2 * 2 + 1] - 128;
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

// ============================================================
// AndroidVideoPlayer — single class for overlay & layer modes
// ============================================================
class AndroidVideoPlayer : public OverlayVideoPlayer, public LayerVideoPlayer
{
    uint32_t ref = 1;

    // Stream data in memory (backed by ashmem)
    int ashmemFd = -1;
    uint8_t* streamData = nullptr;
    size_t streamSize = 0;
    int extractorFd = -1; // dup'd fd for extractor

    // NDK media
    AMediaExtractor* extractor = nullptr;
    AMediaCodec* vcodec = nullptr;
    AMediaCodec* acodec = nullptr;
    int videoTrackIdx = -1;
    int audioTrackIdx = -1;
    int vWidth = 0, vHeight = 0;
    int aSampleRate = 0, aChannels = 0;

    // Playback state
    std::atomic<bool> playing{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> ended{false};
    std::atomic<int64_t> seekTargetUs{-1}; // seek target in microseconds

    // Decode thread & frame ring buffer
    std::thread* decodeThread = nullptr;
    std::atomic<bool> running{false};

    static const int MAX_FRAMES = 3;
    struct VideoFrame { uint8_t* rgba = nullptr; int w = 0, h = 0; int64_t pts = 0; };
    VideoFrame frames[MAX_FRAMES];
    int writeIdx = 0, readIdx = 0, frameCount = 0;
    tTJSCriticalSection frameMtx;

    // Layer output buffers
    tTVPBaseTexture* bmp[2] = {};
    long bmpSize = 0;

    std::vector<uint8_t> rgbaScratch;

public:
    AndroidVideoPlayer() {}
    ~AndroidVideoPlayer() { Close(); }

    // ── OpenStream ──────────────────────────────────────────
    bool OpenStream(TJS::tTJSBinaryStream* stream, const ttstr& streamname) override
    {
        // Read entire stream into memory
        streamSize = (size_t)stream->GetSize();
        if (streamSize == 0) return false;
        streamData = new uint8_t[streamSize];
        stream->SetPosition(0);
        size_t total = 0;
        while (total < streamSize) {
            tjs_uint n = stream->Read(streamData + total, (tjs_uint)(streamSize - total));
            if (n <= 0) break;
            total += n;
        }
        if (total != streamSize) { delete[] streamData; streamData = nullptr; return false; }

        // Create a shared memory fd (no temp file on disk)
        ttstr ext = TVPExtractStorageExt(streamname);
        ext.ToLowerCase();
        std::string name = "krkr_video";
        if (!ext.IsEmpty()) name += "." + ext.AsStdString();
        ashmemFd = ASharedMemory_create(name.c_str(), streamSize);
        if (ashmemFd < 0) { delete[] streamData; streamData = nullptr; return false; }
        void* ptr = mmap(nullptr, streamSize, PROT_READ | PROT_WRITE, MAP_SHARED, ashmemFd, 0);
        if (ptr == MAP_FAILED) { close(ashmemFd); ashmemFd = -1; delete[] streamData; streamData = nullptr; return false; }
        memcpy(ptr, streamData, streamSize);
        munmap(ptr, streamSize);
        int fd2 = dup(ashmemFd);
        if (fd2 < 0) { Close(); return false; }
        extractorFd = fd2;

        // Create extractor from the fd
        extractor = AMediaExtractor_new();
        media_status_t err = AMediaExtractor_setDataSourceFd(extractor, fd2, 0, streamSize);
        if (err != AMEDIA_OK) {
            TVPConsoleLog("AMediaExtractor_setDataSourceFd failed");
            Close(); return false;
        }

        // Enumerate tracks
        int numTracks = AMediaExtractor_getTrackCount(extractor);
        for (int i = 0; i < numTracks; i++) {
            AMediaFormat* fmt = AMediaExtractor_getTrackFormat(extractor, i);
            if (!fmt) continue;
            const char* mime = nullptr;
            AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &mime);
            if (!mime) { AMediaFormat_delete(fmt); continue; }

            if (strncmp(mime, "video/", 6) == 0 && videoTrackIdx < 0) {
                AMediaCodec* codec = AMediaCodec_createDecoderByType(mime);
                if (codec) {
                    AMediaCodec_configure(codec, fmt, nullptr, nullptr, 0);
                    AMediaCodec_start(codec);
                    vcodec = codec;
                    videoTrackIdx = i;
                    AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, &vWidth);
                    AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, &vHeight);
                    if (vWidth > 0 && vHeight > 0)
                        rgbaScratch.resize(vWidth * vHeight * 4);
                }
            } else if (strncmp(mime, "audio/", 6) == 0 && audioTrackIdx < 0) {
                AMediaCodec* codec = AMediaCodec_createDecoderByType(mime);
                if (codec) {
                    AMediaCodec_configure(codec, fmt, nullptr, nullptr, 0);
                    AMediaCodec_start(codec);
                    acodec = codec;
                    audioTrackIdx = i;
                    AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, &aSampleRate);
                    AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &aChannels);
                }
            }
            AMediaFormat_delete(fmt);
        }

        if (videoTrackIdx < 0) {
            TVPConsoleLog("No video track found");
            Close(); return false;
        }

        AMediaExtractor_selectTrack(extractor, videoTrackIdx);
        running = true;
        decodeThread = new std::thread(&AndroidVideoPlayer::DecodeLoop, this);
        return true;
    }

    void Close()
    {
        running = false; playing = false; paused = false;
        if (decodeThread) { decodeThread->join(); delete decodeThread; decodeThread = nullptr; }
        { tTJSCriticalSectionHolder lk(frameMtx);
          for (int i = 0; i < MAX_FRAMES; i++) { delete[] frames[i].rgba; frames[i].rgba = nullptr; }
          frameCount = 0; }

        if (vcodec) { AMediaCodec_stop(vcodec); AMediaCodec_delete(vcodec); vcodec = nullptr; }
        if (acodec) { AMediaCodec_stop(acodec); AMediaCodec_delete(acodec); acodec = nullptr; }
        if (extractor) { AMediaExtractor_delete(extractor); extractor = nullptr; }
        if (extractorFd >= 0) { close(extractorFd); extractorFd = -1; }
        if (ashmemFd >= 0) { close(ashmemFd); ashmemFd = -1; }
        delete[] streamData; streamData = nullptr;
    }

    // ── Decode loop ─────────────────────────────────────────
    void DecodeLoop()
    {
        if (!extractor) return;
        int vW = vWidth, vH = vHeight;
        std::vector<uint8_t> rgbaBuf;
        if (vW > 0 && vH > 0) rgbaBuf.resize(vW * vH * 4);

        AMediaCodecBufferInfo vInfo = {};
        bool inputDone = false, outputDone = false;
        bool vDecodeDone = false, aDecodeDone = false;

        while (running && !outputDone) {
            // Handle seek
            int64_t seekUs = seekTargetUs.exchange(-1);
            if (seekUs >= 0) {
                AMediaExtractor_seekTo(extractor, seekUs, AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC);
                inputDone = false; vDecodeDone = false; aDecodeDone = false; outputDone = false;
                { tTJSCriticalSectionHolder lk(frameMtx); frameCount = 0; }
                ended = false;
                // Flush codecs
                if (vcodec) AMediaCodec_flush(vcodec);
                if (acodec) AMediaCodec_flush(acodec);
                continue;
            }

            if (paused || (!playing && ended)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            if (!playing) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            // --- Feed input ---
            if (!inputDone) {
                ssize_t sampleIdx = AMediaExtractor_getSampleTrackIndex(extractor);
                if (sampleIdx < 0) {
                    if (vcodec) AMediaCodec_queueInputBuffer(vcodec, -1, 0, 0,
                        AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM, 0);
                    if (acodec) AMediaCodec_queueInputBuffer(acodec, -1, 0, 0,
                        AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM, 0);
                    inputDone = true;
                } else {
                    size_t sampleSize = AMediaExtractor_getSampleSize(extractor);
                    int64_t sampleTime = AMediaExtractor_getSampleTime(extractor);
                    if (sampleIdx == videoTrackIdx && vcodec) {
                        ssize_t idx = AMediaCodec_dequeueInputBuffer(vcodec, 10000);
                        if (idx >= 0) {
                            size_t bufSize = 0;
                            uint8_t* buf = AMediaCodec_getInputBuffer(vcodec, idx, &bufSize);
                            if (buf && bufSize >= sampleSize) {
                                ssize_t n = AMediaExtractor_readSampleData(extractor, buf, sampleSize);
                                AMediaCodec_queueInputBuffer(vcodec, idx, 0, n, sampleTime, 0);
                            }
                        }
                    } else if (sampleIdx == audioTrackIdx && acodec) {
                        ssize_t idx = AMediaCodec_dequeueInputBuffer(acodec, 10000);
                        if (idx >= 0) {
                            size_t bufSize = 0;
                            uint8_t* buf = AMediaCodec_getInputBuffer(acodec, idx, &bufSize);
                            if (buf && bufSize >= sampleSize) {
                                ssize_t n = AMediaExtractor_readSampleData(extractor, buf, sampleSize);
                                AMediaCodec_queueInputBuffer(acodec, idx, 0, n, sampleTime, 0);
                            }
                        }
                    }
                    AMediaExtractor_advance(extractor);
                }
            }

            // --- Drain video ---
            if (vcodec && !vDecodeDone) {
                ssize_t vOutIdx = AMediaCodec_dequeueOutputBuffer(vcodec, &vInfo, 10000);
                if (vOutIdx >= 0) {
                    if (vInfo.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
                        vDecodeDone = true;
                    } else if (vInfo.size > 0) {
                        size_t bufSize = 0;
                        uint8_t* buf = AMediaCodec_getOutputBuffer(vcodec, vOutIdx, &bufSize);
                        if (buf && vW > 0 && vH > 0) {
                            AMediaFormat* fmt = AMediaCodec_getOutputFormat(vcodec);
                            int32_t stride = 0, sliceH = 0;
                            if (fmt) {
                                AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_STRIDE, &stride);
                                AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_SLICE_HEIGHT, &sliceH);
                                AMediaFormat_delete(fmt);
                            }
                            if (stride <= 0) stride = vW;
                            if (sliceH <= 0) sliceH = vH;
                            NV12ToRGBA(buf, stride, sliceH, rgbaBuf.data(), vW, vH);

                            { tTJSCriticalSectionHolder lk(frameMtx);
                              if (frameCount >= MAX_FRAMES) { readIdx = (readIdx + 1) % MAX_FRAMES; frameCount--; }
                              VideoFrame& f = frames[writeIdx];
                              if (!f.rgba) f.rgba = new uint8_t[vW * vH * 4];
                              memcpy(f.rgba, rgbaBuf.data(), vW * vH * 4);
                              f.w = vW; f.h = vH; f.pts = vInfo.presentationTimeUs;
                              writeIdx = (writeIdx + 1) % MAX_FRAMES; frameCount++; }
                        }
                    }
                    AMediaCodec_releaseOutputBuffer(vcodec, vOutIdx, false);
                } else if (vOutIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                    AMediaFormat* fmt = AMediaCodec_getOutputFormat(vcodec);
                    if (fmt) {
                        AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, &vW);
                        AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, &vH);
                        rgbaBuf.resize(vW * vH * 4);
                        AMediaFormat_delete(fmt);
                    }
                }
            }

            // --- Drain audio (skip) ---
            if (acodec && !aDecodeDone) {
                ssize_t aOutIdx = AMediaCodec_dequeueOutputBuffer(acodec, &vInfo, 10000);
                if (aOutIdx >= 0) {
                    if (vInfo.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) aDecodeDone = true;
                    AMediaCodec_releaseOutputBuffer(acodec, aOutIdx, false);
                }
            }

            if (inputDone && vDecodeDone && aDecodeDone) outputDone = true;
            if (!outputDone) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // Mark end
        playing = false;
        { tTJSCriticalSectionHolder lk(frameMtx); if (frameCount > 0) { /* keep last frame */ } }
    }

    // ── iTVPVideoOverlay ────────────────────────────────────
    void AddRef() override { ref++; }
    void Release() override { if (--ref == 0) delete this; }

    void OnContinuousCallback(tjs_uint64) override {}

    void SetWindow(class tTJSNI_Window*) override {}
    void SetMessageDrainWindow(void*) override {}
    void SetRect(int, int, int, int) override {}
    void SetVisible(bool) override {}

    void Play() override
    {
        if (ended) { seekTargetUs = 0; ended = false; }
        playing = true; paused = false;
    }

    void Stop() override
    {
        playing = false; paused = false; ended = true;
        seekTargetUs = 0;
    }

    void Pause() override { paused = true; }

    void SetPosition(uint64_t tick) override
    {
        // tick in ms -> us
        seekTargetUs = (int64_t)tick * 1000;
    }

    void GetPosition(uint64_t* tick) override
    {
        if (!tick) return;
        tTJSCriticalSectionHolder lk(frameMtx);
        if (frameCount > 0) {
            int last = (writeIdx == 0) ? MAX_FRAMES - 1 : writeIdx - 1;
            *tick = frames[last].pts / 1000; // us -> ms
        } else *tick = 0;
    }

    void GetStatus(tTVPVideoStatus* s) override
    {
        if (!s) return;
        if (ended)        *s = vsEnded;
        else if (paused)  *s = vsPaused;
        else if (playing) *s = vsPlaying;
        else              *s = vsStopped;
    }

    void Rewind() override { seekTargetUs = 0; ended = false; }

    void SetFrame(int) override {} // frame-accurate seek not implemented yet
    void GetFrame(int* f) override { if (f) { uint64_t pos = 0; GetPosition(&pos); *f = (int)(pos / 33); } }
    void GetFPS(double* fps) override { if (fps) *fps = 30.0; }
    void GetNumberOfFrame(int* n) override { if (n) *n = 0; }
    void GetTotalTime(int64_t* t) override { if (t) *t = 0; }

    void GetVideoSize(long* w, long* h) override { if (w) *w = vWidth; if (h) *h = vHeight; }

    tTVPBaseTexture* GetFrontBuffer() override
    {
        tTJSCriticalSectionHolder lk(frameMtx);
        if (frameCount > 0 && bmp[readIdx % 2]) {
            VideoFrame& f = frames[readIdx];
            if (f.rgba) {
                bmp[readIdx % 2]->Update(f.rgba, f.w * 4, 0, 0, f.w, f.h);
                return bmp[readIdx % 2];
            }
        }
        return nullptr;
    }

    void SetVideoBuffer(tTVPBaseTexture* b1, tTVPBaseTexture* b2, long size) override
    {
        bmp[0] = b1; bmp[1] = b2; bmpSize = size;
    }

    void SetStopFrame(int) override {} void GetStopFrame(int* f) override { if (f) *f = 0; } void SetDefaultStopFrame() override {}
    void SetPlayRate(double) override {} void GetPlayRate(double* r) override { if (r) *r = 1.0; }
    void SetAudioBalance(long) override {} void GetAudioBalance(long* b) override { if (b) *b = 0; }
    void SetAudioVolume(long) override {} void GetAudioVolume(long* v) override { if (v) *v = 100; }
    void GetNumberOfAudioStream(unsigned long* c) override { if (c) *c = (audioTrackIdx >= 0) ? 1 : 0; }
    void SelectAudioStream(unsigned long) override {}
    void GetEnableAudioStreamNum(long* n) override { if (n) *n = (audioTrackIdx >= 0) ? 0 : -1; }
    void DisableAudioStream() override {}
    void GetNumberOfVideoStream(unsigned long* c) override { if (c) *c = 1; }
    void SelectVideoStream(unsigned long) override {}
    void GetEnableVideoStreamNum(long* n) override { if (n) *n = 1; }
    void SetLoopSegement(int, int) override {}
    void SetMixingBitmap(class tTVPBaseTexture*, float) override {} void ResetMixingBitmap() override {}
    void SetMixingMovieAlpha(float) override {} void GetMixingMovieAlpha(float* a) override { if (a) *a = 1.0f; }
    void SetMixingMovieBGColor(unsigned long) override {} void GetMixingMovieBGColor(unsigned long* c) override { if (c) *c = 0xFF000000; }
    void PresentVideoImage() override {}
    void GetContrastRangeMin(float* v) override { if (v) *v = -1.0f; } void GetContrastRangeMax(float* v) override { if (v) *v = 1.0f; }
    void GetContrastDefaultValue(float* v) override { if (v) *v = 0.0f; } void GetContrastStepSize(float* v) override { if (v) *v = 0.01f; }
    void GetContrast(float* v) override { if (v) *v = 0.0f; } void SetContrast(float) override {}
    void GetBrightnessRangeMin(float* v) override { if (v) *v = -1.0f; } void GetBrightnessRangeMax(float* v) override { if (v) *v = 1.0f; }
    void GetBrightnessDefaultValue(float* v) override { if (v) *v = 0.0f; } void GetBrightnessStepSize(float* v) override { if (v) *v = 0.01f; }
    void GetBrightness(float* v) override { if (v) *v = 0.0f; } void SetBrightness(float) override {}
    void GetHueRangeMin(float* v) override { if (v) *v = -180.0f; } void GetHueRangeMax(float* v) override { if (v) *v = 180.0f; }
    void GetHueDefaultValue(float* v) override { if (v) *v = 0.0f; } void GetHueStepSize(float* v) override { if (v) *v = 1.0f; }
    void GetHue(float* v) override { if (v) *v = 0.0f; } void SetHue(float) override {}
    void GetSaturationRangeMin(float* v) override { if (v) *v = -1.0f; } void GetSaturationRangeMax(float* v) override { if (v) *v = 1.0f; }
    void GetSaturationDefaultValue(float* v) override { if (v) *v = 0.0f; } void GetSaturationStepSize(float* v) override { if (v) *v = 0.01f; }
    void GetSaturation(float* v) override { if (v) *v = 0.0f; } void SetSaturation(float) override {}
};

// ============================================================
// Factory functions
// ============================================================
OverlayVideoPlayer* CreateOverlayVideoPlayer() { return new AndroidVideoPlayer(); }
LayerVideoPlayer*    CreateLayerVideoPlayer()    { return new AndroidVideoPlayer(); }
