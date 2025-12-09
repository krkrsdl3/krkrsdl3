#include "tjsCommHead.h"
#include "tjsNativeCDDASoundBuffer.h"

#include "TVPEvent.h"

#ifdef ENABLE_CDDA

//---------------------------------------------------------------------------
// CD-DA volume controling functions ( by mixer )
//---------------------------------------------------------------------------
tTVPCDDAVolumeControlType TVPCDDAVolumeControlType = cvctMixer;
// method for controling CD-DA volume
static bool TVPCDDAVolumeControlTypeInit = false;
void TVPInitCDDAVolumeControlType() {
    // retrieve command line option "-cdvol"
    if(TVPCDDAVolumeControlTypeInit)
        return;

    // -cdvol=mixer  : use mixer (default)
    // -cdvol=direct : use direct volume control

    tTJSVariant val;
    if(TVPGetCommandLine(TJS_W("-cdvol"), &val)) {
        ttstr str(val);
        if(str == TJS_W("mixer"))
            TVPCDDAVolumeControlType = cvctMixer;
        else if(str == TJS_W("direct"))
            TVPCDDAVolumeControlType = cvctDirect;
        else
            TVPThrowExceptionMessage(TVPCommandLineParamIgnoredAndDefaultUsed,
                                     TJS_W("-cdvol"), str);
    }

    TVPCDDAVolumeControlTypeInit = true;
}
//---------------------------------------------------------------------------
#define NUMVOLUMEFADERS_MAX 0x40
static UINT TVPMixerIDs[NUMVOLUMEFADERS_MAX];
static MIXERCONTROL TVPMixerControls[NUMVOLUMEFADERS_MAX];
static MIXERCONTROLDETAILS TVPMixerControlDetails[NUMVOLUMEFADERS_MAX];
static MIXERCONTROLDETAILS_UNSIGNED TVPOriginalVolumes[NUMVOLUMEFADERS_MAX];
static tjs_int TVPNumMixerControls = 0;
static bool TVPMixerInit = false;
//---------------------------------------------------------------------------
static void TVPGetMixerControls(void) {
    // list up all of volume feders connected to CD
    if(TVPMixerInit)
        return;

    tjs_int i;
    tjs_int nummixers =
        mixerGetNumDevs(); // count of mixers implemented in system
    for(i = 0; i < nummixers; i++) // for each mixer
    {
        HMIXER hmx;
        if(MMSYSERR_NOERROR != mixerOpen(&hmx, i, 0, 0, 0))
            continue;

        MIXERCAPS mxcaps;
        if(MMSYSERR_NOERROR !=
           mixerGetDevCaps((UINT)hmx, &mxcaps, sizeof(mxcaps))) {
            mixerClose(hmx);
            continue;
        }

        tjs_int j;
        for(j = 0; j < (tjs_int)mxcaps.cDestinations;
            j++) // for each destination
        {
            MIXERLINE mxl;
            mxl.cbStruct = sizeof(mxl);
            mxl.dwDestination = j;

            if(MMSYSERR_NOERROR !=
               mixerGetLineInfo((HMIXEROBJ)hmx, &mxl,
                                MIXER_GETLINEINFOF_DESTINATION))
                continue;

            tjs_int numcon = mxl.cConnections;
            tjs_int k;
            for(k = 0; k < numcon; k++) // for each source
            {
                mxl.cbStruct = sizeof(mxl);
                mxl.dwDestination = j;
                mxl.dwSource = k;

                if(MMSYSERR_NOERROR !=
                   mixerGetLineInfo((HMIXEROBJ)hmx, &mxl,
                                    MIXER_GETLINEINFOF_SOURCE))
                    continue;

                if(mxl.fdwLine & MIXERLINE_LINEF_SOURCE) {
                    // the line is source
                    uint32_t componenttype = mxl.dwComponentType;

                    mxl.cbStruct = sizeof(mxl);
                    mxl.dwLineID = mxl.dwLineID;

                    if(MMSYSERR_NOERROR !=
                       mixerGetLineInfo((HMIXEROBJ)hmx, &mxl,
                                        MIXER_GETLINEINFOF_LINEID))
                        continue;

                    if(mxl.cControls == 0) {
                        // no controls found
                        continue;
                    }

                    MIXERCONTROL *pmxc;
                    pmxc = new MIXERCONTROL[mxl.cControls];

                    MIXERLINECONTROLS mxlc;
                    mxlc.cbStruct = sizeof(mxlc);
                    mxlc.dwLineID = mxl.dwLineID;
                    mxlc.cControls = mxl.cControls;
                    mxlc.cbmxctrl = sizeof(MIXERCONTROL);
                    mxlc.pamxctrl = pmxc;

                    if(MMSYSERR_NOERROR !=
                       mixerGetLineControls((HMIXEROBJ)hmx, &mxlc,
                                            MIXER_GETLINECONTROLSF_ALL)) {
                        delete[] pmxc;
                        continue;
                    }

                    tjs_int l;
                    for(l = 0; l < (int)mxlc.cControls; l++) // for all controls
                    {
                        if(pmxc[l].dwControlType ==
                               MIXERCONTROL_CONTROLTYPE_VOLUME &&
                           (componenttype ==
                                MIXERLINE_COMPONENTTYPE_SRC_COMPACTDISC ||
                            (pmxc[l].szShortName[0] == 'C' &&
                             pmxc[l].szShortName[1] == 'D'))) {
                            // the control is a volume fader, and
                            // 1. its component type is COMPACTDISC
                            // 2. or its name starts with "CD"

                            TVPMixerIDs[TVPNumMixerControls] = i;

                            MIXERCONTROLDETAILS *pmxcd =
                                TVPMixerControlDetails + TVPNumMixerControls;

                            ZeroMemory(pmxcd, sizeof(MIXERCONTROLDETAILS));
                            pmxcd->cbStruct = sizeof(MIXERCONTROLDETAILS);
                            pmxcd->dwControlID = pmxc[l].dwControlID;
                            pmxcd->cChannels = 1;
                            pmxcd->cMultipleItems = pmxc[l].cMultipleItems;
                            pmxcd->cbDetails =
                                sizeof(MIXERCONTROLDETAILS_UNSIGNED);
                            pmxcd->paDetails =
                                TVPOriginalVolumes + TVPNumMixerControls;

                            TVPMixerControls[TVPNumMixerControls] = pmxc[l];

                            if(MMSYSERR_NOERROR ==
                               mixerGetControlDetails((HMIXEROBJ)hmx, pmxcd,
                                                      0L)) // get initial volume
                            {
                                TVPNumMixerControls++;
                            }

                            break;
                        }
                    }

                    delete[] pmxc;
                    if(TVPNumMixerControls >= NUMVOLUMEFADERS_MAX)
                        break;
                }
                if(TVPNumMixerControls >= NUMVOLUMEFADERS_MAX)
                    break;
            }
            if(TVPNumMixerControls >= NUMVOLUMEFADERS_MAX)
                break;
        }
        mixerClose(hmx);
        if(TVPNumMixerControls >= NUMVOLUMEFADERS_MAX)
            break;
    }

    TVPMixerInit = true;
}

//---------------------------------------------------------------------------
void TVPRestoreCDVolume(void) {
    if(!TVPMixerInit)
        return;
    tjs_int i;
    for(i = 0; i < TVPNumMixerControls; i++) {
        HMIXER hmx;
        if(MMSYSERR_NOERROR == mixerOpen(&hmx, TVPMixerIDs[i], 0, 0, 0)) {
            mixerSetControlDetails((HMIXEROBJ)hmx, TVPMixerControlDetails + i,
                                   0L);
            mixerClose(hmx);
        }
    }
}
//---------------------------------------------------------------------------
static tTVPAtExit TVPRestoreCDVolumeAtExit(TVP_ATEXIT_PRI_RELEASE,
                                           TVPRestoreCDVolume);
//---------------------------------------------------------------------------
static void TVPSetCDVolume(int v) {
    if(!TVPMixerInit)
        TVPGetMixerControls();
    tjs_int i;
    for(i = 0; i < TVPNumMixerControls; i++) {
        HMIXER hmx;
        if(MMSYSERR_NOERROR == mixerOpen(&hmx, TVPMixerIDs[i], 0, 0, 0)) {
            MIXERCONTROLDETAILS mxcd = TVPMixerControlDetails[i];
            MIXERCONTROLDETAILS_UNSIGNED mxcd_u = TVPOriginalVolumes[i];
            mxcd.paDetails = &mxcd_u;

            float vd = mxcd_u.dwValue;
            vd *= (float)v / 100000.0;
            if(vd < 0)
                vd = 0;
            uint32_t vdd = vd;
            if(vdd >= mxcd_u.dwValue)
                vdd = mxcd_u.dwValue;

            mxcd_u.dwValue = vdd;

            mixerSetControlDetails((HMIXEROBJ)hmx, &mxcd, 0L);

            mixerClose(hmx);
        }
    }
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// MCI error string retrieval function
//---------------------------------------------------------------------------
ttstr TVPGetMCIErrorString(MCIERROR err) {
    char msg[1024];
    mciGetErrorString(err, msg, sizeof(msg));
    return ttstr(msg);
}
void TVPThrowMCIErrorString(MCIERROR err) {
    TVPThrowExceptionMessage(TVPMCIError, TVPGetMCIErrorString(err));
}
//---------------------------------------------------------------------------
#endif // ENABLE_CDDA


//---------------------------------------------------------------------------
// tTJSNI_CDDASoundBuffer
//---------------------------------------------------------------------------
tTJSNI_CDDASoundBuffer::tTJSNI_CDDASoundBuffer() {
#ifdef ENABLE_CDDA
    TVPInitCDDAVolumeControlType();
    Volume = 100000;
    Volume2 = 100000;
    Drive = -1;
    MaxTrackNum = 0;
    TimerBeatPhase = false;
    Looping = false;
#endif
}
//---------------------------------------------------------------------------
tjs_error TJS_INTF_METHOD tTJSNI_CDDASoundBuffer::Construct(
    tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *tjs_obj) {
    tjs_error hr = inherited::Construct(numparams, param, tjs_obj);
    if(TJS_FAILED(hr))
        return hr;

    return TJS_S_OK;
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTJSNI_CDDASoundBuffer::Invalidate() {
    inherited::Invalidate();

#ifdef ENABLE_CDDA
    StopPlay();
    Close();
#endif
}
//---------------------------------------------------------------------------
#ifdef ENABLE_CDDA
void tTJSNI_CDDASoundBuffer::Open(const ttstr &storage) {
    tjs_int drv = 0;
    const tjs_char *c = storage.c_str();
    char drive[4] = { ' ', ':', 0, 0 };

    if(storage.GetLen() >= 2 && storage[1] == TJS_W(':')) {
        // drive is specified

        // search the drive connected to the system
        bool found = false;
        char dr[4];
        dr[1] = ':';
        dr[2] = '\\';
        dr[3] = 0;
        for(dr[0] = 'A'; dr[0] <= 'Z'; dr[0]++) {
            if(GetDriveType(dr) == DRIVE_CDROM) {
                if((dr[0] & 0x1f) == (storage[0] & 0x1f)) {
                    found = true;
                    drive[0] = dr[0];
                    break;
                }
                drv++;
            }
        }

        if(!found) {
            SetStatus(ssUnload);
            TVPThrowExceptionMessage(TVPInvalidCDDADrive);
        }

        c += 2;
    } else {
        // drive is not specified

        // read the system registry to determine default CD-DA drive
        TRegistry *reg = new TRegistry();
        reg->RootKey = HKEY_LOCAL_MACHINE;
        uint32_t defdrive = 0;
        if(!reg->OpenKey("System\\CurrentControlSet\\Control\\MediaResources\\m"
                         "ci\\cdaudio",
                         false)) {
            // cannot open the key
            delete reg;
            reg = NULL;
            defdrive = 0;
        }

        if(reg) {
            try {
                // read value of "Default Drive"
                if(sizeof(defdrive) !=
                   reg->ReadBinaryData("Default Drive", (void *)(&defdrive),
                                       sizeof(defdrive))) {
                    defdrive = 0;
                }
            } catch(...) {
                defdrive = 0;
            }

            reg->CloseKey();
            delete reg;
        }

        char dr[4];
        dr[1] = ':';
        dr[2] = '\\';
        dr[3] = 0;
        char firstdr = ' ';
        bool found = false;
        tjs_int i = 0;
        for(dr[0] = 'A'; dr[0] <= 'Z'; dr[0]++) {
            if(GetDriveType(dr) == DRIVE_CDROM) {
                if(i == 0)
                    firstdr = dr[0];

                if(i == (int)defdrive) {
                    drive[0] = dr[0];
                    drv = i;
                    found = true;
                    break;
                }
                i++;
            }
        }

        if(!found) {
            if(firstdr == ' ')
                TVPThrowExceptionMessage(TVPCDDADriveNotFound);

            // use default drive
            drive[0] = firstdr;
            drv = 0;
        }
    }

    tjs_int track = (tjs_int)TJS_atoi(c);

    StopPlay();
    SetStatus(ssStop);

    if(Drive != drv) {
        // re-open MCI device
        Close();

        Drive = drv;
        strcpy(DriveLetter, drive);

        // open with MCI
        BeforeOpenMedia();

        MCI_OPEN_PARMS openparams;
        ZeroMemory(&openparams, sizeof(openparams));
        openparams.lpstrDeviceType = (LPCSTR)MCI_DEVTYPE_CD_AUDIO;
        openparams.lpstrElementName = drive;

        MCIERROR err;
        err = mciSendCommand(0, MCI_OPEN,
                             MCI_WAIT | MCI_OPEN_TYPE_ID | MCI_OPEN_TYPE |
                                 MCI_OPEN_ELEMENT | MCI_OPEN_SHAREABLE,
                             (uint32_t)&openparams);

        if(err == 0) {
            // if no error

            DeviceID = openparams.wDeviceID;

            // set time format to TMSF
            MCI_SET_PARMS setparams;
            setparams.dwCallback = NULL;
            setparams.dwTimeFormat = MCI_FORMAT_TMSF;
            setparams.dwAudio = 0;

            err = mciSendCommand(DeviceID, MCI_SET,
                                 MCI_WAIT | MCI_SET_TIME_FORMAT,
                                 (uint32_t)&setparams);

            if(err) {
                MCI_GENERIC_PARMS params;
                ZeroMemory(&params, sizeof(params));
                mciSendCommand(DeviceID, MCI_CLOSE, MCI_WAIT,
                               (uint32_t)&params);
            } else {
                // get maximum number of tracks
                MCI_STATUS_PARMS statusparams;
                ZeroMemory(&statusparams, sizeof(statusparams));
                statusparams.dwItem = MCI_STATUS_NUMBER_OF_TRACKS;
                MCIERROR err;
                err = mciSendCommand(DeviceID, MCI_STATUS,
                                     MCI_STATUS_ITEM | MCI_WAIT,
                                     (uint32_t)&statusparams);

                if(err) {
                    MCI_GENERIC_PARMS params;
                    ZeroMemory(&params, sizeof(params));
                    mciSendCommand(DeviceID, MCI_CLOSE, MCI_WAIT,
                                   (uint32_t)&params);
                }

                MaxTrackNum = (tjs_int)statusparams.dwReturn;
            }
        }

        if(err) {
            SetStatus(ssUnload);
            AfterOpenMedia();
            TVPThrowMCIErrorString(err);
        }

        AfterOpenMedia();
        SetStatus(ssStop);
    }

    Track = track;
}
//---------------------------------------------------------------------------
void tTJSNI_CDDASoundBuffer::Close() {
    if(Status != ssUnload && Drive != -1) {
        // close MCI
        MCI_GENERIC_PARMS params;
        ZeroMemory(&params, sizeof(params));
        mciSendCommand(DeviceID, MCI_CLOSE, MCI_WAIT, (uint32_t)&params);
        SetStatus(ssUnload);
    }
    MaxTrackNum = 0;
    Drive = -1;
}
//---------------------------------------------------------------------------
MCIERROR tTJSNI_CDDASoundBuffer::StartPlay() {
    // start playing with MCI
    MCIERROR err;
    MCI_PLAY_PARMS playparams;
    ZeroMemory(&playparams, sizeof(playparams));
    playparams.dwFrom = MCI_MAKE_TMSF(Track, 0, 0, 0);
    playparams.dwTo = MCI_MAKE_TMSF(Track + 1, 0, 0, 0);

    uint32_t flags = MCI_FROM;
    if((int)MaxTrackNum != Track) // if the track is not last track
        flags |= MCI_TO;

    _SetVolume(Volume);
    err = mciSendCommand(DeviceID, MCI_PLAY, flags, (uint32_t)&playparams);
    return err;
}
//---------------------------------------------------------------------------
MCIERROR tTJSNI_CDDASoundBuffer::StopPlay() {
    // stop playing
    if(Drive != -1 && Status != ssUnload) {

        MCI_GENERIC_PARMS genparams;
        genparams.dwCallback = 0;
        MCIERROR err;
        err =
            mciSendCommand(DeviceID, MCI_STOP, MCI_WAIT, (uint32_t)&genparams);
        return err;
    }
    return 0;
}
//---------------------------------------------------------------------------
void tTJSNI_CDDASoundBuffer::Play() {
    // play

    if(Status != ssPlay && Drive != -1 && Status != ssUnload) {
        MCIERROR err;
        err = StartPlay();
        if(err)
            TVPThrowMCIErrorString(err);
        SetStatus(ssPlay);
    }
}
//---------------------------------------------------------------------------
void tTJSNI_CDDASoundBuffer::Stop() {
    if(Status == ssPlay) {
        MCIERROR err;
        err = StopPlay();
        if(err)
            TVPThrowMCIErrorString(err);
        SetStatus(ssStop);
    }
}
//---------------------------------------------------------------------------
void tTJSNI_CDDASoundBuffer::TimerBeatHandler() {
    TimerBeatPhase = !TimerBeatPhase;
    if(!TimerBeatPhase) // thin-out
    {
        // look MCI status to check the stopping

        if(Status == ssPlay) {
            MCI_STATUS_PARMS statusparams;
            ZeroMemory(&statusparams, sizeof(MCI_STATUS_PARMS));
            statusparams.dwItem = MCI_STATUS_MODE;
            if(!mciSendCommand(DeviceID, MCI_STATUS, MCI_WAIT | MCI_STATUS_ITEM,
                               (uint32_t)&statusparams)) {
                if(statusparams.dwReturn == MCI_MODE_STOP) {
                    if(Looping)
                        StartPlay();
                    else
                        SetStatusAsync(ssStop);
                }
            }
        }
    }

    inherited::TimerBeatHandler();
}
//---------------------------------------------------------------------------
bool tTJSNI_CDDASoundBuffer::ReadOrgVolumeData() {
    // read current CD-DA volume from the system registry

    if(TVPCDDAVolumeControlType != cvctDirect)
        return false;

    char key[256];
    sprintf(key,
            "System\\CurrentControlSet\\Control\\MediaResources\\mci\\cdaudio\\"
            "unit %d",
            Drive);

    TRegistry *reg = new TRegistry();
    reg->RootKey = HKEY_LOCAL_MACHINE;
    if(!reg->OpenKey(key, true)) {
        // can not open the key
        delete reg;
        return false;
    }

    // read current volume data
    if(sizeof(OrgVolumeData) !=
       reg->ReadBinaryData("Volume Settings", (void *)&OrgVolumeData,
                           sizeof(OrgVolumeData))) {
        reg->CloseKey();
        delete reg;
        // generate orgvolumedata
        OrgVolumeData.dwUnitNo = Drive;
        OrgVolumeData.dwVolume = 0xff;
        return false;
    }

    reg->CloseKey();
    delete reg;

    return true;
}
//---------------------------------------------------------------------------
void tTJSNI_CDDASoundBuffer::WriteVolumeRegistry(tjs_int vol) {
    // write new CD-DA volume information to the system registry

    if(TVPCDDAVolumeControlType != cvctDirect)
        return;

    char key[256];
    sprintf(key,
            "System\\CurrentControlSet\\Control\\MediaResources\\mci\\cdaudio\\"
            "unit %d",
            Drive);

    TRegistry *reg = new TRegistry();
    reg->RootKey = HKEY_LOCAL_MACHINE;
    if(!reg->OpenKey(key, true)) {
        // cannot open the key
        delete reg;
        return; // fail
    }

    CD_AUDIO_VOLUME_DATA cavd = OrgVolumeData;
    if(vol != -1)
        cavd.dwVolume = vol;

    try {
        reg->WriteBinaryData("Volume Settings", (void *)(&cavd), sizeof(cavd));
    } catch(...) {
        reg->CloseKey();
        delete reg;
        return;
    }

    reg->CloseKey();
    delete reg;
}
//---------------------------------------------------------------------------
bool tTJSNI_CDDASoundBuffer::BeforeOpenMedia() {
    // called before opening MCI device

    tjs_int v = (Volume / 10) * (Volume2 / 10) / 1000;

    if(TVPCDDAVolumeControlType == cvctMixer || !ReadOrgVolumeData()) {
        // mixer or cannot open the key;
        // use mixer
        TVPSetCDVolume(v);
        return false;
    }

    // compute new volume
    float vl = (float)v / 100000;
    vl = vl * vl;
    vl *= OrgVolumeData.dwVolume;
    v = vl;
    if(v < 0)
        v = 0;
    if(v > 255)
        v = 255;

    // write new volume registry
    WriteVolumeRegistry(v);

    return true;
}
//---------------------------------------------------------------------------
void tTJSNI_CDDASoundBuffer::AfterOpenMedia() {
    // called after opening MCI device

    // write original volume information to the system registry
    WriteVolumeRegistry(-1);
}
//---------------------------------------------------------------------------
void tTJSNI_CDDASoundBuffer::_SetVolume(tjs_int v) {
    Volume = v;

    if(Drive == -1)
        return;

    if(!BeforeOpenMedia())
        return; // no need to direct access

    // open MCI device to update CD-ROM's volume register and immediately
    // close it .

    char elmname[30];
    strcpy(elmname, DriveLetter);
    char alias[30];
    sprintf(alias, "__%lu", GetTickCount()); // unique alias

    MCI_OPEN_PARMS params;
    ZeroMemory(&params, sizeof(params));
    params.lpstrDeviceType = (LPCSTR)MCI_DEVTYPE_CD_AUDIO;
    params.lpstrElementName = elmname;
    params.lpstrAlias = alias;

    MCIERROR err =
        mciSendCommand(0, MCI_OPEN,
                       MCI_WAIT | MCI_OPEN_SHAREABLE | MCI_OPEN_ELEMENT |
                           MCI_OPEN_ALIAS | MCI_OPEN_TYPE_ID | MCI_OPEN_TYPE,
                       (uint32_t)&params);

    if(err == 0) {
        MCI_GENERIC_PARMS genparams;
        ZeroMemory(&genparams, sizeof(genparams));
        genparams.dwCallback = 0;

        mciSendCommand(params.wDeviceID, MCI_CLOSE, MCI_WAIT,
                       (uint32_t)&genparams);
    }

    AfterOpenMedia();
}
//---------------------------------------------------------------------------
void tTJSNI_CDDASoundBuffer::SetVolume(tjs_int v) {
    // set new volume
    if(v < 0)
        v = 0;
    if(v > 100000)
        v = 100000;

    if(Volume != v) {
        _SetVolume(v);
    }
}
//---------------------------------------------------------------------------
void tTJSNI_CDDASoundBuffer::SetVolume2(tjs_int v) {
    // set new alternative volume
    if(v < 0)
        v = 0;
    if(v > 100000)
        v = 100000;

    if(Volume2 != v) {
        Volume2 = v;
        _SetVolume(Volume);
    }
}
//---------------------------------------------------------------------------
#endif

//---------------------------------------------------------------------------
// tTJSNI_BaseCDDASoundBuffer
//---------------------------------------------------------------------------
tTJSNI_BaseCDDASoundBuffer::tTJSNI_BaseCDDASoundBuffer() {}
//---------------------------------------------------------------------------
tjs_error TJS_INTF_METHOD tTJSNI_BaseCDDASoundBuffer::Construct(
    tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *tjs_obj) {
    tjs_error hr = inherited::Construct(numparams, param, tjs_obj);
    if(TJS_FAILED(hr))
        return hr;

    return TJS_S_OK;
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTJSNI_BaseCDDASoundBuffer::Invalidate() {

    inherited::Invalidate();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_CDDASoundBuffer : TJS CDDASoundBuffer class
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_CDDASoundBuffer::ClassID = -1;
tTJSNC_CDDASoundBuffer::tTJSNC_CDDASoundBuffer() :
	tTJSNativeClass(TJS_W("CDDASoundBuffer"))
{
	// registration of native members

	TJS_BEGIN_NATIVE_MEMBERS(CDDASoundBuffer) // constructor
		TJS_DECL_EMPTY_FINALIZE_METHOD
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(/*var.name*/_this,
			/*var.type*/tTJSNI_CDDASoundBuffer,
			/*TJS class name*/CDDASoundBuffer)
	{
		return TJS_S_OK;
	}
	TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/CDDASoundBuffer)
		//----------------------------------------------------------------------

		//-- methods

		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/open)
	{
		TJS_GET_NATIVE_INSTANCE(/*var. name*/_this,
			/*var. type*/tTJSNI_CDDASoundBuffer);

		if (numparams < 1) return TJS_E_BADPARAMCOUNT;
#ifdef ENABLE_CDDA
		_this->Open(*param[0]);
#endif
		return TJS_S_OK;
	}
	TJS_END_NATIVE_METHOD_DECL(/*func. name*/open)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/play)
	{
		TJS_GET_NATIVE_INSTANCE(/*var. name*/_this,
			/*var. type*/tTJSNI_CDDASoundBuffer);
#ifdef ENABLE_CDDA

		_this->Play();
#endif

		return TJS_S_OK;
	}
	TJS_END_NATIVE_METHOD_DECL(/*func. name*/play)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/stop)
	{
		TJS_GET_NATIVE_INSTANCE(/*var. name*/_this,
			/*var. type*/tTJSNI_CDDASoundBuffer);

#ifdef ENABLE_CDDA
		_this->Stop();
#endif

		return TJS_S_OK;
	}
	TJS_END_NATIVE_METHOD_DECL(/*func. name*/stop)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/fade)
	{
		TJS_GET_NATIVE_INSTANCE(/*var. name*/_this,
			/*var. type*/tTJSNI_CDDASoundBuffer);
		if (numparams < 2) return TJS_E_BADPARAMCOUNT;

#ifdef ENABLE_CDDA
		tjs_int to;
		tjs_int time;
		tjs_int delay = 0;
		to = (tjs_int)(*param[0]);
		time = (tjs_int)(*param[1]);
		if (numparams >= 3 && param[2]->Type() != tvtVoid)
			delay = (tjs_int)(*param[2]);

		_this->Fade(to, time, delay);
#endif

		return TJS_S_OK;
	}
	TJS_END_NATIVE_METHOD_DECL(/*func. name*/fade)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/stopFade)
	{
		TJS_GET_NATIVE_INSTANCE(/*var. name*/_this,
			/*var. type*/tTJSNI_CDDASoundBuffer);

#ifdef ENABLE_CDDA
		_this->StopFade(false, true);
#endif

		return TJS_S_OK;
	}
	TJS_END_NATIVE_METHOD_DECL(/*func. name*/stopFade)
		//----------------------------------------------------------------------

		//-- events

		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/onStatusChanged)
	{
		TJS_GET_NATIVE_INSTANCE(/*var. name*/_this,
			/*var. type*/tTJSNI_CDDASoundBuffer);

		tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
		if (obj.Object)
		{
			TVP_ACTION_INVOKE_BEGIN(1, "onStatusChanged", objthis);
			TVP_ACTION_INVOKE_MEMBER("status");
			TVP_ACTION_INVOKE_END(obj);
		}

		return TJS_S_OK;
	}
	TJS_END_NATIVE_METHOD_DECL(/*func. name*/onStatusChanged)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/onFadeCompleted)
	{
		TJS_GET_NATIVE_INSTANCE(/*var. name*/_this,
			/*var. type*/tTJSNI_CDDASoundBuffer);

		tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
		if (obj.Object)
		{
			TVP_ACTION_INVOKE_BEGIN(0, "onFadeCompleted", objthis);
			TVP_ACTION_INVOKE_END(obj);
		}

		return TJS_S_OK;
	}
	TJS_END_NATIVE_METHOD_DECL(/*func. name*/onFadeCompleted)
		//----------------------------------------------------------------------

		//-- properties

		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_PROP_DECL(position)
	{
		TJS_BEGIN_NATIVE_PROP_GETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_CDDASoundBuffer);

		// not yet implemented

		return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_GETTER

			TJS_BEGIN_NATIVE_PROP_SETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_CDDASoundBuffer);

		// not yet implemented

		return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_SETTER
	}
	TJS_END_NATIVE_PROP_DECL(position)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_PROP_DECL(paused)
	{
		TJS_BEGIN_NATIVE_PROP_GETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_CDDASoundBuffer);

		// not yet implemented

		return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_GETTER

			TJS_BEGIN_NATIVE_PROP_SETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_CDDASoundBuffer);

		// not yet implemented

		return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_SETTER
	}
	TJS_END_NATIVE_PROP_DECL(paused)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_PROP_DECL(totalTime)
	{
		TJS_BEGIN_NATIVE_PROP_GETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_CDDASoundBuffer);

		// not yet implemented

		return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_GETTER

			TJS_BEGIN_NATIVE_PROP_SETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_CDDASoundBuffer);

		// not yet implemented

		return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_SETTER
	}
	TJS_END_NATIVE_PROP_DECL(totalTime)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_PROP_DECL(looping)
	{
		TJS_BEGIN_NATIVE_PROP_GETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_CDDASoundBuffer);

	#ifdef ENABLE_CDDA
			* result = _this->GetLooping();
	#endif
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_GETTER

			TJS_BEGIN_NATIVE_PROP_SETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_CDDASoundBuffer);

	#ifdef ENABLE_CDDA
			_this->SetLooping(*param);
	#endif

			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_SETTER
	}
	TJS_END_NATIVE_PROP_DECL(looping)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_PROP_DECL(volume)
	{
		TJS_BEGIN_NATIVE_PROP_GETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_CDDASoundBuffer);

	#ifdef ENABLE_CDDA
			* result = _this->GetVolume();
	#endif

			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_GETTER

			TJS_BEGIN_NATIVE_PROP_SETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_CDDASoundBuffer);

	#ifdef ENABLE_CDDA
			_this->SetVolume(*param);
	#endif

			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_SETTER
	}
	TJS_END_NATIVE_PROP_DECL(volume)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_PROP_DECL(volume2)
	{
		TJS_BEGIN_NATIVE_PROP_GETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_CDDASoundBuffer);

	#ifdef ENABLE_CDDA
			* result = _this->GetVolume2();
	#else
			* result = 0;
	#endif

			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_GETTER

			TJS_BEGIN_NATIVE_PROP_SETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_CDDASoundBuffer);

	#ifdef ENABLE_CDDA
			_this->SetVolume2(*param);
	#endif

			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_SETTER
	}
	TJS_END_NATIVE_PROP_DECL(volume2)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_PROP_DECL(status)
	{
		TJS_BEGIN_NATIVE_PROP_GETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_CDDASoundBuffer);

			*result = _this->GetStatusString();

			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_GETTER

			TJS_DENY_NATIVE_PROP_SETTER
	}
	TJS_END_NATIVE_PROP_DECL(status)
		//---------------------------------------------------------------------------

		TJS_END_NATIVE_MEMBERS

		//----------------------------------------------------------------------


}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_CDDASoundBuffer
//---------------------------------------------------------------------------
tTJSNativeInstance* tTJSNC_CDDASoundBuffer::CreateNativeInstance()
{
	return new tTJSNI_CDDASoundBuffer();
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// TVPCreateNativeClass_CDDASoundBuffer
//---------------------------------------------------------------------------
tTJSNativeClass* TVPCreateNativeClass_CDDASoundBuffer()
{
	return new tTJSNC_CDDASoundBuffer();
}
//---------------------------------------------------------------------------