#pragma once

#include "tjsNative.h"

//---------------------------------------------------------------------------
// tTJSNC_Scripts : TJS Scripts class
//---------------------------------------------------------------------------
class tTJSNC_Scripts : public tTJSNativeClass
{
	typedef tTJSNativeClass inherited;

public:
	tTJSNC_Scripts();
	static tjs_uint32 ClassID;

protected:
	tTJSNativeInstance* CreateNativeInstance();
};
//---------------------------------------------------------------------------
extern tTJSNativeClass* TVPCreateNativeClass_Scripts();
//---------------------------------------------------------------------------