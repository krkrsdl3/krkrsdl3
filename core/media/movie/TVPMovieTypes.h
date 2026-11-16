#pragma once
//---------------------------------------------------------------------------
// TVPMovieTypes.h — 影片子系统的基础类型
//
// 命名与内核保持一致：tTVP* 为值类型，iTVP* 为接口，TVP* 为自由函数。
// 整个子系统只使用「毫秒 + 双精度」作为时间基，不再混用 100ns 单位。
//---------------------------------------------------------------------------

#include <cstdint>

namespace krkrsdl3
{

//---------------------------------------------------------------------------
// 时间
//---------------------------------------------------------------------------

// 没有时间戳的哨兵值（毫秒）。0 是合法时间，所以不能用 0 表示「无」。
constexpr double TVPMovieNoTime = -1.0;

// 1 秒对应的毫秒数，换算时用它代替魔法数字。
constexpr double TVPMovieMsPerSecond = 1000.0;

//---------------------------------------------------------------------------
// 流类型
//---------------------------------------------------------------------------
enum class tTVPStreamKind : int
{
    Unknown = -1,
    Video = 0,
    Audio = 1,
    Data = 2,
    Subtitle = 3,
};

// 参与音视频流选择的类型。
inline bool TVPIsSelectableStream(tTVPStreamKind kind)
{
    return kind == tTVPStreamKind::Video || kind == tTVPStreamKind::Audio ||
           kind == tTVPStreamKind::Subtitle;
}

//---------------------------------------------------------------------------
// 像素格式
//
// 解码器只需把自己的输出映射到其中之一即可接入：平面 YUV、半平面 NV、
// 或已经完成色彩转换的打包 RGB。播放器据此选择转换路径，不再假定 YUV420P。
//---------------------------------------------------------------------------
enum class tTVPPixelFormat : int
{
    Unknown = -1,
    YUV420P = 0,
    YUV422P,
    YUV444P,
    NV12,
    NV21,
    RGBA,
    BGRA,
    ARGB,
    RGB24,
    BGR24,
    Gray8,
};

// 打包格式每像素字节数；平面格式返回 0。
inline int TVPPixelBytes(tTVPPixelFormat format)
{
    switch (format)
    {
        case tTVPPixelFormat::RGBA:
        case tTVPPixelFormat::BGRA:
        case tTVPPixelFormat::ARGB:
            return 4;
        case tTVPPixelFormat::RGB24:
        case tTVPPixelFormat::BGR24:
            return 3;
        case tTVPPixelFormat::Gray8:
            return 1;
        default:
            return 0;
    }
}

inline bool TVPIsPackedPixelFormat(tTVPPixelFormat format)
{
    return TVPPixelBytes(format) > 0;
}

inline bool TVPIsYuvPixelFormat(tTVPPixelFormat format)
{
    switch (format)
    {
        case tTVPPixelFormat::YUV420P:
        case tTVPPixelFormat::YUV422P:
        case tTVPPixelFormat::YUV444P:
        case tTVPPixelFormat::NV12:
        case tTVPPixelFormat::NV21:
            return true;
        default:
            return false;
    }
}

//---------------------------------------------------------------------------
// 采样格式
//---------------------------------------------------------------------------
enum class tTVPSampleFormat : int
{
    Unknown = -1,
    U8 = 0,
    S16,
    S32,
    FLT,
    DBL,
    U8P,
    S16P,
    S32P,
    FLTP,
    DBLP,
};

inline bool TVPIsPlanarSampleFormat(tTVPSampleFormat format)
{
    return format == tTVPSampleFormat::U8P || format == tTVPSampleFormat::S16P ||
           format == tTVPSampleFormat::S32P || format == tTVPSampleFormat::FLTP ||
           format == tTVPSampleFormat::DBLP;
}

inline int TVPSampleBytes(tTVPSampleFormat format)
{
    switch (format)
    {
        case tTVPSampleFormat::U8:
        case tTVPSampleFormat::U8P:
            return 1;
        case tTVPSampleFormat::S16:
        case tTVPSampleFormat::S16P:
            return 2;
        case tTVPSampleFormat::S32:
        case tTVPSampleFormat::S32P:
        case tTVPSampleFormat::FLT:
        case tTVPSampleFormat::FLTP:
            return 4;
        case tTVPSampleFormat::DBL:
        case tTVPSampleFormat::DBLP:
            return 8;
        default:
            return 0;
    }
}

//---------------------------------------------------------------------------
// 编码归类
//
// 解码器匹配用的是规范名（"h264"/"theora"/"vp8"…），这张表只用于
// 「这个流大概是什么」这类便宜判断与日志输出。
//---------------------------------------------------------------------------
enum class tTVPCodecFamily : int
{
    Unknown = 0,
    H264,
    HEVC,
    VP8,
    VP9,
    AV1,
    Theora,
    MPEG1,
    MPEG2,
    MPEG4,
    WMV,
    MJPEG,
    AAC,
    MP3,
    Vorbis,
    Opus,
    FLAC,
    PCM,
    AC3,
    DTS,
};

// 从规范名归类，大小写不敏感。
tTVPCodecFamily TVPClassifyCodec(const char* name);

//---------------------------------------------------------------------------
// 线程预算
//
// 影片子系统自身固定只用 1 个解码线程（解复用 + 音视频解码）；这个函数给
// 「解码器内部的并行度」用（FFmpeg 的 thread_count 之类）。约定与内核其它
// 子系统一致：wasm 固定 1 —— 浏览器里的 pthread 池是按 _PTHREAD_POOL_SIZE
// 预分配的，多开只会把池占满、拖慢整个引擎。
//---------------------------------------------------------------------------
int TVPMovieThreadBudget();

} // namespace krkrsdl3
