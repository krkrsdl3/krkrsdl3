//---------------------------------------------------------------------------
// DrawDeviceD2Dm：软件多图层绘制设备
//
// 原版 DrawDeviceD2Dm.dll 提供两项能力：
//   1. 一个绘制设备可以挂多个 LayerManager（ZoomView 的根图层独占一个）
//   2. Layer.viewState：每个 LayerManager 的取景矩形与缩放
//
// 这里用软件路径实现：
//   - 每个 manager 渲染到自己的 DrawBuffer（沿用引擎既有逻辑）
//   - 合成时主 manager 的 DrawBuffer 作底，其余 manager 按 viewState
//     的矩形拉伸（bmAlpha）叠加，再交给 Window 呈现
//   - Layer.viewState 用 Dictionary + 原生 setRect 提供
//
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include "ncbind/ncbind.hpp"
#include "DrawDevice.h"
#include "LayerManager.h"
#include "TVPWindow.h"
#include "LayerBitmap.h"
#include "TVPCompositor.h"
#include "tjsDictionary.h"
#include "tjsNative.h"
#include "tjsNativeLayer.h"

#include <map>
#include <vector>

#define NCB_MODULE_NAME TJS_N("DrawDeviceD2D.dll")

//---------------------------------------------------------------------------
// Layer.viewState
//---------------------------------------------------------------------------
namespace viewstate
{

//! 每个 Layer 的 TJS 对象对应一个 viewState 字典
static std::map<iTJSDispatch2*, iTJSDispatch2*> ViewStates;

//! 取景矩形（key 为原生图层指针）
struct ViewRect
{
    tjs_int X = 0, Y = 0, W = 0, H = 0;
};
static std::map<iTJSDispatch2*, ViewRect> ViewRects;

//! 从 TJS 对象取原生图层
static tTJSNI_BaseLayer* NativeLayer(iTJSDispatch2* obj)
{
    if (!obj)
        return nullptr;
    tTJSNI_BaseLayer* layer = nullptr;
    if (TJS_FAILED(obj->NativeInstanceSupport(TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                                              (iTJSNativeInstance**)&layer)))
        return nullptr;
    return layer;
}

//! viewState.setRect(x, y, x2, y2, type)：记录目标矩形
static tjs_error SetRect(tTJSVariant* result, tjs_int numparams, tTJSVariant** param,
                         iTJSDispatch2* objthis)
{
    if (result)
        result->Clear();
    if (numparams < 4)
        return TJS_S_OK;

    tTJSNI_BaseLayer* layer = NativeLayer(objthis);
    if (!layer)
        return TJS_S_OK;

    ViewRect& vr = ViewRects[(iTJSDispatch2*)layer];
    vr.X = (tjs_int)*param[0];
    vr.Y = (tjs_int)*param[1];
    vr.W = (tjs_int)*param[2] - vr.X;
    vr.H = (tjs_int)*param[3] - vr.Y;
    return TJS_S_OK;
}

//! 新建 viewState 字典
static iTJSDispatch2* Create()
{
    iTJSDispatch2* d = TJSCreateDictionaryObject();
    if (!d)
        return nullptr;

    const struct
    {
        const tjs_char* Name;
        tjs_int Value;
    } init[] = {
        {TJS_N("visible"), 1}, {TJS_N("opacity"), 255}, {TJS_N("zinterp"), 0}, {TJS_N("order"), 0},
    };
    for (const auto& kv : init)
    {
        tTJSVariant v(kv.Value);
        d->PropSet(TJS_MEMBERENSURE, kv.Name, NULL, &v, d);
    }

    iTJSDispatch2* fn = TJSCreateNativeClassMethod(&SetRect);
    if (fn)
    {
        tTJSVariant fv(fn);
        d->PropSet(TJS_MEMBERENSURE, TJS_N("setRect"), NULL, &fv, d);
        fn->Release();
    }
    return d;
}

//! Layer.viewState 的 getter（objthis 是 Layer 的 TJS 对象）
static tjs_error GetViewState(tTJSVariant* result, tjs_int /*numparams*/, tTJSVariant** /*param*/,
                              iTJSDispatch2* objthis)
{
    if (!result)
        return TJS_S_OK;
    result->Clear();
    if (!objthis)
        return TJS_S_OK;

    auto it = ViewStates.find(objthis);
    if (it != ViewStates.end())
    {
        *result = it->second;
        return TJS_S_OK;
    }

    iTJSDispatch2* d = Create();
    if (!d)
        return TJS_S_OK;

    objthis->AddRef();
    d->AddRef();
    ViewStates[objthis] = d;
    *result = d;
    return TJS_S_OK;
}

//! 取某个原生图层记录的目标矩形
static bool GetRect(iTJSDispatch2* layerKey, tjs_int& x, tjs_int& y, tjs_int& w, tjs_int& h)
{
    auto it = ViewRects.find(layerKey);
    if (it == ViewRects.end())
        return false;
    x = it->second.X;
    y = it->second.Y;
    w = it->second.W;
    h = it->second.H;
    return w > 0 && h > 0;
}

} // namespace viewstate

//! NCB_ATTACH_CLASS 需要一个类名（本插件不挂实例数据，仅用于属性注册）
class LayerViewState
{
public:
    LayerViewState() {}
};

NCB_ATTACH_CLASS(LayerViewState, Layer)
{
    // 只读属性：Layer.viewState
    RawCallback(TJS_N("viewState"), &viewstate::GetViewState, 0, 0);
}

//---------------------------------------------------------------------------
// 软件多图层绘制设备
//---------------------------------------------------------------------------
class DrawDeviceD2D : public iTVPDrawDevice
{
    std::vector<iTVPLayerManager*> Managers;

    TVPWindow* Window = nullptr;
    tTVPRect DestRect;

    tTVPDestTexture* Composite = nullptr; //!< 合成目标
    void* ScratchTexture = nullptr;       //!< 呈现用贴图
    tjs_int ScratchW = 0, ScratchH = 0;

    tjs_int Width = 0, Height = 0; //!< 画面（虚拟）尺寸

    tjs_int LayerIndex = 0;
    tjs_int StretchType = 0;
    tjs_real BicubicParam = 0.5;
    tjs_real TransState = 0.0;
    tjs_int MaskMode = 0;
    tjs_int ClearColor = 0xFF000000;

public:
    DrawDeviceD2D(tjs_int w, tjs_int h)
      : Width(w),
        Height(h)
    {
        DestRect = tTVPRect(0, 0, w, h);
    }

    //! 无参构造（used2d.tjs 的 create() 不带参数）
    DrawDeviceD2D()
      : Width(1920),
        Height(1080)
    {
        DestRect = tTVPRect(0, 0, Width, Height);
    }

    ~DrawDeviceD2D()
    {
        for (auto* m : Managers)
            m->Release();
        Managers.clear();
        delete Composite;
        Composite = nullptr;
    }

    //---- 脚本侧 API ----
    tjs_int getWidth() const { return Width; }
    void setWidth(tjs_int w) { Width = w; }
    tjs_int getHeight() const { return Height; }
    void setHeight(tjs_int h) { Height = h; }
    void setSize(tjs_int w, tjs_int h)
    {
        Width = w;
        Height = h;
    }
    tjs_int64 getInterface() { return (tjs_int64)(tjs_intptr_t)(iTVPDrawDevice*)this; }

    void setScreenRect(tjs_int /*x*/, tjs_int /*y*/, tjs_int w, tjs_int h)
    {
        Width = w;
        Height = h;
    }
    void setPrimarySize(tjs_int w, tjs_int h)
    {
        Width = w;
        Height = h;
    }
    void setOffset(tjs_int /*x*/, tjs_int /*y*/) {}

    void update(tjs_real /*diff*/)
    {
        Tick();
    }
    void capture(iTJSDispatch2* /*layer*/, tjs_int /*index*/) {}
    void startTransition() { TransState = 1.0; }
    void stopTransition() { TransState = 0.0; }
    void recreate() {}
    void finalize() {}
    tjs_int checkEnable() { return 1; }
    tTJSVariant getModule(tTJSString /*name*/) { return tTJSVariant(); }

    tTJSVariant getPrimaryLayers()
    {
        iTJSDispatch2* arr = TJSCreateArrayObject();
        tjs_int idx = 0;
        for (auto* m : Managers)
        {
            tTJSNI_BaseLayer* pri = m ? m->GetPrimaryLayer() : nullptr;
            if (!pri)
                continue;
            iTJSDispatch2* owner = pri->GetOwnerNoAddRef();
            if (!owner)
                continue;
            tTJSVariant v(owner, owner);
            arr->PropSetByNum(TJS_MEMBERENSURE, idx++, &v, arr);
        }
        return tTJSVariant(arr, arr);
    }

    tjs_int getLayerManagerIndex() const { return LayerIndex; }
    void setLayerManagerIndex(tjs_int index) { LayerIndex = index; }
    tjs_int getStretchType() const { return StretchType; }
    void setStretchType(tjs_int t) { StretchType = t; }
    tjs_real getBicubicParam() const { return BicubicParam; }
    void setBicubicParam(tjs_real p) { BicubicParam = p; }
    tjs_real getTransState() const { return TransState; }
    void setTransState(tjs_real s) { TransState = s; }
    tjs_int getMaskMode() const { return MaskMode; }
    void setMaskMode(tjs_int m) { MaskMode = m; }
    tjs_int getClearColor() const { return ClearColor; }
    void setClearColor(tjs_int c) { ClearColor = c; }

    //---- iTVPDrawDevice ----
    void Destruct() override;

    void SetWindowInterface(TVPWindow* window) override
    {
        Window = window;
    }

    void AddLayerManager(iTVPLayerManager* manager) override;
    void RemoveLayerManager(iTVPLayerManager* manager) override;

    void SetDestRectangle(const tTVPRect& rect) override { DestRect = rect; }
    void SetWindowSize(tjs_int /*w*/, tjs_int /*h*/) override {}
    void SetClipRectangle(const tTVPRect& /*rect*/) override {}
    void GetSrcSize(tjs_int& w, tjs_int& h) override;

    void NotifyLayerResize(iTVPLayerManager* /*manager*/) override
    {
        if (Window)
            Window->NotifySrcResize();
    }
    void NotifyLayerImageChange(iTVPLayerManager* /*manager*/) override
    {
        // 层内容变化 → 请求窗口更新，否则 UpdateContent 不会触发
        if (Window)
            Window->RequestUpdate();
    }

    void OnClick(tjs_int x, tjs_int y) override
    {
        if (iTVPLayerManager* m = Primary())
            m->NotifyClick(x, y);
    }
    void OnDoubleClick(tjs_int x, tjs_int y) override
    {
        if (iTVPLayerManager* m = Primary())
            m->NotifyDoubleClick(x, y);
    }
    void OnMouseDown(tjs_int x, tjs_int y, tTVPMouseButton mb, tjs_uint32 flags) override
    {
        if (iTVPLayerManager* m = Primary())
            m->NotifyMouseDown(x, y, mb, flags);
    }
    void OnMouseUp(tjs_int x, tjs_int y, tTVPMouseButton mb, tjs_uint32 flags) override
    {
        if (iTVPLayerManager* m = Primary())
            m->NotifyMouseUp(x, y, mb, flags);
    }
    void OnMouseMove(tjs_int x, tjs_int y, tjs_uint32 flags) override
    {
        if (iTVPLayerManager* m = Primary())
            m->NotifyMouseMove(x, y, flags);
    }
    void OnReleaseCapture() override
    {
        if (iTVPLayerManager* m = Primary())
            m->ReleaseCapture();
    }
    void OnMouseOutOfWindow() override
    {
        if (iTVPLayerManager* m = Primary())
            m->NotifyMouseOutOfWindow();
    }
    void OnKeyDown(tjs_uint key, tjs_uint32 shift) override
    {
        if (iTVPLayerManager* m = Primary())
            m->NotifyKeyDown(key, shift);
    }
    void OnKeyUp(tjs_uint key, tjs_uint32 shift) override
    {
        if (iTVPLayerManager* m = Primary())
            m->NotifyKeyUp(key, shift);
    }
    void OnKeyPress(tjs_uint16 key) override
    {
        if (iTVPLayerManager* m = Primary())
            m->NotifyKeyPress(key);
    }
    void OnMouseWheel(tjs_uint32 shift, tjs_int delta, tjs_int x, tjs_int y) override
    {
        if (iTVPLayerManager* m = Primary())
            m->NotifyMouseWheel(shift, delta, x, y);
    }
    void OnTouchDown(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id) override
    {
        if (iTVPLayerManager* m = Primary())
            m->NotifyTouchDown(x, y, cx, cy, id);
    }
    void OnTouchUp(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id) override
    {
        if (iTVPLayerManager* m = Primary())
            m->NotifyTouchUp(x, y, cx, cy, id);
    }
    void OnTouchMove(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id) override
    {
        if (iTVPLayerManager* m = Primary())
            m->NotifyTouchMove(x, y, cx, cy, id);
    }
    void OnTouchScaling(tjs_real startdist, tjs_real curdist, tjs_real cx, tjs_real cy,
                        tjs_int flag) override
    {
        if (iTVPLayerManager* m = Primary())
            m->NotifyTouchScaling(startdist, curdist, cx, cy, flag);
    }
    void OnTouchRotate(tjs_real startangle, tjs_real curangle, tjs_real dist, tjs_real cx,
                       tjs_real cy, tjs_int flag) override
    {
        if (iTVPLayerManager* m = Primary())
            m->NotifyTouchRotate(startangle, curangle, dist, cx, cy, flag);
    }
    void OnMultiTouch() override
    {
        if (iTVPLayerManager* m = Primary())
            m->NotifyMultiTouch();
    }
    void OnDisplayRotate(tjs_int /*orientation*/, tjs_int /*rotate*/, tjs_int /*bpp*/,
                         tjs_int /*width*/, tjs_int /*height*/) override
    {
    }
    void RecheckInputState() override
    {
        if (iTVPLayerManager* m = Primary())
            m->RecheckInputState();
    }

    void SetDefaultMouseCursor(iTVPLayerManager* manager) override;
    void SetMouseCursor(iTVPLayerManager* manager, tjs_int cursor) override;
    void GetCursorPos(iTVPLayerManager* manager, tjs_int& x, tjs_int& y) override;
    void SetCursorPos(iTVPLayerManager* manager, tjs_int x, tjs_int y) override;
    void SetHintText(iTVPLayerManager* manager, iTJSDispatch2* sender, const ttstr& text) override;
    void WindowReleaseCapture(iTVPLayerManager* manager) override;

    void SetAttentionPoint(iTVPLayerManager* manager, tTJSNI_BaseLayer* layer, tjs_int l,
                           tjs_int t) override;
    void DisableAttentionPoint(iTVPLayerManager* manager) override;
    void SetImeMode(iTVPLayerManager* manager, tTVPImeMode mode) override;
    void ResetImeMode(iTVPLayerManager* manager) override;

    tTJSNI_BaseLayer* GetPrimaryLayer() override
    {
        iTVPLayerManager* m = Primary();
        return m ? m->GetPrimaryLayer() : nullptr;
    }
    tTJSNI_BaseLayer* GetFocusedLayer() override
    {
        iTVPLayerManager* m = Primary();
        return m ? m->GetFocusedLayer() : nullptr;
    }
    void SetFocusedLayer(tTJSNI_BaseLayer* layer) override
    {
        if (iTVPLayerManager* m = Primary())
            m->SetFocusedLayer(layer);
    }

    void RequestInvalidation(const tTVPRect& rect) override
    {
        for (auto* m : Managers)
            m->RequestInvalidation(rect);
    }
    void Update() override
    {
        RenderFrame();
    }
    void Show() override
    {
        // KAG 只对 D3D/GPU 设备每帧调 update()，普通设备靠失效通知；
        // 这里每次送显前重画一遍，保证不会停在某一帧的旧内容上。
        RenderFrame();
        Present();
    }
    bool WaitForVBlank(tjs_int* /*in_vblank*/, tjs_int* /*delayed*/) override { return false; }
    void DumpLayerStructure() override
    {
        if (iTVPLayerManager* m = Primary())
            m->DumpLayerStructure();
    }
    void SetShowUpdateRect(bool /*b*/) override {}
    bool SwitchToFullScreen(int /*window*/, tjs_uint /*w*/, tjs_uint /*h*/, tjs_uint /*bpp*/,
                            tjs_uint /*color*/, bool /*changeresolution*/) override
    {
        return true;
    }
    void RevertFromFullScreen(int /*window*/, tjs_uint /*w*/, tjs_uint /*h*/, tjs_uint /*bpp*/,
                              tjs_uint /*color*/) override
    {
    }

    void StartBitmapCompletion(iTVPLayerManager* /*manager*/) override {}
    void NotifyBitmapCompleted(iTVPLayerManager* /*manager*/, tjs_int /*x*/, tjs_int /*y*/,
                               tTVPBaseTexture* /*bmp*/, const tTVPRect& /*cliprect*/,
                               tTVPLayerType /*type*/, tjs_int /*opacity*/) override
    {
    }
    void EndBitmapCompletion(iTVPLayerManager* /*manager*/) override {}

private:
    iTVPLayerManager* Primary() const { return Managers.empty() ? nullptr : Managers.front(); }

    //! 脚本 update：刷新所有 manager 并上屏
    void Tick()
    {
        RenderFrame();
        Present();
    }

    void RenderFrame();
    void Present();
};

//---------------------------------------------------------------------------
void DrawDeviceD2D::Destruct()
{
    if (Window)
        Window->ReleaseBorrowedTexture();
    if (ScratchTexture)
    {
        if (auto* backend = krkrsdl3::TVPGetRenderBackend())
            backend->DestroyTexture(ScratchTexture);
        ScratchTexture = nullptr;
    }
    delete this;
}

//---------------------------------------------------------------------------
void DrawDeviceD2D::AddLayerManager(iTVPLayerManager* manager)
{
    if (!manager)
        return;
    manager->AddRef();

    if (Managers.empty())
    {
        manager->SetDesiredLayerType(ltOpaque);
    }
    else
    {
        // 额外 manager（ZoomView 根图层）保留 alpha，便于叠加到主画面上
        manager->SetDesiredLayerType(ltAlpha);
        if (auto* lm = dynamic_cast<tTVPLayerManager*>(manager))
            lm->SetHoldAlpha(false);
    }
    Managers.push_back(manager);
}

//---------------------------------------------------------------------------
void DrawDeviceD2D::RemoveLayerManager(iTVPLayerManager* manager)
{
    for (auto it = Managers.begin(); it != Managers.end(); ++it)
    {
        if (*it == manager)
        {
            manager->Release();
            Managers.erase(it);
            return;
        }
    }
}

//---------------------------------------------------------------------------
void DrawDeviceD2D::GetSrcSize(tjs_int& w, tjs_int& h)
{
    iTVPLayerManager* m = Primary();
    if (m && m->GetPrimaryLayerSize(w, h))
        return;
    w = Width;
    h = Height;
}

//---------------------------------------------------------------------------
void DrawDeviceD2D::SetDefaultMouseCursor(iTVPLayerManager* manager)
{
    if (Window && Primary() == manager)
        Window->SetDefaultMouseCursor();
}

void DrawDeviceD2D::SetMouseCursor(iTVPLayerManager* manager, tjs_int cursor)
{
    if (Window && Primary() == manager)
        Window->SetMouseCursor(cursor);
}

void DrawDeviceD2D::GetCursorPos(iTVPLayerManager* manager, tjs_int& x, tjs_int& y)
{
    if (Window && Primary() == manager)
        Window->GetCursorPos(x, y);
    else
        x = y = 0;
}

void DrawDeviceD2D::SetCursorPos(iTVPLayerManager* manager, tjs_int x, tjs_int y)
{
    if (Window && Primary() == manager)
        Window->SetCursorPos(x, y);
}

void DrawDeviceD2D::SetHintText(iTVPLayerManager* manager, iTJSDispatch2* sender, const ttstr& text)
{
    if (Window && Primary() == manager)
        Window->SetHintText(sender, text);
}

void DrawDeviceD2D::WindowReleaseCapture(iTVPLayerManager* manager)
{
    if (Window && Primary() == manager)
        Window->WindowReleaseCapture();
}

void DrawDeviceD2D::SetAttentionPoint(iTVPLayerManager* manager, tTJSNI_BaseLayer* layer, tjs_int l,
                                      tjs_int t)
{
    if (Window && Primary() == manager)
        Window->SetAttentionPoint(layer, l, t);
}

void DrawDeviceD2D::DisableAttentionPoint(iTVPLayerManager* manager)
{
    if (Window && Primary() == manager)
        Window->DisableAttentionPoint();
}

void DrawDeviceD2D::SetImeMode(iTVPLayerManager* manager, tTVPImeMode mode)
{
    if (Window && Primary() == manager)
        Window->SetImeMode(mode);
}

void DrawDeviceD2D::ResetImeMode(iTVPLayerManager* manager)
{
    if (Window && Primary() == manager)
        Window->ResetImeMode();
}

//---------------------------------------------------------------------------
// 读取某个 manager 的 viewState 取景矩形
//---------------------------------------------------------------------------
static bool GetViewRect(iTVPLayerManager* manager, tjs_int& x, tjs_int& y, tjs_int& w, tjs_int& h,
                        tjs_int& opacity, bool& visible)
{
    if (!manager)
        return false;
    tTJSNI_BaseLayer* pri = manager->GetPrimaryLayer();
    if (!pri)
        return false;
    iTJSDispatch2* owner = pri->GetOwnerNoAddRef();
    if (!owner)
        return false;

    tTJSVariant vsVar;
    tjs_error pe = owner->PropGet(0, TJS_N("viewState"), NULL, &vsVar, owner);
    if (TJS_FAILED(pe))
        return false;
    iTJSDispatch2* vs = vsVar.AsObjectNoAddRef();
    if (!vs)
        return false;

    bool rectOk = viewstate::GetRect((iTJSDispatch2*)pri, x, y, w, h);
    if (!rectOk)
        rectOk = viewstate::GetRect(vs, x, y, w, h);
    opacity = pri->GetOpacity();
    visible = pri->GetVisible();
    if (rectOk)
        return true;

    tTJSVariant v;
    auto getInt = [&](const tjs_char* name, tjs_int def) -> tjs_int {
        if (TJS_SUCCEEDED(vs->PropGet(0, name, NULL, &v, vs)))
            return (tjs_int)v;
        return def;
    };

    x = getInt(TJS_N("rectX"), 0);
    y = getInt(TJS_N("rectY"), 0);
    w = getInt(TJS_N("rectW"), 0);
    h = getInt(TJS_N("rectH"), 0);
    opacity = getInt(TJS_N("opacity"), 255);
    visible = getInt(TJS_N("visible"), 1) != 0;
    return w > 0 && h > 0;
}

//---------------------------------------------------------------------------
void DrawDeviceD2D::RenderFrame()
{
    if (Managers.empty())
        return;

    // 1) 所有 manager 整幅重绘：
    //    - 普通页面（KAG 的 fore/back）按不透明处理
    //    - 带 viewState 的子画面（ZoomView 根图层）按 alpha 处理，并清成透明
    struct DrawItem
    {
        iTVPLayerManager* Mgr = nullptr;
        iTVPBaseBitmap* Src = nullptr;
        bool Overlay = false;
        tjs_int X = 0, Y = 0, W = 0, H = 0, Opacity = 255;
    };
    std::vector<DrawItem> items;

    tjs_int screenW = 0, screenH = 0;

    for (size_t i = 0; i < Managers.size(); ++i)
    {
        iTVPLayerManager* mgr = Managers[i];
        if (!mgr)
            continue;

        tTJSNI_BaseLayer* pri = mgr->GetPrimaryLayer();
        if (!pri)
            continue;

        const tjs_int pw = (tjs_int)pri->GetWidth();
        const tjs_int ph = (tjs_int)pri->GetHeight();
        if (pw <= 0 || ph <= 0)
            continue;
        if (pw > screenW)
            screenW = pw;
        if (ph > screenH)
            screenH = ph;

        tjs_int vx = 0, vy = 0, vw = 0, vh = 0, opacity = 255;
        bool visible = true;
        const bool overlay = GetViewRect(mgr, vx, vy, vw, vh, opacity, visible);

        if (auto* lm = dynamic_cast<tTVPLayerManager*>(mgr))
        {
            if (overlay)
            {
                lm->SetHoldAlpha(false);
                mgr->SetDesiredLayerType(ltAlpha);
            }
            else
            {
                lm->SetHoldAlpha(true);
                mgr->SetDesiredLayerType(ltOpaque);
            }
        }

        if (overlay)
        {
            if (tTVPBaseTexture* buf = dynamic_cast<tTVPBaseTexture*>(mgr->GetDrawBuffer()))
            {
                const tjs_int bw = (tjs_int)buf->GetWidth();
                const tjs_int bh = (tjs_int)buf->GetHeight();
                if ((tjs_int)pri->GetWidth() != bw || (tjs_int)pri->GetHeight() != bh)
                    buf->SetSize((tjs_uint)pw, (tjs_uint)ph, false);
                buf->Fill(tTVPRect(0, 0, (tjs_int)buf->GetWidth(), (tjs_int)buf->GetHeight()), 0x00000000);
            }
        }

        mgr->RequestInvalidation(tTVPRect(0, 0, pw, ph));
        mgr->UpdateToDrawDevice();

        // ZoomView 根图层在标题阶段还是 32x32 的空占位，不能拿它覆盖主画面
        if (overlay && pw <= 32 && ph <= 32)
            continue;

        DrawItem it;
        it.Mgr = mgr;
        it.Src = mgr->GetDrawBuffer();
        it.Overlay = overlay;
        it.X = vx;
        it.Y = vy;
        it.W = vw;
        it.H = vh;
        it.Opacity = opacity;
        if (it.Src)
            items.push_back(it);
    }

    if (screenW <= 0 || screenH <= 0)
        return;

    if (!Composite || (tjs_int)Composite->GetWidth() != screenW || (tjs_int)Composite->GetHeight() != screenH)
    {
        delete Composite;
        Composite = new tTVPDestTexture(screenW, screenH);
    }
    if (!Composite)
        return;

    Composite->Fill(tTVPRect(0, 0, screenW, screenH), (tjs_uint32)ClearColor);

    // 2) 先画整页（fore/back），再画带 viewState 的叠加画面
    for (int pass = 0; pass < 2; ++pass)
    {
        for (const DrawItem& it : items)
        {
            if ((pass == 0) == it.Overlay)
                continue;
            if (!it.Src)
                continue;

            const tjs_int dw = (tjs_int)it.Src->GetWidth();
            const tjs_int dh = (tjs_int)it.Src->GetHeight();
            if (dw <= 0 || dh <= 0)
                continue;

            tjs_int destX = 0, destY = 0, destW = screenW, destH = screenH;
            tjs_int opa = 255;
            if (it.Overlay)
            {
                if (!it.Src || it.W <= 0 || it.H <= 0)
                    continue;
                destX = it.X;
                destY = it.Y;
                destW = it.W;
                destH = it.H;
                opa = it.Opacity;
            }

            tTVPRect dest(destX, destY, destX + destW, destY + destH);
            if (dest.left < 0) dest.left = 0;
            if (dest.top < 0) dest.top = 0;
            if (dest.right > screenW) dest.right = screenW;
            if (dest.bottom > screenH) dest.bottom = screenH;
            if (dest.get_width() <= 0 || dest.get_height() <= 0)
                continue;

            // 图层缓冲的 alpha 通道不可靠（多数为 0），叠加画面按 bmCopy 直接覆盖
            Composite->StretchBlt(tTVPRect(0, 0, screenW, screenH), dest, it.Src,
                                  tTVPRect(0, 0, dw, dh), bmCopy, opa, true,
                                  StretchType == 0 ? stFastLinear : stNearest);
        }
    }
}

void DrawDeviceD2D::Present()
{
    if (!Window || !Composite)
        return;
    iTVPTexture2D* tex = Composite->GetTexture();
    if (!tex)
        return;

    const tjs_int w = tex->GetWidth();
    const tjs_int h = tex->GetHeight();

    if (!tex->IsCPUResident() && tex->GetTextureHandle())
    {
        Window->PresentTexture(tex->GetTextureHandle(), w, h);
        return;
    }

    krkrsdl3::iTVPRenderBackend* backend = krkrsdl3::TVPGetRenderBackend();
    if (!backend)
        return;

    if (!ScratchTexture || w != ScratchW || h != ScratchH)
    {
        if (ScratchTexture)
            backend->DestroyTexture(ScratchTexture);
        ScratchTexture = backend->CreateTexture(w, h);
        ScratchW = w;
        ScratchH = h;
    }
    if (!ScratchTexture)
        return;

    void* pixels = tex->LockCPURead();
    if (pixels)
    {
        backend->UpdateTexture(ScratchTexture, (tjs_uint8*)pixels, w, h, tex->GetPitch());
        tex->UnlockCPU();
    }
    Window->PresentTexture(ScratchTexture, w, h);
}

//---------------------------------------------------------------------------
// 脚本类
//---------------------------------------------------------------------------
NCB_REGISTER_CLASS(DrawDeviceD2D)
{
    NCB_CONSTRUCTOR(());
    NCB_PROPERTY_RO(interface, getInterface);
    NCB_PROPERTY(width, getWidth, setWidth);
    NCB_PROPERTY(height, getHeight, setHeight);
    NCB_METHOD(setSize);
    NCB_PROPERTY(clearColor, getClearColor, setClearColor);
    NCB_PROPERTY(layerManagerIndex, getLayerManagerIndex, setLayerManagerIndex);
    NCB_PROPERTY(stretchType, getStretchType, setStretchType);
    NCB_PROPERTY(bicubicParam, getBicubicParam, setBicubicParam);
    NCB_PROPERTY(transState, getTransState, setTransState);
    NCB_PROPERTY(maskMode, getMaskMode, setMaskMode);
    NCB_PROPERTY_RO(primaryLayers, getPrimaryLayers);
    NCB_METHOD(checkEnable);
    NCB_METHOD(getModule);
    NCB_METHOD(update);
    NCB_METHOD(capture);
    NCB_METHOD(startTransition);
    NCB_METHOD(stopTransition);
    NCB_METHOD(setScreenRect);
    NCB_METHOD(setPrimarySize);
    NCB_METHOD(setOffset);
    NCB_METHOD(recreate);
    NCB_METHOD(finalize);
}
