#include "tjsCommHead.h"

#include "TVPMovieBackend.h"

extern "C"
{
#include <theora/theoradec.h>
}

#include <cstring>

//---------------------------------------------------------------------------
// libtheora 后端
//
// 这是一个「非 FFmpeg 解码器」的完整示范：只需要实现 iTVPMovieDecoder，
// 把 Theora 的 YCbCr 平面填进 tTVPMovieFrame，其余全部由播放器负责。
// 它可以叠在任何能给出 "theora" 流的解复用器上（当前是 FFmpeg 的 Ogg）。
//---------------------------------------------------------------------------

namespace krkrsdl3
{

namespace
{

tTVPPixelFormat FromTheoraPixelFormat(th_pixel_fmt format)
{
    switch (format)
    {
        case TH_PF_420:
            return tTVPPixelFormat::YUV420P;
        case TH_PF_422:
            return tTVPPixelFormat::YUV422P;
        case TH_PF_444:
            return tTVPPixelFormat::YUV444P;
        default:
            return tTVPPixelFormat::Unknown;
    }
}

class tTVPMovieTheoraDecoder : public iTVPMovieDecoder
{
public:
    ~tTVPMovieTheoraDecoder() override { Close(); }

    const char* Name() const override { return "theora"; }

    bool Open(const tTVPMovieStream& stream) override;
    void Close() override;

    bool Feed(const tTVPMoviePacket& packet) override;
    bool Receive(tTVPMovieFrame& frame) override;
    void Flush() override;
    void Drain() override {}

private:
    bool AllocateDecoder();

    th_info info_{};
    th_comment comment_{};
    th_setup_info* setup_ = nullptr;
    th_dec_ctx* decoder_ = nullptr;

    // 最近一次成功解码的时间戳与画面是否与上一帧相同。
    double pendingPts_ = TVPMovieNoTime;
    double lastPts_ = TVPMovieNoTime;
    bool haveFrame_ = false;

    int picX_ = 0;
    int picY_ = 0;
};

bool tTVPMovieTheoraDecoder::Open(const tTVPMovieStream& stream)
{
    Close();

    th_info_init(&info_);
    th_comment_init(&comment_);

    if (stream.extradata.empty())
        return false;

    // FFmpeg 的 Ogg 解复用器把三个头包按顺序拼进 extradata，
    // th_decode_headerin 会逐个吃掉并把指针往前推。
    ogg_packet packet;
    memset(&packet, 0, sizeof(packet));
    packet.packet = const_cast<unsigned char*>(stream.extradata.data());
    packet.bytes = static_cast<long>(stream.extradata.size());

    int result = 0;
    while ((result = th_decode_headerin(&info_, &comment_, &setup_, &packet)) > 0)
    {
    }

    // 0 表示头包已全部消费完；负值说明数据不完整或有错。
    if (result != 0)
    {
        Close();
        return false;
    }

    picX_ = static_cast<int>(info_.pic_x);
    picY_ = static_cast<int>(info_.pic_y);
    lastPts_ = TVPMovieNoTime;

    return AllocateDecoder();
}

bool tTVPMovieTheoraDecoder::AllocateDecoder()
{
    if (setup_ == nullptr)
        return false;

    decoder_ = th_decode_alloc(&info_, setup_);
    if (decoder_ == nullptr)
        return false;

    haveFrame_ = false;
    return true;
}

void tTVPMovieTheoraDecoder::Close()
{
    if (decoder_)
        th_decode_free(decoder_);
    decoder_ = nullptr;

    if (setup_)
        th_setup_free(setup_);
    setup_ = nullptr;

    th_comment_clear(&comment_);
    th_info_clear(&info_);

    haveFrame_ = false;
}

bool tTVPMovieTheoraDecoder::Feed(const tTVPMoviePacket& packet)
{
    if (decoder_ == nullptr || packet.Empty())
        return true;

    ogg_packet ogg;
    memset(&ogg, 0, sizeof(ogg));
    ogg.packet = const_cast<unsigned char*>(packet.data);
    ogg.bytes = packet.size;
    ogg.granulepos = -1; // 播放器自带时钟，不依赖 Theora 的 granule 换算

    const int result = th_decode_packetin(decoder_, &ogg, nullptr);
    if (result < 0)
        return true; // 坏包：跳过，别让播放停下

    // TH_DUPFRAME(1) 表示画面与上一帧相同，仍然要往上抛一帧。
    pendingPts_ = packet.pts;
    haveFrame_ = true;
    return true;
}

bool tTVPMovieTheoraDecoder::Receive(tTVPMovieFrame& frame)
{
    frame.Clear();

    if (decoder_ == nullptr || !haveFrame_)
        return false;

    th_ycbcr_buffer planes;
    if (th_decode_ycbcr_out(decoder_, planes) != 0)
        return false;

    const tTVPPixelFormat format = FromTheoraPixelFormat(info_.pixel_fmt);
    if (format == tTVPPixelFormat::Unknown)
        return false;

    // 只交出可见区域：Theora 的编码尺寸是 16 的倍数，右边/下边是填充。
    const int visibleWidth = static_cast<int>(info_.pic_width);
    const int visibleHeight = static_cast<int>(info_.pic_height);
    const int chromaShiftX = (format == tTVPPixelFormat::YUV444P) ? 0 : 1;
    const int chromaShiftY = (format == tTVPPixelFormat::YUV420P) ? 1 : 0;

    frame.kind = tTVPMovieFrame::tKind::Video;
    frame.format = format;
    frame.width = visibleWidth;
    frame.height = visibleHeight;
    frame.displayWidth = visibleWidth;
    frame.displayHeight = visibleHeight;

    frame.planes[0] = planes[0].data + picY_ * planes[0].stride + picX_;
    frame.planes[1] = planes[1].data + (picY_ >> chromaShiftY) * planes[1].stride + (picX_ >> chromaShiftX);
    frame.planes[2] = planes[2].data + (picY_ >> chromaShiftY) * planes[2].stride + (picX_ >> chromaShiftX);

    frame.strides[0] = planes[0].stride;
    frame.strides[1] = planes[1].stride;
    frame.strides[2] = planes[2].stride;

    frame.pts = pendingPts_ == TVPMovieNoTime
                    ? (lastPts_ == TVPMovieNoTime ? 0.0 : lastPts_ + TVPMovieMsPerSecond / 25.0)
                    : pendingPts_;
    frame.duration = frame.pts - lastPts_;
    frame.repeat = (lastPts_ != TVPMovieNoTime && frame.pts <= lastPts_);

    lastPts_ = frame.pts;
    pendingPts_ = TVPMovieNoTime;
    haveFrame_ = false;

    return true;
}

void tTVPMovieTheoraDecoder::Flush()
{
    // 定位后需要全新的解码器状态：th_decode_* 没有 flush 接口。
    if (decoder_)
        th_decode_free(decoder_);
    decoder_ = nullptr;

    AllocateDecoder();

    pendingPts_ = TVPMovieNoTime;
    lastPts_ = TVPMovieNoTime;
    haveFrame_ = false;
}

class tTVPMovieTheoraBackend : public iTVPMovieBackend
{
public:
    const char* Name() const override { return "theora"; }

    // 专用解码器排在兜底后端之前：明确认得这条流就优先用它。
    int Priority() const override { return 10; }

    iTVPMovieDecoder* CreateDecoder(const tTVPMovieStream& stream) override
    {
        if (!stream.IsVideo())
            return nullptr;
        if (stream.codec != "theora")
            return nullptr;
        return new tTVPMovieTheoraDecoder();
    }
};

} // namespace

void TVPRegisterTheoraBackend(tTVPMovieBackends& backends)
{
    backends.Add(std::make_unique<tTVPMovieTheoraBackend>());
}

} // namespace krkrsdl3
