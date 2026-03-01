#include "tjsCommHead.h"
#include "simd_image.h"

/*
#include <hwy/highway.h>
#include <chrono>

namespace hn = hwy::HWY_NAMESPACE;

#include "opencv2/opencv.hpp"

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
    const hn::ScalableTag<uint8_t> du8;
    const hn::Repartition<uint16_t, decltype(du8)> du16;
    const size_t N = hn::Lanes(du8);

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
        for (int x = 0; x < dst_w; x += N / 4)
        {
            uint8_t out[16] = {0};
            for (int i = 0; i < N / 4 && x + i < dst_w; i++)
            {
                int x1 = std::max(0, x + i - hk), x2 = std::min(src_w, x + i + hk + 1);
                int y1 = std::max(0, y - vk), y2 = std::min(src_h, y + vk + 1);
                uint32_t* tl = integral.data() + y1 * (src_w + 1) * 4 + x1 * 4;
                uint32_t* tr = integral.data() + y1 * (src_w + 1) * 4 + x2 * 4;
                uint32_t* bl = integral.data() + y2 * (src_w + 1) * 4 + x1 * 4;
                uint32_t* br = integral.data() + y2 * (src_w + 1) * 4 + x2 * 4;
                for (int c = 0; c < 4; c++)
                {
                    uint32_t sum = br[c] - tr[c] - bl[c] + tl[c];
                    out[i * 4 + c] = sum / ((x2 - x1) * (y2 - y1));
                }
            }
            memcpy(dst_row + x * 4, out, std::min((size_t)(dst_w - x) * 4, N));
        }
    }
}
void resize_8UC4(const uint8_t* src,
                 int src_w,
                 int src_h,
                 int src_stride,
                 uint8_t* dst,
                 int dst_w,
                 int dst_h,
                 int dst_stride,
                 int interpolation)
{
    const hn::ScalableTag<uint8_t> du8;
    const hn::Repartition<float, decltype(du8)> df;
    const size_t N = hn::Lanes(df);
    float scale_x = (float)src_w / dst_w;
    float scale_y = (float)src_h / dst_h;
    for (int y = 0; y < dst_h; y++)
    {
        float src_y = y * scale_y;
        int y0 = (int)src_y;
        float wy = src_y - y0;
        uint8_t* dst_row = dst + y * dst_stride;
        for (int x = 0; x < dst_w; x += N / 4)
        {
            float vals[4][4] = {{0}};
            for (int i = 0; i < N / 4 && x + i < dst_w; i++)
            {
                float src_x = (x + i) * scale_x;
                int x0 = (int)src_x;
                float wx = src_x - x0;

                if (interpolation == 0)
                {
                    int sx = std::min(src_w - 1, (int)(src_x + 0.5f));
                    int sy = std::min(src_h - 1, (int)(src_y + 0.5f));
                    const uint8_t* src_pixel = src + sy * src_stride + sx * 4;
                    for (int c = 0; c < 4; c++)
                        vals[i][c] = src_pixel[c];
                }
                else
                {
                    int x1 = std::min(src_w - 1, x0);
                    int x2 = std::min(src_w - 1, x0 + 1);
                    int y1 = std::min(src_h - 1, y0);
                    int y2 = std::min(src_h - 1, y0 + 1);
                    const uint8_t* p11 = src + y1 * src_stride + x1 * 4;
                    const uint8_t* p12 = src + y1 * src_stride + x2 * 4;
                    const uint8_t* p21 = src + y2 * src_stride + x1 * 4;
                    const uint8_t* p22 = src + y2 * src_stride + x2 * 4;
                    for (int c = 0; c < 4; c++)
                    {
                        float v11 = p11[c], v12 = p12[c], v21 = p21[c], v22 = p22[c];
                        float v1 = v11 * (1 - wx) + v12 * wx;
                        float v2 = v21 * (1 - wx) + v22 * wx;
                        vals[i][c] = v1 * (1 - wy) + v2 * wy;
                    }
                }
            }
            for (int i = 0; i < N / 4 && x + i < dst_w; i++)
            {
                for (int c = 0; c < 4; c++)
                {
                    dst_row[(x + i) * 4 + c] = (uint8_t)vals[i][c];
                }
            }
        }
    }
}
void boxFilterResize_8UC4(const uint8_t* src,
                          int src_w,
                          int src_h,
                          int src_stride,
                          uint8_t* dst,
                          int dst_w,
                          int dst_h,
                          int dst_stride,
                          int kx,
                          int ky,
                          int interpolation)
{
    const hn::ScalableTag<uint8_t> du8;
    const hn::Repartition<float, decltype(du8)> df;
    const size_t N = hn::Lanes(df);
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
    float scale_x = (float)src_w / dst_w;
    float scale_y = (float)src_h / dst_h;
    int hk = kx / 2, vk = ky / 2;
    for (int dy = 0; dy < dst_h; dy++)
    {
        uint8_t* dst_row = dst + dy * dst_stride;
        float src_y = dy * scale_y;

        for (int dx = 0; dx < dst_w; dx += N / 4)
        {
            float vals[4][4] = {{0}};

            for (int i = 0; i < N / 4 && dx + i < dst_w; i++)
            {
                float src_x = (dx + i) * scale_x;

                if (interpolation == 0)
                {
                    int sx = std::min(src_w - 1, (int)(src_x + 0.5f));
                    int sy = std::min(src_h - 1, (int)(src_y + 0.5f));

                    int x1 = std::max(0, sx - hk), x2 = std::min(src_w, sx + hk + 1);
                    int y1 = std::max(0, sy - vk), y2 = std::min(src_h, sy + vk + 1);

                    uint32_t* tl = integral.data() + y1 * (src_w + 1) * 4 + x1 * 4;
                    uint32_t* tr = integral.data() + y1 * (src_w + 1) * 4 + x2 * 4;
                    uint32_t* bl = integral.data() + y2 * (src_w + 1) * 4 + x1 * 4;
                    uint32_t* br = integral.data() + y2 * (src_w + 1) * 4 + x2 * 4;

                    int area = (x2 - x1) * (y2 - y1);
                    for (int c = 0; c < 4; c++)
                    {
                        uint32_t sum = br[c] - tr[c] - bl[c] + tl[c];
                        vals[i][c] = (float)sum / area;
                    }
                }
                else
                {
                    int x0 = (int)src_x, y0 = (int)src_y;
                    float wx = src_x - x0, wy = src_y - y0;
                    int x1 = std::min(src_w - 1, x0), x2 = std::min(src_w - 1, x0 + 1);
                    int y1 = std::min(src_h - 1, y0), y2 = std::min(src_h - 1, y0 + 1);
                    float samples[4][4] = {{0}};
                    int points[4][2] = {{x1, y1}, {x2, y1}, {x1, y2}, {x2, y2}};
                    for (int p = 0; p < 4; p++)
                    {
                        int sx = points[p][0], sy = points[p][1];
                        int x1b = std::max(0, sx - hk), x2b = std::min(src_w, sx + hk + 1);
                        int y1b = std::max(0, sy - vk), y2b = std::min(src_h, sy + vk + 1);

                        uint32_t* tl = integral.data() + y1b * (src_w + 1) * 4 + x1b * 4;
                        uint32_t* tr = integral.data() + y1b * (src_w + 1) * 4 + x2b * 4;
                        uint32_t* bl = integral.data() + y2b * (src_w + 1) * 4 + x1b * 4;
                        uint32_t* br = integral.data() + y2b * (src_w + 1) * 4 + x2b * 4;

                        int area = (x2b - x1b) * (y2b - y1b);
                        for (int c = 0; c < 4; c++)
                        {
                            uint32_t sum = br[c] - tr[c] - bl[c] + tl[c];
                            samples[p][c] = (float)sum / area;
                        }
                    }

                    for (int c = 0; c < 4; c++)
                    {
                        float v1 = samples[0][c] * (1 - wx) + samples[1][c] * wx;
                        float v2 = samples[2][c] * (1 - wx) + samples[3][c] * wx;
                        vals[i][c] = v1 * (1 - wy) + v2 * wy;
                    }
                }
            }

            for (int i = 0; i < N / 4 && dx + i < dst_w; i++)
            {
                for (int c = 0; c < 4; c++)
                {
                    dst_row[(dx + i) * 4 + c] =
                        (uint8_t)std::min(255.0f, std::max(0.0f, vals[i][c]));
                }
            }
        }
    }
}
}

void test_simd()
{
    cv::Mat img = cv::imread("C:\\Users\\Administrator\\Pictures\\img68.jpg", cv::IMREAD_UNCHANGED);
    cv::imshow(std::string("before"), img);
    cv::Mat src;
    if (img.channels() == 3)
    {
        cv::cvtColor(img, src, cv::COLOR_BGR2RGBA);
    }
    else if (img.channels() == 4)
    {
        cv::cvtColor(img, src, cv::COLOR_BGRA2RGBA);
    }
    else
    {
        throw;
    }
    cv::Mat dst(720, 1280, CV_8UC4);
    int kx = 15, ky = 15; // 核大小，可调整
    

    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < 1; i++)
    {
        cv::boxFilter(src, dst, -1, cv::Size(kx, ky));
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto t1 = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < 1; i++)
    {
        krkrsimd::boxFilterResize_8UC4(src.data, src.cols, src.rows, (int)src.step, dst.data,
                                       dst.cols, dst.rows, (int)dst.step, kx, ky, 0);
    }
    end = std::chrono::high_resolution_clock::now();
    auto t2 = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    std::cout << "OpenCV: " << t1 / 1000.0 << " ms, krkrsimd: " << t2 / 1000.0 << " ms, "
              << "krkrsimd is " << (float)t1 / t2 << "x faster" << std::endl;
    // 优化太难了，就用opencv得了
    throw;
}
*/