//---------------------------------------------------------------------------
// extNagano : 追加トランジションプラグイン (吉里吉里Z / krkrsdl3 向け復元版)
//
// 来源：https://github.com/wamsoft/extNagano
// 原始插件：吉里吉里2 用 extNagano.dll ver 1.2.4.726 (2006)
// 原作者：ヤマモト / Shun / chiyoclone.net / シノハラ
//         tp_stub.h / tp_stub.cpp / common.h は W.Dee
//
// 原始ソースは失われていたため、残存 DLL の Ghidra 解析と吉里吉里Z の
// extrans プラグイン構造を基に 12 種のトランジションを再構築したもの。
// 本文件是 krkrsdl3 内嵌移植版：
//   - 直接包含引擎头文件，不再依赖 tp_stub.h / simplebinder.hpp
//   - TJS_W 宽字符串改为引擎的 TJS_N 窄字符串
//   - 注册方式改为 ncbind（InitPlugin_extNagano / NCB_PRE_REGIST_CALLBACK）
//
// ライセンス：元 DLL は吉里吉里ライセンスと GPL のデュアルライセンス。
// 本移植版は吉里吉里本体のライセンスに準拠します。
//---------------------------------------------------------------------------
#include "tjsCommHead.h"
#include "cpu_types.h"
#include "transhandler.h"
#include "tjsErrorDefs.h"
#include "TVPMsg.h"
#include "TVPTrans.h"
#include "tjsVariant.h"
#include "tjsConfig.h"
#include "ncbind/ncbind.hpp"

#include "3duniversal.h"
#include "blurfade.h"
#include "scanline.h"
#include "zoomfade.h"
#include "rgbfade.h"
#include "spin.h"
#include "flutter.h"
#include "book.h"
#include "imagewipe.h"
#include "honeyturn.h"
#include "morphing.h"
#include "multiripple.h"

#define NCB_MODULE_NAME TJS_N("extNagano.dll")

//---------------------------------------------------------------------------
void InitPlugin_extNagano()
{
	// トランジションハンドラプロバイダの登録
	Register3duniversalTransHandlerProvider();
	RegisterBlurFadeTransHandlerProvider();
	RegisterScanLineTransHandlerProvider();
	RegisterZoomFadeTransHandlerProvider();
	RegisterRGBFadeTransHandlerProvider();
	RegisterSpinFadeTransHandlerProvider();
	RegisterFlutterTransHandlerProvider();
	RegisterBookTransHandlerProvider();
	RegisterImageWipeTransHandlerProvider();
	RegisterHoneyTurnTransHandlerProvider();
	RegisterMorphingTransHandlerProvider();
	RegisterMultiRippleTransHandlerProvider();
}
//---------------------------------------------------------------------------
void DonePlugin_extNagano()
{
	// トランジションハンドラプロバイダの登録削除
	Unregister3duniversalTransHandlerProvider();
	UnregisterBlurFadeTransHandlerProvider();
	UnregisterScanLineTransHandlerProvider();
	UnregisterZoomFadeTransHandlerProvider();
	UnregisterRGBFadeTransHandlerProvider();
	UnregisterSpinFadeTransHandlerProvider();
	UnregisterFlutterTransHandlerProvider();
	UnregisterBookTransHandlerProvider();
	UnregisterImageWipeTransHandlerProvider();
	UnregisterHoneyTurnTransHandlerProvider();
	UnregisterMorphingTransHandlerProvider();
	UnregisterMultiRippleTransHandlerProvider();
}

NCB_PRE_REGIST_CALLBACK(InitPlugin_extNagano);
NCB_POST_UNREGIST_CALLBACK(DonePlugin_extNagano);
//---------------------------------------------------------------------------
