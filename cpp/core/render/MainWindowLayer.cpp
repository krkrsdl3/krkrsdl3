#include "tjsCommHead.h"
#include "MainWindowLayer.h"
#include "TVPWindow.h"
#include "VelocityTracker.h"
#include "Platform.h"
#include "PlatformThread.h"
#include "PlatformView.h"
#include "Random.h"
#include "TVPApplication.h"
#include "RenderManager.h"
#include "WindowIntf.h"
#include "TVPSystem.h"

#include "eventCallbackFun.h"
#include "TVPCompositor.h"

class TVPWindowLayer;
static TVPWindowLayer *_firstWindowLayer = NULL, *_lastWindowLayer = NULL, *_currentWindowLayer;
static tTVPMouseButton _mouseBtn;

static tjs_uint8 _scancode[0x200];
static tjs_uint16 _keymap[0x200];

enum
{
    mrOk,
    mrAbort,
    mrCancel,
};

bool TVPGetKeyMouseAsyncState(tjs_uint keycode, bool getcurrent)
{
    if (keycode >= sizeof(_scancode) / sizeof(_scancode[0]))
        return false;
    tjs_uint8 code = _scancode[keycode];
    _scancode[keycode] &= 1;
    return code & (getcurrent ? 1 : 0x10);
}
bool TVPGetJoyPadAsyncState(tjs_uint keycode, bool getcurrent)
{
    if (keycode >= sizeof(_scancode) / sizeof(_scancode[0]))
        return false;
    tjs_uint8 code = _scancode[keycode];
    _scancode[keycode] &= 1;
    return code & (getcurrent ? 1 : 0x10);
}

static int TVPConvertMouseBtnToVKCode(tTVPMouseButton _mouseBtn)
{
    int btncode;
    switch (_mouseBtn)
    {
        case mbLeft:
            btncode = VK_LBUTTON;
            break;
        case mbMiddle:
            btncode = VK_MBUTTON;
            break;
        case mbRight:
            btncode = VK_RBUTTON;
            break;
        default:
            btncode = 0;
            break;
    }
    return btncode;
}
static tjs_uint32 TVPGetCurrentShiftKeyState()
{
    tjs_uint32 f = 0;

    if (_scancode[VK_SHIFT] & 1)
        f |= ssShift;
    if (_scancode[VK_MENU] & 1)
        f |= ssAlt;
    if (_scancode[VK_CONTROL] & 1)
        f |= ssCtrl;
    if (_scancode[VK_LBUTTON] & 1)
        f |= ssLeft;
    if (_scancode[VK_RBUTTON] & 1)
        f |= ssRight;
    return f;
}
static void AdjustNumerAndDenom(tjs_int& n, tjs_int& d)
{
    tjs_int a = n;
    tjs_int b = d;
    while (b)
    {
        tjs_int t = b;
        b = a % b;
        a = t;
    }
    n = n / a;
    d = d / a;
}

class TVPWindowLayer : public iWindowLayer
{
    tTJSNI_Window* TJSNativeInstance;
    tjs_int ActualZoomDenom; // Zooming factor denominator (actual)
    tjs_int ActualZoomNumer; // Zooming factor numerator (actual)
    int LayerWidth = 0, LayerHeight = 0;

    friend class TVPWindowManagerOverlay;

    int _LastMouseX = 0, _LastMouseY = 0;
    std::string _caption;
    float _drawSpriteScaleX = 1.0f, _drawSpriteScaleY = 1.0f;
    float _drawTextureScaleX = 1.f, _drawTextureScaleY = 1.f;
    bool UseMouseKey = false, MouseLeftButtonEmulatedPushed = false,
         MouseRightButtonEmulatedPushed = false;
    bool LastMouseMoved = false, Visible = false;
    tjs_uint64 LastMouseKeyTick = 0;
    tjs_int MouseKeyXAccel = 0;
    tjs_int MouseKeyYAccel = 0;
    int LastMouseDownX = 0, LastMouseDownY = 0;
    VelocityTrackers TouchVelocityTracker;
    VelocityTracker MouseVelocityTracker;
    static const int TVP_MOUSE_MAX_ACCEL = 30;
    static const int TVP_MOUSE_SHIFT_ACCEL = 40;
    static const int TVP_TOOLTIP_SHOW_DELAY = 500;

    bool isFullScreen = false;
    tTVPImeMode LastSetImeMode = ::imDisable;
    tTVPImeMode DefaultImeMode = ::imDisable;

public:
    TVPSprite* pSprite = NULL;
    TVPWindowLayer *_prevWindow, *_nextWindow;
    TVPWindowLayer(tTJSNI_Window* w) : TJSNativeInstance(w)
    {
        _nextWindow = nullptr;
        _prevWindow = _lastWindowLayer;
        _lastWindowLayer = this;
        ActualZoomDenom = 1;
        ActualZoomNumer = 1;
        if (_prevWindow)
        {
            _prevWindow->_nextWindow = this;
        }
    }

    virtual ~TVPWindowLayer()
    {
        if (_lastWindowLayer == this)
            _lastWindowLayer = _prevWindow;
        if (_nextWindow)
            _nextWindow->_prevWindow = _prevWindow;
        if (_prevWindow)
            _prevWindow->_nextWindow = _nextWindow;

        if (_currentWindowLayer == this)
        {
            TVPWindowLayer* anotherWin = _lastWindowLayer;
            while (anotherWin && !anotherWin->GetVisible())
            {
                anotherWin = anotherWin->_prevWindow;
            }
            if (anotherWin && anotherWin->GetVisible())
            {
                anotherWin->SetPosition(0, 0);
            }
            _currentWindowLayer = anotherWin;
        }

        if (pSprite != NULL)
        {
            if (pSprite->texture.gpuTexture != 0)
            {
                krkrsdl3::TVPDepartTexture(pSprite);
                krkrsdl3::TVPDestroyTexture(pSprite);
            }
            delete pSprite;
        }
    }

    bool Init()
    {
        {
            if (pSprite == NULL)
            {
                int w = TJSNativeInstance->GetWidth();
                int h = TJSNativeInstance->GetHeight();
                if (w <= 0 || h <= 0)
                {
                    TVPGetWindowSize(&w, &h);
                }
                pSprite = new TVPSprite;
                pSprite->width = w;
                pSprite->height = h;
                krkrsdl3::TVPCreateTexture(*pSprite);
                krkrsdl3::TVPJoinTexture(pSprite);
            }
        }

        return true;
    }

    static TVPWindowLayer* create(tTJSNI_Window* w)
    {
        TVPWindowLayer* ret = new TVPWindowLayer(w);
        ret->Init();
        return ret;
    }

    void onMouseDownEvent(tTVPMouseButton mouseId, int x, int y)
    {
        switch (mouseId)
        {
            case mbRight:
                _mouseBtn = mbRight;
                onMouseDown(x, y);
                break;
            case mbMiddle:
                _mouseBtn = mbMiddle;
                onMouseDown(x, y);
                break;
            case mbLeft:
                _mouseBtn = mbLeft;
                onMouseDown(x, y);
                break;
            default:
                break;
        }
    }

    void onMouseUpEvent(tTVPMouseButton mouseId, int x, int y)
    {
        switch (mouseId)
        {
            case mbRight:
                _mouseBtn = mbRight;
                onMouseUp(x, y);
                break;
            case mbMiddle:
                _mouseBtn = mbMiddle;
                onMouseUp(x, y);
                break;
            case mbLeft:
                _mouseBtn = mbLeft;
                OnMouseClick(x, y);
                onMouseUp(x, y);
                break;
            default:
                break;
        }
    }

    void onMouseMoveEvent(int x, int y)
    {
        if (_currentWindowLayer == this)
        {
            onMouseMove(x, y);
        }
    }

    void onMouseScroll(int dx, int dy, int x, int y)
    {
        TJSNativeInstance->OnMouseWheel(TVPGetCurrentShiftKeyState(), dy > 0 ? -120 : 120, x, y);
    }

    void onMouseDown(int x, int y)
    {
        _LastMouseX = x, _LastMouseY = y;
        _scancode[TVPConvertMouseBtnToVKCode(_mouseBtn)] = 0x11;
        TVPPostInputEvent(new tTVPOnMouseDownInputEvent(TJSNativeInstance, _LastMouseX, _LastMouseY,
                                                        _mouseBtn, TVPGetCurrentShiftKeyState()));
    }

    void onMouseUp(int x, int y)
    {
        _LastMouseX = x;
        _LastMouseY = y;
        _scancode[TVPConvertMouseBtnToVKCode(_mouseBtn)] &= 0x10;
        TVPPostInputEvent(new tTVPOnMouseUpInputEvent(TJSNativeInstance, _LastMouseX, _LastMouseY,
                                                      _mouseBtn, TVPGetCurrentShiftKeyState()));
    }

    void onMouseMove(int x, int y)
    {
        _LastMouseX = x, _LastMouseY = y;
        TVPPostInputEvent(new tTVPOnMouseMoveInputEvent(TJSNativeInstance, _LastMouseX, _LastMouseY,
                                                        TVPGetCurrentShiftKeyState()),
                          TVP_EPT_DISCARDABLE);
        int pos = (_LastMouseY << 16) + _LastMouseX;
        TVPPushEnvironNoise(&pos, sizeof(pos));
    }

    void OnMouseClick(int x, int y)
    {
        if (TJSNativeInstance)
        {
            TVPPostInputEvent(new tTVPOnClickInputEvent(TJSNativeInstance, x, y));
        }
    }

    virtual void SetPaintBoxSize(tjs_int w, tjs_int h) override
    {
        LayerWidth = w;
        LayerHeight = h;
        RecalcPaintBox();
    }

    virtual bool GetFormEnabled() override { return pSprite->isVisible; }

    virtual void SetDefaultMouseCursor() override {}

    virtual void GetCursorPos(tjs_int& x, tjs_int& y) override
    {
        x = _LastMouseX;
        y = _LastMouseY;
    }

    virtual void SetCursorPos(tjs_int x, tjs_int y) override
    {
        // Vec2 worldPt = PrimaryLayerArea->convertToWorldSpace(
        //     Vec2(x, PrimaryLayerArea->getContentSize().height - y));
        // Vec2 pt = getParent()->convertToNodeSpace(worldPt);
        //_LastMouseX = pt.x;
        //_LastMouseY = pt.y;
        // if (_mouseCursor) {
        //     _mouseCursor->setPosition(pt);
        //     _refadeMouseCursor();
        // }
    }

    virtual void SetHintText(const ttstr& text) override {}

    tjs_int _textInputPosY;

    virtual void SetAttentionPoint(tjs_int left, tjs_int top, const struct tTVPFont* font) override
    {
        _textInputPosY = top;
    }

    virtual void SetImeMode(tTVPImeMode mode) override
    {
        LastSetImeMode = mode;
        switch (mode)
        {
            case ::imDisable:
            case ::imClose:
            {
                TVPHideIME();
                break;
            }
            case ::imOpen:
            default:
            {
                TVPShowIME(0, 0, 0, 0);
                break;
            }
        }
    }

    virtual void ZoomRectangle(tjs_int& left,
                               tjs_int& top,
                               tjs_int& right,
                               tjs_int& bottom) override
    {
        left = tjs_int64(left) * ActualZoomNumer / ActualZoomDenom;
        top = tjs_int64(top) * ActualZoomNumer / ActualZoomDenom;
        right = tjs_int64(right) * ActualZoomNumer / ActualZoomDenom;
        bottom = tjs_int64(bottom) * ActualZoomNumer / ActualZoomDenom;
    }

    virtual void BringToFront() override
    {
        if (_currentWindowLayer != this)
        {
            if (_currentWindowLayer)
            {
                tjs_int w = 0, h = 0;
                _currentWindowLayer->GetSize(w, h);
                _currentWindowLayer->SetPosition(w, 0);
                _currentWindowLayer->TJSNativeInstance->OnReleaseCapture();
            }
            _currentWindowLayer = this;
        }
    }

    virtual void ShowWindowAsModal() override
    {
        pSprite->type = 1;
        in_mode_ = true;
        SetVisible(true);
        BringToFront();
        SetPosition(0, 0);

        modal_result_ = 0;
        while (this == _currentWindowLayer && !modal_result_)
        {
            int remain = TVPDrawSceneOnce(30); // 30 fps
            if (::Application->IsTarminate())
            {
                modal_result_ = mrCancel;
            }
            else if (modal_result_ != 0)
            {
                break;
            }
            else if (remain > 0)
            {
                TVPSleepFor(remain);
            }
        }
        in_mode_ = false;
    }

    virtual bool GetVisible() override { return pSprite->isVisible; }

    virtual void SetVisible(bool bVisible) override
    {
        pSprite->isVisible = bVisible;
        if (bVisible)
        {
            BringToFront();
        }
        else
        {
            if (_currentWindowLayer == this)
            {
                _currentWindowLayer = _prevWindow ? _prevWindow : _nextWindow;
            }
        }
    }

    virtual const char* GetCaption() override { return _caption.c_str(); }

    virtual void SetCaption(const std::string& s) override
    {
        TVPSetWindowTitle(s.c_str());
        _caption = s;
    }

    void ResetDrawSprite()
    {
        if (pSprite->width != LayerWidth || pSprite->height != LayerHeight)
        {
            krkrsdl3::TVPDestroyTexture(pSprite);
            pSprite->width = LayerWidth;
            pSprite->height = LayerHeight;
            krkrsdl3::TVPCreateTexture(*pSprite);
        }
        pSprite->width = LayerWidth;
        pSprite->height = LayerHeight;
        if (this == _firstWindowLayer)
        {
            TVPSetWindowSize(LayerWidth, LayerHeight);
        }
    }

    void RecalcPaintBox()
    {
        if (!LayerWidth || !LayerHeight)
            return;
        ResetDrawSprite();
    }

    virtual void SetWidth(tjs_int w) override
    {
        LayerWidth = w;
        RecalcPaintBox();
    }

    virtual void SetHeight(tjs_int h) override
    {
        LayerHeight = h;
        RecalcPaintBox();
    }

    virtual void SetSize(tjs_int w, tjs_int h) override { SetPaintBoxSize(w, h); }

    virtual void GetSize(tjs_int& w, tjs_int& h) override { GetWinSize(w, h); }

    virtual void GetWinSize(tjs_int& w, tjs_int& h) override
    {
        TVPGetWindowSize(&w, &h);
    }

    virtual tjs_int GetWidth() const override { return LayerWidth; }

    virtual tjs_int GetHeight() const override { return LayerHeight; }

    void SetPosition(tjs_int x, tjs_int y)
    {
        pSprite->xPos = x;
        pSprite->yPos = y;
    }

    virtual void SetFullScreenMode(bool isFull) override
    {
        if (this == _firstWindowLayer)
        {
            TVPSetWindowFullscreen(isFull);
            isFullScreen = isFull;
        }
    }

    virtual bool GetFullScreenMode() override { return isFullScreen; }

    virtual void SetZoom(tjs_int numer, tjs_int denom) override
    {
        AdjustNumerAndDenom(numer, denom);
        ZoomNumer = numer;
        ZoomDenom = denom;
        ActualZoomDenom = denom;
        ActualZoomNumer = numer;
        RecalcPaintBox();
    }

    virtual void UpdateDrawBuffer(iTVPTexture2D* tex) override
    {
        if (!tex)
            return;
        if (pSprite->texture.gpuTexture == 0)
            return;

        {
            if (pSprite->width != tex->GetWidth() || pSprite->height != tex->GetHeight())
            {
                SetSize(tex->GetWidth(), tex->GetHeight());
            }

            tjs_uint8* picData = nullptr;
            tjs_int pic_pitch;
            bool isNeedFree = tex->GetTextureData(&picData, pic_pitch);
            krkrsdl3::TVPUpdateTexture(pSprite, picData, pSprite->width, pSprite->height,
                                           pic_pitch);
            if (isNeedFree)
                free(picData);
        }
    }

    tTJSNI_Window* GetWindow() { return TJSNativeInstance; }

    virtual void InvalidateClose() override
    {
        // closing action by object invalidation;
        // this will not cause any user confirmation of closing the
        // window.

        delete this;
    }

    virtual bool GetWindowActive() override { return _currentWindowLayer == this; }

    virtual void InternalKeyDown(tjs_uint16 key, tjs_uint32 shift) override
    {
        tjs_uint64 tick = TVPGetRoughTickCount();
        TVPPushEnvironNoise(&tick, sizeof(tick));
        TVPPushEnvironNoise(&key, sizeof(key));
        TVPPushEnvironNoise(&shift, sizeof(shift));
        TVPPostInputEvent(new tTVPOnKeyDownInputEvent(TJSNativeInstance, key, shift));
    }

    int GetMouseButtonState() const
    {
        int s = 0;
        if (TVPGetAsyncKeyState(VK_LBUTTON))
            s |= ssLeft;
        if (TVPGetAsyncKeyState(VK_RBUTTON))
            s |= ssRight;
        if (TVPGetAsyncKeyState(VK_MBUTTON))
            s |= ssMiddle;
        return s;
    }

    void onKeyUpEvent(int vk)
    {
        unsigned int code = TVPConvertKeyCodeToVKCode(vk);
        if (!code || code >= 0x200)
            return;

        bool isPressed = _scancode[code] & 1;
        _scancode[code] &= 0x10;

        if (isPressed && TJSNativeInstance && code)
        {
            TVPPostInputEvent(
                new tTVPOnKeyUpInputEvent(TJSNativeInstance, code, TVPGetCurrentShiftKeyState()));
        }
    }

    void onKeyDownEvent(int vk)
    {
        unsigned int code = TVPConvertKeyCodeToVKCode(vk);
        if (!code || code >= 0x200)
            return;

        bool isPressed = _scancode[code] & 1;
        _scancode[code] = 0x11;

        if (TJSNativeInstance && code)
        {
            TVPPostInputEvent(
                new tTVPOnKeyDownInputEvent(TJSNativeInstance, code, TVPGetCurrentShiftKeyState()));
        }
    }

    virtual void OnKeyUp(tjs_uint16 vk, int shift) override {}

    virtual void OnKeyPress(tjs_uint16 vk, int repeat, bool prevkeystate, bool convertkey) override
    {
        if (TJSNativeInstance && vk)
        {
            if (UseMouseKey && (vk == 0x1b || vk == 13 || vk == 32))
                return;
            TVPPostInputEvent(new tTVPOnKeyPressInputEvent(TJSNativeInstance, vk));
        }
    }

    virtual tTVPImeMode GetDefaultImeMode() const override { return DefaultImeMode; }

    virtual void ResetImeMode() override { SetImeMode(DefaultImeMode); }

    bool Closing = false, ProgramClosing = false, CanCloseWork = false;
    bool in_mode_ = false; // is modal
    int modal_result_ = 0;
    enum CloseAction
    {
        caNone,
        caHide,
        caFree,
        caMinimize
    };

    void OnClose(CloseAction& action)
    {
        if (modal_result_ == 0)
            action = caNone;
        else
            action = caHide;

        if (ProgramClosing)
        {
            if (TJSNativeInstance)
            {
                if (TJSNativeInstance->IsMainWindow())
                {
                    // this is the main window
                }
                else
                {
                    // not the main window
                    action = caFree;
                }
                // if (TVPFullScreenedWindow != this) {
                //  if this is not a fullscreened window
                //	SetVisible(false);
                // }
                iTJSDispatch2* obj = TJSNativeInstance->GetOwnerNoAddRef();
                TJSNativeInstance->NotifyWindowClose();
                obj->Invalidate(0, nullptr, nullptr, obj);
                TJSNativeInstance = nullptr;
                SetVisible(false);
            }
        }
    }

    bool OnCloseQuery()
    {
        // closing actions are 3 patterns;
        // 1. closing action by the user
        // 2. "close" method
        // 3. object invalidation

        if (TVPGetBreathing())
        {
            return false;
        }

        // the default event handler will invalidate this object when
        // an onCloseQuery event reaches the handler.
        if (TJSNativeInstance &&
            (modal_result_ == 0 ||
             modal_result_ == mrCancel /* mrCancel=when close button is pushed in modal window */))
        {
            iTJSDispatch2* obj = TJSNativeInstance->GetOwnerNoAddRef();
            if (obj)
            {
                tTJSVariant arg[1] = {true};
                static ttstr eventname(TJS_N("onCloseQuery"));

                if (!ProgramClosing)
                {
                    // close action does not happen immediately
                    if (TJSNativeInstance)
                    {
                        TVPPostInputEvent(new tTVPOnCloseInputEvent(TJSNativeInstance));
                    }

                    Closing = true; // waiting closing...
                    //	TVPSystemControl->NotifyCloseClicked();
                    return false;
                }
                else
                {
                    CanCloseWork = true;
                    TVPPostEvent(obj, obj, eventname, 0, TVP_EPT_IMMEDIATE, 1, arg);
                    // this event happens immediately
                    // and does not return until done
                    return CanCloseWork; // CanCloseWork is set by the
                    // event handler
                }
            }
            else
            {
                return true;
            }
        }
        else
        {
            return true;
        }
    }

    virtual void Close() override
    {
        // closing action by "close" method
        if (Closing)
            return; // already waiting closing...

        ProgramClosing = true;
        try
        {
            // tTVPWindow::Close();
            if (in_mode_)
            {
                modal_result_ = mrCancel;
            }
            else if (OnCloseQuery())
            {
                CloseAction action = caFree;
                OnClose(action);
                switch (action)
                {
                    case caNone:
                        break;
                    case caHide:
                        break;
                    case caMinimize:
                        break;
                    case caFree:
                    default:
                        break;
                }
            }
        }
        catch (...)
        {
            ProgramClosing = false;
            throw;
        }
        ProgramClosing = false;
    }

    virtual void OnCloseQueryCalled(bool b) override
    {
        // closing is allowed by onCloseQuery event handler
        if (!ProgramClosing)
        {
            // closing action by the user
            if (b)
            {
                if (in_mode_)
                    modal_result_ = 1; // when modal
                else
                    SetVisible(false); // just hide

                Closing = false;
                if (TJSNativeInstance)
                {
                    if (TJSNativeInstance->IsMainWindow())
                    {
                        // this is the main window
                        iTJSDispatch2* obj = TJSNativeInstance->GetOwnerNoAddRef();
                        obj->Invalidate(0, nullptr, nullptr, obj);
                    }
                }
                else
                {
                    delete this;
                }
            }
            else
            {
                Closing = false;
            }
        }
        else
        {
            // closing action by the program
            CanCloseWork = b;
        }
    }

    virtual void UpdateWindow(tTVPUpdateType type) override
    {
        if (TJSNativeInstance)
        {
            tTVPRect r;
            r.left = 0;
            r.top = 0;
            r.right = LayerWidth;
            r.bottom = LayerHeight;
            TJSNativeInstance->NotifyWindowExposureToLayer(r);
            TVPDeliverWindowUpdateEvents();
        }
    }

    virtual void SetVisibleFromScript(bool b) override { SetVisible(b); }

    virtual void SetUseMouseKey(bool b) override
    {
        UseMouseKey = b;
        if (b)
        {
            MouseLeftButtonEmulatedPushed = false;
            MouseRightButtonEmulatedPushed = false;
            LastMouseKeyTick = TVPGetRoughTickCount();
        }
        else
        {
            if (MouseLeftButtonEmulatedPushed)
            {
                MouseLeftButtonEmulatedPushed = false;
                OnMouseUp(mbLeft, 0, _LastMouseX, _LastMouseY);
            }
            if (MouseRightButtonEmulatedPushed)
            {
                MouseRightButtonEmulatedPushed = false;
                OnMouseUp(mbRight, 0, _LastMouseX, _LastMouseY);
            }
        }
    }

    virtual bool GetUseMouseKey() const override { return UseMouseKey; }

    void OnMouseUp(int button, int shift, int x, int y)
    {
        //	TranslateWindowToDrawArea(x, y);
        //	ReleaseMouseCapture();
        MouseVelocityTracker.addMovement(TVPGetRoughTickCount(), (float)x, (float)y);
    }

    virtual void ResetTouchVelocity(tjs_int id) override { TouchVelocityTracker.end(id); }

    virtual void ResetMouseVelocity() override { MouseVelocityTracker.clear(); }

    bool GetMouseVelocity(float& x, float& y, float& speed) const override
    {
        if (MouseVelocityTracker.getVelocity(x, y))
        {
            speed = hypotf(x, y);
            return true;
        }
        return false;
    }

    virtual void TickBeat() override
    {
        bool focused = _currentWindowLayer == this;
        // mouse key
        if (UseMouseKey && focused)
        {
            // GenerateMouseEvent(false, false, false, false);
        }
    }
};

// events
namespace krkrsdl3
{
TVPSprite* KRKR_Get_Current_Sprite()
{
    if (_currentWindowLayer != NULL)
        return _currentWindowLayer->pSprite;
    return NULL;
}
void KRKR_Trig_MouseDown(tTVPMouseButton mouseId, int x, int y)
{
    if (_currentWindowLayer != NULL)
    {
        TVPSprite* tmp = _currentWindowLayer->pSprite;
        if (tmp != NULL)
            _currentWindowLayer->onMouseDownEvent(mouseId, (x - tmp->xPos) / tmp->scale,
                                                  (y - tmp->yPos) / tmp->scale);
    }
}
void KRKR_Trig_MouseUp(tTVPMouseButton mouseId, int x, int y)
{
    if (_currentWindowLayer != NULL)
    {
        TVPSprite* tmp = _currentWindowLayer->pSprite;
        if (tmp != NULL)
            _currentWindowLayer->onMouseUpEvent(mouseId, (x - tmp->xPos) / tmp->scale,
                                                (y - tmp->yPos) / tmp->scale);
    }
}
void KRKR_Trig_MouseMove(int x, int y)
{
    if (_currentWindowLayer != NULL)
    {
        TVPSprite* tmp = _currentWindowLayer->pSprite;
        if (tmp != NULL)
            _currentWindowLayer->onMouseMoveEvent((x - tmp->xPos) / tmp->scale,
                                                  (y - tmp->yPos) / tmp->scale);
    }
}
void KRKR_Trig_MouseScroll(int dx, int dy, int x, int y)
{
    if (_currentWindowLayer != NULL)
        _currentWindowLayer->onMouseScroll(dx, dy, x, y);
}
void KRKR_Trig_KeyDown(int vk)
{
    if (_currentWindowLayer != NULL)
    {
        TVPSprite* tmp = _currentWindowLayer->pSprite;
        if (tmp != NULL)
            _currentWindowLayer->onKeyDownEvent(vk);
    }
}
void KRKR_Trig_KeyUp(int vk)
{
    if (_currentWindowLayer != NULL)
    {
        TVPSprite* tmp = _currentWindowLayer->pSprite;
        if (tmp != NULL)
            _currentWindowLayer->onKeyUpEvent(vk);
    }
}
void KRKR_Trig_TextInput(std::string text)
{
    if (_currentWindowLayer != NULL)
    {
        tjs_wchar chwd = 0;
        const char* ptr = text.data();
        const char* ptrEnd = text.data() + text.size();
        while(ptr < ptrEnd)
        {
            int len = utf8_char_len(ptr);
            if (TVP_utf8_to_utf16(ptr, &chwd))
                _currentWindowLayer->OnKeyPress(chwd, 0, false, false);
            if (len <= 0)
                len = 1;
            ptr += len;
        }
    }
}
}

iWindowLayer* TVPCreateAndAddWindow(tTJSNI_Window* w)
{
    TVPWindowLayer* ret = TVPWindowLayer::create(w);
    if (_firstWindowLayer == NULL)
    {
        _firstWindowLayer = ret;
    }
    return ret;
}

tTJSNI_Window *TVPGetActiveWindow()
{
	if (!_currentWindowLayer) return nullptr;
	return _currentWindowLayer->GetWindow();
}