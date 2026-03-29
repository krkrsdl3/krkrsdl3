#pragma once

#include "krffmpeg.h"
#include "TVPEvent.h"
#include "ComplexRect.h"

#include "SDL2/SDL.h"
#include "../eventCallbackFun.h"

struct SwsContext;
class iTVPSoundBuffer;

NS_KRMOVIE_BEGIN

class VideoPresentOverlay : public TVPMoviePlayer, public tTVPContinuousEventCallbackIntf
{
protected:
    SDL_Sprite* pSprite;

    VideoPresentOverlay();
    ~VideoPresentOverlay();

public:
    virtual void Stop() override;
    virtual void Play() override;
    virtual void OnContinuousCallback(tjs_uint64 tick) override;

protected:
    virtual const tTVPRect& GetBounds() = 0;
};

class MoviePlayerOverlay : public VideoPresentOverlay
{
    tTJSNI_VideoOverlay* m_pCallbackWin = nullptr;

    void OnPlayEvent(KRMovieEvent msg, void* p);

public:
    ~MoviePlayerOverlay();
    virtual void SetWindow(class tTJSNI_Window* window) override;

    void BuildGraph(tTJSNI_VideoOverlay* callbackwin,
                    tTJSBinaryStream* stream,
                    const tjs_char* streamname,
                    const tjs_char* type,
                    uint64_t size);

    virtual const tTVPRect& GetBounds() override;
    virtual void SetVisible(bool b) override;
};

NS_KRMOVIE_END
