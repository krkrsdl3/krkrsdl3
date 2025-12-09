#pragma once
#include "WaveIntf.h"

class VorbisWaveDecoderCreator : public tTVPWaveDecoderCreator
{
public:
    tTVPWaveDecoder * Create(const ttstr & storagename, const ttstr & extension);
};