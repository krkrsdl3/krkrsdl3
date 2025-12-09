#pragma once

#include "WaveIntf.h"

class OpusWaveDecoderCreator : public tTVPWaveDecoderCreator {
public:
    tTVPWaveDecoder *Create(const ttstr &storagename, const ttstr &extension);
};