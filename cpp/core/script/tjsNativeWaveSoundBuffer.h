#pragma once

#include "tjsNative.h"
#include "WaveSegmentQueue.h"
#include "WaveIntf.h"

//---------------------------------------------------------------------------
// tTVPSoundStatus
//---------------------------------------------------------------------------
enum tTVPSoundStatus {
    ssUnload, // data is not specified
    ssStop, // stop
    ssPlay, // play
    ssPause, // pause
    ssReady, // ready
};
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// tTJSNI_BaseSoundBuffer
//---------------------------------------------------------------------------
class tTJSNI_BaseSoundBuffer : public tTJSNativeInstance {
    typedef tTJSNativeInstance inherited;
    friend class tTVPSoundBufferTimerDispatcher;

public:
    tTJSNI_BaseSoundBuffer();
    tjs_error TJS_INTF_METHOD Construct(tjs_int numparams, tTJSVariant **param,
                                        iTJSDispatch2 *tjs_obj);
    void TJS_INTF_METHOD Invalidate();

protected:
    iTJSDispatch2 *Owner; // owner object
    tTJSVariantClosure ActionOwner; // object to send action
    tTVPSoundStatus Status; // status

    // volume functions ( implement this in child classes )
    // tTJSNI_BaseSoundBuffer/tTJSNI_SoundBuffer manage this when fading the
    // volume.
    virtual void SetVolume(tjs_int i) = 0;
    virtual tjs_int GetVolume() const = 0;

public:
    ttstr GetStatusString() const;

protected:
    bool CanDeliverEvents;
    void SetStatus(tTVPSoundStatus s);
    void SetStatusAsync(tTVPSoundStatus s);

public:
    tTVPSoundStatus GetStatus() const { return Status; }


public:
    tTJSVariantClosure GetActionOwnerNoAddRef() const { return ActionOwner; }

    //-- fading stuff -----------------------------------------------------
protected:
    virtual void TimerBeatHandler();
    // call this in tTJSNI_SoundBuffer periodically.
    // interval time must be declered as TVP_SB_BEAT_INTERVAL in ms.
    // accuracy is so not be required.

private:
    bool InFading;
    tjs_int TargetVolume; // distination volume
    tjs_int DeltaVolume; // delta volume for each interval
    tjs_int FadeCount; // beat count over fading
    tjs_int BlankLeft; // blank time until fading

public:
    void Fade(tjs_int to, tjs_int time, tjs_int blanktime);
    void StopFade(bool async, bool settargetvol);
};

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
class tTJSNI_SoundBuffer : public tTJSNI_BaseSoundBuffer {
    typedef tTJSNI_BaseSoundBuffer inherited;

public:
    tTJSNI_SoundBuffer();
    tjs_error TJS_INTF_METHOD Construct(tjs_int numparams, tTJSVariant **param,
                                        iTJSDispatch2 *tjs_obj);
    void TJS_INTF_METHOD Invalidate();

public:
protected:
};

//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// tTJSNI_BaseWaveSoundBuffer
//---------------------------------------------------------------------------
class tTVPWaveLoopManager;
class tTJSNI_BaseWaveSoundBuffer : public tTJSNI_SoundBuffer
{
    typedef tTJSNI_SoundBuffer inherited;

    iTJSDispatch2* WaveFlagsObject;
    iTJSDispatch2* WaveLabelsObject;

    struct tFilterObjectAndInterface
    {
        tTJSVariant Filter;             // filter object
        iTVPBasicWaveFilter* Interface; // filter interface
        tFilterObjectAndInterface(const tTJSVariant& filter, iTVPBasicWaveFilter* interf)
          : Filter(filter),
            Interface(interf)
        {
            ;
        }
    };
    std::vector<tFilterObjectAndInterface> FilterInterfaces; // backupped filter interface array

protected:
    tTVPWaveLoopManager* LoopManager;       // will be set by tTJSNI_WaveSoundBuffer
    tTVPSampleAndLabelSource* FilterOutput; // filter output
    iTJSDispatch2* Filters;                 // wave filters array (TJS2 array object)
public:
    tTJSNI_BaseWaveSoundBuffer();
    tjs_error TJS_INTF_METHOD Construct(tjs_int numparams,
                                        tTJSVariant** param,
                                        iTJSDispatch2* tjs_obj);
    void TJS_INTF_METHOD Invalidate();

protected:
    void InvokeLabelEvent(const ttstr& name);
    void RecreateWaveLabelsObject();
    void RebuildFilterChain();
    void ClearFilterChain();
    void ResetFilterChain();
    void UpdateFilterChain();

public:
    iTJSDispatch2* GetWaveFlagsObjectNoAddRef();
    iTJSDispatch2* GetWaveLabelsObjectNoAddRef();
    tTVPWaveLoopManager* GetWaveLoopManager() const { return LoopManager; }
    iTJSDispatch2* GetFiltersNoAddRef() { return Filters; }
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNI_WaveSoundBuffer : Wave Native Instance
//---------------------------------------------------------------------------
class tTVPWaveLoopManager;
class tTVPWaveSoundBufferDecodeThread;
class tTJSNI_WaveSoundBuffer : public tTJSNI_BaseWaveSoundBuffer
{
    typedef tTJSNI_BaseWaveSoundBuffer inherited;

public:
    tTJSNI_WaveSoundBuffer();
    tjs_error TJS_INTF_METHOD Construct(tjs_int numparams,
                                        tTJSVariant** param,
                                        iTJSDispatch2* tjs_obj);
    void TJS_INTF_METHOD Invalidate();

    //-- buffer management ------------------------------------------------
private:
    iTVPSoundBuffer* SoundBuffer;
    void ThrowSoundBufferException(const ttstr& reason);
    void TryCreateSoundBuffer(bool use3d);
    void CreateSoundBuffer();
    void DestroySoundBuffer();
    void ResetSoundBuffer();
    void ResetSamplePositions();

    tTVPWaveFormat C_InputFormat;
    tTVPWaveFormat InputFormat;

    tjs_int BufferBytes;
    tjs_int AccessUnitBytes;
    tjs_int AccessUnitSamples;
    tjs_int L2AccessUnitBytes;

    tjs_int L2BufferUnits;
    tjs_int L1BufferUnits;

    tjs_int Level2BufferSize;
    tjs_uint8* Level2Buffer;

public:
    void FreeDirectSoundBuffer(bool disableevent = true)
    {
        // called at exit ( system uninitialization )
        bool b = CanDeliverEvents;
        if (disableevent)
            CanDeliverEvents = false; // temporarily disables event derivering
        Stop();
        DestroySoundBuffer();
        CanDeliverEvents = b;
    }

    //-- playing stuff ----------------------------------------------------
private:
    tTJSCriticalSection BufferCS;
    tTJSCriticalSection L2BufferCS;
    tTJSCriticalSection L2BufferRemainCS;

public:
    tTJSCriticalSection& GetBufferCS() { return BufferCS; }

private:
    tTVPWaveDecoder* Decoder;
    tTVPWaveSoundBufferDecodeThread* Thread;

public:
    bool ThreadCallbackEnabled;

private:
    bool BufferPlaying;   // whether this sound buffer is playing
    bool DSBufferPlaying; // whether the DS buffer is 'actually' playing
    bool Paused;

    bool UseVisBuffer;

    tjs_int SoundBufferPrevReadPos;
    tjs_int SoundBufferWritePos;
    tjs_int PlayStopPos; // in bytes

    tjs_int L2BufferReadPos; // in unit
    tjs_int L2BufferWritePos;
    tjs_int L2BufferRemain;
    bool L2BufferEnded;
    tjs_uint8* VisBuffer; // buffer for visualization
    tjs_int* L2BufferDecodedSamplesInUnit;
    tTVPWaveSegmentQueue* L1BufferSegmentQueues;
    std::vector<tTVPWaveLabel> LabelEventQueue;
    tjs_int64* L1BufferDecodeSamplePos;
    tTVPWaveSegmentQueue* L2BufferSegmentQueues;

    tjs_int64 DecodePos;            // decoded samples from directsound buffer play
    tjs_int64 LastCheckedDecodePos; // last sured position (-1 for not checked) and
    tjs_uint64 LastCheckedTick;     // last sured tick time

    bool Looping;

    void Clear();

    tjs_uint Decode(void* buffer, tjs_uint bufsamplelen, tTVPWaveSegmentQueue& segments);

public:
    bool FillL2Buffer(bool firstwrite, bool fromdecodethread);

private:
    void PrepareToReadL2Buffer(bool firstread);
    tjs_uint ReadL2Buffer(void* buffer, tTVPWaveSegmentQueue& segments);

    void FillDSBuffer(tjs_int writepos, tTVPWaveSegmentQueue& segments);

public:
    bool FillBuffer(bool firstwrite = false, bool allowpause = true);

private:
    void ResetLastCheckedDecodePos(uint32_t pp = (uint32_t)-1);

public:
    tjs_int FireLabelEventsAndGetNearestLabelEventStep(tjs_int64 tick);
    tjs_int GetNearestEventStep();
    void FlushAllLabelEvents();

private:
    void StartPlay();
    void StopPlay();

public:
    void Play();
    void Stop();
    void SetBufferPaused(bool bPaused);

    bool GetPaused() const { return Paused; }
    void SetPaused(bool b);

    tjs_int GetBitsPerSample() const { return InputFormat.BitsPerSample; }
    tjs_int GetChannels() const { return InputFormat.Channels; }

protected:
    void TimerBeatHandler(); // override

public:
    void Open(const ttstr& storagename);

public:
    void SetLooping(bool b);
    bool GetLooping() const { return Looping; }

    tjs_uint64 GetSamplePosition();
    void SetSamplePosition(tjs_uint64 pos);

    tjs_uint64 GetPosition();
    void SetPosition(tjs_uint64 pos);

    tjs_uint64 GetTotalTime();

    //-- volume/pan/3D position/freq stuff -------------------------------------
private:
    tjs_int Volume;
    tjs_int Volume2;
    tjs_int Frequency;
    static tjs_int GlobalVolume;
    static tTVPSoundGlobalFocusMode GlobalFocusMode;

    bool BufferCanControlPan;
    bool BufferCanControlFrequency;
    tjs_int Pan;               // -100000 .. 0 .. 100000
    D3DVALUE PosX, PosY, PosZ; // 3D position

public:
    void SetVolumeToSoundBuffer();

public:
    void SetVolume(tjs_int v);
    tjs_int GetVolume() const { return Volume; }
    void SetVolume2(tjs_int v);
    tjs_int GetVolume2() const { return Volume2; }
    void SetPan(tjs_int v);
    tjs_int GetPan() const { return Pan; }
    static void SetGlobalVolume(tjs_int v);
    static tjs_int GetGlobalVolume() { return GlobalVolume; }
    static void SetGlobalFocusMode(tTVPSoundGlobalFocusMode b);
    static tTVPSoundGlobalFocusMode GetGlobalFocusMode() { return GlobalFocusMode; }

private:
    void Set3DPositionToBuffer();

public:
    void SetPos(D3DVALUE x, D3DVALUE y, D3DVALUE z);
    void SetPosX(D3DVALUE v);
    D3DVALUE GetPosX() const { return PosX; }
    void SetPosY(D3DVALUE v);
    D3DVALUE GetPosY() const { return PosY; }
    void SetPosZ(D3DVALUE v);
    D3DVALUE GetPosZ() const { return PosZ; }

private:
    void SetFrequencyToBuffer();

public:
    tjs_int GetFrequency() const { return Frequency; }
    void SetFrequency(tjs_int freq);

    //-- visualization stuff ----------------------------------------------
public:
    void SetUseVisBuffer(bool b);
    bool GetUseVisBuffer() const { return UseVisBuffer; }

protected:
    void ResetVisBuffer(); // reset or recreate visualication buffer
    void DeallocateVisBuffer();

    void CopyVisBuffer(tjs_int16* dest, const tjs_uint8* src, tjs_int numsamples, tjs_int channels);

public:
    tjs_int GetVisBuffer(tjs_int16* dest,
                         tjs_int numsamples,
                         tjs_int channels,
                         tjs_int aheadsamples);
};

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNI_WaveFlags : Wave Flags object
//---------------------------------------------------------------------------
class tTJSNI_WaveSoundBuffer;
class tTJSNI_WaveFlags : public tTJSNativeInstance
{
    typedef tTJSNativeInstance inherited;

    tTJSNI_WaveSoundBuffer* Buffer;

public:
    tTJSNI_WaveFlags();
    ~tTJSNI_WaveFlags();
    tjs_error TJS_INTF_METHOD Construct(tjs_int numparams,
                                        tTJSVariant** param,
                                        iTJSDispatch2* tjs_obj);
    void TJS_INTF_METHOD Invalidate();

    tTJSNI_WaveSoundBuffer* GetBuffer() const { return Buffer; }
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_WaveFlags : Wave Flags class
//---------------------------------------------------------------------------
class tTJSNC_WaveFlags : public tTJSNativeClass
{
public:
    tTJSNC_WaveFlags();
    static tjs_uint32 ClassID;

protected:
    tTJSNativeInstance* CreateNativeInstance() { return new tTJSNI_WaveFlags(); }
};
//---------------------------------------------------------------------------
iTJSDispatch2* TVPCreateWaveFlagsObject(iTJSDispatch2* buffer);
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_WaveSoundBuffer : TJS WaveSoundBuffer class
//---------------------------------------------------------------------------
class tTJSNC_WaveSoundBuffer : public tTJSNativeClass
{
public:
	tTJSNC_WaveSoundBuffer();
	static tjs_uint32 ClassID;

protected:
	tTJSNativeInstance* CreateNativeInstance();
};
//---------------------------------------------------------------------------
extern tTJSNativeClass* TVPCreateNativeClass_WaveSoundBuffer();
//---------------------------------------------------------------------------