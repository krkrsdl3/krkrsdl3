#pragma once

#include "tjsNative.h"

//---------------------------------------------------------------------------
// tTJSNC_Plugins : TJS Plugins class
//---------------------------------------------------------------------------
class tTJSNC_Plugins : public tTJSNativeClass
{
	typedef tTJSNativeClass inherited;

public:
	tTJSNC_Plugins();
	static tjs_uint32 ClassID;

protected:
	tTJSNativeInstance* CreateNativeInstance();
};
//---------------------------------------------------------------------------
extern tTJSNativeClass* TVPCreateNativeClass_Plugins();
//---------------------------------------------------------------------------


