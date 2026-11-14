#include "SWRenderBackend.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

#include "PlatformView.h"

//---------------------------------------------------------------------------
// 软件渲染后端（合并实现）
//
// 一个类同时承担两个角色（同一接口内，见 RenderBackend.h）：
//   1. 窗口贴图合成后端——窗口贴图使用 SDL_Texture，
//      经平台钩子 TVPCreateTextureBackend / TVPUpdateTextureBackend /
//      TVPDestroyTextureBackend / TVPRenderTextureBackend / TVPRenderClearBackend /
//      TVPRenderPresentBackend 完成（与迁移前 sdl3_entry.cpp
//      中的 SW 路径行为一致）。
//   2. 2D 网格渲染器——一般贴图使用 CPU buffer，
//      由 cpp/plugins/emoteplayer/emoterunner.cpp 的 drawSoftware 迁移而来
//      （edge function + 仿射纹理步进，混合公式保持原实现语义）。
//---------------------------------------------------------------------------
namespace krkrsdl3
{
namespace
{
struct ColorRGBA
{
    uint8_t r, g, b, a;
};

// Preserve the division's float result for all byte values; a reciprocal
// multiplication can round differently. The table is portable and read-only.
static constexpr auto NormalizedChannels = []() constexpr
{
    std::array<float, 256> values{};
    for (size_t i = 0; i < values.size(); ++i)
        values[i] = (float)i / 255.0f;
    return values;
}();

static inline uint8_t Clampf(float v)
{
    if (v < 0)
        return 0;
    if (v > 255)
        return 255;
    return (uint8_t)v;
}

// Opacity is invalidated by writable locks and every render operation.
static bool IsOpaqueBuffer(const std::vector<uint8_t>& pixels)
{
    for (size_t i = 3; i < pixels.size(); i += 4)
        if (pixels[i] != 255)
            return false;
    return true;
}

// Like InternalAffineBlt, clip once per scanline before transferring a span.
// Integer pixel vertices within this range keep the old float edge stepping
// exact, including its inclusive shared edges. Other geometry retains that
// stepping rather than changing its coverage/texture rounding.
static bool IsIntegralVertex(float value)
{
    return value >= 0.0f && value <= 2048.0f && value == std::floor(value);
}

static void ClipIntegralEdge(
    int64_t slope, int64_t value, int origin, int64_t& first, int64_t& last)
{
    if (slope > 0)
    {
        const int64_t numerator = -value;
        const int64_t offset = numerator / slope + (numerator % slope > 0);
        first = std::max(first, origin + offset);
    }
    else if (slope < 0)
    {
        const int64_t denominator = -slope;
        const int64_t offset = value / denominator - (value % denominator < 0);
        last = std::min(last, origin + offset);
    }
    else if (value < 0)
        first = last + 1;
}

static void ClipFloatEdge(
    float slope, float value, int origin, int length, int64_t& first, int64_t& last)
{
    if (slope == 0.0f || (slope > 0.0f && value >= 0.0f))
    {
        if (value < 0.0f)
            first = last + 1;
        return;
    }
    if (slope < 0.0f && value < 0.0f)
    {
        first = last + 1;
        return;
    }
    // Adding a constant is monotonic. A conservative rounding-error bound lets
    // us accept/reject an entire edge without walking it. Near its crossing we
    // use the original repeated float additions, so fractional coverage stays
    // byte-identical too; there is no fixed-point conversion of the geometry.
    const double n = length - 1;
    const double nu = n * std::numeric_limits<float>::epsilon();
    if (nu < 0.5)
    {
        const double end = (double)value + n * slope;
        const double error =
            nu / (1.0 - nu) * (std::abs((double)value) + n * std::abs((double)slope)) +
            n * std::numeric_limits<float>::min();
        if (slope < 0.0f && end > error)
            return;
        if (slope > 0.0f && end < -error)
        {
            first = last + 1;
            return;
        }
    }
    int offset = 0;
    if (slope > 0.0f)
    {
        while (offset < length && value < 0.0f)
        {
            value += slope;
            ++offset;
        }
        first = std::max(first, (int64_t)origin + offset);
    }
    else
    {
        while (offset < length && value >= 0.0f)
        {
            value += slope;
            ++offset;
        }
        last = std::min(last, (int64_t)origin + offset - 1);
    }
}

// 与迁移前 emoteplayer 的 blendPixels 语义一致
static ColorRGBA BlendPixels(
    ColorRGBA src, ColorRGBA dst, int mode, float opa, ColorRGBA uniformColor = {0, 0, 0, 0})
{
    float sa = NormalizedChannels[src.a] * opa;
    float da = NormalizedChannels[dst.a];

    if (sa <= 0.001f)
        return dst;
    // The default straight-alpha modes also reduce exactly to a source copy
    // for an opaque source at full opacity. Avoid their per-channel divisions.
    if ((mode == 0 && sa >= 0.999f) || (src.a == 255 && opa == 1.0f && mode != 1 && mode != 3 &&
                                        mode != 4 && mode != 6 && mode != 21))
        return src;

    if (mode == 21)
    {
        src = uniformColor;
        src.a = (uint8_t)(uniformColor.a * opa);
        sa = NormalizedChannels[src.a];
    }
    float sr = NormalizedChannels[src.r], sg = NormalizedChannels[src.g],
          sb = NormalizedChannels[src.b];
    float dr = NormalizedChannels[dst.r], dg = NormalizedChannels[dst.g],
          db = NormalizedChannels[dst.b];
    float outR = 0, outG = 0, outB = 0, outA = 0;

    switch (mode)
    {
        case 1:
        case 4:
            outR = sr * dr + dr;
            outG = sg * dg + dg;
            outB = sb * db + db;
            outA = da * 1.0f;
            break;
        case 3:
            outR = sr * sa + dr * (1 - sa);
            outG = sg * sa + dg * (1 - sa);
            outB = sb * sa + db * (1 - sa);
            outA = std::max(sa, da);
            break;
        case 6:
            break;
        case 0:
        default:
            outA = sa + da * (1.0f - sa);
            if (outA > 0.001f)
            {
                outR = sr * sa + dr * da * (1.0f - sa);
                outG = sg * sa + dg * da * (1.0f - sa);
                outB = sb * sa + db * da * (1.0f - sa);
                // Opaque destinations commonly make the divisor exactly one.
                // Skip those divisions without using an approximate reciprocal.
                if (outA != 1.0f)
                {
                    outR /= outA;
                    outG /= outA;
                    outB /= outA;
                }
            }
            else
            {
                outR = sr;
                outG = sg;
                outB = sb;
            }
            outA = sa + da;
            break;
    }

    return {Clampf(outR * 255), Clampf(outG * 255), Clampf(outB * 255), Clampf(outA * 255)};
}

//---------------------------------------------------------------------------
// Layer 合成：软件 RenderManager（tvpgl.cpp bm* 方法）精确语义
//---------------------------------------------------------------------------
static inline uint8_t Sat8(int v)
{
    return v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)v);
}

// tvpgl 的 alpha 混合核心：dest = dest + (src - dest) * opa >> 8（opa 0..255）
static inline int Lerp8(int dst, int src, int opa)
{
    return dst + ((src - dst) * opa >> 8);
}

// 单像素混合。字节序与后端缓冲一致：0=R, 1=G, 2=B, 3=A。
// 公式与 tvpgl.cpp 的 _o 变体（RenderManager 实际使用的带 opacity 方法）一致。
static uint32_t LayerBlendPixel(int method, uint32_t src, uint32_t dst, int opa8, uint32_t color)
{
    int sr = (int)(src & 0xFF), sg = (int)((src >> 8) & 0xFF), sb = (int)((src >> 16) & 0xFF),
        sa = (int)((src >> 24) & 0xFF);
    int dr = (int)(dst & 0xFF), dg = (int)((dst >> 8) & 0xFF), db = (int)((dst >> 16) & 0xFF),
        da = (int)((dst >> 24) & 0xFF);

    switch (method)
    {
        case iTVPRenderBackend::LBM_COPY:
            return src;
        case iTVPRenderBackend::LBM_ALPHA:
        {
            int sopa = (sa * opa8) >> 8;
            return (uint32_t)Lerp8(dr, sr, sopa) | (uint32_t)Lerp8(dg, sg, sopa) << 8 |
                   (uint32_t)Lerp8(db, sb, sopa) << 16 | (uint32_t)Lerp8(da, sa, sopa) << 24;
        }
        case iTVPRenderBackend::LBM_CONSTALPHA:
            return (uint32_t)Lerp8(dr, sr, opa8) | (uint32_t)Lerp8(dg, sg, opa8) << 8 |
                   (uint32_t)Lerp8(db, sb, opa8) << 16 | (uint32_t)Lerp8(da, sa, opa8) << 24;
        case iTVPRenderBackend::LBM_ADD:
        {
            // 软件 AddBlend_o：仅 RGB 按 opa8 缩放后饱和加，alpha 不变
            int s_r = (sr * opa8) >> 8, s_g = (sg * opa8) >> 8, s_b = (sb * opa8) >> 8;
            return (uint32_t)Sat8(dr + s_r) | (uint32_t)Sat8(dg + s_g) << 8 |
                   (uint32_t)Sat8(db + s_b) << 16 | (uint32_t)da << 24;
        }
        case iTVPRenderBackend::LBM_SUB:
        {
            // 软件 SubBlend_o：s.RGB = 255-(255-src.RGB)*opa8>>8，s.A = src.A（不缩放）
            int s_r = 255 - ((255 - sr) * opa8 >> 8), s_g = 255 - ((255 - sg) * opa8 >> 8),
                s_b = 255 - ((255 - sb) * opa8 >> 8);
            return (uint32_t)Sat8(dr - s_r) | (uint32_t)Sat8(dg - s_g) << 8 |
                   (uint32_t)Sat8(db - s_b) << 16 | (uint32_t)Sat8(da - sa) << 24;
        }
        case iTVPRenderBackend::LBM_MUL:
        case iTVPRenderBackend::LBM_MUL_HDA:
        {
            int s_r = 255 - ((255 - sr) * opa8 >> 8), s_g = 255 - ((255 - sg) * opa8 >> 8),
                s_b = 255 - ((255 - sb) * opa8 >> 8);
            uint32_t rgb = (uint32_t)((dr * s_r) >> 8) | (uint32_t)((dg * s_g) >> 8) << 8 |
                           (uint32_t)((db * s_b) >> 8) << 16;
            if (method == iTVPRenderBackend::LBM_MUL_HDA)
                return rgb | (uint32_t)da << 24; // MulBlend_HDA：alpha 保留
            return rgb;                          // MulBlend：alpha 清 0
        }
        case iTVPRenderBackend::LBM_FILL:
            return color;
        case iTVPRenderBackend::LBM_COPYCOLOR:
            return (dst & 0xff000000) | (src & 0x00ffffff); // CopyColor：RGB 复制，alpha 保留
        case iTVPRenderBackend::LBM_COPYOPAQUE:
            return src | 0xff000000; // CopyOpaqueImage：RGB 复制，alpha 置 255
        case iTVPRenderBackend::LBM_COPYMASK:
            return (dst & 0x00ffffff) | (src & 0xff000000); // CopyMask：alpha 复制
        default:
            return src;
    }
}
} // namespace

//---------------------------------------------------------------------------
// 窗口贴图（iTVPRenderBackend）：SDL_Texture，经平台钩子
//---------------------------------------------------------------------------
void SWRenderBackend::BeginFrame(int, int)
{
    TVPRenderClearBackend();
}

void SWRenderBackend::EndFrame()
{
    TVPRenderPresentBackend();
}

void* SWRenderBackend::CreateWindowTexture(int width, int height)
{
    TVPSprite sp;
    sp.width = width;
    sp.height = height;
    TVPCreateTextureBackend(sp);
    return sp.texture;
}

void SWRenderBackend::UpdateWindowTexture(void* handle, const uint8_t* buff, int width, int height, int pitch)
{
    TVPSprite sp;
    sp.texture = handle;
    TVPUpdateTextureBackend(&sp, (uint8_t*)buff, width, height, pitch);
}

void SWRenderBackend::DestroyWindowTexture(void* handle)
{
    TVPSprite sp;
    sp.texture = handle;
    TVPDestroyTextureBackend(&sp);
}

void SWRenderBackend::DrawWindowTexture(void* handle, float posX, float posY, float width, float height)
{
    TVPSprite sp;
    sp.texture = handle;
    TVPRenderTextureBackend(&sp, (int)posX, (int)posY, (int)width, (int)height);
}

//---------------------------------------------------------------------------
// 目标（CPU buffer）
//---------------------------------------------------------------------------
SWRenderBackend::~SWRenderBackend()
{
    for (Target* t : targets_)
        delete t;
    targets_.clear();
    for (Texture* t : textures_)
        delete t;
    textures_.clear();
}

SWRenderBackend::Target* SWRenderBackend::FindTarget(void* handle) const
{
    for (Target* t : targets_)
    {
        if (t == handle)
            return t;
    }
    return nullptr;
}

SWRenderBackend::Texture* SWRenderBackend::FindTexture(void* handle) const
{
    for (Texture* t : textures_)
    {
        if (t == handle)
            return t;
    }
    return nullptr;
}

void* SWRenderBackend::CreateTarget(int width, int height)
{
    if (width <= 0 || height <= 0)
        return nullptr;
    Target* target = new Target();
    target->width = width;
    target->height = height;
    target->pixels.resize((size_t)width * height * 4, 0);
    targets_.push_back(target);
    return target;
}

void SWRenderBackend::DestroyTarget(void* handle)
{
    Target* target = FindTarget(handle);
    if (!target)
        return;
    if (currentTarget_ == target)
        currentTarget_ = nullptr;
    if (maskTarget_ == target)
        maskTarget_ = nullptr;
    for (size_t i = 0; i < targets_.size(); i++)
    {
        if (targets_[i] == target)
        {
            targets_.erase(targets_.begin() + i);
            break;
        }
    }
    delete target;
}

void SWRenderBackend::SetTarget(void* handle)
{
    currentTarget_ = FindTarget(handle);
}

void SWRenderBackend::ClearTarget(bool clearColor)
{
    // 软件后端无深度缓冲：仅 clearColor=true 时清屏
    if (!currentTarget_ || !clearColor)
        return;
    currentTarget_->opaque = false;
    std::memset(currentTarget_->pixels.data(), 0, currentTarget_->pixels.size());
}

uint8_t* SWRenderBackend::LockTarget(void* handle, int& pitch)
{
    Target* target = FindTarget(handle);
    if (!target)
        return nullptr;
    target->opaque = false;
    pitch = target->width * 4;
    return target->pixels.data();
}

void SWRenderBackend::UnlockTarget(void* handle)
{
    (void)handle;
}

void* SWRenderBackend::GetTargetTexture(void* handle)
{
    // 软渲染目标即 CPU 缓冲；返回目标自身，DrawMesh 支持目标句柄作为纹理
    return FindTarget(handle) ? handle : nullptr;
}

void SWRenderBackend::UpdateTargetTexture(void* handle,
                                          const uint8_t* pixels,
                                          int width,
                                          int height,
                                          int pitch)
{
    Target* target = FindTarget(handle);
    if (!target || !pixels)
        return;
    for (int y = 0; y < height; y++)
    {
        std::memcpy(target->pixels.data() + (size_t)y * target->width * 4,
                    pixels + (size_t)y * pitch, (size_t)width * 4);
    }
    target->opaque = IsOpaqueBuffer(target->pixels);
}

//---------------------------------------------------------------------------
// 一般贴图：内部持有 CPU 像素副本
//---------------------------------------------------------------------------
void* SWRenderBackend::CreateTexture(int width, int height)
{
    if (width <= 0 || height <= 0)
        return nullptr;
    Texture* texture = new Texture();
    texture->width = width;
    texture->height = height;
    texture->pixels.resize((size_t)width * height * 4, 0);
    textures_.push_back(texture);
    return texture;
}

void SWRenderBackend::UpdateTexture(void* handle, const uint8_t* pixels, int width, int height, int pitch)
{
    Texture* texture = FindTexture(handle);
    if (!texture || !pixels)
        return;
    // 按行拷贝（源 pitch 可能含对齐填充）
    for (int y = 0; y < height; y++)
    {
        std::memcpy(texture->pixels.data() + (size_t)y * texture->width * 4,
                    pixels + (size_t)y * pitch, (size_t)width * 4);
    }
    texture->opaque = IsOpaqueBuffer(texture->pixels);
}

uint8_t* SWRenderBackend::LockTexture(void* handle, int& pitch)
{
    Texture* texture = FindTexture(handle);
    if (!texture)
        return nullptr;
    texture->opaque = false;
    pitch = texture->width * 4;
    return texture->pixels.data();
}

void SWRenderBackend::DestroyTexture(void* handle)
{
    Texture* texture = FindTexture(handle);
    if (!texture)
        return;
    for (size_t i = 0; i < textures_.size(); i++)
    {
        if (textures_[i] == texture)
        {
            textures_.erase(textures_.begin() + i);
            break;
        }
    }
    delete texture;
}

//---------------------------------------------------------------------------
// 绘制状态
//---------------------------------------------------------------------------
void SWRenderBackend::SetMask(void* handle)
{
    maskTarget_ = FindTarget(handle);
}

void SWRenderBackend::SetBlendMode(int mode, const float* uniformColor)
{
    blendMode_ = mode;
    skipDraw_ = (mode == 6);
    if (mode == 21 && uniformColor)
    {
        uniformColor_[0] = uniformColor[0];
        uniformColor_[1] = uniformColor[1];
        uniformColor_[2] = uniformColor[2];
        uniformColor_[3] = uniformColor[3];
    }
}

void SWRenderBackend::DrawMesh(const float* vertices,
                               int vertexCount,
                               const uint16_t* indices,
                               int indexCount,
                               void* handle,
                               float opacity,
                               const float* colorModulation)
{
    if (skipDraw_ || !currentTarget_ || !vertices || !indices || vertexCount <= 0 || indexCount <= 0)
        return;
    // 纹理句柄可以是普通纹理，也可以是离屏目标（其缓冲直接作为采样源）
    Texture* texture = FindTexture(handle);
    Target* targetAsTexture = texture ? nullptr : FindTarget(handle);
    if (!texture && !targetAsTexture)
        return;

    const int width = currentTarget_->width;
    const int height = currentTarget_->height;
    uint32_t* dst = (uint32_t*)currentTarget_->pixels.data();
    const uint32_t* maskRowBase = maskTarget_ ? (uint32_t*)maskTarget_->pixels.data() : nullptr;
    const ColorRGBA* texData = (const ColorRGBA*)(texture ? texture->pixels.data()
                                                          : targetAsTexture->pixels.data());
    const int texW = texture ? texture->width : targetAsTexture->width;
    const int texH = texture ? texture->height : targetAsTexture->height;
    if (!texData)
        return;
    const bool sourceOpaque = texture ? texture->opaque : targetAsTexture->opaque;
    currentTarget_->opaque = false;
    const bool hasStencil = (maskRowBase != nullptr);
    const int pitch = width;

    ColorRGBA uniformColor = {(uint8_t)(uniformColor_[0] * 255), (uint8_t)(uniformColor_[1] * 255),
                              (uint8_t)(uniformColor_[2] * 255), (uint8_t)(uniformColor_[3] * 255)};
    const float whiteModulation[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    const float* modulation = colorModulation ? colorModulation : whiteModulation;
    const bool modulated = modulation[0] != 1.0f || modulation[1] != 1.0f ||
                           modulation[2] != 1.0f || modulation[3] != 1.0f;
    const int blendMode = blendMode_;

    for (int idx = 0; idx + 2 < indexCount; idx += 3)
    {
        uint16_t i0 = indices[idx];
        uint16_t i1 = indices[idx + 1];
        uint16_t i2 = indices[idx + 2];
        if (i0 >= (uint16_t)vertexCount || i1 >= (uint16_t)vertexCount ||
            i2 >= (uint16_t)vertexCount)
            continue;
        const float* v0 = vertices + (size_t)i0 * 4;
        const float* v1 = vertices + (size_t)i1 * 4;
        const float* v2 = vertices + (size_t)i2 * 4;
        float x0 = (v0[0] + 1.0f) * 0.5f * width;
        float y0 = (v0[1] + 1.0f) * 0.5f * height;
        float x1 = (v1[0] + 1.0f) * 0.5f * width;
        float y1 = (v1[1] + 1.0f) * 0.5f * height;
        float x2 = (v2[0] + 1.0f) * 0.5f * width;
        float y2 = (v2[1] + 1.0f) * 0.5f * height;

        int minX = (int)std::max(0.0f, std::min(x0, std::min(x1, x2)));
        int maxX = (int)std::min((float)width - 1, std::max(x0, std::max(x1, x2)));
        int minY = (int)std::max(0.0f, std::min(y0, std::min(y1, y2)));
        int maxY = (int)std::min((float)height - 1, std::max(y0, std::max(y1, y2)));
        if (minX > maxX || minY > maxY)
            continue;

        // Edge function: f_ij(x,y) = a*x + b*y + c
        float a01 = y0 - y1, b01 = x1 - x0, c01 = x0 * y1 - x1 * y0;
        float a12 = y1 - y2, b12 = x2 - x1, c12 = x1 * y2 - x2 * y1;
        float a20 = y2 - y0, b20 = x0 - x2, c20 = x2 * y0 - x0 * y2;

        float area = a12 * x0 + b12 * y0 + c12; // = 2*有符号面积
        if (std::abs(area) < 1e-6f)
            continue;
        float invArea = 1.0f / area;
        bool ccw = area > 0;

        // 仿射纹理步进参数
        float A_u = v0[2] * a12 + v1[2] * a20 + v2[2] * a01;
        float B_u = v0[2] * b12 + v1[2] * b20 + v2[2] * b01;
        float A_v = v0[3] * a12 + v1[3] * a20 + v2[3] * a01;
        float B_v = v0[3] * b12 + v1[3] * b20 + v2[3] * b01;

        float tu0 = (A_u * minX + B_u * minY + (v0[2] * c12 + v1[2] * c20 + v2[2] * c01)) * invArea;
        float tv0 = (A_v * minX + B_v * minY + (v0[3] * c12 + v1[3] * c20 + v2[3] * c01)) * invArea;
        float du_dx = A_u * invArea, dv_dx = A_v * invArea;
        float du_dy = B_u * invArea, dv_dy = B_v * invArea;

        float f01 = a01 * minX + b01 * minY + c01;
        float f12 = a12 * minX + b12 * minY + c12;
        float f20 = a20 * minX + b20 * minY + c20;
        float df01_dy = b01, df12_dy = b12, df20_dy = b20;

        const int texW_1 = texW - 1, texH_1 = texH - 1;
        const bool integralEdges = IsIntegralVertex(x0) && IsIntegralVertex(y0) &&
                                   IsIntegralVertex(x1) && IsIntegralVertex(y1) &&
                                   IsIntegralVertex(x2) && IsIntegralVertex(y2);
        const bool separable = du_dy == 0.0f && dv_dx == 0.0f;
        struct CopyRun
        {
            int first, end, source;
        };
        std::vector<CopyRun> copyRuns;
        const bool copySpan = separable && sourceOpaque && !hasStencil && !modulated &&
                              opacity == 1.0f && blendMode != 1 && blendMode != 3 &&
                              blendMode != 4 && blendMode != 6 && blendMode != 21 &&
                              targetAsTexture != currentTarget_;
        if (separable)
        {
            sampleColumns_.resize(maxX - minX + 1);
            float tu = tu0;
            for (int px = minX; px <= maxX; ++px, tu += du_dx)
                sampleColumns_[px - minX] =
                    std::max(0, std::min(texW_1, (int)(tu * texW_1 + 0.5f)));
            if (copySpan)
                for (int first = minX; first <= maxX;)
                {
                    int end = first + 1;
                    while (end <= maxX &&
                           sampleColumns_[end - minX] == sampleColumns_[end - minX - 1] + 1)
                        ++end;
                    copyRuns.push_back({first, end, sampleColumns_[first - minX]});
                    if (copyRuns.size() > 32)
                        break;
                    first = end;
                }
        }

        for (int py = minY; py <= maxY; ++py)
        {
            int64_t first = minX, last = maxX;
            const int sign = ccw ? 1 : -1;
            if (integralEdges)
            {
                ClipIntegralEdge((int64_t)a01 * sign, (int64_t)f01 * sign, minX, first, last);
                ClipIntegralEdge((int64_t)a12 * sign, (int64_t)f12 * sign, minX, first, last);
                ClipIntegralEdge((int64_t)a20 * sign, (int64_t)f20 * sign, minX, first, last);
            }
            else
            {
                const int length = maxX - minX + 1;
                ClipFloatEdge(a01 * sign, f01 * sign, minX, length, first, last);
                ClipFloatEdge(a12 * sign, f12 * sign, minX, length, first, last);
                ClipFloatEdge(a20 * sign, f20 * sign, minX, length, first, last);
            }
            ColorRGBA* row = (ColorRGBA*)(dst + (size_t)py * pitch);
            const uint32_t* maskRow = maskRowBase ? maskRowBase + (size_t)py * width : nullptr;
            const int rowY = std::max(0, std::min(texH_1, (int)(tv0 * texH_1 + 0.5f)));
            if (copySpan)
            {
                const ColorRGBA* sourceRow = texData + (size_t)rowY * texW;
                // Decimated/reversed mappings may contain many one-pixel runs.
                // Gather those directly instead of issuing tiny memcpy calls.
                if (copyRuns.size() > 32)
                {
                    for (int64_t px = first; px <= last; ++px)
                        row[px] = sourceRow[sampleColumns_[px - minX]];
                }
                else
                    for (const auto& run : copyRuns)
                    {
                        const int64_t begin = std::max(first, (int64_t)run.first);
                        const int64_t end = std::min(last + 1, (int64_t)run.end);
                        if (begin < end)
                            std::memcpy(row + begin, sourceRow + run.source + begin - run.first,
                                        (size_t)(end - begin) * sizeof(ColorRGBA));
                    }
            }
            else
            {
                float tu_row = tu0, tv_row = tv0;
                // Preserve repeated-addition rounding when entering a clipped span.
                if (!separable && first <= last)
                    for (int px = minX; px < first; ++px)
                    {
                        tu_row += du_dx;
                        tv_row += dv_dx;
                    }
                for (int64_t px = first; px <= last; ++px)
                {
                    if (!hasStencil || ((maskRow[px] >> 24) & 0xFF) >= 128)
                    {
                        const int tx =
                            separable
                                ? sampleColumns_[px - minX]
                                : std::max(0, std::min(texW_1, (int)(tu_row * texW_1 + 0.5f)));
                        const int ty =
                            separable
                                ? rowY
                                : std::max(0, std::min(texH_1, (int)(tv_row * texH_1 + 0.5f)));
                        ColorRGBA src = texData[(size_t)ty * texW + tx];
                        if (modulated)
                        {
                            src.r = static_cast<uint8_t>(src.r * modulation[0] + 0.5f);
                            src.g = static_cast<uint8_t>(src.g * modulation[1] + 0.5f);
                            src.b = static_cast<uint8_t>(src.b * modulation[2] + 0.5f);
                            src.a = static_cast<uint8_t>(src.a * modulation[3] + 0.5f);
                        }
                        if (src.a != 0)
                            row[px] = BlendPixels(src, row[px], blendMode, opacity, uniformColor);
                    }
                    if (!separable)
                    {
                        tu_row += du_dx;
                        tv_row += dv_dx;
                    }
                }
            }
            f01 += df01_dy;
            f12 += df12_dy;
            f20 += df20_dy;
            tu0 += du_dy;
            tv0 += dv_dy;
        }
    }
}

//---------------------------------------------------------------------------
// Layer 合成（图层合成路径，软件 RenderManager 语义）
//---------------------------------------------------------------------------
void SWRenderBackend::LayerSetBlend(int method, float opacity, const float* uniformColor)
{
    layerMethod_ = method;
    // 软件方法 opacity 参数为 0..255：浮点 0..1 圆整回 0..255
    int opa8 = (int)(opacity * 255.0f + 0.5f);
    if (opa8 < 0)
        opa8 = 0;
    if (opa8 > 255)
        opa8 = 255;
    layerOpa8_ = opa8;
    if (uniformColor)
    {
        layerColor_[0] = uniformColor[0];
        layerColor_[1] = uniformColor[1];
        layerColor_[2] = uniformColor[2];
        layerColor_[3] = uniformColor[3];
    }
}

void SWRenderBackend::LayerDrawRect(
    void* handle, float x, float y, float w, float h, float u0, float v0, float u1, float v1)
{
    if (!currentTarget_ || w <= 0.0f || h <= 0.0f)
        return;
    // 纹理句柄可以是普通纹理，也可以是离屏目标（其缓冲直接作为采样源）
    Texture* texture = FindTexture(handle);
    Target* targetAsTexture = texture ? nullptr : FindTarget(handle);
    if (!texture && !targetAsTexture)
        return;

    const uint32_t* texData =
        (const uint32_t*)(texture ? texture->pixels.data() : targetAsTexture->pixels.data());
    const int texW = texture ? texture->width : targetAsTexture->width;
    const int texH = texture ? texture->height : targetAsTexture->height;
    if (!texData || texW <= 0 || texH <= 0)
        return;

    const int width = currentTarget_->width;
    const int height = currentTarget_->height;
    uint32_t* dst = (uint32_t*)currentTarget_->pixels.data();

    const bool sourceOpaque = texture ? texture->opaque : targetAsTexture->opaque;
    currentTarget_->opaque = false;

    // 目标矩形（与软件 RenderManager 的 OperateRect 一样逐像素处理）
    int x0 = (int)std::floor(x);
    int y0 = (int)std::floor(y);
    int x1 = (int)std::ceil(x + w);
    int y1 = (int)std::ceil(y + h);
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > width)
        x1 = width;
    if (y1 > height)
        y1 = height;
    if (x0 >= x1 || y0 >= y1)
        return;
    const bool fillsOpaqueTarget =
        x0 == 0 && y0 == 0 && x1 == width && y1 == height &&
        ((layerMethod_ == LBM_COPY && sourceOpaque) || layerMethod_ == LBM_COPYOPAQUE);

    // An unscaled copy maps each destination texel to the same source texel.
    // Keep self-blits on the scalar path, which has defined traversal behavior.
    if (layerMethod_ == LBM_COPY && targetAsTexture != currentTarget_ && w == texW && h == texH &&
        x == std::floor(x) && y == std::floor(y) && u0 == 0.0f && v0 == 0.0f && u1 == 1.0f &&
        v1 == 1.0f)
    {
        const int sourceX = x0 - (int)x, sourceY = y0 - (int)y;
        for (int py = y0; py < y1; ++py)
            std::memcpy(dst + (size_t)py * width + x0,
                        texData + (size_t)(sourceY + py - y0) * texW + sourceX,
                        (size_t)(x1 - x0) * sizeof(uint32_t));
        currentTarget_->opaque = fillsOpaqueTarget;
        return;
    }

    // 采样：texel 中心映射（与 DrawMesh 的 (int)(tu * texW_1 + 0.5f) 一致）
    const int texW_1 = texW - 1, texH_1 = texH - 1;
    // Horizontal sampling is independent of the destination row.
    sampleColumns_.resize(x1 - x0);
    for (int px = x0; px < x1; ++px)
    {
        const float tx = ((float)px - x + 0.5f) / w;
        const int sx = (int)((u0 + (u1 - u0) * tx) * texW_1 + 0.5f);
        sampleColumns_[px - x0] = std::max(0, std::min(texW_1, sx));
    }
    const uint32_t fillColor = ((uint32_t)(layerColor_[3] * 255.0f + 0.5f) << 24) |
                               ((uint32_t)(layerColor_[2] * 255.0f + 0.5f) << 16) |
                               ((uint32_t)(layerColor_[1] * 255.0f + 0.5f) << 8) |
                               ((uint32_t)(layerColor_[0] * 255.0f + 0.5f));

    for (int py = y0; py < y1; py++)
    {
        float ty = ((float)py - y + 0.5f) / h;
        int sy = (int)((v0 + (v1 - v0) * ty) * texH_1 + 0.5f);
        if (sy < 0)
            sy = 0;
        else if (sy > texH_1)
            sy = texH_1;
        uint32_t* row = dst + (size_t)py * width;
        const uint32_t* texRow = texData + (size_t)sy * texW;
        if (layerMethod_ == LBM_COPY)
        {
            for (int px = x0; px < x1; ++px)
                row[px] = texRow[sampleColumns_[px - x0]];
        }
        else
            for (int px = x0; px < x1; ++px)
                row[px] = LayerBlendPixel(layerMethod_, texRow[sampleColumns_[px - x0]], row[px],
                                          layerOpa8_, fillColor);
    }
    currentTarget_->opaque = fillsOpaqueTarget;
}
} // namespace krkrsdl3

//---------------------------------------------------------------------------
// 静态注册：合并后端（窗口合成 + 2D 网格）一次性注册
// （无后端时由插件经 TVPGetRenderBackend 的软件兜底实例提供 2D 渲染）
//---------------------------------------------------------------------------
namespace
{
struct SWRenderBackendAutoRegister
{
    SWRenderBackendAutoRegister()
    {
        krkrsdl3::TVPRenderBackendDesc desc;
        desc.name = "software";
        desc.description = "Software renderer (SDL Renderer)";
        desc.probe = TVPSoftwareRenderBackendAvailable;
        desc.create = nullptr;
        krkrsdl3::TVPRegisterRenderBackend(desc);
    }
} gSWRenderBackendAutoRegister;
} // namespace
