#include "tjsCommHead.h"
#include "WaveMixer.h"
#include "TVPSystem.h"
#include "TickCount.h"
#include <iomanip>
#include "TVPDebug.h"
#include <unordered_set>
#include <algorithm>
#include <Log.h>

#define USE_SDL 1

#if USE_SDL
#include "SDL3/SDL.h"
#else
#include <Al/alc.h>
#include <AL/alext.h>
static ALCdevice* _al_device = nullptr;
static ALCcontext* _al_context = nullptr;
#endif

#if USE_SDL
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
    std::mutex _buffer_mtx;

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

    virtual bool Init()
    {
        if (spec.format == SDL_AUDIO_UNKNOWN)
        {
            SDL_Log("Couldn't create audio stream: Unknow Format!");
            delete this;
            return false;
        }

        sdl_audio_device =
            SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);

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

        std::lock_guard<std::mutex> lk(_buffer_mtx);
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
        std::lock_guard<std::mutex> lk(_buffer_mtx);
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
        std::lock_guard<std::mutex> lk(_buffer_mtx);
        UpdateQueueData();
        return queued;
    }
    virtual tjs_uint GetLatencySamples() override { return 0; }
    virtual float GetLatencySeconds() override { return 0; }

    virtual void SetPosition(float x, float y, float z)
    {
        // not implemented
    }
};
#else
class tTVPSoundBufferAL : public iTVPSoundBuffer
{
    ALuint _alSource;
    ALenum _alFormat;
    ALuint *_bufferIds, *_bufferIds2;
    tjs_uint* _bufferSize;
    tjs_uint _bufferCount;
    int _bufferIdx = -1;
    tTVPWaveFormat _format;

    int _frame_size = 0;
    std::mutex _buffer_mtx;
    tjs_uint _sendedSamples = 0;
    bool _playing = false;

public:
    int16_t _volume_raw[8];
    float _pan = 0;
    float _volume = 1;
    const signed int MAX_VOLUME = 16384;
    void RecalcVolume()
    {
        if (_pan > 0)
        {
            _volume_raw[0] = (1.0f - _pan) * _volume * MAX_VOLUME;
        }
        else
        {
            _volume_raw[0] = _volume * MAX_VOLUME;
        }
        if (_pan < 0)
        {
            _volume_raw[1] = (_pan + 1.0f) * _volume * MAX_VOLUME;
        }
        else
        {
            _volume_raw[1] = _volume * MAX_VOLUME;
        }
        _volume_raw[2] = _volume_raw[0]; // for SIMD
        _volume_raw[3] = _volume_raw[1];
    }

    tTVPSoundBufferAL(tTVPWaveFormat& desired, int bufcount)
      : _frame_size(desired.BytesPerSample * desired.Channels),
        _bufferCount(bufcount)
    {
        _bufferIds = new ALuint[bufcount];
        _bufferIds2 = new ALuint[bufcount];
        _bufferSize = new tjs_uint[bufcount];
        _format = desired;
        alGenSources(1, &_alSource);
        alGenBuffers(_bufferCount, _bufferIds);
        alSourcef(_alSource, AL_GAIN, 1.0f);
    }

    ~tTVPSoundBufferAL() override
    {
        alDeleteBuffers(_bufferCount, _bufferIds);
        alDeleteSources(1, &_alSource);
        delete[] _bufferIds;
        delete[] _bufferIds2;
        delete[] _bufferSize;
    }

    bool Init() override
    {
        if (_format.Channels == 1)
        {
            switch (_format.BitsPerSample)
            {
                case 8:
                    _alFormat = AL_FORMAT_MONO8;
                    break;
                case 16:
                    _alFormat = AL_FORMAT_MONO16;
                    break;
                default:
                    return false;
            }
        }
        else if (_format.Channels == 2)
        {
            switch (_format.BitsPerSample)
            {
                case 8:
                    _alFormat = AL_FORMAT_STEREO8;
                    break;
                case 16:
                    _alFormat = AL_FORMAT_STEREO16;
                    break;
                default:
                    return false;
            }
        }
        else
            return false;
    }

    void Release() override
    {
        delete this;
    }

    bool IsValidFormat(tTVPWaveFormat& fmt) override
    {
        if (_format.SamplesPerSec != fmt.SamplesPerSec ||
            _format.BitsPerSample != fmt.BitsPerSample || _format.Channels != fmt.Channels)
            return false;
        return true;
    }

    bool IsBufferValid() override
    {
        ALint processed = 0;
        alGetSourcei(_alSource, AL_BUFFERS_PROCESSED, &processed);
        if (processed > 0)
            return true;
        ALint queued = 0;
        alGetSourcei(_alSource, AL_BUFFERS_QUEUED, &queued);
        return queued < _bufferCount;
    }

    void AppendBuffer(const void* buf, unsigned int len /*, int tag = 0*/) override
    {
        if (len <= 0)
            return;
        std::lock_guard<std::mutex> lk(_buffer_mtx);

        /* First remove any processed buffers. */
        ALint processed = 0;
        alGetSourcei(_alSource, AL_BUFFERS_PROCESSED, &processed);
        if (processed > 0)
        {
            alSourceUnqueueBuffers(_alSource, processed, _bufferIds2);
            for (int i = 0; i < processed; ++i)
            {
                for (int j = 0; j < _bufferCount; ++j)
                {
                    if (_bufferIds[j] == _bufferIds2[i])
                    {
                        _sendedSamples += _bufferSize[j] / _frame_size;
                        break;
                    }
                }
            }
        }

        /* Refill the buffer queue. */
        ALint queued = 0;
        alGetSourcei(_alSource, AL_BUFFERS_QUEUED, &queued);

        if (queued >= _bufferCount)
            return;
        ++_bufferIdx;
        if (_bufferIdx >= _bufferCount)
            _bufferIdx = 0;
        ALuint bufid = _bufferIds[_bufferIdx];
        alBufferData(bufid, _alFormat, buf, len, _format.SamplesPerSec);
        alSourceQueueBuffers(_alSource, 1, &bufid);
        _bufferSize[_bufferIdx] = len;
    }

    void Reset() override
    {
        std::lock_guard<std::mutex> lk(_buffer_mtx);
        _sendedSamples = 0;

        alSourceRewind(_alSource);
        alSourcei(_alSource, AL_BUFFER, 0);
    }

    void Pause() override
    {
        alSourcePause(_alSource);
        _playing = false;
    }

    void Play() override
    {
        ALenum state;
        alGetSourcei(_alSource, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING)
        {
            alSourcePlay(_alSource);
        }

        _playing = true;
    }

    void Stop() override
    {
        alSourceStop(_alSource);
        Reset();
        _bufferIdx = -1;
        _playing = false;
    }

    void SetVolume(float volume) override
    {
        alSourcef(_alSource, AL_GAIN, volume);
    }

    float GetVolume() override
    {
        float volume = 0;
        alGetSourcef(_alSource, AL_GAIN, &volume);
        return volume;
    }

    void SetPan(float pan) override
    {
        float sourcePosAL[] = {pan, 0.0f, 0.0f};
        alSourcefv(_alSource, AL_POSITION, sourcePosAL);
    }

    float GetPan() override
    {
        float sourcePosAL[3];
        alGetSourcefv(_alSource, AL_POSITION, sourcePosAL);
        return sourcePosAL[0];
    }

    bool IsPlaying() override
    {
        ALenum state;
        alGetSourcei(_alSource, AL_SOURCE_STATE, &state);
        return state == AL_PLAYING;
    }

    void SetPosition(float x, float y, float z) override
    {
        float sourcePosAL[] = {x, y, z};
        alSourcefv(_alSource, AL_POSITION, sourcePosAL);
    }

    int GetRemainBuffers() override
    {
        ALint processed, queued = 0;
        alGetSourcei(_alSource, AL_BUFFERS_PROCESSED, &processed);
        alGetSourcei(_alSource, AL_BUFFERS_QUEUED, &queued);
        return queued - processed;
    }

    tjs_uint GetLatencySamples() override
    {
        std::lock_guard<std::mutex> lk(_buffer_mtx);
        ALint offset = 0, queued = 0;
        alGetSourcei(_alSource, AL_BYTE_OFFSET, &offset);
        alGetSourcei(_alSource, AL_BUFFERS_QUEUED, &queued);
        int remainBuffers = queued;
        if (remainBuffers == 0)
            return 0;
        tjs_int total = -offset;
        for (int i = 0; i < remainBuffers; ++i)
        {
            int idx = _bufferIdx + 1 - remainBuffers + i;
            if (idx >= _bufferCount)
                idx -= _bufferCount;
            else if (idx < 0)
                idx += _bufferCount;
            total += _bufferSize[idx];
        }
        return total / _frame_size;
    }

    float GetLatencySeconds() override
    {
        return (float)GetLatencySamples() / _format.SamplesPerSec;
    }

    tjs_uint GetCurrentPlaySamples() override
    {
        ALint offset = 0;
        alGetSourcei(_alSource, AL_SAMPLE_OFFSET, &offset);
        return _sendedSamples + offset;
    }
};
#endif

static bool isGetSoundDevice = false;
void TVPInitDirectSound(int freq)
{
#if USE_SDL
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
#else
    ALboolean enumeration = alcIsExtensionPresent(nullptr, "ALC_ENUMERATION_EXT");
    if (enumeration == AL_FALSE)
    {
        // enumeration not supported
        _al_device = alcOpenDevice(nullptr);
    }
    else
    {
        // enumeration supported
        const ALCchar* devices = alcGetString(nullptr, ALC_DEVICE_SPECIFIER);
        std::vector<std::string> alldev;
        ttstr log(TJS_W("(info) Sound Driver/Device found : "));
        while (*devices)
        {
            TVPAddImportantLog(log + devices);
            alldev.emplace_back(devices);
            devices += alldev.back().length();
        }
        _al_device = alcOpenDevice(alldev[0].c_str());
    }
    if (!_al_device)
    {
        TVPConsoleLog("AL Init Failed！");
        return;
    }

    _al_context = alcCreateContext(_al_device, nullptr);
    alcMakeContextCurrent(_al_context);
#endif
}

void TVPUninitDirectSound()
{
#if USE_SDL
    #else
    if (_al_context)
    {
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(_al_context);
    }
    if (_al_device)
        alcCloseDevice(_al_device);
#endif
}

iTVPSoundBuffer* TVPCreateSoundBuffer(tTVPWaveFormat& fmt, int bufcount)
{
#if USE_SDL
    tTVPSoundBufferSDL* s = new tTVPSoundBufferSDL(fmt, bufcount);
    if (s == nullptr)
        return nullptr;
    if (s->Init())
        return s;
    else
        return nullptr;
#else
    iTVPSoundBuffer* s = new tTVPSoundBufferAL(fmt, bufcount);
    if (s == nullptr)
        return nullptr;
    if (!s->Init())
    {
        delete s;
        return nullptr;
    }
    return s;
#endif
}
