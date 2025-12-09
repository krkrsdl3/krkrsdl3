#pragma once

#include "tjsNative.h"

//---------------------------------------------------------------------------
// tTJSNC_System : TJS System class
//---------------------------------------------------------------------------
class tTJSNC_System : public tTJSNativeClass
{
	typedef tTJSNativeClass inherited;

public:
	tTJSNC_System();
	static tjs_uint32 ClassID;

protected:
	tTJSNativeInstance* CreateNativeInstance();
};
//---------------------------------------------------------------------------
extern tTJSNativeClass* TVPCreateNativeClass_System();
//---------------------------------------------------------------------------

