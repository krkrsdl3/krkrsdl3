// WASM OpenAL audio — maps to Web Audio API
// Key optimizations for smooth playback:
// - Large buffer count (16 default) to absorb decode jitter
// - Auto-grow buffer queue when full (don't drop audio)
// - Uses Emscripten's native OpenAL → Web Audio mapping (no vcpkg)

#include "tjsCommHead.h"
#include "PlatformAudio.h"
#include "PlatformMutex.h"
#include "PlatformThread.h"
#include "TVPSystem.h"
#include "TVPDebug.h"
#include "WaveIntf.h"

#include <emscripten/emscripten.h>

#include <AL/al.h>
#include <AL/alc.h>
#include <atomic>
#include <cstring>
#include <algorithm>
#include <vector>

// ── OpenAL device + context (singleton) ──────────────────────────────────
static ALCdevice*  g_alDevice  = nullptr;
static ALCcontext* g_alContext = nullptr;
static bool         g_alInited  = false;

static bool InitOpenAL()
{
    if (g_alInited) return true;
    g_alDevice = alcOpenDevice(nullptr);
    if (!g_alDevice) { TVPAddLog(ttstr(TJS_N("[openal] alcOpenDevice failed"))); return false; }
    g_alContext = alcCreateContext(g_alDevice, nullptr);
    if (!g_alContext) { alcCloseDevice(g_alDevice); g_alDevice = nullptr; TVPAddLog(ttstr(TJS_N("[openal] alcCreateContext failed"))); return false; }
    alcMakeContextCurrent(g_alContext);
    g_alInited = true;
    TVPAddLog(ttstr(TJS_N("[openal] initialized")));
    return true;
}

// ── tTVPSoundBufferWASM ─────────────────────────────────────────────────
// Implements iTVPSoundBuffer using OpenAL buffer queueing.
// Buffers are dynamically expanded when the queue fills up to avoid drops.
class tTVPSoundBufferWASM : public iTVPSoundBuffer
{
    float    _volume = 1.0f, _pan = 0.0f;
    tTVPWaveFormat _format;
    int      _frameSize = 0, _bytesPerSample = 0;

    ALuint   _alSource = 0;
    ALenum   _alFormat = AL_FORMAT_STEREO16;

    // Dynamic buffer pool — grows when needed
    std::vector<ALuint>  _bufIds;      // OpenAL buffer IDs
    std::vector<tjs_uint> _bufSizes;   // bytes in each buffer
    int      _bufIdx = -1;             // round-robin write index
    tjs_uint _sentSamples = 0;         // total samples enqueued

    bool     _playing = false;
    tTJSCriticalSection _mtx;

public:
    tTVPSoundBufferWASM(tTVPWaveFormat& fmt, int bufcount)
      : _frameSize(fmt.BytesPerSample * fmt.Channels), _bytesPerSample(fmt.BytesPerSample)
    {
        _format = fmt;
        int n = bufcount > 0 ? bufcount : 16;          // default 16 buffers
        _bufIds.resize(n);
        _bufSizes.resize(n, 0);

        if (fmt.Channels == 1)
            _alFormat = (fmt.BitsPerSample == 16) ? AL_FORMAT_MONO16 : AL_FORMAT_MONO8;
        else
            _alFormat = (fmt.BitsPerSample == 16) ? AL_FORMAT_STEREO16 : AL_FORMAT_STEREO8;
    }

    ~tTVPSoundBufferWASM()
    {
        Stop();
        if (_alSource) {
            if (alcGetCurrentContext() != g_alContext) alcMakeContextCurrent(g_alContext);
            alDeleteSources(1, &_alSource);
        }
        if (!_bufIds.empty()) {
            if (alcGetCurrentContext() != g_alContext) alcMakeContextCurrent(g_alContext);
            alDeleteBuffers((ALsizei)_bufIds.size(), _bufIds.data());
        }
    }

    bool Init() override
    {
        if (_format.BitsPerSample != 16 && _format.BitsPerSample != 8)
        {
            TVPAddLog(ttstr(TJS_N("[openal] unsupported bits: ")) + ttstr((int)_format.BitsPerSample));
            delete this; return false;
        }
        if (!InitOpenAL()) { delete this; return false; }

        if (alcGetCurrentContext() != g_alContext) alcMakeContextCurrent(g_alContext);
        alGenSources(1, &_alSource);
        alGenBuffers((ALsizei)_bufIds.size(), _bufIds.data());
        alSourcef(_alSource, AL_GAIN, 1.0f);
        TVPAddLog(ttstr(TJS_N("[openal] buf=")) + ttstr((int)_bufIds.size())
                   + ttstr(TJS_N(" freq=")) + ttstr((int)_format.SamplesPerSec)
                   + ttstr(TJS_N(" ch=")) + ttstr((int)_format.Channels));
        return true;
    }

    void Release() override { delete this; }
    void Play() override  { if (_alSource) { alSourcePlay(_alSource); _playing = true; } }
    void Pause() override { if (_alSource) alSourcePause(_alSource); _playing = false; }

    void Stop() override
    {
        _playing = false;
        if (_alSource) { alSourceStop(_alSource); alSourcei(_alSource, AL_BUFFER, 0); }
        _bufIdx = -1; _sentSamples = 0;
    }

    void Reset() override
    {
        if (_alSource) { alSourceRewind(_alSource); alSourcei(_alSource, AL_BUFFER, 0); }
        _bufIdx = -1; _sentSamples = 0;
    }

    bool IsPlaying() override
    {
        if (!_alSource) return false;
        ALenum s; alGetSourcei(_alSource, AL_SOURCE_STATE, &s);
        return s == AL_PLAYING;
    }

    void SetVolume(float v) override       { _volume = v; if (_alSource) alSourcef(_alSource, AL_GAIN, v); }
    float GetVolume() override            { return _volume; }
    void SetPan(float v) override         { _pan = v; if (_alSource) { float p[] = {v,0,0}; alSourcefv(_alSource, AL_POSITION, p); } }
    float GetPan() override               { return _pan; }

    // ── Core: Append decoded PCM data to the playback queue ──────────────
    void AppendBuffer(const void* buf, unsigned int len) override
    {
        if (!buf || len <= 0 || !_alSource) { TVPAddLog(ttstr(TJS_N("[AL] Append skip buf=")) + ttstr((int)(intptr_t)buf) + ttstr(TJS_N(" len=")) + ttstr((int)len)); return; }
        tTJSCSH lk(_mtx);
        if (alcGetCurrentContext() != g_alContext) alcMakeContextCurrent(g_alContext);

        // Recycle processed buffers
        ALint processed = 0;
        alGetSourcei(_alSource, AL_BUFFERS_PROCESSED, &processed);
        if (processed > 0) {
            for (ALint i = 0; i < processed; ++i) {
                ALuint b; alSourceUnqueueBuffers(_alSource, 1, &b);
                for (size_t j = 0; j < _bufIds.size(); ++j)
                    if (_bufIds[j] == b) { _sentSamples += _bufSizes[j] / _frameSize; break; }
            }
        }

        // If queue is full, grow the buffer pool instead of dropping frames
        ALint queued = 0;
        alGetSourcei(_alSource, AL_BUFFERS_QUEUED, &queued);
        if (queued >= (ALint)_bufIds.size()) {
            ALuint extra;
            alGenBuffers(1, &extra);
            _bufIds.push_back(extra);
            _bufSizes.push_back(0);
            TVPAddLog(ttstr(TJS_N("[AL] pool grew ")) + ttstr((int)_bufIds.size()));
        }

        // Fill the next slot round-robin
        ++_bufIdx;
        if (_bufIdx >= (int)_bufIds.size()) _bufIdx = 0;
        ALuint bid = _bufIds[_bufIdx];
        alBufferData(bid, _alFormat, buf, (ALsizei)len, (ALsizei)_format.SamplesPerSec);
        alSourceQueueBuffers(_alSource, 1, &bid);
        _bufSizes[_bufIdx] = len;

        // Resume if needed
        if (_playing) {
            ALenum s; alGetSourcei(_alSource, AL_SOURCE_STATE, &s);
            if (s != AL_PLAYING) alSourcePlay(_alSource);
        }
    }

    bool IsBufferValid() override
    {
        if (!_alSource) return false;
        ALint p = 0, q = 0;
        alGetSourcei(_alSource, AL_BUFFERS_PROCESSED, &p);
        alGetSourcei(_alSource, AL_BUFFERS_QUEUED, &q);
        return p > 0 || q < (ALint)_bufIds.size();
    }

    bool IsValidFormat(tTVPWaveFormat& fmt) override
    {
        return _format.SamplesPerSec == fmt.SamplesPerSec &&
               _format.BitsPerSample  == fmt.BitsPerSample  &&
               _format.Channels      == fmt.Channels;
    }

    tjs_uint GetCurrentPlaySamples() override
    {
        if (!_alSource) return 0;
        ALint off = 0;
        alGetSourcei(_alSource, AL_SAMPLE_OFFSET, &off);
        return _sentSamples + off;
    }

    int GetRemainBuffers() override
    {
        if (!_alSource) return 0;
        ALint p = 0, q = 0;
        alGetSourcei(_alSource, AL_BUFFERS_PROCESSED, &p);
        alGetSourcei(_alSource, AL_BUFFERS_QUEUED, &q);
        return q - p > 0 ? q - p : 0;
    }

    tjs_uint GetLatencySamples() override
    {
        tTJSCSH lk(_mtx);
        if (!_alSource) return 0;
        ALint off = 0, qu = 0;
        alGetSourcei(_alSource, AL_BYTE_OFFSET, &off);
        alGetSourcei(_alSource, AL_BUFFERS_QUEUED, &qu);
        int total = -off;
        for (int i = 0; i < qu; ++i) {
            int idx = _bufIdx + 1 - qu + i;
            while (idx < 0) idx += (int)_bufIds.size();
            while (idx >= (int)_bufIds.size()) idx -= (int)_bufIds.size();
            total += _bufSizes[idx];
        }
        return total / _frameSize;
    }

    float GetLatencySeconds() override { return (float)GetLatencySamples() / _format.SamplesPerSec; }

    void SetPosition(float x, float y, float z) override
    {
        if (!_alSource) return;
        float p[] = {x, y, z};
        alSourcefv(_alSource, AL_POSITION, p);
    }
};

// ── Global init / shutdown ──────────────────────────────────────────────
static bool g_devInited = false;

void TVPInitDirectSound(int freq)
{
    if (!g_devInited) { g_devInited = true; TVPAddLog(ttstr(TJS_N("[openal] TVPInitDirectSound"))); InitOpenAL(); }
}

void TVPUninitDirectSound()
{
    if (alcGetCurrentContext() != g_alContext) alcMakeContextCurrent(g_alContext);
    if (g_alContext) { alcMakeContextCurrent(nullptr); alcDestroyContext(g_alContext); g_alContext = nullptr; }
    if (g_alDevice)  { alcCloseDevice(g_alDevice); g_alDevice = nullptr; }
    g_alInited = false;
}

iTVPSoundBuffer* TVPCreateSoundBuffer(tTVPWaveFormat& fmt, int bufcount)
{
    tTVPSoundBufferWASM* buf = new tTVPSoundBufferWASM(fmt, bufcount);
    return (buf && buf->Init()) ? buf : nullptr;
}
