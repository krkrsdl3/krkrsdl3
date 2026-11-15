#include "tjsCommHead.h"
#include "ncbind/ncbind.hpp"

#define NCB_MODULE_NAME TJS_N("PackinOne.dll")

/*
 * 原版 PackinOne.dll 是若干插件的合集，导出符号包括：
 *   Layer.fillAlpha / copyBottomBlueToTopAlpha / copyRightBlueToLeftAlpha /
 *   clipAlphaRect / copyAlphaToProvince / fillByProvince / shrinkCopy /
 *   shrinkCopyFast、layerExImage、layerExRaster、FontEx、ScriptsAdd、
 *   StoragesFstat、ArrayAdd / DictAdd。
 * 本工程把这些实现拆成了独立模块，这里按原 DLL 的组合把它们一起拉起来。
 *
 * 其中 layerExBTOA 是 GFX_Movie.tjs 的 MovieControlLayer 每帧要用的
 * （onFrameUpdate 里 targetLayer.fillAlpha()），不加载它原版视频链路会中断。
 */
static void InitPlugin_PackinOne()
{
    try
    {
        ncbAutoRegister::LoadModule(TJS_N("layerExBTOA.dll"));
        ncbAutoRegister::LoadModule(TJS_N("layerExImage.dll"));
        ncbAutoRegister::LoadModule(TJS_N("layerExRaster.dll"));
        ncbAutoRegister::LoadModule(TJS_N("shrinkCopy.dll"));
        ncbAutoRegister::LoadModule(TJS_N("addFont.dll"));
        ncbAutoRegister::LoadModule(TJS_N("scriptsEx.dll"));
        ncbAutoRegister::LoadModule(TJS_N("fstat.dll"));
        ncbAutoRegister::LoadModule(TJS_N("saveStruct.dll"));
    }
    catch (...)
    {
    }
}

NCB_PRE_REGIST_CALLBACK(InitPlugin_PackinOne);
