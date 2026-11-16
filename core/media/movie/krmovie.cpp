#include "tjsCommHead.h"

#include "krmovie.h"

#include "Platform.h"
#include "PlatformVideo.h"
#include "TVPEvent.h"
#include "TVPMsg.h"
#include "TVPStorage.h"
#include "krnull.h"

#include "TVPMoviePresenter.h"

//---------------------------------------------------------------------------
// 影片播放对象的工厂
//
// 有内置解码后端时一律交给 tTVPMoviePresenter：overlay / layer / mixer 三种
// 挂载方式共用同一份实现，差别只在构造时选模式。
// 平台自带播放器（MediaFoundation / MediaCodec）只在没有内置后端的构建里兜底。
//---------------------------------------------------------------------------

#if defined(_KRKRSDL3_USE_FFMPEG) || defined(_KRKRSDL3_USE_THEORA)
#define KRKRSDL_HAS_MOVIE_BACKEND 1
#endif

namespace
{

#if defined(KRKRSDL_HAS_MOVIE_BACKEND)

// 打开失败时 presenter 会在第一次 tick 立刻回报 EC_COMPLETE，
// 调用方不需要处理空指针，与旧的 Null 播放器行为一致。
iTVPVideoOverlay* CreatePresenter(krkrsdl3::tTVPMoviePresenter::tMode mode,
                                  iTVPVideoCallback* callbackwin,
                                  TJS::tTJSBinaryStream* stream,
                                  const tjs_char* streamname,
                                  const tjs_char* type,
                                  uint64_t size)
{
    auto* presenter = new krkrsdl3::tTVPMoviePresenter(mode);
    presenter->Open(callbackwin, stream, streamname, type, size);
    return presenter;
}

#else

void VideoPostEvent(void* ctx, unsigned msg, uintptr_t wparam, uintptr_t lparam)
{
    iTVPVideoCallback* callback = static_cast<iTVPVideoCallback*>(ctx);
    NativeEvent event(msg);
    event.WParam = wparam;
    event.LParam = lparam;
    if (wparam == EC_COMPLETE)
        callback->PostEvent(event);
    else
        callback->WndProc(event);
}

void CreatePlatformOverlay(iTVPVideoCallback* callbackwin,
                           TJS::tTJSBinaryStream* stream,
                           const tjs_char* streamname,
                           iTVPVideoOverlay** out)
{
    OverlayVideoPlayer* player = CreateOverlayVideoPlayer(VideoPostEvent, callbackwin);
    if (player != nullptr && player->OpenStream(stream, ttstr(streamname)))
    {
        *out = player;
        return;
    }
    delete player;
    *out = new NullOverlayPlayer(callbackwin);
}

void CreatePlatformLayer(iTVPVideoCallback* callbackwin,
                         TJS::tTJSBinaryStream* stream,
                         const tjs_char* streamname,
                         iTVPVideoOverlay** out)
{
    LayerVideoPlayer* player = CreateLayerVideoPlayer(VideoPostEvent, callbackwin);
    if (player != nullptr && player->OpenStream(stream, ttstr(streamname)))
    {
        *out = player;
        return;
    }
    delete player;
    *out = new NullLayerPlayer(callbackwin);
}

#endif // KRKRSDL_HAS_MOVIE_BACKEND

} // namespace

//---------------------------------------------------------------------------
void GetVideoOverlayObject(iTVPVideoCallback* callbackwin,
                           TJS::tTJSBinaryStream* stream,
                           const tjs_char* streamname,
                           const tjs_char* type,
                           uint64_t size,
                           iTVPVideoOverlay** out)
{
#ifdef KRKRSDL_HAS_MOVIE_BACKEND
    *out = CreatePresenter(krkrsdl3::tTVPMoviePresenter::tMode::Overlay, callbackwin, stream,
                           streamname, type, size);
#else
    CreatePlatformOverlay(callbackwin, stream, streamname, out);
#endif
}

//---------------------------------------------------------------------------
void GetVideoLayerObject(iTVPVideoCallback* callbackwin,
                         TJS::tTJSBinaryStream* stream,
                         const tjs_char* streamname,
                         const tjs_char* type,
                         uint64_t size,
                         iTVPVideoOverlay** out)
{
#ifdef KRKRSDL_HAS_MOVIE_BACKEND
    *out = CreatePresenter(krkrsdl3::tTVPMoviePresenter::tMode::Layer, callbackwin, stream,
                           streamname, type, size);
#else
    CreatePlatformLayer(callbackwin, stream, streamname, out);
#endif
}

//---------------------------------------------------------------------------
void GetMixingVideoOverlayObject(iTVPVideoCallback* callbackwin,
                                 TJS::tTJSBinaryStream* stream,
                                 const tjs_char* streamname,
                                 const tjs_char* type,
                                 uint64_t size,
                                 iTVPVideoOverlay** out)
{
#ifdef KRKRSDL_HAS_MOVIE_BACKEND
    *out = CreatePresenter(krkrsdl3::tTVPMoviePresenter::tMode::Mixer, callbackwin, stream,
                           streamname, type, size);
#else
    CreatePlatformOverlay(callbackwin, stream, streamname, out);
#endif
}

//---------------------------------------------------------------------------
void GetMFVideoOverlayObject(iTVPVideoCallback* callbackwin,
                             TJS::tTJSBinaryStream* stream,
                             const tjs_char* streamname,
                             const tjs_char* type,
                             uint64_t size,
                             iTVPVideoOverlay** out)
{
#ifdef KRKRSDL_HAS_MOVIE_BACKEND
    *out = CreatePresenter(krkrsdl3::tTVPMoviePresenter::tMode::Overlay, callbackwin, stream,
                           streamname, type, size);
#else
    CreatePlatformOverlay(callbackwin, stream, streamname, out);
#endif
}
