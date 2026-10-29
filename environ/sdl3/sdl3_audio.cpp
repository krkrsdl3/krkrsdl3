#include "tjsCommHead.h"
#include "PlatformAudio.h"
#include "PlatformMutex.h"
#include "TVPSystem.h"
#include "TVPDebug.h"
#include <iomanip>
#include <unordered_set>
#include <algorithm>
#include <vector>
#include <mutex>

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_init.h>

// 所有音效共用同一个 SDL 播放设备。每个音效各开一个设备会反复重建
// CoreAudio 通路，播语音时能听到明显的断续。
static SDL_AudioDeviceID g_shared_device = 0;
// 复用的音频流对象池。频繁创建/销毁 SDL_AudioStream 同样会打断其它正在播放的流。
static std::vector<SDL_AudioStream*> g_idle_streams;
static constexpr size_t TVP_AUDIO_STREAM_POOL = 8;

static std::mutex g_audio_dev_mtx;
static int g_active_streams = 0;

class tTVPSoundBufferSDL : public iTVPSoundBuffer
{
public:
    bool _playing = false;
    float _volume = 1;
    float _pan = 0;

    tjs_uint _bufferLimitCount = 0;
    int totalBufferSize = 0;
    tjs_uint* _bufferSizeCache;
    int _bufferIdx = -1;
    tTJSCriticalSection _buffer_mtx;

    int queued = 0, processed = 0;

    SDL_AudioDeviceID sdl_audio_device = 0;
    SDL_AudioStream* _stream = NULL;
    SDL_AudioSpec spec;
    tjs_int BitsPerSample = 0;
    int _frame_size = 0;

    void UpdateQueueData()
    {
        int dataVar = SDL_GetAudioStreamQueued(_stream);
        int newSize = dataVar;
        int idxVar = _bufferIdx;
        if (idxVar < 0)
            return;
        if (dataVar < _frame_size)
        {
            queued = 0;
        }
        else
        {
            // queue
            queued = 0;
            while (dataVar > 0)
            {
                dataVar -= _bufferSizeCache[idxVar];
                idxVar--;
                if (idxVar < 0)
                    idxVar = _bufferLimitCount - 1;
                queued++;
            }
        }

        // remain
        int tmp = totalBufferSize;
        totalBufferSize = newSize - dataVar;
        dataVar = tmp;
        idxVar = _bufferIdx;
        processed = -queued;
        while (dataVar > 0)
        {
            dataVar -= _bufferSizeCache[idxVar];
            idxVar--;
            if (idxVar < 0)
                idxVar = _bufferLimitCount - 1;
            processed++;
        }
    }

    tTVPSoundBufferSDL(tTVPWaveFormat& fmt, int bufcount)
      : _bufferLimitCount(bufcount),
        _frame_size(fmt.BitsPerSample * fmt.Channels)
    {
        _bufferSizeCache = new tjs_uint[bufcount];
        memset(&spec, 0, sizeof(spec));
        spec.freq = fmt.SamplesPerSec;
        spec.channels = fmt.Channels;
        BitsPerSample = fmt.BitsPerSample;
        switch (fmt.BitsPerSample)
        {
            case 8:
                spec.format = SDL_AUDIO_S8;
                break;
            case 16:
                spec.format = SDL_AUDIO_S16LE;
                break;
            case 32:
                spec.format = SDL_AUDIO_S32LE;
                break;
            default:
                spec.format = SDL_AUDIO_UNKNOWN;
                break;
        }
    }

    virtual bool Init() override
    {
        if (spec.format == SDL_AUDIO_UNKNOWN)
        {
            SDL_Log("Couldn't create audio stream: Unknow Format!");
            delete this;
            return false;
        }

        // 语音播放期间不要重开设备或重新绑定 CoreAudio 通路，优先复用共享设备。
        {
            std::lock_guard<std::mutex> lock(g_audio_dev_mtx);
            sdl_audio_device = g_shared_device;
            if (!g_idle_streams.empty())
            {
                _stream = g_idle_streams.back();
                g_idle_streams.pop_back();
            }
        }
        if (!_stream && sdl_audio_device)
        {
            _stream = SDL_CreateAudioStream(&spec, nullptr);
            if (_stream && !SDL_BindAudioStream(sdl_audio_device, _stream))
            {
                SDL_DestroyAudioStream(_stream);
                _stream = nullptr;
            }
        }
        if (!_stream || !sdl_audio_device || !SDL_SetAudioStreamFormat(_stream, &spec, nullptr))
        {
            if (_stream)
            {
                SDL_DestroyAudioStream(_stream);
                _stream = nullptr;
            }
            delete this;
            return false;
        }
        return true;
    }

    virtual ~tTVPSoundBufferSDL()
    {
        Stop();

        if (_stream)
        {
            SDL_SetAudioStreamGetCallback(_stream, nullptr, nullptr);
            SDL_ClearAudioStream(_stream);
            SDL_SetAudioStreamGain(_stream, 1.0f);

            std::lock_guard<std::mutex> lock(g_audio_dev_mtx);
            // 放回对象池，下次播放直接复用；池满或设备已关闭才真正销毁。
            if (g_shared_device && g_idle_streams.size() < TVP_AUDIO_STREAM_POOL)
                g_idle_streams.push_back(_stream);
            else
                SDL_DestroyAudioStream(_stream);
        }
        // 共享设备由 TVPUninitDirectSound 统一关闭。
        delete[] _bufferSizeCache;
    }

    virtual void Release() override { delete this; }
    virtual void Play() override
    {
        if (_playing)
            return;
        // 所有缓冲区共用播放设备，用引用计数决定何时真正暂停/恢复设备。
        // 通过增益静音会让 @wvoice 依赖的播放状态错乱，因此只在没有活动流时暂停设备。
        {
            std::lock_guard<std::mutex> lk(g_audio_dev_mtx);
            ++g_active_streams;
            if (g_active_streams == 1 && g_shared_device)
                SDL_ResumeAudioDevice(g_shared_device);
        }
        _playing = true;
    }
    virtual void Pause() override
    {
        if (!_playing)
            return;
        {
            std::lock_guard<std::mutex> lk(g_audio_dev_mtx);
            if (g_active_streams > 0)
                --g_active_streams;
            if (g_active_streams == 0 && g_shared_device)
                SDL_PauseAudioDevice(g_shared_device);
        }
        _playing = false;
    }

    virtual void Stop() override
    {
        Pause();
        totalBufferSize = 0;
        _bufferIdx = -1;
        Reset();
    }
    virtual void Reset() override
    {
        if (_stream)
            SDL_ClearAudioStream(_stream);
        queued = 0;
        processed = 0;
    }
    virtual bool IsPlaying() override { return _playing; }
    virtual void SetVolume(float v) override
    {
        if (_stream)
            SDL_SetAudioStreamGain(_stream, v);
        _volume = v;
    }
    virtual float GetVolume() override
    {
        if (_stream)
            _volume = SDL_GetAudioStreamGain(_stream);
        return _volume;
    }
    virtual void SetPan(float v) override { _pan = v; }
    virtual float GetPan() override { return _pan; }
    virtual void AppendBuffer(const void* _inbuf, unsigned int inlen /*, int tag = 0*/) override
    {
        if (inlen <= 0 || !_stream)
            return;

        tTJSCSH lk(_buffer_mtx);
        UpdateQueueData();
        if (queued >= _bufferLimitCount)
            return;

        ++_bufferIdx;
        if (_bufferIdx >= _bufferLimitCount)
            _bufferIdx = 0;

        SDL_PutAudioStreamData(_stream, _inbuf, inlen);
        _bufferSizeCache[_bufferIdx] = inlen;
        totalBufferSize += inlen;
    }
    virtual bool IsBufferValid() override
    {
        tTJSCSH lk(_buffer_mtx);
        UpdateQueueData();
        if (processed > 0)
            return true;
        return (processed + queued) < _bufferLimitCount;
        // unlimited buffer size
        // return !_buffers.empty(); // thread safe if read only
    }
    virtual bool IsValidFormat(tTVPWaveFormat& fmt) override
    {
        if (spec.freq != fmt.SamplesPerSec || BitsPerSample != fmt.BitsPerSample ||
            spec.channels != fmt.Channels)
            return false;
        return true;
    }
    virtual tjs_uint GetCurrentPlaySamples() override
    {
        return _stream ? SDL_GetAudioStreamQueued(_stream) / _frame_size : 0;
    }
    virtual int GetRemainBuffers() override
    {
        tTJSCSH lk(_buffer_mtx);
        UpdateQueueData();
        return queued;
    }
    virtual tjs_uint GetLatencySamples() override { return 0; }
    virtual float GetLatencySeconds() override { return 0; }

    virtual void SetPosition(float x, float y, float z) override
    {
        // not implemented
    }
};

void TVPInitDirectSound(int freq)
{
    std::lock_guard<std::mutex> lock(g_audio_dev_mtx);
    if (g_shared_device)
        return;

    g_shared_device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!g_shared_device)
    {
        SDL_Log("Couldn't open default audio device: %s", SDL_GetError());
        return;
    }
    // 设备在有流播放时才恢复，避免空转。
    SDL_PauseAudioDevice(g_shared_device);

    SDL_AudioSpec source{SDL_AUDIO_S16LE, 2, freq};
    for (size_t i = 0; i < TVP_AUDIO_STREAM_POOL; ++i)
    {
        SDL_AudioStream* stream = SDL_CreateAudioStream(&source, nullptr);
        if (!stream)
            break;
        if (!SDL_BindAudioStream(g_shared_device, stream))
        {
            SDL_DestroyAudioStream(stream);
            break;
        }
        g_idle_streams.push_back(stream);
    }
}

void TVPUninitDirectSound()
{
    std::lock_guard<std::mutex> lock(g_audio_dev_mtx);
    for (auto* stream : g_idle_streams)
        SDL_DestroyAudioStream(stream);
    g_idle_streams.clear();
    if (g_shared_device)
        SDL_CloseAudioDevice(g_shared_device);
    g_shared_device = 0;
    g_active_streams = 0;
}

iTVPSoundBuffer* TVPCreateSoundBuffer(tTVPWaveFormat& fmt, int bufcount)
{
    tTVPSoundBufferSDL* s = new tTVPSoundBufferSDL(fmt, bufcount);
    if (s == nullptr)
        return nullptr;
    if (s->Init())
        return s;
    else
        return nullptr;
}
