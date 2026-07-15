#include "tjsCommHead.h"
#include "PlatformAudio.h"
#include "PlatformMutex.h"
#include "TVPSystem.h"
#include "TVPDebug.h"
#include <iomanip>
#include <unordered_set>
#include <algorithm>

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_init.h>

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

        sdl_audio_device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);

        _stream = SDL_CreateAudioStream(&spec, NULL);
        if (!_stream)
        {
            SDL_Log("Couldn't create audio stream: %s", SDL_GetError());
            delete this;
            return false;
        }
        SDL_BindAudioStream(sdl_audio_device, _stream);
        return true;
    }

    virtual ~tTVPSoundBufferSDL()
    {
        Stop();

        if (_stream)
        {
            SDL_UnbindAudioStream(_stream);
            SDL_DestroyAudioStream(_stream);
        }
        if (sdl_audio_device)
            SDL_CloseAudioDevice(sdl_audio_device);
        delete[] _bufferSizeCache;
    }

    virtual void Release() override { delete this; }
    virtual void Play() override
    {
        if (_playing)
            return;
        SDL_ResumeAudioStreamDevice(_stream);
        _playing = true;
    }
    virtual void Pause() override
    {
        if (!_playing)
            return;
        SDL_PauseAudioStreamDevice(_stream);
        _playing = false;
    }

    virtual void Stop() override
    {
        _playing = false;
        totalBufferSize = 0;
        _bufferIdx = -1;
        Reset();
    }
    virtual void Reset() override
    {
        SDL_ClearAudioStream(_stream);
        queued = 0;
        processed = 0;
    }
    virtual bool IsPlaying() override { return _playing; }
    virtual void SetVolume(float v) override
    {
        SDL_SetAudioStreamGain(_stream, v);
        _volume = v;
    }
    virtual float GetVolume() override
    {
        _volume = SDL_GetAudioStreamGain(_stream);
        return _volume;
    }
    virtual void SetPan(float v) override { _pan = v; }
    virtual float GetPan() override { return _pan; }
    virtual void AppendBuffer(const void* _inbuf, unsigned int inlen /*, int tag = 0*/) override
    {
        if (inlen <= 0)
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
        return SDL_GetAudioStreamQueued(_stream) / _frame_size;
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

static bool isGetSoundDevice = false;
void TVPInitDirectSound(int freq)
{
    if (!isGetSoundDevice)
    {
        isGetSoundDevice = true;
        if (SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
            int i, num_devices;
            SDL_AudioDeviceID* devices = SDL_GetAudioPlaybackDevices(&num_devices);
            if (devices)
            {
                for (i = 0; i < num_devices; ++i)
                {
                    SDL_AudioDeviceID instance_id = devices[i];
                    SDL_Log("AudioDevice %" SDL_PRIu32 ": %s", instance_id,
                            SDL_GetAudioDeviceName(instance_id));
                }
                SDL_free(devices);
            }
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }
    }
}

void TVPUninitDirectSound()
{
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