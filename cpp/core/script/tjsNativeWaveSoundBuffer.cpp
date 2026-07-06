#include "tjsCommHead.h"
#include "tjsNativeWaveSoundBuffer.h"

#include "WaveIntf.h"
#include "TVPEvent.h"
#include "TVPMsg.h"
#include "TVPTimer.h"
#include "WaveLoopManager.h"
#include "tjsArray.h"
#include "tjsDictionary.h"
#include "TVPThread.h"
#include "TVPSystem.h"
#include "NativeEventQueue.h"
#include "Random.h"
#include "TickCount.h"
#include "Platform.h"

#include "WaveMixer.h"

#define TVP_SB_BEAT_INTERVAL 60

//---------------------------------------------------------------------------
// tTJSNI_SoundBuffer
//---------------------------------------------------------------------------
tTJSNI_BaseSoundBuffer::tTJSNI_BaseSoundBuffer()
{
    InFading = false;
    CanDeliverEvents = true;
    Owner = NULL;
    ActionOwner.Object = ActionOwner.ObjThis = NULL;
    Status = ssUnload;
}
//---------------------------------------------------------------------------
tjs_error tTJSNI_BaseSoundBuffer::Construct(tjs_int numparams,
                                            tTJSVariant** param,
                                            iTJSDispatch2* tjs_obj)
{
    if (numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    tjs_error hr = inherited::Construct(numparams, param, tjs_obj);
    if (TJS_FAILED(hr))
        return hr;

    ActionOwner = param[0]->AsObjectClosure();
    Owner = tjs_obj;

    return TJS_S_OK;
}
//---------------------------------------------------------------------------
void tTJSNI_BaseSoundBuffer::Invalidate()
{
    CanDeliverEvents = false;
    TVPCancelSourceEvents(Owner);
    Owner = NULL;

    ActionOwner.Release();
    ActionOwner.ObjThis = ActionOwner.Object = NULL;

    inherited::Invalidate();
}
//---------------------------------------------------------------------------
ttstr tTJSNI_BaseSoundBuffer::GetStatusString() const
{
    static ttstr unload(TJS_N("unload"));
    static ttstr play(TJS_N("play"));
    static ttstr stop(TJS_N("stop"));
    static ttstr unknown(TJS_N("unknown"));

    switch (Status)
    {
        case ssUnload:
            return unload;
        case ssPlay:
            return play;
        case ssStop:
            return stop;
        default:
            return unknown;
    }
}
//---------------------------------------------------------------------------
void tTJSNI_BaseSoundBuffer::SetStatus(tTVPSoundStatus s)
{
    if (Status != s)
    {
        Status = s;

        if (Owner)
        {
            // Cancel Previous un-delivered Events
            TVPCancelSourceEvents(Owner);

            // fire
            if (CanDeliverEvents)
            {
                // fire onStatusChanged event
                tTJSVariant param(GetStatusString());
                static ttstr eventname(TJS_N("onStatusChanged"));
                TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 1, &param);
            }
        }
    }
}
//---------------------------------------------------------------------------
void tTJSNI_BaseSoundBuffer::SetStatusAsync(tTVPSoundStatus s)
{
    // asynchronous version of SetStatus
    // the event may not be delivered immediately.

    if (Status != s)
    {
        Status = s;

        if (CanDeliverEvents)
        {
            // fire onStatusChanged event
            if (Owner)
            {
                tTJSVariant param(GetStatusString());
                static ttstr eventname(TJS_N("onStatusChanged"));
                TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_POST, 1, &param);
            }
        }
    }
}
//---------------------------------------------------------------------------
void tTJSNI_BaseSoundBuffer::TimerBeatHandler()
{
    // fade handling

    if (!Owner)
        return; // "Owner" indicates the owner object is valid

    if (!InFading)
        return;

    if (BlankLeft)
    {
        BlankLeft -= TVP_SB_BEAT_INTERVAL;
        if (BlankLeft < 0)
            BlankLeft = 0;
    }
    else if (FadeCount)
    {
        if (FadeCount == 1)
        {
            StopFade(true, true);
        }
        else
        {
            FadeCount--;
            tjs_int v;
            v = GetVolume();
            v += DeltaVolume;
            if (v < 0)
                v = 0;
            if (v > 100000)
                v = 100000;
            SetVolume(v);
        }
    }
}
//---------------------------------------------------------------------------
void tTJSNI_BaseSoundBuffer::Fade(tjs_int to, tjs_int time, tjs_int blanktime)
{
    // start fading

    if (!Owner)
        return;

    if (time <= 0 || blanktime < 0)
    {
        TVPThrowExceptionMessage(TVPInvalidParam);
    }

    // stop current fade
    if (InFading)
        StopFade(false, false);

    // set some parameters
    DeltaVolume = (to - GetVolume()) * TVP_SB_BEAT_INTERVAL / time;
    TargetVolume = to;
    FadeCount = time / TVP_SB_BEAT_INTERVAL;
    BlankLeft = blanktime;
    InFading = true;
    if (FadeCount == 0 && BlankLeft == 0)
        StopFade(false, true);
}
//---------------------------------------------------------------------------
void tTJSNI_BaseSoundBuffer::StopFade(bool async, bool settargetvol)
{
    // stop fading

    if (!Owner)
        return;

    if (InFading)
    {
        InFading = false;

        if (settargetvol)
            SetVolume(TargetVolume);

        // post "onFadeCompleted" event to the owner
        if (CanDeliverEvents)
        {
            static ttstr eventname(TJS_N("onFadeCompleted"));
            TVPPostEvent(Owner, Owner, eventname, 0, async ? TVP_EPT_POST : TVP_EPT_IMMEDIATE, 0,
                         NULL);
        }
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Sound Buffer Timer Dispatcher ( for fading or commiting defered settings )
//---------------------------------------------------------------------------
static class TVPTimer* TVPSoundBufferTimer = NULL;
static std::vector<tTJSNI_SoundBuffer*> TVPSoundBufferVector;
//---------------------------------------------------------------------------
class tTVPSoundBufferTimerDispatcher
{
public:
    void Handler()
    {
        std::vector<tTJSNI_SoundBuffer*>::iterator i;
        for (i = TVPSoundBufferVector.begin(); i != TVPSoundBufferVector.end(); i++)
        {
            (*i)->TimerBeatHandler();
        }
        TVPWaveSoundBufferCommitSettings();
    }
} static TVPSoundBufferTimerDispatcher;
//---------------------------------------------------------------------------
void TVPAddSoundBuffer(tTJSNI_SoundBuffer* buf)
{
    if (TVPSoundBufferVector.size() == 0)
    {
        // first buffer
        TVPSoundBufferTimer = new TVPTimer(); // Create Timer Object
        TVPSoundBufferTimer->SetInterval(TVP_SB_BEAT_INTERVAL);
        TVPSoundBufferTimer->SetOnTimerHandler(&TVPSoundBufferTimerDispatcher,
                                               &tTVPSoundBufferTimerDispatcher::Handler);
    }

    TVPSoundBufferVector.push_back(buf);
}
//---------------------------------------------------------------------------
void TVPRemoveSoundBuffer(tTJSNI_SoundBuffer* buf)
{
    if (TVPSoundBufferVector.size() != 0)
    {
        std::vector<tTJSNI_SoundBuffer*>::iterator i;
        i = std::find(TVPSoundBufferVector.begin(), TVPSoundBufferVector.end(), buf);
        if (i != TVPSoundBufferVector.end())
        {
            TVPSoundBufferVector.erase(i);
        }
    }

    if (TVPSoundBufferVector.size() == 0)
    {
        // all buffer was removed
        delete TVPSoundBufferTimer;
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNI_SoundBuffer
//---------------------------------------------------------------------------
tTJSNI_SoundBuffer::tTJSNI_SoundBuffer()
{
}
//---------------------------------------------------------------------------
tjs_error tTJSNI_SoundBuffer::Construct(tjs_int numparams,
                                        tTJSVariant** param,
                                        iTJSDispatch2* tjs_obj)
{
    tjs_error hr = inherited::Construct(numparams, param, tjs_obj);
    if (TJS_FAILED(hr))
        return hr;

    TVPAddSoundBuffer(this);

    return TJS_S_OK;
}
//---------------------------------------------------------------------------
void tTJSNI_SoundBuffer::Invalidate()
{
    TVPRemoveSoundBuffer(this);

    inherited::Invalidate();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// CPU specific optimized routine prototypes
//---------------------------------------------------------------------------
/**
 * int16→float32変換
 */
static void PCMConvertLoopInt16ToFloat32_c(void* dest, const void* src, size_t numsamples)
{
    float* d = static_cast<float*>(dest);
    const tjs_int16* s = static_cast<const tjs_int16*>(src);
    const tjs_int16* s_lim = s + numsamples;

    while (s < s_lim)
    {
        *d = *s * (1.0f / 32768.0f);
        d += 1;
        s += 1;
    }
}
//---------------------------------------------------------------------------
/**
 * float32→int16変換
 */
static void PCMConvertLoopFloat32ToInt16_c(void* dest, const void* src, size_t numsamples)
{
    tjs_uint16* d = static_cast<tjs_uint16*>(dest);
    const float* s = static_cast<const float*>(src);
    const float* s_lim = s + numsamples;

    while (s < s_lim)
    {
        float v = *s * 32767.0f;
        *d = v > (float)32767    ? 32767
             : v < (float)-32768 ? -32768
             : v < 0             ? (tjs_int16)(v - 0.5)
                                 : (tjs_int16)(v + 0.5);
        d += 1;
        s += 1;
    }
}
//---------------------------------------------------------------------------

void (*PCMConvertLoopInt16ToFloat32)(void* dest,
                                     const void* src,
                                     size_t numsamples) = PCMConvertLoopInt16ToFloat32_c;
void (*PCMConvertLoopFloat32ToInt16)(void* dest,
                                     const void* src,
                                     size_t numsamples) = PCMConvertLoopFloat32ToInt16_c;

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Wave format convertion routines
//---------------------------------------------------------------------------
static void TVPConvertFloatPCMTo16bits(
    tjs_int16* output, const float* input, tjs_int channels, tjs_int count, bool downmix)
{
    // convert 32bit float to 16bit integer

    // float PCM is in range of +1.0 ... 0 ... -1.0
    // clip sample which is out of the range.

    if (!downmix)
    {
        tjs_int total = channels * count;
        PCMConvertLoopFloat32ToInt16(output, input, total);
    }
    else
    {
        float nc = 32768.0f / (float)channels;
        while (count--)
        {
            tjs_int n = channels;
            float t = 0;
            while (n--)
                t += *(input++) * nc;
            if (t > 0)
            {
                int i = (int)(t + 0.5);
                if (i > 32767)
                    i = 32767;
                *(output++) = (tjs_int16)i;
            }
            else
            {
                int i = (int)(t - 0.5);
                if (i < -32768)
                    i = -32768;
                *(output++) = (tjs_int16)i;
            }
        }
    }
}
//---------------------------------------------------------------------------
static void TVPConvertIntegerPCMTo16bits(tjs_int16* output,
                                         const void* input,
                                         tjs_int bytespersample,
                                         tjs_int validbits,
                                         tjs_int channels,
                                         tjs_int count,
                                         bool downmix)
{
    // convert integer PCMs to 16bit integer PCM
#define PROCESS_BY_CHANNELS \
    switch (channels) \
    { \
        case 2: \
            PROCESS(2); \
            break; \
        case 4: \
            PROCESS(4); \
            break; \
        case 8: \
            PROCESS(8); \
            break; \
        default: \
            PROCESS(channels); \
            break; \
    }

#if TJS_HOST_IS_BIG_ENDIAN
#define GET_24BIT(p) (p[2] + (p[1] << 8) + (p[0] << 16))
#else
#define GET_24BIT(p) (p[0] + (p[1] << 8) + (p[2] << 16))
#endif

    if (bytespersample == 1)
    {
        // here assumes that the input 8bit PCM has always 8bit valid data
        const tjs_int8* p = (tjs_int8*)input;
        if (!downmix || channels == 1)
        {
            tjs_int total = channels * count;
            while (total--)
                *(output++) = (tjs_int16)(((tjs_int) * (p++) - 0x80) * 0x100);
        }
        else
        {
#define PROCESS(channels) \
    while (count--) \
    { \
        tjs_int v = 0; \
        tjs_int n = channels; \
        while (n--) \
            v += (tjs_int16)(((tjs_int) * (p++) - 0x80) * 0x100); \
        v = v / channels; \
        *(output++) = (tjs_int16)v; \
    }
            PROCESS_BY_CHANNELS
#undef PROCESS
        }
    }
    else if (bytespersample == 2)
    {
        tjs_uint16 mask = ~((1 << (16 - validbits)) - 1);
        const tjs_int16* p = (const tjs_int16*)input;
        if (!downmix || channels == 1)
        {
            tjs_int total = channels * count;
            while (total--)
                *(output++) = (tjs_int16)(*(p++) & mask);
        }
        else
        {
#define PROCESS(channels) \
    while (count--) \
    { \
        tjs_int v = 0; \
        tjs_int n = channels; \
        while (n--) \
            v += (tjs_int16)(*(p++) & mask); \
        v = v / channels; \
        *(output++) = (tjs_int16)v; \
    }
            PROCESS_BY_CHANNELS
#undef PROCESS
        }
    }
    else if (bytespersample == 3)
    {
        tjs_uint32 mask = ~((1 << (24 - validbits)) - 1);
        const tjs_uint8* p = (const tjs_uint8*)input;

        if (!downmix || channels == 1)
        {
            tjs_int total = channels * count;
            while (total--)
            {
                tjs_int32 t = GET_24BIT(p);
                p += 3;
                t |= -(t & 0x800000); // extend sign
                t &= mask;            // apply mask
                t >>= 8;
                *(output++) = (tjs_int16)t;
            }
        }
        else
        {
#define PROCESS(channels) \
    while (count--) \
    { \
        tjs_int v = 0; \
        tjs_int n = channels; \
        while (n--) \
        { \
            tjs_int32 t = GET_24BIT(p); \
            p += 3; \
            t |= -(t & 0x800000); \
            t &= mask; \
            t >>= 8; \
            v += t; \
        } \
        v = v / channels; \
        *(output++) = (tjs_int16)v; \
    }
            PROCESS_BY_CHANNELS
#undef PROCESS
        }
    }
    else if (bytespersample == 4)
    {
        tjs_int32 mask = ~((1 << (32 - validbits)) - 1);
        const tjs_int32* p = (const tjs_int32*)input;
        if (!downmix || channels == 1)
        {
            tjs_int total = channels * count;
            while (total--)
                *(output++) = (tjs_int16)((*(p++) & mask) >> 16);
        }
        else
        {
#define PROCESS(channels) \
    while (count--) \
    { \
        tjs_int v = 0; \
        tjs_int n = channels; \
        while (n--) \
            v += (tjs_int16)((*(p++) & mask) >> 16); \
        v = v / channels; \
        *(output++) = (tjs_int16)v; \
    }
            PROCESS_BY_CHANNELS
#undef PROCESS
        }
    }

#undef PROCESS_BY_CHANNELS
#undef GET_24BIT
}
//---------------------------------------------------------------------------
void TVPConvertPCMTo16bits(tjs_int16* output,
                           const void* input,
                           tjs_int channels,
                           tjs_int bytespersample,
                           tjs_int bitspersample,
                           bool isfloat,
                           tjs_int count,
                           bool downmix)
{
    // cconvert specified format to 16bit PCM

    if (isfloat)
        TVPConvertFloatPCMTo16bits(output, (const float*)input, channels, count, downmix);
    else
        TVPConvertIntegerPCMTo16bits(output, input, bytespersample, bitspersample, channels, count,
                                     downmix);
}
//---------------------------------------------------------------------------
void TVPConvertPCMTo16bits(
    tjs_int16* output, const void* input, const tTVPWaveFormat& format, tjs_int count, bool downmix)
{
    // cconvert specified format to 16bit PCM
    TVPConvertPCMTo16bits(output, input, format.Channels, format.BytesPerSample,
                          format.BitsPerSample, format.IsFloat, count, downmix);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
static void TVPConvertFloatPCMToFloat(float* output,
                                      const float* input,
                                      tjs_int channels,
                                      tjs_int count)
{
    // convert 32bit float to float

    // yes, acctually, this does nothing.
    memcpy(output, input, sizeof(float) * channels * count);
}
//---------------------------------------------------------------------------
static void TVPConvertIntegerPCMToFloat(float* output,
                                        const void* input,
                                        tjs_int bytespersample,
                                        tjs_int validbits,
                                        tjs_int channels,
                                        tjs_int count)
{
    // convert integer PCMs to float PCM

#ifdef TJS_HOST_IS_BIG_ENDIAN
#undef TJS_HOST_IS_BIG_ENDIAN
#endif
#if TJS_HOST_IS_BIG_ENDIAN
#define GET_24BIT(p) (p[2] + (p[1] << 8) + (p[0] << 16))
#else
#define GET_24BIT(p) (p[0] + (p[1] << 8) + (p[2] << 16))
#endif

    if (bytespersample == 1)
    {
        // here assumes that the input 8bit PCM has always 8bit valid data
        const tjs_int8* p = (tjs_int8*)input;
        tjs_int total = channels * count;
        while (total--)
            *(output++) = (float)(((tjs_int) * (p++) - 0x80) * (1.0 / 128));
    }
    else if (bytespersample == 2)
    {
        const tjs_int16* p = (const tjs_int16*)input;
        tjs_int total = channels * count;

        if (validbits == 16)
        {
            PCMConvertLoopInt16ToFloat32(output, p, total);
        }
        else
        {
            // generic
            tjs_uint16 mask = ~((1 << (16 - validbits)) - 1);

            while (total--)
                *(output++) = (float)((*(p++) & mask) * (1.0 / 32768));
        }
    }
    else if (bytespersample == 3)
    {
        tjs_uint32 mask = ~((1 << (24 - validbits)) - 1);
        const tjs_uint8* p = (const tjs_uint8*)input;

        tjs_int total = channels * count;
        while (total--)
        {
            tjs_int32 t = GET_24BIT(p);
            p += 3;
            t |= -(t & 0x800000); // extend sign
            t &= mask;            // apply mask
            *(output++) = (float)(t * (1.0 / (1 << 23)));
        }
    }
    else if (bytespersample == 4)
    {
        tjs_int32 mask = ~((1 << (32 - validbits)) - 1);
        const tjs_int32* p = (const tjs_int32*)input;
        tjs_int total = channels * count;
        while (total--)
            *(output++) = (float)(((*(p++) & mask) >> 0) * (1.0 / (1 << 31)));
    }
}
//---------------------------------------------------------------------------
void TVPConvertPCMToFloat(float* output,
                          const void* input,
                          tjs_int channels,
                          tjs_int bytespersample,
                          tjs_int bitspersample,
                          bool isfloat,
                          tjs_int count)
{
    // cconvert specified format to 16bit PCM

    if (isfloat)
        TVPConvertFloatPCMToFloat(output, (const float*)input, channels, count);
    else
        TVPConvertIntegerPCMToFloat(output, input, bytespersample, bitspersample, channels, count);
}
//---------------------------------------------------------------------------
void TVPConvertPCMToFloat(float* output,
                          const void* input,
                          const tTVPWaveFormat& format,
                          tjs_int count)
{
    // cconvert specified format to 16bit PCM
    TVPConvertPCMToFloat(output, input, format.Channels, format.BytesPerSample,
                         format.BitsPerSample, format.IsFloat, count);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// sound format convert filter
//---------------------------------------------------------------------------
class iSoundBufferConvertToPCM16
  : public tTJSDispatch, // could be hold by tFilterObjectAndInterface
    public iTVPBasicWaveFilter,
    public tTVPSampleAndLabelSource
{
protected:
    tTVPSampleAndLabelSource* Source; // source filter
    tTVPWaveFormat OutputFormat;

public:
    virtual tTVPSampleAndLabelSource* Recreate(tTVPSampleAndLabelSource* source)
    {
        Source = source;
        OutputFormat = source->GetFormat();
        OutputFormat.IsFloat = false;
        OutputFormat.BitsPerSample = 16;
        OutputFormat.BytesPerSample = 2;
        return this;
    }
    virtual ~iSoundBufferConvertToPCM16() {}
    virtual void Clear(void) { Source = nullptr; }
    virtual void Update(void){};
    virtual void Reset(void){};

    virtual const tTVPWaveFormat& GetFormat() const { return OutputFormat; }
};
//---------------------------------------------------------------------------
class SoundBufferConvertFloatToPCM16 : public iSoundBufferConvertToPCM16
{
    std::vector<float> buffer;
    virtual void Decode(void* dest,
                        tjs_uint samples,
                        tjs_uint& written,
                        tTVPWaveSegmentQueue& segments)
    {
        tjs_uint numsamples = samples * OutputFormat.Channels;
        if (buffer.size() < numsamples)
            buffer.resize(numsamples);
        float* p = &buffer[0];
        Source->Decode(p, samples, written, segments);
        numsamples = written * OutputFormat.Channels;
        PCMConvertLoopFloat32ToInt16(dest, p, numsamples);
    }
};
//---------------------------------------------------------------------------
class SoundBufferConvertPCM24ToPCM16 : public iSoundBufferConvertToPCM16
{
    std::vector<char> buffer;
    virtual void Decode(void* dest,
                        tjs_uint samples,
                        tjs_uint& written,
                        tTVPWaveSegmentQueue& segments)
    {
        tjs_uint numsamples = samples * OutputFormat.Channels;
        if (buffer.size() < numsamples * 3)
            buffer.resize(numsamples * 3);
        char* p = &buffer[0];
        int16_t* d = (int16_t*)dest;
        Source->Decode(p, samples, written, segments);
        numsamples = written * OutputFormat.Channels;
        for (int i = 0; i < numsamples; ++i)
        {
            d[i] = *(int16_t*)(p + 1);
            p += 3;
        }
    }
};
//---------------------------------------------------------------------------
class SoundBufferConvertPCM32ToPCM16 : public iSoundBufferConvertToPCM16
{
    std::vector<int32_t> buffer;
    virtual void Decode(void* dest,
                        tjs_uint samples,
                        tjs_uint& written,
                        tTVPWaveSegmentQueue& segments)
    {
        tjs_uint numsamples = samples * OutputFormat.Channels;
        if (buffer.size() < numsamples)
            buffer.resize(numsamples);
        char* p = (char*)&buffer[0];
        int16_t* d = (int16_t*)dest;
        Source->Decode(p, samples, written, segments);
        numsamples = written * OutputFormat.Channels;
        for (int i = 0; i < numsamples; ++i)
        {
            d[i] = *(int16_t*)(p + 2);
            p += 4;
        }
    }
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNI_BaseWaveSoundBuffer
//---------------------------------------------------------------------------
tTJSNI_BaseWaveSoundBuffer::tTJSNI_BaseWaveSoundBuffer()
{
    LoopManager = NULL;
    WaveFlagsObject = NULL;
    WaveLabelsObject = NULL;
    Filters = TJSCreateArrayObject();
}
//---------------------------------------------------------------------------
tjs_error tTJSNI_BaseWaveSoundBuffer::Construct(tjs_int numparams,
                                                tTJSVariant** param,
                                                iTJSDispatch2* tjs_obj)
{
    tjs_error hr = inherited::Construct(numparams, param, tjs_obj);
    if (TJS_FAILED(hr))
        return hr;

    return TJS_S_OK;
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWaveSoundBuffer::Invalidate()
{
    // invalidate wave flags object
    RecreateWaveLabelsObject();

    // release filter arrays
    if (Filters)
        Filters->Release(), Filters = NULL;

    inherited::Invalidate();
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWaveSoundBuffer::InvokeLabelEvent(const ttstr& name)
{
    // the invoked event is to be delivered asynchronously.
    // the event is to be erased when the SetStatus is called, but it's ok.
    // ( SetStatus calls TVPCancelSourceEvents(Owner); )

    if (Owner && CanDeliverEvents)
    {
        tTJSVariant param(name);
        static ttstr eventname(TJS_N("onLabel"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_POST, 1, &param);
    }
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWaveSoundBuffer::RecreateWaveLabelsObject()
{
    // indicate recreating WaveLabelsObject
    if (WaveLabelsObject)
    {
        WaveLabelsObject->Invalidate(0, NULL, NULL, WaveLabelsObject);
        WaveLabelsObject->Release();
        WaveLabelsObject = NULL;
    }
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWaveSoundBuffer::RebuildFilterChain()
{
    // rebuild filter array
    FilterInterfaces.clear();

    // get filter count
    tTJSVariant v;
    tjs_int count = 0;
    Filters->PropGet(0, TJS_N("count"), NULL, &v, Filters);
    count = v;

    // reset filter output
    FilterOutput = LoopManager;

    // for each filter ...
    for (int i = 0; i < count; i++)
    {
        Filters->PropGetByNum(0, i, &v, Filters);

        // get iTVPBasicWaveFilter interface
        tTJSVariantClosure clo = v.AsObjectClosureNoAddRef();
        tTJSVariant iface_v;
        if (TJS_FAILED(clo.PropGet(0, TJS_N("interface"), NULL, &iface_v, NULL)))
            continue;
        iTVPBasicWaveFilter* filter =
            reinterpret_cast<iTVPBasicWaveFilter*>((tjs_intptr_t)(tjs_int64)iface_v);
        // save to the backupped array
        FilterInterfaces.emplace_back(v, filter);
    }

    // reset filter output
    FilterOutput = LoopManager;

    // for each filter ...
    for (std::vector<tFilterObjectAndInterface>::iterator i = FilterInterfaces.begin();
         i != FilterInterfaces.end(); i++)
    {
        // recreate filter
        FilterOutput = i->Interface->Recreate(FilterOutput);
    }

    const tTVPWaveFormat& filteredFormat = FilterOutput->GetFormat();
    if (filteredFormat.IsFloat)
    {
        SoundBufferConvertFloatToPCM16* filter = new SoundBufferConvertFloatToPCM16;
        FilterInterfaces.emplace_back(filter, filter);
        FilterOutput = filter->Recreate(FilterOutput);
        filter->Release();
    }
    else if (filteredFormat.BitsPerSample == 24)
    {
        SoundBufferConvertPCM24ToPCM16* filter = new SoundBufferConvertPCM24ToPCM16;
        FilterInterfaces.emplace_back(filter, filter);
        FilterOutput = filter->Recreate(FilterOutput);
        filter->Release();
    }
    else if (filteredFormat.BitsPerSample == 32)
    {
        SoundBufferConvertPCM32ToPCM16* filter = new SoundBufferConvertPCM32ToPCM16;
        FilterInterfaces.emplace_back(filter, filter);
        FilterOutput = filter->Recreate(FilterOutput);
        filter->Release();
    }
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWaveSoundBuffer::ClearFilterChain()
{
    // delete object which is created from this filter array

    // reset filter output
    FilterOutput = NULL;

    for (std::vector<tFilterObjectAndInterface>::iterator i = FilterInterfaces.begin();
         i != FilterInterfaces.end(); i++)
    {
        // recreate filter
        i->Interface->Clear();
    }

    // clear backupped filter array
    FilterInterfaces.clear();
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWaveSoundBuffer::ResetFilterChain()
{
    // Reset filter chain.
    for (std::vector<tFilterObjectAndInterface>::iterator i = FilterInterfaces.begin();
         i != FilterInterfaces.end(); i++)
        i->Interface->Reset();
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWaveSoundBuffer::UpdateFilterChain()
{
    // Update filter chain.
    // This method is called before that the player is about to decode a small
    // PCM unit (typically piece of 125ms of sound).
    // Note that this method may be called by decoding thread,
    // but it's guaranteed that the call is never overlapped with
    // UpdateFilterChain self and ClearFilterChain and RebuildFilterChain.
    // so we does not need to protect this call by CriticalSection.
    for (std::vector<tFilterObjectAndInterface>::iterator i = FilterInterfaces.begin();
         i != FilterInterfaces.end(); i++)
        i->Interface->Update();
}
//---------------------------------------------------------------------------
iTJSDispatch2* tTJSNI_BaseWaveSoundBuffer::GetWaveFlagsObjectNoAddRef()
{
    if (WaveFlagsObject)
        return WaveFlagsObject;

    if (!Owner)
        TVPThrowInternalError;

    WaveFlagsObject = TVPCreateWaveFlagsObject(Owner);

    return WaveFlagsObject;
}
//---------------------------------------------------------------------------
iTJSDispatch2* tTJSNI_BaseWaveSoundBuffer::GetWaveLabelsObjectNoAddRef()
{
    if (WaveLabelsObject)
        return WaveLabelsObject;

    // build label dictionay from WaveLoopManager
    WaveLabelsObject = TJSCreateDictionaryObject();

    if (LoopManager)
    {
        const std::vector<tTVPWaveLabel>& labels = LoopManager->GetLabels();

        int freq = FilterOutput->GetFormat().SamplesPerSec;

        int count = 0;
        for (std::vector<tTVPWaveLabel>::const_iterator i = labels.begin(); i != labels.end();
             i++, count++)
        {
            iTJSDispatch2* item_dic = TJSCreateDictionaryObject();
            try
            {
                tTJSVariant val;
                val = i->Name.c_str(); // c_str() to avoid race condition for ttstr
                item_dic->PropSet(TJS_MEMBERENSURE, TJS_N("name"), NULL, &val, item_dic);
                val = i->Position;
                item_dic->PropSet(TJS_MEMBERENSURE, TJS_N("samplePosition"), NULL, &val, item_dic);
                val = freq ? i->Position * 1000 / freq : 0;
                item_dic->PropSet(TJS_MEMBERENSURE, TJS_N("position"), NULL, &val, item_dic);

                tTJSVariant item_dic_var(item_dic, item_dic);

                if (!i->Name.IsEmpty())
                    WaveLabelsObject->PropSet(TJS_MEMBERENSURE, i->Name.c_str(), NULL,
                                              &item_dic_var, WaveLabelsObject);
            }
            catch (...)
            {
                item_dic->Release();
                throw;
            }
            item_dic->Release();
        }
    }

    return WaveLabelsObject;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNI_WaveFlags : Wave Flags object
//---------------------------------------------------------------------------
tTJSNI_WaveFlags::tTJSNI_WaveFlags()
{
    Buffer = NULL;
}
//---------------------------------------------------------------------------
tTJSNI_WaveFlags::~tTJSNI_WaveFlags()
{
}
//---------------------------------------------------------------------------
tjs_error tTJSNI_WaveFlags::Construct(tjs_int numparams,
                                      tTJSVariant** param,
                                      iTJSDispatch2* tjs_obj)
{
    if (numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    iTJSDispatch2* dsp = param[0]->AsObjectNoAddRef();

    tTJSNI_WaveSoundBuffer* buffer = NULL;
    if (TJS_FAILED(dsp->NativeInstanceSupport(TJS_NIS_GETINSTANCE, tTJSNC_WaveSoundBuffer::ClassID,
                                              (iTJSNativeInstance**)&buffer)))
        TVPThrowInternalError;

    Buffer = buffer;

    return TJS_S_OK;
}
//---------------------------------------------------------------------------
void tTJSNI_WaveFlags::Invalidate()
{
    Buffer = NULL;

    inherited::Invalidate();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_WaveFlags : Wave Flags class
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_WaveFlags::ClassID = -1;
tTJSNC_WaveFlags::tTJSNC_WaveFlags()
  : tTJSNativeClass(TJS_N("WaveFlags")){
        TJS_BEGIN_NATIVE_MEMBERS(WaveFlags) // constructor
        TJS_DECL_EMPTY_FINALIZE_METHOD
            //----------------------------------------------------------------------
            TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(/*var.name*/ _this,
                                              /*var.type*/ tTJSNI_WaveFlags,
                                              /*TJS class name*/ WaveFlags){return TJS_S_OK;
}
TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/ WaveFlags)
//----------------------------------------------------------------------

//-- methods

//---------------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ reset)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveFlags);

    tTVPWaveLoopManager* manager =
        _this->GetBuffer()->GetWaveLoopManager(); /* note that the manager can be null */

    if (manager)
        manager->ClearFlags();

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ reset)
//----------------------------------------------------------------------

//-- properties

//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(count){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveFlags);
*result = TVP_WL_MAX_FLAGS; // always this value
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(count)
//----------------------------------------------------------------------
#define TVP_DEFINE_FLAG_PROP(N) \
    TJS_BEGIN_NATIVE_PROP_DECL(N){TJS_BEGIN_NATIVE_PROP_GETTER{ \
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveFlags); \
    tTVPWaveLoopManager* manager = \
        _this->GetBuffer()->GetWaveLoopManager(); /* note that the manager can be null */ \
    if (manager) \
        *result = manager->GetFlag(N); \
    else \
        *result = (tjs_int)0; \
    return TJS_S_OK; \
    } \
    TJS_END_NATIVE_PROP_GETTER \
\
    TJS_BEGIN_NATIVE_PROP_SETTER \
    { \
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveFlags); \
        tTVPWaveLoopManager* manager = \
            _this->GetBuffer()->GetWaveLoopManager(); /* note that the manager can be null */ \
        if (manager) \
            manager->SetFlag(N, (tjs_int) * param); \
        return TJS_S_OK; \
    } \
    TJS_END_NATIVE_PROP_SETTER \
    } \
    TJS_END_NATIVE_PROP_DECL(N)

TVP_DEFINE_FLAG_PROP(0)
TVP_DEFINE_FLAG_PROP(1)
TVP_DEFINE_FLAG_PROP(2)
TVP_DEFINE_FLAG_PROP(3)
TVP_DEFINE_FLAG_PROP(4)
TVP_DEFINE_FLAG_PROP(5)
TVP_DEFINE_FLAG_PROP(6)
TVP_DEFINE_FLAG_PROP(7)
TVP_DEFINE_FLAG_PROP(8)
TVP_DEFINE_FLAG_PROP(9)
TVP_DEFINE_FLAG_PROP(10)
TVP_DEFINE_FLAG_PROP(11)
TVP_DEFINE_FLAG_PROP(12)
TVP_DEFINE_FLAG_PROP(13)
TVP_DEFINE_FLAG_PROP(14)
TVP_DEFINE_FLAG_PROP(15)

//----------------------------------------------------------------------
TJS_END_NATIVE_MEMBERS
}
//---------------------------------------------------------------------------
iTJSDispatch2* TVPCreateWaveFlagsObject(iTJSDispatch2* buffer)
{
    struct tHolder
    {
        iTJSDispatch2* Obj;
        tHolder() { Obj = new tTJSNC_WaveFlags(); }
        ~tHolder() { Obj->Release(); }
    } static waveflagsclass;

    iTJSDispatch2* out;
    tTJSVariant param(buffer);
    tTJSVariant* pparam = &param;
    if (TJS_FAILED(
            waveflagsclass.Obj->CreateNew(0, NULL, NULL, &out, 1, &pparam, waveflagsclass.Obj)))
        TVPThrowInternalError;

    return out;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Options management
//---------------------------------------------------------------------------
static tTVPThreadPriority TVPDecodeThreadHighPriority = ttpHigher;
static tTVPThreadPriority TVPDecodeThreadLowPriority = ttpLowest;
static bool TVPSoundOptionsInit = false;
static bool TVPControlPrimaryBufferRun = true;
static bool TVPUseSoftwareBuffer = true;
static bool TVPAlwaysRecreateSoundBuffer = false;
static bool TVPPrimaryFloat = false;
static tjs_int TVPPriamrySBFrequency = 44100;
static tjs_int TVPPrimarySBBits = 16;
static tTVPSoundGlobalFocusMode TVPSoundGlobalFocusModeByOption = sgfmNeverMute;
static tjs_int TVPSoundGlobalFocusMuteVolume = 0;
static enum tTVPForceConvertMode { fcmNone, fcm16bit, fcm16bitMono } TVPForceConvertMode = fcm16bit;
static tjs_int TVPPrimarySBCreateTryLevel = -1;
static bool TVPExpandToQuad = false;
static tjs_int TVPL1BufferLength = 1000; // in ms
static tjs_int TVPL2BufferLength = 1000; // in ms
static bool TVPDirectSoundUse3D = false;
static tjs_int TVPVolumeLogFactor = 3322;
static bool TVPPrimarySoundBufferPlaying = false;
//---------------------------------------------------------------------------
static void TVPInitSoundOptions()
{
    if (TVPSoundOptionsInit)
        return;

    // retrieve options from commandline
    /*
     ttpIdle = 0
     ttpLowest = 1
     ttpLower = 2
     ttpNormal = 3
     ttpHigher = 4
     ttpHighest = 5
     ttpTimeCritical = 6
    */

    tTJSVariant val;
    if (TVPGetCommandLine(TJS_N("-wsdecpri"), &val))
    {
        tjs_int v = val;
        if (v < 0)
            v = 0;
        if (v > 5)
            v = 5; // tpTimeCritical is dangerous...
        TVPDecodeThreadLowPriority = (tTVPThreadPriority)v;
        if (TVPDecodeThreadHighPriority < TVPDecodeThreadLowPriority)
            TVPDecodeThreadHighPriority = TVPDecodeThreadLowPriority;
    }

    if (TVPGetCommandLine(TJS_N("-wscontrolpri"), &val))
    {
        if (ttstr(val) == TJS_N("yes"))
            TVPControlPrimaryBufferRun = true;
        else
            TVPControlPrimaryBufferRun = false;
    }

    if (TVPGetCommandLine(TJS_N("-wssoft"), &val))
    {
        if (ttstr(val) == TJS_N("no"))
            TVPUseSoftwareBuffer = false;
        else
            TVPUseSoftwareBuffer = true;
    }

    if (TVPGetCommandLine(TJS_N("-wsrecreate"), &val))
    {
        if (ttstr(val) == TJS_N("yes"))
            TVPAlwaysRecreateSoundBuffer = true;
        else
            TVPAlwaysRecreateSoundBuffer = false;
    }

    if (TVPGetCommandLine(TJS_N("-wsfreq"), &val))
    {
        TVPPriamrySBFrequency = val;
    }

    if (TVPGetCommandLine(TJS_N("-wsbits"), &val))
    {
        ttstr sval(val);
        if (sval == TJS_N("f32"))
        {
            TVPPrimaryFloat = true;
            TVPPrimarySBBits = 32;
        }
        else if (sval[0] == TJS_N('i'))
        {
            TVPPrimaryFloat = false;
            TVPPrimarySBBits = TJS_atoi(sval.c_str() + 1);
        }
    }

    if (TVPGetCommandLine(TJS_N("-wspritry"), &val))
    {
        ttstr sval(val);
        if (sval == TJS_N("all"))
            TVPPrimarySBCreateTryLevel = -1;
        else
            TVPPrimarySBCreateTryLevel = val;
    }

    if (TVPGetCommandLine(TJS_N("-wsuse3d"), &val))
    {
        ttstr sval(val);
        if (sval == TJS_N("no"))
            TVPDirectSoundUse3D = false;
        else
            TVPDirectSoundUse3D = true;
    }
    if (TVPGetCommandLine(TJS_N("-wsexpandquad"), &val))
    {
        if (ttstr(val) == TJS_N("yes"))
            TVPExpandToQuad = true;
        else
            TVPExpandToQuad = false;
    }
    if (TVPDirectSoundUse3D)
        TVPExpandToQuad = false;
    // quad expansion is disabled when using 3D sounds

    if (TVPGetCommandLine(TJS_N("-wsmute"), &val))
    {
        ttstr str(val);
        if (str == TJS_N("no") || str == TJS_N("never"))
            TVPSoundGlobalFocusModeByOption = sgfmNeverMute;
        else if (str == TJS_N("minimize"))
            TVPSoundGlobalFocusModeByOption = sgfmMuteOnMinimize;
        else if (str == TJS_N("yes") || str == TJS_N("deactive"))
            TVPSoundGlobalFocusModeByOption = sgfmMuteOnDeactivate;
    }

    if (TVPGetCommandLine(TJS_N("-wsmutevol"), &val))
    {
        tjs_int n = (tjs_int)val;
        if (n >= 0 && n <= 100)
            TVPSoundGlobalFocusMuteVolume = n * 1000;
    }

    if (TVPGetCommandLine(TJS_N("-wsl1len"), &val))
    {
        tjs_int n = (tjs_int)val;
        if (n > 0 && n < 600000)
            TVPL1BufferLength = n;
    }

    if (TVPGetCommandLine(TJS_N("-wsl2len"), &val))
    {
        tjs_int n = (tjs_int)val;
        if (n > 0 && n < 600000)
            TVPL2BufferLength = n;
    }

    if (TVPGetCommandLine(TJS_N("-wsvolfactor"), &val))
    {
        tjs_int n = (tjs_int)val;
        if (n > 0 && n < 200000)
            TVPVolumeLogFactor = n;
    }

    TVPSoundOptionsInit = true;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Log Table for DirectSound volume
//---------------------------------------------------------------------------
static bool TVPLogTableInit = false;
static tjs_int TVPLogTable[101];
static void TVPInitLogTable()
{
    if (TVPLogTableInit)
        return;
    TVPLogTableInit = true;
    tjs_int i;
    TVPLogTable[0] = -10000;
    for (i = 1; i <= 100; i++)
    {
        TVPLogTable[i] = static_cast<tjs_int>(log10((double)i / 100.0) * TVPVolumeLogFactor);
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// DirectSound management
//---------------------------------------------------------------------------
static bool TVPPrimaryBufferPlayingByProgram = false;
static bool TVPDirectSoundShutdown = false;
static bool TVPDeferedSettingAvailable = false;
//---------------------------------------------------------------------------
static void TVPEnsurePrimaryBufferPlay()
{
    if (!TVPControlPrimaryBufferRun)
        return;
    TVPInitDirectSound();
    if (!TVPPrimaryBufferPlayingByProgram)
    {
        TVPPrimaryBufferPlayingByProgram = true;
    }
}
//---------------------------------------------------------------------------
static ttstr TVPGetSoundBufferFormatString(const tTVPWaveFormat& wfx)
{
    ttstr debuglog(TJS_N("format container = "));
    debuglog += TJS_N("WAVE_PCM_");
    debuglog += wfx.IsFloat ? TJS_N("F") : TJS_N("S");
    debuglog += ttstr((tjs_int)wfx.BitsPerSample);
    debuglog += TJS_N("LE");
    debuglog += TJS_N(", ");

    debuglog += TJS_N("frequency = ") + ttstr((tjs_int)wfx.SamplesPerSec) + TJS_N("Hz, ") +
                TJS_N("bits = ") + ttstr((tjs_int)wfx.BitsPerSample) + TJS_N("bits, ") +
                TJS_N("channels = ") + ttstr((tjs_int)wfx.Channels);

    return debuglog;
}
//---------------------------------------------------------------------------
void TVPWaveSoundBufferCommitSettings()
{
}
//---------------------------------------------------------------------------
static void TVPMakeSilentWave(void* dest, tjs_int count, const tTVPWaveFormat* format)
{
    tjs_int bytes = count * format->Channels * format->BytesPerSample;
    memset(dest, 0x00, bytes);
    // TVPMakeSilentWaveBytes(dest, bytes, format);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Buffer management
//---------------------------------------------------------------------------
std::vector<tTJSNI_WaveSoundBuffer*>& TVPWaveSoundBufferVector =
    *(new std::vector<tTJSNI_WaveSoundBuffer*>); // to avoid release order in
                                                 // shutdown
tTJSCriticalSection TVPWaveSoundBufferVectorCS;

//---------------------------------------------------------------------------
// tTVPWaveSoundBufferThread : playing thread
//---------------------------------------------------------------------------
/*
    The system has one playing thread.
    The playing thread fills each sound buffer's L1 (DirectSound) buffer, and
    also manages timing for label events.
    The technique used in this algorithm is similar to Timer claass
   implementation.
*/
class tTVPWaveSoundBufferThread : public tTVPThread
{
    tTVPThreadEvent Event;

    // HWND UtilWindow; // utility window to notify the pending events occur
    bool PendingLabelEventExists;
    bool WndProcToBeCalled;
    uint32_t NextLabelEventTick;
    uint32_t LastFilledTick;

    NativeEventQueue<tTVPWaveSoundBufferThread> EventQueue;

public:
    tTVPWaveSoundBufferThread();
    ~tTVPWaveSoundBufferThread();

private:
    // void __fastcall UtilWndProc(Messages::TMessage &Msg);
    void UtilWndProc(NativeEvent& ev);

public:
    void ReschedulePendingLabelEvent(tjs_int tick);

protected:
    void Execute(void);

public:
    void Start(void);
} static* TVPWaveSoundBufferThread = NULL;
//---------------------------------------------------------------------------
void TVPLockSoundMixer()
{
    TVPPrimaryBufferPlayingByProgram = false;
}
void TVPUnlockSoundMixer()
{
    if (TVPWaveSoundBufferThread)
        TVPEnsurePrimaryBufferPlay();
}
//---------------------------------------------------------------------------
tTVPWaveSoundBufferThread::tTVPWaveSoundBufferThread()
  : tTVPThread(),
    EventQueue(this, &tTVPWaveSoundBufferThread::UtilWndProc)
{
    EventQueue.Allocate();
    PendingLabelEventExists = false;
    NextLabelEventTick = 0;
    LastFilledTick = 0;
    WndProcToBeCalled = false;
    SetPriority(ttpHighest);
    Resume();
}
//---------------------------------------------------------------------------
tTVPWaveSoundBufferThread::~tTVPWaveSoundBufferThread()
{
    SetPriority(ttpNormal);
    Terminate();
    Resume();
    Event.Set();
    WaitFor();
    EventQueue.Deallocate();
}
//---------------------------------------------------------------------------
// void __fastcall tTVPWaveSoundBufferThread::UtilWndProc(Messages::TMessage
// &Msg)
void tTVPWaveSoundBufferThread::UtilWndProc(NativeEvent& ev)
{
    // Window procedure of UtilWindow
    if (ev.Message == TVP_EV_WAVE_SND_BUF_THREAD && !GetTerminated())
    {
        // pending events occur
        tTJSCriticalSectionHolder holder(TVPWaveSoundBufferVectorCS); // protect the object

        WndProcToBeCalled = false;

        tjs_int64 tick = TVPGetTickCount();

        int nearest_next = TVP_TIMEOFS_INVALID_VALUE;

        std::vector<tTJSNI_WaveSoundBuffer*>::iterator i;
        for (i = TVPWaveSoundBufferVector.begin(); i != TVPWaveSoundBufferVector.end(); i++)
        {
            int next = (*i)->FireLabelEventsAndGetNearestLabelEventStep(tick);
            // fire label events and get nearest label event step
            if (next != TVP_TIMEOFS_INVALID_VALUE)
            {
                if (nearest_next == TVP_TIMEOFS_INVALID_VALUE || nearest_next > next)
                    nearest_next = next;
            }
        }

        if (nearest_next != TVP_TIMEOFS_INVALID_VALUE)
        {
            PendingLabelEventExists = true;
            NextLabelEventTick = TVPGetRoughTickCount32() + nearest_next;
        }
        else
        {
            PendingLabelEventExists = false;
        }
    }
    else
    {
        EventQueue.HandlerDefault(ev);
    }
}
//---------------------------------------------------------------------------
void tTVPWaveSoundBufferThread::ReschedulePendingLabelEvent(tjs_int tick)
{
    if (tick == TVP_TIMEOFS_INVALID_VALUE)
        return; // no need to reschedule
    uint32_t eventtick = TVPGetRoughTickCount32() + tick;

    tTJSCriticalSectionHolder holder(TVPWaveSoundBufferVectorCS);

    if (PendingLabelEventExists)
    {
        if ((tjs_int32)NextLabelEventTick - (tjs_int32)eventtick > 0)
            NextLabelEventTick = eventtick;
    }
    else
    {
        PendingLabelEventExists = true;
        NextLabelEventTick = eventtick;
    }
}
//---------------------------------------------------------------------------
#define TVP_WSB_THREAD_SLEEP_TIME 60
void tTVPWaveSoundBufferThread::Execute(void)
{
    while (!GetTerminated())
    {
        // thread loop for playing thread
        uint32_t time = TVPGetRoughTickCount32();
        TVPPushEnvironNoise(&time, sizeof(time));

        { // thread-protected
            tTJSCriticalSectionHolder holder(TVPWaveSoundBufferVectorCS);

            if (TVPPrimaryBufferPlayingByProgram != TVPPrimarySoundBufferPlaying)
            {
                TVPPrimarySoundBufferPlaying = TVPPrimaryBufferPlayingByProgram;
                std::vector<tTJSNI_WaveSoundBuffer*>::iterator i;
                for (i = TVPWaveSoundBufferVector.begin(); i != TVPWaveSoundBufferVector.end(); i++)
                {
                    if ((*i)->ThreadCallbackEnabled)
                        (*i)->SetBufferPaused(!TVPPrimaryBufferPlayingByProgram); // for
                                                                                  // preventing
                                                                                  // buffer runs
                                                                                  // out on iOS'
                                                                                  // OpenAL
                                                                                  // implement
                }
            }

            // check PendingLabelEventExists
            if (PendingLabelEventExists)
            {
                if (!WndProcToBeCalled)
                {
                    WndProcToBeCalled = true;
                    EventQueue.PostEvent(NativeEvent(TVP_EV_WAVE_SND_BUF_THREAD));
                }
            }

            if (TVPPrimarySoundBufferPlaying && time - LastFilledTick >= TVP_WSB_THREAD_SLEEP_TIME)
            {
                std::vector<tTJSNI_WaveSoundBuffer*>::iterator i;
                for (i = TVPWaveSoundBufferVector.begin(); i != TVPWaveSoundBufferVector.end(); i++)
                {
                    if ((*i)->ThreadCallbackEnabled)
                        (*i)->FillBuffer(); // fill sound buffer
                }
                LastFilledTick = time;
            }
        } // end-of-thread-protected

        uint32_t time2;
        time2 = TVPGetRoughTickCount32();
        time = time2 - time;

        if (time < TVP_WSB_THREAD_SLEEP_TIME)
        {
            tjs_int sleep_time = TVP_WSB_THREAD_SLEEP_TIME - time;
            if (PendingLabelEventExists)
            {
                tjs_int step_to_next = (tjs_int32)NextLabelEventTick - (tjs_int32)time2;
                if (step_to_next < sleep_time)
                    sleep_time = step_to_next;
                if (sleep_time < 1)
                    sleep_time = 1;
            }
            Event.WaitFor(sleep_time);
        }
        else
        {
            Event.WaitFor(1);
        }
    }
}
//---------------------------------------------------------------------------
void tTVPWaveSoundBufferThread::Start()
{
    TVPPrimaryBufferPlayingByProgram = true;
    Event.Set();
    Resume();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
static void TVPReleaseSoundBuffers(bool disableevent = true)
{
    // release all secondary buffers.
    tTJSCriticalSectionHolder holder(TVPWaveSoundBufferVectorCS);
    std::vector<tTJSNI_WaveSoundBuffer*>::iterator i;
    for (i = TVPWaveSoundBufferVector.begin(); i != TVPWaveSoundBufferVector.end(); i++)
    {
        (*i)->FreeDirectSoundBuffer(disableevent);
    }
}
//---------------------------------------------------------------------------
static void TVPShutdownWaveSoundBuffers()
{
    // clean up soundbuffers at exit
    if (TVPWaveSoundBufferThread)
        delete TVPWaveSoundBufferThread, TVPWaveSoundBufferThread = NULL;
    TVPReleaseSoundBuffers();
}
static tTVPAtExit TVPShutdownWaveSoundBuffersAtExit(TVP_ATEXIT_PRI_PREPARE,
                                                    TVPShutdownWaveSoundBuffers);
//---------------------------------------------------------------------------
static void TVPEnsureWaveSoundBufferWorking()
{
    if (!TVPWaveSoundBufferThread)
        TVPWaveSoundBufferThread = new tTVPWaveSoundBufferThread();
    TVPWaveSoundBufferThread->Start();
}
//---------------------------------------------------------------------------
static void TVPAddWaveSoundBuffer(tTJSNI_WaveSoundBuffer* buffer)
{
    tTJSCriticalSectionHolder holder(TVPWaveSoundBufferVectorCS);
    TVPWaveSoundBufferVector.push_back(buffer);
}
//---------------------------------------------------------------------------
static void TVPRemoveWaveSoundBuffer(tTJSNI_WaveSoundBuffer* buffer)
{
    bool bufferempty;

    {
        tTJSCriticalSectionHolder holder(TVPWaveSoundBufferVectorCS);
        std::vector<tTJSNI_WaveSoundBuffer*>::iterator i;
        i = std::find(TVPWaveSoundBufferVector.begin(), TVPWaveSoundBufferVector.end(), buffer);
        if (i != TVPWaveSoundBufferVector.end())
            TVPWaveSoundBufferVector.erase(i);
        bufferempty = TVPWaveSoundBufferVector.size() == 0;
    }

    if (bufferempty)
    {
        if (TVPWaveSoundBufferThread)
            delete TVPWaveSoundBufferThread, TVPWaveSoundBufferThread = NULL;
    }
}
//---------------------------------------------------------------------------
static void TVPReschedulePendingLabelEvent(tjs_int tick)
{
    if (TVPWaveSoundBufferThread)
        TVPWaveSoundBufferThread->ReschedulePendingLabelEvent(tick);
}
//---------------------------------------------------------------------------
void TVPResetVolumeToAllSoundBuffer()
{
    // call each SoundBuffer's SetVolumeToSoundBuffer
    tTJSCriticalSectionHolder holder(TVPWaveSoundBufferVectorCS);
    std::vector<tTJSNI_WaveSoundBuffer*>::iterator i;
    for (i = TVPWaveSoundBufferVector.begin(); i != TVPWaveSoundBufferVector.end(); i++)
    {
        (*i)->SetVolumeToSoundBuffer();
    }
}
//---------------------------------------------------------------------------
void TVPReleaseDirectSound()
{
    TVPReleaseSoundBuffers(false);
    TVPUninitDirectSound();
}
//---------------------------------------------------------------------------
void TVPSetWaveSoundBufferUse3DMode(bool b)
{
    // changing the 3D mode will stop all the buffers.
    if (b != TVPDirectSoundUse3D)
    {
        TVPReleaseDirectSound();
        TVPDirectSoundUse3D = b;
    }
}
//---------------------------------------------------------------------------
bool TVPGetWaveSoundBufferUse3DMode()
{
    return TVPDirectSoundUse3D;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPWaveSoundBufferDcodeThread : decoding thread
//---------------------------------------------------------------------------
class tTVPWaveSoundBufferDecodeThread : public tTVPThread
{
    tTJSNI_WaveSoundBuffer* Owner;
    tTVPThreadEvent Event;
    tTJSCriticalSection OneLoopCS;
    volatile bool Running;

public:
    tTVPWaveSoundBufferDecodeThread(tTJSNI_WaveSoundBuffer* owner);
    ~tTVPWaveSoundBufferDecodeThread();

    void Execute(void);

    void Interrupt();
    void Continue();

    bool GetRunning() const { return Running; }
};
//---------------------------------------------------------------------------
tTVPWaveSoundBufferDecodeThread::tTVPWaveSoundBufferDecodeThread(tTJSNI_WaveSoundBuffer* owner)
  : tTVPThread()
{
    TVPInitSoundOptions();

    Owner = owner;
    SetPriority(TVPDecodeThreadHighPriority);
    Running = false;
    Resume();
}
//---------------------------------------------------------------------------
tTVPWaveSoundBufferDecodeThread::~tTVPWaveSoundBufferDecodeThread()
{
    SetPriority(TVPDecodeThreadHighPriority);
    Running = false;
    Terminate();
    Resume();
    Event.Set();
    WaitFor();
}
//---------------------------------------------------------------------------
#define TVP_WSB_DECODE_THREAD_SLEEP_TIME 110
void tTVPWaveSoundBufferDecodeThread::Execute(void)
{
    while (!GetTerminated())
    {
        // decoder thread main loop
        uint32_t st = TVPGetTickCount();
        while (Running)
        {
            bool wait;
            uint32_t et;

            if (Running)
            {
                volatile tTJSCriticalSectionHolder cs_holder(OneLoopCS);
                wait = !Owner->FillL2Buffer(false, true); // fill
            }

            if (GetTerminated())
                break;

            if (Running)
            {
                et = TVPGetTickCount();
                TVPPushEnvironNoise(&et, sizeof(et));
                if (wait)
                {
                    // buffer is full; sleep longer
                    uint32_t elapsed = et - st;
                    if (elapsed < TVP_WSB_DECODE_THREAD_SLEEP_TIME)
                    {
                        Event.WaitFor(TVP_WSB_DECODE_THREAD_SLEEP_TIME - elapsed);
                    }
                }
                else
                {
                    // buffer is not full; sleep shorter
                    TVPRelinquishCPU();
                    if (!GetTerminated())
                        SetPriority(TVPDecodeThreadLowPriority);
                }
                st = et;
            }
        }
        if (GetTerminated())
            break;
        // sleep while running
        Event.WaitFor(/*INFINITE*/ 0);
    }
}
//---------------------------------------------------------------------------
void tTVPWaveSoundBufferDecodeThread::Interrupt()
{
    // interrupt the thread
    if (!Running)
        return;
    SetPriority(TVPDecodeThreadHighPriority);
    Event.Set();
    tTJSCriticalSectionHolder cs_holder(OneLoopCS);
    // this ensures that this function stops the decoding
    Running = false;
}
//---------------------------------------------------------------------------
void tTVPWaveSoundBufferDecodeThread::Continue()
{
    SetPriority(TVPDecodeThreadHighPriority);
    Running = true;
    Event.Set();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNI_WaveSoundBuffer
//---------------------------------------------------------------------------
tjs_int tTJSNI_WaveSoundBuffer::GlobalVolume = 100000;
tTVPSoundGlobalFocusMode tTJSNI_WaveSoundBuffer::GlobalFocusMode = sgfmNeverMute;
//---------------------------------------------------------------------------
tTJSNI_WaveSoundBuffer::tTJSNI_WaveSoundBuffer()
{
    TVPInitSoundOptions();
    TVPInitLogTable();
    Decoder = NULL;
    LoopManager = NULL;
    Thread = NULL;
    UseVisBuffer = false;
    VisBuffer = NULL;
    ThreadCallbackEnabled = false;
    Level2Buffer = NULL;
    Level2BufferSize = 0;
    Volume = 100000;
    Volume2 = 100000;
    BufferCanControlPan = false;
    Pan = 0;
    PosX = PosY = PosZ = (D3DVALUE)0.0;
    SoundBuffer = NULL;
    //	Sound3DBuffer = NULL;
    L2BufferDecodedSamplesInUnit = NULL;
    L1BufferSegmentQueues = NULL;
    L2BufferSegmentQueues = NULL;
    L1BufferDecodeSamplePos = NULL;
    DecodePos = 0;
    L1BufferUnits = 0;
    L2BufferUnits = 0;
    TVPAddWaveSoundBuffer(this);
    Thread = new tTVPWaveSoundBufferDecodeThread(this);
    memset(&C_InputFormat, 0, sizeof(C_InputFormat));
    memset(&InputFormat, 0, sizeof(InputFormat));
    Looping = false;
    DSBufferPlaying = false;
    BufferPlaying = false;
    Paused = false;
    BufferBytes = 0;
    AccessUnitBytes = 0;
    AccessUnitSamples = 0;
    L2AccessUnitBytes = 0;
    SoundBufferPrevReadPos = 0;
    SoundBufferWritePos = 0;
    PlayStopPos = 0;
    L2BufferReadPos = 0;
    L2BufferWritePos = 0;
    L2BufferRemain = 0;
    L2BufferEnded = false;
    LastCheckedDecodePos = -1;
    LastCheckedTick = 0;
}
//---------------------------------------------------------------------------
tjs_error tTJSNI_WaveSoundBuffer::Construct(tjs_int numparams,
                                            tTJSVariant** param,
                                            iTJSDispatch2* tjs_obj)
{
    tjs_error hr = inherited::Construct(numparams, param, tjs_obj);
    if (TJS_FAILED(hr))
        return hr;

    return TJS_S_OK;
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::Invalidate()
{
    inherited::Invalidate();

    Clear();

    DestroySoundBuffer();

    if (Thread)
        delete Thread, Thread = NULL;

    TVPRemoveWaveSoundBuffer(this);
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::ThrowSoundBufferException(const ttstr& reason)
{
    TVPThrowExceptionMessage(TVPCannotCreateDSSecondaryBuffer, reason,
                             ttstr().printf(TJS_N("frequency=%d/channels=%d/bits=%d"),
                                            InputFormat.SamplesPerSec, InputFormat.Channels,
                                            InputFormat.BitsPerSample));
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::TryCreateSoundBuffer(bool use3d)
{
    // release previous sound buffer
    if (SoundBuffer)
        SoundBuffer->Release(), SoundBuffer = NULL;

    // compute buffer bytes
    AccessUnitSamples = InputFormat.SamplesPerSec / TVP_WSB_ACCESS_FREQ;
    AccessUnitBytes = AccessUnitSamples * InputFormat.Channels * InputFormat.BytesPerSample;

    L1BufferUnits = TVPAL_BUFFER_COUNT /*TVPL1BufferLength / (1000 / TVP_WSB_ACCESS_FREQ)*/;
    if (L1BufferUnits <= 1)
        L1BufferUnits = 2;
    if (L1BufferSegmentQueues)
        delete[] L1BufferSegmentQueues, L1BufferSegmentQueues = NULL;
    L1BufferSegmentQueues = new tTVPWaveSegmentQueue[L1BufferUnits];
    LabelEventQueue.clear();
    if (L1BufferDecodeSamplePos)
        delete[] L1BufferDecodeSamplePos, L1BufferDecodeSamplePos = NULL;
    L1BufferDecodeSamplePos = new tjs_int64[L1BufferUnits];
    BufferBytes = AccessUnitBytes * L1BufferUnits;
    // l1 buffer bytes

    if (BufferBytes <= 0)
        ThrowSoundBufferException(TJS_N("Invalid format."));

    // allocate visualization buffer
    if (UseVisBuffer)
        ResetVisBuffer();

    // allocate level2 buffer ( 4sec. buffer )
    L2BufferUnits = TVPL2BufferLength / (1000 / TVP_WSB_ACCESS_FREQ);
    if (L2BufferUnits <= 1)
        L2BufferUnits = 2;

    if (L2BufferDecodedSamplesInUnit)
        delete[] L2BufferDecodedSamplesInUnit, L2BufferDecodedSamplesInUnit = NULL;
    if (L2BufferSegmentQueues)
        delete[] L2BufferSegmentQueues, L2BufferSegmentQueues = NULL;
    L2BufferDecodedSamplesInUnit = new tjs_int[L2BufferUnits];
    L2BufferSegmentQueues = new tTVPWaveSegmentQueue[L2BufferUnits];

    L2AccessUnitBytes = AccessUnitSamples * InputFormat.BytesPerSample * InputFormat.Channels;
    Level2BufferSize = L2AccessUnitBytes * L2BufferUnits;
    if (Level2Buffer)
        delete[] Level2Buffer, Level2Buffer = NULL;
    Level2Buffer = new tjs_uint8[Level2BufferSize];

    SoundBuffer = TVPCreateSoundBuffer(InputFormat, L1BufferUnits);
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::CreateSoundBuffer()
{
    // create a direct sound secondary buffer which has given format.

    TVPInitDirectSound(); // ensure DirectSound object

    bool format_is_not_identical = TVPAlwaysRecreateSoundBuffer ||
                                   C_InputFormat.SamplesPerSec != InputFormat.SamplesPerSec ||
                                   C_InputFormat.Channels != InputFormat.Channels ||
                                   C_InputFormat.BitsPerSample != InputFormat.BitsPerSample ||
                                   C_InputFormat.BytesPerSample != InputFormat.BytesPerSample ||
                                   C_InputFormat.SpeakerConfig != InputFormat.SpeakerConfig ||
                                   C_InputFormat.IsFloat != InputFormat.IsFloat;

    if (format_is_not_identical)
    {
        try
        {
            ttstr msg;
            bool failed;
            bool firstfailed = false;
            ttstr firstformat;
            bool use3d = (InputFormat.Channels >= 3 || InputFormat.SpeakerConfig != 0)
                             ? false
                             : TVPDirectSoundUse3D;
            // currently DirectSound3D cannot handle multiple speaker
            // configuration other than stereo.
            int forcemode = 0;

            if (TVPForceConvertMode == fcm16bit)
                goto try16bits;
            if (TVPForceConvertMode == fcm16bitMono)
                goto try16bits_mono;

            failed = false;
            // TVPWaveFormatToWAVEFORMATEXTENSIBLE(&InputFormat, &Format,
            // use3d);
            try
            {
                TryCreateSoundBuffer(use3d);
            }
            catch (eTJSError& e)
            {
                failed = true;
                msg = e.GetMessage();
            }

            if (failed || !SoundBuffer)
            {
                failed = false;
                // TVPWaveFormatToWAVEFORMATEXTENSIBLE2(&InputFormat, &Format,
                // use3d);
                try
                {
                    TryCreateSoundBuffer(use3d);
                }
                catch (eTJSError& e)
                {
                    firstformat = TVPGetSoundBufferFormatString(InputFormat);
                    failed = true;
                    firstfailed = true;
                    msg = e.GetMessage();
                }
            }

            if (failed || !SoundBuffer)
            {
            try16bits:
                failed = false;
                // TVPWaveFormatToWAVEFORMATEXTENSIBLE16bits(&InputFormat,
                // &Format, use3d);
                try
                {
                    TryCreateSoundBuffer(use3d);
                }
                catch (eTJSError& e)
                {
                    failed = true;
                    msg = e.GetMessage();
                }
                if (!failed)
                    forcemode = 1;
            }

            if (failed)
            {
            try16bits_mono:
                failed = false;
                // TVPWaveFormatToWAVEFORMATEXTENSIBLE16bitsMono(&InputFormat,
                // &Format, use3d);
                try
                {
                    TryCreateSoundBuffer(use3d);
                }
                catch (eTJSError& e)
                {
                    failed = true;
                    msg = e.GetMessage();
                }
                if (!failed)
                    forcemode = 2;
            }

            if (failed)
                TVPThrowExceptionMessage(msg.c_str());
        }
        catch (ttstr& e)
        {
            ThrowSoundBufferException(e);
        }
        catch (...)
        {
            throw;
        }
    }

    // reset volume, sound position and frequency
    SetVolumeToSoundBuffer();
    Set3DPositionToBuffer();
    SetFrequencyToBuffer();

    // reset sound buffer
    ResetSoundBuffer();

    C_InputFormat = InputFormat;
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::DestroySoundBuffer()
{
    if (SoundBuffer)
    {
        SoundBuffer->Stop();
        SoundBuffer->Release();
        SoundBuffer = NULL;
    }

    DSBufferPlaying = false;
    BufferPlaying = false;

    if (L1BufferSegmentQueues)
        delete[] L1BufferSegmentQueues, L1BufferSegmentQueues = NULL;
    LabelEventQueue.clear();
    if (L1BufferDecodeSamplePos)
        delete[] L1BufferDecodeSamplePos, L1BufferDecodeSamplePos = NULL;
    if (L2BufferDecodedSamplesInUnit)
        delete[] L2BufferDecodedSamplesInUnit, L2BufferDecodedSamplesInUnit = NULL;
    if (L2BufferSegmentQueues)
        delete[] L2BufferSegmentQueues, L2BufferSegmentQueues = NULL;
    if (Level2Buffer)
        delete[] Level2Buffer, Level2Buffer = NULL;
    L1BufferUnits = 0;
    L2BufferUnits = 0;

    memset(&C_InputFormat, 0x00, sizeof(C_InputFormat));

    Level2BufferSize = 0;

    DeallocateVisBuffer();
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::ResetSoundBuffer()
{
    if (SoundBuffer)
    {
        SoundBuffer->Reset();
    }

    ResetSamplePositions();
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::ResetSamplePositions()
{
    // reset L1BufferSegmentQueues and L2BufferSegmentQueues, and labels
    if (L1BufferSegmentQueues)
    {
        for (int i = 0; i < L1BufferUnits; i++)
            L1BufferSegmentQueues[i].Clear();
    }
    if (L2BufferSegmentQueues)
    {
        for (int i = 0; i < L2BufferUnits; i++)
            L2BufferSegmentQueues[i].Clear();
    }
    if (L1BufferDecodeSamplePos)
    {
        for (int i = 0; i < L1BufferUnits; i++)
            L1BufferDecodeSamplePos[i] = -1;
    }
    LabelEventQueue.clear();
    DecodePos = 0;
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::Clear()
{
    // clear all status and unload current decoder
    Stop();
    ThreadCallbackEnabled = false;
    Thread->Interrupt();
    if (LoopManager)
        delete LoopManager, LoopManager = NULL;
    ClearFilterChain();
    if (Decoder)
        delete Decoder, Decoder = NULL;
    BufferPlaying = false;
    DSBufferPlaying = false;
    Paused = false;

    ResetSamplePositions();

    SetStatus(ssUnload);
}
//---------------------------------------------------------------------------
tjs_uint tTJSNI_WaveSoundBuffer::Decode(void* buffer,
                                        tjs_uint bufsamplelen,
                                        tTVPWaveSegmentQueue& segments)
{
    // decode one buffer unit
    tjs_uint w = 0;

    try
    {
        // decode
        FilterOutput->Decode((tjs_uint8*)buffer, bufsamplelen, w, segments);
    }
    catch (...)
    {
        // ignore errors
        w = 0;
    }

    return w;
}
//---------------------------------------------------------------------------
bool tTJSNI_WaveSoundBuffer::FillL2Buffer(bool firstwrite, bool fromdecodethread)
{
    if (!fromdecodethread && Thread->GetRunning())
        Thread->SetPriority(ttpHighest);
    // make decoder thread priority high, before entering critical section

    tTJSCriticalSectionHolder holder(L2BufferCS);

    if (firstwrite)
    {
        // only main thread runs here
        L2BufferReadPos = L2BufferWritePos = L2BufferRemain = 0;
        L2BufferEnded = false;
        for (tjs_int i = 0; i < L2BufferUnits; i++)
            L2BufferDecodedSamplesInUnit[i] = 0;
    }

    {
        tTVPThreadPriority ttpbefore = TVPDecodeThreadHighPriority;
        bool retflag = false;
        if (Thread->GetRunning())
        {
            ttpbefore = Thread->GetPriority();
            Thread->SetPriority(TVPDecodeThreadHighPriority);
        }
        {
            tTJSCriticalSectionHolder holder(L2BufferRemainCS);
            if (L2BufferRemain == L2BufferUnits)
                retflag = true;
        }
        if (!retflag)
            UpdateFilterChain(); // if the buffer is not full, update filter
                                 // internal state
        if (Thread->GetRunning())
            Thread->SetPriority(ttpbefore);
        if (retflag)
            return false; // buffer is full
    }

    if (L2BufferEnded)
    {
        L2BufferSegmentQueues[L2BufferWritePos].Clear();
        L2BufferDecodedSamplesInUnit[L2BufferWritePos] = 0;
    }
    else
    {
        L2BufferSegmentQueues[L2BufferWritePos].Clear();
        tjs_uint decoded = Decode(L2BufferWritePos * L2AccessUnitBytes + Level2Buffer,
                                  AccessUnitSamples, L2BufferSegmentQueues[L2BufferWritePos]);

        if (decoded < (tjs_uint)AccessUnitSamples)
            L2BufferEnded = true;

        L2BufferDecodedSamplesInUnit[L2BufferWritePos] = decoded;
    }

    L2BufferWritePos++;
    if (L2BufferWritePos >= L2BufferUnits)
        L2BufferWritePos = 0;

    {
        tTVPThreadPriority ttpbefore = TVPDecodeThreadHighPriority;
        if (Thread->GetRunning())
        {
            ttpbefore = Thread->GetPriority();
            Thread->SetPriority(TVPDecodeThreadHighPriority);
        }
        {
            tTJSCriticalSectionHolder holder(L2BufferRemainCS);
            L2BufferRemain++;
        }
        if (Thread->GetRunning())
            Thread->SetPriority(ttpbefore);
    }

    return true;
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::PrepareToReadL2Buffer(bool firstread)
{
    if (L2BufferRemain == 0 && !L2BufferEnded)
        FillL2Buffer(firstread, false);

    if (Thread->GetRunning())
        Thread->SetPriority(TVPDecodeThreadHighPriority);
    // make decoder thread priority higher than usual,
    // before entering critical section
}
//---------------------------------------------------------------------------
tjs_uint tTJSNI_WaveSoundBuffer::ReadL2Buffer(void* buffer, tTVPWaveSegmentQueue& segments)
{
    // This routine is protected by BufferCS, not L2BufferCS, while
    // this routine reads L2 buffer.
    // But It's ok because this function will never read currently writing L2
    // buffer. L2 buffer having at least one rendered unit is
    // guaranteed at this point.

    tjs_uint decoded = L2BufferDecodedSamplesInUnit[L2BufferReadPos];

    segments = L2BufferSegmentQueues[L2BufferReadPos];
    if (decoded)
    {
        SoundBuffer->AppendBuffer(L2BufferReadPos * L2AccessUnitBytes + Level2Buffer,
                                  decoded * InputFormat.BytesPerSample *
                                      InputFormat.Channels /*, SoundBufferWritePos*/);
        if (buffer)
        { // for VisBuffer
            memcpy(buffer, L2BufferReadPos * L2AccessUnitBytes + Level2Buffer,
                   decoded * InputFormat.BytesPerSample * InputFormat.Channels);
        }
    }
    if (buffer && decoded < (tjs_uint)AccessUnitSamples)
    {
        // fill rest with silence
        TVPMakeSilentWave((tjs_uint8*)buffer +
                              decoded * InputFormat.Channels * InputFormat.BytesPerSample,
                          AccessUnitSamples - decoded, &InputFormat);
    }

    L2BufferReadPos++;
    if (L2BufferReadPos >= L2BufferUnits)
        L2BufferReadPos = 0;

    {
        tTJSCriticalSectionHolder holder(L2BufferRemainCS);
        L2BufferRemain--;
    }

    return decoded;
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::FillDSBuffer(tjs_int writepos, tTVPWaveSegmentQueue& segments)
{
    segments.Clear();

    if (SoundBuffer->IsBufferValid())
    {
        tjs_uint decoded = ReadL2Buffer(UseVisBuffer ? VisBuffer + writepos : nullptr, segments);
    }
}
//---------------------------------------------------------------------------
bool tTJSNI_WaveSoundBuffer::FillBuffer(bool firstwrite, bool allowpause)
{
    // fill DirectSound secondary buffer with one render unit.

    tTJSCriticalSectionHolder holder(BufferCS);

    if (!SoundBuffer)
        return true;
    if (!Decoder)
        return true;
    if (!BufferPlaying)
        return true;
    if (!TVPPrimarySoundBufferPlaying)
        return true;

    // check paused state
    if (allowpause)
    {
        if (Paused)
        {
            if (DSBufferPlaying)
            {
                SoundBuffer->Pause();
                DSBufferPlaying = false;
            }
            return true;
        }
        else
        {
            if (!DSBufferPlaying)
            {
                SoundBuffer->Play(/*0, 0, DSBPLAY_LOOPING*/);
                DSBufferPlaying = true;
            }
        }
    }

    // check decoder thread status
    tjs_int bufferremain;
    {
        tTJSCriticalSectionHolder holder(L2BufferRemainCS);
        bufferremain = L2BufferRemain;
    }

    if (Thread->GetRunning() && bufferremain < TVP_WSB_ACCESS_FREQ)
        Thread->SetPriority(ttpNormal); // buffer remains under 1 sec

    // check buffer playing position
    tjs_int writepos;
    // SoundBufferWritePos = SoundBuffer->GetNextBufferIndex();

    // check position
    tTVPWaveSegmentQueue* segment;
    tjs_int64* bufferdecodesamplepos;

    if (firstwrite)
    {
        writepos = 0;
        segment = L1BufferSegmentQueues + 0;
        bufferdecodesamplepos = L1BufferDecodeSamplePos + 0;
#if 1
        PlayStopPos = -1;
        SoundBufferWritePos = 1;
        SoundBufferPrevReadPos = 0;
#endif
        // SoundBuffer->SetSampleOffset(0);
    }
    else
    {
        ResetLastCheckedDecodePos(/*pp*/);
        if (L2BufferEnded)
        {
            if (SoundBuffer->GetRemainBuffers() == 0)
            {
                FlushAllLabelEvents();
                SoundBuffer->Pause();
                ResetSamplePositions();
                DSBufferPlaying = false;
                BufferPlaying = false;
                if (LoopManager)
                    LoopManager->SetPosition(0);
                return true;
            }
        }
        writepos = SoundBufferWritePos * AccessUnitBytes;
        if (SoundBuffer->GetRemainBuffers() >= TVPAL_BUFFER_COUNT)
        {
            return true;
        }

        segment = L1BufferSegmentQueues + SoundBufferWritePos;
        bufferdecodesamplepos = L1BufferDecodeSamplePos + SoundBufferWritePos;
        SoundBufferWritePos++;
        if (SoundBufferWritePos >= L1BufferUnits)
            SoundBufferWritePos = 0;
    }

    // SoundBufferPrevReadPos = pp;

    // decode
    if (bufferremain > 1) // buffer is ready
    {
        // with no locking operations
        FillDSBuffer(writepos, *segment);
    }
    else
    {
        PrepareToReadL2Buffer(false); // complete decoding before reading from L2

        {
            tTJSCriticalSectionHolder l2holder(L2BufferCS);
            FillDSBuffer(writepos, *segment);
        }
    }

    // insert labels into LabelEventQueue and sort
    const std::deque<tTVPWaveLabel>& labels = segment->GetLabels();
    if (labels.size() != 0)
    {
        // add DecodePos offset to each item->Offset
        // and insert into LabelEventQueue
        for (std::deque<tTVPWaveLabel>::const_iterator i = labels.begin(); i != labels.end(); i++)
        {
            LabelEventQueue.emplace_back(i->Position, i->Name,
                                         static_cast<tjs_int>(i->Offset + DecodePos));
        }

        // sort
        std::sort(LabelEventQueue.begin(), LabelEventQueue.end(),
                  tTVPWaveLabel::tSortByOffsetFuncObj());

        // re-schedule label events
        TVPReschedulePendingLabelEvent(GetNearestEventStep());
    }

    // write bufferdecodesamplepos
    *bufferdecodesamplepos = DecodePos;
    DecodePos += AccessUnitSamples;

    return false;
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::ResetLastCheckedDecodePos(uint32_t pp)
{
    // set LastCheckedDecodePos and  LastCheckedTick
    // we shoud reset these values because the clock sources are usually
    // not identical.
    tTJSCriticalSectionHolder holder(BufferCS);
    if (!SoundBuffer)
        return;

    int offset, rblock;
    if (SoundBuffer->GetRemainBuffers() == 0)
    {
        rblock = SoundBufferWritePos;
        offset = 0;
    }
    else
    {
        offset = SoundBuffer->GetCurrentPlaySamples();
        rblock = offset / AccessUnitSamples;
        offset %= AccessUnitSamples;
        rblock %= L1BufferUnits;
    }
    if (L1BufferDecodeSamplePos[rblock] != -1)
    {
        LastCheckedDecodePos = L1BufferDecodeSamplePos[rblock] + offset;
        LastCheckedTick = TVPGetTickCount();
    }
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_WaveSoundBuffer::FireLabelEventsAndGetNearestLabelEventStep(tjs_int64 tick)
{
    // fire events, event.EventTick <= tick, and return relative time to
    // next nearest event (return TVP_TIMEOFS_INVALID_VALUE for no events).

    // the vector LabelEventQueue must be sorted by the position.
    tTJSCriticalSectionHolder holder(BufferCS);

    if (!BufferPlaying)
        return TVP_TIMEOFS_INVALID_VALUE; // buffer is not currently playing
    if (!DSBufferPlaying)
        return TVP_TIMEOFS_INVALID_VALUE; // direct sound buffer is not
                                          // currently playing

    if (LabelEventQueue.size() == 0)
        return TVP_TIMEOFS_INVALID_VALUE; // no more events

    // calculate current playing decodepos
    // at this point, LastCheckedDecodePos must not be -1
    if (LastCheckedDecodePos == -1)
        ResetLastCheckedDecodePos();
    tjs_int64 decodepos = (tick - LastCheckedTick) * Frequency / 1000 + LastCheckedDecodePos;

    while (true)
    {
        if (LabelEventQueue.size() == 0)
            break;
        std::vector<tTVPWaveLabel>::iterator i = LabelEventQueue.begin();
        int diff = (tjs_int32)i->Offset - (tjs_int32)decodepos;
        if (diff <= 0)
            InvokeLabelEvent(i->Name);
        else
            break;
        LabelEventQueue.erase(i);
    }

    if (LabelEventQueue.size() == 0)
        return TVP_TIMEOFS_INVALID_VALUE; // no more events

    return (tjs_int)((LabelEventQueue[0].Offset - (tjs_int32)decodepos) * 1000 / Frequency);
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_WaveSoundBuffer::GetNearestEventStep()
{
    // get nearest event stop from current tick
    // (current tick is taken from TVPGetTickCount)
    tTJSCriticalSectionHolder holder(BufferCS);

    if (LabelEventQueue.size() == 0)
        return TVP_TIMEOFS_INVALID_VALUE; // no more events

    // calculate current playing decodepos
    // at this point, LastCheckedDecodePos must not be -1
    if (LastCheckedDecodePos == -1)
        ResetLastCheckedDecodePos();
    tjs_int64 decodepos =
        (TVPGetTickCount() - LastCheckedTick) * Frequency / 1000 + LastCheckedDecodePos;

    return (tjs_int)((LabelEventQueue[0].Offset - (tjs_int32)decodepos) * 1000 / Frequency);
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::FlushAllLabelEvents()
{
    // called at the end of the decode.
    // flush all undelivered events.
    tTJSCriticalSectionHolder holder(BufferCS);

    for (std::vector<tTVPWaveLabel>::iterator i = LabelEventQueue.begin();
         i != LabelEventQueue.end(); i++)
        InvokeLabelEvent(i->Name);

    LabelEventQueue.clear();
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::StartPlay()
{
    if (!Decoder)
        return;

    // let primary buffer to start running
    TVPEnsurePrimaryBufferPlay();

    // ensure playing thread
    TVPEnsureWaveSoundBufferWorking();

    // play from first

    { // thread protected block
        if (Thread->GetRunning())
        {
            Thread->SetPriority(TVPDecodeThreadHighPriority);
        }
        tTJSCriticalSectionHolder holder(BufferCS);
        tTJSCriticalSectionHolder l2holder(L2BufferCS);

        CreateSoundBuffer();

        // reset filter chain
        ResetFilterChain();

        // fill sound buffer with some first samples
        BufferPlaying = true;
        FillL2Buffer(true, false);
        FillBuffer(true, false);
        FillBuffer(false, false);
        FillBuffer(false, false);
        FillBuffer(false, false);

        // start playing
        if (!Paused)
        {
            SoundBuffer->Play(/*0, 0, DSBPLAY_LOOPING*/);
            DSBufferPlaying = true;
        }

        // re-schedule label events
        ResetLastCheckedDecodePos();
        TVPReschedulePendingLabelEvent(GetNearestEventStep());
    } // end of thread protected block

    // ensure thread
    TVPEnsureWaveSoundBufferWorking(); // wake the playing thread up again
    ThreadCallbackEnabled = true;
    Thread->Continue();
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::StopPlay()
{
    if (!Decoder)
        return;
    if (!SoundBuffer)
        return;

    if (Thread->GetRunning())
    {
        Thread->SetPriority(TVPDecodeThreadHighPriority);
    }
    tTJSCriticalSectionHolder holder(BufferCS);
    tTJSCriticalSectionHolder l2holder(L2BufferCS);

    SoundBuffer->Stop();
    DSBufferPlaying = false;
    BufferPlaying = false;
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::Play()
{
    // play from first or current position
    if (!Decoder)
        return;
    if (BufferPlaying)
        return;

    StopPlay();

    TVPEnsurePrimaryBufferPlay(); // let primary buffer to start running

    if (Thread->GetRunning())
    {
        Thread->SetPriority(TVPDecodeThreadHighPriority);
    }
    tTJSCriticalSectionHolder holder(BufferCS);
    tTJSCriticalSectionHolder l2holder(L2BufferCS);

    StartPlay();
    SetStatus(ssPlay);
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::Stop()
{
    // stop playing
    StopPlay();

    // delete thread
    ThreadCallbackEnabled = false;
    Thread->Interrupt();

    // set status
    if (Status != ssUnload)
        SetStatus(ssStop);

    // rewind
    if (LoopManager)
        LoopManager->SetPosition(0);
}

void tTJSNI_WaveSoundBuffer::SetBufferPaused(bool bPaused)
{
    if (!Decoder || !SoundBuffer)
        return;

    if (bPaused)
        SoundBuffer->Pause();
    else
    { // restore
        if (!Paused && DSBufferPlaying && BufferPlaying)
        {
            SoundBuffer->Play();
        }
    }
}

//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetPaused(bool b)
{
    if (Thread->GetRunning())
    { /*orgpri = Thread->Priority;*/
        Thread->SetPriority(TVPDecodeThreadHighPriority);
    }
    tTJSCriticalSectionHolder holder(BufferCS);
    tTJSCriticalSectionHolder l2holder(L2BufferCS);

    Paused = b;
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::TimerBeatHandler()
{
    inherited::TimerBeatHandler();

    // check buffer stopping
    if (Status == ssPlay && !BufferPlaying)
    {
        // buffer was stopped
        ThreadCallbackEnabled = false;
        Thread->Interrupt();
        SetStatusAsync(ssStop);
    }
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::Open(const ttstr& storagename)
{
    // open a storage and prepare to play
    TVPEnsurePrimaryBufferPlay(); // let primary buffer to start running

    Clear();

    Decoder = TVPCreateWaveDecoder(storagename);

    try
    {
        // make manager
        LoopManager = new tTVPWaveLoopManager();
        LoopManager->SetDecoder(Decoder);
        LoopManager->SetLooping(Looping);

        // build filter chain
        RebuildFilterChain();

        // retrieve format
        InputFormat = FilterOutput->GetFormat();
        Frequency = InputFormat.SamplesPerSec;
    }
    catch (...)
    {
        Clear();
        throw;
    }

    // open loop information file
    ttstr sliname = storagename + TJS_N(".sli");
    if (TVPIsExistentStorage(sliname))
    {
        tTVPStreamHolder slistream(sliname);
        char* buffer;
        tjs_uint size;
        buffer = new char[(size = static_cast<tjs_uint>(slistream->GetSize())) + 1];
        try
        {
            slistream->ReadBuffer(buffer, size);
            buffer[size] = 0;

            if (!LoopManager->ReadInformation(buffer))
                TVPThrowExceptionMessage(TVPInvalidLoopInformation, sliname);
            RecreateWaveLabelsObject();
        }
        catch (...)
        {
            delete[] buffer;
            Clear();
            throw;
        }
        delete[] buffer;
    }

    // set status to stop
    SetStatus(ssStop);
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetLooping(bool b)
{
    Looping = b;
    if (LoopManager)
        LoopManager->SetLooping(Looping);
}
//---------------------------------------------------------------------------
tjs_uint64 tTJSNI_WaveSoundBuffer::GetSamplePosition()
{
    if (!Decoder)
        return 0L;
    if (!SoundBuffer)
        return 0L;

    tTJSCriticalSectionHolder holder(BufferCS);
    /*
    DWORD wp, pp;
    if(FAILED(SoundBuffer->GetCurrentPosition(&pp, &wp))) return 0L;
    */

    int offset, rblock;
    if (SoundBuffer->GetRemainBuffers() == 0)
    {
        rblock = SoundBufferWritePos;
        offset = 0;
    }
    else
    {
        offset = SoundBuffer->GetCurrentPlaySamples();
        rblock = offset / AccessUnitSamples;
        offset %= AccessUnitSamples;
        rblock %= L1BufferUnits;
    }
    // tjs_int rblock = pp / AccessUnitBytes;

    tTVPWaveSegmentQueue& segs = L1BufferSegmentQueues[rblock];

    // tjs_int offset = pp % AccessUnitBytes;

    // offset /= Format.Format.nBlockAlign;

    return segs.FilteredPositionToDecodePosition(offset);
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetSamplePosition(tjs_uint64 pos)
{
    tjs_uint64 possamples = pos; // in samples

    if (InputFormat.TotalSamples && InputFormat.TotalSamples <= possamples)
        return;

    if (BufferPlaying && DSBufferPlaying)
    {
        StopPlay();
        LoopManager->SetPosition(possamples);
        StartPlay();
    }
    else
    {
        LoopManager->SetPosition(possamples);
    }
}
//---------------------------------------------------------------------------
tjs_uint64 tTJSNI_WaveSoundBuffer::GetPosition()
{
    if (!Decoder)
        return 0L;
    if (!SoundBuffer)
        return 0L;

    return GetSamplePosition() * 1000 / InputFormat.SamplesPerSec;
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetPosition(tjs_uint64 pos)
{
    SetSamplePosition(pos * InputFormat.SamplesPerSec / 1000); // in samples
}
//---------------------------------------------------------------------------
tjs_uint64 tTJSNI_WaveSoundBuffer::GetTotalTime()
{
    return InputFormat.TotalSamples * 1000 / InputFormat.SamplesPerSec;
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetVolumeToSoundBuffer()
{
    // set current volume/pan to DirectSound buffer
    if (SoundBuffer)
    {
        tjs_int v;
        tjs_int mutevol = 100000;
        if (TVPSoundGlobalFocusModeByOption >= sgfmMuteOnDeactivate &&
            TVPSoundGlobalFocusMuteVolume == 0)
        {
            // no mute needed here;
            // muting will be processed in DirectSound framework.
            ;
        }
        else
        {
            // mute mode is choosen from GlobalFocusMode or
            // TVPSoundGlobalFocusModeByOption which is more restrictive.
            tTVPSoundGlobalFocusMode mode = GlobalFocusMode > TVPSoundGlobalFocusModeByOption
                                                ? GlobalFocusMode
                                                : TVPSoundGlobalFocusModeByOption;
        }

        // compute volume for each buffer
        v = (Volume / 10) * (Volume2 / 10) / 1000;
        v = (v / 10) * (GlobalVolume / 10) / 1000;
        v = (v / 10) * (mutevol / 10) / 1000;
        SoundBuffer->SetVolume(/*TVPVolumeToDSAttenuate*/ (v / 100000.0f));

        if (BufferCanControlPan)
        {
            // set pan
            SoundBuffer->SetPan(/*TVPPanToDSAttenuate*/ (Pan / 100000.0f));
        }
    }
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetVolume(tjs_int v)
{
    if (v < 0)
        v = 0;
    if (v > 100000)
        v = 100000;

    if (Volume != v)
    {
        Volume = v;
        SetVolumeToSoundBuffer();
    }
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetVolume2(tjs_int v)
{
    if (v < 0)
        v = 0;
    if (v > 100000)
        v = 100000;

    if (Volume2 != v)
    {
        Volume2 = v;
        SetVolumeToSoundBuffer();
    }
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetPan(tjs_int v)
{
    if (v < -100000)
        v = -100000;
    if (v > 100000)
        v = 100000;
    if (Pan != v)
    {
        Pan = v;
        if (BufferCanControlPan)
        {
            // set pan with SetPan
            SetVolumeToSoundBuffer();
        }
        else
        {
            // set pan with 3D sound facility
            // note that setting pan can reset 3D position.
            PosZ = (D3DVALUE)0.0;
            PosY = (D3DVALUE)0.001;
            // PosX = -0.003 .. -0.0001 = 0 = +0.0001 ... +0.003
            float t;
            t = static_cast<float>(((float)v / 100000.0));
            t *= static_cast<float>(t * 0.003);
            if (v < 0)
                t = -t;
            PosX = t;
            Set3DPositionToBuffer();
        }
    }
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetGlobalVolume(tjs_int v)
{
    if (v < 0)
        v = 0;
    if (v > 100000)
        v = 100000;

    if (GlobalVolume != v)
    {
        GlobalVolume = v;
        TVPResetVolumeToAllSoundBuffer();
    }
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetGlobalFocusMode(tTVPSoundGlobalFocusMode b)
{
    if (GlobalFocusMode != b)
    {
        GlobalFocusMode = b;
        TVPResetVolumeToAllSoundBuffer();
    }
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::Set3DPositionToBuffer()
{
    if (SoundBuffer)
    {
        SoundBuffer->SetPosition(PosX, PosY, PosZ /*, DS3D_DEFERRED*/);
        // defered settings are to be commited at next tickbeat event.
        TVPDeferedSettingAvailable = true;
    }
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetPos(D3DVALUE x, D3DVALUE y, D3DVALUE z)
{
    PosX = x;
    PosY = y;
    PosZ = z;
    Set3DPositionToBuffer();
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetPosX(D3DVALUE v)
{
    PosX = v;
    Set3DPositionToBuffer();
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetPosY(D3DVALUE v)
{
    PosY = v;
    Set3DPositionToBuffer();
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetPosZ(D3DVALUE v)
{
    PosZ = v;
    Set3DPositionToBuffer();
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetFrequencyToBuffer()
{
    if (BufferCanControlFrequency)
    {
        // if(SoundBuffer) SoundBuffer->SetFrequency(Frequency);
    }
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetFrequency(tjs_int freq)
{
    // set frequency
    Frequency = freq;
    SetFrequencyToBuffer();
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetUseVisBuffer(bool b)
{
    tTJSCriticalSectionHolder holder(BufferCS);

    if (b)
    {
        UseVisBuffer = true;

        if (SoundBuffer)
            ResetVisBuffer();
    }
    else
    {
        DeallocateVisBuffer();
        UseVisBuffer = false;
    }
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::ResetVisBuffer()
{
    // reset or recreate visualication buffer
    tTJSCriticalSectionHolder holder(BufferCS);

    DeallocateVisBuffer();

    VisBuffer = new tjs_uint8[BufferBytes];
    UseVisBuffer = true;
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::DeallocateVisBuffer()
{
    tTJSCriticalSectionHolder holder(BufferCS);

    if (VisBuffer)
        delete[] VisBuffer, VisBuffer = NULL;
    UseVisBuffer = false;
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::CopyVisBuffer(tjs_int16* dest,
                                           const tjs_uint8* src,
                                           tjs_int numsamples,
                                           tjs_int channels)
{
    if (channels == 1)
    {
        TVPConvertPCMTo16bits(dest, (const void*)src, InputFormat.Channels,
                              InputFormat.BytesPerSample, InputFormat.BitsPerSample,
                              InputFormat.IsFloat, numsamples, true);
    }
    else if (channels == InputFormat.Channels)
    {
        TVPConvertPCMTo16bits(dest, (const void*)src, InputFormat.Channels,
                              InputFormat.BytesPerSample, InputFormat.BitsPerSample,
                              InputFormat.IsFloat, numsamples, false);
    }
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_WaveSoundBuffer::GetVisBuffer(tjs_int16* dest,
                                             tjs_int numsamples,
                                             tjs_int channels,
                                             tjs_int aheadsamples)
{
    // read visualization buffer samples
    if (!UseVisBuffer)
        return 0;
    if (!VisBuffer)
        return 0;
    if (!Decoder)
        return 0;
    if (!SoundBuffer)
        return 0;
    if (!DSBufferPlaying || !BufferPlaying)
        return 0;

    if (channels != InputFormat.Channels && channels != 1)
        return 0;

    // retrieve current playing position

    tjs_int buffersamples = BufferBytes / (InputFormat.Channels * InputFormat.BytesPerSample);
    int offset;
    // DWORD wp, pp;
    {
        tTJSCriticalSectionHolder holder(BufferCS);
        // the critical section protects only here;
        // the rest is not important code (does anyone care about that the
        // retrieved visualization becomes wrong a little ?)
        offset = SoundBuffer->GetCurrentPlaySamples() + aheadsamples;
        int rblock = offset / AccessUnitSamples;
        offset %= buffersamples;
        if (L1BufferSegmentQueues[rblock % L1BufferUnits].GetFilteredLength() == 0)
            return 0;
    }

    // copy to distination buffer
    tjs_int writtensamples = 0;
    if (numsamples > 0)
    {
        while (true)
        {
            tjs_int bufrest = buffersamples - offset;
            tjs_int copysamples = (bufrest > numsamples ? numsamples : bufrest);

            CopyVisBuffer(dest,
                          VisBuffer + offset * InputFormat.Channels * InputFormat.BytesPerSample,
                          copysamples, channels);

            numsamples -= copysamples;
            writtensamples += copysamples;
            if (numsamples <= 0)
                break;

            dest += channels * copysamples;
            offset += copysamples;
            offset = offset % buffersamples;
        }
    }

    return writtensamples;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_WaveSoundBuffer : TJS WaveSoundBuffer class
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_WaveSoundBuffer::ClassID = -1;
tTJSNC_WaveSoundBuffer::tTJSNC_WaveSoundBuffer()
  : tTJSNativeClass(TJS_N("WaveSoundBuffer")){
        // registration of native members

        TJS_BEGIN_NATIVE_MEMBERS(WaveSoundBuffer) // constructor
        TJS_DECL_EMPTY_FINALIZE_METHOD
            //----------------------------------------------------------------------
            TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(/*var.name*/ _this,
                                              /*var.type*/ tTJSNI_WaveSoundBuffer,
                                              /*TJS class name*/ WaveSoundBuffer){return TJS_S_OK;
}
TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/ WaveSoundBuffer)
//----------------------------------------------------------------------

//-- methods

//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ open)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_WaveSoundBuffer);

    if (numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    _this->Open(*param[0]);

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ open)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ play)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_WaveSoundBuffer);

    _this->Play();

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ play)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ stop)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_WaveSoundBuffer);

    _this->Stop();

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ stop)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ fade)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_WaveSoundBuffer);
    if (numparams < 2)
        return TJS_E_BADPARAMCOUNT;

    tjs_int to;
    tjs_int time;
    tjs_int delay = 0;
    to = (tjs_int)(*param[0]);
    time = (tjs_int)(*param[1]);
    if (numparams >= 3 && param[2]->Type() != tvtVoid)
        delay = (tjs_int)(*param[2]);

    _this->Fade(to, time, delay);

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ fade)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ stopFade)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_WaveSoundBuffer);

    _this->StopFade(false, true);

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ stopFade)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setPos) // not setPosition
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_WaveSoundBuffer);

    if (numparams < 3)
        return TJS_E_BADPARAMCOUNT;

    tTVReal x, y, z;
    x = (*param[0]);
    y = (*param[1]);
    z = (*param[2]);

    _this->SetPos(static_cast<D3DVALUE>(x), static_cast<D3DVALUE>(y), static_cast<D3DVALUE>(z));

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setPos)
//----------------------------------------------------------------------

//-- events

//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onStatusChanged)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_WaveSoundBuffer);

    tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
    if (obj.Object)
    {
        TVP_ACTION_INVOKE_BEGIN(1, "onStatusChanged", objthis);
        TVP_ACTION_INVOKE_MEMBER("status");
        TVP_ACTION_INVOKE_END(obj);
    }

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onStatusChanged)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onFadeCompleted)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_WaveSoundBuffer);

    tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
    if (obj.Object)
    {
        TVP_ACTION_INVOKE_BEGIN(0, "onFadeCompleted", objthis);
        TVP_ACTION_INVOKE_END(obj);
    }

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onFadeCompleted)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onLabel)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_WaveSoundBuffer);

    tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
    if (obj.Object)
    {
        TVP_ACTION_INVOKE_BEGIN(1, "onLabel", objthis);
        TVP_ACTION_INVOKE_MEMBER("name");
        TVP_ACTION_INVOKE_END(obj);
    }

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onLabel)
//----------------------------------------------------------------------

//-- properties

//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(position){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

*result = (tjs_int64)_this->GetPosition();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

    _this->SetPosition((tjs_uint64)(tjs_int64)*param);

    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(position)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(samplePosition){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

*result = (tjs_int64)_this->GetSamplePosition();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

    _this->SetSamplePosition((tjs_uint64)(tjs_int64)*param);

    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(samplePosition)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(paused){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

*result = _this->GetPaused();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

    _this->SetPaused(*param);

    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(paused)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(totalTime){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

*result = (tjs_int64)_this->GetTotalTime();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

    // not yet implemented

    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(totalTime)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(looping){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

*result = _this->GetLooping();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

    _this->SetLooping(*param);

    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(looping)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(volume){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

*result = _this->GetVolume();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

    _this->SetVolume(*param);

    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(volume)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(volume2){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

*result = _this->GetVolume2();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

    _this->SetVolume2(*param);

    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(volume2)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(pan){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

*result = _this->GetPan();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

    _this->SetPan(*param);

    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(pan)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(posX){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

*result = (tTVReal)_this->GetPosX();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

    _this->SetPosX(static_cast<D3DVALUE>((tTVReal)*param));

    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(posX)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(posY){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

*result = (tTVReal)_this->GetPosY();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

    _this->SetPosY(static_cast<D3DVALUE>((tTVReal)*param));

    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(posY)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(posZ){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

*result = (tTVReal)_this->GetPosZ();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

    _this->SetPosZ(static_cast<D3DVALUE>((tTVReal)*param));

    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(posZ)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(status){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

*result = _this->GetStatusString();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(status)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(frequency){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

*result = _this->GetFrequency();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

    _this->SetFrequency((tjs_int)*param);

    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(frequency)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(bits) // not bitsPerSample
{TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

*result = _this->GetBitsPerSample();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(bits)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(channels){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

*result = _this->GetChannels();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(channels)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(flags){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

iTJSDispatch2* dsp = _this->GetWaveFlagsObjectNoAddRef();
*result = tTJSVariant(dsp, dsp);
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(flags)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(labels){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

iTJSDispatch2* dsp = _this->GetWaveLabelsObjectNoAddRef();
*result = tTJSVariant(dsp, dsp);
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(labels)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(filters){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_WaveSoundBuffer);

iTJSDispatch2* dsp = _this->GetFiltersNoAddRef();
*result = tTJSVariant(dsp, dsp);
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(filters)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(globalVolume){
    TJS_BEGIN_NATIVE_PROP_GETTER{* result = tTJSNI_WaveSoundBuffer::GetGlobalVolume();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    tTJSNI_WaveSoundBuffer::SetGlobalVolume(*param);

    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL(globalVolume)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(globalFocusMode){
    TJS_BEGIN_NATIVE_PROP_GETTER{* result = (tjs_int)tTJSNI_WaveSoundBuffer::GetGlobalFocusMode();

return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    tTJSNI_WaveSoundBuffer::SetGlobalFocusMode((tTVPSoundGlobalFocusMode)(tjs_int)*param);

    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL(globalFocusMode)
//----------------------------------------------------------------------

TJS_END_NATIVE_MEMBERS

//----------------------------------------------------------------------
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_WaveSoundBuffer
//---------------------------------------------------------------------------
tTJSNativeInstance* tTJSNC_WaveSoundBuffer::CreateNativeInstance()
{
    return new tTJSNI_WaveSoundBuffer();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPCreateNativeClass_WaveSoundBuffer
//---------------------------------------------------------------------------
tTJSNativeClass* TVPCreateNativeClass_WaveSoundBuffer()
{
    tTJSNativeClass* cls = new tTJSNC_WaveSoundBuffer();
    static tjs_uint32 TJS_NCM_CLASSID;
    TJS_NCM_CLASSID = tTJSNC_WaveSoundBuffer::ClassID;

    //----------------------------------------------------------------------
    // methods
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ freeDirectSound) /* static */
    {
        // release directsound
        TVPReleaseDirectSound();

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL_OUTER(/*object to register*/ cls,
                                     /*func. name*/ freeDirectSound)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getVisBuffer)
    {
        // get samples for visualization
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_WaveSoundBuffer);

        if (numparams < 3)
            return TJS_E_BADPARAMCOUNT;
        tjs_int16* dest = (tjs_int16*)(tjs_int64)(*param[0]);

        tjs_int ahead = 0;
        if (numparams >= 4)
            ahead = (tjs_int)*param[3];

        tjs_int res = _this->GetVisBuffer(dest, *param[1], *param[2], ahead);

        if (result)
            *result = res;

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL_OUTER(/*object to register*/ cls,
                                     /*func. name*/ getVisBuffer)
    //----------------------------------------------------------------------

    //----------------------------------------------------------------------
    // properties
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_PROP_DECL(useVisBuffer){
        TJS_BEGIN_NATIVE_PROP_GETTER{TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                                             /*var. type*/ tTJSNI_WaveSoundBuffer);

    *result = _this->GetUseVisBuffer();
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_WaveSoundBuffer);

    _this->SetUseVisBuffer(0 != (tjs_int)*param);

    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL_OUTER(cls, useVisBuffer)
//----------------------------------------------------------------------
return cls;
}
//---------------------------------------------------------------------------