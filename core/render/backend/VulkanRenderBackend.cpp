#include "VulkanRenderBackend.h"

#include "tjsCommHead.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <chrono>
#include <unordered_set>
#include <vector>

#include "Platform.h"
#include "TVPDebug.h"
#include "TVPSettings.h"

#include "shader/vulkan_shaders.h"
#include "shader/vulkan_shaders2d.h"

//---------------------------------------------------------------------------
// Vulkan 渲染后端（合并实现）
//
// 一个类同时承担两个角色（同一接口内，见 backend/RenderBackend.h）：
//   1. 窗口贴图合成后端——上屏呈现。
//   2. 2D 网格渲染器——离屏目标 + 网格绘制（emoteplayer 等插件）。
//
// GPU 后端下窗口贴图与一般贴图是同一套纹理实现
// （CreateWindowTexture == CreateTexture 等），共享设备/描述符池/采样器；
// 窗口合成与 2D 网格各自使用独立的管线/命令缓冲，互不干扰。
//---------------------------------------------------------------------------
namespace krkrsdl3
{
namespace
{
constexpr uint32_t kMaxDescriptorSets = 1024;
constexpr uint32_t kMaxDescriptorCount = 2048;
constexpr uint32_t kVertexCount = 4;
constexpr uint32_t kIndexCount = 6;

constexpr int kPipelineNormal = 0;   // bm 0 / 3 / 默认
constexpr int kPipelineMultiply = 1; // bm 1 / 4
constexpr int kPipelineColor = 2;    // bm 21

//---------------------------------------------------------------------------
// 阶段耗时插桩（KRKR_VK_PROF=1 时每 120 帧输出一次均值，单位 ms/帧）
// 仅用于定位热路径，默认完全关闭，不影响正常运行。
//---------------------------------------------------------------------------
struct VkProf
{
    using Clock = std::chrono::steady_clock;
    double waitFenceMs = 0; // BeginFrame 等上一帧栅栏
    double acquireMs = 0;   // vkAcquireNextImageKHR
    double beginMs = 0;     // BeginFrame 其余（重置/录制 pass 头）
    double uploadMs = 0;    // 纹理上传（memcpy + copy 录制）
    double drawMs = 0;      // DrawMesh / DrawRect / DrawQuads
    double drawFindMs = 0;  // 其中：句柄查找
    double drawPassMs = 0;  // 其中：BeginPass / 写后读提交
    double drawWriteMs = 0; // 其中：顶点/索引写入
    double drawCmdMs = 0;   // 其中：vkCmd* 录制
    double drawFlushMs = 0; // 其中：写后读 FlushMeshCommands
    double drawEnsureMs = 0;// 其中：EnsureVertexBuffers
    double drawBeginMs = 0; // 其中：BeginPass
    double bpWaitMs = 0;    //   BeginPass 内：WaitMeshIdle（GPU 等待）
    double bpResetMs = 0;   //   BeginPass 内：重置/开始命令缓冲
    double bpBarrierMs = 0; //   BeginPass 内：image barrier
    double bpRpMs = 0;      //   BeginPass 内：vkCmdBeginRenderPass
    int waitTotal = 0;      // WaitMeshIdle 调用次数
    int waitNotReady = 0;   // 其中 fence 尚未完成的比例
    double endMs = 0;       // EndFrame（含 mesh 提交）
    double lockMs = 0;      // LockTarget 回读
    int frames = 0;
    int drawCalls = 0;      // 每帧落到 vkCmdDrawIndexed 的次数
    int quadCount = 0;      // 每帧四边形总数

    static bool Enabled()
    {
        static const bool on = []() {
            const char* v = getenv("KRKR_VK_PROF");
            return v && *v && *v != '0';
        }();
        return on;
    }
    static VkProf& Get()
    {
        static VkProf p;
        return p;
    }
    struct Scope
    {
        double* slot;
        Clock::time_point t0;
        explicit Scope(double* s) : slot(s), t0(Clock::now()) {}
        ~Scope() { *slot += std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }
    };
    static Scope Time(double& slot) { return Scope(&slot); }

    void Tick()
    {
        if (!Enabled())
            return;
        if (++frames % 120)
            return;
        const double n = frames;
        TVPConsoleLog("[vkprof] frames=%d wait=%.3f acq=%.3f begin=%.3f upload=%.3f "
                      "draw=%.3f end=%.3f lock=%.3f drw/f=%.1f quad/f=%.1f (ms/frame)",
                      frames, waitFenceMs / n, acquireMs / n, beginMs / n, uploadMs / n,
                      drawMs / n, endMs / n, lockMs / n, drawCalls / n, quadCount / n);
        TVPConsoleLog("[vkprof]   draw = find %.3f + pass %.3f + write %.3f + cmd %.3f",
                      drawFindMs / n, drawPassMs / n, drawWriteMs / n, drawCmdMs / n);
        TVPConsoleLog("[vkprof]   detail = flush %.3f + ensurebuf %.3f + beginpass %.3f",
                      drawFlushMs / n, drawEnsureMs / n, drawBeginMs / n);
        TVPConsoleLog("[vkprof]   beginpass = waitidle %.3f + reset %.3f + barrier %.3f + rp %.3f",
                      bpWaitMs / n, bpResetMs / n, bpBarrierMs / n, bpRpMs / n);
        TVPConsoleLog("[vkprof]   waitMesh calls=%d notReady=%d (%.0f%% already done)",
                      waitTotal, waitNotReady,
                      waitTotal > 0 ? 100.0 * (waitTotal - waitNotReady) / waitTotal : 0.0);
    }
};

// 窗口合成 push constant：position(2) + size(2) + viewport(2)
struct WindowPushConstants
{
    float posX, posY;
    float sizeX, sizeY;
    float viewW, viewH;
};
static_assert(sizeof(WindowPushConstants) == 24, "window push constant layout");

// 2D 网格 push constant：与 vk2d_quad.frag 的 std140 布局一致（64 字节）
struct MeshPushConstants
{
    float viewportX, viewportY; // vec2 @0
    float enableMask;           // @8
    float enableColor;          // @12
    float opa;                  // @16
    float pad[3];               // @20-31
    float uniformColor[4];      // @32（16 字节对齐）
    float colorModulation[4];   // @48（16 字节对齐）
};
static_assert(sizeof(MeshPushConstants) == 64, "mesh push constant layout");

// Layer 合成 push constant：与 vk2d_layer.frag 的 std140 布局一致（32 字节）
struct LayerPushConstants
{
    float opa;                  // @0
    int method;                 // @4（BlendMode）
    float pad[2];               // @8-15
    alignas(16) float uniformColor[4]; // @16（16 字节对齐）
};
static_assert(sizeof(LayerPushConstants) == 32, "layer push constant layout");

bool CheckVkResult(VkResult result, const char* what)
{
    if (result != VK_SUCCESS)
    {
        TVPConsoleLog("Vulkan %s failed: %d", what, (int)result);
        return false;
    }
    return true;
}

uint32_t FindHostVisibleMemory(VkPhysicalDevice phys,
                               const VkMemoryRequirements& req,
                               bool coherentOnly)
{
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(phys, &props);
    // 打分选择：优先 device-local（GPU 采样/写入带宽最高），其次 host-cached
    // （CPU 读写不绕过缓存），最后 host-coherent（免手动 flush）。
    // 集显上常见 device-local|host-visible|host-cached 这一档，选它比默认取
    // 第一个 host-visible（通常是不带缓存的写合并内存）快得多。
    uint32_t best = 0xFFFFFFFF;
    int bestScore = -1;
    for (uint32_t i = 0; i < props.memoryTypeCount; i++)
    {
        if (!(req.memoryTypeBits & (1u << i)))
            continue;
        VkMemoryPropertyFlags flags = props.memoryTypes[i].propertyFlags;
        if (!(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
            continue;
        if (coherentOnly && !(flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
            continue;
        int score = 0;
        if (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
            score += 4;
        if (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
            score += 2;
        if (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
            score += 1;
        if (score > bestScore)
        {
            bestScore = score;
            best = i;
        }
    }
    return best;
}

// 是否存在"既是 device-local 又 host-visible"的内存类型。
// 集显/统一内存架构（UMA）：只有单一堆，CPU 映射写的内存 GPU 采样同样快，
// 此时 LINEAR 直写比 OPTIMAL+暂存拷贝少一次全帧 memcpy —— 应选直写路径。
// 独显：host 内存在 PCIe 另一侧，采样慢，应选 device-local OPTIMAL+暂存。
bool HasUnifiedHostVisibleMemory(VkPhysicalDevice phys)
{
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(phys, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; i++)
    {
        VkMemoryPropertyFlags f = props.memoryTypes[i].propertyFlags;
        if ((f & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) && (f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
            return true;
    }
    return false;
}

uint32_t FindDeviceLocalMemory(VkPhysicalDevice phys, const VkMemoryRequirements& req)
{
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(phys, &props);
    // 采样贴图优先纯 device-local（独显 VRAM）；集显的 DEVICE_LOCAL|HOST_VISIBLE
    // 也满足，退而求其次；最后才回退到任意可用类型，保证低端设备仍可运行。
    uint32_t best = 0xFFFFFFFF;
    int bestScore = -1;
    for (uint32_t i = 0; i < props.memoryTypeCount; i++)
    {
        if (!(req.memoryTypeBits & (1u << i)))
            continue;
        VkMemoryPropertyFlags flags = props.memoryTypes[i].propertyFlags;
        int score = 0;
        if (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
            score += 4;
        if (!(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) && (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            score += 2; // 纯 VRAM 优于集显共享内存
        if (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
            score += 1;
        if (score > bestScore)
        {
            bestScore = score;
            best = i;
        }
    }
    return best;
}
} // namespace

class VulkanRenderBackend : public iTVPRenderBackend
{
public:
    VulkanRenderBackend(VkInstance _instance, VkSurfaceKHR _surface)
      : instance_(_instance),
        surface_(_surface)
    {
    }
    ~VulkanRenderBackend() override { Shutdown(); }

    const char* GetName() const override { return "vulkan"; }
    bool IsHardware() const override { return true; }

    // ---- 窗口贴图合成 ----
    bool Initialize();
    void Shutdown();

    void BeginFrame(int winWidth, int winHeight) override;
    void EndFrame() override;

    void* CreateWindowTexture(int width, int height) override;
    void UpdateWindowTexture(void* handle, const uint8_t* buff, int width, int height, int pitch) override;
    void DestroyWindowTexture(void* handle) override;
    void DrawWindowTexture(void* handle, float posX, float posY, float width, float height) override;

    // ---- 2D 网格渲染（一般贴图 + 离屏网格绘制）----
    void* CreateTarget(int width, int height) override;
    void DestroyTarget(void* target) override;
    void SetTarget(void* target) override;
    void ClearTarget(bool clearColor) override;
    uint8_t* LockTarget(void* target, int& pitch) override;
    void UnlockTarget(void* target) override;
    void* GetTargetTexture(void* target) override;
    void UpdateTargetTexture(void* target,
                             const uint8_t* pixels,
                             int width,
                             int height,
                             int pitch) override;
    void* CreateTexture(int width, int height) override;
    void UpdateTexture(void* texture, const uint8_t* pixels, int width, int height, int pitch) override;
    void UploadTextureRect(void* texture, int x, int y, int width, int height,
                           const uint8_t* pixels, int pitch) override;
    void DestroyTexture(void* texture) override;
    uint8_t* LockTexture(void* texture, int& pitch) override;
    void SetMask(void* maskTarget) override;
    void SetBlend(int mode, float opacity, const float* uniformColor,
                  const float* colorModulation) override;
    void DrawMesh(const float* vertices,
                  int vertexCount,
                  const uint16_t* indices,
                  int indexCount,
                  void* texture,
                  float opacity,
                  const float* colorModulation = nullptr) override;
    void DrawQuads(void* texture, const float* vertices, int quadCount,
                   const uint16_t* indices, int indexCount) override;
    void DrawRect(void* texture,
                  float x,
                  float y,
                  float w,
                  float h,
                  float u0,
                  float v0,
                  float u1,
                  float v1) override;

private:
    void applyEmoteBlend(int mode, const float* uniformColor);
    void applyRMBlend(int method, float opacity, const float* uniformColor);
    // ---- 统一贴图（窗口贴图与一般贴图共用同一实现）----
    struct Texture
    {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE; // set0（纹理）
        uint8_t* mapped = nullptr; // 仅遗留线性贴图（回退/blankMask）使用
        int width = 0, height = 0;
        int pitch = 0;
        bool optimal = false; // true=device-local OPTIMAL，上传走暂存环

    };
    // 2D 离屏目标
    struct Target
    {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkDescriptorSet maskSet = VK_NULL_HANDLE; // 作为蒙版采样时绑定（set1）
        Texture* texture = nullptr; // 采样包装（set0 布局，注册在 textures_）
        int width = 0, height = 0;
    };
    Texture* FindTexture(void* handle) const;
    Texture* CreateTextureInternal(int width, int height, bool optimal = true);
    void UpdateTextureInternal(Texture* texture, const uint8_t* pixels, int width, int height, int pitch);
    void DestroyTextureInternal(Texture* texture);
    // 现代上传：子矩形入队到暂存环，录制 vkCmdCopyBufferToImage 进 mesh 命令缓冲，
    // 由帧末的单一提交统一执行；CPU 全程不等待。
    void EnqueueTextureUpload(Texture* texture, int x, int y, int width, int height,
                              const uint8_t* pixels, int pitch);
    // 暂存环：环形主机可见 buffer，按帧推进；只在环回时确认上一次使用已完成。
    bool EnsureUploadRing(size_t bytes);
    void RetireCurrentUploadRegion();

    // 窗口合成
    bool PickDevice();
    bool CreateDeviceAndQueues();
    bool CreateSwapchain(int width, int height); // 仅交换链 + 图像视图
    void DestroySwapchain();
    bool CreateWindowRenderPass();
    bool CreateWindowFramebuffers(); // 依赖 windowRenderPass_ 与 swapchainViews_
    bool CreateWindowPipeline();
    bool CreateSyncObjects();
    bool CreateWindowVertexBuffer();
    bool CreateSampler();
    void RecreateSwapchainIfNeeded();

    // 2D 网格（惰性初始化）
    bool EnsureMeshResources();
    bool EnsureMeshRenderPasses();
    bool EnsureMeshPipelines();
    bool BeginPass(Target* target, bool clear);
    void EndPass();
    bool FlushMeshCommands(bool wait = true, VkSemaphore signal = VK_NULL_HANDLE); // 提交并（可选）等待
    void WaitMeshIdle(); // 命令缓冲复用前确认上一次提交已完成
    bool EnsureStaging(size_t bytes);
    bool EnsureVertexBuffers(size_t vertexBytes, size_t indexBytes);
    uint8_t* LockTargetInternal(Target* target, int& pitch);

    // Layer 合成（惰性初始化）
    bool EnsureLayerPipelines();

    Target* FindTarget(void* handle) const;

    bool initialized_ = false;
    // UMA（集显/统一内存）：host-visible 同时 device-local，采样贴图走 LINEAR 直写
    // 比 OPTIMAL+暂存拷贝少一次全帧 memcpy；独显才走 OPTIMAL+暂存环。
    bool preferLinearTextures_ = false;
    // 自旋等待 fence（KRKR_VK_SPIN=0 可关闭）：规避 Windows 事件等待的调度粒度
    bool spinWait_ = true;

    bool swapchainDirty_ = false;
    bool frameActive_ = false; // BeginFrame 成功开始、等待 EndFrame 提交




    // 共享设备
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    uint32_t graphicsFamily_ = 0;
    uint32_t presentFamily_ = 0;

    // 交换链（窗口）
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D swapchainExtent_{};
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainViews_;
    std::vector<VkFramebuffer> swapchainFramebuffers_;
    uint32_t currentImage_ = 0;

    // 共享描述符/采样器/命令池（窗口与 2D 网格共用）
    VkDescriptorSetLayout textureSetLayout_ = VK_NULL_HANDLE; // set0（纹理）
    VkDescriptorSetLayout maskSetLayout_ = VK_NULL_HANDLE;    // set1（蒙版，2D）
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;

    // 窗口管线
    VkRenderPass windowRenderPass_ = VK_NULL_HANDLE;
    VkPipelineLayout windowPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline windowPipeline_ = VK_NULL_HANDLE;
    VkBuffer windowVertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory windowVertexMemory_ = VK_NULL_HANDLE;
    VkBuffer windowIndexBuffer_ = VK_NULL_HANDLE;   // 两个三角形 (0,1,2)/(2,3,0)，与 GL 后端一致
    VkDeviceMemory windowIndexMemory_ = VK_NULL_HANDLE;
    VkCommandBuffer frameCommandBuffer_ = VK_NULL_HANDLE;
    VkFence frameFence_ = VK_NULL_HANDLE;
    VkSemaphore imageReady_ = VK_NULL_HANDLE;
    VkSemaphore renderFinished_ = VK_NULL_HANDLE;
    // mesh 提交 → 窗口帧提交之间的跨提交依赖（不等待栅栏时使用）
    VkSemaphore meshDone_ = VK_NULL_HANDLE;
    int lastWidth_ = 0, lastHeight_ = 0;
    bool vsync_ = true;

    // 2D 网格管线（惰性创建）
    VkRenderPass meshClearPass_ = VK_NULL_HANDLE; // loadOp CLEAR
    VkRenderPass meshLoadPass_ = VK_NULL_HANDLE;  // loadOp LOAD
    VkPipelineLayout meshPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline meshPipelines_[3] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkCommandBuffer meshCommandBuffer_ = VK_NULL_HANDLE;
    VkFence meshFence_ = VK_NULL_HANDLE;
    bool meshReady_ = false;
    bool commandActive_ = false;

    // Layer 合成管线（惰性创建；与网格共用 render pass/命令缓冲/顶点缓冲）
    VkPipelineLayout layerPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline layerPipelines_[9] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE,
                                     VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE,
                                     VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
    bool layerReady_ = false;

    //---------------------------------------------------------------------------
    // 帧流水（多槽）
    //
    // 单槽时每帧第一次绘制都要 vkWaitForFences 等上一帧的命令执行完——CPU 与 GPU
    // 完全串行，帧时间 = CPU 时间 + GPU 时间。实测该等待占 1.2ms/帧（GPU 真的
    // 还在跑，不是等待机制的调度延迟），而 CPU 侧只要 2.1ms。
    //
    // 改成 kFramesInFlight 个槽后，每帧等到的是"N 帧前"的提交，稳态下早已完成，
    // 于是帧时间收敛到 max(CPU, GPU) 而不是两者之和。
    //
    // 句柄重绑定：meshCommandBuffer_ / meshFence_ / 顶点索引缓冲等成员保持原样，
    // 切槽时把槽内句柄重新赋给它们，因此既有绘制代码无需感知槽的存在。
    //---------------------------------------------------------------------------
    static constexpr int kFramesInFlight = 2;
    struct MeshSlot
    {
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
        uint8_t* vertexMapped = nullptr;
        size_t vertexCapacity = 0;
        size_t vertexCursor = 0;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory = VK_NULL_HANDLE;
        uint8_t* indexMapped = nullptr;
        size_t indexCapacity = 0;
        size_t indexCursor = 0;
        bool inFlight = false;
    };
    MeshSlot meshSlots_[kFramesInFlight];
    int meshSlot_ = 0;
    void SaveMeshSlot();          // 把当前句柄/游标写回所在槽
    void LoadMeshSlot(int slot);  // 载入指定槽的句柄/游标
    void AdvanceMeshSlot();       // 帧首切槽并等待该槽上一次提交

    VkBuffer meshVertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory meshVertexMemory_ = VK_NULL_HANDLE;
    size_t meshVertexCapacity_ = 0;
    uint8_t* meshVertexMapped_ = nullptr;
    size_t meshVertexCursor_ = 0; // 本帧已写入字节（DrawMesh/DrawQuads 递增，避免相互覆盖）
    VkBuffer meshIndexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory meshIndexMemory_ = VK_NULL_HANDLE;
    size_t meshIndexCapacity_ = 0;
    uint8_t* meshIndexMapped_ = nullptr;
    size_t meshIndexCursor_ = 0;

    VkBuffer stagingBuffer_ = VK_NULL_HANDLE; // 回读用（TRANSFER_DST）
    VkDeviceMemory stagingMemory_ = VK_NULL_HANDLE;
    size_t stagingSize_ = 0;
    uint8_t* stagingMapped_ = nullptr;

    // 上传暂存环（TRANSFER_SRC + host-visible）：一般贴图/modern 路径写入这里，
    // 再由 mesh 命令缓冲里的 vkCmdCopyBufferToImage 搬到 device-local 采样图。
    // 双区交替：一帧的上传写入当前区，帧末提交后 flip；GPU 在下一次复用该区前
    // 已完成上一批 copy（EndFrame 的 mesh 提交 → 下一帧 BeginPass 的 WaitMeshIdle），
    // 因此稳态帧内零等待、且不需要逐上传扩容。
    VkBuffer uploadRingBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory uploadRingMemory_ = VK_NULL_HANDLE;
    uint8_t* uploadRingMapped_ = nullptr;
    size_t uploadRingRegionSize_ = 0; // 每个区的容量（buffer 总容量 = 2 × 此值）
    size_t uploadRingCursor_ = 0;     // 当前区已写入字节数
    int uploadRingRegion_ = 0;        // 当前区索引 0/1
    bool uploadRingUsedThisBatch_ = false;

    // 空白蒙版（1x1 全白，无蒙版时绑定）
    Texture* blankMask_ = nullptr;

    // 2D 状态
    Target* currentTarget_ = nullptr;
    Target* maskTarget_ = nullptr;
    bool passActive_ = false;
    bool passClear_ = false;
    int blendMode_ = 0;
    bool skipDraw_ = false;
    bool enableColor_ = false;
    float uniformColor_[4] = {0, 0, 0, 0};
    // 批绘制状态（blendOpacity_ / blendModulation_）在基类 iTVPRenderBackend 中，
    // 由 SetBlend 写入，GL/SW/VK 三个后端的 DrawQuads 共用同一来源。

    // Layer 合成状态
    int layerMethod_ = 0;
    float layerOpa_ = 1.0f;
    float layerUniformColor_[4] = {0, 0, 0, 0};

    std::vector<Target*> targets_;
    std::vector<Texture*> textures_;
    // 句柄索引：FindTexture/FindTarget 在绘制热路径上被每条绘制调用，
    // 线性遍历 vector 会随资源数量线性退化（实测数千张贴图时单帧 1.2ms）。
    // vector 保留做生命周期遍历，这里只做 O(1) 存在性判定。
    std::unordered_set<const void*> textureIndex_;
    std::unordered_set<const void*> targetIndex_;

    // 当前 command buffer 内被写入过的目标图像（采样前需要先提交，避免同提交内写后读）
    std::vector<VkImage> writtenImages_;
    bool IsImageWritten(VkImage image) const
    {
        return std::find(writtenImages_.begin(), writtenImages_.end(), image) != writtenImages_.end();
    }
    // 当前/最近一次提交中被采样的图像（覆盖其映射内存前需要确认 GPU 不再读取）
    std::vector<VkImage> sampledImages_;
    bool IsImageSampled(VkImage image) const
    {
        return std::find(sampledImages_.begin(), sampledImages_.end(), image) != sampledImages_.end();
    }
    bool meshInFlight_ = false;
    // 上一帧上屏采样的图像（窗口 pass 可能仍在读取）
    std::vector<VkImage> presentedImages_;
    bool IsImagePresented(VkImage image) const
    {
        return std::find(presentedImages_.begin(), presentedImages_.end(), image) !=
               presentedImages_.end();
    }
};

//---------------------------------------------------------------------------
// 初始化
//---------------------------------------------------------------------------
bool VulkanRenderBackend::Initialize()
{
    if (initialized_)
        return true;

    vsync_ = TVPSettings.vsync != 0;

    if (!PickDevice())
        return false;
    if (!CreateDeviceAndQueues())
        return false;

    // Vulkan 严格初始化顺序（参考 out/Vulkan-SDL3 实例）：
    //   Instance → Surface → Device → Swapchain → RenderPass → Framebuffers → Pipeline
    // 交换链只依赖 surface；帧缓冲依赖交换链图像视图与 render pass，故在其后创建。
    int w = 0, h = 0;
    TVPGetWindowSizeInPixels(&w, &h);
    if (!CreateSwapchain(w, h))
        return false;
    if (!CreateWindowRenderPass())
        return false;
    if (!CreateWindowFramebuffers())
        return false;
    if (!CreateWindowPipeline())
        return false;
    if (!CreateSyncObjects())
        return false;
    if (!CreateWindowVertexBuffer())
        return false;
    if (!CreateSampler())
        return false;

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = graphicsFamily_;
    if (!CheckVkResult(vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_), "CreateCommandPool"))
        return false;

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    if (!CheckVkResult(vkAllocateCommandBuffers(device_, &allocInfo, &frameCommandBuffer_), "AllocateCommandBuffers"))
        return false;

    TVPConsoleLog("Vulkan backend initialized: %s / %s", "Vulkan 1.0", "quad compositor + 2D mesh");
    initialized_ = true;
    return true;
}

bool VulkanRenderBackend::PickDevice()
{
    uint32_t deviceCount = 0;
    if (!CheckVkResult(vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr), "EnumeratePhysicalDevices") ||
        deviceCount == 0)
    {
        TVPConsoleLog("No Vulkan physical devices found");
        return false;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());

    // 优先选择独立显卡，其次集成显卡（简单起见取第一个满足条件的）
    for (VkPhysicalDevice dev : devices)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU || devices.size() == 1)
        {
            physicalDevice_ = dev;
            break;
        }
    }
    if (physicalDevice_ == VK_NULL_HANDLE)
        physicalDevice_ = devices[0];

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice_, &props);
    TVPConsoleLog("Vulkan device: %s", props.deviceName);
    // UMA 判定：host-visible 同时 device-local（集显/统一内存）。
    // 直写 LINEAR 贴图可省掉每帧一次全幅 staging→VRAM 拷贝，且不损失采样带宽；
    // 独显则用 device-local OPTIMAL + 暂存环，避免从 PCIe 采样。
    preferLinearTextures_ = HasUnifiedHostVisibleMemory(physicalDevice_);
    if (const char* spin = getenv("KRKR_VK_SPIN"))
        spinWait_ = (*spin != '0');
    TVPConsoleLog("Vulkan texture path: %s", preferLinearTextures_
                  ? "LINEAR direct-map (unified memory)" : "OPTIMAL + staging ring (discrete)");
    return true;
}

bool VulkanRenderBackend::CreateDeviceAndQueues()
{
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &familyCount, families.data());

    // 找图形队列族与呈现队列族
    int graphicsFamily = -1, presentFamily = -1;
    for (uint32_t i = 0; i < familyCount; i++)
    {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            graphicsFamily = (int)i;
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice_, i, surface_, &presentSupport);
            if (presentSupport)
            {
                presentFamily = (int)i;
                break; // 同一队列族同时支持图形与呈现
            }
        }
    }
    if (graphicsFamily < 0)
    {
        // 图形与呈现族不同
        for (uint32_t i = 0; i < familyCount && presentFamily < 0; i++)
        {
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice_, i, surface_, &presentSupport);
            if (presentSupport)
                presentFamily = (int)i;
        }
        if (graphicsFamily < 0 || presentFamily < 0)
        {
            TVPConsoleLog("No suitable Vulkan queue families");
            return false;
        }
    }
    else if (presentFamily < 0)
    {
        presentFamily = graphicsFamily; // 同族
    }

    graphicsFamily_ = (uint32_t)graphicsFamily;
    presentFamily_ = (uint32_t)presentFamily;

    const float queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    auto addQueueInfo = [&](uint32_t family) {
        for (auto& qi : queueInfos)
            if (qi.queueFamilyIndex == family)
                return;
        VkDeviceQueueCreateInfo qi{};
        qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = family;
        qi.queueCount = 1;
        qi.pQueuePriorities = &queuePriority;
        queueInfos.push_back(qi);
    };
    addQueueInfo(graphicsFamily_);
    addQueueInfo(presentFamily_);

    const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = (uint32_t)queueInfos.size();
    deviceInfo.pQueueCreateInfos = queueInfos.data();
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = deviceExtensions;

    if (!CheckVkResult(vkCreateDevice(physicalDevice_, &deviceInfo, nullptr, &device_), "CreateDevice"))
        return false;

    vkGetDeviceQueue(device_, graphicsFamily_, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, presentFamily_, 0, &presentQueue_);
    return true;
}

//---------------------------------------------------------------------------
// 交换链
//---------------------------------------------------------------------------
bool VulkanRenderBackend::CreateSwapchain(int width, int height)
{
    if (width <= 0 || height <= 0)
        return false;

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &caps);

    // 格式
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, formats.data());
    swapchainFormat_ = VK_FORMAT_B8G8R8A8_UNORM;
    for (const auto& f : formats)
    {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM)
        {
            swapchainFormat_ = f.format;
            break;
        }
    }

    // 呈现模式
    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &presentModeCount, presentModes.data());
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR; // 有 vsync，保证可用
    if (!vsync_)
    {
        if (std::find(presentModes.begin(), presentModes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != presentModes.end())
            presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
        else
            presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    }

    // 尺寸（限制在能力范围内）
    VkExtent2D extent{};
    if (caps.currentExtent.width != 0xFFFFFFFF)
        extent = caps.currentExtent;
    else
    {
        extent.width = (uint32_t)std::max(1, std::min(width, (int)caps.maxImageExtent.width));
        extent.height = (uint32_t)std::max(1, std::min(height, (int)caps.maxImageExtent.height));
    }
    swapchainExtent_ = extent;

    // Android 的 Vulkan surface 可能通过 currentTransform 表示屏幕旋转
    // （例如横屏 Activity 上得到 ROTATE_90/270 + 竖向 extent）。如果直接把
    // preTransform 设为 currentTransform，却不在 shader/viewport 中补偿旋转，
    // 会出现内容正确但方向未跟随横屏、宽度被压缩的问题。
    // 优先请求 IDENTITY，让呈现引擎处理方向；不支持时才退回 currentTransform。
    VkSurfaceTransformFlagBitsKHR preTransform = caps.currentTransform;
    if (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
        preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR swapInfo{};
    swapInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapInfo.surface = surface_;
    swapInfo.minImageCount = imageCount;
    swapInfo.imageFormat = swapchainFormat_;
    swapInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapInfo.imageExtent = swapchainExtent_;
    swapInfo.imageArrayLayers = 1;
    swapInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (graphicsFamily_ != presentFamily_)
    {
        uint32_t queueFamilies[] = {graphicsFamily_, presentFamily_};
        swapInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapInfo.queueFamilyIndexCount = 2;
        swapInfo.pQueueFamilyIndices = queueFamilies;
    }
    else
    {
        swapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    swapInfo.preTransform = preTransform;
    swapInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapInfo.presentMode = presentMode;
    swapInfo.clipped = VK_TRUE;

    if (!CheckVkResult(vkCreateSwapchainKHR(device_, &swapInfo, nullptr, &swapchain_), "CreateSwapchain"))
        return false;

    vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr);
    swapchainImages_.resize(imageCount);
    vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, swapchainImages_.data());

    // 图像视图（帧缓冲在 CreateWindowFramebuffers 中创建——
    // 需先有 render pass，见初始化顺序注释）
    swapchainViews_.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; i++)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = swapchainImages_[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = swapchainFormat_;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (!CheckVkResult(vkCreateImageView(device_, &viewInfo, nullptr, &swapchainViews_[i]), "CreateImageView"))
            return false;
    }
    return true;
}

// 帧缓冲：必须在 windowRenderPass_ 与交换链图像视图创建之后调用
bool VulkanRenderBackend::CreateWindowFramebuffers()
{
    if (windowRenderPass_ == VK_NULL_HANDLE || swapchainViews_.empty())
        return false;
    swapchainFramebuffers_.resize(swapchainViews_.size());
    for (size_t i = 0; i < swapchainViews_.size(); i++)
    {
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = windowRenderPass_;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &swapchainViews_[i];
        fbInfo.width = swapchainExtent_.width;
        fbInfo.height = swapchainExtent_.height;
        fbInfo.layers = 1;
        if (!CheckVkResult(vkCreateFramebuffer(device_, &fbInfo, nullptr, &swapchainFramebuffers_[i]), "CreateFramebuffer"))
            return false;
    }
    return true;
}

void VulkanRenderBackend::DestroySwapchain()
{
    if (device_ == VK_NULL_HANDLE)
        return;
    vkDeviceWaitIdle(device_);
    for (auto fb : swapchainFramebuffers_)
        if (fb)
            vkDestroyFramebuffer(device_, fb, nullptr);
    for (auto v : swapchainViews_)
        if (v)
            vkDestroyImageView(device_, v, nullptr);
    swapchainFramebuffers_.clear();
    swapchainViews_.clear();
    swapchainImages_.clear();
    if (swapchain_)
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
}

void VulkanRenderBackend::RecreateSwapchainIfNeeded()
{
    int w = 0, h = 0;
    TVPGetWindowSizeInPixels(&w, &h);
    if (w <= 0 || h <= 0)
        return;
    if (swapchain_ && (uint32_t)w == swapchainExtent_.width && (uint32_t)h == swapchainExtent_.height && !swapchainDirty_)
        return;
    swapchainDirty_ = false;
    DestroySwapchain();
    if (!CreateSwapchain(w, h) || !CreateWindowFramebuffers())
    {
        TVPConsoleLog("Vulkan swapchain recreation failed");
        swapchainDirty_ = true;
    }
}

//---------------------------------------------------------------------------
// 渲染管线（窗口合成）
//---------------------------------------------------------------------------
bool VulkanRenderBackend::CreateWindowRenderPass()
{
    VkAttachmentDescription attachment{};
    attachment.format = swapchainFormat_;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &attachment;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    return CheckVkResult(vkCreateRenderPass(device_, &rpInfo, nullptr, &windowRenderPass_), "CreateRenderPass");
}

bool VulkanRenderBackend::CreateWindowPipeline()
{
    // 描述符集布局：set0/binding0 = combined image sampler（纹理，窗口与 2D 共用）
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (!CheckVkResult(vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &textureSetLayout_),
                       "CreateDescriptorSetLayout"))
        return false;

    // push constant（顶点阶段）
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(WindowPushConstants);

    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &textureSetLayout_;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pushRange;
    if (!CheckVkResult(vkCreatePipelineLayout(device_, &plInfo, nullptr, &windowPipelineLayout_), "CreatePipelineLayout"))
        return false;

    VkShaderModule vertModule = VK_NULL_HANDLE, fragModule = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo vsInfo{};
    vsInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vsInfo.codeSize = sizeof(kVulkanVertSpv);
    vsInfo.pCode = kVulkanVertSpv;
    VkShaderModuleCreateInfo fsInfo{};
    fsInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fsInfo.codeSize = sizeof(kVulkanFragSpv);
    fsInfo.pCode = kVulkanFragSpv;
    if (!CheckVkResult(vkCreateShaderModule(device_, &vsInfo, nullptr, &vertModule), "CreateVertexShader") ||
        !CheckVkResult(vkCreateShaderModule(device_, &fsInfo, nullptr, &fragModule), "CreateFragmentShader"))
    {
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    // 顶点输入：pos(2f) + uv(2f)
    VkVertexInputBindingDescription vertexBinding{};
    vertexBinding.binding = 0;
    vertexBinding.stride = 4 * sizeof(float);
    vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription vertexAttribs[2]{};
    vertexAttribs[0].location = 0;
    vertexAttribs[0].binding = 0;
    vertexAttribs[0].format = VK_FORMAT_R32G32_SFLOAT;
    vertexAttribs[0].offset = 0;
    vertexAttribs[1].location = 1;
    vertexAttribs[1].binding = 0;
    vertexAttribs[1].format = VK_FORMAT_R32G32_SFLOAT;
    vertexAttribs[1].offset = 2 * sizeof(float);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &vertexBinding;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions = vertexAttribs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // 无混合（与 GL 后端行为一致）
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = windowPipelineLayout_;
    pipelineInfo.renderPass = windowRenderPass_;
    pipelineInfo.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &windowPipeline_);
    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);
    if (!CheckVkResult(result, "CreateGraphicsPipelines"))
        return false;

    // 描述符池（窗口与 2D 网格共用）
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = kMaxDescriptorCount;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = kMaxDescriptorSets;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    return CheckVkResult(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_), "CreateDescriptorPool");
}

bool VulkanRenderBackend::CreateSyncObjects()
{
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    return CheckVkResult(vkCreateFence(device_, &fenceInfo, nullptr, &frameFence_), "CreateFence") &&
           CheckVkResult(vkCreateSemaphore(device_, &semInfo, nullptr, &imageReady_), "CreateSemaphore") &&
           CheckVkResult(vkCreateSemaphore(device_, &semInfo, nullptr, &renderFinished_), "CreateSemaphore");
}

bool VulkanRenderBackend::CreateWindowVertexBuffer()
{
    // 单位四边形（与 GL 后端同一套顶点/纹理坐标）
    float vertices[] = {
        // 位置   // 纹理坐标
        0.f, 0.f, 0.f, 1.f, // 左下
        1.f, 0.f, 1.f, 1.f, // 右下
        1.f, 1.f, 1.f, 0.f, // 右上
        0.f, 1.f, 0.f, 0.f, // 左上
    };
    const VkDeviceSize size = sizeof(vertices);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!CheckVkResult(vkCreateBuffer(device_, &bufferInfo, nullptr, &windowVertexBuffer_), "CreateVertexBuffer"))
        return false;

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device_, windowVertexBuffer_, &memReq);
    uint32_t memoryType = FindHostVisibleMemory(physicalDevice_, memReq, false);
    if (memoryType == 0xFFFFFFFF)
        return false;

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memoryType;
    if (!CheckVkResult(vkAllocateMemory(device_, &allocInfo, nullptr, &windowVertexMemory_), "AllocateVertexMemory"))
        return false;
    vkBindBufferMemory(device_, windowVertexBuffer_, windowVertexMemory_, 0);

    void* data = nullptr;
    vkMapMemory(device_, windowVertexMemory_, 0, size, 0, &data);
    memcpy(data, vertices, size);
    vkUnmapMemory(device_, windowVertexMemory_);

    // 索引：两个三角形（与 GL 后端的 EBO 一致：0,1,2 / 2,3,0）。
    // 注意：不能无索引 vkCmdDraw(4)——TRIANGLE_LIST 下 4 顶点只会覆盖右半屏。
    uint16_t indices[] = {0, 1, 2, 2, 3, 0};
    const VkDeviceSize indexSize = sizeof(indices);
    VkBufferCreateInfo indexInfo{};
    indexInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    indexInfo.size = indexSize;
    indexInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    indexInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!CheckVkResult(vkCreateBuffer(device_, &indexInfo, nullptr, &windowIndexBuffer_), "CreateWindowIndexBuffer"))
        return false;
    VkMemoryRequirements indexMemReq;
    vkGetBufferMemoryRequirements(device_, windowIndexBuffer_, &indexMemReq);
    uint32_t indexMemoryType = FindHostVisibleMemory(physicalDevice_, indexMemReq, false);
    if (indexMemoryType == 0xFFFFFFFF)
        return false;
    VkMemoryAllocateInfo indexAlloc{};
    indexAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    indexAlloc.allocationSize = indexMemReq.size;
    indexAlloc.memoryTypeIndex = indexMemoryType;
    if (!CheckVkResult(vkAllocateMemory(device_, &indexAlloc, nullptr, &windowIndexMemory_), "AllocateWindowIndexMemory"))
        return false;
    vkBindBufferMemory(device_, windowIndexBuffer_, windowIndexMemory_, 0);
    void* indexData = nullptr;
    vkMapMemory(device_, windowIndexMemory_, 0, indexSize, 0, &indexData);
    memcpy(indexData, indices, indexSize);
    vkUnmapMemory(device_, windowIndexMemory_);
    return true;
}

bool VulkanRenderBackend::CreateSampler()
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.maxLod = 0.0f;
    return CheckVkResult(vkCreateSampler(device_, &samplerInfo, nullptr, &sampler_), "CreateSampler");
}

//---------------------------------------------------------------------------
// 帧控制（窗口）
//---------------------------------------------------------------------------
void VulkanRenderBackend::BeginFrame(int winWidth, int winHeight)
{
    if (!initialized_)
        return;
    (void)winWidth;
    (void)winHeight;
    frameActive_ = false;
    RecreateSwapchainIfNeeded();
    if (!swapchain_ || swapchainDirty_)
        return;

    // 等待上一帧完成（超时则跳过本帧；不重置栅栏，避免无提交可等待）
    VkResult waitRes;
    {
        auto _t = VkProf::Time(VkProf::Get().waitFenceMs);
        waitRes = vkWaitForFences(device_, 1, &frameFence_, VK_TRUE, 500000000ull);
    }
    if (waitRes == VK_TIMEOUT)
    {
        VkResult fs = vkGetFenceStatus(device_, frameFence_);
        TVPConsoleLog("Vulkan BeginFrame: frame fence timeout (prev frame never signaled, status=%d)",
                      (int)fs);
        return; // 上一帧仍在执行，跳过本帧
    }

    VkResult result;
    {
        auto _t = VkProf::Time(VkProf::Get().acquireMs);
        result = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, imageReady_, VK_NULL_HANDLE,
                                       &currentImage_);
    }
    // 上一帧窗口 pass 已完成（上一帧的贴图上传也已在此之前发生），清空呈现图像记录
    presentedImages_.clear();
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        TVPConsoleLog("Vulkan BeginFrame: acquire returned %d (out-of-date/suboptimal)", (int)result);
        swapchainDirty_ = true;
        return; // 栅栏仍处于已信号状态，下次可继续等待
    }
    if (!CheckVkResult(result, "AcquireNextImage"))
        return;

    // 只有帧确定会提交后才重置栅栏；若后续失败则用空提交重新置位
    auto _beginTimer = VkProf::Time(VkProf::Get().beginMs);
    vkResetFences(device_, 1, &frameFence_);
    vkResetCommandBuffer(frameCommandBuffer_, 0);
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!CheckVkResult(vkBeginCommandBuffer(frameCommandBuffer_, &beginInfo), "BeginCommandBuffer"))
    {
        vkQueueSubmit(graphicsQueue_, 0, nullptr, frameFence_); // 重新置位栅栏
        return;
    }

    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 0.0f}}};
    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = windowRenderPass_;
    rpBegin.framebuffer = swapchainFramebuffers_[currentImage_];
    rpBegin.renderArea.offset = {0, 0};
    rpBegin.renderArea.extent = swapchainExtent_;
    rpBegin.clearValueCount = 1;
    rpBegin.pClearValues = &clearColor;
    vkCmdBeginRenderPass(frameCommandBuffer_, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
    frameActive_ = true;

    VkViewport viewport{};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = (float)swapchainExtent_.width;
    viewport.height = (float)swapchainExtent_.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(frameCommandBuffer_, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchainExtent_;
    vkCmdSetScissor(frameCommandBuffer_, 0, 1, &scissor);

    vkCmdBindPipeline(frameCommandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, windowPipeline_);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(frameCommandBuffer_, 0, 1, &windowVertexBuffer_, &offset);
}

void VulkanRenderBackend::DrawWindowTexture(void* handle, float posX, float posY, float width, float height)
{
    if (!initialized_ || !swapchain_ || !handle)
        return;
    Texture* tex = FindTexture(handle);
    if (!tex)
        return;
    if (!IsImagePresented(tex->image))
        presentedImages_.push_back(tex->image);

    WindowPushConstants pc{};
    pc.posX = posX;
    pc.posY = posY;
    pc.sizeX = width;
    pc.sizeY = height;
    pc.viewW = (float)swapchainExtent_.width;
    pc.viewH = (float)swapchainExtent_.height;
    vkCmdPushConstants(frameCommandBuffer_, windowPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

    vkCmdBindDescriptorSets(frameCommandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, windowPipelineLayout_, 0, 1,
                            &tex->set, 0, nullptr);
    // 索引绘制：两个三角形覆盖整个四边形（与 GL 后端 EBO 一致）
    vkCmdBindIndexBuffer(frameCommandBuffer_, windowIndexBuffer_, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(frameCommandBuffer_, 6, 1, 0, 0, 0);
}

void VulkanRenderBackend::EndFrame()
{
    if (!initialized_ || !swapchain_ || swapchainDirty_ || !frameActive_)
    {
        if (frameActive_)
            TVPConsoleLog("Vulkan EndFrame early-return (init=%d swap=%d dirty=%d)",
                          (int)initialized_, swapchain_ != VK_NULL_HANDLE, (int)swapchainDirty_);
        frameActive_ = false;
        return;
    }
    frameActive_ = false;
    auto _endTimer = VkProf::Time(VkProf::Get().endMs);

    vkCmdEndRenderPass(frameCommandBuffer_);
    if (!CheckVkResult(vkEndCommandBuffer(frameCommandBuffer_), "EndCommandBuffer"))
        return;

    // 提交未提交的 2D 网格命令（离屏 composite/填充绘制）。
    // 纯 GPU 路径下 composite 完成后没有 LockTarget 提交点，必须在这里把
    // mesh 命令提交到队列，且先于窗口帧提交（同队列串行 → 帧采样看到最新内容）。
    const bool meshSubmitted = FlushMeshCommands(false, meshDone_); // 只提交不等待
    RetireCurrentUploadRegion(); // 帧内上传攒批结束；下次扩容/回绕前由栅栏兜底

    // 窗口帧采样 mesh 写的合成目标：用二值信号量建立跨提交的依赖（不阻塞 CPU）
    VkSemaphore waitSems[2] = {imageReady_, meshDone_};
    VkPipelineStageFlags waitStages[2] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = meshSubmitted ? 2 : 1;
    submitInfo.pWaitSemaphores = waitSems;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frameCommandBuffer_;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinished_;
    if (!CheckVkResult(vkQueueSubmit(graphicsQueue_, 1, &submitInfo, frameFence_), "QueueSubmit"))
    {
        // 窗口帧没提交：用空提交消费掉 meshDone_ 并重新置位帧栅栏，避免后续等待悬挂
        if (meshSubmitted)
        {
            VkPipelineStageFlags drainStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            VkSubmitInfo drain{};
            drain.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            drain.waitSemaphoreCount = 1;
            drain.pWaitSemaphores = &meshDone_;
            drain.pWaitDstStageMask = &drainStage;
            vkQueueSubmit(graphicsQueue_, 1, &drain, frameFence_);
        }
        WaitMeshIdle();
        return;
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinished_;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &currentImage_;
    VkResult result = vkQueuePresentKHR(presentQueue_, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        TVPConsoleLog("Vulkan EndFrame: present returned %d (dirty)", (int)result);
        swapchainDirty_ = true;
    }
    else
        CheckVkResult(result, "QueuePresent");
    VkProf::Get().Tick();

    // 帧流水：本帧已提交，切到下一槽。必须在这里切而不是 BeginFrame——
    // 合成器的绘制（DrawDeviceD3D::RenderFrame）发生在 Application->Run() 里，
    // 早于 TVPRenderOnce 的 BeginFrame；若在 BeginFrame 切槽，这批命令会留在
    // 旧槽的命令缓冲里永不提交（表现为画面在第一帧之后完全冻结）。
    AdvanceMeshSlot();
}

//---------------------------------------------------------------------------
// 统一贴图（窗口贴图与一般贴图共用同一实现）
//---------------------------------------------------------------------------
VulkanRenderBackend::Texture* VulkanRenderBackend::FindTexture(void* handle) const
{
    // O(1)：句柄就是 Texture*，索引只用于确认它仍属于本后端实例。
    if (handle && textureIndex_.find(handle) != textureIndex_.end())
        return static_cast<Texture*>(handle);
    return nullptr;
}

VulkanRenderBackend::Texture* VulkanRenderBackend::CreateTextureInternal(int width, int height, bool optimal)
{
    if (width <= 0 || height <= 0)
        return nullptr;

    Texture* texture = new Texture();
    texture->width = width;
    texture->height = height;
    // UMA（集显）上 LINEAR 直写更快（少一次全幅拷贝），独显上 OPTIMAL+暂存更快。
    // blankMask 调用方显式传 false；其余一般贴图跟随设备能力。
    if (optimal)
        optimal = !preferLinearTextures_;

    // 现代采样图：device-local OPTIMAL tiling（GPU 采样带宽/缓存最优）。
    // 上传不再是 CPU 直接写映射内存，而是写入暂存环 → vkCmdCopyBufferToImage。
    // 布局全程 GENERAL（与离屏目标一致），绘制/采样/回读免布局转换。
    // optimal=false 仅用于 EnsureMeshResources 内部提前创建的 blankMask（1x1），
    // 保持旧的 host-visible 线性路径，避免在网格资源初始化完成前触发上传环。
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {(uint32_t)width, (uint32_t)height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = optimal ? VK_IMAGE_TILING_OPTIMAL : VK_IMAGE_TILING_LINEAR;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!CheckVkResult(vkCreateImage(device_, &imageInfo, nullptr, &texture->image), "CreateTextureImage"))
    {
        delete texture;
        return nullptr;
    }
    texture->optimal = optimal;
    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device_, texture->image, &memReq);
    uint32_t memoryType = optimal ? FindDeviceLocalMemory(physicalDevice_, memReq)
                                  : FindHostVisibleMemory(physicalDevice_, memReq, true);
    if (memoryType == 0xFFFFFFFF)
    {
        vkDestroyImage(device_, texture->image, nullptr);
        delete texture;
        return nullptr;
    }
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memoryType;
    if (!CheckVkResult(vkAllocateMemory(device_, &allocInfo, nullptr, &texture->memory), "AllocateTextureMemory"))
    {
        vkDestroyImage(device_, texture->image, nullptr);
        delete texture;
        return nullptr;
    }
    vkBindImageMemory(device_, texture->image, texture->memory, 0);

    if (optimal)
    {
        // OPTIMAL 图不可映射；逻辑行距固定 RGBA8 紧排，回读另行走 staging。
        texture->pitch = width * 4;
    }
    else
    {
        // 遗留线性贴图（blankMask）：保留行距查询与主机映射。
        VkImageSubresource subres{};
        subres.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        VkSubresourceLayout subresLayout;
        vkGetImageSubresourceLayout(device_, texture->image, &subres, &subresLayout);
        texture->pitch = (int)subresLayout.rowPitch;
        vkMapMemory(device_, texture->memory, 0, memReq.size, 0, (void**)&texture->mapped);
    }

    // 一次性转换到 GENERAL（采样布局）
    VkCommandBufferAllocateInfo cmdAlloc{};
    cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAlloc.commandPool = commandPool_;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;
    VkCommandBuffer cmd;
    if (!CheckVkResult(vkAllocateCommandBuffers(device_, &cmdAlloc, &cmd), "AllocateBarrierCmd"))
    {
        vkFreeMemory(device_, texture->memory, nullptr);
        vkDestroyImage(device_, texture->image, nullptr);
        delete texture;
        return nullptr;
    }
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture->image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue_);
    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);

    // 视图
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = texture->image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (!CheckVkResult(vkCreateImageView(device_, &viewInfo, nullptr, &texture->view), "CreateTextureView"))
    {
        DestroyTextureInternal(texture);
        return nullptr;
    }

    // set0 描述符
    VkDescriptorSetAllocateInfo descAlloc{};
    descAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descAlloc.descriptorPool = descriptorPool_;
    descAlloc.descriptorSetCount = 1;
    descAlloc.pSetLayouts = &textureSetLayout_;
    if (!CheckVkResult(vkAllocateDescriptorSets(device_, &descAlloc, &texture->set), "AllocateTextureSet"))
    {
        // 池耗尽：重建并重试一次
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = kMaxDescriptorCount * 2;
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = kMaxDescriptorSets * 2;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        if (!CheckVkResult(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_), "RecreateDescriptorPool") ||
            !CheckVkResult(vkAllocateDescriptorSets(device_, &descAlloc, &texture->set), "AllocateTextureSet"))
        {
            DestroyTextureInternal(texture);
            return nullptr;
        }
    }
    VkDescriptorImageInfo imageInfoDesc{};
    imageInfoDesc.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfoDesc.imageView = texture->view;
    imageInfoDesc.sampler = sampler_;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = texture->set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfoDesc;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

    textures_.push_back(texture);
    textureIndex_.insert(texture);
    return texture;
}

void VulkanRenderBackend::UpdateTextureInternal(Texture* texture, const uint8_t* pixels, int width, int height, int pitch)
{
    EnqueueTextureUpload(texture, 0, 0, width, height, pixels, pitch);
}

void VulkanRenderBackend::RetireCurrentUploadRegion()
{
    // 帧末提交后 flip 到另一半暂存区。当前区正被本帧 mesh 提交读取，
    // 下一帧不碰它；再过一帧复用时，中间帧的 BeginPass/WaitMeshIdle 已
    // 确认 GPU 读完。双区交替 → 稳态帧内零等待。
    uploadRingUsedThisBatch_ = false;
    uploadRingRegion_ ^= 1;
    uploadRingCursor_ = 0;
}

bool VulkanRenderBackend::EnsureUploadRing(size_t bytes)
{
    if (uploadRingBuffer_ && uploadRingRegionSize_ >= bytes)
        return true;

    // 扩容：每个区至少 4MB（覆盖整幅 1080p），buffer 总容量 = 2 × 区大小。
    size_t newRegion = std::max(bytes, uploadRingRegionSize_ * 2);
    newRegion = std::max<size_t>(newRegion, 4 * 1024 * 1024);
    size_t total = newRegion * 2;

    if (uploadRingBuffer_)
    {
        // 旧 buffer 可能仍被上一次提交的 copy 引用：调用方在扩容前已 Flush+Wait。
        if (meshInFlight_)
            WaitMeshIdle();
        vkUnmapMemory(device_, uploadRingMemory_);
        vkDestroyBuffer(device_, uploadRingBuffer_, nullptr);
        vkFreeMemory(device_, uploadRingMemory_, nullptr);
        uploadRingBuffer_ = VK_NULL_HANDLE;
        uploadRingMemory_ = VK_NULL_HANDLE;
        uploadRingMapped_ = nullptr;
        uploadRingRegionSize_ = 0;
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = total;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!CheckVkResult(vkCreateBuffer(device_, &bufferInfo, nullptr, &uploadRingBuffer_), "CreateUploadRing"))
        return false;
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device_, uploadRingBuffer_, &memReq);
    uint32_t memoryType = FindHostVisibleMemory(physicalDevice_, memReq, true);
    if (memoryType == 0xFFFFFFFF)
    {
        vkDestroyBuffer(device_, uploadRingBuffer_, nullptr);
        uploadRingBuffer_ = VK_NULL_HANDLE;
        return false;
    }
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memoryType;
    if (!CheckVkResult(vkAllocateMemory(device_, &allocInfo, nullptr, &uploadRingMemory_), "AllocateUploadRing"))
    {
        vkDestroyBuffer(device_, uploadRingBuffer_, nullptr);
        uploadRingBuffer_ = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(device_, uploadRingBuffer_, uploadRingMemory_, 0);
    vkMapMemory(device_, uploadRingMemory_, 0, memReq.size, 0, (void**)&uploadRingMapped_);
    uploadRingRegionSize_ = newRegion;
    uploadRingRegion_ = 0;
    uploadRingCursor_ = 0;
    return true;
}

void VulkanRenderBackend::EnqueueTextureUpload(Texture* texture, int x, int y, int width, int height,
                                               const uint8_t* pixels, int pitch)
{
    auto _t = VkProf::Time(VkProf::Get().uploadMs);
    if (!texture || !pixels || width <= 0 || height <= 0 || !initialized_)
        return;
    // 直写路径（UMA 集显的一般贴图 / blankMask）：CPU 直接写映射内存，零拷贝。
    if (!texture->optimal)
    {
        if (!texture->mapped)
            return;
        // 线性贴图可能正被尚未提交的绘制采样；覆盖前确认 GPU 读完。
        if (IsImageSampled(texture->image))
            FlushMeshCommands();
        if (IsImagePresented(texture->image))
            vkWaitForFences(device_, 1, &frameFence_, VK_TRUE, UINT64_MAX);
        for (int yy = 0; yy < height; yy++)
            memcpy(texture->mapped + (size_t)(yy + y) * texture->pitch + x * 4,
                   pixels + (size_t)yy * pitch, (size_t)width * 4);
        return;
    }

    size_t bytes = (size_t)width * height * 4;
    if (!EnsureMeshResources())
        return;

    // 双区暂存：本帧上传写当前区；帧末提交后 flip。若本区剩余空间装不下本批，
    // 必须按"已用 + 本批"扩容——只按本批大小判断会让 EnsureUploadRing 原样返回
    // （4MB 区 >= 1MB 批次），随后仍按旧游标写入，越过本区尾部（堆越界写）。
    size_t regionBase = (size_t)uploadRingRegion_ * uploadRingRegionSize_;
    if (uploadRingCursor_ + bytes > uploadRingRegionSize_)
    {
        if (commandActive_)
            FlushMeshCommands(); // 提交并等待，释放旧 buffer 引用
        if (!EnsureUploadRing(uploadRingCursor_ + bytes))
            return;
        uploadRingCursor_ = 0; // 扩容后从头写当前区
        regionBase = (size_t)uploadRingRegion_ * uploadRingRegionSize_;
    }
    uploadRingUsedThisBatch_ = true;

    // 逐行紧排写入当前区（VK 侧 pitch 固定 width*4，拷贝源按调用方 pitch）
    uint8_t* dst = uploadRingMapped_ + regionBase + uploadRingCursor_;
    for (int yy = 0; yy < height; yy++)
        memcpy(dst + (size_t)yy * width * 4, pixels + (size_t)yy * pitch, (size_t)width * 4);

    // vkCmdCopyBufferToImage 不能录制在 render pass 内；DrawDeviceD3D 会在多个
    // LayerManager 之间穿插"上传→绘制"，所以在 copy 前收尾当前 pass。
    // 后续 DrawMesh/DrawRect 会以 LOAD 变体重开 pass（同目标，无提交/等待）。
    if (passActive_)
    {
        EndPass();
        // 上一 pass 可能采样过同一张贴图（ScratchTexture 复用）；显式保证
        // 采样读完成后再覆盖图像（WAR 依赖），不跨提交、不阻塞 CPU。
        // 跨提交的一侧由 !commandActive_ 分支里的 WaitMeshIdle() 兜底。
        VkImageMemoryBarrier war{};
        war.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        war.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        war.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        war.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        war.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        war.image = texture->image;
        war.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        war.subresourceRange.levelCount = 1;
        war.subresourceRange.layerCount = 1;
        war.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        war.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(meshCommandBuffer_, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &war);
    }
    if (!commandActive_)
    {
        WaitMeshIdle();
        vkResetCommandBuffer(meshCommandBuffer_, 0);
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (!CheckVkResult(vkBeginCommandBuffer(meshCommandBuffer_, &beginInfo), "BeginUploadCmd"))
            return;
        commandActive_ = true;
    }

    VkBufferImageCopy region{};
    region.bufferOffset = regionBase + uploadRingCursor_;
    region.bufferRowLength = (uint32_t)width;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {(int32_t)x, (int32_t)y, 0};
    region.imageExtent = {(uint32_t)width, (uint32_t)height, 1};
    vkCmdCopyBufferToImage(meshCommandBuffer_, uploadRingBuffer_, texture->image,
                           VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    uploadRingCursor_ += bytes;

    // copy写入 → 片元采样 可见性（同提交内，GENERAL→GENERAL 免布局转换）。
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture->image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(meshCommandBuffer_, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void VulkanRenderBackend::UploadTextureRect(void* handle, int x, int y, int width, int height,
                                            const uint8_t* pixels, int pitch)
{
    EnqueueTextureUpload(FindTexture(handle), x, y, width, height, pixels, pitch);
}

void VulkanRenderBackend::DestroyTextureInternal(Texture* texture)
{
    if (!texture)
        return;
    if (device_ == VK_NULL_HANDLE)
    {
        // 设备已销毁时仅回收对象
        for (size_t i = 0; i < textures_.size(); i++)
        {
            if (textures_[i] == texture)
            {
                textures_.erase(textures_.begin() + i);
                textureIndex_.erase(texture);
                break;
            }
        }
        delete texture;
        return;
    }
    // 已录制的 mesh 命令可能引用本纹理的描述符集：先提交并等待
    FlushMeshCommands();
    vkDeviceWaitIdle(device_);
    presentedImages_.clear();
    for (size_t i = 0; i < textures_.size(); i++)
    {
        if (textures_[i] == texture)
        {
            textures_.erase(textures_.begin() + i);
            textureIndex_.erase(texture);
            break;
        }
    }
    if (texture->set)
        vkFreeDescriptorSets(device_, descriptorPool_, 1, &texture->set);
    if (texture->view)
        vkDestroyImageView(device_, texture->view, nullptr);
    if (texture->memory)
        vkFreeMemory(device_, texture->memory, nullptr);
    if (texture->image)
        vkDestroyImage(device_, texture->image, nullptr);
    delete texture;
}

//---------------------------------------------------------------------------
// 窗口贴图（iTVPRenderBackend）：与一般贴图同一实现
//---------------------------------------------------------------------------
void* VulkanRenderBackend::CreateWindowTexture(int width, int height)
{
    if (!initialized_)
        return nullptr;
    return CreateTextureInternal(width, height);
}

void VulkanRenderBackend::UpdateWindowTexture(void* handle, const uint8_t* buff, int width, int height, int pitch)
{
    UpdateTextureInternal(FindTexture(handle), buff, width, height, pitch);
}

void VulkanRenderBackend::DestroyWindowTexture(void* handle)
{
    DestroyTextureInternal(FindTexture(handle));
}

//---------------------------------------------------------------------------
// 2D 网格（一般贴图 + 离屏网格绘制）
//---------------------------------------------------------------------------
bool VulkanRenderBackend::EnsureMeshResources()
{
    if (meshReady_)
        return true;
    if (!initialized_ || device_ == VK_NULL_HANDLE)
        return false;

    // 共享设备/命令池/采样器/描述符池/纹理布局由窗口侧创建，这里只建网格专属资源

    // 帧流水：每槽一套命令缓冲 + 栅栏（顶点/索引缓冲在首次绘制时按需创建）
    for (int i = 0; i < kFramesInFlight; i++)
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool_;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        if (!CheckVkResult(vkAllocateCommandBuffers(device_, &allocInfo, &meshSlots_[i].cmd),
                           "AllocateMeshCommandBuffer"))
            return false;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (!CheckVkResult(vkCreateFence(device_, &fenceInfo, nullptr, &meshSlots_[i].fence),
                           "CreateMeshFence"))
            return false;
    }
    // 首帧从槽 0 开始（AdvanceMeshSlot 会切到槽 1，所以这里预置成 -1 的等价物）
    meshSlot_ = kFramesInFlight - 1;
    LoadMeshSlot(kFramesInFlight - 1);
    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (!CheckVkResult(vkCreateSemaphore(device_, &semInfo, nullptr, &meshDone_), "CreateMeshSemaphore"))
        return false;

    // 蒙版布局（set1，与 set0 纹理布局相同）
    VkDescriptorSetLayoutBinding bindings[1]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = bindings;
    if (!CheckVkResult(vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &maskSetLayout_), "CreateMaskLayout"))
        return false;

    if (!EnsureMeshRenderPasses() || !EnsureMeshPipelines())
        return false;

    // 空白蒙版（1x1 全白）
    blankMask_ = CreateTextureInternal(1, 1, false); // 线性+映射：初始化期不可用上传环
    if (!blankMask_)
        return false;
    uint8_t white[4] = {255, 255, 255, 255};
    UpdateTextureInternal(blankMask_, white, 1, 1, 4);

    // blankMask 作为 set1（蒙版）绑定，必须按 maskSetLayout_ 分配描述符集
    {
        VkDescriptorSetAllocateInfo bmAlloc{};
        bmAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        bmAlloc.descriptorPool = descriptorPool_;
        bmAlloc.descriptorSetCount = 1;
        bmAlloc.pSetLayouts = &maskSetLayout_;
        if (!CheckVkResult(vkAllocateDescriptorSets(device_, &bmAlloc, &blankMask_->set),
                           "AllocateBlankMaskSet"))
            return false;
        VkDescriptorImageInfo bmInfo{};
        bmInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        bmInfo.imageView = blankMask_->view;
        bmInfo.sampler = sampler_;
        VkWriteDescriptorSet bmWrite{};
        bmWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        bmWrite.dstSet = blankMask_->set;
        bmWrite.dstBinding = 0;
        bmWrite.descriptorCount = 1;
        bmWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bmWrite.pImageInfo = &bmInfo;
        vkUpdateDescriptorSets(device_, 1, &bmWrite, 0, nullptr);
    }

    meshReady_ = true;
    return true;
}

bool VulkanRenderBackend::EnsureMeshRenderPasses()
{
    VkAttachmentDescription attachment{};
    attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // 两个变体仅 loadOp 不同
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_GENERAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &attachment;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;

    if (!CheckVkResult(vkCreateRenderPass(device_, &rpInfo, nullptr, &meshClearPass_), "CreateClearRenderPass"))
        return false;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    if (!CheckVkResult(vkCreateRenderPass(device_, &rpInfo, nullptr, &meshLoadPass_), "CreateLoadRenderPass"))
        return false;
    return true;
}

bool VulkanRenderBackend::EnsureMeshPipelines()
{
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(MeshPushConstants);

    VkDescriptorSetLayout layouts[2] = {textureSetLayout_, maskSetLayout_};
    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 2;
    plInfo.pSetLayouts = layouts;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pushRange;
    if (!CheckVkResult(vkCreatePipelineLayout(device_, &plInfo, nullptr, &meshPipelineLayout_), "CreatePipelineLayout"))
        return false;

    VkShaderModule vertModule = VK_NULL_HANDLE, fragModule = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo vsInfo{};
    vsInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vsInfo.codeSize = sizeof(kVulkan2DVertSpv);
    vsInfo.pCode = kVulkan2DVertSpv;
    VkShaderModuleCreateInfo fsInfo{};
    fsInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fsInfo.codeSize = sizeof(kVulkan2DFragSpv);
    fsInfo.pCode = kVulkan2DFragSpv;
    if (!CheckVkResult(vkCreateShaderModule(device_, &vsInfo, nullptr, &vertModule), "CreateVertexShader") ||
        !CheckVkResult(vkCreateShaderModule(device_, &fsInfo, nullptr, &fragModule), "CreateFragmentShader"))
    {
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 4 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attribs[2]{};
    attribs[0].location = 0;
    attribs[0].binding = 0;
    attribs[0].format = VK_FORMAT_R32G32_SFLOAT;
    attribs[0].offset = 0;
    attribs[1].location = 1;
    attribs[1].binding = 0;
    attribs[1].format = VK_FORMAT_R32G32_SFLOAT;
    attribs[1].offset = 2 * sizeof(float);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions = attribs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE; // 原 GL 路径深度测试恒通过，等价省略
    depthStencil.depthWriteEnable = VK_FALSE;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // 混合变体：与 GL 的 bm 映射一致
    VkPipelineColorBlendAttachmentState blendAttachments[3]{};
    // bm 0/3：SRC_ALPHA / ONE_MINUS_SRC_ALPHA（RGB）+ ONE/ONE（A，MAX 方程）
    blendAttachments[kPipelineNormal].blendEnable = VK_TRUE;
    blendAttachments[kPipelineNormal].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachments[kPipelineNormal].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachments[kPipelineNormal].colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachments[kPipelineNormal].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachments[kPipelineNormal].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachments[kPipelineNormal].alphaBlendOp = VK_BLEND_OP_MAX;
    blendAttachments[kPipelineNormal].colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                                      VK_COLOR_COMPONENT_G_BIT |
                                                      VK_COLOR_COMPONENT_B_BIT |
                                                      VK_COLOR_COMPONENT_A_BIT;
    // bm 1/4：DST_COLOR / ONE
    blendAttachments[kPipelineMultiply] = blendAttachments[kPipelineNormal];
    blendAttachments[kPipelineMultiply].srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
    blendAttachments[kPipelineMultiply].dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachments[kPipelineMultiply].srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachments[kPipelineMultiply].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachments[kPipelineMultiply].alphaBlendOp = VK_BLEND_OP_ADD;
    // bm 21（FillARGB/FillColor/FillMask）：软件语义为覆盖写（无视 alpha 混合）。
    // 若用 alpha 混合，透明色填充（如 0x00FFFFFF 中性色）无法覆盖旧内容，
    // 会把图层白底留在目标上。
    blendAttachments[kPipelineColor] = blendAttachments[kPipelineNormal];
    blendAttachments[kPipelineColor].blendEnable = VK_FALSE;

    for (int i = 0; i < 3; i++)
    {
        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &blendAttachments[i];

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlend;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = meshPipelineLayout_;
        pipelineInfo.renderPass = meshClearPass_;
        pipelineInfo.subpass = 0;
        if (!CheckVkResult(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                                     &meshPipelines_[i]), "CreateGraphicsPipelines"))
        {
            vkDestroyShaderModule(device_, vertModule, nullptr);
            vkDestroyShaderModule(device_, fragModule, nullptr);
            return false;
        }
    }
    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);
    return true;
}

//---------------------------------------------------------------------------
// Layer 合成管线（软件 RenderManager bm* 语义的混合变体）
// 与 2D 网格共用 render pass/命令缓冲/顶点缓冲/描述符池；每个方法对应
// 一个固定混合状态的管线（混合方程/因子烘焙进管线），颜色变换在
// vk2d_layer.frag 中按 push constant 的 method 完成。
//---------------------------------------------------------------------------
bool VulkanRenderBackend::EnsureLayerPipelines()
{
    if (layerReady_)
        return true;
    if (!initialized_ || device_ == VK_NULL_HANDLE || !meshReady_)
        return false;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(LayerPushConstants);

    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &textureSetLayout_;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pushRange;
    if (!CheckVkResult(vkCreatePipelineLayout(device_, &plInfo, nullptr, &layerPipelineLayout_),
                       "CreateLayerPipelineLayout"))
        return false;

    VkShaderModule vertModule = VK_NULL_HANDLE, fragModule = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo vsInfo{};
    vsInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vsInfo.codeSize = sizeof(kVulkan2DVertSpv);
    vsInfo.pCode = kVulkan2DVertSpv;
    VkShaderModuleCreateInfo fsInfo{};
    fsInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fsInfo.codeSize = sizeof(kVulkan2DLayerFragSpv);
    fsInfo.pCode = kVulkan2DLayerFragSpv;
    if (!CheckVkResult(vkCreateShaderModule(device_, &vsInfo, nullptr, &vertModule), "CreateVertexShader") ||
        !CheckVkResult(vkCreateShaderModule(device_, &fsInfo, nullptr, &fragModule), "CreateFragmentShader"))
    {
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 4 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attribs[2]{};
    attribs[0].location = 0;
    attribs[0].binding = 0;
    attribs[0].format = VK_FORMAT_R32G32_SFLOAT;
    attribs[0].offset = 0;
    attribs[1].location = 1;
    attribs[1].binding = 0;
    attribs[1].format = VK_FORMAT_R32G32_SFLOAT;
    attribs[1].offset = 2 * sizeof(float);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions = attribs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // 各 BlendMode 的混合状态（与 GL 后端 SetBlend 一一对应）：
    //   0 COPY / 7 FILL / 9 COPYOPAQUE：不混合（直写）
    //   1 ALPHA / 2 CONSTALPHA：SRC_ALPHA/ONE_MINUS_SRC_ALPHA（全通道）
    //   3 ADD：ONE/ONE 加
    //   4 SUB：ONE/ONE 反向减（dst - src，钳制）
    //   5 MUL：color=DST_COLOR/ZERO，alpha=ZERO/ZERO
    //   6 MUL_HDA：color=DST_COLOR/ZERO，alpha=ONE/ZERO
    //   8 COPYCOLOR：color=ONE/ZERO，alpha=ZERO/ONE
    //   10 COPYMASK：color=ZERO/ONE，alpha=ONE/ZERO
    VkPipelineColorBlendAttachmentState blendAttachments[9]{};
    for (int i = 0; i < 9; i++)
    {
        blendAttachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    }
    // 0: 直写
    // 1: alpha
    blendAttachments[1].blendEnable = VK_TRUE;
    blendAttachments[1].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachments[1].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachments[1].colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachments[1].srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachments[1].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachments[1].alphaBlendOp = VK_BLEND_OP_ADD;
    // 2: const alpha（与 1 相同混合状态）
    blendAttachments[2] = blendAttachments[1];
    // 3: add
    blendAttachments[3].blendEnable = VK_TRUE;
    blendAttachments[3].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachments[3].dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachments[3].colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachments[3].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachments[3].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachments[3].alphaBlendOp = VK_BLEND_OP_ADD;
    // 4: sub（dst - src）
    blendAttachments[4] = blendAttachments[3];
    blendAttachments[4].colorBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;
    blendAttachments[4].alphaBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;
    // 5: mul
    blendAttachments[5].blendEnable = VK_TRUE;
    blendAttachments[5].srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
    blendAttachments[5].dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachments[5].colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachments[5].srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachments[5].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachments[5].alphaBlendOp = VK_BLEND_OP_ADD;
    // 6: mul_hda
    blendAttachments[6] = blendAttachments[5];
    blendAttachments[6].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachments[6].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    // 7: copycolor
    blendAttachments[7].blendEnable = VK_TRUE;
    blendAttachments[7].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachments[7].dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachments[7].colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachments[7].srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachments[7].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachments[7].alphaBlendOp = VK_BLEND_OP_ADD;
    // 8: copymask（dest = (dest & RGB) | (src & A)）
    blendAttachments[8].blendEnable = VK_TRUE;
    blendAttachments[8].srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachments[8].dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachments[8].colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachments[8].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachments[8].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachments[8].alphaBlendOp = VK_BLEND_OP_ADD;

    for (int i = 0; i < 9; i++)
    {
        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &blendAttachments[i];

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlend;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = layerPipelineLayout_;
        pipelineInfo.renderPass = meshClearPass_;
        pipelineInfo.subpass = 0;
        if (!CheckVkResult(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                                     &layerPipelines_[i]), "CreateLayerPipelines"))
        {
            vkDestroyShaderModule(device_, vertModule, nullptr);
            vkDestroyShaderModule(device_, fragModule, nullptr);
            return false;
        }
    }
    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);
    layerReady_ = true;
    return true;
}

//---------------------------------------------------------------------------
// 目标（离屏）
//---------------------------------------------------------------------------
VulkanRenderBackend::Target* VulkanRenderBackend::FindTarget(void* handle) const
{
    // O(1)：同 FindTexture，句柄即 Target*，索引负责归属校验。
    if (handle && targetIndex_.find(handle) != targetIndex_.end())
        return static_cast<Target*>(handle);
    return nullptr;
}

void* VulkanRenderBackend::CreateTarget(int width, int height)
{
    if (!EnsureMeshResources() || width <= 0 || height <= 0)
        return nullptr;

    Target* target = new Target();
    target->width = width;
    target->height = height;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {(uint32_t)width, (uint32_t)height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!CheckVkResult(vkCreateImage(device_, &imageInfo, nullptr, &target->image), "CreateTargetImage"))
    {
        delete target;
        return nullptr;
    }

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device_, target->image, &memReq);
    uint32_t memoryType = FindHostVisibleMemory(physicalDevice_, memReq, false);
    if (memoryType == 0xFFFFFFFF)
        memoryType = 0;
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memoryType;
    if (!CheckVkResult(vkAllocateMemory(device_, &allocInfo, nullptr, &target->memory), "AllocateTargetMemory"))
    {
        vkDestroyImage(device_, target->image, nullptr);
        delete target;
        return nullptr;
    }
    vkBindImageMemory(device_, target->image, target->memory, 0);

    // 一次性转换到 GENERAL（之后绘制/采样/回读均无需再转换）
    VkCommandBufferAllocateInfo cmdAlloc{};
    cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAlloc.commandPool = commandPool_;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;
    VkCommandBuffer cmd;
    if (!CheckVkResult(vkAllocateCommandBuffers(device_, &cmdAlloc, &cmd), "AllocateBarrierCmd"))
    {
        vkFreeMemory(device_, target->memory, nullptr);
        vkDestroyImage(device_, target->image, nullptr);
        delete target;
        return nullptr;
    }
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = target->image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue_);
    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);

    // 视图
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = target->image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (!CheckVkResult(vkCreateImageView(device_, &viewInfo, nullptr, &target->view), "CreateTargetView"))
    {
        DestroyTarget(target);
        return nullptr;
    }

    // 帧缓冲（meshClearPass_ 与 meshLoadPass_ 兼容，可共用）
    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = meshClearPass_;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = &target->view;
    fbInfo.width = (uint32_t)width;
    fbInfo.height = (uint32_t)height;
    fbInfo.layers = 1;
    if (!CheckVkResult(vkCreateFramebuffer(device_, &fbInfo, nullptr, &target->framebuffer), "CreateTargetFramebuffer"))
    {
        DestroyTarget(target);
        return nullptr;
    }

    // 蒙版描述符（set1：作为蒙版采样）
    VkDescriptorSetAllocateInfo descAlloc{};
    descAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descAlloc.descriptorPool = descriptorPool_;
    descAlloc.descriptorSetCount = 1;
    descAlloc.pSetLayouts = &maskSetLayout_;
    if (!CheckVkResult(vkAllocateDescriptorSets(device_, &descAlloc, &target->maskSet), "AllocateMaskSet"))
    {
        DestroyTarget(target);
        return nullptr;
    }
    VkDescriptorImageInfo imageInfoDesc{};
    imageInfoDesc.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfoDesc.imageView = target->view;
    imageInfoDesc.sampler = sampler_;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = target->maskSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfoDesc;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

    // 采样包装：set0（纹理）布局，供 DrawMesh / DrawWindowTexture 按 Texture* 查找
    target->texture = new Texture();
    target->texture->image = target->image;
    target->texture->view = target->view;
    target->texture->memory = VK_NULL_HANDLE; // 不拥有（归 target）
    target->texture->width = width;
    target->texture->height = height;
    {
        VkDescriptorSetAllocateInfo texAlloc{};
        texAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        texAlloc.descriptorPool = descriptorPool_;
        texAlloc.descriptorSetCount = 1;
        texAlloc.pSetLayouts = &textureSetLayout_;
        if (CheckVkResult(vkAllocateDescriptorSets(device_, &texAlloc, &target->texture->set),
                          "AllocateTargetTextureSet"))
        {
            VkDescriptorImageInfo texInfo{};
            texInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            texInfo.imageView = target->view;
            texInfo.sampler = sampler_;
            VkWriteDescriptorSet texWrite{};
            texWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            texWrite.dstSet = target->texture->set;
            texWrite.dstBinding = 0;
            texWrite.descriptorCount = 1;
            texWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            texWrite.pImageInfo = &texInfo;
            vkUpdateDescriptorSets(device_, 1, &texWrite, 0, nullptr);
            textures_.push_back(target->texture);
            textureIndex_.insert(target->texture);
        }
        else
        {
            delete target->texture;
            target->texture = nullptr;
        }
    }

    targets_.push_back(target);
    targetIndex_.insert(target);
    return target;
}

void VulkanRenderBackend::DestroyTarget(void* handle)
{
    Target* target = FindTarget(handle);
    if (!target)
        return;
    if (!meshReady_)
    {
        // 网格资源未初始化时仅回收对象
        for (size_t i = 0; i < targets_.size(); i++)
        {
            if (targets_[i] == target)
            {
                targets_.erase(targets_.begin() + i);
                targetIndex_.erase(target);
                break;
            }
        }
        delete target;
        return;
    }
    // 已录制的 mesh 命令可能引用本 target 的 imageView/描述符集：先提交并等待
    FlushMeshCommands();
    vkDeviceWaitIdle(device_);
    presentedImages_.clear();
    if (currentTarget_ == target)
    {
        passActive_ = false;
        currentTarget_ = nullptr;
    }
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
    if (target->maskSet)
        vkFreeDescriptorSets(device_, descriptorPool_, 1, &target->maskSet);
    if (target->texture)
    {
        for (size_t i = 0; i < textures_.size(); i++)
        {
            if (textures_[i] == target->texture)
            {
                textures_.erase(textures_.begin() + i);
                textureIndex_.erase(target->texture);
                break;
            }
        }
        if (target->texture->set)
            vkFreeDescriptorSets(device_, descriptorPool_, 1, &target->texture->set);
        delete target->texture; // 不销毁 image/view/memory（归 target）
    }
    if (target->framebuffer)
        vkDestroyFramebuffer(device_, target->framebuffer, nullptr);
    if (target->view)
        vkDestroyImageView(device_, target->view, nullptr);
    if (target->memory)
        vkFreeMemory(device_, target->memory, nullptr);
    if (target->image)
        vkDestroyImage(device_, target->image, nullptr);
    delete target;
}

void VulkanRenderBackend::SetTarget(void* handle)
{
    Target* next = FindTarget(handle);
    if (next && next == currentTarget_)
    {
        // 同一目标：保持当前 pass 连续录制，避免每个图层都 EndPass/BeginPass。
        // 已请求清屏时才结束当前 pass，让下一次绘制以 CLEAR 变体重开。
        if (passClear_)
            EndPass();
        return;
    }
    EndPass();
    // 清屏标志语义（与 GL 立即模式对齐）：ClearTarget(true) 的请求应作用于
    // 下一次对"当前目标"的 pass——调用方常在 ClearTarget 之后再次 SetTarget
    // 同一目标（如 emote 引擎逐节点 SetTarget），同目标的 SetTarget 不得抹掉
    // 清屏请求，否则 pass 走 LOAD 变体导致残影。
    if (next != currentTarget_)
        passClear_ = false;
    currentTarget_ = next;
}

void VulkanRenderBackend::ClearTarget(bool clearColor)
{
    // 软件后端语义：clearColor=false 仅清深度（Vulkan 2D 无深度附件）→ 不清屏。
    // 清屏请求一旦提出（true）即保持到下一次 pass 消费为止——与 GL 立即清屏的
    // 净效果一致；后续 ClearTarget(false) 不能"撤回"已请求的清屏
    //（例如 RenderFrame 先 ClearTarget(true)、各 D3D 层绘制前又 ClearTarget(false)，
    // GL 下目标已被清过，VK 下清屏必须仍然发生）。
    if (clearColor)
    {
        passClear_ = true;
        // 清屏请求要作用于"下一次绘制"：若当前已有进行中的 pass，先结束它，
        // 让下一次绘制用 CLEAR 变体重开（与原先每层 EndPass 的语义一致）。
        if (passActive_)
            EndPass();
    }
}

//---------------------------------------------------------------------------
// 命令录制（2D 网格）
//---------------------------------------------------------------------------
bool VulkanRenderBackend::BeginPass(Target* target, bool clear)
{
    if (!target)
        return false;
    const bool prof = VkProf::Enabled();
    auto bdt = [](VkProf::Clock::time_point a, VkProf::Clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    VkProf::Clock::time_point b0, b1, b2, b3, b4;
    if (prof)
        b0 = VkProf::Clock::now();
    if (!commandActive_)
    {
        WaitMeshIdle(); // 上一次提交可能仍在执行，复用它之前必须确认完成
        if (prof)
            b1 = VkProf::Clock::now();
        vkResetCommandBuffer(meshCommandBuffer_, 0);
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (!CheckVkResult(vkBeginCommandBuffer(meshCommandBuffer_, &beginInfo), "BeginCommandBuffer"))
            return false;
        commandActive_ = true;
        if (prof)
        {
            b2 = VkProf::Clock::now();
            VkProf::Get().bpWaitMs += bdt(b0, b1);
            VkProf::Get().bpResetMs += bdt(b1, b2);
        }
    }
    else if (prof)
        b2 = VkProf::Clock::now();

    VkClearValue clearValue = {{{0.0f, 0.0f, 0.0f, 0.0f}}};
    if (!clear)
    {
        // LOAD pass 会读取目标自身内容（混合到已有内容）：确保同一 command buffer 内
        // 上一次对该目标的 color attachment 写入对本次 LOAD 可见。
        VkImageMemoryBarrier loadBarrier{};
        loadBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        loadBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        loadBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        loadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        loadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        loadBarrier.image = target->image;
        loadBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        loadBarrier.subresourceRange.levelCount = 1;
        loadBarrier.subresourceRange.layerCount = 1;
        loadBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        loadBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(meshCommandBuffer_, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &loadBarrier);
    }
    if (prof)
        b3 = VkProf::Clock::now();
    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = clear ? meshClearPass_ : meshLoadPass_;
    rpBegin.framebuffer = target->framebuffer;
    rpBegin.renderArea.offset = {0, 0};
    rpBegin.renderArea.extent = {(uint32_t)target->width, (uint32_t)target->height};
    rpBegin.clearValueCount = 1;
    rpBegin.pClearValues = &clearValue;
    vkCmdBeginRenderPass(meshCommandBuffer_, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = (float)target->width;
    viewport.height = (float)target->height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(meshCommandBuffer_, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {(uint32_t)target->width, (uint32_t)target->height};
    vkCmdSetScissor(meshCommandBuffer_, 0, 1, &scissor);

    passActive_ = true;
    if (clear)
        passClear_ = false; // 清屏请求已消费，后续同目标 pass 用 LOAD 累积
    if (!IsImageWritten(target->image))
        writtenImages_.push_back(target->image);
    if (prof)
    {
        b4 = VkProf::Clock::now();
        VkProf::Get().bpBarrierMs += bdt(b2, b3);
        VkProf::Get().bpRpMs += bdt(b3, b4);
    }
    return true;
}

void VulkanRenderBackend::EndPass()
{
    if (passActive_ && commandActive_)
        vkCmdEndRenderPass(meshCommandBuffer_);
    passActive_ = false;
}

void VulkanRenderBackend::WaitMeshIdle()
{
    if (!meshInFlight_)
        return;
    if (VkProf::Enabled())
    {
        // 诊断：等待前 fence 是否已完成。已完成说明慢在等待机制本身
        // （Windows 上 vkWaitForFences 走内核事件，可能受调度粒度影响）；
        // 未完成说明 GPU 真的还没跑完，要找提交内容的问题。
        VkResult st = vkGetFenceStatus(device_, meshFence_);
        ++VkProf::Get().waitTotal;
        if (st != VK_SUCCESS)
            ++VkProf::Get().waitNotReady;
    }
    if (spinWait_)
    {
        // 自旋等待：Windows 的 vkWaitForFences 走内核事件对象，最坏情况受
        // 线程调度粒度（1~15ms）影响；低延迟渲染路径用 poll 更稳。
        // 超过阈值仍没完成则退回阻塞等待，避免极端情况下空转。
        int spins = 0;
        while (vkGetFenceStatus(device_, meshFence_) != VK_SUCCESS)
        {
            if (++spins > 200000)
            {
                vkWaitForFences(device_, 1, &meshFence_, VK_TRUE, UINT64_MAX);
                break;
            }
#if defined(_MSC_VER)
            _mm_pause();
#elif defined(__x86_64__) || defined(__i386__)
            __builtin_ia32_pause();
#endif
        }
    }
    else
        vkWaitForFences(device_, 1, &meshFence_, VK_TRUE, UINT64_MAX);
    meshInFlight_ = false;
    meshSlots_[meshSlot_].inFlight = false;
    writtenImages_.clear();
    sampledImages_.clear();
    meshVertexCursor_ = 0;
    meshIndexCursor_ = 0;
}

bool VulkanRenderBackend::FlushMeshCommands(bool wait, VkSemaphore signal)
{
    // 提交并（可选）等待当前录制的 mesh 命令。调用场景：
    //   1. 渲染目标被销毁前（已录制命令可能引用其 imageView/描述符集）；
    //   2. 每帧呈现前（纯 GPU 路径下没有其他提交点）；
    //   3. 采样一个"本 command buffer 内刚被写入"的 target 前
    //      （写后读需要跨提交边界保证可见性——RADV 会忽略 GENERAL→GENERAL 的
    //       image barrier，同提交内写后读结果未定义）。
    // wait=false 只提交不等待（signal 用于让后续窗口帧等待该提交完成），
    // 命令缓冲在下一次录制前由 WaitMeshIdle 兜底。
    if (!commandActive_)
    {
        if (wait)
            WaitMeshIdle();
        return false;
    }
    EndPass();
    bool submitted = false;
    if (CheckVkResult(vkEndCommandBuffer(meshCommandBuffer_), "EndMeshCommandBuffer"))
    {
        vkResetFences(device_, 1, &meshFence_);
        VkSubmitInfo meshSubmit{};
        meshSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        meshSubmit.commandBufferCount = 1;
        meshSubmit.pCommandBuffers = &meshCommandBuffer_;
        if (signal)
        {
            meshSubmit.signalSemaphoreCount = 1;
            meshSubmit.pSignalSemaphores = &signal;
        }
        if (CheckVkResult(vkQueueSubmit(graphicsQueue_, 1, &meshSubmit, meshFence_), "SubmitMeshCommandBuffer"))
        {
            submitted = true;
            meshInFlight_ = true;
            meshSlots_[meshSlot_].inFlight = true;
            if (wait)
            {
                vkWaitForFences(device_, 1, &meshFence_, VK_TRUE, UINT64_MAX);
                meshInFlight_ = false;
                meshSlots_[meshSlot_].inFlight = false;
                // 提交已完成：顶点/索引缓冲内容不再被 GPU 引用，游标与
                // WaitMeshIdle 保持一致地归零，避免帧内多次 flush 后单调膨胀。
                meshVertexCursor_ = 0;
                meshIndexCursor_ = 0;
            }
        }
        else
            vkQueueSubmit(graphicsQueue_, 0, nullptr, meshFence_); // 重新置位栅栏，避免后续等待悬挂
    }
    commandActive_ = false;
    // 写入已经离开当前 command buffer：后续采样由提交顺序保证
    writtenImages_.clear();
    if (wait)
        sampledImages_.clear();
    return submitted;
}

bool VulkanRenderBackend::EnsureStaging(size_t bytes)
{
    if (stagingSize_ >= bytes)
        return true;
    if (stagingBuffer_)
    {
        vkDestroyBuffer(device_, stagingBuffer_, nullptr);
        vkFreeMemory(device_, stagingMemory_, nullptr);
        stagingBuffer_ = VK_NULL_HANDLE;
        stagingMemory_ = VK_NULL_HANDLE;
        stagingMapped_ = nullptr;
        stagingSize_ = 0;
    }
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bytes;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!CheckVkResult(vkCreateBuffer(device_, &bufferInfo, nullptr, &stagingBuffer_), "CreateStagingBuffer"))
        return false;
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device_, stagingBuffer_, &memReq);
    uint32_t memoryType = FindHostVisibleMemory(physicalDevice_, memReq, true);
    if (memoryType == 0xFFFFFFFF)
        return false;
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memoryType;
    if (!CheckVkResult(vkAllocateMemory(device_, &allocInfo, nullptr, &stagingMemory_), "AllocateStagingMemory"))
        return false;
    vkBindBufferMemory(device_, stagingBuffer_, stagingMemory_, 0);
    vkMapMemory(device_, stagingMemory_, 0, memReq.size, 0, (void**)&stagingMapped_);
    stagingSize_ = bytes;
    return true;
}

void VulkanRenderBackend::SaveMeshSlot()
{
    MeshSlot& s = meshSlots_[meshSlot_];
    s.vertexBuffer = meshVertexBuffer_;
    s.vertexMemory = meshVertexMemory_;
    s.vertexMapped = meshVertexMapped_;
    s.vertexCapacity = meshVertexCapacity_;
    s.vertexCursor = meshVertexCursor_;
    s.indexBuffer = meshIndexBuffer_;
    s.indexMemory = meshIndexMemory_;
    s.indexMapped = meshIndexMapped_;
    s.indexCapacity = meshIndexCapacity_;
    s.indexCursor = meshIndexCursor_;
    s.inFlight = meshInFlight_;
}

void VulkanRenderBackend::LoadMeshSlot(int slot)
{
    meshSlot_ = slot;
    MeshSlot& s = meshSlots_[slot];
    meshCommandBuffer_ = s.cmd;
    meshFence_ = s.fence;
    meshVertexBuffer_ = s.vertexBuffer;
    meshVertexMemory_ = s.vertexMemory;
    meshVertexMapped_ = s.vertexMapped;
    meshVertexCapacity_ = s.vertexCapacity;
    meshVertexCursor_ = s.vertexCursor;
    meshIndexBuffer_ = s.indexBuffer;
    meshIndexMemory_ = s.indexMemory;
    meshIndexMapped_ = s.indexMapped;
    meshIndexCapacity_ = s.indexCapacity;
    meshIndexCursor_ = s.indexCursor;
    meshInFlight_ = s.inFlight;
    commandActive_ = false;
    passActive_ = false;
}

void VulkanRenderBackend::AdvanceMeshSlot()
{
    if (!meshReady_)
        return;
    // 防御：若当前槽还有未提交的录制（绘制可能发生在本帧提交点之外），
    // 先提交，避免切槽后这批命令永远留在未提交的命令缓冲里。
    if (commandActive_)
        FlushMeshCommands(false, VK_NULL_HANDLE);
    SaveMeshSlot();
    LoadMeshSlot((meshSlot_ + 1) % kFramesInFlight);
    // 等本槽上一次提交完成：那是 kFramesInFlight 帧前提交的，稳态下已完成，
    // 这正是"CPU 不再被上一帧 GPU 拖住"的关键。首帧/卡顿时才会真的等待。
    WaitMeshIdle();
    writtenImages_.clear();
    sampledImages_.clear();
    meshVertexCursor_ = 0;
    meshIndexCursor_ = 0;
}

bool VulkanRenderBackend::EnsureVertexBuffers(size_t vertexBytes, size_t indexBytes)
{
    // vertexBytes/indexBytes 是"本帧累计所需容量"（调用方传 游标+本批），不是本批大小。
    // 需要扩容时必须先把已录制的命令提交并等待 GPU 完成：旧 buffer/memory 已被
    // 命令缓冲引用，直接 vkDestroy 会让在飞的命令访问已释放内存（UAF）。
    if ((meshVertexCapacity_ < vertexBytes || meshIndexCapacity_ < indexBytes) && commandActive_)
    {
        FlushMeshCommands(true);
        // 提交完成 → WaitMeshIdle 已把游标归零，后续写入从头开始
        if (meshVertexCapacity_ >= vertexBytes && meshIndexCapacity_ >= indexBytes)
            return true;
    }

    auto ensureBuffer = [&](VkBuffer& buffer, VkDeviceMemory& memory, size_t& capacity, size_t bytes,
                            VkBufferUsageFlags usage, uint8_t*& mapped) -> bool {
        if (capacity >= bytes && buffer)
            return true;
        // 预留 2 倍 + 64KB 余量：帧间顶点数小幅波动时不再重建缓冲。
        // 重建必须等 GPU 完成（旧 buffer 可能仍被命令缓冲引用），
        // 精确分配会让"每帧多一点"的负载退化成每帧同步。
        bytes = std::max(bytes * 2, bytes + 64 * 1024);
        if (buffer)
        {
            vkDestroyBuffer(device_, buffer, nullptr);
            vkFreeMemory(device_, memory, nullptr);
            buffer = VK_NULL_HANDLE;
            memory = VK_NULL_HANDLE;
            capacity = 0;
            mapped = nullptr; // 旧映射失效
        }
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bytes;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (!CheckVkResult(vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer), "CreateMeshBuffer"))
            return false;
        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(device_, buffer, &memReq);
        uint32_t memoryType = FindHostVisibleMemory(physicalDevice_, memReq, true);
        if (memoryType == 0xFFFFFFFF)
            return false;
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = memoryType;
        if (!CheckVkResult(vkAllocateMemory(device_, &allocInfo, nullptr, &memory), "AllocateMeshMemory"))
            return false;
        vkBindBufferMemory(device_, buffer, memory, 0);
        capacity = bytes;
        return true;
    };
    if (!ensureBuffer(meshVertexBuffer_, meshVertexMemory_, meshVertexCapacity_, vertexBytes,
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, meshVertexMapped_) ||
        !ensureBuffer(meshIndexBuffer_, meshIndexMemory_, meshIndexCapacity_, indexBytes,
                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT, meshIndexMapped_))
        return false;
    if (!meshVertexMapped_)
        vkMapMemory(device_, meshVertexMemory_, 0, meshVertexCapacity_, 0, (void**)&meshVertexMapped_);
    if (!meshIndexMapped_)
        vkMapMemory(device_, meshIndexMemory_, 0, meshIndexCapacity_, 0, (void**)&meshIndexMapped_);
    SaveMeshSlot(); // 扩容后把新句柄写回当前槽，避免切槽时用回旧 buffer
    return true;
}

//---------------------------------------------------------------------------
// 一般贴图：与窗口贴图同一实现
//---------------------------------------------------------------------------
void* VulkanRenderBackend::CreateTexture(int width, int height)
{
    if (!EnsureMeshResources())
        return nullptr;
    return CreateTextureInternal(width, height);
}

void VulkanRenderBackend::UpdateTexture(void* handle, const uint8_t* pixels, int width, int height, int pitch)
{
    UpdateTextureInternal(FindTexture(handle), pixels, width, height, pitch);
}

void VulkanRenderBackend::DestroyTexture(void* handle)
{
    DestroyTextureInternal(FindTexture(handle));
}

//---------------------------------------------------------------------------
// 绘制状态与网格绘制
//---------------------------------------------------------------------------
void VulkanRenderBackend::SetMask(void* handle)
{
    maskTarget_ = FindTarget(handle);
}

void VulkanRenderBackend::SetBlend(int mode, float opacity, const float* uniformColor,
                                   const float* colorModulation)
{
    // 批绘制状态：DrawQuads 一次提交整批时从此处取 opacity/染色。
    blendOpacity_ = opacity;
    const float white[4] = {1, 1, 1, 1};
    std::memcpy(blendModulation_, colorModulation ? colorModulation : white, sizeof(blendModulation_));
    if (iTVPRenderBackend::IsRMBlend(mode))
        applyRMBlend(mode, opacity, uniformColor);
    else
        applyEmoteBlend(mode, uniformColor);
}

void VulkanRenderBackend::applyEmoteBlend(int mode, const float* uniformColor)
{
    blendMode_ = mode;
    skipDraw_ = (mode == 6);
    enableColor_ = false;
    if (mode == 21 && uniformColor)
    {
        enableColor_ = true;
        std::memcpy(uniformColor_, uniformColor, sizeof(uniformColor_));
    }
}

void VulkanRenderBackend::DrawMesh(const float* vertices,
                                   int vertexCount,
                                   const uint16_t* indices,
                                   int indexCount,
                                   void* handle,
                                   float opacity,
                                   const float* colorModulation)
{
    auto _t = VkProf::Time(VkProf::Get().drawMs);
    if (skipDraw_ || !currentTarget_ || !vertices || !indices || vertexCount <= 0 || indexCount <= 0)
        return;
    if (!EnsureMeshResources())
        return;
    const bool prof = VkProf::Enabled();
    VkProf::Clock::time_point tp0, tpA, tpB, tpC, tp1, tp2, tp3;
    auto dtms = [](VkProf::Clock::time_point a, VkProf::Clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    if (prof)
        tp0 = VkProf::Clock::now();
    Texture* texture = FindTexture(handle);
    Target* targetAsTexture = texture ? nullptr : FindTarget(handle);
    if (!texture && !targetAsTexture)
        return;
    if (prof)
    {
        tpA = VkProf::Clock::now();
        VkProf::Get().drawFindMs += dtms(tp0, tpA);
    }

    // 写后读可见性：只有采样"本 command buffer 内刚写入过的目标图像"时才需要
    // 先提交（跨提交边界保证可见——RADV 忽略 GENERAL→GENERAL 的同提交 barrier）。
    // 普通的新 pass 不再提交等待，否则每帧每个图层都会产生一次 GPU 同步。
    VkImage sampleImage = texture ? texture->image : targetAsTexture->image;
    if (IsImageWritten(sampleImage) || (maskTarget_ && IsImageWritten(maskTarget_->image)))
        FlushMeshCommands();
    if (sampleImage != VK_NULL_HANDLE && !IsImageSampled(sampleImage))
        sampledImages_.push_back(sampleImage);
    if (maskTarget_ && !IsImageSampled(maskTarget_->image))
        sampledImages_.push_back(maskTarget_->image);
    if (prof)
    {
        tpB = VkProf::Clock::now();
        VkProf::Get().drawFlushMs += dtms(tpA, tpB);
    }

    // 顶点/索引缓冲（主机可见，录制期间写入；提交发生在 LockTarget，串行安全）
    size_t vertexBytes = (size_t)vertexCount * 4 * sizeof(float);
    size_t indexBytes = (size_t)indexCount * sizeof(uint16_t);
    // 容量判断必须用"累计游标 + 本批"——缓冲区按帧复用，游标会一直递增，
    // 只按本批大小判断会通过检查却写到缓冲之外（堆越界）。
    // 必须在 BeginPass 之前：扩容会提交并结束当前 pass，之后就不能再录制了。
    if (!EnsureVertexBuffers(meshVertexCursor_ + vertexBytes, meshIndexCursor_ + indexBytes))
        return;
    if (prof)
    {
        tpC = VkProf::Clock::now();
        VkProf::Get().drawEnsureMs += dtms(tpB, tpC);
    }

    if (!passActive_)
    {
        if (!BeginPass(currentTarget_, passClear_))
        {
            TVPConsoleLog("VK DrawMesh: BeginPass failed");
            return;
        }
    }
    if (prof)
    {
        tp1 = VkProf::Clock::now();
        VkProf::Get().drawBeginMs += dtms(tpC, tp1);
    }

    int pipelineIndex = kPipelineNormal;
    switch (blendMode_)
    {
        case 1:
        case 4:
            pipelineIndex = kPipelineMultiply;
            break;
        case 21:
            pipelineIndex = kPipelineColor;
            break;
        default:
            pipelineIndex = kPipelineNormal;
            break;
    }
    vkCmdBindPipeline(meshCommandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipelines_[pipelineIndex]);

    // 写入本帧游标位置，后续 draw 用对应 baseVertex/index 偏移，避免相互覆盖
    std::memcpy(meshVertexMapped_ + meshVertexCursor_, vertices, vertexBytes);
    uint32_t vBase = (uint32_t)(meshVertexCursor_ / 16); // 每顶点 4 float = 16B
    uint16_t* idxDst = (uint16_t*)(meshIndexMapped_ + meshIndexCursor_);
    for (int k = 0; k < indexCount; ++k)
        idxDst[k] = (uint16_t)(indices[k] + vBase);
    uint32_t iBase = (uint32_t)(meshIndexCursor_ / sizeof(uint16_t));
    meshVertexCursor_ += vertexBytes;
    meshIndexCursor_ += indexBytes;
    if (prof)
        tp2 = VkProf::Clock::now();

    // 顶点缓冲整段绑定（偏移 0）：索引里已经加了 vBase（相对本帧起点），
    // 若这里再绑定 vOffset，顶点地址会被偏移两次——本帧第二次及之后的绘制
    // 会读到别的 quad 的顶点（表现为图层被放大/串位）。
    VkDeviceSize bindOffset = 0;
    vkCmdBindVertexBuffers(meshCommandBuffer_, 0, 1, &meshVertexBuffer_, &bindOffset);
    vkCmdBindIndexBuffer(meshCommandBuffer_, meshIndexBuffer_, 0, VK_INDEX_TYPE_UINT16);

    // 描述符：set0 = 纹理（或目标自身）；set1 = 蒙版（无蒙版时用全白 1x1）
    VkDescriptorSet textureSet = texture ? texture->set : targetAsTexture->maskSet;
    VkDescriptorSet sets[2] = {textureSet,
                               maskTarget_ ? maskTarget_->maskSet : blankMask_->set};
    vkCmdBindDescriptorSets(meshCommandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipelineLayout_, 0, 2,
                            sets, 0, nullptr);

    MeshPushConstants pc{};
    std::memcpy(pc.uniformColor, uniformColor_, sizeof(uniformColor_));
    const float whiteModulation[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    std::memcpy(pc.colorModulation, colorModulation ? colorModulation : whiteModulation,
                sizeof(pc.colorModulation));
    pc.viewportX = (float)currentTarget_->width;
    pc.viewportY = (float)currentTarget_->height;
    pc.enableMask = maskTarget_ ? 1.0f : 0.0f;
    pc.enableColor = enableColor_ ? 1.0f : 0.0f;
    pc.opa = opacity;
    vkCmdPushConstants(meshCommandBuffer_, meshPipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc),
                       &pc);

    vkCmdDrawIndexed(meshCommandBuffer_, (uint32_t)indexCount, 1, iBase, 0, 0);
    ++VkProf::Get().drawCalls;
    ++VkProf::Get().quadCount;
    if (prof)
    {
        tp3 = VkProf::Clock::now();
        VkProf::Get().drawWriteMs += dtms(tp1, tp2);
        VkProf::Get().drawCmdMs += dtms(tp2, tp3);
    }
}

void VulkanRenderBackend::DrawQuads(void* handle, const float* vertices, int quadCount,
                                    const uint16_t* indices, int indexCount)
{
    // 批绘制：抽象层契约是"同一 texture / 当前 SetBlend 状态下一批 quad"。
    // VK 实现逐 quad 走 DrawMesh 的成熟路径——描述符/管线/缓冲绑定与单图绘制
    // 完全一致，避免专用批路径在复杂资源生命周期下出现命令缓冲状态失配。
    // 批处理的核心收益在调用方：一次 SetBlend、一段连续顶点数据，CPU 缓存友好。
    if (skipDraw_ || !currentTarget_ || !vertices || quadCount <= 0)
        return;
    const uint16_t defaultIdx[6] = {0, 1, 2, 2, 3, 0};
    const uint16_t* srcIdx = (indices && indexCount >= quadCount * 6) ? indices : defaultIdx;
    for (int q = 0; q < quadCount; ++q)
        DrawMesh(vertices + (size_t)q * 16, 4, srcIdx, 6, handle, blendOpacity_, blendModulation_);
}

//---------------------------------------------------------------------------
// Layer 合成（图层合成路径，软件 RenderManager 语义）
//---------------------------------------------------------------------------
void VulkanRenderBackend::applyRMBlend(int method, float opacity, const float* uniformColor)
{
    layerMethod_ = method;
    layerOpa_ = opacity;
    if (uniformColor)
        std::memcpy(layerUniformColor_, uniformColor, sizeof(layerUniformColor_));
}

void VulkanRenderBackend::DrawRect(void* handle,
                                   float x,
                                   float y,
                                   float w,
                                   float h,
                                   float u0,
                                   float v0,
                                   float u1,
                                   float v1)
{
    auto _t = VkProf::Time(VkProf::Get().drawMs);
    if (!currentTarget_ || !handle)
        return;
    if (!EnsureMeshResources() || !EnsureLayerPipelines())
        return;
    Texture* texture = FindTexture(handle);
    Target* targetAsTexture = texture ? nullptr : FindTarget(handle);
    if (!texture && !targetAsTexture)
        return;

    // 写后读可见性：采样源在本 command buffer 内刚被写入时才需要先提交
    // （与 DrawMesh 相同策略；普通新 pass 不再提交等待）
    VkImage sampleImage = texture ? texture->image : targetAsTexture->image;
    if (IsImageWritten(sampleImage))
        FlushMeshCommands();
    if (sampleImage != VK_NULL_HANDLE && !IsImageSampled(sampleImage))
        sampledImages_.push_back(sampleImage);

    // 顶点/索引缓冲必须在 BeginPass 之前确保容量：扩容会提交并结束当前 pass，
    // 之后继续 vkCmd* 录制会落到已结束的命令缓冲上，整次绘制作废。
    size_t vertexBytes = 16 * sizeof(float);
    size_t indexBytes = 6 * sizeof(uint16_t);
    if (!EnsureVertexBuffers(meshVertexCursor_ + vertexBytes, meshIndexCursor_ + indexBytes))
        return;

    if (!passActive_)
    {
        if (!BeginPass(currentTarget_, passClear_))
        {
            TVPConsoleLog("VK DrawRect: BeginPass failed");
            return;
        }
    }

    // 目标像素坐标 → NDC（与 DrawDeviceD3D 的 Layer 合成路径同一约定：
    // 内容 y 向下，内容顶 t=0 → NDC -1；VK NDC y 向下使回读与 GL/SW 一致）
    float tw = (float)currentTarget_->width, th = (float)currentTarget_->height;
    float lndc = x / tw * 2.0f - 1.0f;
    float tndc = y / th * 2.0f - 1.0f;
    float rndc = (x + w) / tw * 2.0f - 1.0f;
    float bndc = (y + h) / th * 2.0f - 1.0f;
    float vertices[16] = {
        lndc, tndc, u0, v0, //
        rndc, tndc, u1, v0, //
        rndc, bndc, u1, v1, //
        lndc, bndc, u0, v1, //
    };
    uint16_t indices[6] = {0, 1, 2, 2, 3, 0};

    // 方法 → 管线（与 EnsureLayerPipelines 的混合状态一一对应）
    int pipelineIndex = 0;
    switch (layerMethod_)
    {
        case iTVPRenderBackend::BM_ALPHA:
        case iTVPRenderBackend::BM_CONSTALPHA:
            pipelineIndex = 1;
            break;
        case iTVPRenderBackend::BM_ADD_RM:
            pipelineIndex = 3;
            break;
        case iTVPRenderBackend::BM_SUB:
            pipelineIndex = 4;
            break;
        case iTVPRenderBackend::BM_MUL_RM:
            pipelineIndex = 5;
            break;
        case iTVPRenderBackend::BM_MUL_HDA:
            pipelineIndex = 6;
            break;
        case iTVPRenderBackend::BM_COPYCOLOR:
            pipelineIndex = 7;
            break;
        case iTVPRenderBackend::BM_COPYMASK:
            pipelineIndex = 8;
            break;
        default: // BM_COPY / BM_FILL / BM_COPYOPAQUE
            pipelineIndex = 0;
            break;
    }
    vkCmdBindPipeline(meshCommandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, layerPipelines_[pipelineIndex]);

    // 写入本帧游标位置，后续 draw 用对应 baseVertex/index 偏移，避免相互覆盖
    std::memcpy(meshVertexMapped_ + meshVertexCursor_, vertices, vertexBytes);
    uint32_t vBase = (uint32_t)(meshVertexCursor_ / 16); // 每顶点 4 float = 16B
    uint16_t* idxDst = (uint16_t*)(meshIndexMapped_ + meshIndexCursor_);
    for (int k = 0; k < 6; ++k)
        idxDst[k] = (uint16_t)(indices[k] + vBase);
    uint32_t iBase = (uint32_t)(meshIndexCursor_ / sizeof(uint16_t));
    meshVertexCursor_ += vertexBytes;
    meshIndexCursor_ += indexBytes;

    // 顶点缓冲整段绑定（偏移 0）：索引里已经加了 vBase（相对本帧起点），
    // 若这里再绑定 vOffset，顶点地址会被偏移两次——本帧第二次及之后的绘制
    // 会读到别的 quad 的顶点（表现为图层被放大/串位）。
    VkDeviceSize bindOffset = 0;
    vkCmdBindVertexBuffers(meshCommandBuffer_, 0, 1, &meshVertexBuffer_, &bindOffset);
    vkCmdBindIndexBuffer(meshCommandBuffer_, meshIndexBuffer_, 0, VK_INDEX_TYPE_UINT16);

    VkDescriptorSet textureSet = texture ? texture->set : targetAsTexture->maskSet;
    vkCmdBindDescriptorSets(meshCommandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, layerPipelineLayout_, 0, 1,
                            &textureSet, 0, nullptr);

    LayerPushConstants pc{};
    std::memcpy(pc.uniformColor, layerUniformColor_, sizeof(layerUniformColor_));
    pc.opa = layerOpa_;
    pc.method = layerMethod_ - iTVPRenderBackend::BM_COPY;
    vkCmdPushConstants(meshCommandBuffer_, layerPipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(pc), &pc);

    vkCmdDrawIndexed(meshCommandBuffer_, 6, 1, iBase, 0, 0);
    ++VkProf::Get().drawCalls;
    ++VkProf::Get().quadCount;
}

//---------------------------------------------------------------------------
// 回读
//---------------------------------------------------------------------------
uint8_t* VulkanRenderBackend::LockTarget(void* handle, int& pitch)
{
    Target* target = FindTarget(handle);
    return target ? LockTargetInternal(target, pitch) : nullptr;
}

uint8_t* VulkanRenderBackend::LockTargetInternal(Target* target, int& pitch)
{
    auto _t = VkProf::Time(VkProf::Get().lockMs);
    if (!EnsureMeshResources())
        return nullptr;

    EndPass();

    // 渲染 → 回读（GENERAL 布局免转换，仅需内存屏障）
    if (!commandActive_)
    {
        WaitMeshIdle(); // 复用命令缓冲前确认上一次提交完成
        vkResetCommandBuffer(meshCommandBuffer_, 0);
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (!CheckVkResult(vkBeginCommandBuffer(meshCommandBuffer_, &beginInfo), "BeginCommandBuffer"))
            return nullptr;
        commandActive_ = true;
    }
    size_t bytes = (size_t)target->width * target->height * 4;
    if (!EnsureStaging(bytes))
        return nullptr;

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = target->image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(meshCommandBuffer_, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {(uint32_t)target->width, (uint32_t)target->height, 1};
    vkCmdCopyImageToBuffer(meshCommandBuffer_, target->image, VK_IMAGE_LAYOUT_GENERAL, stagingBuffer_, 1,
                           &region);
    if (!CheckVkResult(vkEndCommandBuffer(meshCommandBuffer_), "EndCommandBuffer"))
        return nullptr;
    commandActive_ = false;

    // 同 LockTexture：栅栏未信号时不能 reset，否则等待会立刻返回、读到旧内容
    if (meshInFlight_)
    {
        vkWaitForFences(device_, 1, &meshFence_, VK_TRUE, UINT64_MAX);
        meshInFlight_ = false;
        meshSlots_[meshSlot_].inFlight = false;
    }
    vkResetFences(device_, 1, &meshFence_);
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &meshCommandBuffer_;
    if (!CheckVkResult(vkQueueSubmit(graphicsQueue_, 1, &submitInfo, meshFence_), "QueueSubmit"))
    {
        vkQueueSubmit(graphicsQueue_, 0, nullptr, meshFence_); // 重新置位栅栏，避免后续等待悬挂
        return nullptr;
    }
    vkWaitForFences(device_, 1, &meshFence_, VK_TRUE, UINT64_MAX);
    meshInFlight_ = false;
    meshSlots_[meshSlot_].inFlight = false;
    writtenImages_.clear();
    sampledImages_.clear();
    // 诊断用：环境变量启用全设备等待，排除"fence 只覆盖本提交"导致的读旧数据
    if (VkProf::Enabled())
        vkDeviceWaitIdle(device_);

    pitch = target->width * 4;
    return stagingMapped_;
}

void VulkanRenderBackend::UnlockTarget(void* handle)
{
    (void)handle;
}

uint8_t* VulkanRenderBackend::LockTexture(void* handle, int& pitch)
{
    // 离屏目标的颜色贴图（DrawDevice 合成结果）：走目标那条 image→staging 回读
    for (Target* target : targets_)
    {
        if (target->texture == handle)
            return LockTargetInternal(target, pitch);
    }
    Texture* tex = FindTexture(handle);
    if (!tex)
        return nullptr;
    // 遗留线性贴图：CPU 侧直接可读
    if (tex->mapped)
    {
        pitch = tex->pitch;
        return tex->mapped;
    }
    // 现代 OPTIMAL 贴图：经 image→staging 回读（仅测试截图等低频路径使用）。
    // 先把尚未提交的上传/绘制攒批提交，再在同一个 mesh 命令缓冲里做回读拷贝。
    if (!tex->optimal || !EnsureMeshResources())
        return nullptr;
    EndPass();
    if (!commandActive_)
    {
        WaitMeshIdle();
        vkResetCommandBuffer(meshCommandBuffer_, 0);
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (!CheckVkResult(vkBeginCommandBuffer(meshCommandBuffer_, &beginInfo), "BeginReadbackCmd"))
            return nullptr;
        commandActive_ = true;
    }
    size_t bytes = (size_t)tex->width * tex->height * 4;
    if (!EnsureStaging(bytes))
        return nullptr;
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = tex->image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(meshCommandBuffer_, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {(uint32_t)tex->width, (uint32_t)tex->height, 1};
    vkCmdCopyImageToBuffer(meshCommandBuffer_, tex->image, VK_IMAGE_LAYOUT_GENERAL, stagingBuffer_, 1, &region);
    if (!CheckVkResult(vkEndCommandBuffer(meshCommandBuffer_), "EndReadbackCmd"))
        return nullptr;
    commandActive_ = false;
    // 提交前必须保证栅栏处于已信号状态：本帧可能已有在飞提交（截图发生在帧内），
    // 对未信号栅栏 vkResetFences 是未定义行为，会让随后的等待立刻返回、
    // staging 缓冲保持旧内容（表现为截图永远同一帧）。
    if (meshInFlight_)
    {
        vkWaitForFences(device_, 1, &meshFence_, VK_TRUE, UINT64_MAX);
        meshInFlight_ = false;
    }
    vkResetFences(device_, 1, &meshFence_);
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &meshCommandBuffer_;
    if (!CheckVkResult(vkQueueSubmit(graphicsQueue_, 1, &submitInfo, meshFence_), "QueueReadback"))
        return nullptr;
    vkWaitForFences(device_, 1, &meshFence_, VK_TRUE, UINT64_MAX);
    meshInFlight_ = false;
    meshSlots_[meshSlot_].inFlight = false;
    writtenImages_.clear();
    sampledImages_.clear();
    pitch = tex->width * 4;
    return stagingMapped_;
}

void* VulkanRenderBackend::GetTargetTexture(void* handle)
{
    // 返回 set0 布局的采样包装（DrawMesh / DrawWindowTexture 按 Texture* 句柄查找）
    Target* target = FindTarget(handle);
    return target ? target->texture : nullptr;
}

void VulkanRenderBackend::UpdateTargetTexture(void* handle,
                                              const uint8_t* pixels,
                                              int width,
                                              int height,
                                              int pitch)
{
    Target* target = FindTarget(handle);
    if (!target || !pixels)
        return;
    size_t bytes = (size_t)width * height * 4;

    // 目标图像是设备本地内存：经 staging buffer + vkCmdCopyBufferToImage 上传
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bytes;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!CheckVkResult(vkCreateBuffer(device_, &bufferInfo, nullptr, &staging), "CreateUploadStaging"))
        return;
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device_, staging, &memReq);
    uint32_t memoryType = FindHostVisibleMemory(physicalDevice_, memReq, true);
    if (memoryType == 0xFFFFFFFF)
    {
        vkDestroyBuffer(device_, staging, nullptr);
        return;
    }
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memoryType;
    if (!CheckVkResult(vkAllocateMemory(device_, &allocInfo, nullptr, &stagingMem), "AllocateUploadStaging"))
    {
        vkDestroyBuffer(device_, staging, nullptr);
        return;
    }
    vkBindBufferMemory(device_, staging, stagingMem, 0);
    void* mapped = nullptr;
    vkMapMemory(device_, stagingMem, 0, memReq.size, 0, &mapped);
    for (int y = 0; y < height; y++)
        std::memcpy((uint8_t*)mapped + (size_t)y * width * 4, pixels + (size_t)y * pitch,
                    (size_t)width * 4);
    vkUnmapMemory(device_, stagingMem);

    VkCommandBufferAllocateInfo cmdAlloc{};
    cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAlloc.commandPool = commandPool_;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;
    VkCommandBuffer cmd;
    if (!CheckVkResult(vkAllocateCommandBuffers(device_, &cmdAlloc, &cmd), "AllocateUploadCmd"))
    {
        vkFreeMemory(device_, stagingMem, nullptr);
        vkDestroyBuffer(device_, staging, nullptr);
        return;
    }
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {(uint32_t)width, (uint32_t)height, 1};
    vkCmdCopyBufferToImage(cmd, staging, target->image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue_);
    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
    vkFreeMemory(device_, stagingMem, nullptr);
    vkDestroyBuffer(device_, staging, nullptr);
}

//---------------------------------------------------------------------------
// 清理
//---------------------------------------------------------------------------
void VulkanRenderBackend::Shutdown()
{
    if (!initialized_ && instance_ == VK_NULL_HANDLE)
        return;
    if (device_ != VK_NULL_HANDLE)
        vkDeviceWaitIdle(device_);

    // 2D 网格目标
    for (Target* t : targets_)
    {
        // 先回收目标的"采样包装"：它与 target 共享 image/view（memory 不拥有）。
        // 若不在这里摘除，下面的 textures_ 循环会对同一个 view/image 再销毁一次
        // （double free），在退出阶段表现为堆损坏。
        if (t->texture)
        {
            for (size_t i = 0; i < textures_.size(); i++)
            {
                if (textures_[i] == t->texture)
                {
                    textures_.erase(textures_.begin() + i);
                    textureIndex_.erase(t->texture);
                    break;
                }
            }
            if (t->texture->set)
                vkFreeDescriptorSets(device_, descriptorPool_, 1, &t->texture->set);
            delete t->texture; // 不销毁 image/view/memory（归 target）
            t->texture = nullptr;
        }
        if (t->maskSet)
            vkFreeDescriptorSets(device_, descriptorPool_, 1, &t->maskSet);
        if (t->framebuffer)
            vkDestroyFramebuffer(device_, t->framebuffer, nullptr);
        if (t->view)
            vkDestroyImageView(device_, t->view, nullptr);
        if (t->memory)
            vkFreeMemory(device_, t->memory, nullptr);
        if (t->image)
            vkDestroyImage(device_, t->image, nullptr);
        delete t;
    }
    targets_.clear();
    targetIndex_.clear();

    // 贴图（窗口贴图与一般贴图共用同一列表）
    for (Texture* tex : textures_)
    {
        if (tex->set)
            vkFreeDescriptorSets(device_, descriptorPool_, 1, &tex->set);
        if (tex->view)
            vkDestroyImageView(device_, tex->view, nullptr);
        if (tex->memory)
            vkFreeMemory(device_, tex->memory, nullptr);
        if (tex->image)
            vkDestroyImage(device_, tex->image, nullptr);
        delete tex;
    }
    textures_.clear();
    textureIndex_.clear();
    blankMask_ = nullptr;

    if (device_ != VK_NULL_HANDLE)
    {
        // 2D 网格资源
        if (stagingBuffer_)
            vkDestroyBuffer(device_, stagingBuffer_, nullptr);
        if (stagingMemory_)
            vkFreeMemory(device_, stagingMemory_, nullptr);
        stagingBuffer_ = VK_NULL_HANDLE;
        stagingMemory_ = VK_NULL_HANDLE;
        stagingMapped_ = nullptr;
        stagingSize_ = 0;
        // 上传暂存环
        if (uploadRingBuffer_)
        {
            vkUnmapMemory(device_, uploadRingMemory_);
            vkDestroyBuffer(device_, uploadRingBuffer_, nullptr);
            vkFreeMemory(device_, uploadRingMemory_, nullptr);
            uploadRingBuffer_ = VK_NULL_HANDLE;
            uploadRingMemory_ = VK_NULL_HANDLE;
            uploadRingMapped_ = nullptr;
            uploadRingRegionSize_ = 0;
            uploadRingCursor_ = 0;
            uploadRingRegion_ = 0;
        }
        // 释放所有帧流水槽（每槽各有一套命令缓冲/栅栏/顶点索引缓冲）
        for (int i = 0; i < kFramesInFlight; i++)
        {
            MeshSlot& s = meshSlots_[i];
            if (s.indexBuffer)
                vkDestroyBuffer(device_, s.indexBuffer, nullptr);
            if (s.indexMemory)
                vkFreeMemory(device_, s.indexMemory, nullptr);
            s.indexBuffer = VK_NULL_HANDLE;
            s.indexMemory = VK_NULL_HANDLE;
            s.indexMapped = nullptr;
            s.indexCapacity = 0;
            s.indexCursor = 0;
            if (s.vertexBuffer)
                vkDestroyBuffer(device_, s.vertexBuffer, nullptr);
            if (s.vertexMemory)
                vkFreeMemory(device_, s.vertexMemory, nullptr);
            s.vertexBuffer = VK_NULL_HANDLE;
            s.vertexMemory = VK_NULL_HANDLE;
            s.vertexMapped = nullptr;
            s.vertexCapacity = 0;
            s.vertexCursor = 0;
            if (s.fence)
                vkDestroyFence(device_, s.fence, nullptr);
            s.fence = VK_NULL_HANDLE;
            s.cmd = VK_NULL_HANDLE; // 随 commandPool 一起回收
            s.inFlight = false;
        }
        meshVertexBuffer_ = VK_NULL_HANDLE;
        meshVertexMemory_ = VK_NULL_HANDLE;
        meshVertexCapacity_ = 0;
        meshVertexMapped_ = nullptr;
        meshIndexBuffer_ = VK_NULL_HANDLE;
        meshIndexMemory_ = VK_NULL_HANDLE;
        meshIndexCapacity_ = 0;
        meshIndexMapped_ = nullptr;
        meshCommandBuffer_ = VK_NULL_HANDLE;
        meshFence_ = VK_NULL_HANDLE;
        if (meshDone_)
            vkDestroySemaphore(device_, meshDone_, nullptr);
        meshDone_ = VK_NULL_HANDLE;
        meshInFlight_ = false;
        for (int i = 0; i < 3; i++)
        {
            if (meshPipelines_[i])
                vkDestroyPipeline(device_, meshPipelines_[i], nullptr);
            meshPipelines_[i] = VK_NULL_HANDLE;
        }
        if (meshPipelineLayout_)
            vkDestroyPipelineLayout(device_, meshPipelineLayout_, nullptr);
        meshPipelineLayout_ = VK_NULL_HANDLE;
        // Layer 合成管线
        for (int i = 0; i < 9; i++)
        {
            if (layerPipelines_[i])
                vkDestroyPipeline(device_, layerPipelines_[i], nullptr);
            layerPipelines_[i] = VK_NULL_HANDLE;
        }
        if (layerPipelineLayout_)
            vkDestroyPipelineLayout(device_, layerPipelineLayout_, nullptr);
        layerPipelineLayout_ = VK_NULL_HANDLE;
        layerReady_ = false;
        if (meshLoadPass_)
            vkDestroyRenderPass(device_, meshLoadPass_, nullptr);
        if (meshClearPass_)
            vkDestroyRenderPass(device_, meshClearPass_, nullptr);
        meshClearPass_ = meshLoadPass_ = VK_NULL_HANDLE;
        if (maskSetLayout_)
            vkDestroyDescriptorSetLayout(device_, maskSetLayout_, nullptr);
        maskSetLayout_ = VK_NULL_HANDLE;

        // 窗口资源
        if (commandPool_)
            vkDestroyCommandPool(device_, commandPool_, nullptr);
        commandPool_ = VK_NULL_HANDLE;
        frameCommandBuffer_ = VK_NULL_HANDLE;
        meshCommandBuffer_ = VK_NULL_HANDLE;
        if (frameFence_)
            vkDestroyFence(device_, frameFence_, nullptr);
        if (imageReady_)
            vkDestroySemaphore(device_, imageReady_, nullptr);
        if (renderFinished_)
            vkDestroySemaphore(device_, renderFinished_, nullptr);
        if (sampler_)
            vkDestroySampler(device_, sampler_, nullptr);
        if (windowVertexBuffer_)
            vkDestroyBuffer(device_, windowVertexBuffer_, nullptr);
        if (windowVertexMemory_)
            vkFreeMemory(device_, windowVertexMemory_, nullptr);
        if (windowIndexBuffer_)
            vkDestroyBuffer(device_, windowIndexBuffer_, nullptr);
        if (windowIndexMemory_)
            vkFreeMemory(device_, windowIndexMemory_, nullptr);
        if (descriptorPool_)
            vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        if (textureSetLayout_)
            vkDestroyDescriptorSetLayout(device_, textureSetLayout_, nullptr);
        if (windowPipeline_)
            vkDestroyPipeline(device_, windowPipeline_, nullptr);
        if (windowPipelineLayout_)
            vkDestroyPipelineLayout(device_, windowPipelineLayout_, nullptr);
        if (windowRenderPass_)
            vkDestroyRenderPass(device_, windowRenderPass_, nullptr);
        DestroySwapchain();
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (surface_)
    {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
    if (instance_)
    {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
    initialized_ = false;
    meshReady_ = false;
    currentTarget_ = nullptr;
    maskTarget_ = nullptr;
    passActive_ = false;
}

//---------------------------------------------------------------------------
// 工厂与注册
//---------------------------------------------------------------------------
iTVPRenderBackend* CreateVulkanRenderBackend(VkInstance _instance, VkSurfaceKHR _surface)
{
    VulkanRenderBackend* backend = new VulkanRenderBackend(_instance, _surface);
    if (!backend->Initialize())
    {
        delete backend;
        return nullptr;
    }
    return backend;
}

bool VulkanRenderBackendAvailable()
{
    // 编译进来即可用。真正的 loader/驱动探测在 Initialize() 中完成
    // （SDL_Vulkan_LoadLibrary 需在 SDL_Init 视频子系统之后调用，
    //  而参数解析阶段的 probe 运行在 SDL_Init 之前）。
    return true;
}

namespace
{
struct VulkanRenderBackendAutoRegister
{
    VulkanRenderBackendAutoRegister()
    {
        TVPRenderBackendDesc desc;
        desc.name = "vulkan";
        desc.description = "Vulkan 1.0 (via SDL3 surface)";
        desc.probe = VulkanRenderBackendAvailable;
        desc.create = nullptr;
        TVPRegisterRenderBackend(desc);
    }
} gVulkanRenderBackendAutoRegister;
} // namespace

} // namespace krkrsdl3
