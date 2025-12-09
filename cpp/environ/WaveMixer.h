#pragma once
#include "WaveIntf.h"
#include <list>
#include <mutex>

#define TVPAL_BUFFER_COUNT 4

class iTVPSoundBuffer {
public:
	virtual ~iTVPSoundBuffer() {}
    virtual bool Init() = 0;
	virtual void Release() = 0;
	virtual void Play() = 0;
	virtual void Pause() = 0;
	virtual void Stop() = 0;
	virtual void Reset() = 0;
	virtual bool IsPlaying() = 0;
	virtual void SetVolume(float v) = 0;
	virtual float GetVolume() = 0;
	virtual void SetPan(float v) = 0;
	virtual float GetPan() = 0;
	virtual void AppendBuffer(const void *buf, unsigned int len/*, int tag = 0*/) = 0;
	virtual bool IsBufferValid() = 0;
    virtual bool IsValidFormat(tTVPWaveFormat &fmt) = 0;
	virtual tjs_uint GetCurrentPlaySamples() = 0;
	virtual tjs_uint GetLatencySamples() = 0;
    virtual float GetLatencySeconds() = 0;
	virtual int GetRemainBuffers() = 0;
    virtual void SetPosition(float x, float y, float z) = 0;
};

void TVPInitDirectSound(int freq = 48000);
void TVPUninitDirectSound();
iTVPSoundBuffer* TVPCreateSoundBuffer(tTVPWaveFormat &fmt, int bufcount);