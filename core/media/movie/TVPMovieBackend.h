#pragma once
//---------------------------------------------------------------------------
// TVPMovieBackend.h — 解码后端的注册与挑选
//
// 接入一个新解码器 = 写一个 iTVPMovieBackend 子类 + 在 TVPRegisterMovieBackends
// 里加一行。挑选规则：优先级数值大的先试，第一个自述「能解」的后端胜出。
// 兜底型后端（FFmpeg）优先级给 0，专用后端给正数。
//---------------------------------------------------------------------------

#include <memory>
#include <string>
#include <vector>

#include "TVPMovieDecoder.h"
#include "TVPMovieDemuxer.h"

namespace krkrsdl3
{

class tTVPMovieIO;

class iTVPMovieBackend
{
public:
    virtual ~iTVPMovieBackend() {}

    virtual const char* Name() const = 0;

    // 选择优先级，数值越大越先被尝试。
    virtual int Priority() const { return 0; }

    // 创建解复用器。返回 nullptr 表示本后端不做解复用。
    virtual iTVPMovieDemuxer* CreateDemuxer() { return nullptr; }

    // 创建解码器。返回 nullptr 表示本后端不接这类流。
    virtual iTVPMovieDecoder* CreateDecoder(const tTVPMovieStream& stream)
    {
        (void)stream;
        return nullptr;
    }

    // 注册时的钩子：安装色彩转换加速实现之类。
    virtual void InstallAccelerators() {}
};

// 按优先级排列的后端表。
class tTVPMovieBackends
{
public:
    static tTVPMovieBackends& Get();

    // 幂等：同名后端只保留第一个。
    void Add(std::unique_ptr<iTVPMovieBackend> backend);

    // 依次尝试各后端的解复用器；io 由调用方持有，全部失败时返回 nullptr。
    iTVPMovieDemuxer* CreateDemuxer(tTVPMovieIO* io);

    // 按优先级挑选解码器，返回的实例已 Open 成功。
    iTVPMovieDecoder* CreateDecoder(const tTVPMovieStream& stream);

    std::vector<std::string> Names() const;

private:
    tTVPMovieBackends() {}

    std::vector<std::unique_ptr<iTVPMovieBackend>> backends_;
};

// 注册内置后端。幂等；由影片入口在首次使用时调用。
void TVPRegisterMovieBackends();

#if defined(_KRKRSDL3_USE_FFMPEG)
void TVPRegisterFFmpegBackend(tTVPMovieBackends& backends);
#endif
#if defined(_KRKRSDL3_USE_THEORA)
void TVPRegisterTheoraBackend(tTVPMovieBackends& backends);
#endif

} // namespace krkrsdl3
