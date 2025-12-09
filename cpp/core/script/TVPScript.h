//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// TJS2 Script Managing
//---------------------------------------------------------------------------
#ifndef ScriptMgnImtfH
#define ScriptMgnImtfH

#include "tjs.h"

#include "tjsNative.h"
#include "tjsError.h"

//---------------------------------------------------------------------------
// implementation in this unit
//---------------------------------------------------------------------------
extern ttstr TVPStartupScriptName;
extern bool TVPStartupSuccess;

extern void TVPInitScriptEngine();
extern void TVPUninitScriptEngine();
extern void TVPRestartScriptEngine();
extern tTJS*  TVPGetScriptEngine();
extern iTJSDispatch2* TVPGetScriptDispatch();
extern void TVPExecuteScript(const ttstr& content, tTJSVariant* result = 0);
extern void TVPExecuteScript(const ttstr& content, iTJSDispatch2* context, tTJSVariant* result = 0);
extern void TVPExecuteExpression(const ttstr& content, tTJSVariant* result = 0);
extern void TVPExecuteExpression(const ttstr& content, iTJSDispatch2* context, tTJSVariant* result = 0);
extern void TVPExecuteScript(const ttstr& content, const ttstr& name, tjs_int lineofs, tTJSVariant* result = 0);
extern void TVPExecuteScript(const ttstr& content, const ttstr& name, tjs_int lineofs, iTJSDispatch2* context, tTJSVariant* result = 0);
extern void TVPExecuteExpression(const ttstr& content, const ttstr& name, tjs_int lineofs, tTJSVariant* result = 0);
extern void TVPExecuteExpression(const ttstr& content, const ttstr& name, tjs_int lineofs, iTJSDispatch2* context, tTJSVariant* result = 0);
extern void TVPExecuteStorage(const ttstr& name, tTJSVariant* result = 0, bool isexpression = false, const tjs_char* modestr = 0);
extern void TVPExecuteStorage(const ttstr& name, iTJSDispatch2* context, tTJSVariant* result = 0, bool isexpression = false, const tjs_char* modestr = 0);
extern void TVPExecuteBinaryStream(tTJSBinaryStream* stream, const ttstr& fileName, tTJSVariant* result = 0, bool isexpression = false, const tjs_char* modestr = 0);
extern void TVPDumpScriptEngine();

extern void TVPExecuteBytecode(const tjs_uint8* content, size_t len, iTJSDispatch2* context, tTJSVariant* result = 0, const tjs_char* name = 0);

extern void TVPExecuteStartupScript();
extern void TVPCreateMessageMapFile(const ttstr& filename);

//---------------------------------------------------------------------------




//---------------------------------------------------------------------------
// implementation in each platform to show script exception message
//---------------------------------------------------------------------------
extern void TVPShowScriptException(eTJS &e);
extern void TVPShowScriptException(eTJSScriptError &e);
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// TVPBeforeProcessUnhandledException (implementation in each platform)
//---------------------------------------------------------------------------
extern void TVPBeforeProcessUnhandledException();
//---------------------------------------------------------------------------




//---------------------------------------------------------------------------
// unhandled exception handler related macros
//---------------------------------------------------------------------------
extern bool TVPProcessUnhandledException(eTJSScriptException &e);
extern bool TVPProcessUnhandledException(eTJSScriptError &e);
extern bool TVPProcessUnhandledException(eTJS &e);
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TVPInitializeStartupScript
//---------------------------------------------------------------------------
extern void TVPInitializeStartupScript();
extern bool TVPCheckProcessLog();

//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// unhandled exception handler related macros
//---------------------------------------------------------------------------
#define TVP_CATCH_AND_SHOW_SCRIPT_EXCEPTION(origin) \
	catch(eTJSScriptException &e) \
	{ \
		TVPBeforeProcessUnhandledException(); \
		e.AddTrace(ttstr(origin)); \
		if(!TVPProcessUnhandledException(e)) \
			TVPShowScriptException(e); \
	} \
	catch(eTJSScriptError &e) \
	{ \
		TVPBeforeProcessUnhandledException(); \
		e.AddTrace(ttstr(origin)); \
		if(!TVPProcessUnhandledException(e)) \
			TVPShowScriptException(e); \
	} \
	catch(eTJS &e) \
	{ \
		TVPBeforeProcessUnhandledException(); \
		if(!TVPProcessUnhandledException(e)) \
			TVPShowScriptException(e); \
	} \
	catch(...) \
	{ \
		TVPBeforeProcessUnhandledException(); \
		throw; \
	}
#define TVP_CATCH_AND_SHOW_SCRIPT_EXCEPTION_FORCE_SHOW_EXCEPTION(origin) \
	catch(eTJSScriptError &e) \
	{ \
		TVPBeforeProcessUnhandledException(); \
		e.AddTrace(ttstr(origin)); \
		TVPShowScriptException(e); \
	} \
	catch(eTJS &e) \
	{ \
		TVPBeforeProcessUnhandledException(); \
		TVPShowScriptException(e); \
	} \
	catch(...) \
	{ \
		TVPBeforeProcessUnhandledException(); \
		throw; \
	}
//---------------------------------------------------------------------------

extern tTJS* TVPScriptEngine;
void TVPCompileStorage(const ttstr& name, bool isrequestresult, bool outputdebug, bool isexpression, const ttstr& outputpath);

#endif
