#include "tjsCommHead.h"
#include "PSBData.h"
#include "Platform.h"

namespace PSB
{

bool PSBHeader::isEncrypted() const
{
    return offsetEncrypt > MAX_LENGTH + 16 || offsetNames == 0 ||
           (version > 1 && offsetEncrypt != offsetNames && offsetEncrypt != 0);
}

tjs_uint32 PSBHeader::getHeaderLength() const
{
    if (version < 3)
        return 40u;
    if (version > 3)
        return 56u;
    return 44u;
}

bool PSBHeader::parsePSBHeader(TJS::tTJSBinaryStream* stream)
{
    stream->ReadBuffer(signature, 4);
    stream->ReadBuffer(&version, 2);
    stream->ReadBuffer(&encrypt, 2);
    stream->ReadBuffer(&offsetEncrypt, 4);
    stream->ReadBuffer(&offsetNames, 4);

    if (!memcmp(signature, "MDF", 3) || !memcmp(signature, "MFL", 3))
    {
        TVPConsoleLog("Maybe a MDF file");
        return false;
    }
    if (memcmp(signature, "PSB", 3))
    {
        TVPConsoleLog("Not a valid PSB file");
        return false;
    }
    if (offsetNames < stream->GetSize())
    {
        stream->ReadBuffer(&offsetStrings, 4);
        stream->ReadBuffer(&offsetStringsData, 4);
        stream->ReadBuffer(&offsetChunkOffsets, 4);
        stream->ReadBuffer(&offsetChunkLengths, 4);
        stream->ReadBuffer(&offsetChunkData, 4);
        stream->ReadBuffer(&offsetEntries, 4);

        if (version > 2)
        {
            stream->ReadBuffer(&checksum, 4);
        }
        if (version > 3)
        {
            stream->ReadBuffer(&offsetExtraChunkOffsets, 4);
            stream->ReadBuffer(&offsetExtraChunkLengths, 4);
            stream->ReadBuffer(&offsetExtraChunkData, 4);
        }
    }
    return true;
}

bool parsePSBArray(std::vector<tjs_uint32>* target, tjs_int8 n, TJS::tTJSBinaryStream* stream)
{
    if (n < 0 || n > 8)
    {
        TVPConsoleLog("bad length type size");
        return false;
    }
    tjs_uint32 count = 0;
    stream->ReadBuffer(&count, n);
    if (count > INT32_MAX)
    {
        TVPConsoleLog("Long array is not supported yet");
        return false;
    }
    tjs_uint32 entryLength = stream->ReadI8LE() - static_cast<tjs_uint32>(PSBObjType::NumberN8);
    target->reserve(count);
    for (int i = 0; i < count; i++)
    {
        tjs_uint32 result = 0;
        for (tjs_uint8 j = 0; j < entryLength; ++j)
        {
            result |= stream->ReadI8LE() << (j * 8);
        }
        target->push_back(result);
    }
    return true;
}
} // namespace PSB
