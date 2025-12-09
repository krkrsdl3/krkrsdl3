#pragma once

#include "tjsNative.h"

//---------------------------------------------------------------------------
// tTJSNC_Window : TJS Window class
//---------------------------------------------------------------------------
class tTJSNC_Window : public tTJSNativeClass
{
public:
	tTJSNC_Window();
	static tjs_uint32 ClassID;

protected:
	tTJSNativeInstance* CreateNativeInstance();
	/*
		implement this in each platform.
		this must return a proper instance of tTJSNI_Window.
	*/
};
//---------------------------------------------------------------------------
extern tTJSNativeClass* TVPCreateNativeClass_Window();
/*
	implement this in each platform.
	this must return a proper instance of tTJSNI_Window.
	usually simple returns: new tTJSNC_Window();
*/
//---------------------------------------------------------------------------

