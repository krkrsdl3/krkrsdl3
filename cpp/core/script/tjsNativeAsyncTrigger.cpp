#include "tjsNativeAsyncTrigger.h"

#include "TVPEvent.h"

//---------------------------------------------------------------------------
// tTJSNC_AsyncTrigger
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_AsyncTrigger::ClassID = -1;
tTJSNC_AsyncTrigger::tTJSNC_AsyncTrigger() : inherited(TJS_W("AsyncTrigger"))
{
	// registration of native members

	TJS_BEGIN_NATIVE_MEMBERS(AsyncTrigger) // constructor
		TJS_DECL_EMPTY_FINALIZE_METHOD
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(/*var.name*/_this, /*var.type*/tTJSNI_AsyncTrigger,
			/*TJS class name*/AsyncTrigger)
	{
		return TJS_S_OK;
	}
	TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/AsyncTrigger)
		//----------------------------------------------------------------------

		//-- methods

		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/trigger)
	{
		TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_AsyncTrigger);
		_this->Trigger();
		return TJS_S_OK;
	}
	TJS_END_NATIVE_METHOD_DECL(/*func. name*/trigger)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/cancel)
	{
		TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_AsyncTrigger);
		_this->Cancel();
		return TJS_S_OK;
	}
	TJS_END_NATIVE_METHOD_DECL(/*func. name*/cancel)
		//----------------------------------------------------------------------

		//-- events

		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/onFire)
	{
		TJS_GET_NATIVE_INSTANCE(/*var. name*/_this,
			/*var. type*/tTJSNI_AsyncTrigger);

		tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
		if (obj.Object)
		{
			ttstr& actionname = _this->GetActionName();
			TVP_ACTION_INVOKE_BEGIN(0, "onFire", objthis);
			TVP_ACTION_INVOKE_END_NAME(obj,
				actionname.IsEmpty() ? NULL : actionname.c_str(),
				actionname.IsEmpty() ? NULL : actionname.GetHint());
		}

		return TJS_S_OK;
	}
	TJS_END_NATIVE_METHOD_DECL(/*func. name*/onFire)
		//----------------------------------------------------------------------

		//--properties

		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_PROP_DECL(cached)
	{
		TJS_BEGIN_NATIVE_PROP_GETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_AsyncTrigger);
			*result = _this->GetCached();
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_GETTER

			TJS_BEGIN_NATIVE_PROP_SETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_AsyncTrigger);
			_this->SetCached(*param);
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_SETTER
	}
	TJS_END_NATIVE_PROP_DECL(cached)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_PROP_DECL(mode)
	{
		TJS_BEGIN_NATIVE_PROP_GETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_AsyncTrigger);
			*result = (tjs_int)_this->GetMode();
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_GETTER

			TJS_BEGIN_NATIVE_PROP_SETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_AsyncTrigger);
			_this->SetMode((tTVPAsyncTriggerMode)(tjs_int)*param);
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_SETTER
	}
	TJS_END_NATIVE_PROP_DECL(mode)
		//----------------------------------------------------------------------
		TJS_END_NATIVE_MEMBERS

}
//---------------------------------------------------------------------------
tTJSNativeInstance* tTJSNC_AsyncTrigger::CreateNativeInstance()
{
	return new tTJSNI_AsyncTrigger();
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
tTJSNativeClass* TVPCreateNativeClass_AsyncTrigger()
{
	return new tTJSNC_AsyncTrigger();
}
//---------------------------------------------------------------------------