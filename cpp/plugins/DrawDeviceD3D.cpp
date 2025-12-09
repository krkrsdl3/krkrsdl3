#include "tjsCommHead.h"
#include "tjsNative.h"
#include "DrawDevice.h"
#include "TVPWindow.h"
#include "WindowIntf.h"
#include "ncbind/ncbind.hpp"
#include "Platform.h"

#include "tjsNativeLayer.h"

// 经过多日研究，我认为DrawDeviceD3D这条渲染管线其实并不需要实现
// 唯一需要的一点小技巧是把D3DEmote在此处间接搞定(因为有些游戏只有D3DEmote)
// 做法其实挺简单，将AffineSourceMotion适配成D3DAffineSourceEmote/D3DAffineSourceMotion就行了
// 至于下面的东西也是一些研究成果，就留个档吧
#if 0
#include <opencv2/opencv.hpp>
#include <SDL3/SDL.h>

#pragma region DrawDevice

// DrawDeviceD3D的核心原理，目前来看就是将原来单layer在tjs2的机制，拆分移到底层来提高效率
// 所以 Managers.size() == 4 是必须的
// [0]:无用的非D3D层 <- 非D3D时只有这一层
// [1]:fore [2]:back
// [3]:uibase
// D3DLayer基本在fore/back两处均有备份，通过Visible来切换显示
// D3DImage则是作为ref引用其他layer来充当图像
// D3DPicture最大的作用就算用来接受我们构造的D3DLayer
// startTransition 先stop-back/fore再由fore/back过渡的back/fore(在过渡的时长中，应当显示两者的过渡层并拒绝事件)
// stopTransition stop-fore/back
// 实际显示的时候，应该为fore/back+uibase，其中fore/back由transition进行切换(原因是kag.fore/back会互相交换，而原始渲染中在Layer有一个内嵌交换)
// 但是，我们是懒惰的，所以尽可能想办法使用现有的框架来缝合

//---------------------------------------------------------------------------
//! @brief		「D3D」デバイス(もっとも基本的な描画を行うのみのデバイス)
//---------------------------------------------------------------------------
class tTVPDrawDeviceD3D;
class D3DPicture;
class D3DLayer
{
public:
    D3DLayer(iTJSDispatch2* drawDevice);
    ~D3DLayer();
    void onUpdate(tjs_int64 tick);

    static tjs_int8 getDrawPlaneBoth() { return 2; }
    static tjs_int8 getDrawPlaneFront() { return 0; }
    static tjs_int8 getDrawPlaneBack() { return 1; }
    tjs_int getDrawPlane() { return _drawPlane; }
    void setDrawPlane(tjs_int p);
    tjs_int getFrontIndex() { return _frontIndex; }
    void setFrontIndex(tjs_int p)
    {
        _frontIndex = p;
        _thisFore->SetAbsoluteOrderIndex(_frontIndex);
    }
    tjs_int getBackIndex() { return _backIndex; }
    void setBackIndex(tjs_int p)
    {
        _backIndex = p;
        _thisBack->SetAbsoluteOrderIndex(_backIndex);
    }
    bool getVisible() { return _visible; }
    void setVisible(bool p)
    {
        _visible = p;
        _thisFore->SetVisible(_visible);
    }
    tTJSNI_Layer* getMainLayer(int idx)
    {
        if (idx == 0)
            return _thisFore;
        else
            return _thisBack;
    };
    void setUpdateClo(tTJSVariantClosure clo) { _clo = clo; }
    void setD3DPicture(D3DPicture* picRef) { _dataPic = picRef; }
    void setMatrix(tjs_real m11,
                   tjs_real m12,
                   tjs_real m13,
                   tjs_real m14,
                   tjs_real m21,
                   tjs_real m22,
                   tjs_real m23,
                   tjs_real m24,
                   tjs_real m31,
                   tjs_real m32,
                   tjs_real m33,
                   tjs_real m34,
                   tjs_real m41,
                   tjs_real m42,
                   tjs_real m43,
                   tjs_real m44)
    {
        affine_mat = (cv::Mat_<double>(2, 3) << m11, m12, m14, m21, m22, m24);
    }
    void piledCopy(iTJSDispatch2* orgLayer);

private:
    tTVPDrawDeviceD3D* _d3dDevice = nullptr;
    D3DPicture* _dataPic = nullptr;
    tTJSVariantClosure _clo = NULL;
    tTJSNI_Layer* _thisFore = nullptr;
    tTJSNI_Layer* _thisBack = nullptr;
    tjs_int _drawPlane = 0, _frontIndex = -1, _backIndex = -1;
    bool _visible = true;
    cv::Mat affine_mat;
};
class D3DImage
{
public:
    D3DImage(iTJSDispatch2* drawDevice) {}
    ~D3DImage() {}

    void load(iTJSDispatch2* reflay);
    tjs_int getWidth() { return _width; }
    tjs_int getHeight() { return _height; }
    tTJSNI_Layer* getRefLayer() { return _ths; };

private:
    tTJSNI_Layer* _ths = nullptr;
    tjs_int _width = 0, _height = 0;
};
class D3DPicture
{
public:
    D3DPicture(iTJSDispatch2* d3dlay, iTJSDispatch2* img);
    ~D3DPicture()
    {
        if (imgData != nullptr)
            delete[] imgData;
    }

    void setCoord(tjs_int coordX, tjs_int coordY)
    {
        _coordX = coordX;
        _coordY = coordY;
    }
    tjs_int getBlendMode() { return _blendMode; }
    void setBlendMode(tjs_int p) { _blendMode = p; }
    tjs_int getOpacity() { return _opacity; }
    void setOpacity(tjs_int p) { _opacity = p; }
    void assignImageRange(tjs_int left,
                          tjs_int top,
                          tjs_int width,
                          tjs_int height,
                          tjs_int tgtleft,
                          tjs_int tgtright);

    // 懒得封装
    tjs_uint8* imgData = nullptr;
    tjs_int imgWidth = 0, imgHeight = 0;

private:
    tjs_int _coordX = 0, _coordY = 0, _blendMode = 0, _opacity = 0;
    D3DLayer* _d3dlay = NULL;
    tjs_int _tgtleft = 0, _tgtright = 0;
    tTVPRect _rect;
};

class tTVPDrawDeviceD3D : public iTVPDrawDevice
{
    typedef iTVPDrawDevice inherited;

    iTVPWindow* Window;
    std::vector<iTVPLayerManager*> Managers; //!< レイヤマネージャの配列
    tTVPRect DestRect;                       //!< 描画先位置
    std::vector<D3DLayer*> updateLayer;      //主要用于从drawdevice的update触发layer的update
    tjs_uint8 PrimaryLayerManagerIndex = 1;         //!< プライマリレイヤマネージャ

public:
    tTVPDrawDeviceD3D(); //!< コンストラクタ

private:
    ~tTVPDrawDeviceD3D(); //!< デストラクタ

public:
    //! @brief		指定位置にあるレイヤマネージャを得る
    //! @param		index		インデックス(0～)
    //! @return
    //! 指定位置にあるレイヤマネージャ(AddRefされないので注意)。
    //!				指定位置にレイヤマネージャがなければNULLが返る
    iTVPLayerManager* GetLayerManagerAt(size_t index)
    {
        if (Managers.size() <= index)
            return nullptr;
        return Managers[index];
    }

    //---- オブジェクト生存期間制御
    virtual void TJS_INTF_METHOD Destruct();

    //---- window interface 関連
    virtual void TJS_INTF_METHOD SetWindowInterface(iTVPWindow* window);

    //---- LayerManager の管理関連
    virtual void TJS_INTF_METHOD AddLayerManager(iTVPLayerManager* manager);
    virtual void TJS_INTF_METHOD RemoveLayerManager(iTVPLayerManager* manager);

    //---- 描画位置・サイズ関連
    virtual void TJS_INTF_METHOD SetDestRectangle(const tTVPRect& rect);
    virtual void TJS_INTF_METHOD SetWindowSize(tjs_int w, tjs_int h);
    virtual void TJS_INTF_METHOD SetClipRectangle(const tTVPRect& rect);
    virtual void TJS_INTF_METHOD GetSrcSize(tjs_int& w, tjs_int& h);
    virtual void TJS_INTF_METHOD NotifyLayerResize(iTVPLayerManager* manager);
    virtual void TJS_INTF_METHOD NotifyLayerImageChange(iTVPLayerManager* manager);

    //---- ユーザーインターフェース関連
    // window → drawdevice
    virtual void TJS_INTF_METHOD OnClick(tjs_int x, tjs_int y);
    virtual void TJS_INTF_METHOD OnDoubleClick(tjs_int x, tjs_int y);
    virtual void TJS_INTF_METHOD OnMouseDown(tjs_int x,
                                             tjs_int y,
                                             tTVPMouseButton mb,
                                             tjs_uint32 flags);
    virtual void TJS_INTF_METHOD OnMouseUp(tjs_int x,
                                           tjs_int y,
                                           tTVPMouseButton mb,
                                           tjs_uint32 flags);
    virtual void TJS_INTF_METHOD OnMouseMove(tjs_int x, tjs_int y, tjs_uint32 flags);
    virtual void TJS_INTF_METHOD OnReleaseCapture();
    virtual void TJS_INTF_METHOD OnMouseOutOfWindow();
    virtual void TJS_INTF_METHOD OnKeyDown(tjs_uint key, tjs_uint32 shift);
    virtual void TJS_INTF_METHOD OnKeyUp(tjs_uint key, tjs_uint32 shift);
    virtual void TJS_INTF_METHOD OnKeyPress(tjs_char key);
    virtual void TJS_INTF_METHOD OnMouseWheel(tjs_uint32 shift,
                                              tjs_int delta,
                                              tjs_int x,
                                              tjs_int y);
    virtual void TJS_INTF_METHOD
    OnTouchDown(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id);
    virtual void TJS_INTF_METHOD
    OnTouchUp(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id);
    virtual void TJS_INTF_METHOD
    OnTouchMove(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id);
    virtual void TJS_INTF_METHOD
    OnTouchScaling(tjs_real startdist, tjs_real curdist, tjs_real cx, tjs_real cy, tjs_int flag);
    virtual void TJS_INTF_METHOD OnTouchRotate(tjs_real startangle,
                                               tjs_real curangle,
                                               tjs_real dist,
                                               tjs_real cx,
                                               tjs_real cy,
                                               tjs_int flag);
    virtual void TJS_INTF_METHOD OnMultiTouch();
    virtual void TJS_INTF_METHOD OnDisplayRotate(
        tjs_int orientation, tjs_int rotate, tjs_int bpp, tjs_int width, tjs_int height);
    virtual void TJS_INTF_METHOD RecheckInputState();

    // layer manager → drawdevice
    virtual void TJS_INTF_METHOD SetDefaultMouseCursor(iTVPLayerManager* manager);
    virtual void TJS_INTF_METHOD SetMouseCursor(iTVPLayerManager* manager, tjs_int cursor);
    virtual void TJS_INTF_METHOD GetCursorPos(iTVPLayerManager* manager, tjs_int& x, tjs_int& y);
    virtual void TJS_INTF_METHOD SetCursorPos(iTVPLayerManager* manager, tjs_int x, tjs_int y);
    virtual void TJS_INTF_METHOD SetHintText(iTVPLayerManager* manager,
                                             iTJSDispatch2* sender,
                                             const ttstr& text);
    virtual void TJS_INTF_METHOD WindowReleaseCapture(iTVPLayerManager* manager);

    virtual void TJS_INTF_METHOD SetAttentionPoint(iTVPLayerManager* manager,
                                                   tTJSNI_BaseLayer* layer,
                                                   tjs_int l,
                                                   tjs_int t);
    virtual void TJS_INTF_METHOD DisableAttentionPoint(iTVPLayerManager* manager);
    virtual void TJS_INTF_METHOD SetImeMode(iTVPLayerManager* manager, tTVPImeMode mode);
    virtual void TJS_INTF_METHOD ResetImeMode(iTVPLayerManager* manager);

    //---- プライマリレイヤ関連
    virtual tTJSNI_BaseLayer* TJS_INTF_METHOD GetPrimaryLayer();
    virtual tTJSNI_BaseLayer* TJS_INTF_METHOD GetFocusedLayer();
    virtual void TJS_INTF_METHOD SetFocusedLayer(tTJSNI_BaseLayer* layer);

    //---- 再描画関連
    virtual void TJS_INTF_METHOD RequestInvalidation(const tTVPRect& rect);
    virtual void TJS_INTF_METHOD Update();
    virtual void TJS_INTF_METHOD Show();
    virtual bool TJS_INTF_METHOD WaitForVBlank(tjs_int* in_vblank, tjs_int* delayed);

    //---- デバッグ支援
    virtual void TJS_INTF_METHOD DumpLayerStructure();
    virtual void TJS_INTF_METHOD SetShowUpdateRect(bool b);

    //---- フルスクリーン
    virtual bool TJS_INTF_METHOD SwitchToFullScreen(
        int window, tjs_uint w, tjs_uint h, tjs_uint bpp, tjs_uint color, bool changeresolution);
    virtual void TJS_INTF_METHOD
    RevertFromFullScreen(int window, tjs_uint w, tjs_uint h, tjs_uint bpp, tjs_uint color);

    //---- LayerManager からの画像受け渡し関連
    virtual void TJS_INTF_METHOD StartBitmapCompletion(iTVPLayerManager* manager);
    virtual void TJS_INTF_METHOD NotifyBitmapCompleted(iTVPLayerManager* manager,
                                                       tjs_int x,
                                                       tjs_int y,
                                                       tTVPBaseTexture* bmp,
                                                       const tTVPRect& cliprect,
                                                       tTVPLayerType type,
                                                       tjs_int opacity);
    virtual void TJS_INTF_METHOD EndBitmapCompletion(iTVPLayerManager* manager);

public:
    void setScreenRect(const tTVPRect& rect);
    void setPrimarySize(tjs_int w, tjs_int h);
    void setOffset(tjs_int x, tjs_int y);
    void onUpdate(tjs_int64 tick);
    void addD3DLayer(D3DLayer* lay);
    void removeD3DLayer(D3DLayer* lay);
    void startTransition(tTJSVariant* params);
    void stopTransition();
    void capture(iTJSDispatch2* tgtlay, tjs_uint8 unk);
    tjs_int8 GetPrimaryIndex() { return PrimaryLayerManagerIndex; };

    tTJSNI_BaseLayer* GetPrimaryLayer(tjs_int idx);
    tTJSNI_BaseWindow* GetKag();

    iTJSDispatch2* primaryLayers = nullptr;
};
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------

tTVPDrawDeviceD3D::tTVPDrawDeviceD3D()
{
    // コンストラクタ
    Window = NULL;
    DestRect.clear();
    primaryLayers = TJSCreateArrayObject();
}
//---------------------------------------------------------------------------
tTVPDrawDeviceD3D::~tTVPDrawDeviceD3D()
{
    std::vector<iTVPLayerManager*> backup = Managers;
    for (auto& i : backup)
        i->Release();
    Managers.clear();
    if (primaryLayers != nullptr)
    {
        primaryLayers->Release();
        primaryLayers = nullptr;
    }
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::Destruct()
{
    delete this;
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::SetWindowInterface(iTVPWindow* window)
{
    Window = window;
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::AddLayerManager(iTVPLayerManager* manager)
{
    Managers.push_back(manager);
    manager->AddRef();
    manager->SetDesiredLayerType(ltOpaque); // ltOpaque な出力を受け取りたい
    tTJSVariant pl(manager->GetPrimaryLayer());
    tTJSVariant* args[] = {&pl};
    static tjs_uint addHint = 0;

    primaryLayers->FuncCall(0, TJS_W("add"), &addHint, nullptr, 1, args, primaryLayers);
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::RemoveLayerManager(iTVPLayerManager* manager)
{
    tTJSVariant pl(manager->GetPrimaryLayer());
    tTJSVariant* args[] = {&pl};
    static tjs_uint addHint = 0;
    primaryLayers->FuncCall(0, TJS_W("remove"), &addHint, nullptr, 1, args, primaryLayers);
    std::vector<iTVPLayerManager*>::iterator i =
        std::find(Managers.begin(), Managers.end(), manager);
    if (i == Managers.end())
        TVPThrowInternalError;
    (*i)->Release();
    Managers.erase(i);
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::SetDestRectangle(const tTVPRect& rect)
{
    DestRect = rect;
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::SetClipRectangle(const tTVPRect& rect)
{
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::GetSrcSize(tjs_int& w, tjs_int& h)
{
    if (Managers.size() > 0 && Managers.at(0))
    {
        if (!Managers.at(0)->GetPrimaryLayerSize(w, h))
        {
            w = 0;
            h = 0;
        }
    }
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::NotifyLayerResize(iTVPLayerManager* manager)
{
    for (auto mgn : Managers)
    {
        if (mgn == manager)
            Window->NotifySrcResize();
    }
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::NotifyLayerImageChange(iTVPLayerManager* manager)
{
    for (auto mgn : Managers)
    {
        if (mgn == manager)
            Window->RequestUpdate();
    }
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::OnClick(tjs_int x, tjs_int y)
{
    iTVPLayerManager* manager = GetLayerManagerAt(PrimaryLayerManagerIndex);
    if (!manager)
        return;
    manager->NotifyClick(x, y);
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::OnDoubleClick(tjs_int x, tjs_int y)
{
    iTVPLayerManager* manager = GetLayerManagerAt(PrimaryLayerManagerIndex);
    if (!manager)
        return;
    manager->NotifyDoubleClick(x, y);
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::OnMouseDown(tjs_int x,
                                                    tjs_int y,
                                                    tTVPMouseButton mb,
                                                    tjs_uint32 flags)
{
    iTVPLayerManager* manager = GetLayerManagerAt(PrimaryLayerManagerIndex);
    if (!manager)
        return;
    manager->NotifyMouseDown(x, y, mb, flags);
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::OnMouseUp(tjs_int x,
                                                  tjs_int y,
                                                  tTVPMouseButton mb,
                                                  tjs_uint32 flags)
{
    iTVPLayerManager* manager = GetLayerManagerAt(PrimaryLayerManagerIndex);
    if (!manager)
        return;
    manager->NotifyMouseUp(x, y, mb, flags);
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::OnMouseMove(tjs_int x, tjs_int y, tjs_uint32 flags)
{
    iTVPLayerManager* manager = GetLayerManagerAt(PrimaryLayerManagerIndex);
    if (!manager)
        return;
    manager->NotifyMouseMove(x, y, flags);
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::OnReleaseCapture()
{
    iTVPLayerManager* manager = GetLayerManagerAt(PrimaryLayerManagerIndex);
    if (!manager)
        return;
    manager->ReleaseCapture();
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::OnMouseOutOfWindow()
{
    iTVPLayerManager* manager = GetLayerManagerAt(PrimaryLayerManagerIndex);
    if (!manager)
        return;
    manager->NotifyMouseOutOfWindow();
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::OnKeyDown(tjs_uint key, tjs_uint32 shift)
{
    iTVPLayerManager* manager = GetLayerManagerAt(PrimaryLayerManagerIndex);
    if (!manager)
        return;
    manager->NotifyKeyDown(key, shift);
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::OnKeyUp(tjs_uint key, tjs_uint32 shift)
{
    iTVPLayerManager* manager = GetLayerManagerAt(PrimaryLayerManagerIndex);
    if (!manager)
        return;
    manager->NotifyKeyUp(key, shift);
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::OnKeyPress(tjs_char key)
{
    iTVPLayerManager* manager = GetLayerManagerAt(PrimaryLayerManagerIndex);
    if (!manager)
        return;
    manager->NotifyKeyPress(key);
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::OnMouseWheel(tjs_uint32 shift,
                                                     tjs_int delta,
                                                     tjs_int x,
                                                     tjs_int y)
{
    iTVPLayerManager* manager = GetLayerManagerAt(PrimaryLayerManagerIndex);
    if (!manager)
        return;
    manager->NotifyMouseWheel(shift, delta, x, y);
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD
tTVPDrawDeviceD3D::OnTouchDown(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id)
{
    iTVPLayerManager* manager = GetLayerManagerAt(PrimaryLayerManagerIndex);
    if (!manager)
        return;
    manager->NotifyTouchDown(x, y, cx, cy, id);
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD
tTVPDrawDeviceD3D::OnTouchUp(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id)
{
    iTVPLayerManager* manager = GetLayerManagerAt(PrimaryLayerManagerIndex);
    if (!manager)
        return;
    manager->NotifyTouchUp(x, y, cx, cy, id);
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD
tTVPDrawDeviceD3D::OnTouchMove(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id)
{
    iTVPLayerManager* manager = GetLayerManagerAt(PrimaryLayerManagerIndex);
    if (!manager)
        return;
    manager->NotifyTouchMove(x, y, cx, cy, id);
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::OnTouchScaling(
    tjs_real startdist, tjs_real curdist, tjs_real cx, tjs_real cy, tjs_int flag)
{
    iTVPLayerManager* manager = GetLayerManagerAt(PrimaryLayerManagerIndex);
    if (!manager)
        return;
    manager->NotifyTouchScaling(startdist, curdist, cx, cy, flag);
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::OnTouchRotate(
    tjs_real startangle, tjs_real curangle, tjs_real dist, tjs_real cx, tjs_real cy, tjs_int flag)
{
    iTVPLayerManager* manager = GetLayerManagerAt(PrimaryLayerManagerIndex);
    if (!manager)
        return;
    manager->NotifyTouchRotate(startangle, curangle, dist, cx, cy, flag);
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::OnMultiTouch()
{
    iTVPLayerManager* manager = GetLayerManagerAt(PrimaryLayerManagerIndex);
    if (!manager)
        return;
    manager->NotifyMultiTouch();
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::OnDisplayRotate(
    tjs_int orientation, tjs_int rotate, tjs_int bpp, tjs_int width, tjs_int height)
{
    // 何もしない
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::RecheckInputState()
{
    iTVPLayerManager* manager = GetLayerManagerAt(PrimaryLayerManagerIndex);
    if (!manager)
        return;
    manager->RecheckInputState();
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::SetDefaultMouseCursor(iTVPLayerManager* manager)
{
    for (auto mgn : Managers)
    {
        if (mgn == manager)
            Window->SetDefaultMouseCursor();
    }
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::SetMouseCursor(iTVPLayerManager* manager, tjs_int cursor)
{
    for (auto mgn : Managers)
    {
        if (mgn == manager)
            Window->SetMouseCursor(cursor);
    }
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::GetCursorPos(iTVPLayerManager* manager,
                                                     tjs_int& x,
                                                     tjs_int& y)
{
    bool hasGet = false;
    for (auto mgn : Managers)
    {
        if (mgn == manager)
            hasGet = true;
    }
    Window->GetCursorPos(x, y);
    if (!hasGet)
    {
        // プライマリレイヤマネージャ以外には座標 0,0 で渡しておく
        x = 0;
        y = 0;
    }
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::SetCursorPos(iTVPLayerManager* manager,
                                                     tjs_int x,
                                                     tjs_int y)
{
    for (auto mgn : Managers)
    {
        if (mgn == manager)
            Window->SetCursorPos(x, y);
    }
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::WindowReleaseCapture(iTVPLayerManager* manager)
{
    for (auto mgn : Managers)
    {
        if (mgn == manager)
            Window->WindowReleaseCapture();
    }
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::SetHintText(iTVPLayerManager* manager,
                                                    iTJSDispatch2* sender,
                                                    const ttstr& text)
{
    for (auto mgn : Managers)
    {
        if (mgn == manager)
            Window->SetHintText(sender, text);
    }
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::SetAttentionPoint(iTVPLayerManager* manager,
                                                          tTJSNI_BaseLayer* layer,
                                                          tjs_int l,
                                                          tjs_int t)
{
    for (auto mgn : Managers)
    {
        if (mgn == manager)
            Window->SetAttentionPoint(layer, l, t);
    }
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::DisableAttentionPoint(iTVPLayerManager* manager)
{
    for (auto mgn : Managers)
    {
        if (mgn == manager)
            Window->DisableAttentionPoint();
    }
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::SetImeMode(iTVPLayerManager* manager, tTVPImeMode mode)
{
    for (auto mgn : Managers)
    {
        if (mgn == manager)
            Window->SetImeMode(mode);
    }
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::ResetImeMode(iTVPLayerManager* manager)
{
    for (auto mgn : Managers)
    {
        if (mgn == manager)
            Window->ResetImeMode();
    }
}
//---------------------------------------------------------------------------
tTJSNI_BaseLayer* TJS_INTF_METHOD tTVPDrawDeviceD3D::GetPrimaryLayer()
{
    if (Managers.size() != 4)
        return NULL;
    iTVPLayerManager* manager = GetLayerManagerAt(PrimaryLayerManagerIndex);
    if (!manager)
        return NULL;
    return manager->GetPrimaryLayer();
}
//---------------------------------------------------------------------------
tTJSNI_BaseLayer* TJS_INTF_METHOD tTVPDrawDeviceD3D::GetFocusedLayer()
{
    if (Managers.size() != 4)
        return NULL;
    iTVPLayerManager* manager = GetLayerManagerAt(PrimaryLayerManagerIndex);
    if (!manager)
        return NULL;
    return manager->GetFocusedLayer();
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::SetFocusedLayer(tTJSNI_BaseLayer* layer)
{
    if (Managers.size() != 4)
        return;
    iTVPLayerManager* manager = GetLayerManagerAt(PrimaryLayerManagerIndex);
    if (!manager)
        return;
    manager->SetFocusedLayer(layer);
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::RequestInvalidation(const tTVPRect& rect)
{
    tjs_int l = rect.left, t = rect.top, r = rect.right, b = rect.bottom;
    r++; // 誤差の吸収(本当はもうちょっと厳密にやらないとならないがそれが問題になることはない)
    b++;

    if (Managers.size() != 4)
        return;
    iTVPLayerManager* manager = GetLayerManagerAt(PrimaryLayerManagerIndex);
    if (!manager)
        return;
    manager->RequestInvalidation(tTVPRect(l, t, r, b));
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::Update()
{
    // すべての layer manager の UpdateToDrawDevice を呼ぶ
    for (auto mgn : Managers)
    {
        mgn->UpdateToDrawDevice();
    }
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::Show()
{
    if (Window)
    {
        iWindowLayer* form = Window->GetForm();
        if (form && Managers.size() > 2)
        {
            iTVPBaseBitmap* buf = Managers.at(PrimaryLayerManagerIndex)->GetDrawBuffer();
            if (buf)
                form->UpdateDrawBuffer(buf->GetTexture());

            tjs_uint8* data =
                (tjs_uint8*)Managers.at(0)->GetDrawBuffer()->GetTexture()->GetPixelData();
            if (data != nullptr)
            {
                cv::Mat rgba(Managers.at(0)->GetDrawBuffer()->GetTexture()->GetHeight(),
                             Managers.at(0)->GetDrawBuffer()->GetTexture()->GetWidth(), CV_8UC4,
                             data);
                // cv::imshow("0", rgba);
            }
            data = (tjs_uint8*)Managers.at(1)->GetDrawBuffer()->GetTexture()->GetPixelData();
            if (data != nullptr)
            {
                cv::Mat rgba(Managers.at(1)->GetDrawBuffer()->GetTexture()->GetHeight(),
                             Managers.at(1)->GetDrawBuffer()->GetTexture()->GetWidth(), CV_8UC4,
                             data);
                cv::imshow("1", rgba);
            }
            data = (tjs_uint8*)Managers.at(2)->GetDrawBuffer()->GetTexture()->GetPixelData();
            if (data != nullptr)
            {
                cv::Mat rgba(Managers.at(2)->GetDrawBuffer()->GetTexture()->GetHeight(),
                             Managers.at(2)->GetDrawBuffer()->GetTexture()->GetWidth(), CV_8UC4,
                             data);
                cv::imshow("2", rgba);
            }
            data = (tjs_uint8*)Managers.at(3)->GetDrawBuffer()->GetTexture()->GetPixelData();
            if (data != nullptr)
            {
                cv::Mat rgba(Managers.at(3)->GetDrawBuffer()->GetTexture()->GetHeight(),
                             Managers.at(3)->GetDrawBuffer()->GetTexture()->GetWidth(), CV_8UC4,
                             data);
                // cv::imshow("3", rgba);
            }
        }
    }
}
//---------------------------------------------------------------------------
bool TJS_INTF_METHOD tTVPDrawDeviceD3D::WaitForVBlank(tjs_int* in_vblank, tjs_int* delayed)
{
    return false;
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::DumpLayerStructure()
{
    // すべての layer manager の DumpLayerStructure を呼ぶ
    for (auto mgn : Managers)
    {
        mgn->DumpLayerStructure();
    }
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::SetShowUpdateRect(bool b)
{
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::SetWindowSize(tjs_int w, tjs_int h)
{
}
//---------------------------------------------------------------------------
bool TJS_INTF_METHOD tTVPDrawDeviceD3D::SwitchToFullScreen(
    int window, tjs_uint w, tjs_uint h, tjs_uint bpp, tjs_uint color, bool changeresolution)
{
    return true;
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::RevertFromFullScreen(
    int window, tjs_uint w, tjs_uint h, tjs_uint bpp, tjs_uint color)
{
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::StartBitmapCompletion(iTVPLayerManager* manager)
{
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::NotifyBitmapCompleted(iTVPLayerManager* manager,
                                                              tjs_int x,
                                                              tjs_int y,
                                                              tTVPBaseTexture* bmp,
                                                              const tTVPRect& cliprect,
                                                              tTVPLayerType type,
                                                              tjs_int opacity)
{
    // bits, bitmapinfo で表されるビットマップの cliprect の領域を、x, y に描画
    // する。
    // opacity と type は無視するしかないので無視する
    tjs_int w, h;
    GetSrcSize(w, h);
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPDrawDeviceD3D::EndBitmapCompletion(iTVPLayerManager* manager)
{
}
//---------------------------------------------------------------------------
void tTVPDrawDeviceD3D::setScreenRect(const tTVPRect& rect)
{
    for (auto mgn : Managers)
    {
        mgn->GetPrimaryLayer()->SetBounds(rect);
    }
}
//---------------------------------------------------------------------------
void tTVPDrawDeviceD3D::setPrimarySize(tjs_int w, tjs_int h)
{
    for (auto mgn : Managers)
    {
        mgn->GetPrimaryLayer()->SetSize(w, h);
    }
}
//---------------------------------------------------------------------------
void tTVPDrawDeviceD3D::setOffset(tjs_int x, tjs_int y)
{
    for (auto mgn : Managers)
    {
        mgn->GetPrimaryLayer()->SetPosition(x, y);
    }
}
//---------------------------------------------------------------------------
void tTVPDrawDeviceD3D::onUpdate(tjs_int64 tick)
{
    for (auto itm : updateLayer)
    {
        itm->onUpdate(tick);
    }
}
//---------------------------------------------------------------------------
void tTVPDrawDeviceD3D::addD3DLayer(D3DLayer* lay)
{
    updateLayer.push_back(lay);
}
//---------------------------------------------------------------------------
void tTVPDrawDeviceD3D::removeD3DLayer(D3DLayer* lay)
{
    updateLayer.erase(std::remove_if(updateLayer.begin(), updateLayer.end(),
                                     [&](D3DLayer* itm) { return itm == lay; }),
                      updateLayer.end());
}
//---------------------------------------------------------------------------
void tTVPDrawDeviceD3D::startTransition(tTJSVariant* params)
{
    if (PrimaryLayerManagerIndex == 1)
        PrimaryLayerManagerIndex = 2;
    else
        PrimaryLayerManagerIndex = 1;
}
//---------------------------------------------------------------------------
void tTVPDrawDeviceD3D::stopTransition()
{
}
//---------------------------------------------------------------------------
void tTVPDrawDeviceD3D::capture(iTJSDispatch2* tgtlay, tjs_uint8 unk)
{
    // 获取父类实例
    tTJSNI_Layer* _ths = NULL;
    if (tgtlay->NativeInstanceSupport(TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                                      (iTJSNativeInstance**)&_ths) < 0 ||
        _ths == NULL)
        return;
    tTJSNI_BaseLayer* _src = GetPrimaryLayer();
    tTVPRect rect(0, 0, _src->GetImageWidth(), _src->GetImageHeight());
    _ths->PiledCopy(0, 0, _src, rect);
}
//---------------------------------------------------------------------------
tTJSNI_BaseLayer* tTVPDrawDeviceD3D::GetPrimaryLayer(tjs_int idx)
{
    if (idx < 0 || idx >= Managers.size())
        return NULL;
    return Managers.at(idx)->GetPrimaryLayer();
}
//---------------------------------------------------------------------------
tTJSNI_BaseWindow* tTVPDrawDeviceD3D::GetKag()
{
    if (auto* derived = dynamic_cast<tTJSNI_BaseWindow*>(Window))
    {
        return derived;
    }
    return NULL;
}
//---------------------------------------------------------------------------

#pragma endregion

#pragma region Instance_Class

//---------------------------------------------------------------------------
// tTJSNI_DrawDeviceD3D
//---------------------------------------------------------------------------
class tTJSNI_DrawDeviceD3D : public tTJSNativeInstance
{
    typedef tTJSNativeInstance inherited;

    tTVPDrawDeviceD3D* Device;

public:
    tTJSNI_DrawDeviceD3D();
    ~tTJSNI_DrawDeviceD3D();
    tjs_error TJS_INTF_METHOD Construct(tjs_int numparams,
                                        tTJSVariant** param,
                                        iTJSDispatch2* tjs_obj);
    void TJS_INTF_METHOD Invalidate();

    bool _forceRenderTexture = false;
    tjs_uint32 _clearColor = 0x00000000;

public:
    tTVPDrawDeviceD3D* GetDevice() const { return Device; }
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_DrawDeviceD3D
//---------------------------------------------------------------------------
class tTJSNC_DrawDeviceD3D : public tTJSNativeClass
{
public:
    tTJSNC_DrawDeviceD3D();

    static tjs_uint32 ClassID;

private:
    iTJSNativeInstance* CreateNativeInstance();
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_DrawDeviceD3D : BasicDrawDevice TJS native class
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_DrawDeviceD3D::ClassID = (tjs_uint32)-1;
tTJSNC_DrawDeviceD3D::tTJSNC_DrawDeviceD3D()
  : tTJSNativeClass(TJS_W("DrawDeviceD3D")){
        // register native methods/properties

        TJS_BEGIN_NATIVE_MEMBERS(DrawDeviceD3D) TJS_DECL_EMPTY_FINALIZE_METHOD
            //----------------------------------------------------------------------
            // constructor/methods
            //----------------------------------------------------------------------
            TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(
                /*var.name*/ _this,
                /*var.type*/ tTJSNI_DrawDeviceD3D,
                /*TJS class name*/ DrawDeviceD3D){return TJS_S_OK;
}
TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/ DrawDeviceD3D)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ recreate)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_DrawDeviceD3D);
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ recreate)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ checkEnable)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_DrawDeviceD3D);
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ checkEnable)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setScreenRect)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_DrawDeviceD3D);
    if (numparams == 4 && param[2]->Type() != tvtVoid && param[3]->Type() != tvtVoid)
    {
        // set bounds
        tTVPRect r;
        r.left = *param[0];
        r.top = *param[1];
        r.right = (tjs_int)*param[2] + r.left;
        r.bottom = (tjs_int)*param[3] + r.top;
        _this->GetDevice()->setScreenRect(r);
    }
    else
        return TJS_E_INVALIDPARAM;
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setScreenRect)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setPrimarySize)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_DrawDeviceD3D);
    if (numparams < 2)
        return TJS_E_BADPARAMCOUNT;
    _this->GetDevice()->setPrimarySize((tjs_int)*param[0], (tjs_int)*param[1]);
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setPrimarySize)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setOffset)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_DrawDeviceD3D);
    if (numparams < 2)
        return TJS_E_BADPARAMCOUNT;
    _this->GetDevice()->setOffset((tjs_int)*param[0], (tjs_int)*param[1]);
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setOffset)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ capture)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_DrawDeviceD3D);
    if (numparams < 2)
        return TJS_E_BADPARAMCOUNT;
    _this->GetDevice()->capture(*param[0], (tjs_int)*param[1]);
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ capture)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ update)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_DrawDeviceD3D);
    _this->GetDevice()->onUpdate((tjs_int64)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ update)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ startTransition)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_DrawDeviceD3D);
    _this->GetDevice()->startTransition(param[0]);
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ startTransition)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ stopTransition)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_DrawDeviceD3D);
    _this->GetDevice()->stopTransition();
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ stopTransition)
//----------------------------------------------------------------------

//---------------------------------------------------------------------------
//----------------------------------------------------------------------
// properties
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(interface){
    TJS_BEGIN_NATIVE_PROP_GETTER{TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                                         /*var. type*/ tTJSNI_DrawDeviceD3D);
*result = reinterpret_cast<tjs_int64>(_this->GetDevice());
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(interface)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(clearColor){
    TJS_BEGIN_NATIVE_PROP_GETTER{TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                                         /*var. type*/ tTJSNI_DrawDeviceD3D);
*result = (tjs_int64)_this->_clearColor;
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_DrawDeviceD3D);
    _this->_clearColor = (tjs_int64)*param;
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(clearColor)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(forceRenderTexture){
    TJS_BEGIN_NATIVE_PROP_GETTER{TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                                         /*var. type*/ tTJSNI_DrawDeviceD3D);
*result = _this->_forceRenderTexture;
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_DrawDeviceD3D);
    _this->_forceRenderTexture = *param;
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(forceRenderTexture)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(primaryLayers){
    TJS_BEGIN_NATIVE_PROP_GETTER{TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                                         /*var. type*/ tTJSNI_DrawDeviceD3D);
tTJSVariant layRet(_this->GetDevice()->primaryLayers, _this->GetDevice()->primaryLayers);
*result = layRet;
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(primaryLayers)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(layerManagerIndex){
    TJS_BEGIN_NATIVE_PROP_GETTER{TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                                         /*var. type*/ tTJSNI_DrawDeviceD3D);
*result = _this->GetDevice()->GetPrimaryIndex();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(layerManagerIndex)
//----------------------------------------------------------------------
#define TVP_REGISTER_PTDD_ENUM(name) \
    TJS_BEGIN_NATIVE_PROP_DECL(name){ \
        TJS_BEGIN_NATIVE_PROP_GETTER{* result = (tjs_int64)tTVPDrawDeviceD3D::name; \
    return TJS_S_OK; \
    } \
    TJS_END_NATIVE_PROP_GETTER \
    TJS_DENY_NATIVE_PROP_SETTER \
    } \
    TJS_END_NATIVE_PROP_DECL(name)
//----------------------------------------------------------------------
TJS_END_NATIVE_MEMBERS
}
//---------------------------------------------------------------------------
iTJSNativeInstance* tTJSNC_DrawDeviceD3D::CreateNativeInstance()
{
    return new tTJSNI_DrawDeviceD3D();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
tTJSNI_DrawDeviceD3D::tTJSNI_DrawDeviceD3D()
{
    Device = new tTVPDrawDeviceD3D();
}
//---------------------------------------------------------------------------
tTJSNI_DrawDeviceD3D::~tTJSNI_DrawDeviceD3D()
{
    if (Device)
        Device->Destruct(), Device = NULL;
}
//---------------------------------------------------------------------------
tjs_error TJS_INTF_METHOD tTJSNI_DrawDeviceD3D::Construct(tjs_int numparams,
                                                          tTJSVariant** param,
                                                          iTJSDispatch2* tjs_obj)
{
    return TJS_S_OK;
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTJSNI_DrawDeviceD3D::Invalidate()
{
    if (Device)
        Device->Destruct(), Device = NULL;
}
//---------------------------------------------------------------------------

tTJSNativeClass* TVPCreateNativeClass_DrawDeviceD3D()
{
    return new tTJSNC_DrawDeviceD3D();
}

void DrawDeviceD3D_init()
{
    iTJSDispatch2* global = TVPGetScriptDispatch();
    if (global)
    {
        iTJSDispatch2* tjsclass = TVPCreateNativeClass_DrawDeviceD3D();
        tTJSVariant val(tjsclass);
        tjsclass->Release();
        global->PropSet(TJS_MEMBERENSURE, TJS_W("DrawDeviceD3D"), NULL, &val, global);
        global->Release();
    }

    TVPExecuteScript(TJS_W(" class D3D extends DrawDeviceD3D { function D3D(exD3DWidth, exD3DHeight) { super.DrawDeviceD3D(exD3DWidth, exD3DHeight); } } "));
}

void DrawDeviceD3D_done()
{

    iTJSDispatch2* global = TVPGetScriptDispatch();
    if (global)
    {
        global->DeleteMember(0, TJS_W("DrawDeviceD3D"), NULL, global);
        global->Release();
    }
}

#pragma endregion

#pragma region D3DOther

D3DLayer::D3DLayer(iTJSDispatch2* drawDevice)
{
    // 获取drawDevice
    tTJSNI_DrawDeviceD3D* tDD = NULL;
    if (drawDevice->NativeInstanceSupport(TJS_NIS_GETINSTANCE, tTJSNC_DrawDeviceD3D::ClassID,
                                          (iTJSNativeInstance**)&tDD) < 0 ||
        tDD == NULL)
        TVPThrowExceptionMessage(TVPSpecifyLayer);
    _d3dDevice = tDD->GetDevice();
    _d3dDevice->addD3DLayer(this);

    iTJSDispatch2* global = TVPGetScriptDispatch();
    if (global)
    {
        tTJSVariant kagVar;
        if (TJS_FAILED(global->PropGet(0, TJS_W("kag"), NULL, &kagVar, global)))
        {
            TVPThrowExceptionMessage(TVPSpecifyLayer);
        }

        tTJSVariant baseLayer;
        // kag.fore.base
        iTJSDispatch2* kag = kagVar.AsObjectThisNoAddRef();
        if (TJS_FAILED(kag->PropGet(0, TJS_W("fore"), NULL, &baseLayer, kag)))
        {
            TVPThrowExceptionMessage(TVPSpecifyLayer);
        }
        kag = baseLayer.AsObjectThisNoAddRef();
        if (TJS_FAILED(kag->PropGet(0, TJS_W("base"), NULL, &baseLayer, kag)))
        {
            TVPThrowExceptionMessage(TVPSpecifyLayer);
        }
        if (baseLayer.Type() != tvtObject)
            TVPThrowExceptionMessage(TVPSpecifyLayer);
        // 创建fore实例
        _thisFore = new tTJSNI_Layer();
        tTJSVariant* params[] = {&kagVar, &baseLayer};
        iTJSDispatch2* objClass = ncbInstanceAdaptor<D3DLayer>::CreateAdaptor(this);
        if ((TJS_FAILED(_thisFore->Construct(2, params, objClass)) < 0))
            TVPThrowExceptionMessage(TVPSpecifyLayer);

        // kag.back.base
        kag = kagVar.AsObjectThisNoAddRef();
        if (TJS_FAILED(kag->PropGet(0, TJS_W("back"), NULL, &baseLayer, kag)))
        {
            TVPThrowExceptionMessage(TVPSpecifyLayer);
        }
        kag = baseLayer.AsObjectThisNoAddRef();
        if (TJS_FAILED(kag->PropGet(0, TJS_W("base"), NULL, &baseLayer, kag)))
        {
            TVPThrowExceptionMessage(TVPSpecifyLayer);
        }
        if (baseLayer.Type() != tvtObject)
            TVPThrowExceptionMessage(TVPSpecifyLayer);
        // 创建fore实例
        _thisBack = new tTJSNI_Layer();
        params[0] = &kagVar;
        params[1] = &baseLayer;
        objClass = ncbInstanceAdaptor<D3DLayer>::CreateAdaptor(this);
        if ((TJS_FAILED(_thisBack->Construct(2, params, objClass)) < 0))
            TVPThrowExceptionMessage(TVPSpecifyLayer);
        // 设置参数
        tjs_int dw = 0, dh = 0;
        _d3dDevice->GetSrcSize(dw, dh);
        _thisFore->SetSize(dw, dh);
        _thisFore->SetImageSize(dw, dh);
        _thisBack->SetSize(dw, dh);
        _thisBack->SetImageSize(dw, dh);
        _frontIndex = _thisFore->GetAbsoluteOrderIndex();
        _backIndex = _thisBack->GetAbsoluteOrderIndex();
    }
}
D3DLayer::~D3DLayer()
{
    if (_thisFore != nullptr)
    {
        _thisFore->Invalidate();
        delete _thisFore;
        _thisFore = nullptr;
    }
    if (_thisBack != nullptr)
    {
        _thisBack->Invalidate();
        delete _thisBack;
        _thisBack = nullptr;
    }
    _d3dDevice->removeD3DLayer(this);
}
void D3DLayer::onUpdate(tjs_int64 tick)
{
    if (_clo != NULL)
    {
        tTJSVariant _tik((tjs_int64)tick);
        tTJSVariant* vars[] = {&_tik};
        _clo.FuncCall(0, NULL, NULL, NULL, 1, vars, NULL);
    }
    if (_dataPic != nullptr)
    {
        getMainLayer(0)->SetSize(_dataPic->imgWidth, _dataPic->imgHeight);
        getMainLayer(0)->SetImageSize(_dataPic->imgWidth, _dataPic->imgHeight);
        tjs_uint8* buff = (tjs_uint8*)getMainLayer(0)->GetMainImagePixelBufferForWrite();
        memcpy(buff, _dataPic->imgData, _dataPic->imgWidth * _dataPic->imgHeight * 4);

        // cv::Mat imgSrc(src->GetImageHeight(), src->GetImageWidth(), CV_8UC4,
        //                (tjs_uint8*)src->GetMainImagePixelBuffer());
        getMainLayer(1)->SetSize(_dataPic->imgWidth, _dataPic->imgHeight);
        getMainLayer(1)->SetImageSize(_dataPic->imgWidth, _dataPic->imgHeight);
        buff = (tjs_uint8*)getMainLayer(1)->GetMainImagePixelBufferForWrite();
        memcpy(buff, _dataPic->imgData, _dataPic->imgWidth * _dataPic->imgHeight * 4);
    }
}
void D3DLayer::setDrawPlane(tjs_int p)
{
    if (p == 1)
    {
        _thisFore->SetVisible(false);
        _thisBack->SetVisible(true);
    }
    else
    {
        _thisFore->SetVisible(true);
        _thisBack->SetVisible(false);
    }
    _drawPlane = p;
}
void D3DLayer::piledCopy(iTJSDispatch2* orgLayer)
{
    
}

void D3DImage::load(iTJSDispatch2* reflay)
{
    // 获取父类实例
    // 此玩意只是个中转站
    if (reflay->NativeInstanceSupport(TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                                      (iTJSNativeInstance**)&_ths) < 0 ||
        _ths == NULL)
        TVPThrowExceptionMessage(TVPSpecifyLayer);
    _width = _ths->GetImageWidth();
    _height = _ths->GetImageHeight();
}
D3DPicture::D3DPicture(iTJSDispatch2* d3dlay, iTJSDispatch2* img)
{
    // 将数据从img复制到d3dlay
    // 最好采用copy数据的方式，因为此img-reflay在poolLayer中，随时会被清除覆盖
    _d3dlay = ncbInstanceAdaptor<D3DLayer>::GetNativeInstance(d3dlay);
    D3DImage* _img = ncbInstanceAdaptor<D3DImage>::GetNativeInstance(img);
    if (_d3dlay == NULL || _img == NULL)
        TVPThrowExceptionMessage(TVPSpecifyLayer);
    imgWidth = _img->getRefLayer()->GetImageWidth();
    imgHeight = _img->getRefLayer()->GetImageHeight();
    imgData = new tjs_uint8[imgWidth * imgHeight * 4];
    memcpy(imgData, _img->getRefLayer()->GetMainImagePixelBuffer(), imgWidth * imgHeight * 4);
    // 将onUpdate方法传入
    tTJSVariant cloFun;
    d3dlay->PropGet(0, TJS_W("onUpdate"), NULL, &cloFun, d3dlay);
    tTJSVariantClosure cls = cloFun.AsObjectClosure();
    _d3dlay->setUpdateClo(cls);
    _d3dlay->setD3DPicture(this);
}
void D3DPicture::assignImageRange(
    tjs_int left, tjs_int top, tjs_int width, tjs_int height, tjs_int tgtleft, tjs_int tgtright)
{
    if (_d3dlay == NULL || imgData == NULL)
        TVPThrowExceptionMessage(TVPSpecifyLayer);
    tTVPRect rect(left, top, width, height);
    _tgtleft = tgtleft;
    _tgtright = tgtright;
    _rect = rect;
}

#pragma endregion

#pragma region Regist

#define NCB_MODULE_NAME TJS_W("drawdeviceD3D.dll")
NCB_PRE_REGIST_CALLBACK(DrawDeviceD3D_init);
NCB_POST_UNREGIST_CALLBACK(DrawDeviceD3D_done);
NCB_REGISTER_CLASS(D3DLayer)
{
    NCB_CONSTRUCTOR((iTJSDispatch2*));
    Property(TJS_W("DrawPlaneBoth"), &Class::getDrawPlaneBoth, NULL);
    Property(TJS_W("DrawPlaneFront"), &Class::getDrawPlaneFront, NULL);
    Property(TJS_W("DrawPlaneBack"), &Class::getDrawPlaneBack, NULL);
    Property(TJS_W("drawPlane"), &Class::getDrawPlane, &Class::setDrawPlane);
    Property(TJS_W("frontIndex"), &Class::getFrontIndex, &Class::setFrontIndex);
    Property(TJS_W("backIndex"), &Class::getBackIndex, &Class::setBackIndex);
    Property(TJS_W("visible"), &Class::getVisible, &Class::setVisible);
    Method(TJS_W("setMatrix"), &Class::setMatrix);
    //Method(TJS_W("piledCopy"), &Class::piledCopy);
}
NCB_REGISTER_CLASS(D3DImage)
{
    NCB_CONSTRUCTOR((iTJSDispatch2*));
    Property(TJS_W("width"), &Class::getWidth, NULL);
    Property(TJS_W("height"), &Class::getHeight, NULL);
    Method(TJS_W("load"), &Class::load);
}
NCB_REGISTER_CLASS(D3DPicture)
{
    NCB_CONSTRUCTOR((iTJSDispatch2*, iTJSDispatch2*));
    Method(TJS_W("assignImageRange"), &Class::assignImageRange);
    Property(TJS_W("blendMode"), &Class::getBlendMode, &Class::setBlendMode);
    Property(TJS_W("opacity"), &Class::getOpacity, &Class::setOpacity);
    Method(TJS_W("setCoord"), &Class::setCoord);
}

#undef NCB_MODULE_NAME
void DrawDeviceD3DZ_init()
{
    DrawDeviceD3D_init();
}

void DrawDeviceD3DZ_done()
{
    DrawDeviceD3D_done();
}
#define NCB_MODULE_NAME TJS_W("drawdeviceD3DZ.dll")
NCB_PRE_REGIST_CALLBACK(DrawDeviceD3DZ_init);
NCB_POST_UNREGIST_CALLBACK(DrawDeviceD3DZ_done);
static ncbNativeClassAutoRegister<D3DLayer> ncbNativeClassAutoRegister_D3DLayerZ(NCB_MODULE_NAME,
                                                                                 TJS_W("D3DLayer"));
static ncbNativeClassAutoRegister<D3DImage> ncbNativeClassAutoRegister_D3DImageZ(NCB_MODULE_NAME,
                                                                                 TJS_W("D3DImage"));
static ncbNativeClassAutoRegister<D3DPicture> ncbNativeClassAutoRegister_D3DPictureZ(
    NCB_MODULE_NAME, TJS_W("D3DPicture"));
#pragma endregion

#endif

#define NCB_MODULE_NAME TJS_W("drawdeviceD3D.dll")

void DrawDeviceD3D_init()
{
    ncbAutoRegister::LoadModule(TJS_W("emoteplayer.dll"));
    TVPExecuteBinaryStream(GetResourceStream("D3DEmote.tjs"), ttstr("D3DEmote.tjs"));
}

void DrawDeviceD3D_done()
{

}

NCB_PRE_REGIST_CALLBACK(DrawDeviceD3D_init);
NCB_POST_UNREGIST_CALLBACK(DrawDeviceD3D_done);