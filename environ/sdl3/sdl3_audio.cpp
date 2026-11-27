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
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_init.h>

// Share a single SDL playback device across all sound buffers to avoid
// repeatedly reopening hardware audio backends during rapid playback.
static SDL_AudioDeviceID g_shared_device = 0;
// Pool of reusable audio streams to reduce frequent creation/destruction overhead.
static std::vector<SDL_AudioStream*> g_idle_streams;
static constexpr size_t TVP_AUDIO_STREAM_POOL = 8;

static std::mutex g_audio_dev_mtx;

class tTVPSoundBufferSDL : public iTVPSoundBuffer
{
public:
    bool _playing = false;
    bool _bound = false;
    float _volume = 1.0f;
    float _pan = 0.0f;

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
    bool _is_float = false;
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
        _frame_size((fmt.BitsPerSample / 8) * fmt.Channels)
    {
        if (_frame_size <= 0)
            _frame_size = 4;
        _bufferSizeCache = new tjs_uint[bufcount];
        memset(&spec, 0, sizeof(spec));
        spec.freq = fmt.SamplesPerSec;
        spec.channels = fmt.Channels;
        BitsPerSample = fmt.BitsPerSample;
        _is_float = fmt.IsFloat;
        if (fmt.IsFloat)
        {
            spec.format = (fmt.BitsPerSample == 32) ? SDL_AUDIO_F32LE : SDL_AUDIO_UNKNOWN;
        }
        else
        {
            switch (fmt.BitsPerSample)
            {
                case 8:
                    spec.format = SDL_AUDIO_U8;
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
    }

    virtual bool Init() override
    {
        if (spec.format == SDL_AUDIO_UNKNOWN)
        {
            SDL_Log("Couldn't create audio stream: Unknown Format!");
            delete this;
            return false;
        }

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
        // Keep unbound until Play() to avoid mixing uninitialized or stale stream state.
        SDL_UnbindAudioStream(_stream);
        _bound = false;
        SDL_ClearAudioStream(_stream);
        SDL_SetAudioStreamGain(_stream, _volume);
        return true;
    }

    virtual ~tTVPSoundBufferSDL()
    {
        Stop();

        if (_stream)
        {
            SDL_UnbindAudioStream(_stream);
            _bound = false;
            SDL_SetAudioStreamGetCallback(_stream, nullptr, nullptr);
            SDL_ClearAudioStream(_stream);
            SDL_SetAudioStreamGain(_stream, 1.0f);

            std::lock_guard<std::mutex> lock(g_audio_dev_mtx);
            // Return to pool for reuse; destroy only if pool is full or device is closed.
            if (g_shared_device && g_idle_streams.size() < TVP_AUDIO_STREAM_POOL)
                g_idle_streams.push_back(_stream);
            else
                SDL_DestroyAudioStream(_stream);
        }
        delete[] _bufferSizeCache;
    }

    virtual void Release() override { delete this; }
    virtual void Play() override
    {
        if (_playing)
            return;
        if (_stream && sdl_audio_device && !_bound)
        {
            SDL_SetAudioStreamGain(_stream, _volume);
            if (SDL_BindAudioStream(sdl_audio_device, _stream))
                _bound = true;
        }
        _playing = true;
    }
    virtual void Pause() override
    {
        if (!_playing)
            return;
        if (_stream && _bound)
        {
            SDL_UnbindAudioStream(_stream);
            _bound = false;
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
        {
            // Unbind before clearing/resetting stream format so the audio callback
            // never reads a stream whose resampler state is being reinitialized.
            bool was_bound = _bound;
            if (was_bound)
            {
                SDL_UnbindAudioStream(_stream);
                _bound = false;
            }
            SDL_ClearAudioStream(_stream);
            SDL_SetAudioStreamFormat(_stream, &spec, nullptr);
            if (was_bound && sdl_audio_device)
            {
                if (SDL_BindAudioStream(sdl_audio_device, _stream))
                    _bound = true;
            }
        }
        queued = 0;
        processed = 0;
    }
    virtual bool IsPlaying() override { return _playing; }
    virtual void SetVolume(float v) override
    {
        _volume = v;
        if (_stream)
            SDL_SetAudioStreamGain(_stream, v);
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
    }
    virtual bool IsValidFormat(tTVPWaveFormat& fmt) override
    {
        if (spec.freq != fmt.SamplesPerSec || BitsPerSample != fmt.BitsPerSample ||
            spec.channels != fmt.Channels || _is_float != fmt.IsFloat)
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

    // Use a 4096-frame device buffer and keep the shared device active to prevent
    // underruns and backend device pause/resume glitches during rapid playback.
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "4096");
    g_shared_device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!g_shared_device)
    {
        SDL_Log("Couldn't open default audio device: %s", SDL_GetError());
        return;
    }
    SDL_ResumeAudioDevice(g_shared_device);

    SDL_AudioSpec source{SDL_AUDIO_S16LE, 2, freq > 0 ? freq : 44100};
    for (size_t i = 0; i < TVP_AUDIO_STREAM_POOL; ++i)
    {
        SDL_AudioStream* stream = SDL_CreateAudioStream(&source, nullptr);
        if (!stream)
            break;
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
