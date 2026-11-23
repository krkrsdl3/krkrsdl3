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
#include "TVPDebug.h"
#include "tjsDictionary.h"
#include "tjsNative.h"
#include "tjsNativeLayer.h"
#include "tjsDebug.h"
#include "TVPScript.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <thread>
#include <vector>

#define NCB_MODULE_NAME TJS_N("DrawDeviceD2Dm.dll")

//---------------------------------------------------------------------------
// Layer.viewState
//---------------------------------------------------------------------------
namespace viewstate
{

//! 每个 Layer 的 TJS 对象对应一个 viewState 字典
static std::map<iTJSDispatch2*, iTJSDispatch2*> ViewStates;

//! viewState 字典 -> 拥有它的 Layer TJS 对象（setRect 的 objthis 是字典，不是 Layer）
static std::map<iTJSDispatch2*, iTJSDispatch2*> ViewStateOwners;

//! 取景矩形（key 为原生图层指针）
struct ViewRect
{
    tjs_int X = 0, Y = 0, W = 0, H = 0;
    tjs_int Type = 0; //!< setRect 的第 5 个参数：该 view 的合成类型
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
    {
        // 脚本以 layer.viewState.setRect(...) 调用时 objthis 是字典；
        // 通过字典 -> Layer 的映射找回真正的原生图层
        auto owner = ViewStateOwners.find(objthis);
        if (owner != ViewStateOwners.end())
            layer = NativeLayer(owner->second);
    }
    if (!layer)
        return TJS_S_OK;

    ViewRect& vr = ViewRects[(iTJSDispatch2*)layer];
    vr.X = (tjs_int)*param[0];
    vr.Y = (tjs_int)*param[1];
    vr.W = (tjs_int)*param[2] - vr.X;
    vr.H = (tjs_int)*param[3] - vr.Y;
    if (numparams >= 5)
        vr.Type = (tjs_int)*param[4];
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
    ViewStateOwners[d] = objthis;
    *result = d;
    return TJS_S_OK;
}

//! 取某个原生图层记录的目标矩形
static bool GetRect(iTJSDispatch2* layerKey, tjs_int& x, tjs_int& y, tjs_int& w, tjs_int& h,
                    tjs_int* type = nullptr)
{
    auto it = ViewRects.find(layerKey);
    if (it == ViewRects.end())
        return false;
    x = it->second.X;
    y = it->second.Y;
    w = it->second.W;
    h = it->second.H;
    if (type)
        *type = it->second.Type;
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
static bool GetViewRect(iTVPLayerManager* manager,
                        tjs_int& x,
                        tjs_int& y,
                        tjs_int& w,
                        tjs_int& h,
                        tjs_int& opacity,
                        bool& visible,
                        tjs_int& order);

//! 调试看门狗：主线程卡死时抓 TJS 调用栈
static std::atomic<long long> gD2DRenderCalls{0};
static std::atomic<bool> gD2DWatchdogStop{false};
static std::atomic<bool> gD2DWatchdogStarted{false};

static void StartD2DWatchdog()
{
    // 诊断看门狗已关闭：跨线程读 TJS 栈会崩，等主渲染路径稳定后再做。
    (void)gD2DRenderCalls;
    (void)gD2DWatchdogStop;
    (void)gD2DWatchdogStarted;
}

class DrawDeviceD2D : public iTVPDrawDevice
{
    struct ManagerState
    {
        bool Dirty = true;
        bool Valid = false;
        tTVPRect Content; //!< 非透明像素包围盒（主画面合成时只处理这块）
        tjs_int Order = 0; //!< 脚本 viewState.order
        tTVPRect ViewRect;
        bool HasView = false;
        bool ViewVisible = true;
        tjs_int ViewOpacity = 255;
        tjs_int W = 0, H = 0;
    };

    std::vector<iTVPLayerManager*> Managers;
    std::map<iTVPLayerManager*, ManagerState> States;
    bool CompositeDirty = true;

    TVPWindow* Window = nullptr;
    tTVPRect DestRect;
    tTVPRect ScreenRect; //!< setScreenRect 记录的屏幕矩形
    bool HasScreenRect = false;
    tjs_int PrimaryW = 0, PrimaryH = 0; //!< setPrimarySize 记录的主画面尺寸
    tjs_int OffsetX = 0, OffsetY = 0;   //!< setOffset 记录的屏幕偏移

    tTVPDestTexture* Composite = nullptr; //!< 合成目标
    void* ScratchTexture = nullptr;       //!< 呈现用贴图
    tjs_int ScratchW = 0, ScratchH = 0;

    tjs_int Width = 0, Height = 0; //!< 画面（虚拟）尺寸

    // ZoomView 的取景矩形由脚本按“窗口缩放”计算；窗口/全屏或主画面尺寸变化后
    // 需要让脚本重算一次，否则矩形会停留在旧尺寸（典型：全屏后仍是 3/4）。
    bool PendingZoomViewRefresh = false;
    bool RefreshingZoomView = false;
    bool ZoomProbeValid = false;
    bool ZoomViewRefreshFailed = false;
    tjs_int LastZoomNumer = 1, LastZoomDenom = 1;
    bool LastFullScreen = false;

    tjs_int LayerIndex = 0;
    tjs_int DebugOnlyManager = -1; //!< 调试：只合成指定 manager（-1 = 全部）
    //! 调试统计：每 120 帧打一行，确认卡顿发生在哪一段
    tjs_int StatFrames = 0;
    tjs_int StatImageChanges = 0;
    tjs_int StatMainUpdates = 0;
    tjs_int StatOverlayUpdates = 0;
    tjs_int StatComposites = 0;
    double StatRenderMs = 0.0;
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
        StartD2DWatchdog();
    }

    //! 无参构造（used2d.tjs 的 create() 不带参数）
    DrawDeviceD2D()
      : Width(1920),
        Height(1080)
    {
        DestRect = tTVPRect(0, 0, Width, Height);
        StartD2DWatchdog();
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

    void setScreenRect(tjs_int x, tjs_int y, tjs_int w, tjs_int h)
    {
        const bool valid = w > 0 && h > 0;
        const bool changed = HasScreenRect != valid || ScreenRect.left != x ||
                             ScreenRect.top != y || ScreenRect.right != x + w ||
                             ScreenRect.bottom != y + h;
        if (!changed)
            return;
        ScreenRect = tTVPRect(x, y, x + w, y + h);
        HasScreenRect = valid;
        if (valid)
        {
            Width = w;
            Height = h;
        }
        CompositeDirty = true;
        PendingZoomViewRefresh = true;
    }
    void setPrimarySize(tjs_int w, tjs_int h)
    {
        if (PrimaryW == w && PrimaryH == h)
            return;
        PrimaryW = w;
        PrimaryH = h;
        if (w > 0 && h > 0)
        {
            Width = w;
            Height = h;
        }
        CompositeDirty = true;
        PendingZoomViewRefresh = true;
    }
    void setOffset(tjs_int x, tjs_int y)
    {
        if (OffsetX == x && OffsetY == y)
            return;
        OffsetX = x;
        OffsetY = y;
        CompositeDirty = true;
    }

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

    //! 调试：把所有 manager 的主图层信息与层树写进日志（kag.dump() 会调用）
    void dump(tjs_int /*level*/)
    {
        tjs_int index = 0;
        for (auto* m : Managers)
        {
            tTJSNI_BaseLayer* pri = m ? m->GetPrimaryLayer() : nullptr;
            ttstr line(TJS_N("[D2D] manager "));
            line += ttstr(index);
            if (pri)
            {
                line += TJS_N(" name=");
                line += pri->GetName();
                line += TJS_N(" size=");
                line += ttstr((tjs_int)pri->GetWidth());
                line += TJS_N("x");
                line += ttstr((tjs_int)pri->GetHeight());
                line += TJS_N(" type=");
                line += ttstr((tjs_int)pri->GetType());
                line += TJS_N(" visible=");
                line += ttstr((tjs_int)pri->GetVisible());
                line += TJS_N(" opacity=");
                line += ttstr(pri->GetOpacity());

                tjs_int vx = 0, vy = 0, vw = 0, vh = 0, opacity = 255, vorder = 0;
                bool visible = true;
                const bool hasView = GetViewRect(m, vx, vy, vw, vh, opacity, visible, vorder);
                line += TJS_N(" view=");
                line += hasView ? TJS_N("1") : TJS_N("0");
                if (hasView)
                {
                    line += TJS_N("(");
                    line += ttstr(vx);
                    line += TJS_N(",");
                    line += ttstr(vy);
                    line += TJS_N(",");
                    line += ttstr(vw);
                    line += TJS_N(",");
                    line += ttstr(vh);
                    line += TJS_N(") opa=");
                    line += ttstr(opacity);
                    line += TJS_N(" vis=");
                    line += ttstr((tjs_int)visible);
                    line += TJS_N(" order=");
                    line += ttstr(vorder);
                }
            }
            TVPAddImportantLog(line);
            if (m)
                m->DumpLayerStructure();
            index++;
        }
    }

    tjs_int getLayerManagerIndex() const { return LayerIndex; }
    void setLayerManagerIndex(tjs_int index) { LayerIndex = index; }
    tjs_int getDebugOnlyManager() const { return DebugOnlyManager; }
    void setDebugOnlyManager(tjs_int index) { DebugOnlyManager = index; }

    //! 调试：读某个 manager 的 DrawBuffer 像素（0xAARRGGBB）
    tjs_int probeBuffer(tjs_int index, tjs_int x, tjs_int y)
    {
        if (index < 0 || index >= (tjs_int)Managers.size())
            return -1;
        auto* lm = dynamic_cast<tTVPLayerManager*>(Managers[index]);
        if (!lm)
            return -2;
        tTVPBaseTexture* buf = lm->GetOrCreateDrawBuffer();
        if (!buf)
            return -3;
        return (tjs_int)buf->GetPoint(x, y);
    }
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

    void SetDestRectangle(const tTVPRect& rect) override
    {
        DestRect = rect;
        if (!HasScreenRect && rect.get_width() > 0 && rect.get_height() > 0)
        {
            ScreenRect = rect;
            HasScreenRect = true;
            CompositeDirty = true;
        }
    }
    void SetWindowSize(tjs_int /*w*/, tjs_int /*h*/) override {}
    void SetClipRectangle(const tTVPRect& /*rect*/) override {}
    void GetSrcSize(tjs_int& w, tjs_int& h) override;

    void NotifyLayerResize(iTVPLayerManager* manager) override
    {
        States[manager].Dirty = true;
        CompositeDirty = true;
        if (!RefreshingZoomView)
            PendingZoomViewRefresh = true;
        if (Window)
            Window->NotifySrcResize();
    }
    void NotifyLayerImageChange(iTVPLayerManager* manager) override
    {
        // 层内容变化 → 记脏并请求窗口更新，否则 UpdateContent 不会触发
        States[manager].Dirty = true;
        CompositeDirty = true;
        if (++StatImageChanges % 120 == 0)
        {
            ttstr l(TJS_N("[D2D] imageChanges="));
            l += ttstr(StatImageChanges);
            TVPAddImportantLog(l);
        }
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
        {
            States[m].Dirty = true;
            m->RequestInvalidation(rect);
        }
        CompositeDirty = true;
    }
    void Update() override
    {
        RenderFrame();
    }
    void Show() override
    {
        // 只送显。重绘在 Update()/update() 里完成；Show 再画一遍等于每帧双倍开销，
        // 这是走进度卡顿的主要来源之一。
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

    bool ProbeZoomViewRefresh();
    void TryRefreshZoomView();
    void RenderFrame();
    void Present();
};

//---------------------------------------------------------------------------
void DrawDeviceD2D::Destruct()
{
    gD2DWatchdogStop.store(true);
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

    // 主 manager（KAG 页面）保持 ltOpaque：背景图/对白/UI 作为不透明底。
    // 额外 manager（ZoomView 地图根 ltAddAlpha）按加算 alpha 叠在主画面上。
    // 这样原版 D2D 的 ltAddAlpha 语义成立，也不会把没有 alpha 通道的背景图弄黑。
    if (Managers.empty())
    {
        manager->SetDesiredLayerType(ltOpaque);
        if (auto* lm = dynamic_cast<tTVPLayerManager*>(manager))
            lm->SetHoldAlpha(true);
    }
    else
    {
        manager->SetDesiredLayerType(ltAlpha);
        if (auto* lm = dynamic_cast<tTVPLayerManager*>(manager))
            lm->SetHoldAlpha(false);
    }
    Managers.push_back(manager);
    States[manager] = ManagerState();
    CompositeDirty = true;
    PendingZoomViewRefresh = true;
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
            States.erase(manager);
            CompositeDirty = true;
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
    if (PrimaryW > 0 && PrimaryH > 0)
    {
        w = PrimaryW;
        h = PrimaryH;
        return;
    }
    if (HasScreenRect && ScreenRect.get_width() > 0 && ScreenRect.get_height() > 0)
    {
        w = ScreenRect.get_width();
        h = ScreenRect.get_height();
        return;
    }
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
                        tjs_int& opacity, bool& visible, tjs_int& order)
{
    order = 0;
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

    tTJSVariant v;
    auto getInt = [&](const tjs_char* name, tjs_int def) -> tjs_int {
        if (TJS_SUCCEEDED(vs->PropGet(0, name, NULL, &v, vs)))
            return (tjs_int)v;
        return def;
    };

    // viewState.order：脚本指定的合成顺序（ZoomView 用 setRootVisible(1,-1) 把地图放到最底层）
    order = getInt(TJS_N("order"), 0);

    bool rectOk = viewstate::GetRect((iTJSDispatch2*)pri, x, y, w, h);
    if (!rectOk)
        rectOk = viewstate::GetRect(vs, x, y, w, h);
    opacity = pri->GetOpacity();
    visible = pri->GetVisible();
    if (rectOk)
        return true;

    x = getInt(TJS_N("rectX"), 0);
    y = getInt(TJS_N("rectY"), 0);
    w = getInt(TJS_N("rectW"), 0);
    h = getInt(TJS_N("rectH"), 0);
    opacity = getInt(TJS_N("opacity"), 255);
    visible = getInt(TJS_N("visible"), 1) != 0;
    return w > 0 && h > 0;
}

//---------------------------------------------------------------------------
// 窗口缩放/全屏变化探测：脚本的 ZoomView 取景矩形是按窗口缩放算的，
// 窗口内容尺寸变化后必须让脚本重算一次。
//---------------------------------------------------------------------------
bool DrawDeviceD2D::ProbeZoomViewRefresh()
{
    bool changed = !ZoomProbeValid;
    if (Window)
    {
        const bool full = Window->GetFullScreenMode();
        const tjs_int numer = Window->GetZoomNumer();
        const tjs_int denom = Window->GetZoomDenom();
        if (!ZoomProbeValid || full != LastFullScreen || numer != LastZoomNumer ||
            denom != LastZoomDenom)
        {
            LastFullScreen = full;
            LastZoomNumer = numer;
            LastZoomDenom = denom;
            ZoomProbeValid = true;
            changed = true;
        }
    }
    if (changed)
    {
        PendingZoomViewRefresh = true;
        CompositeDirty = true;
    }
    return changed;
}

//---------------------------------------------------------------------------
// 让 KAG 的 ZoomView 用当前窗口/画布尺寸重算取景矩形。
// 只在窗口缩放/全屏/主画面尺寸变化后调用，避免每帧进入脚本。
//---------------------------------------------------------------------------
void DrawDeviceD2D::TryRefreshZoomView()
{
    if (!PendingZoomViewRefresh || RefreshingZoomView || !Window)
        return;

    iTJSDispatch2* owner = Window->GetOwnerNoAddRef();
    if (!owner)
        return;

    tTJSVariant zoomViewVar;
    if (TJS_FAILED(owner->PropGet(0, TJS_N("ZoomView"), NULL, &zoomViewVar, owner)))
        return; // 地图尚未创建：保留 pending，下一帧再试
    iTJSDispatch2* zoomView = zoomViewVar.AsObjectNoAddRef();
    if (!zoomView)
        return;

    RefreshingZoomView = true;
    try
    {
        tTJSVariant result;
        zoomView->FuncCall(0, TJS_N("resetScreenRect"), NULL, &result, 0, NULL, zoomView);
        PendingZoomViewRefresh = false;
        ZoomViewRefreshFailed = false;
        CompositeDirty = true;
    }
    catch (...)
    {
        PendingZoomViewRefresh = false;
        if (!ZoomViewRefreshFailed)
        {
            ZoomViewRefreshFailed = true;
            TVPAddImportantLog(ttstr(TJS_N("[D2D] ZoomView.resetScreenRect failed")));
        }
    }
    RefreshingZoomView = false;
}

//---------------------------------------------------------------------------
// 脚本按 window zoom 算出的取景矩形（例如全屏下仍是 1440x810）映射回
// 实际内容尺寸（1920x1080）。只有矩形正好等于“内容尺寸 * zoom”时才映射，
// 自定义视口/真正的 letterbox 保持原样。
//---------------------------------------------------------------------------
static tTVPRect MapZoomedViewRect(const tTVPRect& view, tjs_int screenW, tjs_int screenH,
                                  tjs_int numer, tjs_int denom)
{
    if (numer <= 0 || denom <= 0 || numer >= denom || view.get_width() <= 0 ||
        view.get_height() <= 0)
        return view;

    const tjs_int expectedW = (tjs_int)((tjs_int64)screenW * numer / denom);
    const tjs_int expectedH = (tjs_int)((tjs_int64)screenH * numer / denom);
    const tjs_int dw = view.get_width() - expectedW;
    const tjs_int dh = view.get_height() - expectedH;
    if (dw < -2 || dw > 2 || dh < -2 || dh > 2)
        return view;

    tTVPRect mapped;
    mapped.left = (tjs_int)((tjs_int64)view.left * denom / numer);
    mapped.top = (tjs_int)((tjs_int64)view.top * denom / numer);
    mapped.right = (tjs_int)((tjs_int64)view.right * denom / numer);
    mapped.bottom = (tjs_int)((tjs_int64)view.bottom * denom / numer);
    return mapped;
}

//---------------------------------------------------------------------------
void DrawDeviceD2D::RenderFrame()
{
    const auto statStart = std::chrono::steady_clock::now();
    gD2DRenderCalls.fetch_add(1);
    static tjs_int sRenderCalls = 0;
    if (++sRenderCalls % 120 == 0)
    {
        ttstr l(TJS_N("[D2D] renderCalls="));
        l += ttstr(sRenderCalls);
        TVPAddImportantLog(l);
    }
    if (Managers.empty())
        return;

    // 窗口缩放/全屏/画布变化后，先让脚本重算 ZoomView 取景矩形，再收集 manager
    ProbeZoomViewRefresh();
    TryRefreshZoomView();

    auto statFinish = [&](bool composed) {
        StatFrames++;
        if (composed)
            StatComposites++;
        const auto now = std::chrono::steady_clock::now();
        StatRenderMs += std::chrono::duration<double, std::milli>(now - statStart).count();
        if (StatFrames % 120 == 0)
        {
            ttstr line(TJS_N("[D2D] frames="));
            line += ttstr(StatFrames);
            line += TJS_N(" mainUpd=");
            line += ttstr(StatMainUpdates);
            line += TJS_N(" ovlUpd=");
            line += ttstr(StatOverlayUpdates);
            line += TJS_N(" comp=");
            line += ttstr(StatComposites);
            line += TJS_N(" avgRenderMs=");
            line += ttstr((tjs_int)(StatRenderMs / StatFrames));
            TVPAddImportantLog(line);
            StatFrames = StatMainUpdates = StatOverlayUpdates = StatComposites = 0;
            StatRenderMs = 0.0;
        }
    };

    // 1) 收集参与合成的 manager。
    //    顺序、取景矩形、可见性、透明度全部来自脚本 viewState，内核不做猜测：
    //    - ZoomView 的地图 manager 在脚本里 setRootVisible(1, -1) → order=-1，画在主画面下方
    //    - 主 manager（无 viewState）order=0
    //    只要画面上不止一个 manager，主画面就必须输出带 alpha 的缓冲，
    //    设备的 DesiredLayerType(ltAlpha) 才有可能让 UI 的透明区域露出地图。
    struct DrawItem
    {
        iTVPLayerManager* Mgr = nullptr;
        iTVPBaseBitmap* Src = nullptr;
        tjs_int Order = 0;            //!< 脚本 viewState.order
        tjs_int LayerType = ltOpaque; //!< 实际合成类型（DisplayType）
        tjs_int Index = 0;
        tTVPRect SrcRect;
        tTVPRect DestRect;
        tjs_int Opacity = 255;
    };
    std::vector<DrawItem> items;

    tjs_int screenW = 0, screenH = 0;
    bool anyUpdated = false;

    // 画面尺寸以主 manager（注册顺序第一个）的 primary 为准
    if (iTVPLayerManager* first = Managers[0])
    {
        if (tTJSNI_BaseLayer* pri = first->GetPrimaryLayer())
        {
            screenW = (tjs_int)pri->GetWidth();
            screenH = (tjs_int)pri->GetHeight();
        }
    }
    // 主 manager 尚未建立有效主图层时，退回脚本 setPrimarySize/setScreenRect 的记录
    if (screenW <= 0 || screenH <= 0)
    {
        if (PrimaryW > 0 && PrimaryH > 0)
        {
            screenW = PrimaryW;
            screenH = PrimaryH;
        }
        else if (HasScreenRect)
        {
            screenW = ScreenRect.get_width();
            screenH = ScreenRect.get_height();
        }
    }
    if (screenW <= 0 || screenH <= 0)
    {
        statFinish(false);
        return;
    }

    // 先数一下真正参与合成的 manager 数量（跳过不可见与标题阶段的 32x32 占位）
    tjs_int drawnManagers = 0;
    for (size_t i = 0; i < Managers.size(); ++i)
    {
        iTVPLayerManager* mgr = Managers[i];
        tTJSNI_BaseLayer* pri = mgr ? mgr->GetPrimaryLayer() : nullptr;
        if (!pri)
            continue;
        const tjs_int pw = (tjs_int)pri->GetWidth();
        const tjs_int ph = (tjs_int)pri->GetHeight();
        if (pw <= 0 || ph <= 0)
            continue;
        tjs_int vx = 0, vy = 0, vw = 0, vh = 0, opacity = 255, order = 0;
        bool visible = true;
        const bool hasView = GetViewRect(mgr, vx, vy, vw, vh, opacity, visible, order);
        if (!visible)
            continue;
        if (hasView && pw <= 32 && ph <= 32)
            continue;
        drawnManagers++;
    }

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

        tjs_int vx = 0, vy = 0, vw = 0, vh = 0, opacity = 255;
        bool visible = true;
        tjs_int order = 0;
        const bool hasView = GetViewRect(mgr, vx, vy, vw, vh, opacity, visible, order);

        ManagerState& st = States[mgr];
        const bool viewChanged = st.HasView != hasView || st.ViewVisible != visible ||
                                 st.ViewOpacity != opacity || st.W != pw || st.H != ph ||
                                 st.Order != order ||
                                 st.ViewRect.left != vx || st.ViewRect.top != vy ||
                                 st.ViewRect.right != vx + vw || st.ViewRect.bottom != vy + vh;
        if (viewChanged)
        {
            st.HasView = hasView;
            st.ViewVisible = visible;
            st.ViewOpacity = opacity;
            st.Order = order;
            st.ViewRect = tTVPRect(vx, vy, vx + vw, vy + vh);
            st.W = pw;
            st.H = ph;
            CompositeDirty = true;
        }
        if (!visible)
            continue;

        // ZoomView 根图层在标题阶段还是 32x32 的空占位，不能拿它覆盖主画面
        if (hasView && pw <= 32 && ph <= 32)
            continue;

        auto* lm = dynamic_cast<tTVPLayerManager*>(mgr);
        if (!lm)
            continue;

        // 主 manager 是容器根（hasImage=0）：多 manager 叠加时按设备要求的
        // ltAlpha 目标格式合成，UI 的 alpha 才会累积到缓冲里。脚本可见的
        // layer.type 仍是 ltOpaque，不改变任何脚本语义。
        // 原版 D2D 的图层都是带 alpha 的位图，按 source-over 绘制：
        // ltOpaque 在该设备上同样是"按图层自身的 alpha 合成"，所以这里把
        // 主画面树里的 ltOpaque 图层改为按 ltAlpha 合成（不透明度由图像
        // 自身的 alpha 决定）。纯不透明图像(alpha=255)的结果与 bmCopy 相同。
        if (i == 0)
        {
            if (drawnManagers > 1)
            {
                for (tTJSNI_BaseLayer* lay : lm->GetAllNodes())
                {
                    if (lay && lay->GetType() == ltOpaque && lay->GetDisplayType() == ltOpaque)
                        lay->SetDisplayTypeForRender(ltAlpha);
                }
            }
            else
            {
                // 单 manager（剧情/标题）恢复脚本原语义：ltOpaque 图层按不透明复制。
                for (tTJSNI_BaseLayer* lay : lm->GetAllNodes())
                {
                    if (lay && lay->GetType() == ltOpaque && lay->GetDisplayType() == ltAlpha)
                        lay->SetDisplayTypeForRender(ltOpaque);
                }
                if (!pri->GetHasImage())
                    pri->SetDisplayTypeForRender(pri->GetType());
            }
            mgr->SetDesiredLayerType(drawnManagers > 1 ? ltAlpha : ltOpaque);
        }

        if (st.Dirty || !st.Valid)
        {
            lm->SetHoldAlpha(false);

            tTVPBaseTexture* buf = lm->GetOrCreateDrawBuffer();
            if (!buf)
                continue;
            if ((tjs_int)buf->GetWidth() != pw || (tjs_int)buf->GetHeight() != ph)
                buf->SetSize((tjs_uint)pw, (tjs_uint)ph, false);
            buf->Fill(tTVPRect(0, 0, pw, ph), 0x00000000u);

            mgr->RequestInvalidation(tTVPRect(0, 0, pw, ph));
            mgr->UpdateToDrawDevice();

            st.Content = tTVPRect(0, 0, pw, ph);
            st.Dirty = false;
            st.Valid = true;
            anyUpdated = true;
            CompositeDirty = true;
            if (i == 0)
                StatMainUpdates++;
            else
                StatOverlayUpdates++;
        }

        iTVPBaseBitmap* src = lm->GetDrawBuffer();
        if (!src)
            continue;

        DrawItem it;
        it.Mgr = mgr;
        it.Src = src;
        it.Index = (tjs_int)i;
        it.Opacity = opacity;
        it.Order = order;
        it.LayerType = (tjs_int)pri->GetDisplayType();
        it.SrcRect = tTVPRect(0, 0, pw, ph);
        if (hasView)
        {
            it.DestRect = tTVPRect(vx, vy, vx + vw, vy + vh);
            if (Window)
                it.DestRect = MapZoomedViewRect(it.DestRect, screenW, screenH,
                                                Window->GetZoomNumer(), Window->GetZoomDenom());
            it.DestRect.left += OffsetX;
            it.DestRect.top += OffsetY;
            it.DestRect.right += OffsetX;
            it.DestRect.bottom += OffsetY;
        }
        else
        {
            it.DestRect = tTVPRect(0, 0, screenW, screenH);
        }
        items.push_back(it);
    }

    if (items.empty())
    {
        statFinish(false);
        return;
    }

    if (!CompositeDirty && Composite && !anyUpdated)
    {
        statFinish(false);
        return; // 没有任何图层变化，复用上一帧的合成结果
    }

    if (!Composite || (tjs_int)Composite->GetWidth() != screenW || (tjs_int)Composite->GetHeight() != screenH)
    {
        delete Composite;
        Composite = new tTVPDestTexture(screenW, screenH);
    }
    if (!Composite)
    {
        statFinish(false);
        return;
    }

    // 2) 按脚本声明的顺序合成到同一张画面：
    //    order 小的先画（地图 -1），主画面 0 在其上；每个 manager 用自己
    //    根图层的实际合成类型：ltAddAlpha（地图）→ bmAddAlpha，ltAlpha
    //    （主画面容器）→ bmAlpha，ltOpaque（真不透明管理器）→ bmCopy。
    std::stable_sort(items.begin(), items.end(), [](const DrawItem& a, const DrawItem& b) {
        return a.Order != b.Order ? a.Order < b.Order : a.Index < b.Index;
    });

    Composite->Fill(tTVPRect(0, 0, screenW, screenH), (tjs_uint32)ClearColor);

    for (const DrawItem& it : items)
    {
        if (DebugOnlyManager >= 0 && it.Index != DebugOnlyManager)
            continue;
        if (!it.Src)
            continue;

        tTVPRect dest = it.DestRect;
        if (dest.left < 0) dest.left = 0;
        if (dest.top < 0) dest.top = 0;
        if (dest.right > screenW) dest.right = screenW;
        if (dest.bottom > screenH) dest.bottom = screenH;
        if (dest.get_width() <= 0 || dest.get_height() <= 0)
            continue;

        tTVPBBBltMethod method;
        switch (it.LayerType)
        {
            case ltAddAlpha:
                method = bmAddAlpha;
                break;
            case ltAdditive:
                method = bmAdd;
                break;
            case ltSubtractive:
                method = bmSub;
                break;
            case ltMultiplicative:
                method = bmMul;
                break;
            case ltAlpha:
                method = bmAlpha;
                break;
            default:
                method = bmCopy;
                break;
        }

        Composite->StretchBlt(tTVPRect(0, 0, screenW, screenH), dest, it.Src, it.SrcRect, method,
                              it.Opacity, false, stNearest);
    }
    CompositeDirty = false;
    statFinish(true);
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
// 说明：原版 D2Dm 的 GPU 路径需要引擎提供 primaryLayers/backIndex/frontIndex
// 与 GPUBaseLayer 的原生图层模型（baselayer.tjs 依赖），本实现没有这套结构。
// 强行声明为 GPUDrawDevice 会在 baselayer.tjs 处抛异常，因此保持软件设备语义。
NCB_REGISTER_CLASS(DrawDeviceD2D)
{
    NCB_CONSTRUCTOR(());
    NCB_PROPERTY_RO(interface, getInterface);
    NCB_PROPERTY(width, getWidth, setWidth);
    NCB_PROPERTY(height, getHeight, setHeight);
    NCB_METHOD(setSize);
    NCB_PROPERTY(clearColor, getClearColor, setClearColor);
    NCB_PROPERTY(layerManagerIndex, getLayerManagerIndex, setLayerManagerIndex);
    NCB_PROPERTY(debugOnlyManager, getDebugOnlyManager, setDebugOnlyManager);
    NCB_METHOD(probeBuffer);
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
    NCB_METHOD(dump);
}
