#pragma once
#include "KRMovieDef.h"
#include <vector>
extern "C"
{
#include "libavformat/avformat.h"
}

NS_KRMOVIE_BEGIN

#define DMX_SPECIALID_STREAMCHANGE -11
struct DemuxPacket
{
    unsigned char* pData; // data
    int iSize;            // data size
    int iStreamId;        // integer representing the stream index
    int iGroupId; // the group this data belongs to, used to group data from different streams
                  // together

    double pts;      // pts in DVD_TIME_BASE
    double dts;      // dts in DVD_TIME_BASE
    double duration; // duration in DVD_TIME_BASE if available

    int dispTime;

    static void Free(DemuxPacket*);
    static DemuxPacket* Allocate(int iDataSize = 0);
};

class IDemux
{
public:
    IDemux() {}
    virtual ~IDemux() {}
    virtual void Reset() = 0;
    virtual void Abort() = 0;
    virtual void Flush() = 0;
    virtual DemuxPacket* Read() = 0;
    virtual bool SeekTime(int time, bool backwords = false, double* startpts = NULL) = 0;
    virtual void SetSpeed(int iSpeed) = 0;
    virtual int GetStreamLength() = 0;
    virtual AVStream* GetStream(int iStreamId) const = 0;
    virtual std::vector<AVStream*> GetStreams() const = 0;
    virtual int GetNrOfStreams() const = 0;
    virtual std::string GetStreamCodecName(int iStreamId) = 0;
};

NS_KRMOVIE_END
