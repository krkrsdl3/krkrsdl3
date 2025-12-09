#include "tjsNativeDebug.h"

#include "tjsCommHead.h"
#include "TVPDebug.h"

//---------------------------------------------------------------------------
// on-error hook
//---------------------------------------------------------------------------
void TVPOnErrorHook()
{
	//	if(TVPMainForm) TVPMainForm->NotifySystemError();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_Controller : TJS Controller Class
//---------------------------------------------------------------------------
class tTJSNC_Controller : public tTJSNativeClass
{
public:
	tTJSNC_Controller();

	static tjs_uint32 ClassID;
};
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_Controller::ClassID = (tjs_uint32)-1;
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
tTJSNC_Controller::tTJSNC_Controller() : tTJSNativeClass(TJS_W("Controller"))
{
	TJS_BEGIN_NATIVE_MEMBERS(Debug)
		TJS_DECL_EMPTY_FINALIZE_METHOD
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL_NO_INSTANCE(/*TJS class name*/Controller)
	{
		return TJS_S_OK;
	}
	TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/Controller)
		//----------------------------------------------------------------------

		// properties

		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_PROP_DECL(visible)
	{
		TJS_BEGIN_NATIVE_PROP_GETTER
		{
			*result = 0;// TVPMainForm->getVisible();
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_GETTER

			TJS_BEGIN_NATIVE_PROP_SETTER
		{
			//TVPMainForm->setVisible(param->operator bool());
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_SETTER
	}
	TJS_END_NATIVE_STATIC_PROP_DECL(visible)
		//----------------------------------------------------------------------
		TJS_END_NATIVE_MEMBERS

}
//---------------------------------------------------------------------------
static iTJSDispatch2* TVPGetControllerClass()
{
	struct tClassHolder
	{
		iTJSDispatch2* Object;
		tClassHolder() { Object = new tTJSNC_Controller(); }
		~tClassHolder() { Object->Release(); }
	} static Holder;

	Holder.Object->AddRef();
	return Holder.Object;
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// tTJSNC_Console : TJS Console Class
//---------------------------------------------------------------------------
class tTJSNC_Console : public tTJSNativeClass
{
public:
	tTJSNC_Console();

	static tjs_uint32 ClassID;
};
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_Console::ClassID = (tjs_uint32)-1;
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
tTJSNC_Console::tTJSNC_Console() : tTJSNativeClass(TJS_W("Console"))
{
	TJS_BEGIN_NATIVE_MEMBERS(Debug)
		TJS_DECL_EMPTY_FINALIZE_METHOD
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL_NO_INSTANCE(/*TJS class name*/Console)
	{
		return TJS_S_OK;
	}
	TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/Console)
		//----------------------------------------------------------------------

		// properties

		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_PROP_DECL(visible)
	{
		TJS_BEGIN_NATIVE_PROP_GETTER
		{
			//		*result = TVPMainForm->GetConsoleVisible();
					*result = false;
					return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_GETTER

			TJS_BEGIN_NATIVE_PROP_SETTER
		{
			bool visible = param->operator bool();
		//		TVPMainForm->SetConsoleVisible(visible);
				return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_SETTER
	}
	TJS_END_NATIVE_STATIC_PROP_DECL(visible)
		//----------------------------------------------------------------------
		TJS_END_NATIVE_MEMBERS

}
//---------------------------------------------------------------------------
static iTJSDispatch2* TVPGetConsoleClass()
{
	struct tClassHolder
	{
		iTJSDispatch2* Object;
		tClassHolder() { Object = new tTJSNC_Console(); }
		~tClassHolder() { Object->Release(); }
	} static Holder;

	Holder.Object->AddRef();
	return Holder.Object;
}
//---------------------------------------------------------------------------




//---------------------------------------------------------------------------
// tTJSNC_Debug
//---------------------------------------------------------------------------
tTJSNativeInstance* tTJSNC_Debug::CreateNativeInstance()
{
	return NULL;
}
//---------------------------------------------------------------------------
tTJSNativeClass* TVPCreateNativeClass_Debug()
{
	tTJSNativeClass* cls = new tTJSNC_Debug();

	//----------------------------------------------------------------------
	TJS_BEGIN_NATIVE_PROP_DECL(controller)
	{
		TJS_BEGIN_NATIVE_PROP_GETTER
		{
			iTJSDispatch2 * dsp = TVPGetControllerClass();
			*result = tTJSVariant(dsp, dsp);
			dsp->Release();
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_GETTER

			TJS_DENY_NATIVE_PROP_SETTER
	}
	TJS_END_NATIVE_STATIC_PROP_DECL_OUTER(cls, controller)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_PROP_DECL(console)
	{
		TJS_BEGIN_NATIVE_PROP_GETTER
		{
			iTJSDispatch2 * dsp = TVPGetConsoleClass();
			*result = tTJSVariant(dsp, dsp);
			dsp->Release();
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_GETTER

			TJS_DENY_NATIVE_PROP_SETTER
	}
	TJS_END_NATIVE_STATIC_PROP_DECL_OUTER(cls, console)
		//----------------------------------------------------------------------
		return cls;
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// tTJSNC_Debug
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_Debug::ClassID = -1;
tTJSNC_Debug::tTJSNC_Debug() : tTJSNativeClass(TJS_W("Debug"))
{
	TJS_BEGIN_NATIVE_MEMBERS(Debug)
		TJS_DECL_EMPTY_FINALIZE_METHOD
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL_NO_INSTANCE(/*TJS class name*/Debug)
	{
		return TJS_S_OK;
	}
	TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/Debug)
		//----------------------------------------------------------------------

		//-- methods

		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/message)
	{
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		if (numparams == 1)
		{
			TVPAddLog(*param[0]);
		}
		else
		{
			// display the arguments separated with ", "
			ttstr args;
			for (int i = 0; i < numparams; i++)
			{
				if (i != 0) args += TJS_W(", ");
				args += ttstr(*param[i]);
			}
			TVPAddLog(args);
		}

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/message)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/notice)
	{
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		if (numparams == 1)
		{
			TVPAddImportantLog(*param[0]);
		}
		else
		{
			// display the arguments separated with ", "
			ttstr args;
			for (int i = 0; i < numparams; i++)
			{
				if (i != 0) args += TJS_W(", ");
				args += ttstr(*param[i]);
			}
			TVPAddImportantLog(args);
		}

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/notice)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/startLogToFile)
	{
		bool clear = false;

		if (numparams >= 1)
			clear = param[0]->operator bool();

		TVPStartLogToFile(clear);

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/startLogToFile)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/logAsError)
	{
		TVPOnError();

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/logAsError)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/addLoggingHandler)
	{
		// add function to logging handler list

		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();

		TVPAddLoggingHandler(clo);

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/addLoggingHandler)
		//---------------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/removeLoggingHandler)
	{
		// remove function from logging handler list

		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();

		TVPRemoveLoggingHandler(clo);

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/removeLoggingHandler)
		//---------------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/getLastLog)
	{
		tjs_uint lines = TVPLogMaxLines + 100;

		if (numparams >= 1) lines = (tjs_uint)param[0]->AsInteger();

		if (result) *result = TVPGetLastLog(lines);

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/getLastLog)
		//---------------------------------------------------------------------------

		//-- properies

		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_PROP_DECL(logLocation)
	{
		TJS_BEGIN_NATIVE_PROP_GETTER
		{
			*result = TVPLogLocation;
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_GETTER

			TJS_BEGIN_NATIVE_PROP_SETTER
		{
			TVPSetLogLocation(*param);
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_SETTER
	}
	TJS_END_NATIVE_STATIC_PROP_DECL(logLocation)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_PROP_DECL(logToFileOnError)
	{
		TJS_BEGIN_NATIVE_PROP_GETTER
		{
			*result = TVPAutoLogToFileOnError;
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_GETTER

			TJS_BEGIN_NATIVE_PROP_SETTER
		{
			TVPAutoLogToFileOnError = param->operator bool();
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_SETTER
	}
	TJS_END_NATIVE_STATIC_PROP_DECL(logToFileOnError)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_PROP_DECL(clearLogFileOnError)
	{
		TJS_BEGIN_NATIVE_PROP_GETTER
		{
			*result = TVPAutoClearLogOnError;
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_GETTER

			TJS_BEGIN_NATIVE_PROP_SETTER
		{
			TVPAutoClearLogOnError = param->operator bool();
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_SETTER
	}
	TJS_END_NATIVE_STATIC_PROP_DECL(clearLogFileOnError)
		//----------------------------------------------------------------------

		//----------------------------------------------------------------------
		TJS_END_NATIVE_MEMBERS

		// put version information to DMS
} // end of tTJSNC_Debug::tTJSNC_Debug
//---------------------------------------------------------------------------