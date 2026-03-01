#include "tjsNativeSystem.h"

#include "tjsCommHead.h"
#include "tjsArray.h"
#include "TVPSystem.h"
#include "TVPEvent.h"
#include "Platform.h"
#include "TickCount.h"
#include "TVPApplication.h"
#include "TVPScreen.h"
#include "TVPScript.h"
#include "TVPConfig.h"
#include "TVPGraphicsLoader.h"
#include "TVPMsg.h"
#include "Random.h"

#include "tjsNativeLayer.h"

//---------------------------------------------------------------------------
static ttstr TVPAppTitle;
static bool TVPAppTitleInit = false;
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_System
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_System::ClassID = -1;
tTJSNC_System::tTJSNC_System() : inherited(TJS_N("System"))
{
    // registration of native members

    TJS_BEGIN_NATIVE_MEMBERS(System)
    TJS_DECL_EMPTY_FINALIZE_METHOD
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL_NO_INSTANCE(/*TJS class name*/ System)
    {
        return TJS_S_OK;
    }
    TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/ System)
    //----------------------------------------------------------------------

    //-- methods

    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ terminate)
    {
        int code = numparams > 0 ? param[0]->AsInteger() : 0;
        if (!TVPStartupSuccess)
        {
            ;
        }
        else
        {
            TVPTerminateAsync(code);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ terminate)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ exit)
    {
        // this method does not return

        int code = numparams > 0 ? param[0]->AsInteger() : 0;
        if (!TVPStartupSuccess)
        {
            ;
        }
        else
        {
            TVPTerminateSync(code);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ exit)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ inputString)
    {
        if (numparams < 3)
            return TJS_E_BADPARAMCOUNT;

        ttstr value = *param[2];

        //	bool b = TVPInputQuery(*param[0], *param[1], value);

        ttstr caption = *param[0], prompt = *param[1];
        // this shows a dialog box which let user to input a string.
        // return false if the user selects "cancel", otherwise return true.
        // implement in each platform.
        std::vector<ttstr> btns;
        btns.emplace_back("OK");
        btns.emplace_back("Cancel");
        int ret = TVPShowSimpleInputBox(value, caption, prompt, btns);
        bool b = ret == 0; // the left button clicked

        if (result)
        {
            if (b)
                *result = value;
            else
                result->Clear();
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ inputString)
    //---------------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ addContinuousHandler)
    {
        // add function to continus handler list

        if (numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();

        TVPAddContinuousHandler(clo);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ addContinuousHandler)
    //---------------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ removeContinuousHandler)
    {
        // remove function from continuous handler list

        if (numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();

        TVPRemoveContinuousHandler(clo);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ removeContinuousHandler)
    //---------------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ toActualColor)
    {
        // convert color codes to 0xRRGGBB format.

        if (numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        if (result)
        {
            tjs_uint32 color = (tjs_int)(*param[0]);
            color = TVPToActualColor(color);
            *result = (tjs_int)color;
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ toActualColor)
    //---------------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ clearGraphicCache)
    {
        // clear graphic cache
        TVPClearGraphicCache();

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ clearGraphicCache)
    //---------------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ touchImages)
    {
        // try to cache graphics

        if (numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        std::vector<ttstr> storages;
        tTJSVariantClosure array = param[0]->AsObjectClosureNoAddRef();

        tjs_int count = 0;
        while (true)
        {
            tTJSVariant val;
            if (TJS_FAILED(array.Object->PropGetByNum(0, count, &val, array.ObjThis)))
                break;
            if (val.Type() == tvtVoid)
                break;
            storages.push_back(ttstr(val));
            count++;
        }

        tjs_int64 limit = 0;
        tjs_uint64 timeout = 0;

        if (numparams >= 2 && param[1]->Type() != tvtVoid)
            limit = (tjs_int64)*param[1];
        if (numparams >= 3 && param[2]->Type() != tvtVoid)
            timeout = (tjs_int64)*param[2];

        TVPTouchImages(storages, limit, timeout);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ touchImages)
    //---------------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ createUUID)
    {
        // create UUID
        // return UUID string in form of "43abda37-c597-4646-a279-c27a1373af90"

        tjs_uint8 uuid[16];

        TVPGetRandomBits128(uuid);

        uuid[8] &= 0x3f;
        uuid[8] |= 0x80; // override clock_seq

        uuid[6] &= 0x0f;
        uuid[6] |= 0x40; // override version

        tjs_char buf[40];
        TJS_snprintf(buf, sizeof(buf) / sizeof(tjs_char),
                     TJS_N("%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x"),
                     uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7],
                     uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);

        if (result)
            *result = tTJSVariant(buf);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ createUUID)
    //---------------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ assignMessage)
    {
        // assign system message

        if (numparams < 2)
            return TJS_E_BADPARAMCOUNT;

        ttstr id(*param[0]);
        ttstr msg(*param[1]);

        bool res = TJSAssignMessage(id.c_str(), msg.c_str());

        if (result)
            *result = tTJSVariant((tjs_int)res);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ assignMessage)
    //---------------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ doCompact)
    {
        // compact memory usage

        tjs_int level = TVP_COMPACT_LEVEL_MAX;

        if (numparams >= 1 && param[0]->Type() != tvtVoid)
            level = (tjs_int)*param[0];

        TVPDeliverCompactEvent(level);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ doCompact)
    //----------------------------------------------------------------------

    //--properties

    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_PROP_DECL(versionString){
        TJS_BEGIN_NATIVE_PROP_GETTER{* result = TVPGetVersionString();
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL(versionString)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(versionInformation){
    TJS_BEGIN_NATIVE_PROP_GETTER{* result = TVPGetVersionInformation();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL(versionInformation)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(eventDisabled){
    TJS_BEGIN_NATIVE_PROP_GETTER{* result = TVPGetSystemEventDisabledState();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TVPSetSystemEventDisabledState(param->operator bool());
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL(eventDisabled)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(graphicCacheLimit){
    TJS_BEGIN_NATIVE_PROP_GETTER{* result = (tjs_int)TVPGetGraphicCacheLimit();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TVPSetGraphicCacheLimit((tjs_int)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL(graphicCacheLimit)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(platformName){
    TJS_BEGIN_NATIVE_PROP_GETTER{* result = TVPGetPlatformName();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL(platformName)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(osName){TJS_BEGIN_NATIVE_PROP_GETTER{* result = TVPGetOSName();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL(osName)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(exitOnWindowClose){
    TJS_BEGIN_NATIVE_PROP_GETTER{* result = TVPTerminateOnWindowClose;
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TVPTerminateOnWindowClose = 0 != (tjs_int)*param;
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL(exitOnWindowClose)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(drawThreadNum){TJS_BEGIN_NATIVE_PROP_GETTER{* result = TVPDrawThreadNum;
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER
TJS_BEGIN_NATIVE_PROP_SETTER
{
    TVPDrawThreadNum = (tjs_int)*param;
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL(drawThreadNum)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(processorNum){
    TJS_BEGIN_NATIVE_PROP_GETTER{* result = TVPGetProcessorNum();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER
TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL(processorNum)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(exeBits){TJS_BEGIN_NATIVE_PROP_GETTER{
#ifdef TJS_64BIT_OS
        * result = 64;
#else
        * result = 32;
#endif
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER
TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL(exeBits)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(osBits){TJS_BEGIN_NATIVE_PROP_GETTER{* result = TVPGetOSBits();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER
TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL(osBits)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(exitOnNoWindowStartup){
    TJS_BEGIN_NATIVE_PROP_GETTER{* result = TVPTerminateOnNoWindowStartup;
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER
TJS_BEGIN_NATIVE_PROP_SETTER
{
    TVPTerminateOnNoWindowStartup = 0 != (tjs_int)*param;
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL(exitOnNoWindowStartup)
//----------------------------------------------------------------------
TJS_END_NATIVE_MEMBERS

// register default "exceptionHandler" member
tTJSVariant val((iTJSDispatch2*)NULL, (iTJSDispatch2*)NULL);
PropSet(TJS_MEMBERENSURE, TJS_N("exceptionHandler"), NULL, &val, this);

// and onActivate, onDeactivate
PropSet(TJS_MEMBERENSURE, TJS_N("onActivate"), NULL, &val, this);
PropSet(TJS_MEMBERENSURE, TJS_N("onDeactivate"), NULL, &val, this);
}
//---------------------------------------------------------------------------
tTJSNativeInstance* tTJSNC_System::CreateNativeInstance()
{
    // this class cannot create an instance
    TVPThrowExceptionMessage(TVPCannotCreateInstance);

    return NULL;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPCreateNativeClass_System
//---------------------------------------------------------------------------
tTJSNativeClass* TVPCreateNativeClass_System()
{
    tTJSNC_System* cls = new tTJSNC_System();

    // setup some platform-specific members
    //----------------------------------------------------------------------

    //-- methods

    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ inform)
    {
        // show simple message box
        if (numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        ttstr text = *param[0];

        ttstr caption;
        if (numparams >= 2 && param[1]->Type() != tvtVoid)
            caption = *param[1];
        else
            caption = TJS_N("Information");

        if (numparams >= 3 && param[2]->Type() != tvtVoid)
        {
            if (param[2]->Type() == tvtObject)
            { // vector of button
                tTJSArrayNI* ni;
                param[2]->AsObjectNoAddRef()->NativeInstanceSupport(
                    TJS_NIS_GETINSTANCE, TJSGetArrayClassID(), (iTJSNativeInstance**)&ni);
                std::vector<ttstr> vecButtons;
                vecButtons.reserve(ni->Items.size());
                for (const ttstr& label : ni->Items)
                {
                    vecButtons.emplace_back(label);
                }
                int ret = TVPShowSimpleMessageBox(text, caption, vecButtons);
                if (result)
                    result->operator=(ret);
            }
            else
            {
                int nButtons = param[2]->AsInteger();
                std::vector<ttstr> vecButtons;
                if (nButtons >= 1)
                    vecButtons.emplace_back(TJS_N("OK"));
                if (nButtons >= 2)
                    vecButtons.emplace_back(TJS_N("Cancel"));
                int ret = TVPShowSimpleMessageBox(text, caption, vecButtons);
                if (result)
                    result->operator=(ret);
            }
            return TJS_S_OK;
        }

        TVPShowSimpleMessageBox(text, caption);

        if (result)
            result->Clear();

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(/*object to register*/ cls,
                                            /*func. name*/ inform)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getTickCount)
    {
        if (result)
        {
            TVPStartTickCount();

            *result = (tjs_int64)TVPGetTickCount();
        }
        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(/*object to register*/ cls,
                                            /*func. name*/ getTickCount)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getKeyState)
    {
        if (numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        tjs_uint code = (tjs_int)*param[0];

        bool getcurrent = true;
        if (numparams >= 2)
            getcurrent = 0 != (tjs_int)*param[1];

        bool res = TVPGetAsyncKeyState(code, getcurrent);

        if (result)
            *result = (tjs_int)res;
        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(/*object to register*/ cls,
                                            /*func. name*/ getKeyState)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ shellExecute)
    {
        if (numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        ttstr target = *param[0];
        ttstr execparam;

        if (numparams >= 2)
            execparam = *param[1];

        bool res = TVPShellExecute(target, execparam);

        if (result)
            *result = (tjs_int)res;
        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(/*object to register*/ cls,
                                            /*func. name*/ shellExecute)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ system)
    {
        if (numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        ttstr target = *param[0];

        int ret = 0; // _wsystem(target.c_str());

        TVPDeliverCompactEvent(TVP_COMPACT_LEVEL_MAX); // this should clear all caches

        if (result)
            *result = (tjs_int)ret;
        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(/*object to register*/ cls,
                                            /*func. name*/ system)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ readRegValue)
    {
        if (numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        if (!result)
            return TJS_S_OK;

        ttstr key = *param[0];

        TVPReadRegValue(*result, key);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(/*object to register*/ cls,
                                            /*func. name*/ readRegValue)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getArgument)
    {
        if (numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        if (!result)
            return TJS_S_OK;

        ttstr name = *param[0];

        bool res = TVPGetCommandLine(name.c_str(), result);

        if (!res)
            result->Clear();

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(/*object to register*/ cls,
                                            /*func. name*/ getArgument)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setArgument)
    {
        if (numparams < 2)
            return TJS_E_BADPARAMCOUNT;

        ttstr name = *param[0];
        ttstr value = *param[1];

        TVPSetCommandLine(name.c_str(), value);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(/*object to register*/ cls,
                                            /*func. name*/ setArgument)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ createAppLock)
    {
        if (numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        if (!result)
            return TJS_S_OK;

        ttstr lockname = *param[0];

        bool res = TVPCreateAppLock(lockname);

        if (result)
            *result = (tjs_int)res;

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(/*object to register*/ cls,
                                            /*func. name*/ createAppLock)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ dumpHeap)
    {
        //	TVPHeapDump();
        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(/*object to register*/ cls,
                                            /*func. name*/ dumpHeap)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ nullpo)
    {
        // force make a null-po
#ifdef _MSC_VER
        *(int*)0 = 0;
#else
        __builtin_trap();
#endif

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(/*object to register*/ cls,
                                            /*func. name*/ nullpo)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ showVersion)
    {
        //	TVPShowVersionForm();
        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(/*object to register*/ cls,
                                            /*func. name*/ showVersion)
    //---------------------------------------------------------------------------

    //----------------------------------------------------------------------

    //-- properties

    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_PROP_DECL(exePath){TJS_BEGIN_NATIVE_PROP_GETTER{* result = TVPGetAppPath();
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL_OUTER(cls, exePath)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(personalPath){
    TJS_BEGIN_NATIVE_PROP_GETTER{* result = TVPGetPersonalPath();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL_OUTER(cls, personalPath)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(appDataPath){TJS_BEGIN_NATIVE_PROP_GETTER{* result = TVPGetAppDataPath();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL_OUTER(cls, appDataPath)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(dataPath){TJS_BEGIN_NATIVE_PROP_GETTER{* result = TVPDataPath;
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL_OUTER(cls, dataPath)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(exeName){
    TJS_BEGIN_NATIVE_PROP_GETTER{static ttstr exename(TVPNormalizeStorageName(ExePath()));
*result = exename;
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL_OUTER(cls, exeName)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(savedGamesPath){
    TJS_BEGIN_NATIVE_PROP_GETTER{* result = TVPGetSavedGamesPath();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL_OUTER(cls, savedGamesPath)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(title){
    TJS_BEGIN_NATIVE_PROP_GETTER{if (!TVPAppTitleInit){TVPAppTitleInit = true;
TVPAppTitle = Application->GetTitle();
}
*result = TVPAppTitle;
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TVPAppTitle = *param;
    Application->SetTitle(TVPAppTitle.AsStdString());
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL_OUTER(cls, title)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(screenWidth){
    TJS_BEGIN_NATIVE_PROP_GETTER{* result = tTVPScreen::GetWidth();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL_OUTER(cls, screenWidth)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(screenHeight){
    TJS_BEGIN_NATIVE_PROP_GETTER{* result = tTVPScreen::GetHeight();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL_OUTER(cls, screenHeight)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(desktopLeft){
    TJS_BEGIN_NATIVE_PROP_GETTER{* result = tTVPScreen::GetDesktopLeft();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL_OUTER(cls, desktopLeft)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(desktopTop){
    TJS_BEGIN_NATIVE_PROP_GETTER{* result = tTVPScreen::GetDesktopTop();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL_OUTER(cls, desktopTop)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(desktopWidth){
    TJS_BEGIN_NATIVE_PROP_GETTER{* result = tTVPScreen::GetDesktopWidth();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL_OUTER(cls, desktopWidth)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(desktopHeight){
    TJS_BEGIN_NATIVE_PROP_GETTER{* result = tTVPScreen::GetDesktopHeight();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL_OUTER(cls, desktopHeight)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(touchDevice){
    TJS_BEGIN_NATIVE_PROP_GETTER{* result = TVPGetSupportTouchDevice();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL_OUTER(cls, touchDevice)
//----------------------------------------------------------------------

return cls;
}
//---------------------------------------------------------------------------