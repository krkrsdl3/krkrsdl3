#include "tjsCommHead.h"
#include "TVPCompositor.h"
#include "Platform.h"

#include <algorithm>
#include <vector>

//---------------------------------------------------------------------------
// 窗口贴图合成调度器
//
// 职责：
//   1. 维护当前渲染后端（见 backend/RenderBackend.h，GL/Vulkan/SW 等实现）
//   2. 维护参与合成的贴图列表（窗口 sprite + overlay）
//   3. 统一计算 letterbox 变换（scale/xPos/yPos），再交给后端绘制
//
// 历史说明：本文件原为 GL 直写实现（shader/纹理/绘制），
// GL 代码已迁移至 backend/GLRenderBackend.cpp，此处不再包含任何图形 API 调用。
//---------------------------------------------------------------------------
static std::vector<TVPSprite*> renderTexture;

namespace krkrsdl3
{
//---------------------------------------------------------------------------
// 后端注册表
// （函数内静态变量：避免跨编译单元的静态初始化顺序问题）
//---------------------------------------------------------------------------
static std::vector<TVPRenderBackendDesc>& GetRegistry()
{
    static std::vector<TVPRenderBackendDesc> registry;
    return registry;
}
static iTVPRenderBackend*& GetCurrentBackendRef()
{
    static iTVPRenderBackend* backend = nullptr;
    return backend;
}

static bool& GetStretchModeRef()
{
    static bool stretch = false;
    return stretch;
}

void TVPSetStretchMode(bool enable)
{
    GetStretchModeRef() = enable;
}

bool TVPGetStretchMode()
{
    return GetStretchModeRef();
}

void TVPRegisterRenderBackend(const TVPRenderBackendDesc& desc)
{
    if (!desc.name)
        return;
    auto& registry = GetRegistry();
    for (const auto& existing : registry)
    {
        if (std::string(existing.name) == desc.name)
            return; // 已注册，去重
    }
    registry.push_back(desc);
}

std::vector<std::string> TVPListRenderBackends()
{
    std::vector<std::string> result;
    for (const auto& desc : GetRegistry())
    {
        if (desc.probe && !desc.probe())
            continue;
        result.push_back(desc.name);
    }
    return result;
}

bool TVPRenderBackendAvailable(const std::string& name)
{
    for (const auto& desc : GetRegistry())
    {
        if (desc.name == name)
            return !desc.probe || desc.probe();
    }
    return false;
}

//---------------------------------------------------------------------------
// 全局当前后端
//---------------------------------------------------------------------------
void TVPSetRenderBackend(iTVPRenderBackend* backend)
{
    GetCurrentBackendRef() = backend;
}

iTVPRenderBackend* TVPGetRenderBackend()
{
    return GetCurrentBackendRef();
}

void TVPShutdownRenderBackend()
{
    delete GetCurrentBackendRef();
    GetCurrentBackendRef() = nullptr;
}

// 合成器全局信息采集（转发给当前后端，GL 后端输出厂商/版本/扩展日志）
void fetchGLInfo()
{
    iTVPRenderBackend* backend = TVPGetRenderBackend();
    if (backend)
        backend->FetchInfo();
}

// 素材加入渲染
void TVPJoinTexture(TVPSprite* sp)
{
    renderTexture.push_back(sp);
}

// 素材离开渲染
void TVPDepartTexture(TVPSprite* sp)
{
    for (size_t i = 0; i < renderTexture.size(); i++)
    {
        if (renderTexture.at(i)->texture == sp->texture)
        {
            renderTexture.erase(renderTexture.begin() + i);
            break;
        }
    }
}

// 统一 letterbox 计算：保持宽高比地缩放并居中（所有后端共用同一套行为）
static void TVPCalcLetterbox(TVPSprite* sp, int winWidth, int winHeight)
{
    float currScale = std::min(((float)winWidth) / sp->width, ((float)winHeight) / sp->height);
    sp->scale = currScale;
    sp->xPos = (winWidth - currScale * sp->width) / 2.0f;
    sp->yPos = (winHeight - currScale * sp->height) / 2.0f;
}

void TVPRenderOnce(int winWidth, int winHeight)
{
    iTVPRenderBackend* backend = TVPGetRenderBackend();
    if (!backend)
        return;

    backend->BeginFrame(winWidth, winHeight);

    const bool stretch = TVPGetStretchMode();

    // 绘制 currentSprite
    TVPSprite* retSpr = KRKR_Get_Current_Sprite();
    if (retSpr && retSpr->texture)
    {
        if (stretch)
        {
            backend->DrawWindowTexture(retSpr->texture, 0.0f, 0.0f,
                                       (float)winWidth, (float)winHeight);
        }
        else
        {
            TVPCalcLetterbox(retSpr, winWidth, winHeight);
            backend->DrawWindowTexture(retSpr->texture, retSpr->xPos, retSpr->yPos,
                                       retSpr->scale * retSpr->width, retSpr->scale * retSpr->height);
        }
    }

    // 绘制 overlay
    for (auto texture : renderTexture)
    {
        if (texture->isVisible && texture->type == 2 && texture->texture)
        {
            if (stretch)
            {
                backend->DrawWindowTexture(texture->texture, 0.0f, 0.0f,
                                           (float)winWidth, (float)winHeight);
            }
            else
            {
                TVPCalcLetterbox(texture, winWidth, winHeight);
                backend->DrawWindowTexture(texture->texture, texture->xPos, texture->yPos,
                                           texture->scale * texture->width, texture->scale * texture->height);
            }
        }
    }

    backend->EndFrame();
}

// 创建素材
void TVPCreateTexture(TVPSprite& sp)
{
    iTVPRenderBackend* backend = TVPGetRenderBackend();
    if (!backend)
        return;
    sp.texture = backend->CreateWindowTexture(sp.width, sp.height);
}

// 更新素材
void TVPUpdateTexture(TVPSprite* sp, uint8_t* buff, int width, int height, int pitch)
{
    iTVPRenderBackend* backend = TVPGetRenderBackend();
    if (!backend || !sp->texture)
        return;
    backend->UpdateWindowTexture(sp->texture, buff, width, height, pitch);
}

// 销毁素材
void TVPDestroyTexture(TVPSprite* sp)
{
    iTVPRenderBackend* backend = TVPGetRenderBackend();
    // 借用的纹理（GPU 别名）不由 sprite 销毁，只解除引用
    if (backend && sp->texture && !sp->borrowedTexture)
        backend->DestroyWindowTexture(sp->texture);
    sp->texture = nullptr;
    sp->borrowedTexture = false;
}

// TODO 或许应该和window整合起来管理
void TVPClearAllTexture()
{
    for (auto sp : renderTexture)
    {
        TVPDestroyTexture(sp);
    }
    renderTexture.clear();
}

void iTVPRenderBackend::DrawQuads(void* texture,
                                  const float* vertices,
                                  int quadCount,
                                  const uint16_t* indices,
                                  int indexCount)
{
    // 默认回退：逐 quad 调用 DrawMesh（GL/SW 立即模式无批量开销，行为不变）。
    // 顶点布局按每 quad 4 个交错 (x,y,u,v)；索引默认 (0,1,2,2,3,0)。
    if (!vertices || quadCount <= 0)
        return;
    // opacity / 染色属于"当前混合状态"（Batch 接口不带这两个参数）：
    // 必须取 SetBlend 记录的值，不能写死 1.0，否则半透明图层会被画成不透明。
    const float opacity = GetBlendOpacity();
    const float* modulation = GetBlendModulation();
    const uint16_t defaultIdx[6] = {0, 1, 2, 2, 3, 0};
    const uint16_t* idx = indices && indexCount > 0 ? indices : defaultIdx;
    const int nIdx = indices && indexCount > 0 ? indexCount : 6;
    for (int q = 0; q < quadCount; ++q)
    {
        const float* v = vertices + (size_t)q * 16;
        DrawMesh(v, 4, idx, nIdx, texture, opacity, modulation);
    }
}

} // namespace krkrsdl3
