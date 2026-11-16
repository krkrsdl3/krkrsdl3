#pragma once

#include <vector>

#include "TVPCompositor.h"

//---------------------------------------------------------------------------
// 软件渲染后端（合并实现）
//
// 一个类同时承担两个角色（同一接口内，见 RenderBackend.h）：
//   - 窗口贴图合成（上屏）。窗口贴图使用 SDL_Texture，
//     经 PlatformView.h 平台钩子（各平台入口实现 SDL Renderer 细节）。
//   - 2D 网格渲染：一般贴图（CPU buffer）+ 离屏网格光栅化，由
//     cpp/plugins/emoteplayer/emoterunner.cpp 的 drawSoftware 迁移而来
//     （edge function + 仿射纹理步进，混合语义与插件原实现完全一致）。
//
// 软件后端下窗口贴图（SDL_Texture）与一般贴图（CPU buffer）实现不同，
// 因此接口上显式区分（*WindowTexture vs *Texture）；GPU 后端则共用同一实现。
//
// 可用性由平台入口的 TVPSoftwareRenderBackendAvailable() 决定
// （OHOS 无 SDL 软渲染路径，返回 false）。
//---------------------------------------------------------------------------
namespace krkrsdl3
{
class SWRenderBackend : public iTVPRenderBackend
{
public:
    SWRenderBackend() = default;
    ~SWRenderBackend() override;

    const char* GetName() const override { return "software"; }
    bool IsHardware() const override { return false; }

    // ---- 窗口贴图（SDL_Texture，经平台钩子）----
    void BeginFrame(int winWidth, int winHeight) override;
    void EndFrame() override;
    void* CreateWindowTexture(int width, int height) override;
    void UpdateWindowTexture(void* handle, const uint8_t* buff, int width, int height, int pitch) override;
    void DestroyWindowTexture(void* handle) override;
    void DrawWindowTexture(void* handle, float posX, float posY, float width, float height) override;

    // ---- 2D 网格渲染（一般贴图：CPU buffer；目标 + 光栅化）----
    void* CreateTarget(int width, int height) override;
    void DestroyTarget(void* target) override;
    void SetTarget(void* target) override;
    void ClearTarget(bool clearColor) override;
    uint8_t* LockTarget(void* target, int& pitch) override;
    void UnlockTarget(void* target) override;
    void* GetTargetTexture(void* target) override;
    void UpdateTargetTexture(void* target, const uint8_t* pixels, int width, int height, int pitch) override;
    void* CreateTexture(int width, int height) override;
    void UpdateTexture(void* texture, const uint8_t* pixels, int width, int height, int pitch) override;
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
    struct Target
    {
        std::vector<uint8_t> pixels;
        int width = 0, height = 0;
        bool opaque = false;
    };
    struct Texture
    {
        std::vector<uint8_t> pixels;
        int width = 0, height = 0;
        bool opaque = false;
    };

    Target* FindTarget(void* handle) const;
    Texture* FindTexture(void* handle) const;

    Target* currentTarget_ = nullptr;
    Target* maskTarget_ = nullptr;
    int blendMode_ = 0;
    bool skipDraw_ = false;
    float uniformColor_[4] = {0, 0, 0, 0};
    int layerMethod_ = 0;    // LayerBlendMethod
    int layerOpa8_ = 255;    // 0..255（软件 opacity 参数）
    float layerColor_[4] = {0, 0, 0, 0};
    std::vector<Target*> targets_;
    std::vector<Texture*> textures_;
    std::vector<int> sampleColumns_;
};
} // namespace krkrsdl3
