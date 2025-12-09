#include "tjsCommHead.h"
#include "tjsNativeScript.h"

#include "TVPScript.h"
#include "TVPMsg.h"
#include "tjsDebug.h"
#include "tjsArray.h"
#include "TextStream.h"

//---------------------------------------------------------------------------
// tTJSNC_Scripts
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_Scripts::ClassID = -1;
tTJSNC_Scripts::tTJSNC_Scripts() : inherited(TJS_W("Scripts"))
{
	// registration of native members

	TJS_BEGIN_NATIVE_MEMBERS(Scripts)
		TJS_DECL_EMPTY_FINALIZE_METHOD
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL_NO_INSTANCE(/*TJS class name*/Scripts)
	{
		return TJS_S_OK;
	}
	TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/Scripts)
		//----------------------------------------------------------------------

		//-- methods

		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/execStorage)
	{
		// execute script which stored in storage
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		ttstr name = *param[0];

		ttstr modestr;
		if (numparams >= 2 && param[1]->Type() != tvtVoid)
			modestr = *param[1];

		iTJSDispatch2* context = numparams >= 3 && param[2]->Type() != tvtVoid ? param[2]->AsObjectNoAddRef() : NULL;

		TVPExecuteStorage(name, context, result, false, modestr.c_str());

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/execStorage)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/evalStorage)
	{
		// execute expression which stored in storage
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		ttstr name = *param[0];

		ttstr modestr;
		if (numparams >= 2 && param[1]->Type() != tvtVoid)
			modestr = *param[1];

		iTJSDispatch2* context = numparams >= 3 && param[2]->Type() != tvtVoid ? param[2]->AsObjectNoAddRef() : NULL;

		TVPExecuteStorage(name, context, result, true, modestr.c_str());

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/evalStorage)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/compileStorage) // bytecode
	{
		if (numparams < 2) return TJS_E_BADPARAMCOUNT;

		ttstr name = *param[0];
		ttstr output = *param[1];

		bool isresult = false;
		if (numparams >= 3 && (tjs_int)*param[2]) {
			isresult = true;
		}

		bool outputdebug = false;
		if (numparams >= 4 && (tjs_int)*param[3]) {
			outputdebug = true;
		}

		bool isexpression = false;
		if (numparams >= 5 && (tjs_int)*param[4]) {
			isexpression = true;
		}
		TVPCompileStorage(name, isresult, outputdebug, isexpression, output);

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/compileStorage)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/exec)
	{
		// execute given string as a script
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		ttstr content = *param[0];

		ttstr name;
		tjs_int lineofs = 0;
		if (numparams >= 2 && param[1]->Type() != tvtVoid) name = *param[1];
		if (numparams >= 3 && param[2]->Type() != tvtVoid) lineofs = *param[2];

		iTJSDispatch2* context = numparams >= 4 && param[3]->Type() != tvtVoid ? param[3]->AsObjectNoAddRef() : NULL;

		if (TVPScriptEngine)
			TVPScriptEngine->ExecScript(content, result, context,
				&name, lineofs);
		else
			TVPThrowInternalError;

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/exec)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/eval)
	{
		// execute given string as a script
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		ttstr content = *param[0];

		ttstr name;
		tjs_int lineofs = 0;
		if (numparams >= 2 && param[1]->Type() != tvtVoid) name = *param[1];
		if (numparams >= 3 && param[2]->Type() != tvtVoid) lineofs = *param[2];

		iTJSDispatch2* context = numparams >= 4 && param[3]->Type() != tvtVoid ? param[3]->AsObjectNoAddRef() : NULL;

		if (TVPScriptEngine)
			TVPScriptEngine->EvalExpression(content, result, context,
				&name, lineofs);
		else
			TVPThrowInternalError;

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/eval)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/dump)
	{
		// execute given string as a script
		TVPDumpScriptEngine();

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/dump)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/getTraceString)
	{
		// get current stack trace as string
		tjs_int limit = 0;

		if (numparams >= 1 && param[0]->Type() != tvtVoid)
			limit = *param[0];

		if (result)
		{
			*result = TJSGetStackTraceString(limit);
		}

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/getTraceString)
		//----------------------------------------------------------------------
#ifdef TJS_DEBUG_DUMP_STRING
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/dumpStringHeap)
	{
		// dump all strings held by TJS2 framework
		TJSDumpStringHeap();

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/dumpStringHeap)
#endif
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/setCallMissing) /* UNDOCUMENTED: subject to change */
	{
		// set to call "missing" method
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		iTJSDispatch2* dsp = param[0]->AsObjectNoAddRef();

		if (dsp)
		{
			tTJSVariant missing(TJS_W("missing"));
			dsp->ClassInstanceInfo(TJS_CII_SET_MISSING, 0, &missing);
		}

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/setCallMissing) /* UNDOCUMENTED: subject to change */
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/getClassNames) /* UNDOCUMENTED: subject to change */
	{
		// get class name as an array, last (most end) class first.
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		iTJSDispatch2* dsp = param[0]->AsObjectNoAddRef();

		if (dsp)
		{
			iTJSDispatch2* array = TJSCreateArrayObject();
			try
			{
				tjs_uint num = 0;
				while (true)
				{
					tTJSVariant val;
					tjs_error err = dsp->ClassInstanceInfo(TJS_CII_GET, num, &val);
					if (TJS_FAILED(err)) break;
					array->PropSetByNum(TJS_MEMBERENSURE, num, &val, array);
					num++;
				}
				if (result) *result = tTJSVariant(array, array);
			}
			catch (...)
			{
				array->Release();
				throw;
			}
			array->Release();
		}
		else
		{
			return TJS_E_FAIL;
		}

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/getClassNames) /* UNDOCUMENTED: subject to change */
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_PROP_DECL(textEncoding)
	{
		TJS_BEGIN_NATIVE_PROP_GETTER
		{
			*result = TVPGetDefaultReadEncoding();
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_GETTER
			TJS_BEGIN_NATIVE_PROP_SETTER
		{
			TVPSetDefaultReadEncoding(*param);
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_SETTER
	}
	TJS_END_NATIVE_STATIC_PROP_DECL(textEncoding)
		//----------------------------------------------------------------------

		TJS_END_NATIVE_MEMBERS
}
//---------------------------------------------------------------------------
tTJSNativeInstance* tTJSNC_Scripts::CreateNativeInstance()
{
	// this class cannot create an instance
	TVPThrowExceptionMessage(TVPCannotCreateInstance);

	return NULL;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPCreateNativeClass_Scripts
//---------------------------------------------------------------------------
tTJSNativeClass* TVPCreateNativeClass_Scripts()
{
	tTJSNC_Scripts* cls = new tTJSNC_Scripts();

	// setup some platform-specific members

//----------------------------------------------------------------------

// currently none

//----------------------------------------------------------------------
	return cls;
}
//---------------------------------------------------------------------------