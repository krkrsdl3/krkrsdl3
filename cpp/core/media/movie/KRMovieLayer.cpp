#include "tjsCommHead.h"
#include "KRMovieLayer.h"
#include "CodecVideo.h"
#include "LayerBitmap.h"
#include "TVPApplication.h"
extern "C"
{
#include "libswscale/swscale.h"
}

#include "tjsNativeVideoOverlay.h"

NS_KRMOVIE_BEGIN

tTVPBaseTexture* VideoPresentLayer::GetFrontBuffer()
{
    BitmapPicture pic;
    if (!m_usedPicture)
    {
        return nullptr;
    }
    {
        std::lock_guard<std::mutex> lk(m_mtxPicture);
        BitmapPicture& picbuf = m_picture[m_curPicture];
        picbuf.swap(pic);
        m_curPicture = (m_curPicture + 1) & (MAX_BUFFER_COUNT - 1);
        --m_usedPicture;
        assert(m_usedPicture >= 0);
        m_condPicture.notify_all();
    }
    FrameMove();
    int n = m_nCurBmpBuff;
    m_nCurBmpBuff = 1 - m_nCurBmpBuff;
    m_BmpBits[n]->Update(pic.rgba, pic.width * 4, 0, 0, pic.width, pic.height);
    return m_BmpBits[n];
}

void VideoPresentLayer::SetVideoBuffer(tTVPBaseTexture* buff1, tTVPBaseTexture* buff2, long size)
{
    m_BmpBits[0] = buff1;
    m_BmpBits[1] = buff2;
    m_nCurBmpBuff = 0;
}

void VideoPresentLayer::OnContinuousCallback(tjs_uint64 tick)
{
    if (!m_usedPicture)
        return;
    double m_curpts = m_pPlayer->GetClock() / DVD_TIME_BASE;
    {
        std::lock_guard<std::mutex> lk(m_mtxPicture);
        BitmapPicture& picbuf = m_picture[m_curPicture];
        // check pts
        if (picbuf.pts > m_curpts)
        { // present in future
            return;
        }
    }
    OnPlayEvent(KRMovieEvent::Update, nullptr);
}

MoviePlayerLayer::~MoviePlayerLayer()
{
    TVPRemoveContinuousEventHook(this);
}

void MoviePlayerLayer::BuildGraph(tTJSNI_VideoOverlay* callbackwin,
                                  tTJSBinaryStream* stream,
                                  const tjs_char* streamname,
                                  const tjs_char* type,
                                  uint64_t size)
{
    m_pCallbackWin = callbackwin;
    m_pPlayer->SetCallback(std::bind(&MoviePlayerLayer::OnPlayEvent, this, std::placeholders::_1,
                                     std::placeholders::_2));
    m_pPlayer->OpenFromStream(stream, streamname, type, size);
}

void MoviePlayerLayer::OnPlayEvent(KRMovieEvent msg, void* p)
{
    if (msg == KRMovieEvent::Update)
    {
        NativeEvent ev(WM_GRAPHNOTIFY);
        ev.WParam = EC_UPDATE;
        int frame;
        GetFrame(&frame);
        ev.LParam = frame;
        m_pCallbackWin->WndProc(ev); // in the same thread
    }
    else if (msg == KRMovieEvent::Ended)
    {
        NativeEvent ev(WM_GRAPHNOTIFY);
        ev.WParam = EC_COMPLETE;
        ev.LParam = 0;
        m_pCallbackWin->PostEvent(ev);
    }
}

void MoviePlayerLayer::Play()
{
    inherit::Play();
    TVPAddContinuousEventHook(this);
}

NS_KRMOVIE_END
