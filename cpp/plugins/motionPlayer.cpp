#include "ncbind/ncbind.hpp"

#define NCB_MODULE_NAME TJS_W("motionplayer.dll")

static void InitPlugin_MotionPlayer()
{
    ncbAutoRegister::LoadModule(TJS_W("emoteplayer.dll"));
}

NCB_PRE_REGIST_CALLBACK(InitPlugin_MotionPlayer);