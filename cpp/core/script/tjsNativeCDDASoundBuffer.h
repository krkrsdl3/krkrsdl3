#pragma once

#include "tjsNative.h"
#include "tjsNativeWaveSoundBuffer.h"

//---------------------------------------------------------------------------
// tTJSNI_BaseCDDASoundBuffer
//---------------------------------------------------------------------------
class tTJSNI_BaseCDDASoundBuffer : public tTJSNI_SoundBuffer {
    typedef tTJSNI_SoundBuffer inherited;

public:
    tTJSNI_BaseCDDASoundBuffer();
    tjs_error TJS_INTF_METHOD Construct(tjs_int numparams, tTJSVariant **param,
                                        iTJSDispatch2 *tjs_obj);
    void TJS_INTF_METHOD Invalidate();

protected:
public:
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPCDDAVolumeControlType
//---------------------------------------------------------------------------
enum tTVPCDDAVolumeControlType {
    cvctMixer, // use sound card's mixer
    cvctDirect // direct control for CD-ROM drive
};
extern tTVPCDDAVolumeControlType TVPCDDAVolumeControlType;
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// CD_AUDIO_VOLUME_DATA
//---------------------------------------------------------------------------
#pragma pack(push, 1)
struct CD_AUDIO_VOLUME_DATA {
    unsigned long dwUnitNo;
    unsigned long dwVolume;
};
#pragma pack(pop)
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// tTJSNI_CDDASoundBuffer : CDDA Native Instance
//---------------------------------------------------------------------------
class tTJSNI_CDDASoundBuffer : public tTJSNI_BaseCDDASoundBuffer {
    typedef tTJSNI_BaseCDDASoundBuffer inherited;

public:
    tTJSNI_CDDASoundBuffer();
    tjs_error TJS_INTF_METHOD Construct(tjs_int numparams, tTJSVariant **param,
                                        iTJSDispatch2 *tjs_obj);
    void TJS_INTF_METHOD Invalidate();

#ifdef ENABLE_CDDA
    //-- playing stuff ----------------------------------------------------
private:
    tjs_int Drive;
    tjs_int MaxTrackNum;
    char DriveLetter[4];
    tjs_int Track;
    MCIDEVICEID DeviceID;
    bool TimerBeatPhase;
    bool Looping;

public:
    void Open(const ttstr &storage);
    void Close();
    MCIERROR StartPlay();
    MCIERROR StopPlay();
    void Play();
    void Stop();

    bool GetLooping() const { return Looping; };
    void SetLooping(bool b) { Looping = b; };

protected:
    void TimerBeatHandler(); // override


    //-- volume stuff -----------------------------------------------------
private:
    tjs_int Volume;
    tjs_int Volume2;
    CD_AUDIO_VOLUME_DATA OrgVolumeData;
    bool ReadOrgVolumeData();
    void WriteVolumeRegistry(tjs_int vol);
    bool BeforeOpenMedia();
    void AfterOpenMedia();

    void _SetVolume(tjs_int v);

public:
    void SetVolume(tjs_int v);
    tjs_int GetVolume() const { return Volume; }
    void SetVolume2(tjs_int v);
    tjs_int GetVolume2() const { return Volume2; }

protected:
#else
    void SetVolume(tjs_int v) {}
    tjs_int GetVolume() const { return 0; }
#endif
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_CDDASoundBuffer : TJS CDDASoundBuffer class
//---------------------------------------------------------------------------
class tTJSNC_CDDASoundBuffer : public tTJSNativeClass
{
public:
	tTJSNC_CDDASoundBuffer();
	static tjs_uint32 ClassID;

protected:
	tTJSNativeInstance* CreateNativeInstance();
};
//---------------------------------------------------------------------------
extern tTJSNativeClass* TVPCreateNativeClass_CDDASoundBuffer();
//---------------------------------------------------------------------------

