#include "tjsNativePlugins.h"

#include "TVPMsg.h"
#include "tjsArray.h"
#include "TVPPlugin.h"


//---------------------------------------------------------------------------
// TVPCreateNativeClass_Plugins
//---------------------------------------------------------------------------
tTJSNativeClass* TVPCreateNativeClass_Plugins()
{
	tTJSNC_Plugins* cls = new tTJSNC_Plugins();


	// setup some platform-specific members
//---------------------------------------------------------------------------

//-- methods

//----------------------------------------------------------------------
	TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/link)
	{
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		ttstr name = *param[0];

		TVPLoadPlugin(name);

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(/*object to register*/cls,
		/*func. name*/link)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/unlink)
	{
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		ttstr name = *param[0];

		bool res = TVPUnloadPlugin(name);

		if (result) *result = (tjs_int)res;

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(/*object to register*/cls,
		/*func. name*/unlink)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(getList)
	{
		iTJSDispatch2* array = TJSCreateArrayObject();
		try
		{
			tjs_int idx = 0;
			for (ttstr name : TVPRegisteredPlugins) {
				tTJSVariant val(name);
				array->PropSetByNum(TJS_MEMBERENSURE, idx++, &val, array);
			}
			if (result) *result = tTJSVariant(array, array);
		}
		catch (...)
		{
			array->Release();
			throw;
		}
		array->Release();
		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(cls, getList)
		//---------------------------------------------------------------------------


		//---------------------------------------------------------------------------
		return cls;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_Plugins
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_Plugins::ClassID = -1;
tTJSNC_Plugins::tTJSNC_Plugins() : inherited(TJS_W("Plugins"))
{
	// registration of native members

	TJS_BEGIN_NATIVE_MEMBERS(Plugins)
		TJS_DECL_EMPTY_FINALIZE_METHOD
		//----------------------------------------------------------------------

		//-- methods

		//----------------------------------------------------------------------

		//--properties

		//---------------------------------------------------------------------------

		TJS_END_NATIVE_MEMBERS
}
//---------------------------------------------------------------------------
tTJSNativeInstance* tTJSNC_Plugins::CreateNativeInstance()
{
	// this class cannot create an instance
	TVPThrowExceptionMessage(TVPCannotCreateInstance);
	return NULL;
}
//---------------------------------------------------------------------------