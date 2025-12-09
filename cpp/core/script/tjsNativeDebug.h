#pragma once

#include "tjsNative.h"
//---------------------------------------------------------------------------
// tTJSNC_Debug : TJS Debug Class
//---------------------------------------------------------------------------
class tTJSNC_Debug : public tTJSNativeClass
{
public:
	tTJSNC_Debug();

	static tjs_uint32 ClassID;

protected:
	tTJSNativeInstance* CreateNativeInstance();
};
//---------------------------------------------------------------------------
extern tTJSNativeClass* TVPCreateNativeClass_Debug();
//---------------------------------------------------------------------------