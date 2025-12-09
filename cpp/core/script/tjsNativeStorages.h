#pragma once

#include "tjsNative.h"

//---------------------------------------------------------------------------
// tTJSNC_Storages : TJS Storages class
//---------------------------------------------------------------------------
class tTJSNC_Storages : public tTJSNativeClass
{
	typedef tTJSNativeClass inherited;

public:
	tTJSNC_Storages();
	static tjs_uint32 ClassID;

protected:
	tTJSNativeInstance* CreateNativeInstance();
};
//---------------------------------------------------------------------------
extern tTJSNativeClass* TVPCreateNativeClass_Storages();
//---------------------------------------------------------------------------

