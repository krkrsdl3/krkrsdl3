#pragma once

#include "tjsNative.h"
#include "DrawDevice.h"

//---------------------------------------------------------------------------
// tTJSNI_BasicDrawDevice
//---------------------------------------------------------------------------
class tTJSNI_BasicDrawDevice : public tTJSNativeInstance {
    typedef tTJSNativeInstance inherited;

    tTVPBasicDrawDevice *Device;

public:
    tTJSNI_BasicDrawDevice();
    ~tTJSNI_BasicDrawDevice();
    tjs_error TJS_INTF_METHOD Construct(tjs_int numparams, tTJSVariant **param,
                                        iTJSDispatch2 *tjs_obj);
    void TJS_INTF_METHOD Invalidate(); 

public:
    tTVPBasicDrawDevice *GetDevice() const { return Device; }
    enum tDrawerType
    {
        dtNone,    //!< drawer なし
        dtDrawDib, //!< もっとも単純なdrawer
        dtDBGDI,   // GDI によるダブルバッファリングを行うdrawer
        dtDBDD,    // DirectDraw によるダブルバッファリングを行うdrawer
        dtDBD3D    // Direct3D によるダブルバッファリングを行うdrawer
    } DrawerType = dtDrawDib,
      PreferredDrawerType = dtDrawDib;
    tDrawerType GetDrawerType() const { return DrawerType; }
    void SetPreferredDrawerType(tDrawerType type) { PreferredDrawerType = type; }
    tDrawerType GetPreferredDrawerType() const { return PreferredDrawerType; }
};
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// tTJSNC_BasicDrawDevice
//---------------------------------------------------------------------------
class tTJSNC_BasicDrawDevice : public tTJSNativeClass {
public:
    tTJSNC_BasicDrawDevice();

    static tjs_uint32 ClassID;

private:
    iTJSNativeInstance *CreateNativeInstance();
};
//---------------------------------------------------------------------------
