//--------------------------------------------------------------------------- 
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//--------------------------------------------------------------------------- 
//!@file 描画デバイス管理 
//--------------------------------------------------------------------------- 

#include "tjsCommHead.h"

#include "DrawDevice.h"
#include "TVPMsg.h"
#include "LayerManager.h"
#include "WindowIntf.h"
#include "TVPDebug.h"
#include "TVPSystem.h"

//---------------------------------------------------------------------------
// オプション
//---------------------------------------------------------------------------
static tjs_int TVPBasicDrawDeviceOptionsGeneration = 0;
bool TVPZoomInterpolation = true;
//---------------------------------------------------------------------------
static void TVPInitBasicDrawDeviceOptions()
{
    if (TVPBasicDrawDeviceOptionsGeneration == TVPGetCommandLineArgumentGeneration())
        return;
    TVPBasicDrawDeviceOptionsGeneration = TVPGetCommandLineArgumentGeneration();

    tTJSVariant val;
    TVPZoomInterpolation = true;
    if (TVPGetCommandLine(TJS_W("-smoothzoom"), &val))
    {
        ttstr str(val);
        if (str == TJS_W("no"))
            TVPZoomInterpolation = false;
        else
            TVPZoomInterpolation = true;
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
tTVPBasicDrawDevice::tTVPBasicDrawDevice()
{
    TVPInitBasicDrawDeviceOptions(); // read and initialize options

    // コンストラクタ
    Window = NULL;
    Manager = NULL;
    DestRect.clear();
}
//---------------------------------------------------------------------------
tTVPBasicDrawDevice::~tTVPBasicDrawDevice()
{
    // すべての managers を開放する
    // TODO: プライマリレイヤ無効化、あるいはウィンドウ破棄時の終了処理が正しいか？
    // managers は 開放される際、自身の登録解除を行うために
    // RemoveLayerManager() を呼ぶかもしれないので注意。
    // そのため、ここではいったん配列をコピーしてからそれぞれの
    // Release() を呼ぶ。
    if (Manager != NULL)
    {
        Manager->Release();
        Manager = NULL;
    }
}


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::Destruct()
{
	delete this;
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::SetWindowInterface(iTVPWindow* window)
{
	Window = window;
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::AddLayerManager(iTVPLayerManager* manager)
{
    if (Manager != NULL)
    {
        // "Basic" デバイスでは２つ以上のLayer Managerを登録できない
        TVPThrowExceptionMessage(TVPBasicDrawDeviceDoesNotSupporteLayerManagerMoreThanOne);
    }
    // Managers に manager を push する。AddRefするのを忘れないこと。
    Manager = manager;
    manager->AddRef();

    manager->SetDesiredLayerType(ltOpaque); // ltOpaque な出力を受け取りたい
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::RemoveLayerManager(iTVPLayerManager* manager)
{
    // Managers から manager を削除する。Releaseする。
    if (Manager == NULL)
        TVPThrowInternalError;
    Manager->Release();
    Manager = NULL;
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::SetDestRectangle(const tTVPRect& rect)
{
	DestRect = rect;
}
//--------------------------------------------------------------------------- 

//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::SetClipRectangle(const tTVPRect& rect)
{
	
}
//--------------------------------------------------------------------------- 

//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::GetSrcSize(tjs_int& w, tjs_int& h)
{
    if (!Manager) return;
    if (!Manager->GetPrimaryLayerSize(w, h))
    {
        w = 0;
        h = 0;
    }
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::NotifyLayerResize(iTVPLayerManager* manager)
{
    if (Manager == manager)
		Window->NotifySrcResize();
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::NotifyLayerImageChange(iTVPLayerManager* manager)
{
    if (Manager == manager)
		Window->RequestUpdate();
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::OnClick(tjs_int x, tjs_int y)
{
    if (!Manager)
        return;

	Manager->NotifyClick(x, y);
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::OnDoubleClick(tjs_int x, tjs_int y)
{
    if (!Manager)
        return;

	Manager->NotifyDoubleClick(x, y);
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::OnMouseDown(tjs_int x,
                                                      tjs_int y,
                                                      tTVPMouseButton mb,
                                                      tjs_uint32 flags)
{
    if (!Manager)
        return;

	Manager->NotifyMouseDown(x, y, mb, flags);
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::OnMouseUp(tjs_int x,
                                                    tjs_int y,
                                                    tTVPMouseButton mb,
                                                    tjs_uint32 flags)
{
    if (!Manager)
        return;

	Manager->NotifyMouseUp(x, y, mb, flags);
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::OnMouseMove(tjs_int x, tjs_int y, tjs_uint32 flags)
{
    if (!Manager)
        return;

	Manager->NotifyMouseMove(x, y, flags);
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::OnReleaseCapture()
{
    if (!Manager)
        return;

	Manager->ReleaseCapture();
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::OnMouseOutOfWindow()
{
    if (!Manager)
        return;

	Manager->NotifyMouseOutOfWindow();
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::OnKeyDown(tjs_uint key, tjs_uint32 shift)
{
    if (!Manager)
        return;

	Manager->NotifyKeyDown(key, shift);
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::OnKeyUp(tjs_uint key, tjs_uint32 shift)
{
    if (!Manager)
        return;

	Manager->NotifyKeyUp(key, shift);
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::OnKeyPress(tjs_char key)
{
    if (!Manager)
        return;

	Manager->NotifyKeyPress(key);
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::OnMouseWheel(tjs_uint32 shift,
                                                       tjs_int delta,
                                                       tjs_int x,
                                                       tjs_int y)
{
    if (!Manager)
        return;

	Manager->NotifyMouseWheel(shift, delta, x, y);
}
//--------------------------------------------------------------------------- 

//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD
tTVPBasicDrawDevice::OnTouchDown(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id)
{
    if (!Manager)
        return;

	Manager->NotifyTouchDown(x, y, cx, cy, id);
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD
tTVPBasicDrawDevice::OnTouchUp(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id)
{
    if (!Manager)
        return;

	Manager->NotifyTouchUp(x, y, cx, cy, id);
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD
tTVPBasicDrawDevice::OnTouchMove(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id)
{
    if (!Manager)
        return;

	Manager->NotifyTouchMove(x, y, cx, cy, id);
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::OnTouchScaling(
    tjs_real startdist, tjs_real curdist, tjs_real cx, tjs_real cy, tjs_int flag)
{
    if (!Manager)
        return;

	Manager->NotifyTouchScaling(startdist, curdist, cx, cy, flag);
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::OnTouchRotate(
    tjs_real startangle, tjs_real curangle, tjs_real dist, tjs_real cx, tjs_real cy, tjs_int flag)
{
    if (!Manager)
        return;

	Manager->NotifyTouchRotate(startangle, curangle, dist, cx, cy, flag);
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::OnMultiTouch()
{
    if (!Manager)
        return;

	Manager->NotifyMultiTouch();
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::OnDisplayRotate(
    tjs_int orientation, tjs_int rotate, tjs_int bpp, tjs_int width, tjs_int height)
{
	// 何もしない 
}
//--------------------------------------------------------------------------- 

//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::RecheckInputState()
{
    if (!Manager)
        return;

	Manager->RecheckInputState();
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::SetDefaultMouseCursor(iTVPLayerManager* manager)
{
    if (!Manager)
        return;
    if (Manager == manager)
	{
		Window->SetDefaultMouseCursor();
	}
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::SetMouseCursor(iTVPLayerManager* manager, tjs_int cursor)
{
    if (!Manager)
        return;
    if (Manager == manager)
	{
		Window->SetMouseCursor(cursor);
	}
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::GetCursorPos(iTVPLayerManager* manager,
                                                       tjs_int& x,
                                                       tjs_int& y)
{
    if (!Manager)
        return;
	Window->GetCursorPos(x, y);
    if (Manager != manager)
	{
		// プライマリレイヤマネージャ以外には座標 0,0 で渡しておく 
		 x = 0;
		 y = 0;
	}
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::SetCursorPos(iTVPLayerManager* manager,
                                                       tjs_int x,
                                                       tjs_int y)
{
    if (!Manager)
        return;
    if (Manager == manager)
	{
		Window->SetCursorPos(x, y);
	}
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::WindowReleaseCapture(iTVPLayerManager* manager)
{
    if (!Manager)
        return;
    if (Manager == manager)
	{
		Window->WindowReleaseCapture();
	}
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::SetHintText(iTVPLayerManager* manager,
                                                      iTJSDispatch2* sender,
                                                      const ttstr& text)
{
    if (!Manager)
        return;
    if (Manager == manager)
	{
		Window->SetHintText(sender,text);
	}
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::SetAttentionPoint(iTVPLayerManager* manager,
                                                            tTJSNI_BaseLayer* layer,
				tjs_int l, tjs_int t)
{
    if (!Manager)
        return;
    if (Manager == manager)
	{
		Window->SetAttentionPoint(layer, l, t);
	}
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::DisableAttentionPoint(iTVPLayerManager* manager)
{
    if (!Manager)
        return;
    if (Manager == manager)
	{
		Window->DisableAttentionPoint();
	}
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::SetImeMode(iTVPLayerManager* manager, tTVPImeMode mode)
{
    if (!Manager)
        return;
    if (Manager == manager)
	{
		Window->SetImeMode(mode);
	}
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::ResetImeMode(iTVPLayerManager* manager)
{
    if (!Manager)
        return;
    if (Manager == manager)
	{
		Window->ResetImeMode();
	}
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
tTJSNI_BaseLayer* TJS_INTF_METHOD tTVPBasicDrawDevice::GetPrimaryLayer()
{
    if (!Manager)
        return NULL;
    return Manager->GetPrimaryLayer();
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
tTJSNI_BaseLayer* TJS_INTF_METHOD tTVPBasicDrawDevice::GetFocusedLayer()
{
    if (!Manager)
        return NULL;
    return Manager->GetFocusedLayer();
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::SetFocusedLayer(tTJSNI_BaseLayer* layer)
{
    if (!Manager)
        return;
    Manager->SetFocusedLayer(layer);
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::RequestInvalidation(const tTVPRect& rect)
{
    tjs_int l = rect.left, t = rect.top, r = rect.right, b = rect.bottom;
    r++; // 誤差の吸収(本当はもうちょっと厳密にやらないとならないがそれが問題になることはない)
    b++;

    if (!Manager)
        return;
    Manager->RequestInvalidation(tTVPRect(l, t, r, b));
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::Update()
{
    // すべての layer manager の UpdateToDrawDevice を呼ぶ
    if (Manager)
    {
        Manager->UpdateToDrawDevice();
    }
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::Show()
{
    if (Window)
    {
        iWindowLayer* form = Window->GetForm();
        if (form && Manager)
        {
            iTVPBaseBitmap* buf = Manager->GetDrawBuffer();
            if (buf)
                form->UpdateDrawBuffer(buf->GetTexture());
        }
    }
}
//--------------------------------------------------------------------------- 
bool TJS_INTF_METHOD tTVPBasicDrawDevice::WaitForVBlank(tjs_int* in_vblank, tjs_int* delayed)
{
	return false;
}

//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::DumpLayerStructure()
{
    // すべての layer manager の DumpLayerStructure を呼ぶ
    if (Manager)
    {
        Manager->DumpLayerStructure();
    }
}
//--------------------------------------------------------------------------- 


//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::SetShowUpdateRect(bool b)
{
    
}

void TJS_INTF_METHOD tTVPBasicDrawDevice::SetWindowSize(tjs_int w, tjs_int h)
{
    
}

//--------------------------------------------------------------------------- 
bool TJS_INTF_METHOD tTVPBasicDrawDevice::SwitchToFullScreen(
    int window, tjs_uint w, tjs_uint h, tjs_uint bpp, tjs_uint color, bool changeresolution)
{
	return true;
}
//--------------------------------------------------------------------------- 
void TJS_INTF_METHOD tTVPBasicDrawDevice::RevertFromFullScreen(
    int window, tjs_uint w, tjs_uint h, tjs_uint bpp, tjs_uint color)
{

}
//--------------------------------------------------------------------------- 

//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPBasicDrawDevice::StartBitmapCompletion(iTVPLayerManager* manager)
{
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPBasicDrawDevice::NotifyBitmapCompleted(iTVPLayerManager* manager,
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
void TJS_INTF_METHOD tTVPBasicDrawDevice::EndBitmapCompletion(iTVPLayerManager* manager)
{
}