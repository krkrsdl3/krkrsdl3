#pragma once
//---------------------------------------------------------------------------
// TVPMovieDecoder.h — 音视频统一的解码器接口
//
// 视频和音频走同一个接口：Feed 送压缩包，Receive 取帧。
// 后端（FFmpeg / libtheora / libvpx / 平台硬解）只需要实现这一个类，
// 播放器不区分音视频的驱动方式。
//---------------------------------------------------------------------------

#include "TVPMovieStream.h"

namespace krkrsdl3
{

class iTVPMovieDecoder
{
public:
    virtual ~iTVPMovieDecoder() {}

    virtual const char* Name() const = 0;

    // 打开指定流的解码器。返回 false 表示无法解码该流。
    virtual bool Open(const tTVPMovieStream& stream) = 0;
    virtual void Close() = 0;

    // 送入一个压缩包。返回 false 表示解码器输入已满，需要先 Receive。
    virtual bool Feed(const tTVPMoviePacket& packet) = 0;

    // 取出一帧。返回 false 表示暂时没有可输出的帧。
    // 返回的帧内指针有效期到下一次 Feed/Receive/Flush。
    virtual bool Receive(tTVPMovieFrame& frame) = 0;

    // 丢弃内部缓存（定位后调用）。
    virtual void Flush() = 0;

    // 输入结束，把缓存里的帧全部产出。
    virtual void Drain() = 0;

    // 视频侧提示：下一帧很可能被丢弃，解码器可以跳过实际解码工作。
    virtual void SetDropState(bool drop) { (void)drop; }
};

} // namespace krkrsdl3
