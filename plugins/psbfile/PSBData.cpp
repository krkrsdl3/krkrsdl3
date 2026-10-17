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
    if (n < 1 || n > 8)
    {
        TVPConsoleLog("bad length type size");
        return false;
    }
    // ArrayN1..N8 encode the count in up to eight little-endian bytes.
    tjs_uint8 countBytes[8] = {};
    stream->ReadBuffer(countBytes, n);
    tjs_uint64 count = 0;
    for (int i = 0; i < n; ++i)
        count |= static_cast<tjs_uint64>(countBytes[i]) << (i * 8);
    if (count > INT32_MAX)
    {
        TVPConsoleLog("Long array is not supported yet");
        return false;
    }
    const tjs_uint32 entryLength =
        stream->ReadI8LE() - static_cast<tjs_uint32>(PSBObjType::NumberN8);
    if (entryLength < 1 || entryLength > 8)
    {
        TVPConsoleLog("bad array entry size");
        return false;
    }
    const tjs_uint64 position = stream->GetPosition();
    const tjs_uint64 size = stream->GetSize();
    if (position > size || count > (size - position) / entryLength)
    {
        TVPConsoleLog("array runs past the end of the PSB");
        return false;
    }
    target->reserve(target->size() + static_cast<size_t>(count));
    for (tjs_uint64 i = 0; i < count; ++i)
    {
        tjs_uint32 result = 0;
        for (tjs_uint32 j = 0; j < entryLength; ++j)
        {
            const tjs_uint32 byte = stream->ReadI8LE();
            // Consume wide entries fully, but never shift past the 32-bit result.
            if (j < 4)
                result |= byte << (j * 8);
        }
        target->push_back(result);
    }
    return true;
}
} // namespace PSB
