//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// "Window" TJS Class implementation
//---------------------------------------------------------------------------

#include "tjsCommHead.h"

#include <algorithm>
#include "TVPMsg.h"
#include "WindowIntf.h"
#include "TVPDebug.h"
#include "TVPEvent.h"
#include "LayerBitmap.h"
#include "TVPSystem.h"
#include "LayerManager.h"
#include "TVPApplication.h"

#include "MainWindowLayer.h"

#include "tjsNativeBasicDrawDevice.h"
#include "tjsNativeVideoOverlay.h"
#include "tjsNativeLayer.h"
#include "tjsNativeWindow.h"

//---------------------------------------------------------------------------
// Window List
//---------------------------------------------------------------------------
tTJSNI_Window* TVPMainWindow = NULL; // main window
static std::vector<tTJSNI_Window*> TVPWindowVector;
//---------------------------------------------------------------------------
static void TVPRegisterWindowToList(tTJSNI_Window* window)
{
    if (TVPMainWindow == NULL && TVPWindowVector.size() == 0)
    {
        // first time the window is registered
        TVPMainWindow = window; // set as main window
    }
    TVPWindowVector.push_back(window);

    // notify that the layer must lost capture state
    std::vector<tTJSNI_Window*>::iterator i;
    for (i = TVPWindowVector.begin(); i != TVPWindowVector.end(); i++)
    {
        (*i)->PostReleaseCaptureEvent();
    }
}
//---------------------------------------------------------------------------
static void TVPUnregisterWindowToList(tTJSNI_Window* window)
{
    std::vector<tTJSNI_Window*>::iterator i;
    i = std::find(TVPWindowVector.begin(), TVPWindowVector.end(), window);
    if (i != TVPWindowVector.end())
    {
        bool flag = false;
        if (*i == TVPMainWindow)
            flag = true;

        TVPWindowVector.erase(i);

        if (flag)
        {
            TVPMainWindowClosed(); // MainWindow had been closed
            TVPMainWindow = NULL;
        }
    }
}
//---------------------------------------------------------------------------
tTJSNI_Window* TVPGetWindowListAt(tjs_int idx)
{
    return TVPWindowVector[idx];
}
//---------------------------------------------------------------------------
tjs_int TVPGetWindowCount()
{
    return (tjs_int)TVPWindowVector.size();
}
//---------------------------------------------------------------------------
void TVPClearAllWindowInputEvents()
{
    std::vector<tTJSNI_Window*>::iterator i;
    for (i = TVPWindowVector.begin(); i != TVPWindowVector.end(); i++)
    {
        (*i)->ClearInputEvents();
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Input Events
//---------------------------------------------------------------------------
// For each input event tag
tTVPUniqueTagForInputEvent tTVPOnCloseInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnResizeInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnClickInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnDoubleClickInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnMouseDownInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnMouseUpInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnMouseMoveInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnReleaseCaptureInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnMouseOutOfWindowInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnMouseEnterInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnMouseLeaveInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnKeyDownInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnKeyUpInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnKeyPressInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnFileDropInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnMouseWheelInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnPopupHideInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnWindowActivateEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnTouchDownInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnTouchUpInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnTouchMoveInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnTouchScalingInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnTouchRotateInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnMultiTouchInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnHintChangeInputEvent ::Tag;
tTVPUniqueTagForInputEvent tTVPOnDisplayRotateInputEvent ::Tag;
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNI_BaseWindow
//---------------------------------------------------------------------------
tTJSNI_BaseWindow::tTJSNI_BaseWindow()
{
    WaitVSync = false;
    ObjectVectorLocked = false;
    DrawBuffer = NULL;
    WindowExposedRegion.clear();
    WindowUpdating = false;
    DrawDevice = NULL;
}
//---------------------------------------------------------------------------
tTJSNI_BaseWindow::~tTJSNI_BaseWindow()
{
    TVPUnregisterWindowToList(static_cast<tTJSNI_Window*>(this)); // making sure...
}
//---------------------------------------------------------------------------
tjs_error tTJSNI_BaseWindow::Construct(tjs_int numparams,
                                       tTJSVariant** param,
                                       iTJSDispatch2* tjs_obj)
{
    Owner = tjs_obj; // no addref
    TVPRegisterWindowToList(static_cast<tTJSNI_Window*>(this));

    // set default draw device object "PassThrough"
    {
        iTJSDispatch2* cls = NULL;
        iTJSDispatch2* newobj = NULL;
        try
        {
            cls = new tTJSNC_BasicDrawDevice();
            if (TJS_FAILED(cls->CreateNew(0, NULL, NULL, &newobj, 0, NULL, cls)))
                TVPThrowExceptionMessage(TVPInternalError, TJS_N("tTJSNI_Window::Construct"));
            SetDrawDeviceObject(tTJSVariant(newobj, newobj));
        }
        catch (...)
        {
            if (cls)
                cls->Release();
            if (newobj)
                newobj->Release();
            throw;
        }
        if (cls)
            cls->Release();
        if (newobj)
            newobj->Release();
    }

    return TJS_S_OK;
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::Invalidate()
{
    // remove from list
    TVPUnregisterWindowToList(static_cast<tTJSNI_Window*>(this));

    // remove all events
    TVPCancelSourceEvents(Owner);
    TVPCancelInputEvents(this);

    // clear all window update events
    TVPRemoveWindowUpdate((tTJSNI_Window*)this);

    // free DrawBuffer
    if (DrawBuffer)
        delete DrawBuffer;

    // disconnect all VideoOverlay objects
    {
        tObjectListSafeLockHolder<tTJSNI_BaseVideoOverlay> holder(VideoOverlay);
        tjs_int count = VideoOverlay.GetSafeLockedObjectCount();
        for (tjs_int i = 0; i < count; i++)
        {
            tTJSNI_BaseVideoOverlay* item = VideoOverlay.GetSafeLockedObjectAt(i);
            if (!item)
                continue;

            item->Disconnect();
        }
    }

    // invalidate all registered objects
    ObjectVectorLocked = true;
    std::vector<tTJSVariantClosure>::iterator i;

    for (i = ObjectVector.begin(); i != ObjectVector.end(); i++)
    {
        // invalidate each --
        // objects may throw an exception while invalidating,
        // but here we cannot care for them.
        try
        {
            i->Invalidate(0, NULL, NULL, NULL);
            i->Release();
        }
        catch (eTJSError& e)
        {
            TVPAddLog(e.GetMessage()); // just in case, log the error
        }
    }

    // remove all events (again)
    TVPCancelSourceEvents(Owner);
    TVPCancelInputEvents(this);

    // notify that the window is no longer available
    //	if(LayerManager) LayerManager->SetWindow(NULL);

    // clear all window update events (again)
    TVPRemoveWindowUpdate((tTJSNI_Window*)this);

    // release draw device
    SetDrawDeviceObject(tTJSVariant());

    inherited::Invalidate();

    /* NOTE: at this point, Owner is still non-null.
       Caller must ensure that the Owner being null at the end of the
       invalidate chain. */
}
//---------------------------------------------------------------------------
bool tTJSNI_BaseWindow::IsMainWindow() const
{
    return TVPMainWindow == static_cast<const tTJSNI_Window*>(this);
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::FireOnActivate(bool activate_or_deactivate)
{
    // fire Window.onActivate or Window.onDeactivate event
    TVPPostInputEvent(new tTVPOnWindowActivateEvent(this, activate_or_deactivate),
                      TVP_EPT_REMOVE_POST // to discard redundant events
    );
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::SetDrawDeviceObject(const tTJSVariant& val)
{
    // invalidate existing draw device
    if (DrawDeviceObject.Type() == tvtObject)
        DrawDeviceObject.AsObjectClosureNoAddRef().Invalidate(0, NULL, NULL,
                                                              DrawDeviceObject.AsObjectNoAddRef());

    // assign new device
    DrawDeviceObject = val;
    DrawDevice = NULL;

    // extract interface
    if (DrawDeviceObject.Type() == tvtObject)
    {
        tTJSVariantClosure clo = DrawDeviceObject.AsObjectClosureNoAddRef();
        tTJSVariant iface_v;
        if (TJS_FAILED(clo.PropGet(0, TJS_N("interface"), NULL, &iface_v, NULL)))
            TVPThrowExceptionMessage(TVPCannotRetriveInterfaceFromDrawDevice);
        DrawDevice = reinterpret_cast<iTVPDrawDevice*>((tjs_intptr_t)(tjs_int64)iface_v);
        DrawDevice->SetWindowInterface(const_cast<tTJSNI_BaseWindow*>(this));
        ResetDrawDevice();
    }
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::OnClose()
{
    if (!CanDeliverEvents())
        return;
    if (Owner)
    {
        tTJSVariant arg[1] = {true};
        static ttstr eventname(TJS_N("onCloseQuery"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 1, arg);
    }
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::OnResize()
{
    if (!CanDeliverEvents())
        return;
    if (Owner)
    {
        static ttstr eventname(TJS_N("onResize"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 0, NULL);
    }
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::OnClick(tjs_int x, tjs_int y)
{
    if (!CanDeliverEvents())
        return;
    if (Owner)
    {
        tTJSVariant arg[2] = {x, y};
        static ttstr eventname(TJS_N("onClick"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 2, arg);
    }
    if (DrawDevice)
        DrawDevice->OnClick(x, y);
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::OnDoubleClick(tjs_int x, tjs_int y)
{
    if (!CanDeliverEvents())
        return;
    if (Owner)
    {
        tTJSVariant arg[2] = {x, y};
        static ttstr eventname(TJS_N("onDoubleClick"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 2, arg);
    }
    if (DrawDevice)
        DrawDevice->OnDoubleClick(x, y);
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::OnMouseDown(tjs_int x, tjs_int y, tTVPMouseButton mb, tjs_uint32 flags)
{
    if (!CanDeliverEvents())
        return;
    if (Owner)
    {
        tTJSVariant arg[4] = {x, y, (tjs_int64)mb, (tjs_int64)flags};
        static ttstr eventname(TJS_N("onMouseDown"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 4, arg);
    }
    if (DrawDevice)
        DrawDevice->OnMouseDown(x, y, mb, flags);
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::OnMouseUp(tjs_int x, tjs_int y, tTVPMouseButton mb, tjs_uint32 flags)
{
    if (!CanDeliverEvents())
        return;
    if (Owner)
    {
        tTJSVariant arg[4] = {x, y, (tjs_int)mb, (tjs_int)flags};
        static ttstr eventname(TJS_N("onMouseUp"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 4, arg);
    }
    if (DrawDevice)
        DrawDevice->OnMouseUp(x, y, mb, flags);
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::OnMouseMove(tjs_int x, tjs_int y, tjs_uint32 flags)
{
    if (!CanDeliverEvents())
        return;
    if (Owner)
    {
        static ttstr eventname(TJS_N("onMouseMove"));
        tTJSVariant arg[3] = {x, y, (tjs_int64)flags};
        TVPPostEvent(Owner, Owner, eventname, 0,
                     TVP_EPT_DISCARDABLE | TVP_EPT_IMMEDIATE
                     /*discardable!!*/,
                     3, arg);
    }
    if (DrawDevice)
        DrawDevice->OnMouseMove(x, y, flags);
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::OnTouchDown(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id)
{
    if (!CanDeliverEvents())
        return;
    if (Owner)
    {
        tTJSVariant arg[5] = {x, y, cx, cy, (tjs_int64)id};
        static ttstr eventname(TJS_N("onTouchDown"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 5, arg);
    }
    if (DrawDevice)
        DrawDevice->OnTouchDown(x, y, cx, cy, id);
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::OnTouchUp(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id)
{
    if (!CanDeliverEvents())
        return;
    if (Owner)
    {
        tTJSVariant arg[5] = {x, y, cx, cy, (tjs_int64)id};
        static ttstr eventname(TJS_N("onTouchUp"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 5, arg);
    }
    if (DrawDevice)
        DrawDevice->OnTouchUp(x, y, cx, cy, id);
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::OnTouchMove(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id)
{
    if (!CanDeliverEvents())
        return;
    if (Owner)
    {
        tTJSVariant arg[5] = {x, y, cx, cy, (tjs_int64)id};
        static ttstr eventname(TJS_N("onTouchMove"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 5, arg);
    }
    if (DrawDevice)
        DrawDevice->OnTouchMove(x, y, cx, cy, id);
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::OnTouchScaling(
    tjs_real startdist, tjs_real curdist, tjs_real cx, tjs_real cy, tjs_int flag)
{
    if (!CanDeliverEvents())
        return;
    if (Owner)
    {
        tTJSVariant arg[5] = {startdist, curdist, cx, cy, flag};
        static ttstr eventname(TJS_N("onTouchScaling"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 5, arg);
    }
    if (DrawDevice)
        DrawDevice->OnTouchScaling(startdist, curdist, cx, cy, flag);
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::OnTouchRotate(
    tjs_real startangle, tjs_real curangle, tjs_real dist, tjs_real cx, tjs_real cy, tjs_int flag)
{
    if (!CanDeliverEvents())
        return;
    if (Owner)
    {
        tTJSVariant arg[6] = {startangle, curangle, dist, cx, cy, flag};
        static ttstr eventname(TJS_N("onTouchRotate"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 6, arg);
    }
    if (DrawDevice)
        DrawDevice->OnTouchRotate(startangle, curangle, dist, cx, cy, flag);
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::OnMultiTouch()
{
    if (!CanDeliverEvents())
        return;
    if (Owner)
    {
        static ttstr eventname(TJS_N("onMultiTouch"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 0, NULL);
    }
    if (DrawDevice)
        DrawDevice->OnMultiTouch();
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::OnReleaseCapture()
{
    if (!CanDeliverEvents())
        return;
    if (DrawDevice)
        DrawDevice->OnReleaseCapture();
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::OnMouseOutOfWindow()
{
    if (!CanDeliverEvents())
        return;
    if (DrawDevice)
        DrawDevice->OnMouseOutOfWindow();
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::OnMouseEnter()
{
    if (!CanDeliverEvents())
        return;
    if (Owner)
    {
        static ttstr eventname(TJS_N("onMouseEnter"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 0, NULL);
    }
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::OnMouseLeave()
{
    if (!CanDeliverEvents())
        return;
    if (Owner)
    {
        static ttstr eventname(TJS_N("onMouseLeave"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 0, NULL);
    }
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::OnKeyDown(tjs_uint key, tjs_uint32 shift)
{
    if (!CanDeliverEvents())
        return;
    if (Owner)
    {
        tTJSVariant arg[2] = {(tjs_int)key, (tjs_int)shift};
        static ttstr eventname(TJS_N("onKeyDown"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 2, arg);
    }
    if (DrawDevice)
        DrawDevice->OnKeyDown(key, shift);
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::OnKeyUp(tjs_uint key, tjs_uint32 shift)
{
    if (!CanDeliverEvents())
        return;
    if (Owner)
    {
        tTJSVariant arg[2] = {(tjs_int)key, (tjs_int)shift};
        static ttstr eventname(TJS_N("onKeyUp"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 2, arg);
    }
    if (DrawDevice)
        DrawDevice->OnKeyUp(key, shift);
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::OnKeyPress(tjs_char key)
{
    if (!CanDeliverEvents())
        return;
    if (Owner)
    {
        tjs_char buf[2];
        buf[0] = (tjs_char)key;
        buf[1] = 0;
        tTJSVariant arg[1] = {buf};
        static ttstr eventname(TJS_N("onKeyPress"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 1, arg);
    }
    if (DrawDevice)
        DrawDevice->OnKeyPress(key);
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::OnFileDrop(const tTJSVariant& array)
{
    if (!CanDeliverEvents())
        return;
    if (Owner)
    {
        tTJSVariant arg[1] = {array};
        static ttstr eventname(TJS_N("onFileDrop"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 1, arg);
    }
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::OnMouseWheel(tjs_uint32 shift, tjs_int delta, tjs_int x, tjs_int y)
{
    if (!CanDeliverEvents())
        return;
    if (Owner)
    {
        tTJSVariant arg[4] = {(tjs_int)shift, delta, x, y};
        static ttstr eventname(TJS_N("onMouseWheel"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 4, arg);
    }
    if (DrawDevice)
        DrawDevice->OnMouseWheel(shift, delta, x, y);
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::OnPopupHide()
{
    if (!CanDeliverEvents())
        return;
    if (Owner)
    {
        static ttstr eventname(TJS_N("onPopupHide"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 0, NULL);
    }
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::OnActivate(bool activate_or_deactivate)
{
    if (!CanDeliverEvents())
        return;

    // re-check the window activate state
    if (GetWindowActive() == activate_or_deactivate)
    {
        if (Owner)
        {
            static ttstr a_eventname(TJS_N("onActivate"));
            static ttstr d_eventname(TJS_N("onDeactivate"));
            TVPPostEvent(Owner, Owner, activate_or_deactivate ? a_eventname : d_eventname, 0,
                         TVP_EPT_IMMEDIATE, 0, NULL);
        }
    }
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::OnHintChange(const ttstr& text, tjs_int x, tjs_int y, bool isshow)
{
    if (!CanDeliverEvents())
        return;
    if (Owner)
    {
        tTJSVariant arg[6] = {text, x, y, isshow ? 1 : 0};
        static ttstr eventname(TJS_N("onHintChanged"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 4, arg);
    }
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::OnDisplayRotate(
    tjs_int orientation, tjs_int rotate, tjs_int bpp, tjs_int hresolution, tjs_int vresolution)
{
    if (!CanDeliverEvents())
        return;
    if (Owner)
    {
        tTJSVariant arg[5] = {orientation, rotate, bpp, hresolution, vresolution};
        static ttstr eventname(TJS_N("onDisplayRotate"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 5, arg);
    }
    if (DrawDevice)
        DrawDevice->OnDisplayRotate(orientation, rotate, bpp, hresolution, vresolution);
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::ClearInputEvents()
{
    TVPCancelInputEvents(this);
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::PostReleaseCaptureEvent()
{
    TVPPostInputEvent(new tTVPOnReleaseCaptureInputEvent(this));
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::RegisterLayerManager(iTVPLayerManager* manager)
{
    if (DrawDevice)
        DrawDevice->AddLayerManager(manager);
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::UnregisterLayerManager(iTVPLayerManager* manager)
{
    if (DrawDevice)
        DrawDevice->RemoveLayerManager(manager);
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::NotifyWindowExposureToLayer(const tTVPRect& cliprect)
{
    if (DrawDevice)
        DrawDevice->RequestInvalidation(cliprect);
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::NotifyUpdateRegionFixed(const tTVPComplexRect& updaterects)
{
    // is called by layer manager
    BeginUpdate(updaterects);
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::UpdateContent()
{
    if (DrawDevice)
    {
        // is called from event dispatcher
        DrawDevice->Update();

        if (!WaitVSync)
            DrawDevice->Show();

        EndUpdate();
    }
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::DeliverDrawDeviceShow()
{
    // call DrawDevice->Show, at VBlank
    if (DrawDevice)
        DrawDevice->Show();
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::BeginUpdate(const tTVPComplexRect& rects)
{
    WindowUpdating = true;
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::EndUpdate()
{
    WindowUpdating = false;
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::DumpPrimaryLayerStructure()
{
    if (DrawDevice)
        DrawDevice->DumpLayerStructure();
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::RecheckInputState()
{
    // slow timer tick (about 1 sec interval, inaccurate)
    if (DrawDevice)
        DrawDevice->RecheckInputState();
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::SetShowUpdateRect(bool b)
{
    // show update rectangle if possible
    if (DrawDevice)
        DrawDevice->SetShowUpdateRect(b);
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::RequestUpdate()
{
    // is called from primary layer

    // post update event to self
    TVPPostWindowUpdate((tTJSNI_Window*)this);
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::NotifySrcResize()
{
    // is called from primary layer
    if (WindowUpdating)
        TVPThrowExceptionMessage(TVPInvalidMethodInUpdating);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::RegisterVideoOverlayObject(tTJSNI_BaseVideoOverlay* ovl)
{
    VideoOverlay.Add(ovl);
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::UnregisterVideoOverlayObject(tTJSNI_BaseVideoOverlay* ovl)
{
    VideoOverlay.Remove(ovl);
}
//---------------------------------------------------------------------------

//---- methods

//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::Add(tTJSVariantClosure clo)
{
    if (ObjectVectorLocked)
        return;
    if (ObjectVector.end() == std::find(ObjectVector.begin(), ObjectVector.end(), clo))
    {
        ObjectVector.push_back(clo);
        clo.AddRef();
    }
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::Remove(tTJSVariantClosure clo)
{
    if (ObjectVectorLocked)
        return;
    std::vector<tTJSVariantClosure>::iterator i;
    i = std::find(ObjectVector.begin(), ObjectVector.end(), clo);
    if (i != ObjectVector.end())
    {
        clo.Release();
        ObjectVector.erase(i);
    }
}
//---------------------------------------------------------------------------
void tTJSNI_BaseWindow::SetWaitVSync(bool enable)
{
    WaitVSync = enable;
    UpdateVSyncThread();
}
//---------------------------------------------------------------------------
bool tTJSNI_BaseWindow::GetWaitVSync() const
{
    return WaitVSync;
}
//---------------------------------------------------------------------------

#define MK_SHIFT 4
#define MK_CONTROL 8
#define MK_ALT (0x20)
tjs_uint32 TVP_TShiftState_To_uint32(tjs_uint32 state)
{
    tjs_uint32 result = 0;
    if (state & MK_SHIFT)
    {
        result |= ssShift;
    }
    if (state & MK_CONTROL)
    {
        result |= ssCtrl;
    }
    if (state & MK_ALT)
    {
        result |= ssAlt;
    }
    return result;
}
tjs_uint32 TVP_TShiftState_From_uint32(tjs_uint32 state)
{
    tjs_uint32 result = 0;
    if (state & ssShift)
    {
        result |= MK_SHIFT;
    }
    if (state & ssCtrl)
    {
        result |= MK_CONTROL;
    }
    if (state & ssAlt)
    {
        result |= MK_ALT;
    }
    return result;
}

//---------------------------------------------------------------------------
// Mouse Cursor management
//---------------------------------------------------------------------------
static tTJSHashTable<ttstr, tjs_int> TVPCursorTable;
tjs_int TVPGetCursor(const ttstr& name)
{
    // get placed path
    ttstr place(TVPSearchPlacedPath(name));

    // search in cache
    tjs_int* in_hash = TVPCursorTable.Find(place);
    if (in_hash)
        return *in_hash;
    return 0;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNI_Window
//---------------------------------------------------------------------------
tTJSNI_Window::tTJSNI_Window()
{
    // TVPEnsureVSyncTimingThread();
    //	VSyncTimingThread = NULL;
    Form = NULL;
}
//---------------------------------------------------------------------------
tjs_error tTJSNI_Window::Construct(tjs_int numparams, tTJSVariant** param, iTJSDispatch2* tjs_obj)
{
    tjs_error hr = tTJSNI_BaseWindow::Construct(numparams, param, tjs_obj);
    if (TJS_FAILED(hr))
        return hr;
    if (numparams >= 1 && param[0]->Type() == tvtObject)
    {
        tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();
        tTJSNI_Window* win = NULL;
        if (clo.Object != NULL)
        {
            if (TJS_FAILED(clo.Object->NativeInstanceSupport(
                    TJS_NIS_GETINSTANCE, tTJSNC_Window::ClassID, (iTJSNativeInstance**)&win)))
                TVPThrowExceptionMessage(TVPSpecifyWindow);
            if (!win)
                TVPThrowExceptionMessage(TVPSpecifyWindow);
        }
    }
    Form = TVPCreateAndAddWindow(this);
    return TJS_S_OK;
}
//---------------------------------------------------------------------------
void tTJSNI_Window::Invalidate()
{
    tTJSNI_BaseWindow::Invalidate();
    if (Form)
    {
        Form->InvalidateClose();
        Form = NULL;
    }

    // remove all events
    TVPCancelSourceEvents(Owner);
    TVPCancelInputEvents(this);

    // Set Owner null
    Owner = NULL;
}
//---------------------------------------------------------------------------
bool tTJSNI_Window::CanDeliverEvents() const
{
    if (!Form)
        return false;
    return GetVisible() && Form->GetFormEnabled();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::NotifyWindowClose()
{
    Form = NULL;
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SendCloseMessage()
{
    if (Form)
        Form->SendCloseMessage();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::TickBeat()
{
    if (Form)
        Form->TickBeat();
}
//---------------------------------------------------------------------------
bool tTJSNI_Window::GetWindowActive()
{
    if (Form)
        return Form->GetWindowActive();
    return false;
}
//---------------------------------------------------------------------------
void tTJSNI_Window::ResetDrawDevice()
{
    if (Form)
        Form->ResetDrawDevice();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::FullScreenGuard() const
{
    
}
//---------------------------------------------------------------------------
void tTJSNI_Window::PostInputEvent(const ttstr& name, iTJSDispatch2* params)
{
    // posts input event
    if (!Form)
        return;

    static ttstr key_name(TJS_N("key"));
    static ttstr shift_name(TJS_N("shift"));

    // check input event name
    enum tEventType
    {
        etUnknown,
        etOnKeyDown,
        etOnKeyUp,
        etOnKeyPress
    } type;

    if (name == TJS_N("onKeyDown"))
        type = etOnKeyDown;
    else if (name == TJS_N("onKeyUp"))
        type = etOnKeyUp;
    else if (name == TJS_N("onKeyPress"))
        type = etOnKeyPress;
    else
        type = etUnknown;

    if (type == etUnknown)
        TVPThrowExceptionMessage(TVPSpecifiedEventNameIsUnknown, name);

    if (type == etOnKeyDown || type == etOnKeyUp)
    {
        // this needs params, "key" and "shift"
        if (params == NULL)
            TVPThrowExceptionMessage(TVPSpecifiedEventNeedsParameter, name);

        tjs_uint key;
        tjs_uint32 shift = 0;

        tTJSVariant val;
        if (TJS_SUCCEEDED(params->PropGet(0, key_name.c_str(), key_name.GetHint(), &val, params)))
            key = (tjs_int)val;
        else
            TVPThrowExceptionMessage(TVPSpecifiedEventNeedsParameter2, name, TJS_N("key"));

        if (TJS_SUCCEEDED(
                params->PropGet(0, shift_name.c_str(), shift_name.GetHint(), &val, params)))
            shift = (tjs_int)val;
        else
            TVPThrowExceptionMessage(TVPSpecifiedEventNeedsParameter2, name, TJS_N("shift"));

        uint16_t vcl_key = key;
        if (type == etOnKeyDown)
            Form->InternalKeyDown(key, shift);
        else if (type == etOnKeyUp)
            // Form->OnKeyUp(Form, vcl_key, TVP_TShiftState_From_uint32(shift));
            Form->OnKeyUp(vcl_key, TVP_TShiftState_From_uint32(shift));
    }
    else if (type == etOnKeyPress)
    {
        // this needs param, "key"
        if (params == NULL)
            TVPThrowExceptionMessage(TVPSpecifiedEventNeedsParameter, name);

        tjs_uint key;

        tTJSVariant val;
        if (TJS_SUCCEEDED(params->PropGet(0, key_name.c_str(), key_name.GetHint(), &val, params)))
            key = (tjs_int)val;
        else
            TVPThrowExceptionMessage(TVPSpecifiedEventNeedsParameter2, name, TJS_N("key"));

        char vcl_key = key;
        // Form->OnKeyPress(Form, vcl_key);
        Form->OnKeyPress(vcl_key, 0, false, false);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_Window::NotifySrcResize()
{
    tTJSNI_BaseWindow::NotifySrcResize();

    // is called from primary layer
    // ( or from TWindowForm to reset paint box's size )
    tjs_int w, h;
    DrawDevice->GetSrcSize(w, h);
    if (Form)
        Form->SetPaintBoxSize(w, h);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetDefaultMouseCursor()
{
    // set window mouse cursor to default
    if (Form)
        Form->SetDefaultMouseCursor();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetMouseCursor(tjs_int handle)
{
    // set window mouse cursor
    if (Form)
        Form->SetMouseCursor(handle);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::GetCursorPos(tjs_int& x, tjs_int& y)
{
    // get cursor pos in primary layer's coordinates
    if (Form)
        Form->GetCursorPos(x, y);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetCursorPos(tjs_int x, tjs_int y)
{
    // set cursor pos in primar layer's coordinates
    if (Form)
        Form->SetCursorPos(x, y);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::WindowReleaseCapture()
{
    //::ReleaseCapture(); // Windows API
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetHintText(iTJSDispatch2* sender, const ttstr& text)
{
    // set hint text to window
    if (Form)
        Form->SetHintText(sender, text);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetAttentionPoint(tTJSNI_BaseLayer* layer, tjs_int l, tjs_int t)
{
    // set attention point to window
    if (Form)
    {
        // class TFont * font = NULL;
        const tTVPFont* font = NULL;
        if (layer)
        {
            iTVPBaseBitmap* bmp = layer->GetMainImage();
            if (bmp)
            {
                // font = bmp->GetFontCanvas()->GetFont(); =
                //  font = bmp->GetFontCanvas();
                const tTVPFont& finfo = bmp->GetFont();
                font = &finfo;
            }
        }

        Form->SetAttentionPoint(l, t, font);
    }
}
//---------------------------------------------------------------------------
void tTJSNI_Window::DisableAttentionPoint()
{
    // disable attention point
    if (Form)
        Form->DisableAttentionPoint();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetImeMode(tTVPImeMode mode)
{
    // set ime mode
    if (Form)
        Form->SetImeMode(mode);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetDefaultImeMode(tTVPImeMode mode)
{
    // set default ime mode
    if (Form)
    {
        //		Form->SetDefaultImeMode(mode, LayerManager->GetFocusedLayer() == NULL);
    }
}
//---------------------------------------------------------------------------
tTVPImeMode tTJSNI_Window::GetDefaultImeMode() const
{
    if (Form)
        return Form->GetDefaultImeMode();
    return ::imDisable;
}
//---------------------------------------------------------------------------
void tTJSNI_Window::ResetImeMode()
{
    // set default ime mode ( default mode is imDisable; IME is disabled )
    if (Form)
        Form->ResetImeMode();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::BeginUpdate(const tTVPComplexRect& rects)
{
    tTJSNI_BaseWindow::BeginUpdate(rects);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::EndUpdate()
{
    tTJSNI_BaseWindow::EndUpdate();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::ZoomRectangle(tjs_int& left, tjs_int& top, tjs_int& right, tjs_int& bottom)
{
    if (!Form)
        return;
    Form->ZoomRectangle(left, top, right, bottom);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::GetVideoOffset(tjs_int& ofsx, tjs_int& ofsy)
{
    if (!Form)
    {
        ofsx = 0;
        ofsy = 0;
    }
    else
    {
        Form->GetVideoOffset(ofsx, ofsy);
    }
}
//---------------------------------------------------------------------------
void tTJSNI_Window::ReadjustVideoRect()
{
    if (!Form)
        return;

    // re-adjust video rectangle.
    // this reconnects owner window and video offsets.

    tObjectListSafeLockHolder<tTJSNI_BaseVideoOverlay> holder(VideoOverlay);
    tjs_int count = VideoOverlay.GetSafeLockedObjectCount();

    for (tjs_int i = 0; i < count; i++)
    {
        tTJSNI_VideoOverlay* item = (tTJSNI_VideoOverlay*)VideoOverlay.GetSafeLockedObjectAt(i);
        if (!item)
            continue;
        item->ResetOverlayParams();
    }
}
//---------------------------------------------------------------------------
void tTJSNI_Window::WindowMoved()
{
    // inform video overlays that the window has moved.
    // video overlays typically owns Direct3D surface which is not a part of
    // normal window systems and does not matter where the owner window is.
    // so we must inform window moving to overlay window.

    tObjectListSafeLockHolder<tTJSNI_BaseVideoOverlay> holder(VideoOverlay);
    tjs_int count = VideoOverlay.GetSafeLockedObjectCount();
    for (tjs_int i = 0; i < count; i++)
    {
        tTJSNI_VideoOverlay* item = (tTJSNI_VideoOverlay*)VideoOverlay.GetSafeLockedObjectAt(i);
        if (!item)
            continue;
        item->SetRectangleToVideoOverlay();
    }
}
//---------------------------------------------------------------------------
void tTJSNI_Window::DetachVideoOverlay()
{
    // detach video overlay window
    // this is done before the window is being fullscreened or un-fullscreened.
    tObjectListSafeLockHolder<tTJSNI_BaseVideoOverlay> holder(VideoOverlay);
    tjs_int count = VideoOverlay.GetSafeLockedObjectCount();
    for (tjs_int i = 0; i < count; i++)
    {
        tTJSNI_VideoOverlay* item = (tTJSNI_VideoOverlay*)VideoOverlay.GetSafeLockedObjectAt(i);
        if (!item)
            continue;
        item->DetachVideoOverlay();
    }
}
//---------------------------------------------------------------------------
void tTJSNI_Window::RegisterWindowMessageReceiver(tTVPWMRRegMode mode,
                                                  void* proc,
                                                  const void* userdata)
{
    if (!Form)
        return;
    Form->RegisterWindowMessageReceiver(mode, proc, userdata);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::Close()
{
    if (Form)
        Form->Close();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::OnCloseQueryCalled(bool b)
{
    if (Form)
        Form->OnCloseQueryCalled(b);
}
//---------------------------------------------------------------------------
#ifdef USE_OBSOLETE_FUNCTIONS
void tTJSNI_Window::BeginMove()
{
    FullScreenGuard();
    if (Form)
        Form->BeginMove();
}
#endif
//---------------------------------------------------------------------------
void tTJSNI_Window::BringToFront()
{
    if (Form)
        Form->BringToFront();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::Update(tTVPUpdateType type)
{
    if (Form)
        Form->UpdateWindow(type);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::ShowModal()
{
    FullScreenGuard();
    if (Form)
    {
        TVPClearAllWindowInputEvents();
        // cancel all input events that can cause delayed operation
        Form->ShowWindowAsModal();
    }
}
//---------------------------------------------------------------------------
void tTJSNI_Window::HideMouseCursor()
{
    if (Form)
        Form->HideMouseCursor();
}
//---------------------------------------------------------------------------
bool tTJSNI_Window::GetVisible() const
{
    if (!Form)
        return false;
    return Form->GetVisible();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetVisible(bool s)
{
    FullScreenGuard();
    if (Form)
        Form->SetVisibleFromScript(s);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::GetCaption(ttstr& v) const
{
    if (Form)
        v = Form->GetCaption();
    else
        v.Clear();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetCaption(const ttstr& v)
{
    if (Form)
        Form->SetCaption(v.AsStdString());
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetWidth(tjs_int w)
{
    FullScreenGuard();
    if (Form)
        Form->SetWidth(w);
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_Window::GetWidth() const
{
    if (!Form)
        return 0;
    return Form->GetWidth();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetHeight(tjs_int h)
{
    FullScreenGuard();
    if (Form)
        Form->SetHeight(h);
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_Window::GetHeight() const
{
    if (!Form)
        return 0;
    return Form->GetHeight();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetLeft(tjs_int l)
{
    FullScreenGuard();
    if (Form)
        Form->SetLeft(l);
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_Window::GetLeft() const
{
    if (!Form)
        return 0;
    return Form->GetLeft();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetTop(tjs_int t)
{
    FullScreenGuard();
    if (Form)
        Form->SetTop(t);
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_Window::GetTop() const
{
    if (!Form)
        return 0;
    return Form->GetTop();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetSize(tjs_int w, tjs_int h)
{
    FullScreenGuard();
    if (Form)
        Form->SetSize(w, h);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetMinWidth(int v)
{
    FullScreenGuard();
    if (Form)
        Form->SetMinWidth(v);
}
//---------------------------------------------------------------------------
int tTJSNI_Window::GetMinWidth() const
{
    if (Form)
        return Form->GetMinWidth();
    else
        return 0;
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetMinHeight(int v)
{
    FullScreenGuard();
    if (Form)
        Form->SetMinHeight(v);
}
//---------------------------------------------------------------------------
int tTJSNI_Window::GetMinHeight() const
{
    if (Form)
        return Form->GetMinHeight();
    else
        return 0;
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetMinSize(int w, int h)
{
    FullScreenGuard();
    if (Form)
        Form->SetMinSize(w, h);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetMaxWidth(int v)
{
    FullScreenGuard();
    if (Form)
        Form->SetMaxWidth(v);
}
//---------------------------------------------------------------------------
int tTJSNI_Window::GetMaxWidth() const
{
    if (Form)
        return Form->GetMaxWidth();
    else
        return 0;
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetMaxHeight(int v)
{
    FullScreenGuard();
    if (Form)
        Form->SetMaxHeight(v);
}
//---------------------------------------------------------------------------
int tTJSNI_Window::GetMaxHeight() const
{
    if (Form)
        return Form->GetMaxHeight();
    else
        return 0;
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetMaxSize(int w, int h)
{
    FullScreenGuard();
    if (Form)
        Form->SetMaxSize(w, h);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetPosition(tjs_int l, tjs_int t)
{
    FullScreenGuard();
    if (Form)
        Form->SetPosition(l, t);
}
//---------------------------------------------------------------------------
#ifdef USE_OBSOLETE_FUNCTIONS
void tTJSNI_Window::SetLayerLeft(tjs_int l)
{
    if (Form)
        Form->SetLayerLeft(l);
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_Window::GetLayerLeft() const
{
    if (!Form)
        return 0;
    return Form->GetLayerLeft();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetLayerTop(tjs_int t)
{
    if (Form)
        Form->SetLayerTop(t);
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_Window::GetLayerTop() const
{
    if (!Form)
        return 0;
    return Form->GetLayerTop();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetLayerPosition(tjs_int l, tjs_int t)
{
    if (Form)
        Form->SetLayerPosition(l, t);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetInnerSunken(bool b)
{
    FullScreenGuard();
    if (Form)
        Form->SetInnerSunken(b);
}
//---------------------------------------------------------------------------
bool tTJSNI_Window::GetInnerSunken() const
{
    if (!Form)
        return true;
    return Form->GetInnerSunken();
}
#endif
//---------------------------------------------------------------------------
void tTJSNI_Window::SetInnerWidth(tjs_int w)
{
    FullScreenGuard();
    if (Form)
        Form->SetInnerWidth(w);
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_Window::GetInnerWidth() const
{
    if (!Form)
        return 0;
    return Form->GetInnerWidth();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetInnerHeight(tjs_int h)
{
    FullScreenGuard();
    if (Form)
        Form->SetInnerHeight(h);
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_Window::GetInnerHeight() const
{
    if (!Form)
        return 0;
    return Form->GetInnerHeight();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetInnerSize(tjs_int w, tjs_int h)
{
    FullScreenGuard();
    if (Form)
        Form->SetInnerSize(w, h);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetBorderStyle(tTVPBorderStyle st)
{
    FullScreenGuard();
    if (Form)
        Form->SetBorderStyle(st);
}
//---------------------------------------------------------------------------
tTVPBorderStyle tTJSNI_Window::GetBorderStyle() const
{
    if (!Form)
        return (tTVPBorderStyle)0;
    return Form->GetBorderStyle();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetStayOnTop(bool b)
{
    if (!Form)
        return;
    Form->SetStayOnTop(b);
}
//---------------------------------------------------------------------------
bool tTJSNI_Window::GetStayOnTop() const
{
    if (!Form)
        return false;
    return Form->GetStayOnTop();
}
//---------------------------------------------------------------------------
#ifdef USE_OBSOLETE_FUNCTIONS
void tTJSNI_Window::SetShowScrollBars(bool b)
{
    if (Form)
        Form->SetShowScrollBars(b);
}
//---------------------------------------------------------------------------
bool tTJSNI_Window::GetShowScrollBars() const
{
    if (!Form)
        return true;
    return Form->GetShowScrollBars();
}
#endif
//---------------------------------------------------------------------------
void tTJSNI_Window::SetFullScreen(bool b)
{
    if (!Form)
        return;
    Form->SetFullScreenMode(b);
}
//---------------------------------------------------------------------------
bool tTJSNI_Window::GetFullScreen() const
{
    if (!Form)
        return false;
    return Form->GetFullScreenMode();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetUseMouseKey(bool b)
{
    if (!Form)
        return;
    Form->SetUseMouseKey(b);
}
//---------------------------------------------------------------------------
bool tTJSNI_Window::GetUseMouseKey() const
{
    if (!Form)
        return false;
    return Form->GetUseMouseKey();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetTrapKey(bool b)
{
    if (!Form)
        return;
    Form->SetTrapKey(b);
}
//---------------------------------------------------------------------------
bool tTJSNI_Window::GetTrapKey() const
{
    if (!Form)
        return false;
    return Form->GetTrapKey();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetMaskRegion(tjs_int threshold)
{
    if (!Form)
        return;

    if (!DrawDevice)
        TVPThrowExceptionMessage(TVPWindowHasNoLayer);
    tTJSNI_BaseLayer* lay = DrawDevice->GetPrimaryLayer();
    if (!lay)
        TVPThrowExceptionMessage(TVPWindowHasNoLayer);
    //	Form->SetMaskRegion( ((tTJSNI_Layer*)lay)->CreateMaskRgn((tjs_uint)threshold) );
}
//---------------------------------------------------------------------------
void tTJSNI_Window::RemoveMaskRegion()
{
    if (!Form)
        return;
    Form->RemoveMaskRegion();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetMouseCursorState(tTVPMouseCursorState mcs)
{
    if (!Form)
        return;
    Form->SetMouseCursorState(mcs);
}
//---------------------------------------------------------------------------
tTVPMouseCursorState tTJSNI_Window::GetMouseCursorState() const
{
    if (!Form)
        return mcsVisible;
    return Form->GetMouseCursorState();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetFocusable(bool b)
{
    if (!Form)
        return;
    Form->SetFocusable(b);
}
//---------------------------------------------------------------------------
bool tTJSNI_Window::GetFocusable()
{
    if (!Form)
        return true;
    return Form->GetFocusable();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetZoom(tjs_int numer, tjs_int denom)
{
    if (!Form)
        return;
    Form->SetZoom(numer, denom);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetZoomNumer(tjs_int n)
{
    if (!Form)
        return;
    Form->SetZoomNumer(n);
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_Window::GetZoomNumer() const
{
    if (!Form)
        return 1;
    return Form->GetZoomNumer();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetZoomDenom(tjs_int n)
{
    if (!Form)
        return;
    Form->SetZoomDenom(n);
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_Window::GetZoomDenom() const
{
    if (!Form)
        return 1;
    return Form->GetZoomDenom();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetTouchScaleThreshold(tjs_real threshold)
{
    if (!Form)
        return;
    Form->SetTouchScaleThreshold(threshold);
}
//---------------------------------------------------------------------------
tjs_real tTJSNI_Window::GetTouchScaleThreshold() const
{
    if (!Form)
        return 0;
    return Form->GetTouchScaleThreshold();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetTouchRotateThreshold(tjs_real threshold)
{
    if (!Form)
        return;
    Form->SetTouchRotateThreshold(threshold);
}
//---------------------------------------------------------------------------
tjs_real tTJSNI_Window::GetTouchRotateThreshold() const
{
    if (!Form)
        return 0;
    return Form->GetTouchRotateThreshold();
}
//---------------------------------------------------------------------------
tjs_real tTJSNI_Window::GetTouchPointStartX(tjs_int index)
{
    if (!Form)
        return 0;
    return Form->GetTouchPointStartX(index);
}
//---------------------------------------------------------------------------
tjs_real tTJSNI_Window::GetTouchPointStartY(tjs_int index)
{
    if (!Form)
        return 0;
    return Form->GetTouchPointStartY(index);
}
//---------------------------------------------------------------------------
tjs_real tTJSNI_Window::GetTouchPointX(tjs_int index)
{
    if (!Form)
        return 0;
    return Form->GetTouchPointX(index);
}
//---------------------------------------------------------------------------
tjs_real tTJSNI_Window::GetTouchPointY(tjs_int index)
{
    if (!Form)
        return 0;
    return Form->GetTouchPointY(index);
}
//---------------------------------------------------------------------------
tjs_real tTJSNI_Window::GetTouchPointID(tjs_int index)
{
    if (!Form)
        return 0;
    return Form->GetTouchPointID(index);
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_Window::GetTouchPointCount()
{
    if (!Form)
        return 0;
    return Form->GetTouchPointCount();
}
//---------------------------------------------------------------------------
bool tTJSNI_Window::GetTouchVelocity(tjs_int id, float& x, float& y, float& speed) const
{
    if (!Form)
        return false;
    return Form->GetTouchVelocity(id, x, y, speed);
}
//---------------------------------------------------------------------------
bool tTJSNI_Window::GetMouseVelocity(float& x, float& y, float& speed) const
{
    if (!Form)
        return false;
    return Form->GetMouseVelocity(x, y, speed);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::ResetMouseVelocity()
{
    if (!Form)
        return;
    return Form->ResetMouseVelocity();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetHintDelay(tjs_int delay)
{
    if (!Form)
        return;
    Form->SetHintDelay(delay);
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_Window::GetHintDelay() const
{
    if (!Form)
        return 0;
    return Form->GetHintDelay();
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetEnableTouch(bool b)
{
    if (!Form)
        return;
    Form->SetEnableTouch(b);
}
//---------------------------------------------------------------------------
bool tTJSNI_Window::GetEnableTouch() const
{
    if (!Form)
        return 0;
    return Form->GetEnableTouch();
}
//---------------------------------------------------------------------------
int tTJSNI_Window::GetDisplayOrientation()
{
    if (!Form)
        return orientUnknown;
    return Form->GetDisplayOrientation();
}
//---------------------------------------------------------------------------
int tTJSNI_Window::GetDisplayRotate()
{
    if (!Form)
        return -1;
    return Form->GetDisplayRotate();
}
//---------------------------------------------------------------------------
bool tTJSNI_Window::WaitForVBlank(tjs_int* in_vblank, tjs_int* delayed)
{
    if (DrawDevice)
        return DrawDevice->WaitForVBlank(in_vblank, delayed);
    return false;
}
//---------------------------------------------------------------------------
void tTJSNI_Window::UpdateVSyncThread()
{
}
//---------------------------------------------------------------------------
void tTJSNI_Window::StartBitmapCompletion(iTVPLayerManager* manager)
{
    if (DrawDevice)
        DrawDevice->StartBitmapCompletion(manager);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::NotifyBitmapCompleted(class iTVPLayerManager* manager,
                                          tjs_int x,
                                          tjs_int y,
                                          tTVPBaseTexture* bmp,
                                          const tTVPRect& cliprect,
                                          tTVPLayerType type,
                                          tjs_int opacity)
{
    if (DrawDevice)
    {
        DrawDevice->NotifyBitmapCompleted(manager, x, y, bmp, cliprect, type, opacity);
    }
}
//---------------------------------------------------------------------------
void tTJSNI_Window::EndBitmapCompletion(iTVPLayerManager* manager)
{
    if (DrawDevice)
        DrawDevice->EndBitmapCompletion(manager);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetMouseCursor(class iTVPLayerManager* manager, tjs_int cursor)
{
    if (DrawDevice)
    {
        if (cursor == 0)
            DrawDevice->SetDefaultMouseCursor(manager);
        else
            DrawDevice->SetMouseCursor(manager, cursor);
    }
}
//---------------------------------------------------------------------------
void tTJSNI_Window::GetCursorPos(class iTVPLayerManager* manager, tjs_int& x, tjs_int& y)
{
    if (DrawDevice)
        DrawDevice->GetCursorPos(manager, x, y);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetCursorPos(class iTVPLayerManager* manager, tjs_int x, tjs_int y)
{
    if (DrawDevice)
        DrawDevice->SetCursorPos(manager, x, y);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::ReleaseMouseCapture(class iTVPLayerManager* manager)
{
    if (DrawDevice)
        DrawDevice->WindowReleaseCapture(manager);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetHint(class iTVPLayerManager* manager,
                            iTJSDispatch2* sender,
                            const ttstr& hint)
{
    if (DrawDevice)
        DrawDevice->SetHintText(manager, sender, hint);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::NotifyLayerResize(class iTVPLayerManager* manager)
{
    if (DrawDevice)
        DrawDevice->NotifyLayerResize(manager);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::NotifyLayerImageChange(class iTVPLayerManager* manager)
{
    if (DrawDevice)
        DrawDevice->NotifyLayerImageChange(manager);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetAttentionPoint(class iTVPLayerManager* manager,
                                      tTJSNI_BaseLayer* layer,
                                      tjs_int x,
                                      tjs_int y)
{
    if (DrawDevice)
        DrawDevice->SetAttentionPoint(manager, layer, x, y);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::DisableAttentionPoint(class iTVPLayerManager* manager)
{
    if (DrawDevice)
        DrawDevice->DisableAttentionPoint(manager);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::SetImeMode(class iTVPLayerManager* manager,
                               tjs_int mode) // mode == tTVPImeMode
{
    if (DrawDevice)
        DrawDevice->SetImeMode(manager, (tTVPImeMode)mode);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::ResetImeMode(class iTVPLayerManager* manager)
{
    if (DrawDevice)
        DrawDevice->ResetImeMode(manager);
}
//---------------------------------------------------------------------------
void tTJSNI_Window::OnTouchUp(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id)
{
    tTJSNI_BaseWindow::OnTouchUp(x, y, cx, cy, id);
    if (Form)
    {
        Form->ResetTouchVelocity(id);
    }
}
//---------------------------------------------------------------------------