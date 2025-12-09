#include "ncbind/ncbind.hpp"

#include <vector>
#include "TVPFont.h"
#include "TVPStorage.h"

#include "wave.h"
#include "mosaic.h"
#include "turn.h"
#include "rotatetrans.h"
#include "ripple.h"

#define NCB_MODULE_NAME TJS_W("extrans.dll")

void InitPlugin_extrans() {
    RegisterWaveTransHandlerProvider();
    RegisterMosaicTransHandlerProvider();
    RegisterTurnTransHandlerProvider();
    RegisterRotateTransHandlerProvider();
    RegisterRippleTransHandlerProvider();
}

void DonePlugin_extrans() {
    UnregisterWaveTransHandlerProvider();
    UnregisterMosaicTransHandlerProvider();
    UnregisterTurnTransHandlerProvider();
    UnregisterRotateTransHandlerProvider();
    UnregisterRippleTransHandlerProvider();
}

NCB_PRE_REGIST_CALLBACK(InitPlugin_extrans);
NCB_POST_UNREGIST_CALLBACK(DonePlugin_extrans);