#include "tjsCommHead.h"
#include "tjsNative.h"
#include "DrawDevice.h"
#include "TVPWindow.h"
#include "WindowIntf.h"
#include "ncbind/ncbind.hpp"
#include "Platform.h"

#include "tjsNativeLayer.h"

#define NCB_MODULE_NAME TJS_N("drawdeviceD3D.dll")

void DrawDeviceD3D_init()
{
    try
    {
        ncbAutoRegister::LoadModule(TJS_N("emoteplayer.dll"));
        TVPExecuteBinaryStream(GetResourceStream("D3DEmote.tjs"), ttstr("D3DEmote.tjs"));
    } catch(...) {}
}

void DrawDeviceD3D_done()
{
}

NCB_PRE_REGIST_CALLBACK(DrawDeviceD3D_init);
NCB_POST_UNREGIST_CALLBACK(DrawDeviceD3D_done);