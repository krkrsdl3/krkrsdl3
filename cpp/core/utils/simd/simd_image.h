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
                    int ky);
}