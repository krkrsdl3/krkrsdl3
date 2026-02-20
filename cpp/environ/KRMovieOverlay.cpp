#include <thread>
extern "C" {
#include "libswscale/swscale.h"
}

#include "tjsCommHead.h"

#include "KRMovieOverlay.h"
#include "NativeEventQueue.h"
#include "CodecVideo.h"
#include "WaveMixer.h"
#include "Log.h"

#include "tjsNativeVideoOverlay.h"

NS_KRMOVIE_BEGIN
#define DRAW_VIDEO_FRAME 30

VideoPresentOverlay::VideoPresentOverlay()
{
	pSprite = new SDL_Sprite;
	pSprite->isVisible = true;
	pSprite->xPos = 0;
	pSprite->yPos = 0;
}

VideoPresentOverlay::~VideoPresentOverlay()
{
    if (pSprite != NULL)
    {
        if (pSprite->texture != NULL)
        {
            krkrsdl3::SDL_GL_DepartTexture(pSprite);
            krkrsdl3::SDL_GL_DestroyTexture(pSprite);
        }
        delete pSprite;
    }
    if (bufferData != NULL)
    {
        free(bufferData);
        bufferData = NULL;
    }
    TVPRemoveContinuousEventHook(this);
}

void VideoPresentOverlay::Play()
{
    if (pSprite != NULL)
        pSprite->isVisible = true;
	TVPMoviePlayer::Play();
}

void VideoPresentOverlay::Stop()
{
    if (pSprite != NULL)
        pSprite->isVisible = false;
	TVPMoviePlayer::Stop();
}

void VideoPresentOverlay::OnContinuousCallback(tjs_uint64 tick)
{
    if(!m_usedPicture)
        return;
    double m_curpts = m_pPlayer->GetClock() / DVD_TIME_BASE;
    {
        std::lock_guard<std::mutex> lk(m_mtxPicture);
        BitmapPicture &picbuf = m_picture[m_curPicture];
        // check pts
        if(picbuf.pts > m_curpts) { // present in future
            return;
        }
    }
    
	BitmapPicture pic;
    do { // skip frame
        pic.Clear();
        m_picture[m_curPicture].swap(pic);
        m_curPicture = (m_curPicture + 1) & (MAX_BUFFER_COUNT - 1);
        --m_usedPicture;
    } while(m_usedPicture > 0 && m_curpts >= m_picture[m_curPicture].pts);
    assert(m_usedPicture >= 0);
    m_condPicture.notify_all();

    FrameMove();
    if (pic.rgba == NULL)
        return;
    {
        if(pSprite->texture == NULL) {
            pSprite->width = pic.width;
            pSprite->height = pic.height;
            krkrsdl3::SDL_GL_CreateTexture(*pSprite);
            krkrsdl3::SDL_GL_JoinTexture(pSprite);
        }
        int picSize = pic.height * pic.width * 4;
        int pitch = pic.width * 4;
        if(bufferData == NULL) {
            bufferData = (uint8_t *)malloc(picSize);
        }
        memcpy(bufferData, pic.rgba, picSize);
        krkrsdl3::SDL_GL_UpdateTexture(pSprite, bufferData, pic.width, pic.height);
    }
}

MoviePlayerOverlay::~MoviePlayerOverlay()
{
	delete m_pPlayer; m_pPlayer = nullptr;
}

void MoviePlayerOverlay::SetWindow(tTJSNI_Window* window)
{	
	TVPAddContinuousEventHook(this);
}

void MoviePlayerOverlay::BuildGraph(tTJSNI_VideoOverlay* callbackwin, tTJSBinaryStream*stream, const tjs_char * streamname, const tjs_char *type, uint64_t size)
{
	m_pCallbackWin = callbackwin;
	m_pPlayer->SetCallback(std::bind(&MoviePlayerOverlay::OnPlayEvent, this, std::placeholders::_1, std::placeholders::_2));
	m_pPlayer->OpenFromStream(stream, streamname, type, size);
}

const tTVPRect & MoviePlayerOverlay::GetBounds()
{
	return m_pCallbackWin->GetBounds();
}

void KRMovie::MoviePlayerOverlay::SetVisible(bool b)
{
	VideoPresentOverlay::SetVisible(b);
}

void MoviePlayerOverlay::OnPlayEvent(KRMovieEvent msg, void *p)
{
	if (msg == KRMovieEvent::Ended) {
		NativeEvent ev(WM_GRAPHNOTIFY);
		ev.WParam = EC_COMPLETE;
		ev.LParam = 0;
		m_pCallbackWin->PostEvent(ev);
	}
}


NS_KRMOVIE_END
