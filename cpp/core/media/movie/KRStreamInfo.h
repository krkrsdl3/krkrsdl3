#pragma once
#include "KRMovieDef.h"
#include "CodecDemux.h"
#include <string>
#include <mutex>

extern "C"
{
#include "libavcodec/avcodec.h"
}

NS_KRMOVIE_BEGIN

struct CDVDStreamInfo
{
    CDVDStreamInfo();
    CDVDStreamInfo(const CDVDStreamInfo& right, bool withextradata = true);
    CDVDStreamInfo(const AVStream& right, bool withextradata = true, bool m_bMatroska = true);

    ~CDVDStreamInfo();

    void Clear(); // clears current information
    bool Equal(const CDVDStreamInfo& right, bool withextradata);
    bool Equal(const AVStream& right, bool withextradata);

    void Assign(const CDVDStreamInfo& right, bool withextradata);
    void Assign(const AVStream& right, bool withextradata, bool m_bMatroska);

    AVCodecID codec;
    // StreamType type;
    int uniqueId;
    bool realtime;
    int flags;
    bool software; // force software decoding
    std::string filename;
    // bool dvd;

    // VIDEO
    int fpsscale; // scale of 1001 and a rate of 60000 will result in 59.94 fps
    int fpsrate;
    int height;   // height of the stream reported by the demuxer
    int width;    // width of the stream reported by the demuxer
    float aspect; // display aspect as reported by demuxer
    bool stills;  // there may be odd still frames in video
    int level; // encoder level of the stream reported by the decoder. used to qualify hw decoders.
    int profile;        // encoder profile of the stream reported by the decoder. used to qualify hw
                        // decoders.
    bool forced_aspect; // aspect is forced from container
    int orientation;    // orientation of the video in degress counter clockwise
    int bitsperpixel;

    // AUDIO
    int channels;
    int samplerate;
    int bitrate;
    int blockalign;
    int bitspersample;

    // CODEC EXTRADATA
    void* extradata;        // extra data for codec to use
    unsigned int extrasize; // size of extra data
    unsigned int codec_tag; // extra identifier hints for decoding

    bool operator==(const CDVDStreamInfo& right) { return Equal(right, true); }
    bool operator!=(const CDVDStreamInfo& right) { return !Equal(right, true); }

    CDVDStreamInfo& operator=(const CDVDStreamInfo& right)
    {
        if (this != &right)
            Assign(right, true);

        return *this;
    }

    bool operator==(const AVStream& right) { return Equal(CDVDStreamInfo(right, true), true); }
    bool operator!=(const AVStream& right) { return !Equal(CDVDStreamInfo(right, true), true); }

    CDVDStreamInfo& operator=(const AVStream& right)
    {
        Assign(right, true);
        return *this;
    }
};

NS_KRMOVIE_END
