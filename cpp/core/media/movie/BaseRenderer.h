#pragma once
#include "KRMovieDef.h"
#include "CodecVideo.h"

NS_KRMOVIE_BEGIN

class CBaseRenderer
{
public:
    virtual int AddVideoPicture(DVDVideoPicture& picture, int index) = 0;
    virtual int WaitForBuffer(volatile std::atomic_bool& bStop, int timeout = 0) = 0;
    virtual void Flush() = 0;
};

NS_KRMOVIE_END
