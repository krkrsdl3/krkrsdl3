//---------------------------------------------------------------------------
/*
        TJS2 Script Engine( Byte Code )
        Copyright (c), Takenori Imoto

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------

#ifndef tjsBinarySerializerH
#define tjsBinarySerializerH

#include "tjsTypes.h"
#include "tjsVariant.h"
#include "tjsError.h"
#include "tjsGlobalStringMap.h"
#include <vector>
#include <climits>

namespace TJS
{
/**
 * バイナリ形式でデータをストリーム書き出しするためのクラス
 * 形式は、MessagePack に近いもので細部TJS2用に調整している
 * 文字列は、UTF-16のまま格納
 * エンディアンはリトルエンディアンになっている
 * ヘッダーも追加される
 */
class tTJSBinarySerializer
{
public:
    enum
    {
        TYPE_POSITIVE_FIX_NUM_MIN = 0x00,
        TYPE_POSITIVE_FIX_NUM_MAX = 0x7F,
        TYPE_NEGATIVE_FIX_NUM_MIN = 0xE0,
        TYPE_NEGATIVE_FIX_NUM_MAX = 0xFF,
        TYPE_NIL = 0xC0,
        TYPE_VOID = 0xC1,
        TYPE_TRUE = 0xC2,
        TYPE_FALSE = 0xC3,

        TYPE_STRING8 = 0xC4,
        TYPE_STRING16 = 0xC5,
        TYPE_STRING32 = 0xC6,

        TYPE_FLOAT = 0xCA,
        TYPE_DOUBLE = 0xCB,

        TYPE_UINT8 = 0xCC,
        TYPE_UINT16 = 0xCD,
        TYPE_UINT32 = 0xCE,
        TYPE_UINT64 = 0xCF,
        TYPE_INT8 = 0xD0,
        TYPE_INT16 = 0xD1,
        TYPE_INT32 = 0xD2,
        TYPE_INT64 = 0xD3,

        TYPE_FIX_RAW_MIN = 0xD4,
        TYPE_FIX_RAW_MAX = 0xD9,
        TYPE_FIX_RAW_LEN =
            TYPE_FIX_RAW_MAX -
            TYPE_FIX_RAW_MIN, // 5byteまでだから効果少ないが、文字の方が頻度高いので文字にRAWエリアを割り当てる

        TYPE_RAW16 = 0xDA,
        TYPE_RAW32 = 0xDB,
        TYPE_ARRAY16 = 0xDC,
        TYPE_ARRAY32 = 0xDD,
        TYPE_MAP16 = 0xDE,
        TYPE_MAP32 = 0xDF,

        TYPE_FIX_STRING_MIN = 0xA0,
        TYPE_FIX_STRING_MAX = 0xBF,
        TYPE_FIX_STRING_LEN = TYPE_FIX_STRING_MAX - TYPE_FIX_STRING_MIN,
        TYPE_FIX_ARRAY_MIN = 0x90,
        TYPE_FIX_ARRAY_MAX = 0x9F,
        TYPE_FIX_ARRAY_LEN = TYPE_FIX_ARRAY_MAX - TYPE_FIX_ARRAY_MIN,
        TYPE_FIX_MAP_MIN = 0x80,
        TYPE_FIX_MAP_MAX = 0x8F,
        TYPE_FIX_MAP_LEN = TYPE_FIX_MAP_MAX - TYPE_FIX_MAP_MIN,
    };
    static const tjs_int HEADER_LENGTH = 8;
    static const tjs_uint8 HEADER[HEADER_LENGTH];
    static bool IsBinary(const tjs_uint8 header[HEADER_LENGTH]);

    static inline void PutInteger(tTJSBinaryStream* stream, tjs_int64 b)
    {
        if (b < 0)
        {
            if (b >= TYPE_NEGATIVE_FIX_NUM_MIN)
            {
                tjs_uint8 tmp[1];
                tmp[0] = (tjs_uint8)b;
                stream->Write(tmp, sizeof(tmp));
            }
            else if (b >= SCHAR_MIN)
            {
                tjs_uint8 tmp[2];
                tmp[0] = TYPE_INT8;
                tmp[1] = (tjs_uint8)b;
                stream->Write(tmp, sizeof(tmp));
            }
            else if (b >= SHRT_MIN)
            {
                tjs_int16 v = (tjs_int16)b;
                tjs_uint8 tmp[3];
                tmp[0] = TYPE_INT16;
                tmp[1] = (tjs_uint8)(v & 0xff);
                tmp[2] = (tjs_uint8)((v >> 8) & 0xff);
                stream->Write(tmp, sizeof(tmp));
            }
            else if (b >= LONG_MIN)
            {
                tjs_int32 v = (tjs_int32)b;
                tjs_uint8 tmp[5];
                tmp[0] = TYPE_INT32;
                tmp[1] = (tjs_uint8)(v & 0xff);
                tmp[2] = (tjs_uint8)((v >> 8) & 0xff);
                tmp[3] = (tjs_uint8)((v >> 16) & 0xff);
                tmp[4] = (tjs_uint8)((v >> 24) & 0xff);
                stream->Write(tmp, sizeof(tmp));
            }
            else
            {
                tjs_int64 v = b;
                tjs_uint8 tmp[9];
                tmp[0] = TYPE_INT64;
                tmp[1] = (tjs_uint8)(v & 0xff);
                tmp[2] = (tjs_uint8)((v >> 8) & 0xff);
                tmp[3] = (tjs_uint8)((v >> 16) & 0xff);
                tmp[4] = (tjs_uint8)((v >> 24) & 0xff);
                tmp[5] = (tjs_uint8)((v >> 32) & 0xff);
                tmp[6] = (tjs_uint8)((v >> 40) & 0xff);
                tmp[7] = (tjs_uint8)((v >> 48) & 0xff);
                tmp[8] = (tjs_uint8)((v >> 56) & 0xff);
                stream->Write(tmp, sizeof(tmp));
            }
        }
        else
        {
            if (b <= TYPE_POSITIVE_FIX_NUM_MAX)
            {
                tjs_uint8 tmp[1];
                tmp[0] = (tjs_uint8)(b);
                stream->Write(tmp, sizeof(tmp));
            }
            else if (b <= UCHAR_MAX)
            {
                tjs_uint8 tmp[2];
                tmp[0] = TYPE_UINT8;
                tmp[1] = (tjs_uint8)(b);
                stream->Write(tmp, sizeof(tmp));
            }
            else if (b <= USHRT_MAX)
            {
                tjs_uint16 v = (tjs_uint16)b;
                tjs_uint8 tmp[3];
                tmp[0] = TYPE_UINT16;
                tmp[1] = (tjs_uint8)(v & 0xff);
                tmp[2] = (tjs_uint8)((v >> 8) & 0xff);
                stream->Write(tmp, sizeof(tmp));
            }
            else if (b <= UINT_MAX)
            {
                tjs_uint32 v = (tjs_uint32)b;
                tjs_uint8 tmp[5];
                tmp[0] = TYPE_UINT32;
                tmp[1] = (tjs_uint8)(v & 0xff);
                tmp[2] = (tjs_uint8)((v >> 8) & 0xff);
                tmp[3] = (tjs_uint8)((v >> 16) & 0xff);
                tmp[4] = (tjs_uint8)((v >> 24) & 0xff);
                stream->Write(tmp, sizeof(tmp));
            }
            else
            {
                tjs_uint64 v = b;
                tjs_uint8 tmp[9];
                tmp[0] = TYPE_UINT64;
                tmp[1] = (tjs_uint8)(v & 0xff);
                tmp[2] = (tjs_uint8)((v >> 8) & 0xff);
                tmp[3] = (tjs_uint8)((v >> 16) & 0xff);
                tmp[4] = (tjs_uint8)((v >> 24) & 0xff);
                tmp[5] = (tjs_uint8)((v >> 32) & 0xff);
                tmp[6] = (tjs_uint8)((v >> 40) & 0xff);
                tmp[7] = (tjs_uint8)((v >> 48) & 0xff);
                tmp[8] = (tjs_uint8)((v >> 56) & 0xff);
                stream->Write(tmp, sizeof(tmp));
            }
        }
    }
    static inline void PutString(tTJSBinaryStream* stream, const tjs_char* val, tjs_uint len)
    {
        // convert
        tjs_uint64 buffLen = TVPUtf8ToWideCharString(val, NULL);
        if (buffLen <= 0)
            return;
        tjs_wchar* buf = new tjs_wchar[len + 1];
        TVPUtf8ToWideCharString(val, buf);
        buf[len] = 0;
        // len
        if (buffLen <= TYPE_FIX_STRING_LEN)
        {
            tjs_uint8 tmp[1];
            tmp[0] = TYPE_FIX_STRING_MIN + buffLen;
            stream->Write(tmp, sizeof(tmp));
        }
        else if (buffLen <= UCHAR_MAX)
        {
            tjs_uint8 tmp[2];
            tmp[0] = TYPE_STRING8;
            tmp[1] = buffLen;
            stream->Write(tmp, sizeof(tmp));
        }
        else if (buffLen <= USHRT_MAX)
        {
            tjs_uint16 v = buffLen;
            tjs_uint8 tmp[3];
            tmp[0] = TYPE_STRING16;
            tmp[1] = v & 0xff;
            tmp[2] = (v >> 8) & 0xff;
            stream->Write(tmp, sizeof(tmp));
        }
        else if (buffLen <= ULONG_MAX)
        {
            tjs_uint32 v = buffLen;
            tjs_uint8 tmp[5];
            tmp[0] = TYPE_STRING32;
            tmp[1] = v & 0xff;
            tmp[2] = (v >> 8) & 0xff;
            tmp[3] = (v >> 16) & 0xff;
            tmp[4] = (v >> 24) & 0xff;
            stream->Write(tmp, sizeof(tmp));
        }
        else
        {
            TJS_eTJSError(TJSWriteError);
        }
        // data
        if (buffLen)
        {
            stream->Write(buf, sizeof(tjs_wchar) * len);
        }
        // clear
        delete[] buf;
    }
    /**
     * 浮動小数点値を格納する
     */
    static inline void PutDouble(tTJSBinaryStream* stream, double b)
    {
        tjs_uint64 v = *(tjs_uint64*)&b;
        tjs_uint8 tmp[9];
        tmp[0] = TYPE_DOUBLE;
        tmp[1] = v & 0xff;
        tmp[2] = (v >> 8) & 0xff;
        tmp[3] = (v >> 16) & 0xff;
        tmp[4] = (v >> 24) & 0xff;
        tmp[5] = (v >> 32) & 0xff;
        tmp[6] = (v >> 40) & 0xff;
        tmp[7] = (v >> 48) & 0xff;
        tmp[8] = (v >> 56) & 0xff;
        stream->Write(tmp, sizeof(tmp));
    }
    static inline void PutBytes(tTJSBinaryStream* stream, const tjs_uint8* val, tjs_uint len)
    {
        if (len <= TYPE_FIX_RAW_LEN)
        {
            tjs_uint8 tmp[1];
            tmp[0] = TYPE_FIX_RAW_MIN + len;
            stream->Write(tmp, sizeof(tmp));
        }
        else if (len <= USHRT_MAX)
        {
            tjs_uint16 v = len;
            tjs_uint8 tmp[3];
            tmp[0] = TYPE_RAW16;
            tmp[1] = v & 0xff;
            tmp[2] = (v >> 8) & 0xff;
            stream->Write(tmp, sizeof(tmp));
        }
        else if (len <= UINT_MAX)
        {
            tjs_uint32 v = len;
            tjs_uint8 tmp[5];
            tmp[0] = TYPE_RAW32;
            tmp[1] = v & 0xff;
            tmp[2] = (v >> 8) & 0xff;
            tmp[3] = (v >> 16) & 0xff;
            tmp[4] = (v >> 24) & 0xff;
            stream->Write(tmp, sizeof(tmp));
        }
        else
        {
            TJS_eTJSError(TJSWriteError);
        }
        stream->Write(val, len);
    }
    /**
     * オクテット型の値を格納する
     */
    static inline void PutOctet(tTJSBinaryStream* stream, tTJSVariantOctet* val)
    {
        tjs_uint len = 0;
        const tjs_uint8* data = NULL;
        if (val)
        {
            len = val->GetLength();
            data = val->GetData();
        }
        PutBytes(stream, data, len);
    }

    /**
     * 文字列を格納する
     */
    static inline void PutString(tTJSBinaryStream* stream, const tTJSVariantString* val)
    {
        const tjs_char* data = NULL;
        tjs_int len = 0;
        if (val)
        {
            len = val->GetLength();
            if (val->LongString)
            {
                data = val->LongString;
            }
            else
            {
                data = val->ShortString;
            }
        }
        PutString(stream, data, len);
    }
    static inline void PutStartMap(tTJSBinaryStream* stream, tjs_uint count)
    {
        if (count <= TYPE_FIX_MAP_LEN)
        {
            tjs_uint8 tmp[1];
            tmp[0] = TYPE_FIX_MAP_MIN + count;
            stream->Write(tmp, sizeof(tmp));
        }
        else if (count <= USHRT_MAX)
        {
            tjs_uint16 v = count;
            tjs_uint8 tmp[3];
            tmp[0] = TYPE_MAP16;
            tmp[1] = v & 0xff;
            tmp[2] = (v >> 8) & 0xff;
            stream->Write(tmp, sizeof(tmp));
        }
        else if (count <= UINT_MAX)
        {
            tjs_uint32 v = count;
            tjs_uint8 tmp[5];
            tmp[0] = TYPE_MAP32;
            tmp[1] = v & 0xff;
            tmp[2] = (v >> 8) & 0xff;
            tmp[3] = (v >> 16) & 0xff;
            tmp[4] = (v >> 24) & 0xff;
            stream->Write(tmp, sizeof(tmp));
        }
        else
        {
            TJS_eTJSError(TJSWriteError);
        }
    }
    static inline void PutStartArray(tTJSBinaryStream* stream, tjs_uint count)
    {
        if (count <= TYPE_FIX_ARRAY_LEN)
        {
            tjs_uint8 tmp[1];
            tmp[0] = TYPE_FIX_ARRAY_MIN + count;
            stream->Write(tmp, sizeof(tmp));
        }
        else if (count <= USHRT_MAX)
        {
            tjs_uint16 v = count;
            tjs_uint8 tmp[3];
            tmp[0] = TYPE_ARRAY16;
            tmp[1] = v & 0xff;
            tmp[2] = (v >> 8) & 0xff;
            stream->Write(tmp, sizeof(tmp));
        }
        else if (count <= UINT_MAX)
        {
            tjs_uint32 v = count;
            tjs_uint8 tmp[5];
            tmp[0] = TYPE_ARRAY32;
            tmp[1] = v & 0xff;
            tmp[2] = (v >> 8) & 0xff;
            tmp[3] = (v >> 16) & 0xff;
            tmp[4] = (v >> 24) & 0xff;
            stream->Write(tmp, sizeof(tmp));
        }
        else
        {
            TJS_eTJSError(TJSWriteError);
        }
    }
    static inline void PutNull(tTJSBinaryStream* stream)
    {
        tjs_uint8 tmp[1];
        tmp[0] = TYPE_NIL;
        stream->Write(tmp, sizeof(tmp));
    }

    static inline tjs_uint16 Read16(const tjs_uint8* buff, tjs_uint& index)
    {
        tjs_uint16 ret = buff[index] | (buff[index + 1] << 8);
        index += sizeof(tjs_uint16);
        return ret;
    }
    static inline tjs_uint32 Read32(const tjs_uint8* buff, tjs_uint& index)
    {
        tjs_uint32 ret = buff[index] | (buff[index + 1] << 8) | (buff[index + 2] << 16) |
                         (buff[index + 3] << 24);
        index += sizeof(tjs_uint32);
        return ret;
    }
    static inline tjs_uint64 Read64(const tjs_uint8* buff, tjs_uint& index)
    {
        tjs_uint64 ret = (tjs_uint64)buff[index] | ((tjs_uint64)buff[index + 1] << 8) |
                         ((tjs_uint64)buff[index + 2] << 16) | ((tjs_uint64)buff[index + 3] << 24) |
                         ((tjs_uint64)buff[index + 4] << 32) | ((tjs_uint64)buff[index + 5] << 40) |
                         ((tjs_uint64)buff[index + 6] << 48) | ((tjs_uint64)buff[index + 7] << 56);
        index += sizeof(tjs_uint64);
        return ret;
    }
    static inline tTJSVariantString* ReadString(const tjs_uint8* buff,
                                                tjs_uint len,
                                                tjs_uint& index)
    {
        tTJSVariantString* ret = NULL;
        if (len > 0)
        {
            tjs_int64 bufLen = TVPWideCharToUtf8String((tjs_wchar*)(buff + index), NULL, len);
            if (bufLen == (size_t)-1)
                return ret;
            tjs_char* str = new tjs_char[bufLen + 1];
            TVPWideCharToUtf8String((tjs_wchar*)(buff + index), str, len);
            str[bufLen] = 0;
            ret = TJSAllocVariantString(str, bufLen);
            index += len * 2;
            delete[] str;
        }
        return ret;
    }
    static inline tTJSVariant* ReadStringVarint(const tjs_uint8* buff,
                                                tjs_uint len,
                                                tjs_uint& index)
    {
        tTJSVariantString* ret = ReadString(buff, len, index);
        if (!ret)
        {
            tTJSVariant* var = new tTJSVariant(TJSMapGlobalStringMap(ttstr()));
            return var;
        }
        else
        {
            ttstr str(ret);
            tTJSVariant* var = new tTJSVariant(TJSMapGlobalStringMap(str));
            ret->Release();
            return var;
        }
    }
    static inline tTJSVariant* ReadOctetVarint(const tjs_uint8* buff, tjs_uint len, tjs_uint& index)
    {
        tTJSVariantOctet* oct = TJSAllocVariantOctet(&buff[index], len);
        index += len;
        tTJSVariant* var = new tTJSVariant();
        *var = oct;
        oct->Release();
        return var;
    }

    /**
     * バイアント値を格納する
     * オブジェクト型は無視している
     */
    static void PutVariant(tTJSBinaryStream* stream, tTJSVariant& v);

    tTJSBinarySerializer();
    tTJSBinarySerializer(class tTJSDictionaryObject* root);
    tTJSBinarySerializer(class tTJSArrayObject* root);
    ~tTJSBinarySerializer();
    tTJSVariant* Read(tTJSBinaryStream* stream);

private:
    iTJSDispatch2* DicClass;
    class tTJSDictionaryObject* RootDictionary;
    class tTJSArrayObject* RootArray;

    class tTJSDictionaryObject* CreateDictionary(tjs_uint count);
    class tTJSArrayObject* CreateArray(tjs_uint count);
    void AddDictionary(class tTJSDictionaryObject* dic,
                       tTJSVariantString* name,
                       tTJSVariant* value);
    void InsertArray(class tTJSArrayObject* array, tjs_uint index, tTJSVariant* value);
    tTJSVariant* ReadBasicType(const tjs_uint8* buff, const tjs_uint size, tjs_uint& index);
    tTJSVariant* ReadArray(const tjs_uint8* buff,
                           const tjs_uint size,
                           const tjs_uint count,
                           tjs_uint& index);
    tTJSVariant* ReadDictionary(const tjs_uint8* buff,
                                const tjs_uint size,
                                const tjs_uint count,
                                tjs_uint& index);
};

} // namespace TJS
#endif // tjsBinarySerializerH
