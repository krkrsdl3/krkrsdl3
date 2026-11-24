#include "tjsCommHead.h"
#include "ncbind/ncbind.hpp"
#include "TVPStorage.h"
#include "UtilStreams.h"
#include "Platform.h"

#include <algorithm>
#include <cstring>

#define NCB_MODULE_NAME TJS_N("lzfs.dll")

namespace EmoteLz4
{

struct Lz4FrameInfo
{
    int BlockSize = 0;
    bool IndependentBlocks = false;
    bool HasBlockChecksum = false;
    bool HasContentLength = false;
    bool HasContentChecksum = false;
    bool HasDictionary = false;
    long OriginalLength = 0;
    int DictionaryId = 0;

    Lz4FrameInfo(tTJSBinaryStream* input)
    {
        // flags
        input->SetPosition(4);
        tjs_uint8 flags = input->ReadI8LE();
        int version = flags >> 6;
        if (version != 1)
            throw "Invalid LZ4 compressed stream.";
        IndependentBlocks = 0 != (flags & 0x20);
        HasBlockChecksum = 0 != (flags & 0x10);
        HasContentLength = 0 != (flags & 8);
        HasContentChecksum = 0 != (flags & 4);
        HasDictionary = 0 != (flags & 1);

        // blocksize
        tjs_int code = input->ReadI8LE();
        switch ((code >> 4) & 7)
        {
            case 4:
                BlockSize = 0x10000;
                break;
            case 5:
                BlockSize = 0x40000;
                break;
            case 6:
                BlockSize = 0x100000;
                break;
            case 7:
                BlockSize = 0x400000;
                break;
            default:
                throw "Invalid LZ4 compressed stream.";
        }

        // other
        if (HasContentLength)
        {
            tjs_uint64 length = (tjs_uint64)input->ReadI32LE();
            tjs_uint32 lengthex = input->ReadI32LE();
            length |= (tjs_uint64)lengthex << 32;
            OriginalLength = (long)length;
        }
        if (HasDictionary)
        {
            DictionaryId = input->ReadI32LE();
        }
        input->ReadI8LE(); // skip descriptor checksum
    }
};

static const int MinMatch = 4;
static const int LastLiterals = 5;
static const int MFLimit = 12;
static const int MatchLengthBits = 4;
static const int MatchLengthMask = 0xF;
static const int RunMask = 0xF;

static void CopyOverlapped(tjs_uint8* data, int src, int dst, int count)
{
    if (count <= 0)
        return;

    if (dst > src)
    {
        while (count > 0)
        {
            int chunk = std::min(dst - src, count);
            memcpy(data + dst, data + src, chunk);
            dst += chunk;
            count -= chunk;
        }
    }
    else
    {
        memcpy(data + dst, data + src, count);
    }
}

static int DecompressBlock(tjs_uint8* block,
                           tjs_int32 block_size,
                           tjs_uint8* output,
                           tjs_int32 output_size)
{
    int src = 0;
    int iend = block_size;

    int dst = 0;
    int oend = output_size;

    for (;;)
    {
        /* get literal length */
        int token = block[src++];
        int length = token >> MatchLengthBits;
        if (RunMask == length)
        {
            int n;
            do
            {
                n = block[src++];
                length += n;
            } while ((src < iend - RunMask) && (0xFF == n));
            if (dst + length < dst || src + length < src) // overflow detection
                throw "Invalid LZ4 compressed stream.";
        }

        /* copy literals */
        int copy_end = dst + length;
        if ((copy_end > oend - MFLimit) || (src + length > iend - (3 + LastLiterals)))
        {
            if ((src + length != iend) || copy_end > oend)
                throw "Invalid LZ4 compressed stream.";
            memcpy(output + dst, block + src, length);
            src += length;
            dst += length;
            break;
        }
        memcpy(output + dst, block + src, length);
        src += length;
        dst = copy_end;

        /* get offset */
        int offset = 0;
        tjs_uint16 recOffset = 0;
        memcpy(&recOffset, block + src, 2);
        offset = recOffset;
        src += 2;
        int match = dst - offset;
        if (match < 0)
            throw "Invalid LZ4 compressed stream.";

        /* get matchlength */
        length = token & MatchLengthMask;
        if (MatchLengthMask == length)
        {
            int n;
            do
            {
                n = block[src++];
                if (src > iend - LastLiterals)
                    throw "Invalid LZ4 compressed stream.";
                length += n;
            } while (0xFF == n);
            if (dst + length < dst) // overflow detection
                throw "Invalid LZ4 compressed stream.";
        }
        length += MinMatch;

        /* copy match within block */
        CopyOverlapped(output, match, dst, length);
        dst += length;
    }
    return dst; // number of output bytes decoded
}

bool IsLz4Frame(const char* sign)
{
    // 0x04 0x22 0x4D 0x18, i.e. 0x184D2204 little endian
    return (tjs_uint8)sign[0] == 0x04 && (tjs_uint8)sign[1] == 0x22 &&
           (tjs_uint8)sign[2] == 0x4D && (tjs_uint8)sign[3] == 0x18;
}

tTJSBinaryStream* GetLz4Stream(tTJSBinaryStream* orgStream)
{
    // 获取头信息
    Lz4FrameInfo info(orgStream);
    tTVPMemoryStream* _stream = new tTVPMemoryStream(nullptr, info.OriginalLength);
    // 一个Block一个Block地获取数据
    bool m_eof = false;
    tjs_int m_dataBuffSize = info.BlockSize;
    tjs_uint8* m_dataBuff = new tjs_uint8[m_dataBuffSize];
    memset(m_dataBuff, 0, m_dataBuffSize);
    tjs_int m_data_size = 0;
    tjs_int m_blockBuffSize = 0;
    tjs_uint8* m_blockBuff = NULL;
    while (!m_eof)
    {
        // 获取下一个block
        tjs_int32 block_size = 0;
        if (4 != orgStream->Read(&block_size, 4))
            throw "EndOfStreamException";
        if (0 == block_size)
        {
            m_eof = true;
            m_data_size = 0;
            if (info.HasContentChecksum)
                orgStream->ReadI32LE(); // XXX checksum is ignored
        }
        else if (block_size < 0)
        {
            m_data_size = block_size & 0x7FFFFFFF;
            if (m_dataBuff == NULL || m_data_size > m_dataBuffSize)
            {
                if (m_dataBuff != NULL)
                    delete[] m_dataBuff;
                m_dataBuff = new tjs_uint8[m_data_size];
                m_dataBuffSize = m_data_size;
                memset(m_dataBuff, 0, m_dataBuffSize);
            }
            m_data_size = orgStream->Read(m_dataBuff, m_data_size);
            if (info.HasBlockChecksum)
                orgStream->ReadI32LE(); // XXX checksum is ignored
        }
        else
        {
            tjs_int32 m_block_size = block_size;
            if (m_blockBuff == NULL || m_block_size > m_blockBuffSize)
            {
                if (m_blockBuff != NULL)
                    delete[] m_blockBuff;
                m_blockBuff = new tjs_uint8[m_block_size];
                m_blockBuffSize = m_block_size;
                memset(m_blockBuff, 0, m_blockBuffSize);
            }
            if (m_block_size != orgStream->Read(m_blockBuff, m_block_size))
                throw "EndOfStreamException";
            m_data_size =
                DecompressBlock(m_blockBuff, m_block_size, m_dataBuff, m_dataBuffSize);
            if (info.HasBlockChecksum)
                orgStream->ReadI32LE(); // XXX checksum is ignored
        }

        // 写入数据
        _stream->Write(m_dataBuff, m_data_size);
    }
    // 删除原始并返回
    if (m_dataBuff != NULL)
        delete[] m_dataBuff;
    if (m_blockBuff != NULL)
        delete[] m_blockBuff;
    delete orgStream;
    return _stream;
}

} // namespace EmoteLz4

class LZFSMedia : public iTVPStorageMedia
{
public:
    LZFSMedia() : refCount(1) {}

    virtual void AddRef() override { refCount++; };

    virtual void Release() override
    {
        if (refCount == 1)
        {
            delete this;
        }
        else
        {
            refCount--;
        }
    };

    static ttstr Strip(const ttstr& name)
    {
        if (name.StartsWith(TJS_N("lzfs://./")))
            return name.SubString(9, name.GetLen() - 9);
        if (name.StartsWith(TJS_N("lzfs://")))
            return name.SubString(7, name.GetLen() - 7);
        return name;
    }

    // returns media name like "file", "http" etc.
    virtual void GetName(ttstr& name) override { name = TJS_N("lzfs"); }

    //	virtual ttstr IsCaseSensitive() = 0;
    // returns whether this media is case sensitive or not

    // normalize domain name according with the media's rule
    virtual void NormalizeDomainName(ttstr& name) override
    {
        // nothing to do
    }

    // normalize path name according with the media's rule
    virtual void NormalizePathName(ttstr& name) override
    {
        // nothing to do
    }

    // check file existence
    virtual bool CheckExistentStorage(const ttstr& name) override
    {
        // lzfs://./foo.psb -> foo.psb, resolved through the normal storage chain
        ttstr path = Strip(name);
        return TVPIsExistentStorage(path);
    }

    // open a storage and return a tTJSBinaryStream instance.
    // name does not contain in-archive storage name but
    // is normalized.
    virtual tTJSBinaryStream* Open(const ttstr& name, tjs_uint32 flags) override
    {
        // Resolve the archive entry and transparently unwrap an LZ4 frame.
        // This is the single LZ4 decoder shared with psbfile/emoteplayer;
        // the old stub returned nullptr, so every E-mote PSB load failed.
        ttstr path = Strip(name);
        tTJSBinaryStream* stream = TVPCreateStream(path, flags);
        if (!stream)
            return nullptr;
        tjs_uint8 magic[4] = {};
        const tjs_uint got = stream->Read(magic, 4);
        stream->SetPosition(0);
        if (got == 4 && EmoteLz4::IsLz4Frame(reinterpret_cast<const char*>(magic)))
        {
            try
            {
                return EmoteLz4::GetLz4Stream(stream);
            }
            catch (...)
            {
                // GetLz4Stream deletes the source stream on failure paths.
                return nullptr;
            }
        }
        return stream;
    }

    // list files at given place
    virtual void GetListAt(const ttstr& name, iTVPStorageLister* lister) override
    {
        // nothing to do
    }

    // basically the same as above,
    // check wether given name is easily accessible from local OS filesystem.
    // if true, returns local OS native name. otherwise returns an empty string.
    virtual void GetLocallyAccessibleName(ttstr& name) override
    {
        // nothing to do
    }

protected:
    /**
     * デストラクタ
     */
    virtual ~LZFSMedia() {}

private:
    tjs_uint refCount; //< リファレンスカウント
};
static LZFSMedia* _lzfsMedia = nullptr;

void lzfs_init()
{
    // 注册media
    if (_lzfsMedia == nullptr)
    {
        _lzfsMedia = new LZFSMedia();
        TVPRegisterStorageMedia(_lzfsMedia);
    }
}

void lzfs_done()
{
    // 解注media
    if (_lzfsMedia != nullptr)
    {
        TVPUnregisterStorageMedia(_lzfsMedia);
        _lzfsMedia->Release();
        _lzfsMedia = nullptr;
    }
}

NCB_PRE_REGIST_CALLBACK(lzfs_init);
NCB_POST_UNREGIST_CALLBACK(lzfs_done);