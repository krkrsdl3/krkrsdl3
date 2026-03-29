#include "tjsCommHead.h"
#include "WaveMixer.h"
#include "TVPSystem.h"
#include "TickCount.h"
#include <iomanip>
#include "TVPDebug.h"
#include <unordered_set>
#include <algorithm>
#include <Log.h>
#include <deque>

#include "SDL2/SDL.h"

// SDL2 callback-pull audio model (following Kirikiroid2 WaveMixer architecture)
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
    SDL_AudioSpec obtained_spec;
    SDL_AudioSpec desired_spec;
    tjs_int BitsPerSample = 0;
    int _frame_size = 0;

    // Ring buffer for audio data (Kirikiroid2 callback-pull model)
    std::deque<std::vector<uint8_t>> _buffers;
    size_t _buf_read_offset = 0; // offset into front buffer

    static void audio_callback(void* userdata, Uint8* stream, int len)
    {
        tTVPSoundBufferSDL* self = (tTVPSoundBufferSDL*)userdata;
        std::lock_guard<std::mutex> lk(self->_buffer_mtx);

        int written = 0;
        while (written < len && !self->_buffers.empty())
        {
            auto& front = self->_buffers.front();
            size_t avail = front.size() - self->_buf_read_offset;
            size_t need = (size_t)(len - written);
            size_t chunk = std::min(avail, need);
            // apply volume (14-bit scaling, Kirikiroid2 style)
            int vol = (int)(self->_volume * 16384.0f);
            if (vol >= 16384)
            {
                memcpy(stream + written, front.data() + self->_buf_read_offset, chunk);
            }
            else if (vol <= 0)
            {
                memset(stream + written, 0, chunk);
            }
            else
            {
                const int16_t* src = (const int16_t*)(front.data() + self->_buf_read_offset);
                int16_t* dst = (int16_t*)(stream + written);
                for (size_t i = 0; i < chunk / 2; i++)
                {
                    int s = ((int)src[i] * vol) >> 14;
                    // soft clamp
                    if (s > 32767) s = 32767;
                    if (s < -32768) s = -32768;
                    dst[i] = (int16_t)s;
                }
            }
            written += (int)chunk;
            self->_buf_read_offset += chunk;
            if (self->_buf_read_offset >= front.size())
            {
                self->_buffers.pop_front();
                self->_buf_read_offset = 0;
                self->processed++;
            }
        }
        // fill silence if underrun
        if (written < len)
        {
            memset(stream + written, 0, len - written);
        }
    }

    void UpdateQueueData()
    {
        queued = (int)_buffers.size();
    }

    tTVPSoundBufferSDL(tTVPWaveFormat& fmt, int bufcount)
      : _bufferLimitCount(bufcount),
        _frame_size(fmt.BitsPerSample * fmt.Channels)
    {
        _bufferSizeCache = new tjs_uint[bufcount];
        memset(&desired_spec, 0, sizeof(desired_spec));
        desired_spec.freq = fmt.SamplesPerSec;
        desired_spec.channels = fmt.Channels;
        desired_spec.samples = 4096;
        desired_spec.callback = audio_callback;
        desired_spec.userdata = this;
        BitsPerSample = fmt.BitsPerSample;
        switch (fmt.BitsPerSample)
        {
            case 8:
                desired_spec.format = AUDIO_S8;
                break;
            case 16:
                desired_spec.format = AUDIO_S16LSB;
                break;
            case 32:
                desired_spec.format = AUDIO_S32LSB;
                break;
            default:
                desired_spec.format = 0;
                break;
        }
    }

    virtual bool Init() override
    {
        if (desired_spec.format == 0)
        {
            SDL_Log("[krkr2-audio] Couldn't create audio stream: Unknown Format!");
            return false;
        }

        sdl_audio_device = SDL_OpenAudioDevice(NULL, 0, &desired_spec, &obtained_spec, 0);
        if (sdl_audio_device == 0)
        {
            SDL_Log("[krkr2-audio] Couldn't open audio device: %s", SDL_GetError());
            return false;
        }
        return true;
    }

    virtual ~tTVPSoundBufferSDL()
    {
        Stop();

        if (sdl_audio_device)
        {
            SDL_CloseAudioDevice(sdl_audio_device);
            sdl_audio_device = 0;
        }
        delete[] _bufferSizeCache;
    }

    virtual void Release() override { delete this; }
    virtual void Play() override
    {
        if (_playing)
            return;
        SDL_PauseAudioDevice(sdl_audio_device, 0);
        _playing = true;
    }
    virtual void Pause() override
    {
        if (!_playing)
            return;
        SDL_PauseAudioDevice(sdl_audio_device, 1);
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
        std::lock_guard<std::mutex> lk(_buffer_mtx);
        _buffers.clear();
        _buf_read_offset = 0;
        queued = 0;
        processed = 0;
    }
    virtual bool IsPlaying() override { return _playing; }
    virtual void SetVolume(float v) override
    {
        _volume = v;
    }
    virtual float GetVolume() override
    {
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
        if (queued >= (int)_bufferLimitCount)
            return;

        ++_bufferIdx;
        if (_bufferIdx >= (int)_bufferLimitCount)
            _bufferIdx = 0;

        std::vector<uint8_t> buf((const uint8_t*)_inbuf, (const uint8_t*)_inbuf + inlen);
        _buffers.push_back(std::move(buf));
        _bufferSizeCache[_bufferIdx] = inlen;
        totalBufferSize += inlen;
    }
    virtual bool IsBufferValid() override
    {
        std::lock_guard<std::mutex> lk(_buffer_mtx);
        UpdateQueueData();
        if (processed > 0)
            return true;
        return (processed + queued) < (int)_bufferLimitCount;
    }
    virtual bool IsValidFormat(tTVPWaveFormat& fmt) override
    {
        if (desired_spec.freq != (int)fmt.SamplesPerSec || BitsPerSample != fmt.BitsPerSample ||
            desired_spec.channels != fmt.Channels)
            return false;
        return true;
    }
    virtual tjs_uint GetCurrentPlaySamples() override
    {
        std::lock_guard<std::mutex> lk(_buffer_mtx);
        size_t total = 0;
        for (auto& b : _buffers)
            total += b.size();
        if (!_buffers.empty())
            total -= _buf_read_offset;
        return (tjs_uint)(total / _frame_size);
    }
    virtual int GetRemainBuffers() override
    {
        std::lock_guard<std::mutex> lk(_buffer_mtx);
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
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0)
        {
            int num_devices = SDL_GetNumAudioDevices(0);
            for (int i = 0; i < num_devices; ++i)
            {
                SDL_Log("AudioDevice %d: %s", i, SDL_GetAudioDeviceName(i, 0));
            }
            // Keep audio subsystem alive — SDL_OpenAudioDevice needs it later
        }
        else
        {
            SDL_Log("[krkr2-audio] SDL_InitSubSystem(SDL_INIT_AUDIO) failed: %s", SDL_GetError());
        }
    }
}

void TVPUninitDirectSound()
{
    if (isGetSoundDevice)
    {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        isGetSoundDevice = false;
    }
}

iTVPSoundBuffer* TVPCreateSoundBuffer(tTVPWaveFormat& fmt, int bufcount)
{
    tTVPSoundBufferSDL* s = new tTVPSoundBufferSDL(fmt, bufcount);
    if (s == nullptr)
        return nullptr;
    if (s->Init())
        return s;
    delete s;
    return nullptr;
}
