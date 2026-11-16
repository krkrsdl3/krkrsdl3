#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "PlatformView.h"

//---------------------------------------------------------------------------
// TVPCompositor
//---------------------------------------------------------------------------
// 通用合成器，用来管理所有非Layer/Bitmap系列的渲染，包括上屏Windows/Surface、emote-GPU加速
//---------------------------------------------------------------------------
namespace krkrsdl3
{
//---------------------------------------------------------------------------
// 渲染后端抽象层（合并接口）
//
// 目标：让"窗口贴图合成"这一层不再依赖具体图形 API。
// 当前实现的后端：
//   - "software" : SDL 软渲染（各平台入口通过 PlatformView.h 的平台函数提供）
//   - "opengl"   : OpenGL 3.3 Core / OpenGL ES 3.0（桌面 glad / 移动 GLES3）
//   - "vulkan"   : Vulkan 1.0（SDL3 窗口表面，位于 environ/sdl3/render/）
//   - "metal"    : 预留（未来 macOS，接口已按 Metal 适配设计，见 docs）
//
// 每个后端只有一个实现类，同时承担两个角色（合并设计）：
//   - 窗口贴图合成（上屏）：BeginFrame/EndFrame + *WindowTexture 方法
//   - 2D 网格渲染（供 emoteplayer 等插件）：离屏目标 + 一般贴图 + DrawMesh
//
// 贴图区分（接口上显式区分两类贴图）：
//   - 窗口贴图（CreateWindowTexture 等）：上屏合成用。
//     SW=SDL_Texture（经平台钩子）；GPU=与一般贴图同一实现。
//   - 一般贴图（CreateTexture 等）：离屏网格绘制用。
//     SW=CPU buffer；GPU=与窗口贴图同一实现。
//   对 GPU 后端而言两类贴图就是同一个函数；对软件后端实现不同，
//   因此接口上仍保留两套方法名。
//
// 设计要点（为 Metal 预留）：
//   1. 贴图句柄为不透明 void* —— GL 存内部贴图对象，Vulkan 存 VulkanTexture*，
//      Metal 可存 id<MTLTexture> 的堆包装对象，无需改接口。
//   2. 贴图更新以"整张 CPU 像素 + pitch"为单位 —— Metal 的 staging buffer
//      上传路径可直接映射到该接口。
//   3. 帧控制只有 BeginFrame/EndFrame —— Metal 的 CAMetalLayer drawable
//      获取与 present 封装在 EndFrame 内，对上层不可见。
//   4. 绘制以"目标矩形像素坐标"为单位，letterbox 计算由合成器统一完成，
//      所有后端行为一致。
//---------------------------------------------------------------------------
class iTVPRenderBackend
{
public:
    virtual ~iTVPRenderBackend() = default;

    // 后端标识："software" / "opengl" / "vulkan" / "metal"
    virtual const char* GetName() const = 0;
    // 是否硬件加速（用于日志与信息展示）
    virtual bool IsHardware() const { return false; }

    // ---- 帧控制（由 TVPRenderOnce 统一调度）----
    virtual void BeginFrame(int winWidth, int winHeight) = 0;
    virtual void EndFrame() = 0; // 提交并呈现（SwapBuffers / Present / vkQueuePresent）

    // ---- 窗口贴图管理（上屏合成用；SW=SDL_Texture，GPU=与一般贴图同一实现）----
    // 句柄为后端持有的不透明指针，nullptr 表示无效
    virtual void* CreateWindowTexture(int width, int height) = 0;
    virtual void UpdateWindowTexture(void* handle, const uint8_t* buff, int width, int height, int pitch) = 0;
    virtual void DestroyWindowTexture(void* handle) = 0;
    // 以像素坐标绘制窗口贴图（位置与尺寸已由合成器计算，含 letterbox）
    virtual void DrawWindowTexture(void* handle, float posX, float posY, float width, float height) = 0;

    // ---- 2D 网格渲染（离屏目标 + 一般贴图 + 蒙版/混合 + DrawMesh）----
    // 渲染目标（离屏）：GL= FBO，软渲染= CPU 缓冲，Vulkan= 离屏图像
    virtual void* CreateTarget(int width, int height) = 0;
    virtual void DestroyTarget(void* target) = 0;
    // 绑定当前绘制目标（内部完成视口/深度/混合等状态设置）
    virtual void SetTarget(void* target) = 0;
    // 清屏：clearColor=true 清颜色+深度；false 仅清深度（软件后端无深度，此时不清屏）
    virtual void ClearTarget(bool clearColor) = 0;
    // 回读 CPU 像素（调用期间像素有效，Unlock 后失效）
    virtual uint8_t* LockTarget(void* target, int& pitch) = 0;
    virtual void UnlockTarget(void* target) = 0;
    // 目标作为可采样纹理使用（GPU 纹理可直接作为绘制目标；返回的句柄
    // 可传给 DrawMesh 的 texture 参数与 SetMask）。GL 返回 FBO 颜色纹理，
    // Vulkan 返回目标自身（其图像视图），软渲染返回目标自身（其缓冲）。
    virtual void* GetTargetTexture(void* target) = 0;
    // 直接更新目标内容（整幅上传；用于 GPU 纹理的像素上传）。
    // GL 走 glTexSubImage2D，Vulkan 走 staging buffer + copy，
    // 软渲染直接写缓冲。调用后目标内容立即可作为采样/回读源。
    virtual void UpdateTargetTexture(void* target,
                                     const uint8_t* pixels,
                                     int width,
                                     int height,
                                     int pitch) = 0;

    // 一般贴图（SW=CPU buffer；GPU=与窗口贴图同一实现）
    virtual void* CreateTexture(int width, int height) = 0;
    virtual void UpdateTexture(void* texture, const uint8_t* pixels, int width, int height, int pitch) = 0;
    virtual void DestroyTexture(void* texture) = 0;
    // 上传契约（可选优化，带默认实现）：把 pixels 的 (x,y,w,h) 区域"入队"到 texture。
    // 语义与 UpdateTexture 一致：返回后数据对本帧后续绘制可见，但不保证 GPU 已完成，
    // 调用方可立即复用/释放 pixels 缓冲。后端须把同步内部消化（§3 上传契约）。
    // 默认实现忽略 x/y 走整幅 UpdateTexture —— GL/SW 行为与性能完全不变；
    // 支持子矩形的后端（Vulkan）覆盖此方法，把上传攒进暂存环、帧末一次性提交。
    virtual void UploadTextureRect(void* texture, int x, int y, int width, int height,
                                   const uint8_t* pixels, int pitch)
    {
        (void)x;
        (void)y;
        UpdateTexture(texture, pixels, width, height, pitch);
    }
    // 读取贴图像素（SW=CPU 缓冲零拷贝；GPU 后端回读后返回同一格式）。
    // 约定：RGBA 字节序、自上而下行序，返回值在下次上传/回读前有效；nullptr 表示不支持。
    // 测试宿主截屏即经此取帧（见 tests/autotest.md）。
    virtual uint8_t* LockTexture(void* texture, int& pitch) { return nullptr; }

    // ---- 统一绘制状态与原语 ----
    // 蒙版：绑定蒙版目标（alpha >= 128 为有效像素）；nullptr 关闭蒙版
    virtual void SetMask(void* maskTarget) = 0;

    // 一套混合枚举容纳两套语义（数值区间分离，因为 0/1/3/4/6 在两边含义不同）。
    // 后端按区间选各自管线/shader，保证 emote 与 RenderManager(bm*) 的公式都逐像素保持。
    enum BlendMode
    {
        // emoteplayer 约定
        BM_NORMAL = 0,    // 普通 alpha（alpha 通道走 MAX 方程）
        BM_MULTIPLY = 1,  // 乘色（1 与 4 等价）
        BM_ADD = 3,       // 加色
        BM_SKIP = 6,      // 跳过绘制
        BM_SOLID = 21,    // 纯色替换，uniformColor 有效

        // 软件 RenderManager bm* 约定（从 0x100 起避免与 emote 数值冲突）
        BM_COPY = 0x100,
        BM_ALPHA = 0x101,
        BM_CONSTALPHA = 0x102,
        BM_ADD_RM = 0x103,
        BM_SUB = 0x104,
        BM_MUL_RM = 0x105,
        BM_MUL_HDA = 0x106,
        BM_FILL = 0x107,
        BM_COPYCOLOR = 0x108,
        BM_COPYOPAQUE = 0x109,
        BM_COPYMASK = 0x10A,
    };
    static bool IsRMBlend(int mode) { return mode >= BM_COPY; }

    // 统一混合状态：emote 与 bm* 两套语义共用此入口。
    // opacity: 0..1（emote 走 DrawMesh/DrawRect 的 opacity；bm* 路径后端换算成 0..255）。
    // uniformColor: BM_SOLID / BM_FILL 使用，可为 nullptr；colorModulation: 采样后乘色。
    virtual void SetBlend(int mode,
                          float opacity = 1.0f,
                          const float* uniformColor = nullptr,
                          const float* colorModulation = nullptr) = 0;

    // 任意网格：vertices 交错 (x, y, u, v)，每顶点 4 个 float（NDC + UV）。
    // emote PSB 的中性 frame.color 是 0xff808080，调用方应传 identity，不要把 128/255 当染色。
    virtual void DrawMesh(const float* vertices,
                          int vertexCount,
                          const uint16_t* indices,
                          int indexCount,
                          void* texture,
                          float opacity,
                          const float* colorModulation = nullptr) = 0;

    // 轴对齐矩形：在当前目标上绘制源贴图的 (uv0..uv1) 区域到像素矩形 (x,y,w,h)。
    // 与 DrawMesh 是同级原语（不是"另一个渲染器"）：矩形可由后端走更省的管线。
    // 坐标约定：内容 y 向下，内容顶(t=0) 映射到 NDC -1（三后端回读一致）。
    virtual void DrawRect(void* texture,
                          float x,
                          float y,
                          float w,
                          float h,
                          float u0 = 0.0f,
                          float v0 = 0.0f,
                          float u1 = 1.0f,
                          float v1 = 1.0f) = 0;

    // 批绘制（现代原语）：同一 texture、当前 SetBlend 状态下，一次提交 count 个
    // 四边形。vertices 为 count*4 个交错 (x,y,u,v)（NDC + UV），indices 为空时
    // 用默认 (0,1,2,2,3,0) 逐 quad 展开。语义 = 同一批 texture/blend 的多个
    // DrawMesh 合并，避免每 sprite 一次 descriptor/pipeline bind（VK 热路径）。
    // 默认实现逐 quad 回退到 DrawMesh —— GL/SW 行为不变；VK 覆盖为单次 draw。
    virtual void DrawQuads(void* texture,
                           const float* vertices,
                           int quadCount,
                           const uint16_t* indices = nullptr,
                           int indexCount = 0);

    // 后端信息采集（GL 的厂商/版本/扩展日志等），无操作默认实现
    virtual void FetchInfo() {}

    // 当前混合状态里的 opacity / 染色。DrawQuads 不带这两个参数（它们属于
    // "当前状态"），默认实现必须从这里取——否则批绘制会把半透明图层按
    // 全不透明画出来（曾导致标题画面的淡入蒙层直接盖住标题内容）。
    float GetBlendOpacity() const { return blendOpacity_; }
    const float* GetBlendModulation() const { return blendModulation_; }

protected:
    // 由各后端 SetBlend 写入；默认值等价于"不透明、无染色"。
    float blendOpacity_ = 1.0f;
    float blendModulation_[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

//---------------------------------------------------------------------------
// 后端注册表：静态注册 + 平台探测，供 -render 参数选择与日志使用
//---------------------------------------------------------------------------
struct TVPRenderBackendDesc
{
    const char* name;        // 命令行使用的名字（"software"/"opengl"/"vulkan"）
    const char* description; // 人可读描述
    bool (*probe)();         // 平台可用性探测，nullptr 表示编译进来即可用
    iTVPRenderBackend* (*create)(); // 工厂；部分后端需要平台参数（如 SDL 窗口），
                                    // 由入口直接构造，此时可为 nullptr
};
void TVPRegisterRenderBackend(const TVPRenderBackendDesc& desc);
// 画面比例：false=保持比例 letterbox（默认），true=拉伸铺满窗口
void TVPSetStretchMode(bool enable);
bool TVPGetStretchMode();
// 探测通过的可用后端名列表
std::vector<std::string> TVPListRenderBackends();
bool TVPRenderBackendAvailable(const std::string& name);

//---------------------------------------------------------------------------
// 全局当前后端（同时承担窗口合成与 2D 网格渲染，插件经此获取渲染器）
//---------------------------------------------------------------------------
void TVPSetRenderBackend(iTVPRenderBackend* backend);
iTVPRenderBackend* TVPGetRenderBackend();
// 销毁当前后端并清空指针
void TVPShutdownRenderBackend();

//---------------------------------------------------------------------------
// Windows Compositor
//---------------------------------------------------------------------------
void fetchGLInfo();
TVPSprite* KRKR_Get_Current_Sprite();
// 贴图管理
void TVPJoinTexture(TVPSprite* sp);
void TVPDepartTexture(TVPSprite* sp);
// 渲染函数
void TVPRenderOnce(int winWidth, int winHeight);
void TVPCreateTexture(TVPSprite& sp);
void TVPUpdateTexture(TVPSprite* sp, uint8_t* buff, int width, int height, int pitch);
void TVPDestroyTexture(TVPSprite* sp);
// 清理合成器中所有窗口贴图
void TVPClearAllTexture();
} // namespace krkrsdl3
