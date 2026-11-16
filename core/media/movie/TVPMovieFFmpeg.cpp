#include "tjsCommHead.h"

#include "TVPMovieBackend.h"

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libswresample/swresample.h"
#include "libswscale/swscale.h"
}

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Platform.h"
#include "PlatformMutex.h"
#include "TVPMovieIO.h"
#include "TVPMoviePixels.h"

//---------------------------------------------------------------------------
// FFmpeg 后端
//
// 解复用、视频解码、音频解码三个实现都放在这一个 TU 里：它们共享同一套
// 时间戳换算与像素格式映射，拆开反而要额外造一层内部头。
//---------------------------------------------------------------------------

namespace krkrsdl3
{

namespace
{

constexpr int kIOBufferSize = 4096;

//---------------------------------------------------------------------------
// 中立类型 ↔ FFmpeg 的映射
//---------------------------------------------------------------------------

tTVPPixelFormat FromAVPixelFormat(int avFormat)
{
    switch (static_cast<AVPixelFormat>(avFormat))
    {
        case AV_PIX_FMT_YUV420P:
        case AV_PIX_FMT_YUVJ420P:
            return tTVPPixelFormat::YUV420P;
        case AV_PIX_FMT_YUV422P:
        case AV_PIX_FMT_YUVJ422P:
            return tTVPPixelFormat::YUV422P;
        case AV_PIX_FMT_YUV444P:
        case AV_PIX_FMT_YUVJ444P:
            return tTVPPixelFormat::YUV444P;
        case AV_PIX_FMT_NV12:
            return tTVPPixelFormat::NV12;
        case AV_PIX_FMT_NV21:
            return tTVPPixelFormat::NV21;
        case AV_PIX_FMT_RGBA:
            return tTVPPixelFormat::RGBA;
        case AV_PIX_FMT_BGRA:
            return tTVPPixelFormat::BGRA;
        case AV_PIX_FMT_ARGB:
            return tTVPPixelFormat::ARGB;
        case AV_PIX_FMT_RGB24:
            return tTVPPixelFormat::RGB24;
        case AV_PIX_FMT_BGR24:
            return tTVPPixelFormat::BGR24;
        case AV_PIX_FMT_GRAY8:
            return tTVPPixelFormat::Gray8;
        default:
            return tTVPPixelFormat::Unknown;
    }
}

// 超出映射表的格式一律按 YUV420P 处理，与旧实现保持一致的兜底行为。
tTVPPixelFormat FromAVPixelFormatOrDefault(int avFormat)
{
    const tTVPPixelFormat format = FromAVPixelFormat(avFormat);
    return format == tTVPPixelFormat::Unknown ? tTVPPixelFormat::YUV420P : format;
}

int ToAVPixelFormat(tTVPPixelFormat format)
{
    switch (format)
    {
        case tTVPPixelFormat::YUV420P:
            return AV_PIX_FMT_YUV420P;
        case tTVPPixelFormat::YUV422P:
            return AV_PIX_FMT_YUV422P;
        case tTVPPixelFormat::YUV444P:
            return AV_PIX_FMT_YUV444P;
        case tTVPPixelFormat::NV12:
            return AV_PIX_FMT_NV12;
        case tTVPPixelFormat::NV21:
            return AV_PIX_FMT_NV21;
        case tTVPPixelFormat::RGBA:
            return AV_PIX_FMT_RGBA;
        case tTVPPixelFormat::BGRA:
            return AV_PIX_FMT_BGRA;
        case tTVPPixelFormat::ARGB:
            return AV_PIX_FMT_ARGB;
        case tTVPPixelFormat::RGB24:
            return AV_PIX_FMT_RGB24;
        case tTVPPixelFormat::BGR24:
            return AV_PIX_FMT_BGR24;
        case tTVPPixelFormat::Gray8:
            return AV_PIX_FMT_GRAY8;
        default:
            return AV_PIX_FMT_NONE;
    }
}

tTVPStreamKind FromAVMediaType(AVMediaType type)
{
    switch (type)
    {
        case AVMEDIA_TYPE_VIDEO:
            return tTVPStreamKind::Video;
        case AVMEDIA_TYPE_AUDIO:
            return tTVPStreamKind::Audio;
        case AVMEDIA_TYPE_SUBTITLE:
            return tTVPStreamKind::Subtitle;
        case AVMEDIA_TYPE_DATA:
            return tTVPStreamKind::Data;
        default:
            return tTVPStreamKind::Unknown;
    }
}

double ToMilliseconds(int64_t timestamp, AVRational timeBase)
{
    if (timestamp == AV_NOPTS_VALUE)
        return TVPMovieNoTime;
    return double(timestamp) * av_q2d(timeBase) * TVPMovieMsPerSecond;
}

// 容器的显示宽高比判定：matroska 信任容器，其余优先用编码参数。
double SelectAspect(AVStream* stream, bool& forced, bool matroska)
{
    if (av_dict_get(stream->metadata, "stereo_mode", NULL, 0))
    {
        forced = false;
        return av_q2d(stream->codecpar->sample_aspect_ratio);
    }

    if (matroska && stream->sample_aspect_ratio.num != 0)
    {
        forced = true;
        return av_q2d(stream->sample_aspect_ratio);
    }

    forced = false;
    const AVRational container = stream->sample_aspect_ratio;
    if ((container.den == 1 || container.den == 0) && (container.num == 1 || container.num == 0) &&
        stream->codecpar->sample_aspect_ratio.num != 0)
    {
        return av_q2d(stream->codecpar->sample_aspect_ratio);
    }

    forced = true;
    if (container.num != 0)
        return av_q2d(container);

    return 0.0;
}

void BuildStream(AVStream* avStream, bool matroska, tTVPMovieStream& out)
{
    const AVCodecParameters* params = avStream->codecpar;

    out.kind = FromAVMediaType(params->codec_type);
    out.id = avStream->index;
    out.nativeCodec = static_cast<uint32_t>(params->codec_id);
    out.codecTag = params->codec_tag;
    out.disposition = avStream->disposition;
    out.codec = avcodec_get_name(params->codec_id);
    out.timeBaseNumerator = avStream->time_base.num;
    out.timeBaseDenominator = avStream->time_base.den;

    if (params->extradata && params->extradata_size > 0)
    {
        out.extradata.assign(params->extradata,
                             params->extradata + params->extradata_size);
    }

    if (params->codec_type == AVMEDIA_TYPE_VIDEO)
    {
        if (matroska && avStream->avg_frame_rate.den && avStream->avg_frame_rate.num)
        {
            out.fpsNumerator = avStream->avg_frame_rate.num;
            out.fpsDenominator = avStream->avg_frame_rate.den;
        }
        else if (avStream->r_frame_rate.den && avStream->r_frame_rate.num)
        {
            out.fpsNumerator = avStream->r_frame_rate.num;
            out.fpsDenominator = avStream->r_frame_rate.den;
        }

        out.width = params->width;
        out.height = params->height;

        bool forced = false;
        const double ratio = SelectAspect(avStream, forced, matroska);
        if (ratio > 0.0 && params->height > 0)
            out.aspect = float(ratio * params->width / params->height);

        if (AVDictionaryEntry* rotate = av_dict_get(avStream->metadata, "rotate", NULL, 0))
            out.rotation = atoi(rotate->value);
    }
    else if (params->codec_type == AVMEDIA_TYPE_AUDIO)
    {
        out.channels = params->ch_layout.nb_channels;
        out.sampleRate = params->sample_rate;
        out.bitrate = params->bit_rate;
        out.blockAlign = params->block_align;
    }
}

//---------------------------------------------------------------------------
// sws 加速的 RGBA 转换（安装到 TVPMoviePixels）
//---------------------------------------------------------------------------

tTJSCriticalSection gSwsLock;
SwsContext* gSws = nullptr;
int gSwsWidth = 0;
int gSwsHeight = 0;
int gSwsFormat = -1;

bool SwsConvertToRGBA(const tTVPMovieFrame& frame, uint8_t* dst, int dstPitch)
{
    const int sourceFormat = ToAVPixelFormat(frame.format);
    if (sourceFormat == AV_PIX_FMT_NONE || frame.width <= 0 || frame.height <= 0)
        return false;

    tTJSCriticalSectionHolder lock(gSwsLock);

    if (gSws == nullptr || gSwsWidth != frame.width || gSwsHeight != frame.height ||
        gSwsFormat != sourceFormat)
    {
        if (gSws)
            sws_freeContext(gSws);
        gSws = sws_getContext(frame.width, frame.height, static_cast<AVPixelFormat>(sourceFormat),
                              frame.width, frame.height, AV_PIX_FMT_RGBA, SWS_BILINEAR,
                              nullptr, nullptr, nullptr);
        gSwsWidth = frame.width;
        gSwsHeight = frame.height;
        gSwsFormat = sourceFormat;
        if (gSws == nullptr)
            return false;
    }

    uint8_t* destination[4] = {dst, nullptr, nullptr, nullptr};
    int destinationStride[4] = {dstPitch, 0, 0, 0};

    const int scaled = sws_scale(gSws, frame.planes, frame.strides, 0, frame.height, destination,
                                 destinationStride);
    return scaled > 0;
}

//---------------------------------------------------------------------------
// §1 解复用器
//---------------------------------------------------------------------------

class tTVPMovieFFmpegDemuxer : public iTVPMovieDemuxer
{
public:
    tTVPMovieFFmpegDemuxer() {}
    ~tTVPMovieFFmpegDemuxer() override { Dispose(); }

    const char* Name() const override { return "ffmpeg"; }

    bool Open(tTVPMovieIO* io) override;
    const std::vector<tTVPMovieStream>& Streams() const override { return streams_; }
    bool Read(tTVPMoviePacket& packet) override;
    bool Seek(double positionMs) override;
    double Duration() const override { return duration_; }
    void Abort() override { aborted_ = true; }
    bool Aborted() const override { return aborted_; }
    void SetRate(double rate) override { (void)rate; }

private:
    void Dispose();

    static int ReadCallback(void* opaque, uint8_t* buffer, int size);
    static int64_t SeekCallback(void* opaque, int64_t offset, int whence);
    static int InterruptCallback(void* opaque);

    tTVPMovieIO* io_ = nullptr;
    AVFormatContext* format_ = nullptr;
    AVIOContext* avio_ = nullptr;
    AVPacket* packet_ = nullptr;

    std::vector<tTVPMovieStream> streams_;
    double duration_ = 0.0;
    double startTime_ = 0.0; // 容器起始偏移，毫秒
    bool matroska_ = false;
    volatile bool aborted_ = false;
};

int tTVPMovieFFmpegDemuxer::ReadCallback(void* opaque, uint8_t* buffer, int size)
{
    tTVPMovieFFmpegDemuxer* self = static_cast<tTVPMovieFFmpegDemuxer*>(opaque);
    if (self == nullptr || self->io_ == nullptr)
        return AVERROR_EXIT;

    const int read = self->io_->Read(buffer, size);
    if (read < 0)
        return AVERROR_EXIT;
    if (read == 0)
        return AVERROR_EOF;
    return read;
}

int64_t tTVPMovieFFmpegDemuxer::SeekCallback(void* opaque, int64_t offset, int whence)
{
    tTVPMovieFFmpegDemuxer* self = static_cast<tTVPMovieFFmpegDemuxer*>(opaque);
    if (self == nullptr || self->io_ == nullptr)
        return AVERROR_EXIT;

    if (whence == AVSEEK_SIZE)
        return self->io_->Length();

    return self->io_->Seek(offset, whence & ~AVSEEK_FORCE);
}

int tTVPMovieFFmpegDemuxer::InterruptCallback(void* opaque)
{
    tTVPMovieFFmpegDemuxer* self = static_cast<tTVPMovieFFmpegDemuxer*>(opaque);
    return (self && self->aborted_) ? 1 : 0;
}

bool tTVPMovieFFmpegDemuxer::Open(tTVPMovieIO* io)
{
    if (io == nullptr)
        return false;

    io_ = io;

    format_ = avformat_alloc_context();
    if (format_ == nullptr)
        return false;

    format_->interrupt_callback.callback = &tTVPMovieFFmpegDemuxer::InterruptCallback;
    format_->interrupt_callback.opaque = this;

    uint8_t* buffer = static_cast<uint8_t*>(av_malloc(kIOBufferSize));
    if (buffer == nullptr)
    {
        Dispose();
        return false;
    }

    avio_ = avio_alloc_context(buffer, kIOBufferSize, 0, this, &tTVPMovieFFmpegDemuxer::ReadCallback,
                               nullptr, &tTVPMovieFFmpegDemuxer::SeekCallback);
    if (avio_ == nullptr)
    {
        av_free(buffer);
        Dispose();
        return false;
    }

    const std::string& name = io_->Name();
    const AVInputFormat* inputFormat = nullptr;
    av_probe_input_buffer(avio_, &inputFormat, name.c_str(), nullptr, 0, 0);
    if (inputFormat == nullptr)
    {
        Dispose();
        return false;
    }

    format_->pb = avio_;

    if (avformat_open_input(&format_, name.c_str(), inputFormat, nullptr) < 0)
    {
        Dispose();
        return false;
    }

    if (inputFormat->name && std::strcmp(inputFormat->name, "mpegts") == 0)
        av_opt_set_int(format_, "probesize", 10000000, 0);

    matroska_ = inputFormat->name && std::strncmp(inputFormat->name, "matroska", 8) == 0;

    if (avformat_find_stream_info(format_, nullptr) < 0)
    {
        Dispose();
        return false;
    }

    // 允许非阻塞读取，避免无网络流时卡死在 av_read_frame 里。
    format_->flags |= AVFMT_FLAG_NONBLOCK;

    streams_.clear();
    streams_.reserve(format_->nb_streams);
    for (unsigned int i = 0; i < format_->nb_streams; ++i)
    {
        tTVPMovieStream stream;
        BuildStream(format_->streams[i], matroska_, stream);

        // 有些容器（ASF/WMV 这类）不写帧率，avformat 的流信息里也是空的。
        // 让 libavformat 按前几帧的 pts 估一个，避免上层拿不到帧率。
        if (stream.IsVideo() && stream.Fps() <= 0.0)
        {
            const AVRational guessed = av_guess_frame_rate(format_, format_->streams[i], nullptr);
            if (guessed.num > 0 && guessed.den > 0)
            {
                stream.fpsNumerator = guessed.num;
                stream.fpsDenominator = guessed.den;
            }
        }

        streams_.push_back(std::move(stream));
    }

    if (format_->duration != AV_NOPTS_VALUE && format_->duration > 0)
        duration_ = double(format_->duration) * TVPMovieMsPerSecond / AV_TIME_BASE;

    if (duration_ <= 0.0)
    {
        // 有些容器（如 Ogg）不给整体时长，退回到最长的一条流。
        for (unsigned int i = 0; i < format_->nb_streams; ++i)
        {
            AVStream* stream = format_->streams[i];
            if (stream->duration == AV_NOPTS_VALUE || stream->duration <= 0)
                continue;
            const double candidate =
                double(stream->duration) * av_q2d(stream->time_base) * TVPMovieMsPerSecond;
            duration_ = std::max(duration_, candidate);
        }
    }

    if (format_->start_time != AV_NOPTS_VALUE && format_->start_time > 0)
        startTime_ = double(format_->start_time) * TVPMovieMsPerSecond / AV_TIME_BASE;

    packet_ = av_packet_alloc();
    if (packet_ == nullptr)
    {
        Dispose();
        return false;
    }

    return !streams_.empty();
}

void tTVPMovieFFmpegDemuxer::Dispose()
{
    if (packet_)
    {
        av_packet_free(&packet_);
        packet_ = nullptr;
    }

    if (format_)
    {
        // avformat_open_input 见到预置的 pb 会自己打上 AVFMT_FLAG_CUSTOM_IO，
        // 因此 close 不会释放 avio_，由下面统一收尾。
        avformat_close_input(&format_);
        format_ = nullptr;
    }

    if (avio_)
    {
        av_freep(&avio_->buffer);
        avio_context_free(&avio_);
        avio_ = nullptr;
    }

    io_ = nullptr;
}

bool tTVPMovieFFmpegDemuxer::Read(tTVPMoviePacket& packet)
{
    packet = tTVPMoviePacket();

    if (format_ == nullptr || packet_ == nullptr || aborted_)
        return false;

    av_packet_unref(packet_);

    const int result = av_read_frame(format_, packet_);
    if (result == AVERROR(EAGAIN))
    {
        // 非阻塞读暂时没有数据：不是结尾，交回一个空包让调用方继续转。
        return true;
    }
    if (result < 0)
        return false;

    if (packet_->stream_index < 0 ||
        static_cast<size_t>(packet_->stream_index) >= format_->nb_streams)
    {
        av_packet_unref(packet_);
        return false;
    }

    AVStream* stream = format_->streams[packet_->stream_index];

    packet.stream = packet_->stream_index;
    packet.data = packet_->data;
    packet.size = packet_->size;
    packet.keyframe = (packet_->flags & AV_PKT_FLAG_KEY) != 0;
    packet.pts = ToMilliseconds(packet_->pts, stream->time_base);
    packet.dts = ToMilliseconds(packet_->dts, stream->time_base);

    if (packet.pts != TVPMovieNoTime)
        packet.pts = std::max(0.0, packet.pts - startTime_);
    if (packet.dts != TVPMovieNoTime)
        packet.dts = std::max(0.0, packet.dts - startTime_);

    if (packet_->duration > 0)
        packet.duration = ToMilliseconds(packet_->duration, stream->time_base);
    else if (stream->avg_frame_rate.den > 0 && stream->avg_frame_rate.num > 0)
        packet.duration = TVPMovieMsPerSecond * stream->avg_frame_rate.den / stream->avg_frame_rate.num;

    return true;
}

bool tTVPMovieFFmpegDemuxer::Seek(double positionMs)
{
    if (format_ == nullptr)
        return false;

    const int64_t timestamp =
        av_rescale_q(static_cast<int64_t>(positionMs + startTime_), AVRational{1, 1000},
                     AV_TIME_BASE_Q);

    if (av_seek_frame(format_, -1, timestamp, AVSEEK_FLAG_BACKWARD) < 0)
        return false;

    if (packet_)
        av_packet_unref(packet_);

    return true;
}

//---------------------------------------------------------------------------
// §2 视频解码器
//---------------------------------------------------------------------------

class tTVPMovieFFmpegVideoDecoder : public iTVPMovieDecoder
{
public:
    ~tTVPMovieFFmpegVideoDecoder() override { Close(); }

    const char* Name() const override { return "ffmpeg"; }

    bool Open(const tTVPMovieStream& stream) override;
    void Close() override;

    bool Feed(const tTVPMoviePacket& packet) override;
    bool Receive(tTVPMovieFrame& frame) override;
    void Flush() override;
    void Drain() override;
    void SetDropState(bool drop) override;

private:
    AVCodecContext* context_ = nullptr;
    AVFrame* picture_ = nullptr;
    AVRational timeBase_{1, 1000}; // 缺省按毫秒解释，与解复用器给的时间戳对齐
    double pendingPts_ = TVPMovieNoTime;
    double pendingDuration_ = 0.0;
    double lastPts_ = TVPMovieNoTime;
};

bool tTVPMovieFFmpegVideoDecoder::Open(const tTVPMovieStream& stream)
{
    Close();

    const AVCodec* codec = avcodec_find_decoder(static_cast<AVCodecID>(stream.nativeCodec));
    if (codec == nullptr)
        return false;

    context_ = avcodec_alloc_context3(codec);
    if (context_ == nullptr)
        return false;

    context_->codec_type = AVMEDIA_TYPE_VIDEO;
    context_->coded_width = stream.width;
    context_->coded_height = stream.height;
    context_->codec_tag = stream.codecTag;
    context_->workaround_bugs = FF_BUG_AUTODETECT;
    // 并行度由模块统一决定：wasm 上必须是 1。
    context_->thread_count = TVPMovieThreadBudget();

    if (!stream.extradata.empty())
    {
        context_->extradata_size = static_cast<int>(stream.extradata.size());
        context_->extradata = static_cast<uint8_t*>(
            av_mallocz(stream.extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE));
        if (context_->extradata == nullptr)
        {
            Close();
            return false;
        }
        memcpy(context_->extradata, stream.extradata.data(), stream.extradata.size());
    }

    if (avcodec_open2(context_, codec, nullptr) < 0)
    {
        Close();
        return false;
    }

    picture_ = av_frame_alloc();
    if (picture_ == nullptr)
    {
        Close();
        return false;
    }

    if (stream.timeBaseDenominator > 0)
        timeBase_ = AVRational{stream.timeBaseNumerator, stream.timeBaseDenominator};

    return true;
}

void tTVPMovieFFmpegVideoDecoder::Close()
{
    if (picture_)
        av_frame_free(&picture_);
    if (context_)
        avcodec_free_context(&context_);
}

bool tTVPMovieFFmpegVideoDecoder::Feed(const tTVPMoviePacket& packet)
{
    if (context_ == nullptr || packet.Empty())
        return true;

    AVPacket avPacket;
    memset(&avPacket, 0, sizeof(avPacket));
    avPacket.data = const_cast<uint8_t*>(packet.data);
    avPacket.size = packet.size;
    avPacket.pts = packet.pts == TVPMovieNoTime
                       ? AV_NOPTS_VALUE
                       : static_cast<int64_t>(packet.pts / TVPMovieMsPerSecond / av_q2d(timeBase_));
    avPacket.dts = packet.dts == TVPMovieNoTime ? AV_NOPTS_VALUE : avPacket.pts;

    const int result = avcodec_send_packet(context_, &avPacket);
    if (result == AVERROR(EAGAIN))
        return false;
    if (result < 0)
        return true; // 坏包：丢弃，让播放继续

    pendingPts_ = packet.pts;
    pendingDuration_ = packet.duration;
    return true;
}

bool tTVPMovieFFmpegVideoDecoder::Receive(tTVPMovieFrame& frame)
{
    frame.Clear();

    if (context_ == nullptr || picture_ == nullptr)
        return false;

    av_frame_unref(picture_);

    const int result = avcodec_receive_frame(context_, picture_);
    if (result < 0)
        return false;

    frame.kind = tTVPMovieFrame::tKind::Video;
    frame.format = FromAVPixelFormatOrDefault(picture_->format);
    for (int i = 0; i < 4; ++i)
    {
        frame.planes[i] = picture_->data[i];
        frame.strides[i] = picture_->linesize[i];
    }
    frame.width = picture_->width;
    frame.height = picture_->height;

    // 显示尺寸：按像素宽高比补齐，与旧实现一致。
    AVRational pixelAspect = picture_->sample_aspect_ratio;
    double aspect = (pixelAspect.num != 0 && frame.height > 0)
                        ? av_q2d(pixelAspect) * frame.width / frame.height
                        : double(frame.width) / double(frame.height);

    frame.displayHeight = frame.height;
    frame.displayWidth = int(frame.height * aspect) & ~3;
    if (frame.displayWidth > frame.width)
    {
        frame.displayWidth = frame.width;
        frame.displayHeight = int(frame.width / aspect) & ~3;
    }

    double pts = ToMilliseconds(picture_->best_effort_timestamp, timeBase_);
    if (pts == TVPMovieNoTime)
        pts = pendingPts_;
    if (pts == TVPMovieNoTime)
        pts = lastPts_ == TVPMovieNoTime ? 0.0 : lastPts_;

    frame.pts = pts;
    frame.duration = pendingDuration_;
    frame.repeat = (lastPts_ != TVPMovieNoTime && std::fabs(pts - lastPts_) < 0.5);

    lastPts_ = pts;
    pendingPts_ = TVPMovieNoTime;
    pendingDuration_ = 0.0;
    return true;
}

void tTVPMovieFFmpegVideoDecoder::Flush()
{
    if (context_)
        avcodec_flush_buffers(context_);
    lastPts_ = TVPMovieNoTime;
    pendingPts_ = TVPMovieNoTime;
}

void tTVPMovieFFmpegVideoDecoder::Drain()
{
    if (context_)
        avcodec_send_packet(context_, nullptr);
}

void tTVPMovieFFmpegVideoDecoder::SetDropState(bool drop)
{
    if (context_ == nullptr)
        return;

    context_->skip_frame = drop ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;
}

//---------------------------------------------------------------------------
// §3 音频解码器
//
// 输出契约：S16 交错、采样率与声道数等于流的声明值。重采样在这里完成，
// 播放器只负责把字节塞给内核声卡。
//---------------------------------------------------------------------------

class tTVPMovieFFmpegAudioDecoder : public iTVPMovieDecoder
{
public:
    ~tTVPMovieFFmpegAudioDecoder() override { Close(); }

    const char* Name() const override { return "ffmpeg"; }

    bool Open(const tTVPMovieStream& stream) override;
    void Close() override;

    bool Feed(const tTVPMoviePacket& packet) override;
    bool Receive(tTVPMovieFrame& frame) override;
    void Flush() override;
    void Drain() override;

private:
    bool EnsureResampler();

    AVCodecContext* context_ = nullptr;
    AVFrame* picture_ = nullptr;
    SwrContext* resampler_ = nullptr;

    AVRational timeBase_{0, 1};
    int outputRate_ = 0;
    int outputChannels_ = 0;
    double pendingPts_ = TVPMovieNoTime;
    std::vector<uint8_t> output_;
};

bool tTVPMovieFFmpegAudioDecoder::Open(const tTVPMovieStream& stream)
{
    Close();

    const AVCodec* codec = avcodec_find_decoder(static_cast<AVCodecID>(stream.nativeCodec));
    if (codec == nullptr)
        return false;

    context_ = avcodec_alloc_context3(codec);
    if (context_ == nullptr)
        return false;

    context_->codec_type = AVMEDIA_TYPE_AUDIO;
    context_->codec_tag = stream.codecTag;
    // WMA 系（wmapro/wmav2/wmav1）初始化时要看 block_align 与声道布局，
    // 缺了会直接 open 失败，表现就是「画面正常但整片没声音」。
    context_->sample_rate = stream.sampleRate;
    context_->block_align = stream.blockAlign;
    context_->bit_rate = stream.bitrate;
    if (stream.channels > 0)
        av_channel_layout_default(&context_->ch_layout, stream.channels);

    if (!stream.extradata.empty())
    {
        context_->extradata_size = static_cast<int>(stream.extradata.size());
        context_->extradata = static_cast<uint8_t*>(
            av_mallocz(stream.extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE));
        if (context_->extradata == nullptr)
        {
            Close();
            return false;
        }
        memcpy(context_->extradata, stream.extradata.data(), stream.extradata.size());
    }

    if (avcodec_open2(context_, codec, nullptr) < 0)
    {
        Close();
        return false;
    }

    picture_ = av_frame_alloc();
    if (picture_ == nullptr)
    {
        Close();
        return false;
    }

    if (stream.timeBaseDenominator > 0)
        timeBase_ = AVRational{stream.timeBaseNumerator, stream.timeBaseDenominator};

    outputRate_ = stream.sampleRate > 0 ? stream.sampleRate : 44100;
    outputChannels_ = stream.channels > 0 ? stream.channels : 2;

    return true;
}

void tTVPMovieFFmpegAudioDecoder::Close()
{
    if (resampler_)
        swr_free(&resampler_);
    if (picture_)
        av_frame_free(&picture_);
    if (context_)
        avcodec_free_context(&context_);
    output_.clear();
}

bool tTVPMovieFFmpegAudioDecoder::EnsureResampler()
{
    if (resampler_)
        return true;
    if (context_ == nullptr || picture_ == nullptr)
        return false;

    AVChannelLayout inputLayout;
    if (picture_->ch_layout.nb_channels > 0)
        av_channel_layout_copy(&inputLayout, &picture_->ch_layout);
    else
        av_channel_layout_default(&inputLayout, outputChannels_);

    AVChannelLayout outputLayout;
    av_channel_layout_default(&outputLayout, outputChannels_);

    const int inputRate = picture_->sample_rate > 0 ? picture_->sample_rate : outputRate_;

    const int result =
        swr_alloc_set_opts2(&resampler_, &outputLayout, AV_SAMPLE_FMT_S16, outputRate_,
                            &inputLayout, static_cast<AVSampleFormat>(picture_->format),
                            inputRate, 0, nullptr);

    av_channel_layout_uninit(&inputLayout);
    av_channel_layout_uninit(&outputLayout);

    if (result < 0 || resampler_ == nullptr)
        return false;

    return swr_init(resampler_) >= 0;
}

bool tTVPMovieFFmpegAudioDecoder::Feed(const tTVPMoviePacket& packet)
{
    if (context_ == nullptr || packet.Empty())
        return true;

    AVPacket avPacket;
    memset(&avPacket, 0, sizeof(avPacket));
    avPacket.data = const_cast<uint8_t*>(packet.data);
    avPacket.size = packet.size;
    avPacket.pts = packet.pts == TVPMovieNoTime
                       ? AV_NOPTS_VALUE
                       : static_cast<int64_t>(packet.pts / TVPMovieMsPerSecond / av_q2d(timeBase_));

    const int result = avcodec_send_packet(context_, &avPacket);
    if (result == AVERROR(EAGAIN))
        return false;
    if (result < 0)
        return true;

    pendingPts_ = packet.pts;
    return true;
}

bool tTVPMovieFFmpegAudioDecoder::Receive(tTVPMovieFrame& frame)
{
    frame.Clear();

    if (context_ == nullptr || picture_ == nullptr)
        return false;

    av_frame_unref(picture_);

    if (avcodec_receive_frame(context_, picture_) < 0)
        return false;

    if (!EnsureResampler())
        return false;

    const int capacity = swr_get_out_samples(resampler_, picture_->nb_samples);
    if (capacity <= 0)
        return false;

    output_.resize(size_t(capacity) * size_t(outputChannels_) * sizeof(int16_t));

    uint8_t* destination = output_.data();
    const int converted = swr_convert(resampler_, &destination, capacity,
                                      const_cast<const uint8_t**>(picture_->extended_data),
                                      picture_->nb_samples);
    if (converted <= 0)
        return false;

    frame.kind = tTVPMovieFrame::tKind::Audio;
    frame.sampleFormat = tTVPSampleFormat::S16;
    frame.samples[0] = output_.data();
    frame.channels = outputChannels_;
    frame.sampleRate = outputRate_;
    frame.sampleCount = converted;
    frame.pts = pendingPts_;
    frame.duration = double(converted) * TVPMovieMsPerSecond / double(outputRate_);
    pendingPts_ = TVPMovieNoTime;

    return true;
}

void tTVPMovieFFmpegAudioDecoder::Flush()
{
    if (context_)
        avcodec_flush_buffers(context_);
    if (resampler_)
        swr_init(resampler_); // 清空重采样器内部残留
    pendingPts_ = TVPMovieNoTime;
}

void tTVPMovieFFmpegAudioDecoder::Drain()
{
    if (context_)
        avcodec_send_packet(context_, nullptr);
}

//---------------------------------------------------------------------------
// §4 后端描述
//---------------------------------------------------------------------------

class tTVPMovieFFmpegBackend : public iTVPMovieBackend
{
public:
    const char* Name() const override { return "ffmpeg"; }

    // 兜底后端：任何解复用器解出来的流都先交给它试。
    int Priority() const override { return 0; }

    iTVPMovieDemuxer* CreateDemuxer() override { return new tTVPMovieFFmpegDemuxer(); }

    iTVPMovieDecoder* CreateDecoder(const tTVPMovieStream& stream) override
    {
        if (stream.nativeCodec == 0)
            return nullptr;

        if (stream.IsVideo())
            return new tTVPMovieFFmpegVideoDecoder();
        if (stream.IsAudio())
            return new tTVPMovieFFmpegAudioDecoder();
        return nullptr;
    }

    void InstallAccelerators() override { TVPSetFrameConverter(&SwsConvertToRGBA); }
};

} // namespace

void TVPRegisterFFmpegBackend(tTVPMovieBackends& backends)
{
    backends.Add(std::make_unique<tTVPMovieFFmpegBackend>());
}

} // namespace krkrsdl3
