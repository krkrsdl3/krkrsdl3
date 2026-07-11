#include "tjsCommHead.h"

#include "krmovie.h"
#include "TVPMsg.h"
#include "TVPStorage.h"
#include "CodecVideo.h"
#include "VideoPlayerAudio.h"
#include "PlatformVideo.h"
#include "krnull.h"

#ifdef _KRKRSDL3_USE_FFMPEG
#include "KRMovieOverlay.h"
#include "KRMovieLayer.h"
#endif

// ─── System (non-FFmpeg) video player factory ───────────────
// Always succeeds — returns a working player or a null/stub player
// so the caller never hits a null pointer.

static void TryCreateOverlay(tTJSNI_VideoOverlay* callbackwin,
                              tTJSBinaryStream* stream,
                              const tjs_char* streamname,
                              const tjs_char* /*type*/,
                              uint64_t /*size*/,
                              iTVPVideoOverlay** out)
{
    OverlayVideoPlayer* player = CreateOverlayVideoPlayer();
    if (player && player->OpenStream(stream, ttstr(streamname))) {
        *out = player;
        return;
    }
    if (player)
        delete player;
    *out = new NullOverlayPlayer(callbackwin);
}

static void TryCreateLayer(tTJSNI_VideoOverlay* callbackwin,
                            tTJSBinaryStream* stream,
                            const tjs_char* streamname,
                            const tjs_char* /*type*/,
                            uint64_t /*size*/,
                            iTVPVideoOverlay** out)
{
    LayerVideoPlayer* player = CreateLayerVideoPlayer();
    if (player && player->OpenStream(stream, ttstr(streamname))) {
        *out = player;
        return;
    }
    if (player)
        delete player;
    *out = new NullLayerPlayer(callbackwin);
}

void GetVideoOverlayObject(tTJSNI_VideoOverlay* callbackwin,
                           tTJSBinaryStream* stream,
                           const tjs_char* streamname,
                           const tjs_char* type,
                           uint64_t size,
                           iTVPVideoOverlay** out)
{
#ifdef _KRKRSDL3_USE_FFMPEG
    *out = new KRMovie::MoviePlayerOverlay;
    if (*out)
        static_cast<KRMovie::MoviePlayerOverlay*>(*out)->BuildGraph(callbackwin, stream, streamname,
                                                                    type, size);
#else
    TryCreateOverlay(callbackwin, stream, streamname, type, size, out);
#endif
}

void GetVideoLayerObject(tTJSNI_VideoOverlay* callbackwin,
                         tTJSBinaryStream* stream,
                         const tjs_char* streamname,
                         const tjs_char* type,
                         uint64_t size,
                         iTVPVideoOverlay** out)
{
#ifdef _KRKRSDL3_USE_FFMPEG
    *out = new KRMovie::MoviePlayerLayer;
    if (*out)
        static_cast<KRMovie::MoviePlayerLayer*>(*out)->BuildGraph(callbackwin, stream, streamname,
                                                                  type, size);
#else
    TryCreateLayer(callbackwin, stream, streamname, type, size, out);
#endif
}

void GetMixingVideoOverlayObject(tTJSNI_VideoOverlay* callbackwin,
                                 tTJSBinaryStream* stream,
                                 const tjs_char* streamname,
                                 const tjs_char* type,
                                 uint64_t size,
                                 iTVPVideoOverlay** out)
{
#ifdef _KRKRSDL3_USE_FFMPEG
    *out = new KRMovie::MoviePlayerOverlay;
    if (*out)
        static_cast<KRMovie::MoviePlayerOverlay*>(*out)->BuildGraph(callbackwin, stream, streamname,
                                                                    type, size);
#else
    TryCreateOverlay(callbackwin, stream, streamname, type, size, out);
#endif
}

void GetMFVideoOverlayObject(tTJSNI_VideoOverlay* callbackwin,
                             tTJSBinaryStream* stream,
                             const tjs_char* streamname,
                             const tjs_char* type,
                             uint64_t size,
                             iTVPVideoOverlay** out)
{
#ifdef _KRKRSDL3_USE_FFMPEG
    *out = new KRMovie::MoviePlayerOverlay;
    if (*out)
        static_cast<KRMovie::MoviePlayerOverlay*>(*out)->BuildGraph(callbackwin, stream, streamname,
                                                                    type, size);
#else
    TryCreateOverlay(callbackwin, stream, streamname, type, size, out);
#endif
}