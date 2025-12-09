#pragma once
#include "KRMovieDef.h"
#include "KRStreamInfo.h"

extern "C"
{
#include "libavcodec/avcodec.h"
}

NS_KRMOVIE_BEGIN

// should be entirely filled by all codecs
struct DVDVideoPicture
{
    double pts; // timestamp in seconds, used in the CVideoPlayer class to keep track of pts
    double dts;

    uint8_t* data[4]; // [4] = alpha channel, currently not used
    int iLineSize[4]; // [4] = alpha channel, currently not used

    unsigned int iFlags;

    double iRepeatPicture;
    double iDuration;
    unsigned int iFrameType : 4; //< see defines above // 1->I, 2->P, 3->B, 0->Undef
    unsigned int color_matrix : 4;
    unsigned int color_range : 1; //< 1 indicate if we have a full range of color
    unsigned int chroma_position;
    unsigned int color_primaries;
    unsigned int color_transfer;
    unsigned int extended_format;

    int8_t* qp_table; //< Quantization parameters, primarily used by filters
    int qstride;
    int qscale_type;

    unsigned int iWidth;
    unsigned int iHeight;
    unsigned int iDisplayWidth;  //< width of the picture without black bars
    unsigned int iDisplayHeight; //< height of the picture without black bars
};

#define DVP_FLAG_TOP_FIELD_FIRST 0x00000001
#define DVP_FLAG_REPEAT_TOP_FIELD 0x00000002 //< Set to indicate that the top field should be repeated
#define DVP_FLAG_ALLOCATED 0x00000004  //< Set to indicate that this has allocated data
#define DVP_FLAG_INTERLACED 0x00000008 //< Set to indicate that this frame is interlaced
#define DVP_FLAG_DROPPED 0x00000010 //< indicate that this picture has been dropped in decoder stage, will have no data

struct CDVDStreamInfo;

class CDVDVideoCodec
{
public:
    CDVDVideoCodec() {}
    virtual ~CDVDVideoCodec() {}

    /**
     * Open the decoder, returns true on success
     */
    virtual bool Open(CDVDStreamInfo& hints) = 0;

    /**
     * returns one or a combination of VC_ messages
     * pData and iSize can be NULL, this means we should flush the rest of the data.
     */
    virtual bool AddData(const DemuxPacket& packet) = 0;

    /**
     * Reset the decoder.
     * Should be the same as calling Dispose and Open after each other
     */
    virtual void Reset() = 0;

    /**
     * returns true if successfull
     * the data is valid until the next Decode call
     */
    virtual bool GetPicture(DVDVideoPicture* pDvdVideoPicture) = 0;

    /**
     * will be called by video player indicating if a frame will eventually be dropped
     * codec can then skip actually decoding the data, just consume the data set picture headers
     */
    virtual void SetDropState(bool bDrop) = 0;

    /**
     * How many packets should player remember, so codec can recover should
     * something cause it to flush outside of players control
     */
    virtual unsigned GetConvergeCount() = 0;

    /**
     * For calculation of dropping requirements player asks for some information.
     * - pts : right after decoder, used to detect gaps (dropped frames in decoder)
     * - droppedFrames : indicates if decoder has dropped a frame
     *                 -1 means that decoder has no info on this.
     *
     * If codec does not implement this method, pts of decoded frame at input
     * video player is used. In case decoder does post-proc and de-interlacing there
     * may be quite some frames queued up between exit decoder and entry player.
     */
    virtual bool GetCodecStats(double& pts, int& droppedFrames) = 0;
};

NS_KRMOVIE_END
