#include "tjsCommHead.h"
#include "tjsNativeStorages.h"

#include "TVPStorage.h"
#include "TVPMsg.h"
#include "Platform.h"
#include "TextStream.h"

//---------------------------------------------------------------------------
// tTJSNC_Storages
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_Storages::ClassID = -1;
tTJSNC_Storages::tTJSNC_Storages() : inherited(TJS_W("Storages"))
{
	// registration of native members

	TJS_BEGIN_NATIVE_MEMBERS(Storages)
		TJS_DECL_EMPTY_FINALIZE_METHOD
		//----------------------------------------------------------------------

		//-- methods

		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/addAutoPath)
	{
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		ttstr path = *param[0];

		TVPAddAutoPath(path);

		if (result) result->Clear();

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/addAutoPath)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/removeAutoPath)
	{
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		ttstr path = *param[0];

		TVPRemoveAutoPath(path);

		if (result) result->Clear();

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/removeAutoPath)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/getFullPath)
	{
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		ttstr path = *param[0];

		if (result)
			*result = TVPNormalizeStorageName(path);

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/getFullPath)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/getPlacedPath)
	{
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		ttstr path = *param[0];

		if (result)
			*result = TVPGetPlacedPath(path);

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/getPlacedPath)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/isExistentStorage)
	{
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		ttstr path = *param[0];

		if (result)
			*result = (tjs_int)TVPIsExistentStorage(path);

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/isExistentStorage)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/extractStorageExt)
	{
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		ttstr path = *param[0];

		if (result)
			*result = TVPExtractStorageExt(path);

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/extractStorageExt)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/extractStorageName)
	{
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		ttstr path = *param[0];

		if (result)
			*result = TVPExtractStorageName(path);

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/extractStorageName)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/extractStoragePath)
	{
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		ttstr path = *param[0];

		if (result)
			*result = TVPExtractStoragePath(path);

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/extractStoragePath)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/chopStorageExt)
	{
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		ttstr path = *param[0];

		if (result)
			*result = TVPChopStorageExt(path);

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/chopStorageExt)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/clearArchiveCache)
	{
		TVPClearArchiveCache();
		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/clearArchiveCache)
        //----------------------------------------------------------------------
        TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setTextEncoding)
        {
            if (numparams < 1)
                return TJS_E_BADPARAMCOUNT;
            else
            {
                tTJSVariant type = *param[0];
                if (type.Type() == tvtString)
                {
                    TVPSetDefaultReadEncoding(type);
                }
            }
            return TJS_S_OK;
        }
        TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ setTextEncoding)
        //----------------------------------------------------------------------
		TJS_END_NATIVE_MEMBERS
}
//---------------------------------------------------------------------------
tTJSNativeInstance* tTJSNC_Storages::CreateNativeInstance()
{
	// this class cannot create an instance
	TVPThrowExceptionMessage(TVPCannotCreateInstance);

	return NULL;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPSearchCD
//---------------------------------------------------------------------------
ttstr TVPSearchCD(const ttstr& name)
{
	// search CD which has specified volume label name.
	// return drive letter ( such as 'A' or 'B' )
	// return empty string if not found.
	return ttstr();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPCreateNativeClass_Storages
//---------------------------------------------------------------------------
tTJSNativeClass* TVPCreateNativeClass_Storages()
{
	tTJSNC_Storages* cls = new tTJSNC_Storages();


	// setup some platform-specific members
//----------------------------------------------------------------------

//-- methods

//----------------------------------------------------------------------
	TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/searchCD)
	{
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		if (result)
			*result = TVPSearchCD(*param[0]);

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(/*object to register*/cls,
		/*func. name*/searchCD)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/getLocalName)
	{
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		if (result)
		{
			ttstr str(TVPNormalizeStorageName(*param[0]));
			TVPGetLocalName(str);
			*result = str;
		}

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(/*object to register*/cls,
		/*func. name*/getLocalName)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/selectFile)
	{
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		iTJSDispatch2* dsp = param[0]->AsObjectNoAddRef();

		bool res = TVPSelectFile(dsp);

		if (result) *result = (tjs_int)res;

		return TJS_S_OK;
	}
	TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(/*object to register*/cls,
		/*func. name*/selectFile)
		//----------------------------------------------------------------------


		return cls;

}
//---------------------------------------------------------------------------