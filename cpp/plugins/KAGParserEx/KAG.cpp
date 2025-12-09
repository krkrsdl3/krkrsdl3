#include "tjsCommHead.h"
#include "KAGParserEx.hpp"
#include "ncbind/ncbind.hpp"

#define NCB_MODULE_NAME TJS_W("KAGParserEx.dll")

static iTJSDispatch2 *origKAGParser = NULL;

void kagparserex_init() {
    tTJSNI_KAGParserEX::initMethod();

    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(global) {
        tTJSVariant val;
        if(TJS_SUCCEEDED(global->PropGet(0, TVP_KAGPARSER_EX_CLASSNAME, NULL,
                                         &val, global))) {
            origKAGParser = val.AsObject();
            val.Clear();
        }
        iTJSDispatch2 *tjsclass = TVPCreateNativeClass_KAGParserEX();
        val = tTJSVariant(tjsclass);
        tjsclass->Release();
        global->PropSet(TJS_MEMBERENSURE, TVP_KAGPARSER_EX_CLASSNAME, NULL,
                        &val, global);
        global->Release();
    }
}

void kagparserex_done() {

    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(global) {
        global->DeleteMember(0, TVP_KAGPARSER_EX_CLASSNAME, NULL, global);
        if(origKAGParser) {
            tTJSVariant val(origKAGParser);
            origKAGParser->Release();
            origKAGParser = NULL;
            global->PropSet(TJS_MEMBERENSURE, TVP_KAGPARSER_EX_CLASSNAME, NULL,
                            &val, global);
        }
        global->Release();
    }

    tTJSNI_KAGParserEX::doneMethod();
}

NCB_PRE_REGIST_CALLBACK(kagparserex_init);
NCB_POST_UNREGIST_CALLBACK(kagparserex_done);
