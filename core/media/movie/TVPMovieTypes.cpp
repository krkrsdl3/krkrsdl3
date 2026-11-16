#include "tjsCommHead.h"

#include "TVPMovieTypes.h"

#include <algorithm>
#include <cctype>

#include "Platform.h"
#include "PlatformThread.h"

namespace krkrsdl3
{

namespace
{

bool TVPNameEquals(const char* name, const char* expect, bool ignoreCase)
{
    while (*name && *expect)
    {
        char lhs = *name;
        if (ignoreCase)
            lhs = static_cast<char>(std::tolower(static_cast<unsigned char>(lhs)));
        if (lhs != *expect)
            return false;
        ++name;
        ++expect;
    }
    return *name == '\0' && *expect == '\0';
}

} // namespace

int TVPMovieThreadBudget()
{
#if defined(_KRKRSDL3_EMSCRIPTEN)
    // wasm：解码器内部不并行，避免挤占预分配的 pthread 池。
    return 1;
#else
    const int cores = std::max(1, TVPGetProcessorNum());
    return std::min(std::min(cores, 8), TVPMaxThreadNum);
#endif
}

tTVPCodecFamily TVPClassifyCodec(const char* name)
{
    if (name == nullptr || *name == '\0')
        return tTVPCodecFamily::Unknown;

    struct tEntry
    {
        const char* name;
        tTVPCodecFamily family;
    };

    // 左边是 FFmpeg 的规范名，右边是常见别名。
    static const tEntry table[] = {
        {"h264", tTVPCodecFamily::H264},         {"avc", tTVPCodecFamily::H264},
        {"hevc", tTVPCodecFamily::HEVC},         {"h265", tTVPCodecFamily::HEVC},
        {"vp8", tTVPCodecFamily::VP8},           {"vp9", tTVPCodecFamily::VP9},
        {"av1", tTVPCodecFamily::AV1},           {"theora", tTVPCodecFamily::Theora},
        {"mpeg1video", tTVPCodecFamily::MPEG1},  {"mpeg2video", tTVPCodecFamily::MPEG2},
        {"mpeg4", tTVPCodecFamily::MPEG4},       {"msmpeg4v3", tTVPCodecFamily::MPEG4},
        {"wmv1", tTVPCodecFamily::WMV},          {"wmv2", tTVPCodecFamily::WMV},
        {"wmv3", tTVPCodecFamily::WMV},          {"vc1", tTVPCodecFamily::WMV},
        {"mjpeg", tTVPCodecFamily::MJPEG},       {"aac", tTVPCodecFamily::AAC},
        {"mp3", tTVPCodecFamily::MP3},           {"mp3float", tTVPCodecFamily::MP3},
        {"vorbis", tTVPCodecFamily::Vorbis},     {"opus", tTVPCodecFamily::Opus},
        {"flac", tTVPCodecFamily::FLAC},         {"pcm_s16le", tTVPCodecFamily::PCM},
        {"pcm_s16be", tTVPCodecFamily::PCM},     {"pcm_u8", tTVPCodecFamily::PCM},
        {"pcm_f32le", tTVPCodecFamily::PCM},     {"ac3", tTVPCodecFamily::AC3},
        {"eac3", tTVPCodecFamily::AC3},          {"dca", tTVPCodecFamily::DTS},
        {"dts", tTVPCodecFamily::DTS},
    };

    for (const tEntry& entry : table)
    {
        if (TVPNameEquals(name, entry.name, true))
            return entry.family;
    }
    return tTVPCodecFamily::Unknown;
}

} // namespace krkrsdl3
