#include "tjsCommHead.h"
#include "KRStreamInfo.h"

NS_KRMOVIE_BEGIN

CDVDStreamInfo::CDVDStreamInfo()
{
    extradata = NULL;
    Clear();
}
CDVDStreamInfo::CDVDStreamInfo(const CDVDStreamInfo& right, bool withextradata)
{
    extradata = NULL;
    Clear();
    Assign(right, withextradata);
}
CDVDStreamInfo::CDVDStreamInfo(const AVStream& right, bool withextradata, bool m_bMatroska)
{
    extradata = NULL;
    Clear();
    Assign(right, withextradata, m_bMatroska);
}

CDVDStreamInfo::~CDVDStreamInfo()
{
    if (extradata && extrasize)
        free(extradata);

    extradata = NULL;
    extrasize = 0;
}

void CDVDStreamInfo::Clear()
{
    codec = AV_CODEC_ID_NONE;
    //	type = STREAM_NONE;
    uniqueId = -1;
    realtime = false;
    software = false;
    codec_tag = 0;
    flags = 0;
    filename.clear();
    //	dvd = false;

    if (extradata && extrasize)
        free(extradata);

    extradata = NULL;
    extrasize = 0;

    fpsscale = 0;
    fpsrate = 0;
    height = 0;
    width = 0;
    aspect = 0.0;
    stills = false;
    level = 0;
    profile = 0;
    forced_aspect = false;
    bitsperpixel = 0;

    channels = 0;
    samplerate = 0;
    blockalign = 0;
    bitrate = 0;
    bitspersample = 0;

    orientation = 0;
}

bool CDVDStreamInfo::Equal(const CDVDStreamInfo& right, bool withextradata)
{
    if (codec != right.codec
        //	|| type != right.type
        || uniqueId != right.uniqueId || realtime != right.realtime ||
        codec_tag != right.codec_tag || flags != right.flags)
        return false;

    if (withextradata)
    {
        if (extrasize != right.extrasize)
            return false;
        if (extrasize)
        {
            if (memcmp(extradata, right.extradata, extrasize) != 0)
                return false;
        }
    }

    // VIDEO
    if (fpsscale != right.fpsscale || fpsrate != right.fpsrate || height != right.height ||
        width != right.width || stills != right.stills || level != right.level ||
        profile != right.profile || forced_aspect != right.forced_aspect ||
        bitsperpixel != right.bitsperpixel)
        return false;

    // AUDIO
    if (channels != right.channels || samplerate != right.samplerate ||
        blockalign != right.blockalign || bitrate != right.bitrate ||
        bitspersample != right.bitspersample)
        return false;

    // SUBTITLE

    return true;
}

bool CDVDStreamInfo::Equal(const AVStream& right, bool withextradata)
{
    CDVDStreamInfo info;
    info.Assign(right, withextradata);
    return Equal(info, withextradata);
}

// ASSIGNMENT
void CDVDStreamInfo::Assign(const CDVDStreamInfo& right, bool withextradata)
{
    codec = right.codec;
    uniqueId = right.uniqueId;
    realtime = right.realtime;
    codec_tag = right.codec_tag;
    flags = right.flags;
    filename = right.filename;
    //	dvd = right.dvd;

    if (extradata && extrasize)
        free(extradata);

    if (withextradata && right.extrasize)
    {
        extrasize = right.extrasize;
        extradata = malloc(extrasize);
        if (!extradata)
            return;
        memcpy(extradata, right.extradata, extrasize);
    }
    else
    {
        extrasize = 0;
        extradata = 0;
    }

    // VIDEO
    fpsscale = right.fpsscale;
    fpsrate = right.fpsrate;
    height = right.height;
    width = right.width;
    aspect = right.aspect;
    stills = right.stills;
    level = right.level;
    profile = right.profile;
    forced_aspect = right.forced_aspect;
    orientation = right.orientation;
    bitsperpixel = right.bitsperpixel;
    software = right.software;

    // AUDIO
    channels = right.channels;
    samplerate = right.samplerate;
    blockalign = right.blockalign;
    bitrate = right.bitrate;
    bitspersample = right.bitspersample;

    // SUBTITLE
}

static double SelectAspect(AVStream* st, bool& forced, bool m_bMatroska)
{
    // for stereo modes, use codec aspect ratio
    AVDictionaryEntry* entry = av_dict_get(st->metadata, "stereo_mode", NULL, 0);
    if (entry)
    {
        forced = false;
        return av_q2d(st->codecpar->sample_aspect_ratio);
    }

    // trust matroshka container
    if (m_bMatroska && st->sample_aspect_ratio.num != 0)
    {
        forced = true;
        return av_q2d(st->sample_aspect_ratio);
    }

    forced = false;
    /* if stream aspect is 1:1 or 0:0 use codec aspect */
    if ((st->sample_aspect_ratio.den == 1 || st->sample_aspect_ratio.den == 0) &&
        (st->sample_aspect_ratio.num == 1 || st->sample_aspect_ratio.num == 0) &&
        st->codecpar->sample_aspect_ratio.num != 0)
    {
        return av_q2d(st->codecpar->sample_aspect_ratio);
    }

    forced = true;
    if (st->sample_aspect_ratio.num != 0)
        return av_q2d(st->sample_aspect_ratio);

    return 0.0;
}

void CDVDStreamInfo::Assign(const AVStream& right, bool withextradata, bool m_bMatroska)
{
    Clear();

    codec = right.codecpar->codec_id;
    uniqueId = right.index;
    realtime = false;
    codec_tag = right.codecpar->codec_tag;
    profile = right.codecpar->profile;
    level = right.codecpar->level;
    flags = right.disposition;

    if (withextradata && right.codecpar->extradata && right.codecpar->extradata_size > 0)
    {
        extrasize = right.codecpar->extradata_size;
        extradata = malloc(extrasize);
        if (!extradata)
            return;
        memcpy(extradata, right.codecpar->extradata, extrasize);
    }

    if (right.codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
    {
        channels = right.codecpar->ch_layout.nb_channels;
        samplerate = right.codecpar->sample_rate;
        blockalign = right.codecpar->block_align;
        bitrate = right.codecpar->bit_rate;
        bitspersample = right.codecpar->bits_per_raw_sample;
    }
    else if (right.codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
    {
        if (m_bMatroska && right.avg_frame_rate.den && right.avg_frame_rate.num)
        {
            fpsscale = right.avg_frame_rate.den;
            fpsrate = right.avg_frame_rate.num;
        }
        else if (right.r_frame_rate.den && right.r_frame_rate.num)
        {
            fpsscale = right.r_frame_rate.num;
            fpsrate = right.r_frame_rate.den;
        }
        else
        {
            fpsscale = 0;
            fpsrate = 0;
        }
        height = right.codecpar->height;
        width = right.codecpar->width;
        aspect = SelectAspect((AVStream*)&right, forced_aspect, m_bMatroska) *
                 right.codecpar->width / right.codecpar->height;
        AVDictionaryEntry* rtag = av_dict_get(right.metadata, "rotate", NULL, 0);
        if (rtag)
            orientation = atoi(rtag->value);
        else
            orientation = 0;
        bitsperpixel = right.codecpar->bits_per_coded_sample;
    }
}

NS_KRMOVIE_END
