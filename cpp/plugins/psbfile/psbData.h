#pragma once

#include "tjsCommHead.h"

namespace PSB
{
    enum PSBObjType : unsigned char
    {
    None = 0x0,
    Null = 0x1,
    False = 0x2,
    True = 0x3,

    // int
    NumberN0 = 0x4,
    NumberN1 = 0x5,
    NumberN2 = 0x6,
    NumberN3 = 0x7,
    NumberN4 = 0x8,
    NumberN5 = 0x9,
    NumberN6 = 0xA,
    NumberN7 = 0xB,
    NumberN8 = 0xC,

    // array N(NUMBER) is count mask
    ArrayN1 = 0xD,
    ArrayN2 = 0xE,
    ArrayN3 = 0xF,
    ArrayN4 = 0x10,
    ArrayN5 = 0x11,
    ArrayN6 = 0x12,
    ArrayN7 = 0x13,
    ArrayN8 = 0x14,

    // index of key name, only used in PSBv1 (according to GMMan's doc)
    KeyNameN1 = 0x11,
    KeyNameN2 = 0x12,
    KeyNameN3 = 0x13,
    KeyNameN4 = 0x14,

    // index of strings table
    StringN1 = 0x15,
    StringN2 = 0x16,
    StringN3 = 0x17,
    StringN4 = 0x18,

    // resource of thunk
    ResourceN1 = 0x19,
    ResourceN2 = 0x1A,
    ResourceN3 = 0x1B,
    ResourceN4 = 0x1C,

    // fpu value
    Float0 = 0x1D,
    Float = 0x1E,
    Double = 0x1F,

    // objects
    List = 0x20,    // object list
    Objects = 0x21, // object dictionary

    ExtraChunkN1 = 0x22,
    ExtraChunkN2 = 0x23,
    ExtraChunkN3 = 0x24,
    ExtraChunkN4 = 0x25,
};

struct PSBHeader
{
    char signature[4];
    tjs_uint16 version;
    tjs_uint16 encrypt;
    tjs_uint32 offsetEncrypt;
    tjs_uint32 offsetNames;
    tjs_uint32 offsetStrings;
    tjs_uint32 offsetStringsData;
    tjs_uint32 offsetChunkOffsets;
    tjs_uint32 offsetChunkLengths;
    tjs_uint32 offsetChunkData;
    tjs_uint32 offsetEntries;
    tjs_uint32 checksum;
    tjs_uint32 offsetExtraChunkOffsets;
    tjs_uint32 offsetExtraChunkLengths;
    tjs_uint32 offsetExtraChunkData;

    static constexpr int MAX_LENGTH = 56;

    bool isEncrypted() const;

    tjs_uint32 getHeaderLength() const;

    bool parsePSBHeader(TJS::tTJSBinaryStream* stream);
};

bool parsePSBArray(std::vector<tjs_uint32>* target, tjs_int8 count, TJS::tTJSBinaryStream* stream);
}