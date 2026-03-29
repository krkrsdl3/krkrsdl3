#include "tjsCommHead.h"
#include "simd_image.h"
#include <cstring>
#include <algorithm>
#include <vector>

namespace krkrsimd
{
void boxFilter_8UC4(const uint8_t* src,
    int src_w,
    int src_h,
    int src_stride,
    uint8_t* dst,
    int dst_w,
    int dst_h,
    int dst_stride,
    int kx,
    int ky)
{
    // Integral image based box filter (scalar, no SIMD dependency)
    std::vector<uint32_t> integral((src_h + 1) * (src_w + 1) * 4, 0);
    for (int y = 0; y < src_h; y++)
    {
        const uint8_t* row = src + y * src_stride;
        uint32_t* int_row = integral.data() + (y + 1) * (src_w + 1) * 4;
        uint32_t* int_prev = integral.data() + y * (src_w + 1) * 4;
        uint32_t sum[4] = {0};
        for (int x = 0; x < src_w; x++)
        {
            for (int c = 0; c < 4; c++)
                sum[c] += row[x * 4 + c];
            for (int c = 0; c < 4; c++)
                int_row[(x + 1) * 4 + c] = int_prev[(x + 1) * 4 + c] + sum[c];
        }
    }

    int hk = kx / 2, vk = ky / 2;
    for (int y = 0; y < dst_h; y++)
    {
        uint8_t* dst_row = dst + y * dst_stride;
        for (int x = 0; x < dst_w; x++)
        {
            int x1 = std::max(0, x - hk), x2 = std::min(src_w, x + hk + 1);
            int y1 = std::max(0, y - vk), y2 = std::min(src_h, y + vk + 1);
            uint32_t* tl = integral.data() + y1 * (src_w + 1) * 4 + x1 * 4;
            uint32_t* tr = integral.data() + y1 * (src_w + 1) * 4 + x2 * 4;
            uint32_t* bl = integral.data() + y2 * (src_w + 1) * 4 + x1 * 4;
            uint32_t* br = integral.data() + y2 * (src_w + 1) * 4 + x2 * 4;
            int area = (x2 - x1) * (y2 - y1);
            for (int c = 0; c < 4; c++)
            {
                uint32_t s = br[c] - tr[c] - bl[c] + tl[c];
                dst_row[x * 4 + c] = (uint8_t)(s / area);
            }
        }
    }
}
}