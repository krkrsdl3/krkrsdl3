#include "tjsCommHead.h"

extern "C"
{
#include "libswscale/swscale.h"
}
#include "krffmpeg.h"
#include "krmovie.h"
#include "TVPMsg.h"
#include "TVPStorage.h"
#include "KRMovieOverlay.h"
#include "KRMovieLayer.h"
#include "CodecVideo.h"
#include "VideoPlayerAudio.h"

#include "tjsNativeVideoOverlay.h"

TVPMoviePlayer::TVPMoviePlayer()
{
    m_pPlayer = new KRMovie::BasePlayer(this);
}

TVPMoviePlayer::~TVPMoviePlayer()
{
    delete m_pPlayer;
    if (img_convert_ctx)
        sws_freeContext(img_convert_ctx), img_convert_ctx = nullptr;
}

void TVPMoviePlayer::Release()
{
    if (RefCount == 1)
        delete this;
    else
        RefCount--;
}

void TVPMoviePlayer::SetPosition(uint64_t tick)
{
    m_pPlayer->SeekTime(tick);
}

void TVPMoviePlayer::GetPosition(uint64_t* tick)
{
    if (tick)
        *tick = m_pPlayer->GetTime();
}

void TVPMoviePlayer::GetStatus(tTVPVideoStatus* status)
{
    if (m_pPlayer->IsStop())
        *status = vsStopped;
    else if (m_pPlayer->GetSpeed() == 0)
        *status = vsPaused;
    else
        *status = vsPlaying;
}

void TVPMoviePlayer::Rewind()
{
    SetPosition(0);
}

void TVPMoviePlayer::SetFrame(int f)
{
    // TODO seek accurately
    m_pPlayer->SeekTime(f / m_pPlayer->GetFPS() * DVD_PLAYSPEED_NORMAL);
}

void TVPMoviePlayer::GetFrame(int* f)
{
    *f = m_pPlayer->GetCurrentFrame();
}

void TVPMoviePlayer::GetFPS(double* f)
{
    *f = m_pPlayer->GetFPS();
}

void TVPMoviePlayer::GetNumberOfFrame(int* f)
{
    *f = m_pPlayer->GetTotalTime() * m_pPlayer->GetFPS() / DVD_PLAYSPEED_NORMAL;
}

void TVPMoviePlayer::GetTotalTime(int64_t* t)
{
    *t = m_pPlayer->GetTotalTime();
}

void TVPMoviePlayer::GetVideoSize(long* width, long* height)
{
    m_pPlayer->GetVideoSize(width, height);
}

void TVPMoviePlayer::SetPlayRate(double rate)
{
    m_pPlayer->SetSpeed(rate);
}

void TVPMoviePlayer::GetPlayRate(double* rate)
{
    *rate = m_pPlayer->GetSpeed();
}

iTVPSoundBuffer* TVPMoviePlayer::GetSoundDevice()
{
    KRMovie::IDVDStreamPlayerAudio* audioplayer = m_pPlayer->GetAudioPlayer();
    if (!audioplayer)
        return nullptr;

    return audioplayer->GetSoundBuffer();
}

void TVPMoviePlayer::GetAudioBalance(long* balance)
{
    iTVPSoundBuffer* alsound = GetSoundDevice();
    if (alsound)
    {
        *balance = alsound->GetPan() * 100000;
    }
}

void TVPMoviePlayer::SetAudioBalance(long balance)
{
    iTVPSoundBuffer* alsound = GetSoundDevice();
    if (alsound)
    {
        alsound->SetPan(balance / 100000.0f);
    }
}

void TVPMoviePlayer::SetAudioVolume(long volume)
{
    iTVPSoundBuffer* alsound = GetSoundDevice();
    if (alsound)
        alsound->SetVolume(volume / 100000.f);
}

void TVPMoviePlayer::GetAudioVolume(long* volume)
{
    iTVPSoundBuffer* alsound = GetSoundDevice();
    if (alsound)
        *volume = alsound->GetVolume() * 100000;
}

void TVPMoviePlayer::GetNumberOfAudioStream(unsigned long* streamCount)
{
    *streamCount = m_pPlayer->GetAudioStreamCount();
}

void TVPMoviePlayer::SelectAudioStream(unsigned long iStream)
{
    m_pPlayer->GetMessageQueue().Put(new KRMovie::CDVDMsgPlayerSetAudioStream(iStream));
    m_pPlayer->SynchronizeDemuxer();
}

void TVPMoviePlayer::GetEnableAudioStreamNum(long* num)
{
    *num = m_pPlayer->GetAudioStream();
}

void TVPMoviePlayer::DisableAudioStream(void)
{
    // TODO
}

void TVPMoviePlayer::GetNumberOfVideoStream(unsigned long* streamCount)
{
    *streamCount = m_pPlayer->GetVideoStreamCount();
}

void TVPMoviePlayer::SelectVideoStream(unsigned long iStream)
{
    m_pPlayer->GetMessageQueue().Put(new KRMovie::CDVDMsgPlayerSetVideoStream(iStream));
    m_pPlayer->SynchronizeDemuxer();
}

void TVPMoviePlayer::GetEnableVideoStreamNum(long* num)
{
    *num = m_pPlayer->GetVideoStream();
}

int TVPMoviePlayer::WaitForBuffer(volatile std::atomic_bool& bStop, int timeout)
{
    int remainBuf = MAX_BUFFER_COUNT - m_usedPicture;
    if (remainBuf > 0)
        return remainBuf;
    tTJSUniqueLock lk(m_mtxPicture);
    while (!bStop && MAX_BUFFER_COUNT <= m_usedPicture && timeout > 0)
    {
        timeout -= 10;
        m_condPicture.WaitFor(m_mtxPicture, 10);
    }
    return MAX_BUFFER_COUNT - m_usedPicture - 1;
}

void TVPMoviePlayer::Flush()
{
    tTJSUniqueLock lk(m_mtxPicture);
    for (int i = 0; i < MAX_BUFFER_COUNT; ++i)
    {
        m_picture[i].Clear();
    }
    m_curpts = 0.0;
    m_usedPicture = 0;
}

void TVPMoviePlayer::FrameMove()
{
    m_pPlayer->FrameMove();
}

void TVPMoviePlayer::SetLoopSegement(int beginFrame, int endFrame)
{
    m_pPlayer->SetLoopSegement(beginFrame, endFrame);
}

int TVPMoviePlayer::AddVideoPicture(KRMovie::DVDVideoPicture& pic, int index)
{
    if (pic.pts == DVD_NOPTS_VALUE)
        return 0;

    if (m_usedPicture >= MAX_BUFFER_COUNT)
    {
        tTJSUniqueLock lk(m_mtxPicture);
        m_condPicture.Wait(m_mtxPicture);
    }
    if (m_usedPicture >= MAX_BUFFER_COUNT)
        return -1;

    int width = pic.iWidth, height = pic.iHeight;
    uint8_t* data = (uint8_t*)TJSAlignedAlloc(width * height * 4, 4);
    int datasize = width * 4;

    img_convert_ctx =
        sws_getCachedContext(img_convert_ctx, width, height, AV_PIX_FMT_YUV420P, width, height,
                             AV_PIX_FMT_RGBA, SWS_FAST_BILINEAR, NULL, NULL, NULL);
    assert(img_convert_ctx);
    int processed =
        sws_scale(img_convert_ctx, pic.data, pic.iLineSize, 0, pic.iHeight, &data, &datasize);

    {
        tTJSCriticalSectionHolder lk(m_mtxPicture);
        BitmapPicture& picbuf = m_picture[(m_curPicture + m_usedPicture) & (MAX_BUFFER_COUNT - 1)];
        picbuf.Clear();
        picbuf.width = width;
        picbuf.height = height;
        picbuf.rgba = data;
        picbuf.pts = pic.pts / DVD_TIME_BASE;
        ++m_usedPicture;
    }

    return MAX_BUFFER_COUNT - m_usedPicture;
}

void TVPMoviePlayer::BitmapPicture::swap(BitmapPicture& r)
{
    std::swap(data, r.data);
    std::swap(width, r.width);
    std::swap(height, r.height);
}

void TVPMoviePlayer::BitmapPicture::Clear()
{
    for (int i = 0; i < sizeof(data) / sizeof(data[0]); ++i)
        if (data[i])
            TJSAlignedDealloc(data[i]), data[i] = nullptr;
}