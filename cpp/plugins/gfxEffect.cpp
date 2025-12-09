#include "ncbind/ncbind.hpp"

#define NCB_MODULE_NAME TJS_W("gfxEffect.dll")

static void InitPlugin_GFXEffect()
{
	TVPExecuteScript(
		TJS_W("class gfxFire {")
		TJS_W("  function gfxFire() {")
		TJS_W("    Debug.message('gfxFire construct'); ")
		TJS_W("  }")
		TJS_W("  function finalize() {")
		TJS_W("    Debug.message('gfxFire finalize');")
		TJS_W("  }")
		TJS_W("};")
	);
}

NCB_PRE_REGIST_CALLBACK(InitPlugin_GFXEffect);