#include <stdio.h>
#include <list>
#include <map>

#define NCB_MODULE_NAME TJS_W("perspective.dll")

static const char *copyright = 
"----- AntiGrainGeometry Copyright START -----\n"
"Anti-Grain Geometry - Version 2.4\n"
"Copyright (C) 2002-2005 Maxim Shemanarev (McSeem)\n"
"\n"
"Permission to copy, use, modify, sell and distribute this software\n"
"is granted provided this copyright notice appears in all copies. \n"
"This software is provided \"as is\" without express or implied\n"
"warranty, and with no claim as to its suitability for any purpose.\n"
"----- AntiGrainGeometry Copyright END -----\n";

#include "ncbind/ncbind.hpp"

#include "LayerExBase.h"
#include "tjsNative.h"
#include "RenderManager.h"

#include "tjsNativeLayer.h"

static int TJS_NATIVE_CLASSID_NAME = -1;

static tjs_error PerspectiveCopy_GL( tTJSVariant *result, 
    tjs_int numparams, tTJSVariant **param,	iTJSDispatch2 *objthis)
{
    if(!objthis) return TJS_E_NATIVECLASSCRASH;
    tTJSNI_Layer *_this;
    { 
        tjs_error hr; 
        hr = objthis->NativeInstanceSupport(TJS_NIS_GETINSTANCE, 
            tTJSNC_Layer::ClassID, (iTJSNativeInstance**)&_this); 
        if(TJS_FAILED(hr)) return TJS_E_NATIVECLASSCRASH; 
    }

    if (numparams < 13) return TJS_E_BADPARAMCOUNT;

    tTJSNI_BaseLayer * src = NULL;
    tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();
    if(clo.Object)
    {
        if(TJS_FAILED(clo.Object->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
            tTJSNC_Layer::ClassID, (iTJSNativeInstance**)&src)))
            TVPThrowExceptionMessage(TVPSpecifyLayer);
    }

    double g_x1 = param[1]->AsReal();
    double g_y1 = param[2]->AsReal();
    double g_x2 = g_x1 + param[3]->AsReal();
    double g_y2 = g_y1 + param[4]->AsReal();

    tTVPPointD srcpt[4] = {
        { g_x1, g_y1 },
        { g_x2, g_y1 },
		{ g_x1, g_y2 },
        { g_x2, g_y2 },
    };
    tTVPPointD dstpt[4] = {
        { param[5 ]->AsReal(), param[6 ]->AsReal() }, // 左上 
        { param[7 ]->AsReal(), param[8 ]->AsReal() }, // 右上 
		{ param[9]->AsReal(), param[10]->AsReal() }, // 左下 
        { param[11]->AsReal(), param[12]->AsReal() }, // 右下 
    };
	static iTVPRenderMethod *method = TVPGetRenderManager()->GetRenderMethod("PerspectiveAlphaBlend_a");
	static int id_opa = method->EnumParameterID("opacity");
	method->SetParameterOpa(id_opa, 255);
	iTVPTexture2D *tex = src->GetMainImage()->GetTexture();
	tRenderTexQuadArray::Element src_tex[] = {
		tRenderTexQuadArray::Element(tex, srcpt)
	};
	int w = _this->GetMainImage()->GetWidth(), h = _this->GetMainImage()->GetHeight();
	tTVPRect rctar(w, h, 0, 0);
	for (int i = 0; i < 4; ++i) {
		if (rctar.left > dstpt[i].x) rctar.left = dstpt[i].x;
		if (rctar.top > dstpt[i].y) rctar.top = dstpt[i].y;
		if (rctar.right < dstpt[i].x) rctar.right = dstpt[i].x;
		if (rctar.bottom < dstpt[i].y) rctar.bottom = dstpt[i].y;
	}
	if (rctar.left < 0) rctar.left = 0;
	if (rctar.top < 0) rctar.top = 0;
	if (rctar.right > w) rctar.right = w;
	if (rctar.bottom > h) rctar.bottom = h;
	TVPGetRenderManager()->OperatePerspective(method, 1,
		_this->GetMainImage()->GetTextureForRender(method->IsBlendTarget(), &rctar), nullptr, rctar, dstpt,
		tRenderTexQuadArray(src_tex));
	_this->Update();
    return TJS_S_OK;
}

static void
addMethod(iTJSDispatch2 *dispatch, const tjs_char *methodName, tTJSDispatch *method)
{
	tTJSVariant var = tTJSVariant(method);
	method->Release();
	dispatch->PropSet(
		TJS_MEMBERENSURE, // メンバがなかった場合には作成するようにするフラグ 
		methodName, // メンバ名 ( かならず TJS_W( ) で囲む ) 
		NULL, // ヒント ( 本来はメンバ名のハッシュ値だが、NULL でもよい ) 
		&var, // 登録する値 
		dispatch // コンテキスト 
		);
}

static void
delMethod(iTJSDispatch2 *dispatch, const tjs_char *methodName)
{
	dispatch->DeleteMember(
		0, // フラグ ( 0 でよい ) 
		methodName, // メンバ名 
		NULL, // ヒント 
		dispatch // コンテキスト 
		);
}

//--------------------------------------------------------------------------- 
void InitPlugin_Perspective()
{
// 	if (TVPGetRenderManager()->IsSoftware()) { 
// 	    TVPAddImportantLog(ttstr(copyright)); 
// 	 
// 	    // クラスオブジェクトチェック 
// 	    if ((NI_LayerExBase::classId = TJSFindNativeClassID(TJS_W("LayerExBase"))) <= 0) { 
// 		    NI_LayerExBase::classId = TJSRegisterNativeClass(TJS_W("LayerExBase")); 
// 	    } 
// 	 
// 	    { 
// 		    // TJS のグローバルオブジェクトを取得する 
// 		    iTJSDispatch2 * global = TVPGetScriptDispatch(); 
//  
// 		    // Layer クラスオブジェクトを取得 
// 		    tTJSVariant varScripts; 
// 		    TVPExecuteExpression(TJS_W("Layer"), &varScripts); 
// 		    iTJSDispatch2 *dispatch = varScripts.AsObjectNoAddRef(); 
// 		    if (dispatch) { 
// 			    // プロパティ初期化 
// 			    NI_LayerExBase::init(dispatch); 
//  
// 			    // 専用メソッドの追加 
// 			    addMethod(dispatch, TJS_W("perspectiveCopy"), new tPerspectiveCopy()); 
// 		    } 
//  
// 		    global->Release(); 
// 	    } 
//     } else { 
        iTJSDispatch2 * global = TVPGetScriptDispatch();
        tTJSVariant varScripts;
        global->PropGet(0, TJS_W("Layer"), nullptr, &varScripts, global);
        iTJSDispatch2 *dispatch = varScripts.AsObjectNoAddRef();
        if (dispatch) {
            addMethod(dispatch, TJS_W("perspectiveCopy"), TJSCreateNativeClassMethod(PerspectiveCopy_GL));
        }
    //} 
}

NCB_PRE_REGIST_CALLBACK(InitPlugin_Perspective);
