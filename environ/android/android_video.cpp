// Android video player — NDK Media APIs (MediaCodec + MediaExtractor)
// Single AndroidVideoPlayer class for both overlay and layer modes.
// Uses memfd + AMediaExtractor_setDataSourceFd (no temp file on disk).

#include "tjsCommHead.h"
#include "PlatformVideo.h"
#include "Platform.h"
#include "PlatformMutex.h"
#include "TVPSystem.h"
#include "TVPStorage.h"
#include "LayerBitmap.h"
#include "TVPEvent.h"
#include "TVPCompositor.h"

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaDataSource.h>

#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <unistd.h>

// ============================================================
// YUV420 -> RGBA
// MediaCodec 的 COLOR_FormatYUV420Flexible 在不同设备上可能是
// 平面（I420，color=19/20）或半平面（NV12，color=21）；
// 必须按输出格式的 color-format 分派，不能一律当 NV12。
// ============================================================
static void YuvToRGBA(const uint8_t* src, int srcStride, int srcSliceHeight, int colorFormat,
                      uint8_t* dst, int width, int height)
{
    const bool semiPlanar = (colorFormat == 21); // COLOR_FormatYUV420SemiPlanar
    const bool nv21 = (colorFormat == 39);       // COLOR_FormatYUV420PackedSemiPlanar
    const uint8_t* y_plane = src;
    const uint8_t* u_plane;
    const uint8_t* v_plane;
    int chromaStride;
    if (semiPlanar || nv21) {
        u_plane = src + srcStride * srcSliceHeight;
        v_plane = u_plane;
        chromaStride = srcStride;
    } else {
        // I420: U 平面后紧跟 V 平面，色度行列都是亮度的一半
        u_plane = src + srcStride * srcSliceHeight;
        chromaStride = srcStride / 2;
        v_plane = u_plane + chromaStride * (srcSliceHeight / 2);
    }
    for (int y = 0; y < height; y++) {
        const uint8_t* y_row = y_plane + y * srcStride;
        const uint8_t* u_row = u_plane + (y / 2) * chromaStride;
        const uint8_t* v_row = v_plane + (y / 2) * chromaStride;
        uint8_t* dst_row = dst + y * width * 4;
        for (int x = 0; x < width; x++) {
            int Y = y_row[x];
            int uIndex, vIndex;
            if (semiPlanar) {
                uIndex = (x / 2) * 2;
                vIndex = uIndex + 1;
            } else if (nv21) {
                vIndex = (x / 2) * 2;
                uIndex = vIndex + 1;
            } else {
                uIndex = vIndex = x / 2;
            }
            int U = u_row[uIndex] - 128;
            int V = v_row[vIndex] - 128;
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
class AndroidVideoPlayer : public OverlayVideoPlayer, public LayerVideoPlayer,
                           public tTVPContinuousEventCallbackIntf
{
    uint32_t ref = 1;

    // 整个视频流读进内存，经 AMediaDataSource 回调喂给 MediaExtractor
    // （ashmem/普通 fd 不保证可 seek，setDataSourceFd 在部分设备上直接失败）
    uint8_t* streamData = nullptr;
    size_t streamSize = 0;
    AMediaDataSource* dataSource = nullptr;

    // NDK media
    AMediaExtractor* extractor = nullptr;
    AMediaCodec* vcodec = nullptr;
    int videoTrackIdx = -1;
    int vWidth = 0, vHeight = 0;

    // 音频：独立 extractor/codec（同一份内存数据），避免顺序出样拖慢音频
    AMediaExtractor* aextractor = nullptr;
    AMediaDataSource* aDataSource = nullptr;
    AMediaCodec* acodec = nullptr;
    int audioTrackIdx = -1;
    std::atomic<int64_t> seekAudioUs{-1};
    iTVPSoundBuffer* sound = nullptr;
    long audioVolume = 100000;
    long audioBalance = 0;
    bool aInputDone = false, aEosQueued = false, aDecodeDone = false;
    int aSampleRate = 0, aChannels = 0;

    // Playback state
    std::atomic<bool> playing{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> ended{false};
    std::atomic<int64_t> seekTargetUs{-1}; // seek target in microseconds

    // Decode thread & frame ring buffer
    std::thread* decodeThread = nullptr;
    std::atomic<bool> running{false};

    // 事件回调与播放节奏
    TVPVideoEventCallback cb = nullptr;
    void* cbctx = nullptr;
    std::chrono::steady_clock::time_point paceWall;
    int64_t pacePtsUs = -1;
    double vFrameRate = 30.0;
    int64_t durationUs = 0;
    int64_t lastPtsUs = 0; // 最近写入帧的 pts（微秒），环空时 GetPosition 仍可用
    std::atomic<int> pendingFrame{0};
    std::atomic<bool> notifyEnabled{true};
    bool continuousHook = false;

    // overlay / mixer 模式：把帧画到合成器的 overlay sprite 上（与 FFmpeg 后端一致）
    bool layerMode = false;
    bool overlayVisible = false;
    TVPSprite* overlaySprite = nullptr;

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
    AndroidVideoPlayer(TVPVideoEventCallback callback, void* ctx) : cb(callback), cbctx(ctx) {}
    ~AndroidVideoPlayer() { Close(); }

    void PostEnded()
    {
        if (cb)
            cb(cbctx, WM_GRAPHNOTIFY, EC_COMPLETE, 0);
    }

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

    // 按 pts（微秒）控制帧输出节奏；暂停期间不推进时间基准
    void PaceTo(int64_t ptsUs)
    {
        auto now = std::chrono::steady_clock::now();
        if (pacePtsUs < 0 || ptsUs < pacePtsUs)
        {
            pacePtsUs = ptsUs;
            paceWall = now;
            return;
        }
        auto due = paceWall + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                 std::chrono::nanoseconds((ptsUs - pacePtsUs) * 1000));
        while (running)
        {
            if (seekTargetUs >= 0)
                return;
            if (paused || !playing)
            {
                pacePtsUs = ptsUs;
                paceWall = std::chrono::steady_clock::now();
                return;
            }
            now = std::chrono::steady_clock::now();
            if (now >= due)
                break;
            PumpAudio(); // 视频等待期间持续补音频，避免欠载
            auto left = due - now;
            std::this_thread::sleep_for(std::min<decltype(left)>(
                left, std::chrono::milliseconds(5)));
        }
        if (std::chrono::steady_clock::now() - due > std::chrono::seconds(1))
            due = std::chrono::steady_clock::now();
        pacePtsUs = ptsUs;
        paceWall = due;
    }

    // ── AMediaDataSource 回调（可能来自任意线程；streamData 打开后只读）──
    static ssize_t DataSourceReadAt(void* userdata, off64_t offset, void* buffer, size_t size)
    {
        AndroidVideoPlayer* self = static_cast<AndroidVideoPlayer*>(userdata);
        if (!self || !self->streamData || offset < 0 || (size_t)offset >= self->streamSize)
            return 0; // EOF（部分设备把 -1 当 I/O 错误，用 0 更稳）
        size_t avail = self->streamSize - (size_t)offset;
        size_t n = size < avail ? size : avail;
        if (n)
            memcpy(buffer, self->streamData + (size_t)offset, n);
        return (ssize_t)n;
    }
    static ssize_t DataSourceGetSize(void* userdata)
    {
        AndroidVideoPlayer* self = static_cast<AndroidVideoPlayer*>(userdata);
        return self ? (ssize_t)self->streamSize : -1;
    }
    static void DataSourceClose(void*) {}

    // ── 音频：独立 extractor + codec + sound buffer ─────────
    void CreateSound(int rate, int channels)
    {
        if (sound)
        {
            sound->Stop();
            delete sound;
            sound = nullptr;
        }
        if (rate <= 0 || channels <= 0)
            return;
        aSampleRate = rate;
        aChannels = channels;
        tTVPWaveFormat wf = {};
        wf.SamplesPerSec = rate;
        wf.Channels = channels;
        wf.BitsPerSample = 16;
        wf.BytesPerSample = 2;
        wf.IsFloat = false;
        wf.Seekable = false;
        TVPInitDirectSound();
        sound = TVPCreateSoundBuffer(wf, 32);
        if (sound)
        {
            sound->SetVolume(audioVolume / 100000.0f);
            sound->SetPan(audioBalance / 100000.0f);
            TVPConsoleLog("audio: %d Hz %d ch", rate, channels);
        }
        else
            TVPConsoleLog("audio: TVPCreateSoundBuffer failed");
    }

    void CloseAudio()
    {
        if (acodec) { AMediaCodec_stop(acodec); AMediaCodec_delete(acodec); acodec = nullptr; }
        if (aextractor) { AMediaExtractor_delete(aextractor); aextractor = nullptr; }
        if (aDataSource) { AMediaDataSource_delete(aDataSource); aDataSource = nullptr; }
        if (sound) { sound->Stop(); delete sound; sound = nullptr; }
    }

    void SetupAudio()
    {
        if (audioTrackIdx < 0)
            return;
        AMediaFormat* fmt = AMediaExtractor_getTrackFormat(extractor, audioTrackIdx);
        if (!fmt)
            return;
        const char* mime = nullptr;
        AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &mime);
        int32_t rate = 0, channels = 0;
        AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, &rate);
        AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channels);
        if (!mime || rate <= 0 || channels <= 0)
        {
            AMediaFormat_delete(fmt);
            return;
        }

        aextractor = AMediaExtractor_new();
        aDataSource = AMediaDataSource_new();
        if (!aextractor || !aDataSource)
        {
            AMediaFormat_delete(fmt);
            CloseAudio();
            return;
        }
        AMediaDataSource_setUserdata(aDataSource, this);
        AMediaDataSource_setReadAt(aDataSource, &AndroidVideoPlayer::DataSourceReadAt);
        AMediaDataSource_setGetSize(aDataSource, &AndroidVideoPlayer::DataSourceGetSize);
        AMediaDataSource_setClose(aDataSource, &AndroidVideoPlayer::DataSourceClose);
        if (AMediaExtractor_setDataSourceCustom(aextractor, aDataSource) != AMEDIA_OK ||
            AMediaExtractor_selectTrack(aextractor, audioTrackIdx) != AMEDIA_OK)
        {
            AMediaFormat_delete(fmt);
            CloseAudio();
            return;
        }

        acodec = AMediaCodec_createDecoderByType(mime);
        if (!acodec || AMediaCodec_configure(acodec, fmt, nullptr, nullptr, 0) != AMEDIA_OK ||
            AMediaCodec_start(acodec) != AMEDIA_OK)
        {
            AMediaFormat_delete(fmt);
            CloseAudio();
            return;
        }
        CreateSound(rate, channels);
        AMediaFormat_delete(fmt);
        if (!sound)
            CloseAudio();
    }

    // 由视频解码线程的等待间隙泵送：喂 AAC、取 PCM、追加到 sound buffer
    void PumpAudio()
    {
        if (!acodec || !aextractor || !sound)
            return;

        int64_t sk = seekAudioUs.exchange(-1);
        if (sk >= 0)
        {
            AMediaExtractor_seekTo(aextractor, sk, AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC);
            AMediaCodec_flush(acodec);
            aInputDone = false;
            aEosQueued = false;
            aDecodeDone = false;
            sound->Reset();
        }

        if (!running || !playing || paused)
            return;

        for (int i = 0; i < 4; ++i)
        {
            if (!aDecodeDone && !sound->IsBufferValid())
                break;

            if (!aInputDone)
            {
                ssize_t si = AMediaExtractor_getSampleTrackIndex(aextractor);
                if (si < 0)
                {
                    aInputDone = true;
                }
                else
                {
                    if (si == audioTrackIdx)
                    {
                        size_t ss = (size_t)AMediaExtractor_getSampleSize(aextractor);
                        int64_t st = AMediaExtractor_getSampleTime(aextractor);
                        ssize_t idx = AMediaCodec_dequeueInputBuffer(acodec, 0);
                        if (idx < 0)
                            break; // 输入缓冲暂无，下次再喂
                        size_t bs = 0;
                        uint8_t* b = AMediaCodec_getInputBuffer(acodec, idx, &bs);
                        if (b && bs >= ss)
                        {
                            ssize_t n = AMediaExtractor_readSampleData(aextractor, b, ss);
                            AMediaCodec_queueInputBuffer(acodec, idx, 0, n, st, 0);
                        }
                        else
                            AMediaCodec_queueInputBuffer(acodec, idx, 0, 0, st, 0);
                    }
                    AMediaExtractor_advance(aextractor);
                }
            }

            if (aInputDone && !aEosQueued)
            {
                ssize_t idx = AMediaCodec_dequeueInputBuffer(acodec, 0);
                if (idx >= 0)
                {
                    AMediaCodec_queueInputBuffer(acodec, idx, 0, 0, 0,
                                                 AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                    aEosQueued = true;
                }
            }

            AMediaCodecBufferInfo ai = {};
            ssize_t oi = AMediaCodec_dequeueOutputBuffer(acodec, &ai, 0);
            if (oi >= 0)
            {
                if (ai.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM)
                    aDecodeDone = true;
                else if (ai.size > 0)
                {
                    size_t bs = 0;
                    uint8_t* b = AMediaCodec_getOutputBuffer(acodec, oi, &bs);
                    if (b && sound)
                        sound->AppendBuffer(b + ai.offset, ai.size);
                }
                AMediaCodec_releaseOutputBuffer(acodec, oi, false);
            }
            else if (oi == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED)
            {
                AMediaFormat* of = AMediaCodec_getOutputFormat(acodec);
                if (of)
                {
                    int32_t r = 0, c = 0;
                    AMediaFormat_getInt32(of, AMEDIAFORMAT_KEY_SAMPLE_RATE, &r);
                    AMediaFormat_getInt32(of, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &c);
                    AMediaFormat_delete(of);
                    if (r > 0 && c > 0 && (r != aSampleRate || c != aChannels))
                        CreateSound(r, c);
                }
            }
            else
                break;
        }
        if (!sound->IsPlaying())
            sound->Play();
    }

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

        // 用自定义数据源（API 28+）把内存缓冲交给 MediaExtractor
        dataSource = AMediaDataSource_new();
        if (!dataSource) { delete[] streamData; streamData = nullptr; return false; }
        AMediaDataSource_setUserdata(dataSource, this);
        AMediaDataSource_setReadAt(dataSource, &AndroidVideoPlayer::DataSourceReadAt);
        AMediaDataSource_setGetSize(dataSource, &AndroidVideoPlayer::DataSourceGetSize);
        AMediaDataSource_setClose(dataSource, &AndroidVideoPlayer::DataSourceClose);

        // Create extractor from the data source
        extractor = AMediaExtractor_new();
        media_status_t err = AMediaExtractor_setDataSourceCustom(extractor, dataSource);
        if (err != AMEDIA_OK) {
            TVPConsoleLog("AMediaExtractor_setDataSourceCustom failed: %d", (int)err);
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
                    int64_t durUs = 0;
                    if (AMediaFormat_getInt64(fmt, AMEDIAFORMAT_KEY_DURATION, &durUs))
                        durationUs = durUs;
                    int32_t fpsInt = 0;
                    float fpsFloat = 0;
                    if (AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_FRAME_RATE, &fpsInt) && fpsInt > 0)
                        vFrameRate = fpsInt;
                    else if (AMediaFormat_getFloat(fmt, AMEDIAFORMAT_KEY_FRAME_RATE, &fpsFloat) &&
                             fpsFloat > 0.1f)
                        vFrameRate = fpsFloat;
                    if (vWidth > 0 && vHeight > 0)
                        rgbaScratch.resize(vWidth * vHeight * 4);
                }
            } else if (strncmp(mime, "audio/", 6) == 0 && audioTrackIdx < 0) {
                audioTrackIdx = i;
            }
            AMediaFormat_delete(fmt);
        }

        if (videoTrackIdx < 0) {
            TVPConsoleLog("No video track found");
            Close(); return false;
        }

        AMediaExtractor_selectTrack(extractor, videoTrackIdx);
        SetupAudio();
        running = true;
        decodeThread = new std::thread(&AndroidVideoPlayer::DecodeLoop, this);
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
        running = false; playing = false; paused = false;
        if (decodeThread) { decodeThread->join(); delete decodeThread; decodeThread = nullptr; }
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
        { tTJSCriticalSectionHolder lk(frameMtx);
          for (int i = 0; i < MAX_FRAMES; i++) { delete[] frames[i].rgba; frames[i].rgba = nullptr; }
          frameCount = 0; }

        if (vcodec) { AMediaCodec_stop(vcodec); AMediaCodec_delete(vcodec); vcodec = nullptr; }
        CloseAudio();
        if (extractor) { AMediaExtractor_delete(extractor); extractor = nullptr; }
        if (dataSource) { AMediaDataSource_delete(dataSource); dataSource = nullptr; }
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
        bool vDecodeDone = false, vEosQueued = false;

        while (running) {
            // Handle seek
            int64_t seekUs = seekTargetUs.exchange(-1);
            if (seekUs >= 0) {
                AMediaExtractor_seekTo(extractor, seekUs, AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC);
                seekAudioUs = seekUs;
                inputDone = false; vDecodeDone = false; vEosQueued = false; outputDone = false;
                { tTJSCriticalSectionHolder lk(frameMtx); frameCount = 0; }
                ended = false;
                pacePtsUs = -1;
                paceWall = std::chrono::steady_clock::now();
                // Flush codecs
                if (vcodec) AMediaCodec_flush(vcodec);
                continue;
            }

            if (paused || (!playing && ended)) {
                pacePtsUs = -1; // 暂停/停止期间不走时，恢复后不快进
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            if (!playing) {
                pacePtsUs = -1;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            PumpAudio();

            // --- Feed input ---
            if (!inputDone) {
                ssize_t sampleIdx = AMediaExtractor_getSampleTrackIndex(extractor);
                if (sampleIdx < 0) {
                    inputDone = true; // EOS 标记在下面用真实输入缓冲投递
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
                    }
                    AMediaExtractor_advance(extractor);
                }
            }

            // 投递 EOS：必须用 dequeue 到的真实输入缓冲（index=-1 在部分设备无效）
            if (inputDone && !vEosQueued && vcodec) {
                ssize_t idx = AMediaCodec_dequeueInputBuffer(vcodec, 0);
                if (idx >= 0) {
                    AMediaCodec_queueInputBuffer(vcodec, idx, 0, 0, 0,
                                                 AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                    vEosQueued = true;
                }
            }

            // --- Drain video ---
            if (vcodec && !vDecodeDone) {
                ssize_t vOutIdx = AMediaCodec_dequeueOutputBuffer(vcodec, &vInfo, 10000);
                if (vOutIdx >= 0) {
                    if (vInfo.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
                        vDecodeDone = true;
                    } else if (vInfo.size > 0) {
                        PaceTo(vInfo.presentationTimeUs);
                        if (running && seekTargetUs < 0) {
                            size_t bufSize = 0;
                            uint8_t* buf = AMediaCodec_getOutputBuffer(vcodec, vOutIdx, &bufSize);
                            if (buf && vW > 0 && vH > 0) {
                                AMediaFormat* fmt = AMediaCodec_getOutputFormat(vcodec);
                                int32_t stride = 0, sliceH = 0, cf = 0;
                                if (fmt) {
                                    AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_STRIDE, &stride);
                                    AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_SLICE_HEIGHT, &sliceH);
                                    AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_COLOR_FORMAT, &cf);
                                    AMediaFormat_delete(fmt);
                                }
                                if (stride <= 0) stride = vW;
                                if (sliceH <= 0) sliceH = vH;
                                YuvToRGBA(buf, stride, sliceH, cf, rgbaBuf.data(), vW, vH);

                                { tTJSCriticalSectionHolder lk(frameMtx);
                                  if (frameCount >= MAX_FRAMES) { readIdx = (readIdx + 1) % MAX_FRAMES; frameCount--; }
                                  VideoFrame& f = frames[writeIdx];
                                  if (!f.rgba) f.rgba = new uint8_t[vW * vH * 4];
                                  memcpy(f.rgba, rgbaBuf.data(), vW * vH * 4);
                                  f.w = vW; f.h = vH; f.pts = vInfo.presentationTimeUs;
                                  writeIdx = (writeIdx + 1) % MAX_FRAMES; frameCount++; }
                                lastPtsUs = vInfo.presentationTimeUs;
                                pendingFrame = (int)(vInfo.presentationTimeUs * vFrameRate / 1000000.0);
                            }
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

            if (inputDone && vEosQueued && vDecodeDone)
            {
                outputDone = true;
                playing = false;
                if (!ended.exchange(true))
                    PostEnded();
            }
            if (!outputDone) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        { tTJSCriticalSectionHolder lk(frameMtx); if (frameCount > 0) { /* keep last frame */ } }
    }

    // ── iTVPVideoOverlay ────────────────────────────────────
    void AddRef() override { ref++; }
    void Release() override { if (--ref == 0) delete this; }

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
        if (ended) { seekTargetUs = 0; seekAudioUs = 0; ended = false; }
        overlayVisible = true;
        if (overlaySprite)
            overlaySprite->isVisible = true;
        if (sound)
            sound->Play();
        playing = true; paused = false;
    }

    void Stop() override
    {
        playing = false; paused = false; ended = true;
        seekTargetUs = 0;
        seekAudioUs = 0;
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

    void SetPosition(uint64_t tick) override
    {
        // tick in ms -> us
        seekTargetUs = (int64_t)tick * 1000;
        seekAudioUs = seekTargetUs.load();
    }

    void GetPosition(uint64_t* tick) override
    {
        if (!tick) return;
        *tick = (uint64_t)(lastPtsUs / 1000); // us -> ms
    }

    void GetStatus(tTVPVideoStatus* s) override
    {
        if (!s) return;
        if (ended)        *s = vsEnded;
        else if (paused)  *s = vsPaused;
        else if (playing) *s = vsPlaying;
        else              *s = vsStopped;
    }

    void Rewind() override { seekTargetUs = 0; seekAudioUs = 0; ended = false; }

    void SetFrame(int f) override
    {
        if (vFrameRate > 0)
        {
            seekTargetUs = (int64_t)(f * 1000000.0 / vFrameRate);
            seekAudioUs = seekTargetUs.load();
        }
    }
    void GetFrame(int* f) override
    {
        if (!f) return;
        uint64_t pos = 0;
        GetPosition(&pos);
        *f = (int)(pos * vFrameRate / 1000.0);
    }
    void GetFPS(double* fps) override { if (fps) *fps = vFrameRate; }
    void GetNumberOfFrame(int* n) override
    {
        if (n) *n = (int)(durationUs * vFrameRate / 1000000.0);
    }
    void GetTotalTime(int64_t* t) override { if (t) *t = durationUs / 1000; }

    void GetVideoSize(long* w, long* h) override { if (w) *w = vWidth; if (h) *h = vHeight; }

    tTVPBaseTexture* GetFrontBuffer() override
    {
        tTJSCriticalSectionHolder lk(frameMtx);
        if (frameCount <= 0) return nullptr;
        VideoFrame& f = frames[readIdx];
        tTVPBaseTexture* target = bmp[readIdx % 2];
        if (!f.rgba || !target) return nullptr;
        target->Update(f.rgba, f.w * 4, 0, 0, f.w, f.h);
        readIdx = (readIdx + 1) % MAX_FRAMES;
        frameCount--;
        return target;
    }

    void SetVideoBuffer(tTVPBaseTexture* b1, tTVPBaseTexture* b2, long size) override
    {
        layerMode = true;
        bmp[0] = b1; bmp[1] = b2; bmpSize = size;
    }

    void SetStopFrame(int) override {} void GetStopFrame(int* f) override { if (f) *f = 0; } void SetDefaultStopFrame() override {}
    void SetPlayRate(double) override {} void GetPlayRate(double* r) override { if (r) *r = 1.0; }
    void SetAudioBalance(long balance) override
    {
        audioBalance = balance;
        if (sound)
            sound->SetPan(balance / 100000.0f);
    }
    void GetAudioBalance(long* b) override
    {
        if (b) *b = sound ? (long)(sound->GetPan() * 100000) : audioBalance;
    }
    void SetAudioVolume(long volume) override
    {
        audioVolume = volume;
        if (sound)
            sound->SetVolume(volume / 100000.0f);
    }
    void GetAudioVolume(long* v) override
    {
        if (v) *v = sound ? (long)(sound->GetVolume() * 100000) : audioVolume;
    }
    void GetNumberOfAudioStream(unsigned long* c) override { if (c) *c = (audioTrackIdx >= 0 && sound) ? 1 : 0; }
    void SelectAudioStream(unsigned long) override {}
    void GetEnableAudioStreamNum(long* n) override { if (n) *n = (audioTrackIdx >= 0 && sound) ? 0 : -1; }
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
OverlayVideoPlayer* CreateOverlayVideoPlayer(TVPVideoEventCallback cb, void* cbctx) { return new AndroidVideoPlayer(cb, cbctx); }
LayerVideoPlayer*    CreateLayerVideoPlayer(TVPVideoEventCallback cb, void* cbctx)    { return new AndroidVideoPlayer(cb, cbctx); }
