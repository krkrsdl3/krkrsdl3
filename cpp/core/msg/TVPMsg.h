//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Definition of Messages and Message Related Utilities
//---------------------------------------------------------------------------
#ifndef MsgIntfH
#define MsgIntfH

#include "tjs.h"
#include "tjsMessage.h"

#ifndef TVP_MSG_DECL
	#define TVP_MSG_DECL(name, msg) extern tTJSMessageHolder name;
	#define TVP_MSG_DECL_CONST(name, msg) extern tTJSMessageHolder name;
	#define TVP_MSG_DECL_NULL(name) extern tTJSMessageHolder name;
#endif

#include "tvp_msg.h"

#define WIDEN2(x) TJS_W(x)
#define WIDEN(x) WIDEN2(x)


//---------------------------------------------------------------------------
// Message Strings
//---------------------------------------------------------------------------
#include "tvp_msg_inc.h"

//---------------------------------------------------------------------------
// Utility Functions
//---------------------------------------------------------------------------
ttstr TVPFormatMessage(const tjs_char* msg, const ttstr& p1);
ttstr TVPFormatMessage(const tjs_char* msg, const ttstr& p1, const ttstr& p2);
void TVPThrowExceptionMessage(const tjs_char* msg);
void TVPThrowExceptionMessage(const tjs_char* msg, const ttstr& p1, tjs_int num);
void TVPThrowExceptionMessage(const tjs_char* msg, const ttstr& p1);
void TVPThrowExceptionMessage(const tjs_char* msg, const ttstr& p1, const ttstr& p2);

ttstr TVPGetAboutString();
ttstr TVPGetVersionInformation();
ttstr TVPGetVersionString();

#define TVPThrowInternalError \
	TVPThrowExceptionMessage(TVPInternalError, __FILE__,  __LINE__)
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// version retrieving
//---------------------------------------------------------------------------
extern tjs_int TVPVersionMajor;
extern tjs_int TVPVersionMinor;
extern tjs_int TVPVersionRelease;
extern tjs_int TVPVersionBuild;
//---------------------------------------------------------------------------
void TVPGetVersion();
/*
	implement in each platforms;
	fill these four version field.
*/
//---------------------------------------------------------------------------
void TVPGetSystemVersion(tjs_int& major, tjs_int& minor, tjs_int& release, tjs_int& build);
void TVPGetTJSVersion(tjs_int& major, tjs_int& minor, tjs_int& release);
//---------------------------------------------------------------------------


#endif
