#pragma once
//---------------------------------------------------------------------------
// TVPMoviePixels.h — 解码帧到 RGBA 的转换
//
// 播放器只认 RGBA，但解码器可以吐任意 tTVPPixelFormat。转换集中在这里：
//   * 打包格式（RGBA/BGRA/RGB24…）走内置的纯 C++ 路径；
//   * 平面 YUV 优先用后端注入的加速实现（FFmpeg 构建注入 sws），
//     没有加速实现时退回到内置的定点转换。
//---------------------------------------------------------------------------

#include <cstdint>

#include "TVPMovieStream.h"

namespace krkrsdl3
{

// 内置转换；成功返回 true。
bool TVPConvertFrameToRGBA(const tTVPMovieFrame& frame, uint8_t* dst, int dstPitch);

// 加速实现的环境，签名与内置一致。
using tTVPFrameConverter = bool (*)(const tTVPMovieFrame&, uint8_t*, int);

// 注入加速实现（传 nullptr 恢复内置实现）。线程安全，进程内生效。
void TVPSetFrameConverter(tTVPFrameConverter converter);

} // namespace krkrsdl3
