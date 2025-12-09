#pragma once

#include "tjsNative.h"

//---------------------------------------------------------------------------
// tTJSNC_AsyncTrigger : TJS AsyncTrigger class
//---------------------------------------------------------------------------
class tTJSNC_AsyncTrigger : public tTJSNativeClass
{
	typedef tTJSNativeClass inherited;

public:
	tTJSNC_AsyncTrigger();
	static tjs_uint32 ClassID;

protected:
	tTJSNativeInstance* CreateNativeInstance();
};
//---------------------------------------------------------------------------
extern tTJSNativeClass* TVPCreateNativeClass_AsyncTrigger();
//---------------------------------------------------------------------------
