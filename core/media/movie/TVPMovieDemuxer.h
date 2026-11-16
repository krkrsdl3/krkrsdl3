#pragma once
//---------------------------------------------------------------------------
// TVPMovieDemuxer.h — 解复用器接口
//---------------------------------------------------------------------------

#include <vector>

#include "TVPMovieStream.h"

namespace krkrsdl3
{

class tTVPMovieIO;

class iTVPMovieDemuxer
{
public:
    virtual ~iTVPMovieDemuxer() {}

    // 打开数据源。io 的生命周期由播放器负责，解复用器只持有引用，不得释放。
    // 返回 false 表示本解复用器不认识这份数据，注册表会改用下一个后端，
    // 因此失败时必须回到「未打开」状态。
    virtual bool Open(tTVPMovieIO* io) = 0;

    virtual const char* Name() const = 0;

    // 全部流描述。Open 成功后填充，之后只读。
    virtual const std::vector<tTVPMovieStream>& Streams() const = 0;

    // 读一个包。false 表示到达结尾；被中断时也返回 false，用 Aborted() 区分。
    // 包的 data 指向内部缓冲，有效期到下一次 Read()。
    virtual bool Read(tTVPMoviePacket& packet) = 0;

    // 定位到指定毫秒（向前找关键帧）。成功后下一次 Read 从该位置开始。
    virtual bool Seek(double positionMs) = 0;

    // 总时长，毫秒；未知时返回 0。
    virtual double Duration() const = 0;

    // 中断阻塞中的 Read。解码线程退出时调用。
    virtual void Abort() = 0;
    virtual bool Aborted() const = 0;

    // 播放倍速提示：后端可以据此丢弃非关键帧，1.0 为正常。
    virtual void SetRate(double rate) { (void)rate; }
};

} // namespace krkrsdl3
