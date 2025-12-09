#include "tjsCommHead.h"
#include "psbfile.h"
#include "ncbind/ncbind.hpp"

#include "psbMedia.h"
#include "Log.h"

#define NCB_MODULE_NAME TJS_W("psbfile.dll")

void psbfile_init()
{
    // 绑定psbfile类
    iTJSDispatch2* global = TVPScriptEngine->GetGlobalNoAddRef();
    if (global)
    {
        iTJSDispatch2* dsp = TVPCreateNativeClass_PsbFile();
        tTJSVariant val = tTJSVariant(dsp);
        dsp->Release();
        global->PropSet(TJS_MEMBERENSURE, TJS_W("PSBFile"), NULL, &val, global);
    }
    // 注册media
    if (psbVar == nullptr)
    {
        psbVar = new PSB::PSBMedia();
        TVPRegisterStorageMedia(psbVar);
    }
}

void psbfile_done()
{
    // 解绑psbfile类
    iTJSDispatch2* global = TVPScriptEngine->GetGlobalNoAddRef();
    if (global)
    {
        global->DeleteMember(0, TJS_W("PSBFile"), NULL, global);
    }
    // 解注media
    if (psbVar != nullptr)
    {
        TVPUnregisterStorageMedia(psbVar);
        psbVar->Release();
        psbVar = nullptr;
    }
}

NCB_PRE_REGIST_CALLBACK(psbfile_init);
NCB_POST_UNREGIST_CALLBACK(psbfile_done);