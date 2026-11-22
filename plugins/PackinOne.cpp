#include "tjsCommHead.h"
#include "ncbind/ncbind.hpp"
#include "TVPStorage.h"
#include "TVPScript.h"
#include "tjsNative.h"
#include "tjsArray.h"
#include "tjsDictionary.h"
#include "tjsConfig.h"
#include <cstdint>
#include <cstring>
#include <vector>

#define NCB_MODULE_NAME TJS_N("PackinOne.dll")

/*
 * 原版 PackinOne.dll 是若干插件的合集，导出符号包括：
 *   Layer.fillAlpha / copyBottomBlueToTopAlpha / copyRightBlueToLeftAlpha /
 *   clipAlphaRect / copyAlphaToProvince / fillByProvince / shrinkCopy /
 *   shrinkCopyFast、layerExImage、layerExRaster、FontEx、ScriptsAdd、
 *   StoragesFstat、ArrayAdd / DictAdd。
 * 本工程把这些实现拆成了独立模块，这里按原 DLL 的组合把它们一起拉起来。
 *
 * 其中 layerExBTOA 是 GFX_Movie.tjs 的 MovieControlLayer 每帧要用的
 * （onFrameUpdate 里 targetLayer.fillAlpha()），不加载它原版视频链路会中断。
 */
static void InitPlugin_PackinOne()
{
    try
    {
        ncbAutoRegister::LoadModule(TJS_N("layerExBTOA.dll"));
        ncbAutoRegister::LoadModule(TJS_N("layerExImage.dll"));
        ncbAutoRegister::LoadModule(TJS_N("layerExRaster.dll"));
        ncbAutoRegister::LoadModule(TJS_N("addFont.dll"));
    }
    catch (...)
    {
    }
}

namespace pbd {

//----------------------------------------------------------------------
// 基础读取
//----------------------------------------------------------------------
struct Reader
{
    const uint8_t* p;
    size_t n;
    size_t pos;

    Reader(const uint8_t* data, size_t size) : p(data), n(size), pos(0) {}

    bool need(size_t k) const { return pos + k <= n; }
    uint8_t u8()
    {
        if (!need(1))
            throw 1;
        return p[pos++];
    }
    uint16_t u16()
    {
        if (!need(2))
            throw 1;
        uint16_t v = (uint16_t)(p[pos] | (p[pos + 1] << 8));
        pos += 2;
        return v;
    }
    uint32_t u32()
    {
        if (!need(4))
            throw 1;
        uint32_t v = (uint32_t)p[pos] | ((uint32_t)p[pos + 1] << 8) | ((uint32_t)p[pos + 2] << 16) |
                     ((uint32_t)p[pos + 3] << 24);
        pos += 4;
        return v;
    }
    uint64_t u64()
    {
        uint64_t lo = u32();
        uint64_t hi = u32();
        return lo | (hi << 32);
    }
    const uint8_t* take(size_t k)
    {
        if (!need(k))
            throw 1;
        const uint8_t* r = p + pos;
        pos += k;
        return r;
    }
};

static uint32_t rotl32(uint32_t x, int r) { return (x << r) | (x >> (32 - r)); }
static uint32_t rotr32(uint32_t x, int r) { return (x >> r) | (x << (32 - r)); }

//---------------------------------------------------------------------------
// LZ4 block 解压（支持 64KiB 字典）
//---------------------------------------------------------------------------
static bool lz4_decompress_block(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& out,
                                 const std::vector<uint8_t>& dict)
{
    size_t sp = 0;
    const size_t outBase = out.size();

    auto copyMatch = [&](size_t offset, size_t len) -> bool
    {
        for (size_t i = 0; i < len; i++)
        {
            size_t cur = out.size();
            if (offset > cur)
                return false;
            size_t from = cur - offset;
            uint8_t b;
            if (from >= outBase)
                b = out[from];
            else
            {
                size_t idx = dict.size() - (outBase - from);
                if (idx >= dict.size())
                    return false;
                b = dict[idx];
            }
            out.push_back(b);
        }
        return true;
    };

    while (sp < srcSize)
    {
        uint8_t token = src[sp++];
        size_t lit = token >> 4;
        if (lit == 15)
        {
            uint8_t x;
            do
            {
                if (sp >= srcSize)
                    return false;
                x = src[sp++];
                lit += x;
            } while (x == 255);
        }
        if (sp + lit > srcSize)
            return false;
        out.insert(out.end(), src + sp, src + sp + lit);
        sp += lit;

        if (sp >= srcSize)
            break;

        if (sp + 2 > srcSize)
            return false;
        size_t offset = (size_t)src[sp] | ((size_t)src[sp + 1] << 8);
        sp += 2;
        if (offset == 0)
            return false;

        size_t mlen = (size_t)(token & 0x0f) + 4;
        if ((token & 0x0f) == 15)
        {
            uint8_t x;
            do
            {
                if (sp >= srcSize)
                    return false;
                x = src[sp++];
                mlen += x;
            } while (x == 255);
        }
        if (!copyMatch(offset, mlen))
            return false;
    }
    return true;
}

//---------------------------------------------------------------------------
// XXH32
//---------------------------------------------------------------------------
static uint32_t xxh32(const uint8_t* data, size_t len, uint32_t seed)
{
    const uint32_t P1 = 2654435761u, P2 = 2246822519u, P3 = 3266489917u, P4 = 668265263u,
                   P5 = 374761393u;
    const uint8_t* p = data;
    const uint8_t* end = data + len;
    uint32_t h;
    if (len >= 16)
    {
        uint32_t v1 = seed + P1 + P2, v2 = seed + P2, v3 = seed, v4 = seed - P1;
        const uint8_t* limit = end - 16;
        auto rd = [](const uint8_t* q)
        {
            return (uint32_t)q[0] | ((uint32_t)q[1] << 8) | ((uint32_t)q[2] << 16) |
                   ((uint32_t)q[3] << 24);
        };
        do
        {
            v1 = rotl32(v1 + rd(p) * P2, 13) * P1;
            p += 4;
            v2 = rotl32(v2 + rd(p) * P2, 13) * P1;
            p += 4;
            v3 = rotl32(v3 + rd(p) * P2, 13) * P1;
            p += 4;
            v4 = rotl32(v4 + rd(p) * P2, 13) * P1;
            p += 4;
        } while (p <= limit);
        h = rotl32(v1, 1) + rotl32(v2, 7) + rotl32(v3, 12) + rotl32(v4, 18);
    }
    else
    {
        h = seed + P5;
    }
    h += (uint32_t)len;
    while (p + 4 <= end)
    {
        h += ((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
              ((uint32_t)p[3] << 24)) *
             P3;
        h = rotl32(h, 17) * P4;
        p += 4;
    }
    while (p < end)
    {
        h += (*p) * P5;
        h = rotl32(h, 11) * P1;
        p++;
    }
    h ^= h >> 15;
    h *= P2;
    h ^= h >> 13;
    h *= P3;
    h ^= h >> 16;
    return h;
}

//---------------------------------------------------------------------------
// BLAKE2s-256（keyed MAC）
//---------------------------------------------------------------------------
static const uint32_t B2S_IV[8] = { 0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
                                    0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19 };
static const uint8_t B2S_SIGMA[10][16] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
    { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
    { 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 },
    { 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
    { 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13 },
    { 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9 },
    { 12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11 },
    { 13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10 },
    { 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5 },
    { 10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0 }
};

static void b2s_compress(uint32_t h[8], const uint8_t block[64], uint64_t t, bool last)
{
    uint32_t m[16];
    for (int i = 0; i < 16; i++)
        m[i] = (uint32_t)block[i * 4] | ((uint32_t)block[i * 4 + 1] << 8) |
               ((uint32_t)block[i * 4 + 2] << 16) | ((uint32_t)block[i * 4 + 3] << 24);

    uint32_t v[16];
    for (int i = 0; i < 8; i++)
        v[i] = h[i];
    for (int i = 0; i < 8; i++)
        v[8 + i] = B2S_IV[i];
    v[12] ^= (uint32_t)(t & 0xffffffff);
    v[13] ^= (uint32_t)(t >> 32);
    if (last)
        v[14] = ~v[14];

    auto G = [&](int a, int b, int c, int d, uint32_t x, uint32_t y)
    {
        v[a] = v[a] + v[b] + x;
        v[d] = rotr32(v[d] ^ v[a], 16);
        v[c] = v[c] + v[d];
        v[b] = rotr32(v[b] ^ v[c], 12);
        v[a] = v[a] + v[b] + y;
        v[d] = rotr32(v[d] ^ v[a], 8);
        v[c] = v[c] + v[d];
        v[b] = rotr32(v[b] ^ v[c], 7);
    };

    for (int r = 0; r < 10; r++)
    {
        const uint8_t* s = B2S_SIGMA[r];
        G(0, 4, 8, 12, m[s[0]], m[s[1]]);
        G(1, 5, 9, 13, m[s[2]], m[s[3]]);
        G(2, 6, 10, 14, m[s[4]], m[s[5]]);
        G(3, 7, 11, 15, m[s[6]], m[s[7]]);
        G(0, 5, 10, 15, m[s[8]], m[s[9]]);
        G(1, 6, 11, 12, m[s[10]], m[s[11]]);
        G(2, 7, 8, 13, m[s[12]], m[s[13]]);
        G(3, 4, 9, 14, m[s[14]], m[s[15]]);
    }
    for (int i = 0; i < 8; i++)
        h[i] ^= v[i] ^ v[8 + i];
}

static void blake2s_keyed(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t dataLen,
                          uint8_t out[32])
{
    uint32_t h[8];
    for (int i = 0; i < 8; i++)
        h[i] = B2S_IV[i];
    h[0] ^= 0x01010000u ^ ((uint32_t)keyLen << 8) ^ 32u;

    uint8_t block[64];
    uint64_t t = 0;

    memset(block, 0, sizeof(block));

    if (keyLen)
    {
        memcpy(block, key, keyLen);
        if (dataLen == 0)
        {
            // 空消息：key 块就是最后一块
            b2s_compress(h, block, 64, true);
            for (int i = 0; i < 8; i++)
            {
                out[i * 4] = (uint8_t)(h[i] & 0xff);
                out[i * 4 + 1] = (uint8_t)((h[i] >> 8) & 0xff);
                out[i * 4 + 2] = (uint8_t)((h[i] >> 16) & 0xff);
                out[i * 4 + 3] = (uint8_t)((h[i] >> 24) & 0xff);
            }
            return;
        }
        b2s_compress(h, block, 64, false);
        t = 64;
    }

    while (dataLen > 64)
    {
        memcpy(block, data, 64);
        t += 64;
        b2s_compress(h, block, t, false);
        data += 64;
        dataLen -= 64;
    }

    memset(block, 0, sizeof(block));
    if (dataLen)
        memcpy(block, data, dataLen);
    t += dataLen;
    b2s_compress(h, block, t, true);

    for (int i = 0; i < 8; i++)
    {
        out[i * 4] = (uint8_t)(h[i] & 0xff);
        out[i * 4 + 1] = (uint8_t)((h[i] >> 8) & 0xff);
        out[i * 4 + 2] = (uint8_t)((h[i] >> 16) & 0xff);
        out[i * 4 + 3] = (uint8_t)((h[i] >> 24) & 0xff);
    }
}

//---------------------------------------------------------------------------
// ChaCha
//---------------------------------------------------------------------------
static void chacha_block(const uint8_t key[32], uint64_t counter, uint32_t nonceLo,
                         uint32_t nonceHi, int rounds, uint8_t out[64])
{
    static const unsigned char constants[16] = { 'e', 'x', 'p', 'a', 'n', 'd', ' ', '3',
                                                 '2', '-', 'b', 'y', 't', 'e', ' ', 'k' };
    uint32_t st[16];
    for (int i = 0; i < 4; i++)
        st[i] = (uint32_t)constants[i * 4] | ((uint32_t)constants[i * 4 + 1] << 8) |
                ((uint32_t)constants[i * 4 + 2] << 16) | ((uint32_t)constants[i * 4 + 3] << 24);
    for (int i = 0; i < 8; i++)
        st[4 + i] = (uint32_t)key[i * 4] | ((uint32_t)key[i * 4 + 1] << 8) |
                    ((uint32_t)key[i * 4 + 2] << 16) | ((uint32_t)key[i * 4 + 3] << 24);
    st[12] = (uint32_t)(counter & 0xffffffff);
    st[13] = (uint32_t)(counter >> 32);
    st[14] = nonceLo;
    st[15] = nonceHi;

    uint32_t x[16];
    memcpy(x, st, sizeof(x));

    auto QR = [](uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d)
    {
        a += b;
        d ^= a;
        d = rotl32(d, 16);
        c += d;
        b ^= c;
        b = rotl32(b, 12);
        a += b;
        d ^= a;
        d = rotl32(d, 8);
        c += d;
        b ^= c;
        b = rotl32(b, 7);
    };

    for (int i = 0; i < rounds / 2; i++)
    {
        QR(x[0], x[4], x[8], x[12]);
        QR(x[1], x[5], x[9], x[13]);
        QR(x[2], x[6], x[10], x[14]);
        QR(x[3], x[7], x[11], x[15]);
        QR(x[0], x[5], x[10], x[15]);
        QR(x[1], x[6], x[11], x[12]);
        QR(x[2], x[7], x[8], x[13]);
        QR(x[3], x[4], x[9], x[14]);
    }
    for (int i = 0; i < 16; i++)
    {
        uint32_t v = x[i] + st[i];
        out[i * 4] = (uint8_t)(v & 0xff);
        out[i * 4 + 1] = (uint8_t)((v >> 8) & 0xff);
        out[i * 4 + 2] = (uint8_t)((v >> 16) & 0xff);
        out[i * 4 + 3] = (uint8_t)((v >> 24) & 0xff);
    }
}

static uint32_t xorshift32(uint32_t x, uint32_t fallback)
{
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x ? x : fallback;
}

static void expand_stream_block(const uint8_t base[64], size_t blocks, uint32_t fallback,
                                std::vector<uint8_t>& out)
{
    if (blocks <= 1)
    {
        out.insert(out.end(), base, base + 64);
        return;
    }
    std::vector<uint32_t> words;
    words.reserve(blocks * 16);
    for (int i = 0; i < 16; i++)
        words.push_back((uint32_t)base[i * 4] | ((uint32_t)base[i * 4 + 1] << 8) |
                        ((uint32_t)base[i * 4 + 2] << 16) | ((uint32_t)base[i * 4 + 3] << 24));

    size_t src_i = 0;
    size_t extra_groups = blocks * 4 - 4;
    for (size_t g = 0; g < extra_groups; g++)
    {
        for (int k = 0; k < 4; k++)
        {
            uint32_t source = words[src_i++];
            words.push_back(xorshift32(source, fallback));
        }
    }
    for (uint32_t w : words)
    {
        out.push_back((uint8_t)(w & 0xff));
        out.push_back((uint8_t)((w >> 8) & 0xff));
        out.push_back((uint8_t)((w >> 16) & 0xff));
        out.push_back((uint8_t)((w >> 24) & 0xff));
    }
}

static bool crypt_4s0(std::vector<uint8_t>& data, uint32_t seed, uint16_t crypt,
                      const std::vector<uint8_t>& iv)
{
    int rounds = 0;
    size_t expand = 1;
    switch (crypt)
    {
        case 0: return true;
        case 1: rounds = 8; expand = 0x10; break;
        case 2: rounds = 12; expand = 8; break;
        case 3: rounds = 20; expand = 4; break;
        case 4: rounds = 8; expand = 1; break;
        case 5: rounds = 12; expand = 1; break;
        case 6: rounds = 20; expand = 1; break;
        default: return false;
    }

    uint8_t seedBytes[4] = { (uint8_t)(seed & 0xff), (uint8_t)((seed >> 8) & 0xff),
                             (uint8_t)((seed >> 16) & 0xff), (uint8_t)((seed >> 24) & 0xff) };
    uint8_t key[32];
    blake2s_keyed(seedBytes, 4, iv.empty() ? nullptr : iv.data(), iv.size(), key);

    {
        char kb[80];
        for (int i = 0; i < 32; i++)
            snprintf(kb + i * 2, 3, "%02x", key[i]);
    }
    uint32_t nonceLo = xxh32(iv.empty() ? (const uint8_t*)"" : iv.data(), iv.size(), seed);
    uint32_t nonceHi = seed;
    uint32_t fallback = nonceHi ^ nonceLo;
    if (!fallback)
        fallback = seed;
    if (!fallback)
        fallback = 0xffffffffu;

    size_t offset = 0;
    uint64_t counter = 0;
    std::vector<uint8_t> stream;
    while (offset < data.size())
    {
        uint8_t base[64];
        chacha_block(key, counter++, nonceLo, nonceHi, rounds, base);
        stream.clear();
        expand_stream_block(base, expand, fallback, stream);
        size_t take = stream.size() < (data.size() - offset) ? stream.size() : (data.size() - offset);
        for (size_t i = 0; i < take; i++)
            data[offset + i] ^= stream[i];
        offset += take;
    }
    return true;
}

static bool decompress_frames(const std::vector<uint8_t>& in, std::vector<uint8_t>& out)
{
    size_t pos = 0;
    while (pos < in.size())
    {
        if (in.size() - pos < 2)
            return false;
        size_t size = (size_t)in[pos] | ((size_t)in[pos + 1] << 8);
        pos += 2;
        if (size == 0)
            return true;
        if (pos + size > in.size())
            return false;

        std::vector<uint8_t> dict;
        size_t dictStart = out.size() > 65536 ? out.size() - 65536 : 0;
        dict.assign(out.begin() + dictStart, out.end());

        size_t before = out.size();
        if (!lz4_decompress_block(in.data() + pos, size, out, dict))
            return false;
        if (out.size() == before)
            return false;
        if (out.size() - before > 65536)
            return false;
        pos += size;
    }
    return true;
}

//---------------------------------------------------------------------------
// 值解析
//---------------------------------------------------------------------------
static void parse_value(Reader& r, tTJSVariant& out, int depth);

static ttstr read_string(Reader& r)
{
    uint32_t units = r.u32();
    if (units > 16u * 1024u * 1024u)
        throw 1;
    const uint8_t* p = r.take((size_t)units * 2);
    std::vector<tjs_wchar> buf((size_t)units + 1);
    for (uint32_t i = 0; i < units; i++)
        buf[i] = (tjs_wchar)((uint16_t)p[i * 2] | ((uint16_t)p[i * 2 + 1] << 8));
    buf[units] = 0;

    tjs_int64 need = TVPWideCharToUtf8String(buf.data(), NULL);
    if (need < 0)
        throw 1;
    std::vector<char> utf8((size_t)need + 1);
    TVPWideCharToUtf8String(buf.data(), utf8.data(), utf8.size());
    return ttstr(utf8.data());
}

static void parse_value(Reader& r, tTJSVariant& out, int depth)
{
    if (depth > 256)
        throw 1;
    uint8_t type = r.u8();
    r.u8(); // 校验字节

    switch (type)
    {
        case 0: out.Clear(); break;
        case 2: out = read_string(r); break;
        case 4: out = (tjs_int64)(int64_t)r.u64(); break;
        case 5:
        {
            uint64_t bits = r.u64();
            double dv;
            memcpy(&dv, &bits, 8);
            out = dv;
            break;
        }
        case 0x81:
        {
            uint32_t count = r.u32();
            if (count > 1000000u)
                throw 1;
            iTJSDispatch2* arr = TJSCreateArrayObject();
            if (!arr)
                throw 1;
            for (uint32_t i = 0; i < count; i++)
            {
                tTJSVariant item;
                parse_value(r, item, depth + 1);
                arr->PropSetByNum(TJS_MEMBERENSURE, i, &item, arr);
            }
            out.SetObject(arr, arr);
            arr->Release();
            break;
        }
        case 0xc1:
        {
            uint32_t count = r.u32();
            if (count > 1000000u)
                throw 1;
            iTJSDispatch2* dict = TJSCreateDictionaryObject();
            if (!dict)
                throw 1;
            for (uint32_t i = 0; i < count; i++)
            {
                ttstr key = read_string(r);
                tTJSVariant item;
                parse_value(r, item, depth + 1);
                dict->PropSet(TJS_MEMBERENSURE, key.c_str(), key.GetHint(), &item, dict);
            }
            out.SetObject(dict, dict);
            dict->Release();
            break;
        }
        default: throw 1;
    }
}


//---------------------------------------------------------------------------
// 值序列化（供 saveDataPack 使用）
//
// 与 parse_value 一一对应：1 字节类型 + 1 字节保留 + 负载。
// 容器头统一写 TJS/ns0（不加密、不压缩），loadBinaryPack 两种格式都能读。
//---------------------------------------------------------------------------
struct Writer
{
    std::vector<uint8_t>& out;
    explicit Writer(std::vector<uint8_t>& o) : out(o) {}

    void u8(uint8_t v) { out.push_back(v); }
    void u16(uint16_t v)
    {
        u8((uint8_t)(v & 0xff));
        u8((uint8_t)(v >> 8));
    }
    void u32(uint32_t v)
    {
        for (int i = 0; i < 4; i++)
            u8((uint8_t)((v >> (i * 8)) & 0xff));
    }
    void u64(uint64_t v)
    {
        for (int i = 0; i < 8; i++)
            u8((uint8_t)((v >> (i * 8)) & 0xff));
    }
    //! PBD 里的字符串是 UTF-16LE 单位序列（u32 长度 + 数据）
    void str(const ttstr& s)
    {
        const char* p = s.c_str();
        tjs_int64 units = (p && *p) ? TVPUtf8ToWideCharString(p, NULL) : 0;
        if (units < 0)
            units = 0;
        std::vector<tjs_wchar> buf((size_t)units + 1);
        if (units > 0)
            TVPUtf8ToWideCharString(p, buf.data());
        u32((uint32_t)units);
        for (tjs_int64 i = 0; i < units; i++)
            u16((uint16_t)buf[(size_t)i]);
    }
};

//! 字典枚举回调（EnumMembers 的 param = (name, flags, value)）
class DictKeyEnum : public tTJSDispatch
{
public:
    std::vector<ttstr> keys;

    tjs_error FuncCall(tjs_uint32 flag, const tjs_char* membername, tjs_uint32* hint,
                       tTJSVariant* result, tjs_int numparams, tTJSVariant** param,
                       iTJSDispatch2* objthis) override
    {
        if (numparams >= 3 && param[0]->Type() == tvtString)
        {
            const tjs_uint32 f = (tjs_uint32)(tjs_int)*param[1];
            if (!(f & TJS_HIDDENMEMBER))
                keys.push_back(param[0]->GetString());
        }
        if (result)
            *result = (tjs_int)1;
        return TJS_S_OK;
    }
};

static bool serialize_value(const tTJSVariant& v, Writer& w, int depth);

static bool serialize_object(iTJSDispatch2* obj, Writer& w, int depth)
{
    if (!obj)
        return false;

    if (obj->IsInstanceOf(0, NULL, NULL, TJS_N("Array"), obj) == TJS_S_TRUE)
    {
        tTJSVariant countVar;
        tjs_int count = 0;
        if (TJS_SUCCEEDED(obj->PropGet(0, TJS_N("count"), NULL, &countVar, obj)))
            count = (tjs_int)countVar;
        if (count < 0 || count > 1000000)
            return false;

        w.u8(0x81);
        w.u8(0);
        w.u32((uint32_t)count);
        for (tjs_int i = 0; i < count; i++)
        {
            tTJSVariant item;
            if (TJS_FAILED(obj->PropGetByNum(0, i, &item, obj)))
                return false;
            if (!serialize_value(item, w, depth + 1))
                return false;
        }
        return true;
    }

    // Dictionary 以及其它普通对象：按字典写
    DictKeyEnum cb;
    tTJSVariantClosure clo(&cb, NULL);
    obj->EnumMembers(TJS_IGNOREPROP, &clo, obj);

    w.u8(0xc1);
    w.u8(0);
    w.u32((uint32_t)cb.keys.size());
    for (size_t i = 0; i < cb.keys.size(); i++)
    {
        w.str(cb.keys[i]);
        tTJSVariant item;
        if (TJS_FAILED(obj->PropGet(0, cb.keys[i].c_str(), cb.keys[i].GetHint(), &item, obj)))
            return false;
        if (!serialize_value(item, w, depth + 1))
            return false;
    }
    return true;
}

static bool serialize_value(const tTJSVariant& v, Writer& w, int depth)
{
    if (depth > 256)
        return false;

    switch (v.Type())
    {
        case tvtVoid:
            w.u8(0);
            w.u8(0);
            return true;
        case tvtInteger:
            w.u8(4);
            w.u8(0);
            w.u64((uint64_t)(int64_t)v.AsInteger());
            return true;
        case tvtReal:
        {
            w.u8(5);
            w.u8(0);
            const double d = (double)v.AsReal();
            uint64_t bits = 0;
            memcpy(&bits, &d, 8);
            w.u64(bits);
            return true;
        }
        case tvtString:
            w.u8(2);
            w.u8(0);
            w.str(v.GetString());
            return true;
        case tvtObject:
            return serialize_object(v.AsObjectNoAddRef(), w, depth);
        default:
            return false;
    }
}

} // namespace pbd

//---------------------------------------------------------------------------
// SliceLayer / SliceLoader：原版 PackinOne.dll 的图层切片加载器
//
// 游戏的 system/psdlayer.tjs 直接依赖这些接口：
//   SliceLayer.SliceLoader.loadNormalImage(storage, layer)
//   SliceLayer.SliceLoader.loadSlicedImage(storage, imageHeight, layers)
//   new SliceLayer.SliceLoader(layer, sliced) / loadImages / setSliceProp / sliceDelayed
// 缺少它们时 PSDLayer.setImageInfo 会抛 "Member SliceLayer does not exist"，
// 立绘图层全部为空 —— 表现就是「立绘不显示」。
//
// 原版按 TLG 切片做延迟加载；本工程把 PBD 里每个立绘图层当成一张完整 TLG，
// 直接 loadImages + setSizeToImageSize 即可（都是引擎现成的 Layer 能力）。
//---------------------------------------------------------------------------
class SliceLoader
{
    iTJSDispatch2* Owner; //!< 目标图层
    iTJSDispatch2* Sliced; //!< 切片配置（当前实现按整图处理，不使用）

    //! 把 storage 整图载入图层
    static void LoadIntoLayer(iTJSDispatch2* layer, const ttstr& storage)
    {
        if (!layer)
            return;
        tTJSVariant arg(storage);
        tTJSVariant* args[1] = { &arg };
        layer->FuncCall(0, TJS_N("loadImages"), NULL, NULL, 1, args, layer);
        layer->FuncCall(0, TJS_N("setSizeToImageSize"), NULL, NULL, 0, NULL, layer);
        layer->FuncCall(0, TJS_N("update"), NULL, NULL, 0, NULL, layer);
    }

public:
    //! scripts 传进来的第二参数不一定是对象：psdlayer.tjs 在 sliced 为 1 时
    //! 传的是整数（切片高度/标志），此时 typeof != "Object"，脚本侧会传 void。
    //! 所以这里必须按 variant 接收，不能声明成 iTJSDispatch2*，否则
    //! "Cannot convert the variable type ((void) to Object)" 会让整个图层加载失败。
    SliceLoader(tTJSVariant owner, tTJSVariant sliced) : Owner(nullptr), Sliced(nullptr)
    {
        if (owner.Type() == tvtObject)
            Owner = owner.AsObjectNoAddRef();
        if (sliced.Type() == tvtObject)
            Sliced = sliced.AsObjectNoAddRef();
        if (Owner)
            Owner->AddRef();
        if (Sliced)
            Sliced->AddRef();
    }

    ~SliceLoader()
    {
        if (Owner)
            Owner->Release();
        if (Sliced)
            Sliced->Release();
    }

    //! SliceLayer.SliceLoader.loadNormalImage(storage, layer)
    static tjs_error loadNormalImage(tTJSVariant* result, tjs_int numparams, tTJSVariant** param,
                                     iTJSDispatch2* objthis)
    {
        bool ok = false;
        try
        {
            if (numparams >= 2 && param[0]->Type() == tvtString)
            {
                iTJSDispatch2* layer = param[1]->AsObjectNoAddRef();
                if (layer)
                {
                    LoadIntoLayer(layer, param[0]->GetString());
                    ok = true;
                }
            }
        }
        catch (...)
        {
            ok = false;
        }
        if (result)
            *result = (tjs_int)(ok ? 1 : 0);
        return TJS_S_OK;
    }

    //! SliceLayer.SliceLoader.loadSlicedImage(storage, imageHeight, layers)
    static tjs_error loadSlicedImage(tTJSVariant* result, tjs_int numparams, tTJSVariant** param,
                                     iTJSDispatch2* objthis)
    {
        bool ok = false;
        try
        {
            if (numparams >= 3 && param[0]->Type() == tvtString)
            {
                const ttstr storage = param[0]->GetString();
                iTJSDispatch2* layers = param[2]->AsObjectNoAddRef();
                if (layers)
                {
                    tTJSVariant count;
                    if (TJS_SUCCEEDED(layers->PropGet(0, TJS_N("count"), NULL, &count, layers)))
                    {
                        const tjs_int n = (tjs_int)count;
                        for (tjs_int i = 0; i < n; i++)
                        {
                            tTJSVariant item;
                            if (TJS_FAILED(layers->PropGetByNum(0, i, &item, layers)))
                                continue;
                            iTJSDispatch2* layer = item.AsObjectNoAddRef();
                            if (layer)
                            {
                                LoadIntoLayer(layer, storage);
                                ok = true;
                            }
                        }
                    }
                }
            }
        }
        catch (...)
        {
        }
        if (result)
            *result = (tjs_int)(ok ? 1 : 0);
        return TJS_S_OK;
    }

    //! sliceLoader.loadImages(storage, ...)
    static tjs_error loadImages(tTJSVariant* result, tjs_int numparams, tTJSVariant** param,
                                SliceLoader* self)
    {
        bool ok = false;
        try
        {
            if (self && self->Owner && numparams >= 1 && param[0]->Type() == tvtString)
            {
                LoadIntoLayer(self->Owner, param[0]->GetString());
                ok = true;
            }
        }
        catch (...)
        {
        }
        if (result)
            *result = (tjs_int)(ok ? 1 : 0);
        return TJS_S_OK;
    }

    //! sliceLoader.setSliceProp(name, value)：把属性转发给目标图层
    static tjs_error setSliceProp(tTJSVariant* result, tjs_int numparams, tTJSVariant** param,
                                  SliceLoader* self)
    {
        try
        {
            if (self && self->Owner && numparams >= 2 && param[0]->Type() == tvtString)
            {
                ttstr name = param[0]->GetString();
                self->Owner->PropSet(0, name.c_str(), name.GetHint(), param[1], self->Owner);
            }
        }
        catch (...)
        {
        }
        return TJS_S_OK;
    }

    //! sliceDelayed：原版用于延迟切片，本实现没有延迟，读写都当空
    static tjs_error getSliceDelayed(tTJSVariant* result, tjs_int numparams, tTJSVariant** param,
                                     SliceLoader* self)
    {
        if (result)
            result->Clear();
        return TJS_S_OK;
    }

    static tjs_error setSliceDelayed(tTJSVariant* result, tjs_int numparams, tTJSVariant** param,
                                     SliceLoader* self)
    {
        return TJS_S_OK;
    }
};

//! SliceLayer 本体只是 `SliceLayer.SliceLoader` / `SliceLayer.Config` 的载体
class SliceLayer
{
public:
    SliceLayer() {}
};

//! ncbind 默认没有 tTJSVariant 的转换器；这里补一个直通转换，
//! 让原生构造函数可以原样接收任意类型的参数（包括 void / 整数）。
struct NCBVariantPassThrough
{
    inline void operator()(tTJSVariant& dst, const tTJSVariant& src) const { dst = src; }
};
NCB_SET_TOVALUE_CONVERTOR(tTJSVariant, NCBVariantPassThrough);

NCB_REGISTER_CLASS(SliceLoader)
{
    NCB_CONSTRUCTOR((tTJSVariant, tTJSVariant));
    RawCallback(TJS_N("loadImages"), &SliceLoader::loadImages, 0);
    RawCallback(TJS_N("setSliceProp"), &SliceLoader::setSliceProp, 0);
    RawCallback(TJS_N("sliceDelayed"), &SliceLoader::getSliceDelayed, &SliceLoader::setSliceDelayed, 0);
};

NCB_REGISTER_CLASS(SliceLayer)
{
    NCB_CONSTRUCTOR(());
};

//! 注册完成后把静态入口挂到 SliceLayer 上（原版就是 SliceLayer.SliceLoader.*）
static void InitSliceLayerStatics()
{
    iTJSDispatch2* global = TVPGetScriptDispatch();
    if (!global)
    {
        return;
    }

    tTJSVariant loaderVar;
    if (TJS_FAILED(global->PropGet(0, TJS_N("SliceLoader"), NULL, &loaderVar, global)))
    {
        return;
    }
    iTJSDispatch2* loaderClass = loaderVar.AsObjectNoAddRef();
    if (!loaderClass)
        return;

    if (iTJSDispatch2* fnNormal =
            TJSCreateNativeClassMethod(&SliceLoader::loadNormalImage))
    {
        tTJSVariant v(fnNormal);
        loaderClass->PropSet(TJS_MEMBERENSURE, TJS_N("loadNormalImage"), NULL, &v, loaderClass);
        fnNormal->Release();
    }
    if (iTJSDispatch2* fnSliced =
            TJSCreateNativeClassMethod(&SliceLoader::loadSlicedImage))
    {
        tTJSVariant v(fnSliced);
        loaderClass->PropSet(TJS_MEMBERENSURE, TJS_N("loadSlicedImage"), NULL, &v, loaderClass);
        fnSliced->Release();
    }

    tTJSVariant layerVar;
    if (TJS_FAILED(global->PropGet(0, TJS_N("SliceLayer"), NULL, &layerVar, global)))
    {
        return;
    }
    iTJSDispatch2* layerClass = layerVar.AsObjectNoAddRef();
    if (!layerClass)
        return;

    {
        tTJSVariant v(loaderClass);
        layerClass->PropSet(TJS_MEMBERENSURE, TJS_N("SliceLoader"), NULL, &v, layerClass);
    }
    if (iTJSDispatch2* config = TJSCreateDictionaryObject())
    {
        // psdlayer.tjs 的 initLayers 会做：
        //     layer.sliced = (SliceLayer.Config.height < layer.height)
        // Config 为空时右边是 void < 数字，在 TJS2 里恒真 —— 所有立绘图层都会被
        // 当成"超高图"丢给 SliceLoader，立绘直接不显示。这里给一个足够大的上限，
        // 让正常尺寸的图层走普通的 Layer.loadImages 路径（软件渲染没有纹理尺寸限制）。
        tTJSVariant limit((tjs_int)16384);
        config->PropSet(TJS_MEMBERENSURE, TJS_N("height"), NULL, &limit, config);
        config->PropSet(TJS_MEMBERENSURE, TJS_N("width"), NULL, &limit, config);
        tTJSVariant v(config);
        layerClass->PropSet(TJS_MEMBERENSURE, TJS_N("Config"), NULL, &v, layerClass);
        config->Release();
    }
}

NCB_POST_REGIST_CALLBACK(InitSliceLayerStatics);

//---------------------------------------------------------------------------
// Scripts.loadDataPack(name)
//
// 按顺序尝试两种来源，前一个失败就换下一个：
//   1. uipsd/<name>.js —— UI 定义（脚本形式，执行后取 global.mmyPack）
//   2. <name>[.pbd]    —— 二进制 PBD（TJS/4s0 / TJS/ns0）
//
// 游戏脚本里只有调用、没有定义，这个成员完全由 PackinOne 提供。
//---------------------------------------------------------------------------
class PbdDataPack
{
public:
    //! 尝试执行 uipsd/<name>.js
    static bool loadScriptPack(const ttstr& name, tTJSVariant* result)
    {
        try
        {
            const ttstr jsName = ttstr(TJS_N("uipsd/")) + name + ttstr(TJS_N(".js"));
            if (!TVPIsExistentStorage(jsName))
                return false;

            iTJSDispatch2* global = TVPGetScriptDispatch();
            if (!global)
                return false;

            // uipsd/*.js 的内容形如 "global.mmyPack=%[...]"：是语句而不是表达式，
            // 而且必须先把上一次的结果清掉，否则脚本没赋值时会拿到旧包。
            {
                tTJSVariant empty;
                global->PropSet(TJS_MEMBERENSURE, TJS_N("mmyPack"), NULL, &empty, global);
            }

            TVPExecuteStorage(jsName, NULL, NULL, false, TJS_N(""));

            tTJSVariant pack;
            if (TJS_FAILED(global->PropGet(0, TJS_N("mmyPack"), NULL, &pack, global)))
                return false;
            if (pack.Type() == tvtVoid)
                return false;

            *result = pack;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    //! 尝试按二进制 PBD 解析 <name>[.pbd]
    //! 按名字原样读一个 PBD 容器
    static bool loadBinaryPackFile(const ttstr& file, tTJSVariant* result)
    {

        std::vector<uint8_t> raw;
        try
        {
            tTJSBinaryStream* stream = TVPCreateStream(file, TJS_BS_READ);
            if (!stream)
                return false;
            try
            {
                const tjs_uint64 size = stream->GetSize();
                if (size > 0 && size < 64 * 1024 * 1024)
                {
                    raw.resize((size_t)size);
                    stream->ReadBuffer(raw.data(), (tjs_uint)size);
                }
            }
            catch (...)
            {
                raw.clear();
            }
            delete stream;
        }
        catch (...)
        {
            return false;
        }

        if (raw.size() < 16)
            return false;

        const bool is4s0 = memcmp(raw.data(), "TJS/4s0\0", 8) == 0;
        const bool isns0 = memcmp(raw.data(), "TJS/ns0\0", 8) == 0;
        if (!is4s0 && !isns0)
            return false;

        const uint32_t seed = (uint32_t)raw[8] | ((uint32_t)raw[9] << 8) |
                              ((uint32_t)raw[10] << 16) | ((uint32_t)raw[11] << 24);
        const uint16_t crypt = (uint16_t)(raw[12] | (raw[13] << 8));
        const uint16_t ivLen = (uint16_t)(raw[14] | (raw[15] << 8));

        try
        {
            std::vector<uint8_t> body;
            if (isns0)
            {
                body.assign(raw.begin() + 16, raw.end());
            }
            else
            {
                const size_t ivEnd = 16 + (size_t)ivLen;
                if (ivEnd > raw.size())
                    return false;
                std::vector<uint8_t> iv(raw.begin() + 16, raw.begin() + ivEnd);
                std::vector<uint8_t> framed(raw.begin() + ivEnd, raw.end());
                if (!pbd::crypt_4s0(framed, seed, crypt, iv))
                    return false;
                if (!pbd::decompress_frames(framed, body))
                    return false;
            }

            pbd::Reader reader(body.data(), body.size());
            tTJSVariant root;
            pbd::parse_value(reader, root, 0);
            if (root.Type() == tvtVoid)
                return false;

            *result = root;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    //! 先按原样试（savedata/*.ksd 这类带扩展名的存档），再补 .pbd
    static bool loadBinaryPack(const ttstr& name, tTJSVariant* result)
    {
        if (loadBinaryPackFile(name, result))
            return true;

        const tjs_char* s = name.c_str();
        const size_t len = name.GetLen();
        if (len >= 4 && TJS_strcasecmp(s + len - 4, TJS_N(".pbd")) == 0)
            return false;
        return loadBinaryPackFile(name + ttstr(TJS_N(".pbd")), result);
    }

    static tjs_error loadDataPackImpl(tTJSVariant* result, tjs_int numparams, tTJSVariant** param,
                                      iTJSDispatch2* objthis)
    {
        if (!result)
            return TJS_E_BADPARAMCOUNT;
        result->Clear();

        if (numparams < 1 || param[0]->Type() != tvtString)
            return TJS_E_BADPARAMCOUNT;

        const ttstr name = param[0]->GetString();

        if (loadScriptPack(name, result))
            return TJS_S_OK;
        if (loadBinaryPack(name, result))
            return TJS_S_OK;

        result->Clear();
        return TJS_S_OK;
    }

    //! 外层包装：任何异常都不能漏到 TJS 侧。
    /*!
        游戏在加载 txtevlist.tjs 时会用不支持的 PBD（sisconratio.pbd，值类型 0x03）
        调 loadDataPack；原生抛出的 TJS 异常若逃到脚本层会直接终止进程，
        所以这里统一吞掉并返回空值，让调用方走它自己的回退路径。
    */
    static tjs_error loadDataPack(tTJSVariant* result, tjs_int numparams, tTJSVariant** param,
                                  iTJSDispatch2* objthis)
    {
        try
        {
            return loadDataPackImpl(result, numparams, param, objthis);
        }
        catch (...)
        {
            if (result)
                result->Clear();
            return TJS_S_OK;
        }
    }

    //! Scripts.makeDataPackDigest(name, tick, id [, flag])
    /*!
        存档流程用它生成"指纹"字符串：BookMarkIO_DataPack.save 里写进
        struct.seed，MakeLockKey 还拿它拼存档锁的 key（会截取子串）。
        原版返回数据包摘要；这里按参数算一个稳定的 64 位散列并格式化成
        16 位十六进制，只要能返回字符串且不抛异常即可。
    */
    static tjs_error makeDataPackDigest(tTJSVariant* result, tjs_int numparams, tTJSVariant** param,
                                        iTJSDispatch2* objthis)
    {
        uint64_t h = 1469598103934665603ULL; // FNV-1a 64 bit offset basis
        try
        {
            for (tjs_int i = 0; i < numparams; i++)
            {
                if (!param[i])
                    continue;
                if (param[i]->Type() == tvtString)
                {
                    const ttstr str = param[i]->GetString();
                    const uint8_t* p = (const uint8_t*)str.c_str();
                    const size_t n = (size_t)str.GetLen();
                    for (size_t k = 0; k < n; k++)
                    {
                        h ^= p[k];
                        h *= 1099511628211ULL;
                    }
                }
                else
                {
                    const int64_t v = (int64_t)param[i]->AsInteger();
                    uint8_t buf[8];
                    memcpy(buf, &v, 8);
                    for (int k = 0; k < 8; k++)
                    {
                        h ^= buf[k];
                        h *= 1099511628211ULL;
                    }
                }
                h ^= 0x7c; // '|'
                h *= 1099511628211ULL;
            }
        }
        catch (...)
        {
        }

        char buf[17];
        for (int i = 0; i < 16; i++)
        {
            const int nib = (int)((h >> ((15 - i) * 4)) & 0xf);
            buf[i] = (char)(nib < 10 ? ('0' + nib) : ('a' + nib - 10));
        }
        buf[16] = 0;

        if (result)
            *result = ttstr(buf);
        return TJS_S_OK;
    }

    //! Scripts.makeDataPackThumb(storage, ext, quality, component)
    /*!
        原版把存档缩略图编码成 octet 交给 saveDataPack 塞进数据包。本工程的
        saveDataPack 不写缩略图（存档列表里的预览走 savedata/*.bmp），
        这里返回 void 即可，调用方只把它原样传给 saveDataPack。
    */
    static tjs_error makeDataPackThumb(tTJSVariant* result, tjs_int numparams, tTJSVariant** param,
                                       iTJSDispatch2* objthis)
    {
        if (result)
            result->Clear();
        return TJS_S_OK;
    }

    //! Scripts.saveDataPack(name, data, options)
    /*!
        退出时 KAG 的 saveSystemVariables / voicemode.tjs 会把系统变量和播放列表
        写进 savedata/*.ksd；原版由 PackinOne 的这个接口完成，缺了会报
        "Member saveDataPack does not exist" 并存不下数据。

        options 目前只看 compress（本实现固定写未压缩的 TJS/ns0）；缩略图与
        digest 由其它接口负责，这里不处理，写出的文件仍能被 loadDataPack 读回。
    */
    static tjs_error saveDataPack(tTJSVariant* result, tjs_int numparams, tTJSVariant** param,
                                  iTJSDispatch2* objthis)
    {
        bool ok = false;
        try
        {
            if (numparams >= 2 && param[0]->Type() == tvtString)
            {
                const ttstr file = param[0]->GetString();

                std::vector<uint8_t> body;
                pbd::Writer w(body);
                if (pbd::serialize_value(*param[1], w, 0))
                {
                    std::vector<uint8_t> out;
                    static const uint8_t magic[8] = { 'T', 'J', 'S', '/', 'n', 's', '0', 0 };
                    out.insert(out.end(), magic, magic + 8);
                    // seed(4) + crypt(2) + ivlen(2)：ns0 不需要加密，全部填 0
                    for (int i = 0; i < 8; i++)
                        out.push_back(0);
                    out.insert(out.end(), body.begin(), body.end());

                    tTJSBinaryStream* stream = TVPCreateStream(file, TJS_BS_WRITE);
                    if (stream)
                    {
                        try
                        {
                            stream->WriteBuffer(out.data(), (tjs_uint)out.size());
                            ok = true;
                        }
                        catch (...)
                        {
                            ok = false;
                        }
                        delete stream;
                    }
                }
            }
        }
        catch (...)
        {
            ok = false;
        }

        if (result)
            *result = (tjs_int)(ok ? 1 : 0);
        return TJS_S_OK;
    }
};


//---------------------------------------------------------------------------
// 把 loadDataPack 挂成"脚本删不掉"的成员
//
// sysscn/AfterInit.tjs 会在启动末尾执行：
//     Scripts.loadDataPack = function(name) { ...只处理 uipsd/<name>.js，
//                                             其它名字一律返回 1280x720 的假包... };
// 这个覆盖一旦生效，PSDLayers.loadPBD() 拿到的就是假包（没有 layer_type），
// 立绘图层一个都建不出来 —— 表现就是「立绘不显示」。
//
// TJS2 的 tTJSCustomObject::PropSet 有一个既定行为：当成员当前值是对象、且该
// 对象的 PropSet(NULL) 返回成功时，赋值会被吞掉、成员不被替换（属性机制就是
// 靠它实现的）。所以这里把成员做成一个自实现的 dispatch：
//   PropGet(NULL) -> 返回自身（取值即函数）
//   FuncCall(NULL)-> 走 loadDataPack 原生实现
//   PropSet(NULL) -> 直接返回成功（脚本侧赋值无效）
// 这样既保留原生解码能力，又不会被脚本顶掉，也不需要给引擎加任何钩子。
//---------------------------------------------------------------------------
class LoadDataPackMember : public tTJSDispatch
{
    typedef tTJSDispatch inherited;

public:
    tjs_error FuncCall(tjs_uint32 flag, const tjs_char* membername, tjs_uint32* hint,
                       tTJSVariant* result, tjs_int numparams, tTJSVariant** param,
                       iTJSDispatch2* objthis) override
    {
        if (membername)
            return inherited::FuncCall(flag, membername, hint, result, numparams, param, objthis);
        return PbdDataPack::loadDataPack(result, numparams, param, objthis);
    }

    tjs_error PropGet(tjs_uint32 flag, const tjs_char* membername, tjs_uint32* hint,
                      tTJSVariant* result, iTJSDispatch2* objthis) override
    {
        if (membername)
            return inherited::PropGet(flag, membername, hint, result, objthis);
        if (result)
            *result = static_cast<iTJSDispatch2*>(this);
        return TJS_S_OK;
    }

    tjs_error PropSet(tjs_uint32 flag, const tjs_char* membername, tjs_uint32* hint,
                      const tTJSVariant* param, iTJSDispatch2* objthis) override
    {
        if (membername)
            return inherited::PropSet(flag, membername, hint, param, objthis);
        // 脚本侧对 Scripts.loadDataPack 的覆盖无效
        return TJS_S_OK;
    }

    tjs_error IsInstanceOf(tjs_uint32 flag, const tjs_char* membername, tjs_uint32* hint,
                           const tjs_char* classname, iTJSDispatch2* objthis) override
    {
        if (membername)
            return inherited::IsInstanceOf(flag, membername, hint, classname, objthis);
        return TJS_S_FALSE;
    }
};

static void InstallLoadDataPackMember()
{
    iTJSDispatch2* global = TVPGetScriptDispatch();
    if (!global)
        return;

    tTJSVariant scriptsVar;
    if (TJS_FAILED(global->PropGet(0, TJS_N("Scripts"), NULL, &scriptsVar, global)))
        return;
    iTJSDispatch2* scripts = scriptsVar.AsObjectNoAddRef();
    if (!scripts)
        return;

    LoadDataPackMember* member = new LoadDataPackMember();
    tTJSVariant value(static_cast<iTJSDispatch2*>(member));
    scripts->PropSet(TJS_MEMBERENSURE, TJS_N("loadDataPack"), NULL, &value, scripts);
    member->Release();
}

//---------------------------------------------------------------------------
// Storages.saveOctet(name, data) / Storages.loadOctet(name)
//
// 原版 PackinOne 在 Storages 上补的二进制读写接口：
//   - voicemode.tjs 的 store 里用 saveOctet 做兜底写入
//   - 存档缩略图 / 其它二进制块也走这里
// 只接受 octet（以及字符串，按 UTF-8 字节写）；其它类型直接忽略，
// 避免把 "saveDataPack 的返回值" 这种整数当成数据把文件覆盖坏。
//---------------------------------------------------------------------------
class StorageOctet
{
public:
    static tjs_error saveOctet(tTJSVariant* result, tjs_int numparams, tTJSVariant** param,
                               iTJSDispatch2* objthis)
    {
        bool ok = false;
        try
        {
            if (numparams >= 2 && param[0]->Type() == tvtString)
            {
                const ttstr file = param[0]->GetString();

                // 先把数据拷到本地缓冲：ttstr 的 c_str() 在存储层做过名字
                // 归一化之后可能失效，直接拿指针写会写进别的字符串内容。
                std::vector<tjs_uint8> buf;
                bool haveData = false;
                if (param[1]->Type() == tvtOctet)
                {
                    if (tTJSVariantOctet* oct = param[1]->AsOctetNoAddRef())
                    {
                        buf.assign(oct->GetData(), oct->GetData() + oct->GetLength());
                        haveData = true;
                    }
                }
                else if (param[1]->Type() == tvtString)
                {
                    const ttstr text = param[1]->GetString();
                    const char* p = text.c_str();
                    buf.assign((const tjs_uint8*)p, (const tjs_uint8*)p + text.GetLen());
                    haveData = true;
                }

                // 其它类型（例如 saveDataPack 的返回值）什么都不写，
                // 否则会把已经写好的 .ksd 截断成空文件
                if (haveData)
                {
                    tTJSBinaryStream* stream = TVPCreateStream(file, TJS_BS_WRITE);
                    if (stream)
                    {
                        if (!buf.empty())
                            stream->WriteBuffer(buf.data(), (tjs_uint)buf.size());
                        delete stream;
                        ok = true;
                    }
                }
            }
        }
        catch (...)
        {
            ok = false;
        }

        if (result)
            *result = (tjs_int)(ok ? 1 : 0);
        return TJS_S_OK;
    }

    static tjs_error loadOctet(tTJSVariant* result, tjs_int numparams, tTJSVariant** param,
                               iTJSDispatch2* objthis)
    {
        try
        {
            if (numparams >= 1 && param[0]->Type() == tvtString)
            {
                const ttstr file = param[0]->GetString();
                tTJSBinaryStream* stream = TVPCreateStream(file, TJS_BS_READ);
                if (stream)
                {
                    std::vector<tjs_uint8> buf;
                    try
                    {
                        const tjs_uint64 size = stream->GetSize();
                        if (size > 0 && size < 256u * 1024u * 1024u)
                        {
                            buf.resize((size_t)size);
                            stream->ReadBuffer(buf.data(), (tjs_uint)size);
                        }
                    }
                    catch (...)
                    {
                        buf.clear();
                    }
                    delete stream;

                    if (!buf.empty())
                    {
                        tTJSVariantOctet* oct =
                            TJSAllocVariantOctet(buf.data(), (tjs_uint)buf.size());
                        if (oct)
                        {
                            if (result)
                                *result = oct;
                            oct->Release();
                            return TJS_S_OK;
                        }
                    }
                }
            }
        }
        catch (...)
        {
        }

        if (result)
            result->Clear();
        return TJS_S_OK;
    }
};

NCB_ATTACH_CLASS(StorageOctet, Storages)
{
    RawCallback(TJS_N("saveOctet"), &StorageOctet::saveOctet, TJS_STATICMEMBER);
    RawCallback(TJS_N("loadOctet"), &StorageOctet::loadOctet, TJS_STATICMEMBER);
};

NCB_POST_REGIST_CALLBACK(InstallLoadDataPackMember);

// saveDataPack 是普通的原生方法：游戏只调用、不覆盖
NCB_ATTACH_CLASS(PbdDataPack, Scripts)
{
    RawCallback(TJS_N("saveDataPack"), &PbdDataPack::saveDataPack, TJS_STATICMEMBER);
    RawCallback(TJS_N("makeDataPackDigest"), &PbdDataPack::makeDataPackDigest, TJS_STATICMEMBER);
    RawCallback(TJS_N("makeDataPackThumb"), &PbdDataPack::makeDataPackThumb, TJS_STATICMEMBER);
};

NCB_PRE_REGIST_CALLBACK(InitPlugin_PackinOne);
