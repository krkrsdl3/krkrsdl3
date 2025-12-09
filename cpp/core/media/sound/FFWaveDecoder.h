#pragma once
#include "WaveIntf.h"

class FFWaveDecoderCreator : public tTVPWaveDecoderCreator
{
public:
    tTVPWaveDecoder * Create(const ttstr & storagename, const ttstr & extension);
};
