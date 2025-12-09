
#ifndef __TVP_WINDOW_H__
#define __TVP_WINDOW_H__

#include "RenderManager.h"
#include "tvpinputdefs.h"

enum {
	 ssShift = TVP_SS_SHIFT,
	 ssAlt = TVP_SS_ALT,
	 ssCtrl = TVP_SS_CTRL,
	 ssLeft = TVP_SS_LEFT,
	 ssRight = TVP_SS_RIGHT,
	 ssMiddle = TVP_SS_MIDDLE,
	 ssDouble = TVP_SS_DOUBLE,
	 ssRepeat = TVP_SS_REPEAT,
};

enum tTVPWMRRegMode { wrmRegister = 0, wrmUnregister = 1 };
enum {
	orientUnknown,
	orientPortrait,
	orientLandscape,
};

/*[*/
//---------------------------------------------------------------------------
// Window related constants
//---------------------------------------------------------------------------
enum tTVPUpdateType
{
    utNormal, // only needed region
    utEntire  // entire of window
};
enum tTVPBorderStyle
{
    bsNone = 0,
    bsSingle = 1,
    bsSizeable = 2,
    bsDialog = 3,
    bsToolWindow = 4,
    bsSizeToolWin = 5
};
enum tTVPMouseCursorState
{
    mcsVisible,    // the mouse cursor is visible
    mcsTempHidden, // the mouse cursor is temporarily hidden
    mcsHidden      // the mouse cursor is invisible
};
/*]*/
//---------------------------------------------------------------------------

class iWindowLayer
{
protected:
	tTVPMouseCursorState MouseCursorState = mcsVisible;
	tjs_int HintDelay = 500;
	tjs_int ZoomDenom = 1; // Zooming factor denominator (setting)
	tjs_int ZoomNumer = 1; // Zooming factor numerator (setting)
	double TouchScaleThreshold = 5, TouchRotateThreshold = 5;

public:
	virtual void SetPaintBoxSize(tjs_int w, tjs_int h) = 0;
	virtual bool GetFormEnabled() = 0;
	virtual void SetDefaultMouseCursor() = 0;
	virtual void GetCursorPos(tjs_int &x, tjs_int &y) = 0;
	virtual void SetCursorPos(tjs_int x, tjs_int y) = 0;
	virtual void SetHintText(const ttstr &text) = 0;
	virtual void SetAttentionPoint(tjs_int left, tjs_int top, const struct tTVPFont * font) = 0;
	virtual void ZoomRectangle(
		tjs_int & left, tjs_int & top,
		tjs_int & right, tjs_int & bottom) = 0;
	virtual void BringToFront() = 0;
	virtual void ShowWindowAsModal() = 0;
	virtual bool GetVisible() = 0;
	virtual void SetVisible(bool bVisible) = 0;
	virtual const char *GetCaption() = 0;
	virtual void SetCaption(const std::string &) = 0;
	virtual void SetWidth(tjs_int w) = 0;
	virtual void SetHeight(tjs_int h) = 0;
	virtual void SetSize(tjs_int w, tjs_int h) = 0;
	virtual void GetSize(tjs_int &w, tjs_int &h) = 0;
	virtual tjs_int GetWidth() const = 0;
	virtual tjs_int GetHeight() const = 0;
	virtual void GetWinSize(tjs_int &w, tjs_int &h) = 0;
	virtual void SetZoom(tjs_int numer, tjs_int denom) = 0;
	virtual void UpdateDrawBuffer(iTVPTexture2D *tex) = 0;
	virtual void InvalidateClose() = 0;
	virtual bool GetWindowActive() = 0;
	virtual void Close() = 0;
	virtual void OnCloseQueryCalled(bool b) = 0;
	virtual void InternalKeyDown(tjs_uint16 key, tjs_uint32 shift) = 0;
	virtual void OnKeyUp(tjs_uint16 vk, int shift) = 0;
	virtual void OnKeyPress(tjs_uint16 vk, int repeat, bool prevkeystate, bool convertkey) = 0;
	virtual tTVPImeMode GetDefaultImeMode() const = 0;
	virtual void SetImeMode(tTVPImeMode mode) = 0;
	virtual void ResetImeMode() = 0;
	virtual void UpdateWindow(tTVPUpdateType type) = 0;
	virtual void SetVisibleFromScript(bool b) = 0;
	virtual void SetUseMouseKey(bool b) = 0;
	virtual bool GetUseMouseKey() const = 0;
	virtual void ResetMouseVelocity() = 0;
	virtual void ResetTouchVelocity(tjs_int id) = 0;
	virtual bool GetMouseVelocity(float& x, float& y, float& speed) const = 0;
	virtual void TickBeat() = 0;
    virtual bool GetFullScreenMode() = 0;
    virtual void SetFullScreenMode(bool) = 0;

	void SetZoomNumer(tjs_int n) { SetZoom(n, ZoomDenom); }
	tjs_int GetZoomNumer() const { return ZoomNumer; }
	void SetZoomDenom(tjs_int d) { SetZoom(ZoomNumer, d); }
	tjs_int GetZoomDenom() const { return ZoomDenom; }

	// dummy function
	void RegisterWindowMessageReceiver(tTVPWMRRegMode mode, void * proc, const void *userdata) {}
	void SetLeft(tjs_int) {}
	void SetTop(tjs_int) {}
	void SetMinWidth(tjs_int) {}
	void SetMaxWidth(tjs_int) {}
	void SetMinHeight(tjs_int) {}
	void SetMaxHeight(tjs_int) {}
	void SetInnerWidth(tjs_int v) { SetWidth(v); }
	void SetInnerHeight(tjs_int v) { SetHeight(v); }
	void SetInnerSize(tjs_int w, tjs_int h) { SetSize(w, h); }
	void SetMinSize(tjs_int, tjs_int) {}
	void SetMaxSize(tjs_int, tjs_int) {}
	void SetPosition(tjs_int, tjs_int) {}
	void SetBorderStyle(tTVPBorderStyle) {}
	void SetStayOnTop(bool) {}
	tjs_int GetLeft() { return 0; }
	tjs_int GetTop() { return 0; }
	tjs_int GetMinWidth() { return 0; }
	tjs_int GetMaxWidth() { return 1920; }
	tjs_int GetMinHeight() { return 0; }
	tjs_int GetMaxHeight() { return 1080; }
	tjs_int GetInnerWidth() { return GetWidth(); }
	tjs_int GetInnerHeight() { return GetHeight(); }
	bool GetStayOnTop() { return false; }
	tTVPBorderStyle GetBorderStyle() const { return bsNone; }
	void SetTrapKey(bool b) {}
	bool GetTrapKey() const { return false; }
	void RemoveMaskRegion() {}
	void SetMouseCursorState(tTVPMouseCursorState mcs) { MouseCursorState = mcs; }
	tTVPMouseCursorState GetMouseCursorState() const { return MouseCursorState; }
	void HideMouseCursor() { }
	void SetFocusable(bool b) {}
	bool GetFocusable() const { return true; }
	int GetDisplayRotate() { return 0; }
	int GetDisplayOrientation() { return orientLandscape; }
	void SetEnableTouch(bool b) {}
	bool GetEnableTouch() const { return false; }
	void SetHintDelay(tjs_int delay) { HintDelay = delay; }
	tjs_int GetHintDelay() const { return HintDelay; }
	void SetInnerSunken(bool b) {}
	bool GetInnerSunken() const { return false; }

	// TODO
	void SetMouseCursor(tjs_int handle) {}
	void SetHintText(iTJSDispatch2* sender, const ttstr &text) {}
	void DisableAttentionPoint() {}
	void GetVideoOffset(tjs_int &ofsx, tjs_int &ofsy) { ofsx = 0; ofsy = 0; }
	void SetTouchScaleThreshold(double threshold) { TouchScaleThreshold = threshold; }
	double GetTouchScaleThreshold() { return TouchScaleThreshold; }
	void SetTouchRotateThreshold(double threshold) { TouchRotateThreshold = threshold; }
	double GetTouchRotateThreshold() { return TouchRotateThreshold; }
	tjs_real GetTouchPointStartX(tjs_int index) const { return 0; }
	tjs_real GetTouchPointStartY(tjs_int index) const { return 0; }
	tjs_real GetTouchPointX(tjs_int index) const { return 0; }
	tjs_real GetTouchPointY(tjs_int index) const { return 0; }
	tjs_int GetTouchPointID(tjs_int index) const { return 0; }
	tjs_int GetTouchPointCount() const { return 0; }
	bool GetTouchVelocity(tjs_int id, float& x, float& y, float& speed) const { return false; }
	void ResetDrawDevice() {}
	void SendCloseMessage() {}
	void BeginMove() {}
	void SetLayerLeft(tjs_int l) {}
	tjs_int GetLayerLeft() const { return 0; }
	void SetLayerTop(tjs_int t) {}
	tjs_int GetLayerTop() const { return 0; }
	void SetLayerPosition(tjs_int l, tjs_int t) {}
	void SetShowScrollBars(bool b) {}
	bool GetShowScrollBars() const { return true; }
};

#endif // __TVP_WINDOW_H__
