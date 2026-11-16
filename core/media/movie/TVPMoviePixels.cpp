#include "tjsCommHead.h"

#include "TVPMoviePixels.h"

#include <atomic>
#include <cstddef>
#include <cstring>

namespace krkrsdl3
{

namespace
{

std::atomic<tTVPFrameConverter> gConverter{nullptr};

// BT.601 有限范围转全范围 RGBA 的定点系数。
// 用整数运算，避免在没有 sws 的构建里引入浮点开销。
inline uint8_t Clamp8(int value)
{
    return static_cast<uint8_t>(value < 0 ? 0 : (value > 255 ? 255 : value));
}

struct tYuvCoeffs
{
    int yOffset;
    int rv;
    int gu;
    int gv;
    int bu;
    int shift;
};

// BT.601 limited range: Y' = 16..235, C = 16..240
constexpr tYuvCoeffs kBT601 = {16, 359, -88, -183, 454, 8};

inline void StoreRGB(uint8_t* dst,
                     bool packed,
                     const tTVPPixelFormat format,
                     uint8_t r,
                     uint8_t g,
                     uint8_t b)
{
    switch (format)
    {
        case tTVPPixelFormat::RGBA:
            dst[0] = r;
            dst[1] = g;
            dst[2] = b;
            dst[3] = 255;
            break;
        case tTVPPixelFormat::BGRA:
            dst[0] = b;
            dst[1] = g;
            dst[2] = r;
            dst[3] = 255;
            break;
        case tTVPPixelFormat::ARGB:
            dst[0] = 255;
            dst[1] = r;
            dst[2] = g;
            dst[3] = b;
            break;
        case tTVPPixelFormat::RGB24:
            dst[0] = r;
            dst[1] = g;
            dst[2] = b;
            break;
        case tTVPPixelFormat::BGR24:
            dst[0] = b;
            dst[1] = g;
            dst[2] = r;
            break;
        default:
            (void)packed;
            break;
    }
}

// 平面 / 半平面 YUV → RGBA
bool ConvertYuvToRGBA(const tTVPMovieFrame& frame, uint8_t* dst, int dstPitch)
{
    const int width = frame.width;
    const int height = frame.height;
    if (width <= 0 || height <= 0 || frame.planes[0] == nullptr)
        return false;

    const uint8_t* yPlane = frame.planes[0];
    const int yStride = frame.strides[0];

    const uint8_t* uPlane = frame.planes[1];
    const uint8_t* vPlane = frame.planes[2];
    int uStride = frame.strides[1];
    int vStride = frame.strides[2];

    // 半平面格式只有两个平面，UV 交错存放。
    const bool semiPlanar =
        frame.format == tTVPPixelFormat::NV12 || frame.format == tTVPPixelFormat::NV21;

    int chromaShiftX = 0;
    int chromaShiftY = 0;
    switch (frame.format)
    {
        case tTVPPixelFormat::YUV420P:
        case tTVPPixelFormat::NV12:
        case tTVPPixelFormat::NV21:
            chromaShiftX = 1;
            chromaShiftY = 1;
            break;
        case tTVPPixelFormat::YUV422P:
            chromaShiftX = 1;
            chromaShiftY = 0;
            break;
        default:
            break;
    }

    if (semiPlanar)
    {
        if (uPlane == nullptr)
            return false;
        uStride = frame.strides[1];
        vStride = frame.strides[1];
    }
    else
    {
        if (uPlane == nullptr || vPlane == nullptr)
            return false;
    }

    for (int y = 0; y < height; ++y)
    {
        const uint8_t* yRow = yPlane + static_cast<ptrdiff_t>(y) * yStride;
        const int chromaY = y >> chromaShiftY;
        uint8_t* dstRow = dst + static_cast<ptrdiff_t>(y) * dstPitch;

        for (int x = 0; x < width; ++x)
        {
            const int chromaX = x >> chromaShiftX;
            int uValue;
            int vValue;

            if (semiPlanar)
            {
                const uint8_t* chroma =
                    uPlane + static_cast<ptrdiff_t>(chromaY) * uStride + chromaX * 2;
                if (frame.format == tTVPPixelFormat::NV12)
                {
                    uValue = chroma[0];
                    vValue = chroma[1];
                }
                else
                {
                    vValue = chroma[0];
                    uValue = chroma[1];
                }
            }
            else
            {
                uValue = uPlane[static_cast<ptrdiff_t>(chromaY) * uStride + chromaX];
                vValue = vPlane[static_cast<ptrdiff_t>(chromaY) * vStride + chromaX];
            }

            const int c = yRow[x] - kBT601.yOffset;
            const int d = uValue - 128;
            const int e = vValue - 128;

            const int r = (c * 298 + e * kBT601.rv + 128) >> kBT601.shift;
            const int g = (c * 298 + d * kBT601.gu + e * kBT601.gv + 128) >> kBT601.shift;
            const int b = (c * 298 + d * kBT601.bu + 128) >> kBT601.shift;

            StoreRGB(dstRow + x * 4, true, tTVPPixelFormat::RGBA, Clamp8(r), Clamp8(g),
                     Clamp8(b));
        }
    }
    return true;
}

// 打包格式 → RGBA（必要时换通道序）
bool ConvertPackedToRGBA(const tTVPMovieFrame& frame, uint8_t* dst, int dstPitch)
{
    const int width = frame.width;
    const int height = frame.height;
    const int srcBytes = TVPPixelBytes(frame.format);
    if (width <= 0 || height <= 0 || srcBytes <= 0 || frame.planes[0] == nullptr)
        return false;

    const uint8_t* src = frame.planes[0];
    const int srcStride = frame.strides[0];

    if (frame.format == tTVPPixelFormat::RGBA && srcStride == dstPitch)
    {
        memcpy(dst, src, static_cast<size_t>(dstPitch) * height);
        return true;
    }

    for (int y = 0; y < height; ++y)
    {
        const uint8_t* srcRow = src + static_cast<ptrdiff_t>(y) * srcStride;
        uint8_t* dstRow = dst + static_cast<ptrdiff_t>(y) * dstPitch;

        switch (frame.format)
        {
            case tTVPPixelFormat::RGBA:
                memcpy(dstRow, srcRow, static_cast<size_t>(width) * 4);
                break;
            case tTVPPixelFormat::BGRA:
                for (int x = 0; x < width; ++x)
                {
                    const uint8_t* p = srcRow + x * 4;
                    StoreRGB(dstRow + x * 4, true, tTVPPixelFormat::RGBA, p[2], p[1], p[0]);
                }
                break;
            case tTVPPixelFormat::ARGB:
                for (int x = 0; x < width; ++x)
                {
                    const uint8_t* p = srcRow + x * 4;
                    StoreRGB(dstRow + x * 4, true, tTVPPixelFormat::RGBA, p[1], p[2], p[3]);
                }
                break;
            case tTVPPixelFormat::RGB24:
                for (int x = 0; x < width; ++x)
                {
                    const uint8_t* p = srcRow + x * 3;
                    StoreRGB(dstRow + x * 4, true, tTVPPixelFormat::RGBA, p[0], p[1], p[2]);
                }
                break;
            case tTVPPixelFormat::BGR24:
                for (int x = 0; x < width; ++x)
                {
                    const uint8_t* p = srcRow + x * 3;
                    StoreRGB(dstRow + x * 4, true, tTVPPixelFormat::RGBA, p[2], p[1], p[0]);
                }
                break;
            case tTVPPixelFormat::Gray8:
                for (int x = 0; x < width; ++x)
                {
                    const uint8_t gray = srcRow[x];
                    StoreRGB(dstRow + x * 4, true, tTVPPixelFormat::RGBA, gray, gray, gray);
                }
                break;
            default:
                return false;
        }
    }
    return true;
}

} // namespace

bool TVPConvertFrameToRGBA(const tTVPMovieFrame& frame, uint8_t* dst, int dstPitch)
{
    if (frame.kind != tTVPMovieFrame::tKind::Video || dst == nullptr || dstPitch <= 0)
        return false;

    if (TVPIsPackedPixelFormat(frame.format))
        return ConvertPackedToRGBA(frame, dst, dstPitch);

    // 平面 YUV：优先用后端注入的加速实现。
    if (tTVPFrameConverter converter = gConverter.load(std::memory_order_acquire))
    {
        if (converter(frame, dst, dstPitch))
            return true;
    }

    if (TVPIsYuvPixelFormat(frame.format))
        return ConvertYuvToRGBA(frame, dst, dstPitch);

    return false;
}

void TVPSetFrameConverter(tTVPFrameConverter converter)
{
    gConverter.store(converter, std::memory_order_release);
}

} // namespace krkrsdl3
