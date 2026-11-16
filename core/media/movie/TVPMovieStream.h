#pragma once
//---------------------------------------------------------------------------
// TVPMovieStream.h — 流描述、压缩包、解码帧
//
// 这三个结构是解复用器与解码器之间唯一的契约，不含任何后端类型。
//---------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <vector>

#include "TVPMovieTypes.h"

namespace krkrsdl3
{

//---------------------------------------------------------------------------
// tTVPMovieStream — 一条流的完整描述
//---------------------------------------------------------------------------
struct tTVPMovieStream
{
    tTVPStreamKind kind = tTVPStreamKind::Unknown;

    // 解复用器内部的流编号，出现在每个 tTVPMoviePacket::stream 里。
    int id = -1;

    // 规范编解码器名，小写，例如 "h264" / "theora" / "vp8" / "aac"。
    // 解码后端用它匹配自己能不能解。
    std::string codec;

    // 解复用器后端的原生编码 id 与 fourcc。只允许同一个后端的解码器读取。
    uint32_t nativeCodec = 0;
    uint32_t codecTag = 0;

    // 处置位，容器声明「默认流 / 强制字幕」之类。
    int disposition = 0;

    // 容器时间基：把容器原始时间戳换算成秒的比率（分母为 0 表示未知）。
    // 解码器用它把帧自带的 pts 转成毫秒。
    int timeBaseNumerator = 0;
    int timeBaseDenominator = 0;

    enum
    {
        DispositionDefault = 0x0001,
        DispositionForced = 0x0002,
    };

    // 视频
    int width = 0;
    int height = 0;
    int fpsNumerator = 0;
    int fpsDenominator = 0;
    float aspect = 0.0f; // 容器声明的显示宽高比，0 表示未知
    int rotation = 0;    // 顺时针角度

    // 音频
    int channels = 0;
    int sampleRate = 0;
    int bitrate = 0;
    // 编码帧的最小单位（WMA 之类按固定块打包的编码器初始化时要看它）。
    int blockAlign = 0;

    // 解码器初始化数据（如 Theora 的三个头包、H.264 的 SPS/PPS）。
    std::vector<uint8_t> extradata;

    bool IsVideo() const { return kind == tTVPStreamKind::Video; }
    bool IsAudio() const { return kind == tTVPStreamKind::Audio; }

    // 容器声明的帧率，未知时返回 0。
    double Fps() const
    {
        return (fpsDenominator > 0) ? double(fpsNumerator) / double(fpsDenominator) : 0.0;
    }

    tTVPCodecFamily Family() const { return TVPClassifyCodec(codec.c_str()); }
};

//---------------------------------------------------------------------------
// tTVPMoviePacket — 一段压缩数据
//
// data 指向解复用器内部缓冲，在下次 Read() 之前有效；调用方不得保留它。
// 这样整条管线不需要为每个包做一次分配/释放。
//---------------------------------------------------------------------------
struct tTVPMoviePacket
{
    int stream = -1;
    const uint8_t* data = nullptr;
    int size = 0;
    double pts = TVPMovieNoTime; // 毫秒
    double dts = TVPMovieNoTime;
    double duration = 0.0;
    bool keyframe = false;

    bool Empty() const { return data == nullptr || size <= 0; }
};

//---------------------------------------------------------------------------
// tTVPMovieFrame — 解码器输出的一帧（视频或音频）
//
// 平面指针的有效期到下一次 Feed()/Receive() 为止。
//---------------------------------------------------------------------------
struct tTVPMovieFrame
{
    enum class tKind
    {
        None,
        Video,
        Audio,
    };

    tKind kind = tKind::None;
    double pts = TVPMovieNoTime; // 毫秒
    double duration = 0.0;
    bool repeat = false; // 与上一帧画面相同（容器里的重复帧）

    // 视频
    tTVPPixelFormat format = tTVPPixelFormat::Unknown;
    uint8_t* planes[4] = {};
    int strides[4] = {};
    int width = 0;
    int height = 0;
    int displayWidth = 0; // 去掉黑边后的显示尺寸
    int displayHeight = 0;

    // 音频：交错或分平面的原始采样
    tTVPSampleFormat sampleFormat = tTVPSampleFormat::Unknown;
    const uint8_t* samples[8] = {};
    int channels = 0;
    int sampleRate = 0;
    int sampleCount = 0; // 每声道采样数

    void Clear()
    {
        kind = tKind::None;
        pts = TVPMovieNoTime;
        duration = 0.0;
        repeat = false;
        format = tTVPPixelFormat::Unknown;
        for (int i = 0; i < 4; ++i)
        {
            planes[i] = nullptr;
            strides[i] = 0;
        }
        width = height = displayWidth = displayHeight = 0;
        sampleFormat = tTVPSampleFormat::Unknown;
        for (int i = 0; i < 8; ++i)
            samples[i] = nullptr;
        channels = sampleRate = sampleCount = 0;
    }
};

} // namespace krkrsdl3
