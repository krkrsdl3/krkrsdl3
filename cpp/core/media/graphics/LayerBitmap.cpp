//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Base Layer Bitmap implementation
//---------------------------------------------------------------------------
#include "tjsCommHead.h"
#include "LayerBitmap.h"

#include "tjsUtils.h"
#include "TVPDebug.h"
#include "TVPMsg.h"
#include "TVPThread.h"
#include "RenderManager.h"
#include "TVPSystem.h"
#include "TVPStorage.h"
#include "TVPFont.h"
#include "TVPEvent.h"
#include "FreeTypeFontRasterizer.h"
#include "PrerenderedFont.h"
#include "TVPApplication.h"
#include "TVPConfig.h"

#include "public/ResampleImage.h"

iTVPRenderManager* TVPGetSoftwareRenderManager();

//---------------------------------------------------------------------------
// To forcing bilinear interpolation, define TVP_FORCE_BILINEAR.

#ifdef TVP_FORCE_BILINEAR
#define TVP_BILINEAR_FORCE_COND true
#else
#define TVP_BILINEAR_FORCE_COND false
#endif
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// intact ( does not affect ) gamma adjustment data
tTVPGLGammaAdjustData TVPIntactGammaAdjustData = {1.0, 0, 255, 1.0, 0, 255, 1.0, 0, 255};
//---------------------------------------------------------------------------
const static float sBmFactor[] = {
    59, // bmCopy,
    59, // bmCopyOnAlpha,
    52, // bmAlpha,
    52, // bmAlphaOnAlpha,
    61, // bmAdd,
    59, // bmSub,
    45, // bmMul,
    10, // bmDodge,
    58, // bmDarken,
    56, // bmLighten,
    42, // bmScreen,
    52, // bmAddAlpha,
    52, // bmAddAlphaOnAddAlpha,
    52, // bmAddAlphaOnAlpha,
    52, // bmAlphaOnAddAlpha,
    52, // bmCopyOnAddAlpha,
    32, // bmPsNormal,
    30, // bmPsAdditive,
    29, // bmPsSubtractive,
    27, // bmPsMultiplicative,
    27, // bmPsScreen,
    15, // bmPsOverlay,
    15, // bmPsHardLight,
    10, // bmPsSoftLight,
    10, // bmPsColorDodge,
    10, // bmPsColorDodge5,
    10, // bmPsColorBurn,
    29, // bmPsLighten,
    29, // bmPsDarken,
    29, // bmPsDifference,
    26, // bmPsDifference5,
    66, // bmPsExclusion
};

//---------------------------------------------------------------------------
#define RET_VOID
#define BOUND_CHECK(x) \
    { \
        tjs_int i; \
        if (rect.left < 0) \
            rect.left = 0; \
        if (rect.top < 0) \
            rect.top = 0; \
        if (rect.right > (i = GetWidth())) \
            rect.right = i; \
        if (rect.bottom > (i = GetHeight())) \
            rect.bottom = i; \
        if (rect.right - rect.left <= 0 || rect.bottom - rect.top <= 0) \
            return x; \
    }

//---------------------------------------------------------------------------
// tTVPBaseBitmap
//---------------------------------------------------------------------------
void iTVPBaseBitmap::SetSizeWithFill(tjs_uint w, tjs_uint h, tjs_uint32 fillvalue)
{
    // resize, and fill the expanded region with specified value.

    tjs_uint orgw = GetWidth();
    tjs_uint orgh = GetHeight();

    SetSize(w, h);
    fillvalue = TVP_REVRGB(fillvalue);

    if (w > orgw && h > orgh)
    {
        // both width and height were expanded
        tTVPRect rect;
        rect.left = orgw;
        rect.top = 0;
        rect.right = w;
        rect.bottom = h;
        Fill(rect, fillvalue);

        rect.left = 0;
        rect.top = orgh;
        rect.right = orgw;
        rect.bottom = h;
        Fill(rect, fillvalue);
    }
    else if (w > orgw)
    {
        // width was expanded
        tTVPRect rect;
        rect.left = orgw;
        rect.top = 0;
        rect.right = w;
        rect.bottom = h;
        Fill(rect, fillvalue);
    }
    else if (h > orgh)
    {
        // height was expanded
        tTVPRect rect;
        rect.left = 0;
        rect.top = orgh;
        rect.right = w;
        rect.bottom = h;
        Fill(rect, fillvalue);
    }
}
//---------------------------------------------------------------------------
tjs_uint32 iTVPBaseBitmap::GetPoint(tjs_int x, tjs_int y) const
{
    // get specified point's color or color index
    if (x < 0 || y < 0 || x >= (tjs_int)GetWidth() || y >= (tjs_int)GetHeight())
        TVPThrowExceptionMessage(TVPOutOfRectangle);

    return Bitmap->GetPoint(x, y);
}
//---------------------------------------------------------------------------
bool iTVPBaseBitmap::SetPoint(tjs_int x, tjs_int y, tjs_uint32 value)
{
    // set specified point's color(and opacity) or color index
    if (x < 0 || y < 0 || x >= (tjs_int)GetWidth() || y >= (tjs_int)GetHeight())
        TVPThrowExceptionMessage(TVPOutOfRectangle);

    Bitmap->SetPoint(x, y, TVP_REVRGB(value));
    return true;
}
//---------------------------------------------------------------------------
bool iTVPBaseBitmap::SetPointMain(tjs_int x, tjs_int y, tjs_uint32 color)
{
    // set specified point's color (mask is not touched)
    // for 32bpp
    if (!Is32BPP())
        TVPThrowExceptionMessage(TVPInvalidOperationFor8BPP);

    if (x < 0 || y < 0 || x >= (tjs_int)GetWidth() || y >= (tjs_int)GetHeight())
        TVPThrowExceptionMessage(TVPOutOfRectangle);

    tjs_uint32 clr = Bitmap->GetPoint(x, y);
    clr &= 0xff000000;
    clr += TVP_REVRGB(color) & 0xffffff;
    Bitmap->SetPoint(x, y, clr);

    return true;
}
//---------------------------------------------------------------------------
bool iTVPBaseBitmap::SetPointMask(tjs_int x, tjs_int y, tjs_int mask)
{
    // set specified point's mask (color is not touched)
    // for 32bpp
    if (!Is32BPP())
        TVPThrowExceptionMessage(TVPInvalidOperationFor8BPP);

    if (x < 0 || y < 0 || x >= (tjs_int)GetWidth() || y >= (tjs_int)GetHeight())
        TVPThrowExceptionMessage(TVPOutOfRectangle);

    tjs_uint32 clr = Bitmap->GetPoint(x, y);
    clr &= 0x00ffffff;
    clr += (mask & 0xff) << 24;
    Bitmap->SetPoint(x, y, clr);

    return true;
}
//---------------------------------------------------------------------------
bool tTVPBaseBitmap::Fill(tTVPRect rect, tjs_uint32 value)
{
    // fill target rectangle represented as "rect", with color ( and opacity )
    // passed by "value".
    // value must be : 0xAARRGGBB (for 32bpp) or 0xII ( for 8bpp )
    BOUND_CHECK(false);

    if (Is32BPP())
        value = TVP_REVRGB(value);
    static iTVPRenderMethod* method = GetRenderManager()->GetRenderMethod("FillARGB");
    static int paramid = method->EnumParameterID("color");
    method->SetParameterColor4B(paramid, value);
    iTVPTexture2D* reftex = GetTexture();
    GetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rect),
                                    reftex, rect, tRenderTexRectArray());

    return true;
}

bool iTVPBaseBitmap::Fill(tTVPRect rect, tjs_uint32 value)
{
    // fill target rectangle represented as "rect", with color ( and opacity )
    // passed by "value".
    // value must be : 0xAARRGGBB (for 32bpp) or 0xII ( for 8bpp )
    BOUND_CHECK(false);
    if (Is32BPP())
        value = TVP_REVRGB(value);

    static iTVPRenderMethod* method = TVPGetRenderManager()->GetRenderMethod("FillARGB");
    static int paramid = method->EnumParameterID("color");
    method->SetParameterColor4B(paramid, value);
    iTVPTexture2D* reftex = GetTexture();
    TVPGetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rect),
                                       reftex, rect, tRenderTexRectArray());
    return true;
}

//---------------------------------------------------------------------------
bool iTVPBaseBitmap::FillColor(tTVPRect rect, tjs_uint32 color, tjs_int opa)
{
    // fill rectangle with specified color.
    // this ignores destination alpha (destination alpha will not change)
    // opa is fill opacity if opa is positive value.
    // negative value of opa is not allowed.
    BOUND_CHECK(false);

    if (!Is32BPP())
        TVPThrowExceptionMessage(TVPInvalidOperationFor8BPP);

    if (opa == 0)
        return false; // no action

    if (opa < 0)
        opa = 0;
    if (opa > 255)
        opa = 255;

    color = TVP_REVRGB(color);
    iTVPRenderMethod* method;
    if (opa == 255)
    {
        static iTVPRenderMethod* _method = TVPGetRenderManager()->GetRenderMethod("FillColor");
        static int opa_id = _method->EnumParameterID("opacity"),
                   color_id = _method->EnumParameterID("color");
        method = _method;
        method->SetParameterOpa(opa_id, opa);
        method->SetParameterColor4B(color_id, color);
    }
    else
    {
        static iTVPRenderMethod* _method =
            TVPGetRenderManager()->GetRenderMethod("ConstColorAlphaBlend");
        static int color_id = _method->EnumParameterID("color");
        method = _method;
        method->SetParameterColor4B(color_id, color | 0xFF000000);
    }
    iTVPTexture2D* reftex = GetTexture();
    TVPGetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rect),
                                       reftex, rect, tRenderTexRectArray());
    return true;
}

//---------------------------------------------------------------------------
bool iTVPBaseBitmap::BlendColor(tTVPRect rect, tjs_uint32 color, tjs_int opa, bool additive)
{
    // fill rectangle with specified color.
    // this considers destination alpha (additive or simple)

    BOUND_CHECK(false);

    if (!Is32BPP())
        TVPThrowExceptionMessage(TVPInvalidOperationFor8BPP);

    if (opa == 0)
        return false; // no action
    if (opa < 0)
        opa = 0;
    if (opa > 255)
        opa = 255;

    if (opa == 255 && !IsIndependent())
    {
        if (rect.left == 0 && rect.top == 0 && rect.right == (tjs_int)GetWidth() &&
            rect.bottom == (tjs_int)GetHeight())
        {
            // cover overall
            IndependNoCopy(); // indepent with no-copy
        }
    }
    color = TVP_REVRGB(color);
    iTVPRenderMethod* method;
    if (opa == 255)
    {
        static iTVPRenderMethod* _method = TVPGetRenderManager()->GetRenderMethod("FillARGB");
        static int color_id = _method->EnumParameterID("color");
        method = _method;
        method->SetParameterColor4B(color_id, color | 0xFF000000);
    }
    else if (!additive)
    {
        static iTVPRenderMethod* _method =
            TVPGetRenderManager()->GetRenderMethod("ConstColorAlphaBlend_d");
        static int opa_id = _method->EnumParameterID("opacity"),
                   color_id = _method->EnumParameterID("color");
        method = _method;
        method->SetParameterOpa(opa_id, opa);
        method->SetParameterColor4B(color_id, color);
    }
    else
    {
        static iTVPRenderMethod* _method =
            TVPGetRenderManager()->GetRenderMethod("ConstColorAlphaBlend_a");
        static int opa_id = _method->EnumParameterID("opacity"),
                   color_id = _method->EnumParameterID("color");
        method = _method;
        method->SetParameterOpa(opa_id, opa);
        method->SetParameterColor4B(color_id, color);
    }
    iTVPTexture2D* reftex = GetTexture();
    TVPGetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rect),
                                       reftex, rect, tRenderTexRectArray());
    return true;
}

//---------------------------------------------------------------------------
bool iTVPBaseBitmap::RemoveConstOpacity(tTVPRect rect, tjs_int level)
{
    // remove constant opacity from bitmap. ( similar to PhotoShop's eraser tool )
    // level is a strength of removing ( 0 thru 255 )
    // this cannot work with additive alpha mode.

    BOUND_CHECK(false);

    if (!Is32BPP())
        TVPThrowExceptionMessage(TVPInvalidOperationFor8BPP);

    if (level == 0)
        return false; // no action
    if (level < 0)
        level = 0;
    if (level > 255)
        level = 255;

    iTVPRenderMethod* method;
    if (level == 255)
    {
        static iTVPRenderMethod* _method = TVPGetRenderManager()->GetRenderMethod("FillMask");
        static int opa_id = _method->EnumParameterID("opacity");
        method = _method;
        method->SetParameterOpa(opa_id, 0);
    }
    else
    {
        static iTVPRenderMethod* _method =
            TVPGetRenderManager()->GetRenderMethod("RemoveConstOpacity");
        static int opa_id = _method->EnumParameterID("opacity");
        method = _method;
        method->SetParameterOpa(opa_id, level);
    }
    iTVPTexture2D* reftex = GetTexture();
    TVPGetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rect),
                                       reftex, rect, tRenderTexRectArray());
    return true;
}

//---------------------------------------------------------------------------
bool iTVPBaseBitmap::FillMask(tTVPRect rect, tjs_int value)
{
    // fill mask with specified value.
    BOUND_CHECK(false);

    if (!Is32BPP())
        TVPThrowExceptionMessage(TVPInvalidOperationFor8BPP);

    static iTVPRenderMethod* method = TVPGetRenderManager()->GetRenderMethod("FillMask");
    static int opa_id = method->EnumParameterID("opacity");
    method->SetParameterOpa(opa_id, value);
    iTVPTexture2D* reftex = GetTexture();
    TVPGetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rect),
                                       reftex, rect, tRenderTexRectArray());
    return true;
}
//---------------------------------------------------------------------------

bool tTVPBaseBitmap::CopyRect(
    tjs_int x, tjs_int y, const iTVPBaseBitmap* ref, tTVPRect refrect, tjs_int plane)
{
    // copy bitmap rectangle.
    // TVP_BB_COPY_MAIN in "plane" : main image is copied
    // TVP_BB_COPY_MASK in "plane" : mask image is copied
    // "plane" is ignored if the bitmap is 8bpp
    // the source rectangle is ( "refrect" ) and the destination upper-left corner
    // is (x, y).
    if (!Is32BPP())
        plane = (TVP_BB_COPY_MASK | TVP_BB_COPY_MAIN);
    if (x == 0 && y == 0 && refrect.left == 0 && refrect.top == 0 &&
        refrect.right == (tjs_int)ref->GetWidth() && refrect.bottom == (tjs_int)ref->GetHeight() &&
        (tjs_int)GetWidth() == refrect.right && (tjs_int)GetHeight() == refrect.bottom &&
        plane == (TVP_BB_COPY_MASK | TVP_BB_COPY_MAIN) && (bool)!Is32BPP() == (bool)!ref->Is32BPP())
    {
        // entire area of both bitmaps
        AssignTexture(ref->GetTexture());
        return true;
    }

    // bound check
    tjs_int bmpw, bmph;

    bmpw = ref->GetWidth();
    bmph = ref->GetHeight();

    if (refrect.left < 0)
        x -= refrect.left, refrect.left = 0;
    if (refrect.right > bmpw)
        refrect.right = bmpw;

    if (refrect.left >= refrect.right)
        return false;

    if (refrect.top < 0)
        y -= refrect.top, refrect.top = 0;
    if (refrect.bottom > bmph)
        refrect.bottom = bmph;

    if (refrect.top >= refrect.bottom)
        return false;

    bmpw = GetWidth();
    bmph = GetHeight();

    tTVPRect rect;
    rect.left = x;
    rect.top = y;
    rect.right = rect.left + refrect.get_width();
    rect.bottom = rect.top + refrect.get_height();

    if (rect.left < 0)
    {
        refrect.left += -rect.left;
        rect.left = 0;
    }

    if (rect.right > bmpw)
    {
        refrect.right -= (rect.right - bmpw);
        rect.right = bmpw;
    }

    if (refrect.left >= refrect.right)
        return false; // not drawable

    if (rect.top < 0)
    {
        refrect.top += -rect.top;
        rect.top = 0;
    }

    if (rect.bottom > bmph)
    {
        refrect.bottom -= (rect.bottom - bmph);
        rect.bottom = bmph;
    }

    if (refrect.top >= refrect.bottom)
        return false; // not drawable

    iTVPRenderMethod* method;
    switch (plane)
    {
        case TVP_BB_COPY_MAIN:
        {
            static iTVPRenderMethod* _method = GetRenderManager()->GetRenderMethod("CopyColor");
            method = _method;
        }
        break;
        case TVP_BB_COPY_MASK:
        {
            static iTVPRenderMethod* _method = GetRenderManager()->GetRenderMethod("CopyMask");
            method = _method;
        }
        break;
        case TVP_BB_COPY_MAIN | TVP_BB_COPY_MASK:
        {
            static iTVPRenderMethod* _method = GetRenderManager()->GetRenderMethod("Copy");
            method = _method;
        }
        break;
    }
    tRenderTexRectArray::Element src_tex[] = {
        tRenderTexRectArray::Element(ref->GetTexture(), refrect)};
    iTVPTexture2D* reftex = GetTexture();
    GetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rect),
                                    reftex, rect, tRenderTexRectArray(src_tex));

    return true;
}

bool iTVPBaseBitmap::CopyRect(
    tjs_int x, tjs_int y, const iTVPBaseBitmap* ref, tTVPRect refrect, tjs_int plane)
{
    // copy bitmap rectangle.
    // TVP_BB_COPY_MAIN in "plane" : main image is copied
    // TVP_BB_COPY_MASK in "plane" : mask image is copied
    // "plane" is ignored if the bitmap is 8bpp
    // the source rectangle is ( "refrect" ) and the destination upper-left corner
    // is (x, y).
    if (!Is32BPP())
        plane = (TVP_BB_COPY_MASK | TVP_BB_COPY_MAIN);
    if (x == 0 && y == 0 && refrect.left == 0 && refrect.top == 0 &&
        refrect.right == (tjs_int)ref->GetWidth() && refrect.bottom == (tjs_int)ref->GetHeight() &&
        (tjs_int)GetWidth() == refrect.right && (tjs_int)GetHeight() == refrect.bottom &&
        plane == (TVP_BB_COPY_MASK | TVP_BB_COPY_MAIN) && (bool)!Is32BPP() == (bool)!ref->Is32BPP())
    {
        // entire area of both bitmaps
        AssignTexture(ref->GetTexture());
        return true;
    }

    // bound check
    tjs_int bmpw, bmph;

    bmpw = ref->GetWidth();
    bmph = ref->GetHeight();

    if (refrect.left < 0)
        x -= refrect.left, refrect.left = 0;
    if (refrect.right > bmpw)
        refrect.right = bmpw;

    if (refrect.left >= refrect.right)
        return false;

    if (refrect.top < 0)
        y -= refrect.top, refrect.top = 0;
    if (refrect.bottom > bmph)
        refrect.bottom = bmph;

    if (refrect.top >= refrect.bottom)
        return false;

    bmpw = GetWidth();
    bmph = GetHeight();

    tTVPRect rect;
    rect.left = x;
    rect.top = y;
    rect.right = rect.left + refrect.get_width();
    rect.bottom = rect.top + refrect.get_height();

    if (rect.left < 0)
    {
        refrect.left += -rect.left;
        rect.left = 0;
    }

    if (rect.right > bmpw)
    {
        refrect.right -= (rect.right - bmpw);
        rect.right = bmpw;
    }

    if (refrect.left >= refrect.right)
        return false; // not drawable

    if (rect.top < 0)
    {
        refrect.top += -rect.top;
        rect.top = 0;
    }

    if (rect.bottom > bmph)
    {
        refrect.bottom -= (rect.bottom - bmph);
        rect.bottom = bmph;
    }

    if (refrect.top >= refrect.bottom)
        return false; // not drawable

    iTVPRenderMethod* method;
    switch (plane)
    {
        case TVP_BB_COPY_MAIN:
        {
            static iTVPRenderMethod* _method = TVPGetRenderManager()->GetRenderMethod("CopyColor");
            method = _method;
        }
        break;
        case TVP_BB_COPY_MASK:
        {
            static iTVPRenderMethod* _method = TVPGetRenderManager()->GetRenderMethod("CopyMask");
            method = _method;
        }
        break;
        case TVP_BB_COPY_MAIN | TVP_BB_COPY_MASK:
        {
            static iTVPRenderMethod* _method = TVPGetRenderManager()->GetRenderMethod("Copy");
            method = _method;
        }
        break;
    }
    tRenderTexRectArray::Element src_tex[] = {
        tRenderTexRectArray::Element(ref->GetTexture(), refrect)};
    iTVPTexture2D* reftex = GetTexture();
    TVPGetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rect),
                                       reftex, rect, tRenderTexRectArray(src_tex));
    return true;
}
//---------------------------------------------------------------------------
bool iTVPBaseBitmap::Copy9Patch(const iTVPBaseBitmap* ref, tTVPRect& margin)
{
    if (!Is32BPP())
        return false;

    tjs_int w = ref->GetWidth();
    tjs_int h = ref->GetHeight();
    // 9 + 上下の11ピクセルは必要
    if (w < 11 || h < 11)
        return false;
    tjs_int dw = GetWidth();
    tjs_int dh = GetHeight();
    // コピー先が元画像よりも小さい時はコピー不可
    if (dw < (w - 2) || dh < (h - 2))
        return false;

    if (!TVPGetRenderManager()->IsSoftware())
    {
        static iTVPRenderMethod* method = TVPGetRenderManager()->GetRenderMethod("Copy");
        iTVPTexture2D* reftex = GetTexture();
        tTVPRect rcdest(0, 0, dw, dh);
        iTVPTexture2D* dsttex = GetTextureForRender(method->IsBlendTarget(), &rcdest);
        tRenderTexRectArray::Element src_tex;
        tRenderTexRectArray srctex(&src_tex, 1);
        src_tex.first = ref->GetTexture();
        if (margin.top > 0)
        {
            src_tex.second.top = 0;
            src_tex.second.bottom = margin.top;
            tTVPRect rect(0, 0, 0, margin.top);
            if (margin.left > 0)
            { // LT
                src_tex.second.left = 0;
                src_tex.second.right = margin.left;
                rect.left = 0;
                rect.bottom = margin.left;
                TVPGetRenderManager()->OperateRect(method, dsttex, reftex, rect, srctex);
            }
            if (margin.get_width() > 0)
            { // T
                src_tex.second.left = margin.left;
                src_tex.second.right = margin.right;
                TVPGetRenderManager()->OperateRect(
                    method, dsttex, reftex, tTVPRect(margin.left, 0, dw - margin.right, margin.top),
                    srctex);
            }
            if (margin.right < w)
            { // RT
                src_tex.second.left = margin.right;
                src_tex.second.right = w;
                TVPGetRenderManager()->OperateRect(
                    method, dsttex, reftex, tTVPRect(dw - margin.right, 0, dw, margin.top), srctex);
            }
        }
        if (margin.get_height() > 0)
        {
            if (margin.left > 0)
            { // L
                src_tex.second.left = 0;
                src_tex.second.top = margin.top;
                src_tex.second.right = margin.left;
                src_tex.second.bottom = margin.bottom;
                TVPGetRenderManager()->OperateRect(
                    method, dsttex, reftex,
                    tTVPRect(0, margin.top, margin.left, dh - margin.bottom), srctex);
            }
            if (margin.get_width() > 0)
            { // C
                ;
            }
        }
        return false; // TODO implement universal version
    }

    const tjs_uint32* src = (const tjs_uint32*)ref->GetScanLine(0);
    tjs_int pitch = ref->GetPitchBytes() / sizeof(tjs_uint32);
    const tjs_uint32* srcbottom = (const tjs_uint32*)ref->GetScanLine(h - 1);
    tTVPRect scale(-1, -1, -1, -1);
    margin = tTVPRect(-1, -1, -1, -1);
    // tTVPARGB<tjs_uint8> hor, ver;
    //  TODO
    tTVPARGB<tjs_uint32> hor, ver;
    for (tjs_int x = 1; x < (w - 1); x++)
    {
        if (scale.left == -1 && (src[x] & 0xff000000) == 0xff000000)
        {
            scale.left = x;
            hor = src[x];
        }
        else if (scale.left != -1 && scale.right == -1 && (src[x] & 0xff000000) == 0)
        {
            scale.right = x;
        }

        if (margin.left == -1 && (srcbottom[x] & 0xff000000) == 0xff000000)
        {
            margin.left = x - 1;
        }
        else if (margin.left != -1 && margin.right == -1 && (srcbottom[x] & 0xff000000) == 0)
        {
            margin.right = w - x - 1;
        }

        if (scale.right != -1 && margin.right != -1)
            break;
    }

    for (tjs_int y = 1; y < (h - 1); y++)
    {
        if (scale.top == -1 && (src[y * pitch] & 0xff000000) == 0xff000000)
        {
            scale.top = y;
            ver = src[y * pitch];
        }
        else if (scale.top != -1 && scale.bottom == -1 && (src[y * pitch] & 0xff000000) == 0)
        {
            scale.bottom = y;
        }

        if (margin.top == -1 && (src[y * pitch + w - 1] & 0xff000000) == 0xff000000)
        {
            margin.top = y - 1;
        }
        else if (margin.top != -1 && margin.bottom == -1 &&
                 (src[y * pitch + w - 1] & 0xff000000) == 0)
        {
            margin.bottom = h - y - 1;
        }

        if (scale.bottom != -1 && margin.bottom != -1)
            break;
    }
    // スケール用の領域が見付からない時はコピーできない
    if (scale.left == -1 || scale.right == -1 || scale.top == -1 || scale.bottom == -1)
        return false;

    const tjs_int src_left_width = scale.left - 1;
    const tjs_int src_right_width = w - 1 - scale.right;
    const tjs_int dst_center_width = dw - src_left_width - src_right_width;
    const tjs_int src_center_width = scale.right - scale.left;
    const tjs_int src_top_height = scale.top - 1;
    const tjs_int src_bottom_height = h - 1 - scale.bottom;
    const tjs_int src_center_height = scale.bottom - scale.top;
    const tjs_int dst_center_height = dh - src_top_height - src_bottom_height;
    const tjs_int src_center_step = (src_center_width << 16) / dst_center_width;

    tjs_uint32* dst = (tjs_uint32*)GetScanLineForWrite(0);
    tjs_int dpitch = GetPitchBytes() / sizeof(tjs_uint32);
    const tjs_uint32* s1 = src + pitch + 1;
    const tjs_uint32* s2 = src + pitch + scale.right;
    tjs_uint32* d1 = dst;
    tjs_uint32* d2 = dst + src_left_width + dst_center_width;
    // 上側左右端のコピー
    for (tjs_int y = 0; y < src_top_height; y++)
    {
        memcpy(d1, s1, src_left_width * sizeof(tjs_uint32));
        memcpy(d2, s2, src_right_width * sizeof(tjs_uint32));
        d1 += dpitch;
        s1 += pitch;
        d2 += dpitch;
        s2 += pitch;
    }
    // 上側中間
    const tjs_uint32* s3 = src + pitch + scale.left;
    tjs_uint32* d3 = dst + src_left_width;
    if (src_center_width == 1)
    { // コピー元の幅が1の時はその色で塗りつぶす
        for (tjs_int y = 0; y < src_top_height; y++)
        {
            TVPFillARGB(d3, dst_center_width, *s3);
            d3 += dpitch;
            s3 += pitch;
        }
    }
    // else if( v.r == 0 ) { /* scale */ } else if( v.r == 255 ) { /* repeate */ } else { /* mirror
    // */}
    else
    { // scale
        for (tjs_int y = 0; y < src_top_height; y++)
        { // 縦方向はブレンドしないので高速化出来るが……
            TVPInterpStretchCopy(d3, dst_center_width, s3, s3, 0, 0, src_center_step);
            d3 += dpitch;
            s3 += pitch;
        }
    }
    // 中間位置
    // s1 s2 s3 d1 d2 d3 は、中間位置を指しているはず
    if (src_center_height == 1)
    {
        // 中間位置の両端
        for (tjs_int y = 0; y < dst_center_height; y++)
        {
            memcpy(d1, s1, src_left_width * sizeof(tjs_uint32));
            memcpy(d2, s2, src_right_width * sizeof(tjs_uint32));
            d1 += dpitch;
            d2 += dpitch;
        }
        // 中間位置の真ん中
        if (src_center_width == 1)
        {
            for (tjs_int y = 0; y < dst_center_height; y++)
            {
                TVPFillARGB(d3, dst_center_width, *s3);
                d3 += dpitch;
            }
        }
        else
        {
            for (tjs_int y = 0; y < dst_center_height; y++)
            {
                TVPInterpStretchCopy(d3, dst_center_width, s3, s3, 0, 0, src_center_step);
                d3 += dpitch;
            }
        }
    }
    else
    {
        tTVPRect cliprect(0, 0, dw, dh);
        { // 左側
            tTVPRect srcrect(1, scale.top, scale.left, scale.bottom);
            tTVPRect dstrect(0, src_top_height, src_left_width,
                             (src_top_height + dst_center_height));
            TVPResampleImage(cliprect, this, dstrect, ref, srcrect, stSemiFastLinear, 0.0f, bmCopy,
                             255, false);
        }
        { // 中間
            tTVPRect srcrect(scale.left, scale.top, scale.right, scale.bottom);
            tTVPRect dstrect(src_left_width, src_top_height, src_left_width + dst_center_width,
                             src_top_height + dst_center_height);
            TVPResampleImage(cliprect, this, dstrect, ref, srcrect, stSemiFastLinear, 0.0f, bmCopy,
                             255, false);
        }
        { // 右側
            tTVPRect srcrect(scale.right, scale.top, w - 1, scale.bottom);
            tTVPRect dstrect(dw - src_right_width, src_top_height, dw,
                             src_top_height + dst_center_height);
            TVPResampleImage(cliprect, this, dstrect, ref, srcrect, stSemiFastLinear, 0.0f, bmCopy,
                             255, false);
        }
    }

    // 下側左右端のコピー
    s1 = src + pitch * scale.bottom + 1;
    s2 = src + pitch * scale.bottom + scale.right;
    d1 = dst + dpitch * (dh - src_bottom_height);
    d2 = dst + dpitch * (dh - src_bottom_height) + src_left_width + dst_center_width;
    for (tjs_int y = 0; y < src_bottom_height; y++)
    {
        memcpy(d1, s1, src_left_width * sizeof(tjs_uint32));
        memcpy(d2, s2, src_right_width * sizeof(tjs_uint32));
        d1 += dpitch;
        s1 += pitch;
        d2 += dpitch;
        s2 += pitch;
    }
    // 下側中間
    s3 = src + pitch * scale.bottom + scale.left;
    d3 = dst + dpitch * (dh - src_bottom_height) + src_left_width;
    if (src_center_width == 1)
    { // コピー元の幅が1の時はその色で塗りつぶす
        for (tjs_int y = 0; y < src_bottom_height; y++)
        {
            TVPFillARGB(d3, dst_center_width, *s3);
            d3 += dpitch;
            s3 += pitch;
        }
    }
    else
    { // scale
        for (tjs_int y = 0; y < src_bottom_height; y++)
        {
            TVPInterpStretchCopy(d3, dst_center_width, s3, s3, 0, 0, src_center_step);
            d3 += dpitch;
            s3 += pitch;
        }
    }
    return true;
}
//---------------------------------------------------------------------------
bool iTVPBaseBitmap::Blt(tjs_int x,
                         tjs_int y,
                         const iTVPBaseBitmap* ref,
                         tTVPRect refrect,
                         tTVPBBBltMethod method,
                         tjs_int opa,
                         bool hda)
{
    // blt src bitmap with various methods.

    // hda option ( hold destination alpha ) holds distination alpha,
    // but will select more complex function ( and takes more time ) for it ( if
    // can do )

    // this function does not matter whether source and destination bitmap is
    // overlapped.

    if (opa == 255 && method == bmCopy && !hda)
    {
        return CopyRect(x, y, ref, refrect);
    }

    if (!Is32BPP())
        TVPThrowExceptionMessage(TVPInvalidOperationFor8BPP);

    if (opa == 0)
        return false; // opacity==0 has no action

    // bound check
    tjs_int bmpw, bmph;

    bmpw = ref->GetWidth();
    bmph = ref->GetHeight();

    if (refrect.left < 0)
        x -= refrect.left, refrect.left = 0;
    if (refrect.right > bmpw)
        refrect.right = bmpw;

    if (refrect.left >= refrect.right)
        return false;

    if (refrect.top < 0)
        y -= refrect.top, refrect.top = 0;
    if (refrect.bottom > bmph)
        refrect.bottom = bmph;

    if (refrect.top >= refrect.bottom)
        return false;

    bmpw = GetWidth();
    bmph = GetHeight();

    tTVPRect rect;
    rect.left = x;
    rect.top = y;
    rect.right = rect.left + refrect.get_width();
    rect.bottom = rect.top + refrect.get_height();

    if (rect.left < 0)
    {
        refrect.left += -rect.left;
        rect.left = 0;
    }

    if (rect.right > bmpw)
    {
        refrect.right -= (rect.right - bmpw);
        rect.right = bmpw;
    }

    if (refrect.left >= refrect.right)
        return false; // not drawable

    if (rect.top < 0)
    {
        refrect.top += -rect.top;
        rect.top = 0;
    }

    if (rect.bottom > bmph)
    {
        refrect.bottom -= (rect.bottom - bmph);
        rect.bottom = bmph;
    }

    if (refrect.top >= refrect.bottom)
        return false; // not drawable

    tRenderTexRectArray::Element src_tex[] = {
        tRenderTexRectArray::Element(ref->GetTexture(), refrect)};
    iTVPRenderManager* mgr = GetRenderManager();
    iTVPRenderMethod* rmethod = mgr->GetRenderMethod(opa, hda, method);
    if (!rmethod)
        return false;
    iTVPTexture2D* reftex = GetTexture();
    mgr->OperateRect(rmethod, GetTextureForRender(rmethod->IsBlendTarget(), &rect), reftex, rect,
                     tRenderTexRectArray(src_tex));
    return true;
}

//---------------------------------------------------------------------------
bool iTVPBaseBitmap::Blt(tjs_int x,
                         tjs_int y,
                         const iTVPBaseBitmap* ref,
                         const tTVPRect& refrect,
                         tTVPLayerType type,
                         tjs_int opa,
                         bool hda)
{

    tTVPBBBltMethod met;
    switch (type)
    {
        case ltBinder:
            // no action
            return false;

        case ltOpaque: // formerly ltCoverRect
                       // copy
            met = opa == 255 ? bmCopy : bmCopyOnAlpha;
            break;

        case ltAlpha: // formerly ltTransparent
                      // alpha blend
            met = hda ? bmAlpha : bmAlphaOnAlpha;
            break;

        case ltAdditive:
            // additive blend
            // hda = true if destination has alpha
            // ( preserving mask )
            met = bmAdd;
            break;

        case ltSubtractive:
            // subtractive blend
            met = bmSub;
            break;

        case ltMultiplicative:
            // multiplicative blend
            met = bmMul;
            break;

        case ltDodge:
            // color dodge ( "Ooi yaki" in Japanese )
            met = bmDodge;
            break;

        case ltDarken:
            // darken blend (select lower luminosity)
            met = bmDarken;
            break;

        case ltLighten:
            // lighten blend (select higher luminosity)
            met = bmLighten;
            break;

        case ltScreen:
            // screen multiplicative blend
            met = bmScreen;
            break;

        case ltAddAlpha:
            // alpha blend
            met = bmAddAlpha;
            break;

        case ltPsNormal:
            // Photoshop compatible normal blend
            met = bmPsNormal;
            break;

        case ltPsAdditive:
            // Photoshop compatible additive blend
            met = bmPsAdditive;
            break;

        case ltPsSubtractive:
            // Photoshop compatible subtractive blend
            met = bmPsSubtractive;
            break;

        case ltPsMultiplicative:
            // Photoshop compatible multiplicative blend
            met = bmPsMultiplicative;
            break;

        case ltPsScreen:
            // Photoshop compatible screen blend
            met = bmPsScreen;
            break;

        case ltPsOverlay:
            // Photoshop compatible overlay blend
            met = bmPsOverlay;
            break;

        case ltPsHardLight:
            // Photoshop compatible hard light blend
            met = bmPsHardLight;
            break;

        case ltPsSoftLight:
            // Photoshop compatible soft light blend
            met = bmPsSoftLight;
            break;

        case ltPsColorDodge:
            // Photoshop compatible color dodge blend
            met = bmPsColorDodge;
            break;

        case ltPsColorDodge5:
            // Photoshop 5.x compatible color dodge blend
            met = bmPsColorDodge5;
            break;

        case ltPsColorBurn:
            // Photoshop compatible color burn blend
            met = bmPsColorBurn;
            break;

        case ltPsLighten:
            // Photoshop compatible compare (lighten) blend
            met = bmPsLighten;
            break;

        case ltPsDarken:
            // Photoshop compatible compare (darken) blend
            met = bmPsDarken;
            break;

        case ltPsDifference:
            // Photoshop compatible difference blend
            met = bmPsDifference;
            break;

        case ltPsDifference5:
            // Photoshop 5.x compatible difference blend
            met = bmPsDifference5;
            break;

        case ltPsExclusion:
            // Photoshop compatible exclusion blend
            met = bmPsExclusion;
            break;

        default:
            return false;
    }

    return Blt(x, y, ref, refrect, met, opa, hda);
}
//---------------------------------------------------------------------------
bool iTVPBaseBitmap::StretchBlt(tTVPRect cliprect,
                                tTVPRect destrect,
                                const iTVPBaseBitmap* ref,
                                tTVPRect refrect,
                                tTVPBBBltMethod method,
                                tjs_int opa,
                                bool hda,
                                tTVPBBStretchType mode,
                                tjs_real typeopt)
{
    // do stretch blt
    // stFastLinear is enabled only in following condition:
    // -------TODO: write corresponding condition--------

    // stLinear and stCubic mode are enabled only in following condition:
    // any magnification, opa:255, method:bmCopy, hda:false
    // no reverse, destination rectangle is within the image.

    // source and destination check
    tjs_int dw = destrect.get_width(), dh = destrect.get_height();
    tjs_int rw = refrect.get_width(), rh = refrect.get_height();

    if (!dw || !rw || !dh || !rh)
        return false; // nothing to do

    // quick check for simple blt
    if (dw == rw && dh == rh && destrect.included_in(cliprect))
    {
        return Blt(destrect.left, destrect.top, ref, refrect, method, opa, hda);
        // no stretch; do normal blt
    }

    if (!Is32BPP())
        TVPThrowExceptionMessage(TVPInvalidOperationFor8BPP);

    // check for special case noticed above

    //--- extract stretch type
    tTVPBBStretchType type = (tTVPBBStretchType)(mode & stTypeMask);

    //--- pull the dimension
    tjs_int w = GetWidth();
    tjs_int h = GetHeight();
    tjs_int refw = ref->GetWidth();
    tjs_int refh = ref->GetHeight();

    //--- clop clipping rectangle with the image
    tTVPRect cr = cliprect;
    if (cr.left < 0)
        cr.left = 0;
    if (cr.top < 0)
        cr.top = 0;
    if (cr.right > w)
        cr.right = w;
    if (cr.bottom > h)
        cr.bottom = h;

    if (cr.left > destrect.left)
    {
        refrect.left += (float)rw / dw * (cr.left - destrect.left);
        destrect.left = cr.left;
    }
    if (cr.right < destrect.right)
    {
        refrect.right -=
            (float)refrect.get_width() / destrect.get_width() * (destrect.right - cr.right);
        destrect.right = cr.right;
    }
    if (cr.top > destrect.top)
    {
        refrect.top += (float)rh / dh * (cr.top - destrect.top);
        destrect.top = cr.top;
    }
    if (cr.bottom < destrect.bottom)
    {
        refrect.bottom -=
            (float)refrect.get_height() / destrect.get_height() * (destrect.bottom - cr.bottom);
        destrect.bottom = cr.bottom;
    }

    static int StretchTypeId = TVPGetRenderManager()->EnumParameterID("StretchType");
    TVPGetRenderManager()->SetParameterInt(StretchTypeId, (int)type);

    tRenderTexRectArray::Element src_tex[] = {
        tRenderTexRectArray::Element(ref->GetTexture(), refrect)};
    iTVPRenderManager* mgr = GetRenderManager();
    iTVPRenderMethod* rmethod = mgr->GetRenderMethod(opa, hda, method);
    if (!rmethod)
        return false;
    iTVPTexture2D* reftex = GetTexture();
    mgr->OperateRect(rmethod, GetTextureForRender(rmethod->IsBlendTarget(), &destrect), reftex,
                     destrect, tRenderTexRectArray(src_tex));
    return true;
}
//---------------------------------------------------------------------------
bool iTVPBaseBitmap::AffineBlt(tTVPRect destrect,
                               const iTVPBaseBitmap* ref,
                               tTVPRect refrect,
                               const tTVPPointD* points_in,
                               tTVPBBBltMethod method,
                               tjs_int opa,
                               tTVPRect* updaterect,
                               bool hda,
                               tTVPBBStretchType mode,
                               bool clear,
                               tjs_uint32 clearcolor)
{
    // check source rectangle
    if (refrect.left >= refrect.right || refrect.top >= refrect.bottom)
        return false;
    if (refrect.left < 0 || refrect.top < 0 || refrect.right > (tjs_int)ref->GetWidth() ||
        refrect.bottom > (tjs_int)ref->GetHeight())
        TVPThrowExceptionMessage(TVPOutOfRectangle);

    // create point list in fixed point real format

    if (destrect.left < 0)
        destrect.left = 0;
    if (destrect.top < 0)
        destrect.top = 0;
    if (destrect.right > (tjs_int)GetWidth())
        destrect.right = GetWidth();
    if (destrect.bottom > (tjs_int)GetHeight())
        destrect.bottom = GetHeight();

    if (destrect.left >= destrect.right || destrect.top >= destrect.bottom)
        return false; // not drawable

    if (clear)
    {
        if (hda)
            FillColor(destrect, clearcolor, 255);
        else
            Fill(destrect, clearcolor);
    }

    tTVPPointD dstpt[6] = {
        {points_in[0].x, points_in[0].y}, // left-top
        {points_in[1].x, points_in[1].y}, // right-top
        {points_in[2].x, points_in[2].y}, // left-bottom

        {points_in[1].x, points_in[1].y}, // right-top
        {points_in[2].x, points_in[2].y}, // left-bottom
        {points_in[1].x - points_in[0].x + points_in[2].x,
         points_in[1].y - points_in[0].y + points_in[2].y}, // right-bottom
    };
    tTVPPointD refpt[6] = {
        {(double)refrect.left, (double)refrect.top},
        {(double)refrect.right, (double)refrect.top},
        {(double)refrect.left, (double)refrect.bottom},

        {(double)refrect.right, (double)refrect.top},
        {(double)refrect.left, (double)refrect.bottom},
        {(double)refrect.right, (double)refrect.bottom},
    };

    tTVPBBStretchType type = (tTVPBBStretchType)(mode & stTypeMask);
    static int StretchTypeId = TVPGetRenderManager()->EnumParameterID("StretchType");
    TVPGetRenderManager()->SetParameterInt(StretchTypeId, (int)type);
    iTVPRenderManager* mgr = GetRenderManager();
    iTVPRenderMethod* _method = mgr->GetRenderMethod(opa, hda, method);
    if (!_method)
        return false;
    tRenderTexQuadArray::Element src_tex[] = {
        tRenderTexQuadArray::Element(ref->GetTexture(), refpt)};
    iTVPTexture2D* reftex = GetTexture();
    mgr->OperateTriangles(_method, 2, GetTextureForRender(_method->IsBlendTarget(), &destrect),
                          reftex, destrect, dstpt, tRenderTexQuadArray(src_tex));
    if (updaterect)
        *updaterect = destrect;
    return true;
}
//---------------------------------------------------------------------------
bool iTVPBaseBitmap::AffineBlt(tTVPRect destrect,
                               const iTVPBaseBitmap* ref,
                               tTVPRect refrect,
                               const t2DAffineMatrix& matrix,
                               tTVPBBBltMethod method,
                               tjs_int opa,
                               tTVPRect* updaterect,
                               bool hda,
                               tTVPBBStretchType type,
                               bool clear,
                               tjs_uint32 clearcolor)
{
    // do transformation using 2D affine matrix.
    //  x' =  ax + cy + tx
    //  y' =  bx + dy + ty
    tTVPPointD points[3];
    int rp = refrect.get_width();
    int bp = refrect.get_height();

    // note that a pixel has actually 1 x 1 size, so
    // a pixel at (0,0) covers (-0.5, -0.5) - (0.5, 0.5).

#define CALC_X(x, y) (matrix.a * (x) + matrix.c * (y) + matrix.tx)
#define CALC_Y(x, y) (matrix.b * (x) + matrix.d * (y) + matrix.ty)

    // - upper-left
    points[0].x = CALC_X(-0.5, -0.5);
    points[0].y = CALC_Y(-0.5, -0.5);

    // - upper-right
    points[1].x = CALC_X(rp - 0.5, -0.5);
    points[1].y = CALC_Y(rp - 0.5, -0.5);

    /*	// - bottom-right
            points[2].x = CALC_X(rp - 0.5, bp - 0.5);
            points[2].y = CALC_Y(rp - 0.5, bp - 0.5);
    */

    // - bottom-left
    points[2].x = CALC_X(-0.5, bp - 0.5);
    points[2].y = CALC_Y(-0.5, bp - 0.5);

    return AffineBlt(destrect, ref, refrect, points, method, opa, updaterect, hda, type, clear,
                     clearcolor);
}
//---------------------------------------------------------------------------
bool iTVPBaseBitmap::InternalDoBoxBlur(tTVPRect rect, tTVPRect area, bool hasalpha)
{
    BOUND_CHECK(false);

    if (area.right < area.left)
        std::swap(area.right, area.left);
    if (area.bottom < area.top)
        std::swap(area.bottom, area.top);

    if (area.left == 0 && area.right == 0 && area.top == 0 && area.bottom == 0)
        return false; // no conversion occurs

    if (area.left > 0 || area.right < 0 || area.top > 0 || area.bottom < 0)
        TVPThrowExceptionMessage(TVPBoxBlurAreaMustContainCenterPixel);

    iTVPRenderMethod* method;
    int idL, idT, idR, idB;
    if (hasalpha)
    {
        static iTVPRenderMethod* _method = TVPGetRenderManager()->GetRenderMethod("BoxBlurAlpha");
        static int _idL = _method->EnumParameterID("area_left"),
                   _idT = _method->EnumParameterID("area_top"),
                   _idR = _method->EnumParameterID("area_right"),
                   _idB = _method->EnumParameterID("area_bottom");
        idL = _idL, idT = _idT, idR = _idR, idB = _idB;
        method = _method;
    }
    else
    {
        static iTVPRenderMethod* _method = TVPGetRenderManager()->GetRenderMethod("BoxBlur");
        static int _idL = _method->EnumParameterID("area_left"),
                   _idT = _method->EnumParameterID("area_top"),
                   _idR = _method->EnumParameterID("area_right"),
                   _idB = _method->EnumParameterID("area_bottom");
        idL = _idL, idT = _idT, idR = _idR, idB = _idB;
        method = _method;
    }
    method->SetParameterInt(idL, area.left);
    method->SetParameterInt(idT, area.top);
    method->SetParameterInt(idR, area.right);
    method->SetParameterInt(idB, area.bottom);

    iTVPTexture2D* reftex = GetTexture();
    tRenderTexRectArray::Element src_tex[] = {tRenderTexRectArray::Element(reftex, rect)};
    TVPGetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rect),
                                       reftex, rect, tRenderTexRectArray(src_tex));
    return true;
}
//---------------------------------------------------------------------------
bool iTVPBaseBitmap::DoBoxBlur(const tTVPRect& rect, const tTVPRect& area)
{
    // Blur the bitmap with box-blur algorithm.
    // 'rect' is a rectangle to blur.
    // 'area' is an area which destination pixel refers.
    // right and bottom of 'area' *does contain* pixels in the boundary.
    // eg. area:(-1,-1,1,1)  : Blur is to be performed using average of 3x3
    //                          pixels around the destination pixel.
    //     area:(-10,0,10,0) : Blur is to be performed using average of 21x1
    //                          pixels around the destination pixel. This results
    //                          horizontal blur.

    return InternalDoBoxBlur(rect, area, false);
}
//---------------------------------------------------------------------------
bool iTVPBaseBitmap::DoBoxBlurForAlpha(const tTVPRect& rect, const tTVPRect& area)
{
    return InternalDoBoxBlur(rect, area, true);
}
//---------------------------------------------------------------------------
void tTVPBaseBitmap::UDFlip(const tTVPRect& rect)
{
    // up-down flip for given rectangle

    if (rect.left < 0 || rect.top < 0 || rect.right > (tjs_int)GetWidth() ||
        rect.bottom > (tjs_int)GetHeight())
        TVPThrowExceptionMessage(TVPSrcRectOutOfBitmap);

    tjs_int h = (rect.bottom - rect.top) / 2;
    tjs_int w = rect.right - rect.left;
    tjs_int pitch = GetPitchBytes();
    tjs_uint8* l1 = (tjs_uint8*)GetScanLineForWrite(rect.top);
    tjs_uint8* l2 = (tjs_uint8*)GetScanLineForWrite(rect.bottom - 1);

    if (Is32BPP())
    {
        // 32bpp
        l1 += rect.left * sizeof(tjs_uint32);
        l2 += rect.left * sizeof(tjs_uint32);
        while (h--)
        {
            TVPSwapLine32((tjs_uint32*)l1, (tjs_uint32*)l2, w);
            l1 += pitch;
            l2 -= pitch;
        }
    }
    else
    {
        // 8bpp
        l1 += rect.left;
        l2 += rect.left;
        while (h--)
        {
            TVPSwapLine8(l1, l2, w);
            l1 += pitch;
            l2 -= pitch;
        }
    }
}

void iTVPBaseBitmap::UDFlip(const tTVPRect& rect)
{
    // up-down flip for given rectangle

    if (rect.left < 0 || rect.top < 0 || rect.right > (tjs_int)GetWidth() ||
        rect.bottom > (tjs_int)GetHeight())
        TVPThrowExceptionMessage(TVPSrcRectOutOfBitmap);

    iTVPTexture2D* reftex = GetTexture();
    tRenderTexRectArray::Element src_tex[] = {tRenderTexRectArray::Element(
        reftex, tTVPRect(rect.left, rect.bottom, rect.right, rect.top))};
    static iTVPRenderMethod* method = GetRenderManager()->GetRenderMethod("Copy");
    TVPGetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rect),
                                       reftex, rect, tRenderTexRectArray(src_tex));
}
//---------------------------------------------------------------------------
void tTVPBaseBitmap::LRFlip(const tTVPRect& rect)
{
    // left-right flip
    if (rect.left < 0 || rect.top < 0 || rect.right > (tjs_int)GetWidth() ||
        rect.bottom > (tjs_int)GetHeight())
        TVPThrowExceptionMessage(TVPSrcRectOutOfBitmap);

    tjs_int h = rect.bottom - rect.top;
    tjs_int w = rect.right - rect.left;

    tjs_int pitch = GetPitchBytes();
    tjs_uint8* line = (tjs_uint8*)GetScanLineForWrite(rect.top);

    if (Is32BPP())
    {
        // 32bpp
        line += rect.left * sizeof(tjs_uint32);
        while (h--)
        {
            TVPReverse32((tjs_uint32*)line, w);
            line += pitch;
        }
    }
    else
    {
        // 8bpp
        line += rect.left;
        while (h--)
        {
            TVPReverse8(line, w);
            line += pitch;
        }
    }
}

void iTVPBaseBitmap::LRFlip(const tTVPRect& rect)
{
    // left-right flip
    if (rect.left < 0 || rect.top < 0 || rect.right > (tjs_int)GetWidth() ||
        rect.bottom > (tjs_int)GetHeight())
        TVPThrowExceptionMessage(TVPSrcRectOutOfBitmap);

    iTVPTexture2D* reftex = GetTexture();
    tRenderTexRectArray::Element src_tex[] = {tRenderTexRectArray::Element(
        reftex, tTVPRect(rect.right, rect.top, rect.left, rect.bottom))};
    static iTVPRenderMethod* method = GetRenderManager()->GetRenderMethod("Copy");
    TVPGetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rect),
                                       reftex, rect, tRenderTexRectArray(src_tex));
}
//---------------------------------------------------------------------------
void iTVPBaseBitmap::DoGrayScale(tTVPRect rect)
{
    if (!Is32BPP())
        return; // 8bpp is always grayscaled bitmap

    BOUND_CHECK(RET_VOID);
    static iTVPRenderMethod* method = TVPGetRenderManager()->GetRenderMethod("DoGrayScale");
    iTVPTexture2D* reftex = GetTexture();
    TVPGetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rect),
                                       reftex, rect, tRenderTexRectArray());
}
//---------------------------------------------------------------------------
void iTVPBaseBitmap::AdjustGamma(tTVPRect rect, const tTVPGLGammaAdjustData& data)
{
    if (!Is32BPP())
        TVPThrowExceptionMessage(TVPInvalidOperationFor8BPP);

    BOUND_CHECK(RET_VOID);

    if (!memcmp(&data, &TVPIntactGammaAdjustData, sizeof(tTVPGLGammaAdjustData)))
        return;
    static iTVPRenderMethod* method = TVPGetRenderManager()->GetRenderMethod("AdjustGamma");
    static int data_id = method->EnumParameterID("gammaAdjustData");
    method->SetParameterPtr(data_id, &data);
    iTVPTexture2D* reftex = GetTexture();
    TVPGetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rect),
                                       reftex, rect, tRenderTexRectArray());
}
//---------------------------------------------------------------------------
void iTVPBaseBitmap::AdjustGammaForAdditiveAlpha(tTVPRect rect, const tTVPGLGammaAdjustData& data)
{
    if (!Is32BPP())
        TVPThrowExceptionMessage(TVPInvalidOperationFor8BPP);

    BOUND_CHECK(RET_VOID);

    if (!memcmp(&data, &TVPIntactGammaAdjustData, sizeof(tTVPGLGammaAdjustData)))
        return;
    static iTVPRenderMethod* method = TVPGetRenderManager()->GetRenderMethod("AdjustGamma_a");
    static int data_id = method->EnumParameterID("gammaAdjustData");
    method->SetParameterPtr(data_id, &data);
    iTVPTexture2D* reftex = GetTexture();
    TVPGetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rect),
                                       reftex, rect, tRenderTexRectArray());
}
//---------------------------------------------------------------------------
void iTVPBaseBitmap::ConvertAddAlphaToAlpha()
{
    // convert additive alpha representation to alpha representation

    if (!Is32BPP())
        TVPThrowExceptionMessage(TVPInvalidOperationFor8BPP);

    tjs_int w = GetWidth();
    tjs_int h = GetHeight();
    static iTVPRenderMethod* method =
        TVPGetRenderManager()->GetRenderMethod("AdditiveAlphaToAlpha");
    tTVPRect rc(0, 0, w, h);
    iTVPTexture2D* reftex = GetTexture();
    TVPGetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rc),
                                       reftex, rc, tRenderTexRectArray());
}
//---------------------------------------------------------------------------
void iTVPBaseBitmap::ConvertAlphaToAddAlpha()
{
    // convert additive alpha representation to alpha representation

    if (!Is32BPP())
        TVPThrowExceptionMessage(TVPInvalidOperationFor8BPP);

    tjs_int w = GetWidth();
    tjs_int h = GetHeight();
    static iTVPRenderMethod* method =
        TVPGetRenderManager()->GetRenderMethod("AlphaToAdditiveAlpha");
    tTVPRect rc(0, 0, w, h);
    iTVPTexture2D* reftex = GetTexture();
    TVPGetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &rc),
                                       reftex, rc, tRenderTexRectArray());
}
//---------------------------------------------------------------------------

tTVPBaseBitmap::tTVPBaseBitmap(tjs_uint w, tjs_uint h, tjs_uint bpp /*= 32*/)
{
    if (!w)
        w = 1;
    if (!h)
        h = 1;
    Bitmap = TVPGetSoftwareRenderManager()->CreateTexture2D(
        nullptr, 0, w, h, bpp == 8 ? TVPTextureFormat::Gray : TVPTextureFormat::RGBA);
}

bool tTVPBaseBitmap::AssignBitmap(tTVPBitmap* bmp)
{
    return AssignTexture(TVPGetSoftwareRenderManager()->CreateTexture2D(bmp));
}

iTVPRenderManager* tTVPBaseBitmap::GetRenderManager()
{
    return TVPGetSoftwareRenderManager();
}

//---------------------------------------------------------------------------
tTVPBaseTexture::tTVPBaseTexture(tjs_uint w, tjs_uint h, tjs_uint bpp /*= 32*/)
{
    if (!w)
        w = 1;
    if (!h)
        h = 1;
    Bitmap = TVPGetRenderManager()->CreateTexture2D(
        nullptr, 0, w, h, bpp == 8 ? TVPTextureFormat::Gray : TVPTextureFormat::RGBA);
}

bool tTVPBaseTexture::AssignBitmap(tTVPBitmap* bmp)
{
    assert(false);
    return false;
}

iTVPRenderManager* tTVPBaseTexture::GetRenderManager()
{
    return TVPGetRenderManager();
}

void tTVPBaseTexture::Update(const void* pixel, unsigned int pitch, int x, int y, int w, int h)
{
    GetTextureForRender(false, nullptr)
        ->Update(pixel, TVPTextureFormat::RGBA, pitch, tTVPRect(x, y, x + w, y + h));
}

//---------------------------------------------------------------------------
// prototypes
//---------------------------------------------------------------------------
void TVPClearFontCache();
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// default FONT retrieve function
//---------------------------------------------------------------------------
static tjs_int TVPGlobalFontStateMagic = 0;
// this is for checking global font status' change

enum
{
    FONT_RASTER_FREE_TYPE,
    //	FONT_RASTER_GDI,
    FONT_RASTER_EOT
};
static FontRasterizer* TVPFontRasterizers[FONT_RASTER_EOT];
static bool TVPFontRasterizersInit = false;
static tjs_int TVPCurrentFontRasterizers = FONT_RASTER_FREE_TYPE;
// static tjs_int TVPCurrentFontRasterizers = FONT_RASTER_GDI;
extern void TVPUninitializeFreeFont();
void TVPInializeFontRasterizers()
{
    if (TVPFontRasterizersInit == false)
    {
        TVPFontRasterizers[FONT_RASTER_FREE_TYPE] = new FreeTypeFontRasterizer();
        //		TVPFontRasterizers[FONT_RASTER_GDI] = new GDIFontRasterizer();

        TVPCreateDefaultFont();
        TVPFontRasterizersInit = true;
    }
}
void TVPUninitializeFontRasterizers()
{
    for (tjs_int i = 0; i < FONT_RASTER_EOT; i++)
    {
        if (TVPFontRasterizers[i])
        {
            TVPFontRasterizers[i]->Release();
            TVPFontRasterizers[i] = NULL;
        }
    }

    TVPUninitializeFreeFont();
}
static tTVPAtExit TVPUninitializeFontRaster(TVP_ATEXIT_PRI_RELEASE, TVPUninitializeFontRasterizers);

void TVPSetFontRasterizer(tjs_int index)
{
    if (TVPCurrentFontRasterizers != index && index >= 0 && index < FONT_RASTER_EOT)
    {
        TVPCurrentFontRasterizers = index;
        TVPClearFontCache();       // ???
        TVPGlobalFontStateMagic++; // ApplyFont ???
    }
}
tjs_int TVPGetFontRasterizer()
{
    return TVPCurrentFontRasterizers;
}
FontRasterizer* GetCurrentRasterizer()
{
    return TVPFontRasterizers[TVPCurrentFontRasterizers];
}

//---------------------------------------------------------------------------
#define TVP_CH_MAX_CACHE_COUNT 1300
#define TVP_CH_MAX_CACHE_COUNT_LOW 100
#define TVP_CH_MAX_CACHE_HASH_SIZE 512
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Pre-rendered font management
//---------------------------------------------------------------------------
tTJSHashTable<ttstr, tTVPPrerenderedFont*> TVPPrerenderedFonts;

//---------------------------------------------------------------------------
// tTVPPrerenderedFontMap
//---------------------------------------------------------------------------
struct tTVPPrerenderedFontMap
{
    tTVPFont Font;               // mapped font
    tTVPPrerenderedFont* Object; // prerendered font object
};
static std::vector<tTVPPrerenderedFontMap> TVPPrerenderedFontMapVector;
//---------------------------------------------------------------------------
void TVPMapPrerenderedFont(const tTVPFont& font, const ttstr& storage)
{
    // map specified font to specified prerendered font
    ttstr fn = TVPSearchPlacedPath(storage);

    // search or retrieve specified storage
    tTVPPrerenderedFont* object;

    tTVPPrerenderedFont** found = TVPPrerenderedFonts.Find(fn);
    if (!found)
    {
        // not yet exist; create
        object = new tTVPPrerenderedFont(fn);
    }
    else
    {
        // already exist
        object = *found;
        object->AddRef();
    }

    // search existing mapped font
    std::vector<tTVPPrerenderedFontMap>::iterator i;
    for (i = TVPPrerenderedFontMapVector.begin(); i != TVPPrerenderedFontMapVector.end(); i++)
    {
        if (i->Font == font)
        {
            // found font
            // replace existing
            i->Object->Release();
            i->Object = object;
            break;
        }
    }
    if (i == TVPPrerenderedFontMapVector.end())
    {
        // not found
        tTVPPrerenderedFontMap map;
        map.Font = font;
        map.Object = object;
        TVPPrerenderedFontMapVector.push_back(map); // add
    }

    TVPGlobalFontStateMagic++; // increase magic number

    TVPClearFontCache(); // clear font cache
}
//---------------------------------------------------------------------------
void TVPUnmapPrerenderedFont(const tTVPFont& font)
{
    // unmap specified font
    std::vector<tTVPPrerenderedFontMap>::iterator i;
    for (i = TVPPrerenderedFontMapVector.begin(); i != TVPPrerenderedFontMapVector.end(); i++)
    {
        if (i->Font == font)
        {
            // found font
            // replace existing
            i->Object->Release();
            TVPPrerenderedFontMapVector.erase(i);
            TVPGlobalFontStateMagic++; // increase magic number
            TVPClearFontCache();
            return;
        }
    }
}
//---------------------------------------------------------------------------
static void TVPUnmapAllPrerenderedFonts()
{
    // unmap all prerendered fonts
    std::vector<tTVPPrerenderedFontMap>::iterator i;
    for (i = TVPPrerenderedFontMapVector.begin(); i != TVPPrerenderedFontMapVector.end(); i++)
    {
        i->Object->Release();
    }
    TVPPrerenderedFontMapVector.clear();
    TVPGlobalFontStateMagic++; // increase magic number
}
//---------------------------------------------------------------------------
static tTVPAtExit TVPUnmapAllPrerenderedFontsAtExit(TVP_ATEXIT_PRI_PREPARE,
                                                    TVPUnmapAllPrerenderedFonts);
//---------------------------------------------------------------------------
static tTVPPrerenderedFont* TVPGetPrerenderedMappedFont(const tTVPFont& font)
{
    // search mapped prerendered font
    std::vector<tTVPPrerenderedFontMap>::iterator i;
    for (i = TVPPrerenderedFontMapVector.begin(); i != TVPPrerenderedFontMapVector.end(); i++)
    {
        if (i->Font == font)
        {
            // found font
            // replace existing
            i->Object->AddRef();

            // note that the object is AddRefed
            return i->Object;
        }
    }
    return NULL;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
typedef tTJSRefHolder<tTVPCharacterData> tTVPCharacterDataHolder;

typedef tTJSHashCache<tTVPFontAndCharacterData,
                      tTVPCharacterDataHolder,
                      tTVPFontHashFunc,
                      TVP_CH_MAX_CACHE_HASH_SIZE>
    tTVPFontCache;
tTVPFontCache TVPFontCache(TVP_CH_MAX_CACHE_COUNT);
//---------------------------------------------------------------------------
void TVPSetFontCacheForLowMem()
{
    // set character cache limit
    TVPFontCache.SetMaxCount(TVP_CH_MAX_CACHE_COUNT_LOW);
}
//---------------------------------------------------------------------------
void TVPClearFontCache()
{
    TVPFontCache.Clear();
}
//---------------------------------------------------------------------------
struct tTVPClearFontCacheCallback : public tTVPCompactEventCallbackIntf
{
    virtual void OnCompact(tjs_int level)
    {
        if (level >= TVP_COMPACT_LEVEL_MINIMIZE)
        {
            // clear the font cache on application minimize
            TVPClearFontCache();
        }
    }
} static TVPClearFontCacheCallback;
static bool TVPClearFontCacheCallbackInit = false;
//---------------------------------------------------------------------------
static tTVPCharacterData* TVPGetCharacter(const tTVPFontAndCharacterData& font,
                                          tTVPNativeBaseBitmap* bmp,
                                          tTVPPrerenderedFont* pfont,
                                          tjs_int aofsx,
                                          tjs_int aofsy)
{
    // returns specified character data.
    // draw a character if needed.

    // compact interface initialization
    if (!TVPClearFontCacheCallbackInit)
    {
        TVPAddCompactEventHook(&TVPClearFontCacheCallback);
        TVPClearFontCacheCallbackInit = true;
    }

    // make hash and search over cache
    tjs_uint32 hash = tTVPFontCache::MakeHash(font);

    tTVPCharacterDataHolder* ptr = TVPFontCache.FindAndTouchWithHash(font, hash);
    if (ptr)
    {
        // found in the cache
        return ptr->GetObject();
    }

    // not found in the cache

    // look prerendered font
    const tTVPPrerenderedCharacterItem* pitem = NULL;
    if (pfont)
        pitem = pfont->Find(font.Character);

    if (pitem)
    {
        // prerendered font
        tTVPCharacterData* data = new tTVPCharacterData();
        data->BlackBoxX = pitem->Width;
        data->BlackBoxY = pitem->Height;
        data->Metrics.CellIncX = pitem->IncX;
        data->Metrics.CellIncY = pitem->IncY;
        data->OriginX = pitem->OriginX + aofsx;
        data->OriginY = -pitem->OriginY + aofsy;

        data->Antialiased = font.Antialiased;

        data->FullColored = false;

        data->Blured = font.Blured;
        data->BlurWidth = font.BlurWidth;
        data->BlurLevel = font.BlurLevel;

        try
        {
            if (data->BlackBoxX && data->BlackBoxY)
            {
                // render
                tjs_int newpitch = (((pitem->Width - 1) >> 2) + 1) << 2;
                data->Pitch = newpitch;

                data->Alloc(newpitch * data->BlackBoxY);

                pfont->Retrieve(pitem, data->GetData(), newpitch);
                data->Gray = 256;
                // apply blur
                if (font.Blured)
                    data->Blur(); // nasty ...

                // add to hash table
                tTVPCharacterDataHolder holder(data);
                TVPFontCache.AddWithHash(font, hash, holder);
            }
        }
        catch (...)
        {
            data->Release();
            throw;
        }

        return data;
    }
    else
    {
        // render font
        tTVPCharacterData* data = GetCurrentRasterizer()->GetBitmap(font, aofsx, aofsy);

        // add to hash table
        tTVPCharacterDataHolder holder(data);
        TVPFontCache.AddWithHash(font, hash, holder);
        return data;
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPBitmap : internal bitmap object
//---------------------------------------------------------------------------
/*
        important:
        Note that each lines must be started at tjs_uint32 ( 4bytes ) aligned address.
        This is the default Windows bitmap allocate behavior.
*/
tTVPBitmap::tTVPBitmap(tjs_uint width, tjs_uint height, tjs_uint bpp)
{
    // tTVPBitmap constructor

    TVPInitWindowOptions(); // ensure window/bitmap usage options are initialized

    RefCount = 1;

    Allocate(width, height, bpp); // allocate initial bitmap
}
//---------------------------------------------------------------------------
tTVPBitmap::tTVPBitmap(tjs_uint width, tjs_uint height, tjs_uint bpp, void* bits)
{
    // tTVPBitmap constructor
    TVPInitWindowOptions(); // ensure window/bitmap usage options are initialized

    RefCount = 1;

    BitmapInfo = new BitmapInfomation(width, height, bpp);
    Width = width;
    Height = height;
    PitchBytes = BitmapInfo->GetPitchBytes();
    PitchStep = PitchBytes;

    // set bitmap bits
    try
    {
        Bits = bits;
        if (bpp == 8)
        {
            Palette = new tjs_uint[DEFAULT_PALETTE_COUNT];
            ActualPalCount = 0;
        }
        else
        {
            Palette = NULL;
            ActualPalCount = 0;
        }
    }
    catch (...)
    {
        delete BitmapInfo;
        BitmapInfo = NULL;
        throw;
    }
}
//---------------------------------------------------------------------------
tTVPBitmap::~tTVPBitmap()
{
    TJS_free(Bits);
    delete BitmapInfo;
    if (Palette)
        delete Palette;
}
//---------------------------------------------------------------------------
tTVPBitmap::tTVPBitmap(const tTVPBitmap& r)
{
    // constructor for cloning bitmap
    TVPInitWindowOptions(); // ensure window/bitmap usage options are initialized

    RefCount = 1;

    // allocate bitmap which has the same metrics to r
    Allocate(r.GetWidth(), r.GetHeight(), r.GetBPP());

    // copy BitmapInfo
    *BitmapInfo = *r.BitmapInfo;

    // copy Bits
    if (r.Bits)
        memcpy(Bits, r.Bits, r.BitmapInfo->GetImageSize());
    if (r.Palette)
    {
        memcpy(Palette, r.Palette, sizeof(tjs_uint) * DEFAULT_PALETTE_COUNT);
        ActualPalCount = r.ActualPalCount;
    }

    // copy pitch
    PitchBytes = r.PitchBytes;
    PitchStep = r.PitchStep;
}
//---------------------------------------------------------------------------
void tTVPBitmap::Allocate(tjs_uint width, tjs_uint height, tjs_uint bpp)
{
    // allocate bitmap bits
    // bpp must be 8 or 32

    // create BITMAPINFO
    BitmapInfo = new BitmapInfomation(width, height, bpp);

    Width = width;
    Height = height;
    PitchBytes = BitmapInfo->GetPitchBytes();
    PitchStep = PitchBytes;

    // allocate bitmap bits
    try
    {
        Bits = TJS_malloc(BitmapInfo->GetImageSize());
        if (bpp == 8)
        {
            Palette = new tjs_uint[DEFAULT_PALETTE_COUNT];
            ActualPalCount = 0;
        }
        else
        {
            Palette = NULL;
            ActualPalCount = 0;
        }
    }
    catch (...)
    {
        delete BitmapInfo;
        BitmapInfo = NULL;
        throw;
    }
}
//---------------------------------------------------------------------------
void* tTVPBitmap::GetScanLine(tjs_uint l) const
{
    if ((tjs_int)l >= BitmapInfo->GetHeight())
    {
        TVPThrowExceptionMessage(TVPScanLineRangeOver, ttstr((tjs_int)l),
                                 ttstr((tjs_int)BitmapInfo->GetHeight() - 1));
    }

    return l * PitchBytes + (tjs_uint8*)Bits;
}
//---------------------------------------------------------------------------
void tTVPBitmap::SetPaletteCount(tjs_uint count)
{
    if (!Is8bit())
        TVPThrowExceptionMessage(TVPInvalidOperationFor32BPP);
    if (count >= DEFAULT_PALETTE_COUNT)
        TVPThrowExceptionMessage(TJSRangeError);

    ActualPalCount = count;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPNativeBaseBitmap
//---------------------------------------------------------------------------
tTVPNativeBaseBitmap::tTVPNativeBaseBitmap(/*tjs_uint w, tjs_uint h, tjs_uint bpp*/)
{
    TVPInializeFontRasterizers();

    Font = TVPGetDefaultFont();
    PrerenderedFont = NULL;
    // LogFont = TVPDefaultLOGFONT;
    FontChanged = true;
    GlobalFontState = -1;
    TextWidth = TextHeight = 0;
    // Bitmap = new tTVPBitmap(w, h, bpp);
}
//---------------------------------------------------------------------------
tTVPNativeBaseBitmap::tTVPNativeBaseBitmap(const tTVPNativeBaseBitmap& r)
{
    TVPInializeFontRasterizers();
    // TVPFontRasterizer->AddRef(); TODO

    Bitmap = r.Bitmap;
    if (Bitmap)
        Bitmap->AddRef();

    Font = r.Font;
    PrerenderedFont = NULL;
    // LogFont = TVPDefaultLOGFONT;
    FontChanged = true;
    TextWidth = TextHeight = 0;
}
//---------------------------------------------------------------------------
tTVPNativeBaseBitmap::~tTVPNativeBaseBitmap()
{
    if (Bitmap)
        Bitmap->Release();
    if (PrerenderedFont)
        PrerenderedFont->Release();

    // TVPFontRasterizer->Release(); TODO
}
//---------------------------------------------------------------------------
tjs_uint tTVPNativeBaseBitmap::GetWidth() const
{
    return Bitmap->GetWidth();
}
//---------------------------------------------------------------------------
void tTVPNativeBaseBitmap::SetWidth(tjs_uint w)
{
    SetSize(w, Bitmap->GetHeight());
}
//---------------------------------------------------------------------------
tjs_uint tTVPNativeBaseBitmap::GetHeight() const
{
    return Bitmap->GetHeight();
}
//---------------------------------------------------------------------------
void tTVPNativeBaseBitmap::SetHeight(tjs_uint h)
{
    SetSize(Bitmap->GetWidth(), h);
}
//---------------------------------------------------------------------------
void tTVPNativeBaseBitmap::SetSize(tjs_uint w, tjs_uint h, bool keepimage)
{
    if (w == 0)
        w = 1;
    if (h == 0)
        h = 1;
    if (Bitmap->GetWidth() != w || Bitmap->GetHeight() != h)
    {
        // create a new bitmap and copy existing bitmap
        iTVPTexture2D* newbitmap;
        if (keepimage)
            newbitmap = GetRenderManager()->CreateTexture2D(w, h, Bitmap);
        else
            newbitmap = GetRenderManager()->CreateTexture2D(nullptr, 0, w, h, Bitmap->GetFormat());

        Bitmap->Release();
        Bitmap = newbitmap;

        FontChanged = true;
    }
}
//---------------------------------------------------------------------------
void tTVPNativeBaseBitmap::SetSizeAndImageBuffer(tTVPBitmap* bmp)
{
    // create a new bitmap and copy existing bitmap
    iTVPTexture2D* newbitmap = GetRenderManager()->CreateTexture2D(bmp);
    Bitmap->Release();
    Bitmap = newbitmap;
    FontChanged = true;
}
//---------------------------------------------------------------------------
tjs_uint tTVPNativeBaseBitmap::GetBPP() const
{
    switch (Bitmap->GetFormat())
    {
        case TVPTextureFormat::Gray:
            return 8;
        case TVPTextureFormat::RGBA:
            return 32;
        case TVPTextureFormat::RGB:
            return 24;
        default:
            // error !
            return 0;
    }
}
//---------------------------------------------------------------------------
bool tTVPNativeBaseBitmap::Is32BPP() const
{
    return Bitmap->GetFormat() != TVPTextureFormat::Gray;
}
//---------------------------------------------------------------------------
bool tTVPNativeBaseBitmap::Is8BPP() const
{
    return Bitmap->GetFormat() == TVPTextureFormat::Gray;
}

bool tTVPNativeBaseBitmap::IsOpaque() const
{
    return Bitmap->IsOpaque();
}

//---------------------------------------------------------------------------
bool tTVPNativeBaseBitmap::Assign(const tTVPNativeBaseBitmap& rhs)
{
    if (this == &rhs || Bitmap == rhs.Bitmap)
        return false;

    Bitmap->Release();
    Bitmap = rhs.Bitmap;
    Bitmap->AddRef();

    Font = rhs.Font;
    FontChanged = true; // informs internal font information is invalidated

    return true; // changed
}
//---------------------------------------------------------------------------
bool tTVPNativeBaseBitmap::AssignBitmap(const tTVPNativeBaseBitmap& rhs)
{
    // assign only bitmap
    if (this == &rhs || Bitmap == rhs.Bitmap)
        return false;

    Bitmap->Release();
    Bitmap = rhs.Bitmap;
    Bitmap->AddRef();

    // font information are not copyed
    FontChanged = true; // informs internal font information is invalidated

    return true;
}
bool tTVPNativeBaseBitmap::AssignTexture(iTVPTexture2D* tex)
{
    if (Bitmap == tex)
        return false;

    Bitmap->Release();
    Bitmap = tex; // CreateTexture2D(bmp);
    Bitmap->AddRef();

    // font information are not copyed
    FontChanged = true; // informs internal font information is invalidated

    return true;
}
//---------------------------------------------------------------------------
const void* tTVPNativeBaseBitmap::GetScanLine(tjs_uint l) const
{
    return Bitmap->GetScanLineForRead(l);
}
//---------------------------------------------------------------------------
void* tTVPNativeBaseBitmap::GetScanLineForWrite(tjs_uint l)
{
    Independ();
    return Bitmap->GetScanLineForWrite(l);
}
//---------------------------------------------------------------------------
tjs_int tTVPNativeBaseBitmap::GetPitchBytes() const
{
    if (Bitmap)
        return Bitmap->GetPitch();
    return 0;
}
//---------------------------------------------------------------------------
void tTVPNativeBaseBitmap::Independ()
{
    // sever Bitmap's image sharing
    if (Bitmap->IsIndependent() && !Bitmap->IsStatic())
        return;
    iTVPTexture2D* newb =
        GetRenderManager()->CreateTexture2D(Bitmap->GetWidth(), Bitmap->GetHeight(), Bitmap);
    Bitmap->Release();
    Bitmap = newb;
    FontChanged = true; // informs internal font information is invalidated
}
//---------------------------------------------------------------------------
void tTVPNativeBaseBitmap::IndependNoCopy()
{
    // indepent the bitmap, but not to copy the original bitmap
    if (!Bitmap->IsStatic() && Bitmap->IsIndependent())
        return;
    Recreate();
}
//---------------------------------------------------------------------------
void tTVPNativeBaseBitmap::Recreate()
{
    Recreate(Bitmap->GetWidth(), Bitmap->GetHeight(),
             Bitmap->GetFormat() == TVPTextureFormat::Gray ? 8 : 32);
}
//---------------------------------------------------------------------------
void tTVPNativeBaseBitmap::Recreate(tjs_uint w, tjs_uint h, tjs_uint bpp)
{
    Bitmap->Release();
    Bitmap = GetRenderManager()->CreateTexture2D(
        nullptr, 0, w, h, bpp == 8 ? TVPTextureFormat::Gray : TVPTextureFormat::RGBA);
    FontChanged = true; // informs internal font information is invalidated
}

bool tTVPNativeBaseBitmap::IsIndependent() const
{
    return Bitmap->IsIndependent() && !Bitmap->IsStatic();
}

//---------------------------------------------------------------------------
void tTVPNativeBaseBitmap::ApplyFont()
{
    // apply font
    if (FontChanged || GlobalFontState != TVPGlobalFontStateMagic)
    {
        Independ();

        FontChanged = false;
        GlobalFontState = TVPGlobalFontStateMagic;
        CachedText.Clear();
        TextWidth = TextHeight = 0;

        if (PrerenderedFont)
            PrerenderedFont->Release();
        PrerenderedFont = TVPGetPrerenderedMappedFont(Font);

        // compute ascent offset
        GetCurrentRasterizer()->ApplyFont(this, true);
        tjs_int ascent = GetCurrentRasterizer()->GetAscentHeight();
        RadianAngle = Font.Angle * (M_PI / 1800);
        double angle90 = RadianAngle + M_PI_2;
        AscentOfsX = static_cast<tjs_int>(-cos(angle90) * ascent);
        AscentOfsY = static_cast<tjs_int>(sin(angle90) * ascent);

        // compute font hash
        FontHash = tTJSHashFunc<ttstr>::Make(Font.Face);
        FontHash ^= Font.Height ^ Font.Flags ^ Font.Angle;
    }
    else
    {
        GetCurrentRasterizer()->ApplyFont(this, false);
    }
}
//---------------------------------------------------------------------------
void tTVPNativeBaseBitmap::SetFont(const tTVPFont& font)
{
    Font = font;
    FontChanged = true;
}
//---------------------------------------------------------------------------
void tTVPNativeBaseBitmap::GetFontList(tjs_uint32 flags, std::vector<ttstr>& list)
{
    ApplyFont();
    std::vector<ttstr> ansilist;
    TVPGetAllFontList(ansilist);
    for (std::vector<ttstr>::iterator i = ansilist.begin(); i != ansilist.end(); i++)
        list.push_back(*i);
}
//---------------------------------------------------------------------------
void tTVPNativeBaseBitmap::MapPrerenderedFont(const ttstr& storage)
{
    ApplyFont();
    TVPMapPrerenderedFont(Font, storage);
    FontChanged = true;
}
//---------------------------------------------------------------------------
void tTVPNativeBaseBitmap::UnmapPrerenderedFont()
{
    ApplyFont();
    TVPUnmapPrerenderedFont(Font);
    FontChanged = true;
}
//---------------------------------------------------------------------------
struct tTVPDrawTextData
{
    tTVPRect rect;
    tjs_int bmppitch;
    tjs_int opa;
    bool holdalpha;
    tTVPBBBltMethod bltmode;
};

static iTVPTexture2D *_CharacterTexture = nullptr, *_CharacterTextureRGBA = nullptr;

bool tTVPNativeBaseBitmap::InternalBlendText(tTVPCharacterData* data,
                                             tTVPDrawTextData* dtdata,
                                             tjs_uint32 color,
                                             const tTVPRect& srect,
                                             tTVPRect& drect)
{
    // blend to the bitmap
    tjs_int pitch = data->Pitch;
    // tjs_uint8 *sl = (tjs_uint8*)GetScanLineForWrite(drect.top);
    tjs_int h = drect.bottom - drect.top;
    tjs_int w = drect.right - drect.left;
    tjs_uint8* bp = data->GetData() + pitch * srect.top;

    iTVPRenderMethod* method = nullptr;
    int opa_id, clr_id;
#define GEMTHOD_OPA_CLR(n) \
    static iTVPRenderMethod* _method = TVPGetRenderManager()->GetRenderMethod(#n); \
    static int _opa_id = _method->EnumParameterID("opacity"); \
    static int _clr_id = _method->EnumParameterID("color"); \
    method = _method; \
    opa_id = _opa_id; \
    clr_id = _clr_id;

    static bool fastGPURoute = !TVPIsSoftwareRenderManager() && !GameSetting::ogl_accurate_render;

    iTVPTexture2D* pTexSrc;
    if (fastGPURoute && dtdata->bltmode == bmAlphaOnAlpha && dtdata->opa > 0)
    {
        // convert to addalpha bitmap
        tTVPBitmap* tmp = new tTVPBitmap(w, h, 32);
        tjs_int spitch = pitch;
        tjs_int dpitch = tmp->GetPitch();
        tjs_uint8* src = bp;
        tjs_uint8* dst = (tjs_uint8*)tmp->GetBits();
        for (tjs_int y = 0; y < h; ++y)
        {
            for (tjs_int x = 0; x < w; ++x)
            {
                ((tjs_uint32*)dst)[x] = (color & 0xFFFFFF) | (src[x] << 24);
            }
            TVPConvertAlphaToAdditiveAlpha((tjs_uint32*)dst, w);
            dst += dpitch;
            src += spitch;
        }
        if (_CharacterTextureRGBA)
        {
            if (_CharacterTextureRGBA->GetFormat() != TVPTextureFormat::RGBA)
            {
                _CharacterTextureRGBA->Release();
                _CharacterTextureRGBA = nullptr;
            }
        }
        if (!_CharacterTextureRGBA)
        {
            _CharacterTextureRGBA = GetRenderManager()->CreateTexture2D(
                tmp->GetBits(), dpitch, w, h, TVPTextureFormat::RGBA,
                RENDER_CREATE_TEXTURE_FLAG_NO_COMPRESS);
        }
        else if (_CharacterTextureRGBA->GetInternalWidth() < w ||
                 _CharacterTextureRGBA->GetInternalHeight() < h)
        {
            _CharacterTextureRGBA->Release();
            _CharacterTextureRGBA = GetRenderManager()->CreateTexture2D(
                tmp->GetBits(), dpitch, w, h, TVPTextureFormat::RGBA,
                RENDER_CREATE_TEXTURE_FLAG_NO_COMPRESS);
        }
        else
        {
            _CharacterTextureRGBA->Update(tmp->GetBits(), TVPTextureFormat::RGBA, dpitch,
                                          tTVPRect(0, 0, w, h));
        }

        tmp->Release();

        GEMTHOD_OPA_CLR(AlphaBlend_a);
        method->SetParameterOpa(opa_id, dtdata->opa);
        pTexSrc = _CharacterTextureRGBA;
    }
    else
    {
        if (dtdata->bltmode == bmAlphaOnAlpha)
        {
            if (dtdata->opa > 0)
            {
                GEMTHOD_OPA_CLR(ApplyColorMap_d);
            }
            else
            {
                // opacity removal
                GEMTHOD_OPA_CLR(RemoveOpacity);
            }
        }
        else if (dtdata->bltmode == bmAlphaOnAddAlpha)
        {
            GEMTHOD_OPA_CLR(ApplyColorMap_a);
        }
        else
        {
            GEMTHOD_OPA_CLR(ApplyColorMap);
        }

        // blend to the texture
        if (!_CharacterTexture)
        {
            _CharacterTexture =
                GetRenderManager()->CreateTexture2D(nullptr, pitch, w, h, TVPTextureFormat::Gray);
        }
        else if (_CharacterTexture->GetInternalWidth() < w ||
                 _CharacterTexture->GetInternalHeight() < h)
        {
            _CharacterTexture->Release();
            _CharacterTexture =
                GetRenderManager()->CreateTexture2D(nullptr, pitch, w, h, TVPTextureFormat::Gray);
        }
        _CharacterTexture->Update(bp, TVPTextureFormat::Gray, pitch, tTVPRect(0, 0, w, h));

        method->SetParameterOpa(opa_id, dtdata->opa);
        method->SetParameterColor4B(clr_id, color);

        pTexSrc = _CharacterTexture;
    }

    tRenderTexRectArray::Element src_tex[] = {
        tRenderTexRectArray::Element(pTexSrc, tTVPRect(0, 0, w, h))};
    TVPGetRenderManager()->OperateRect(method, GetTextureForRender(method->IsBlendTarget(), &drect),
                                       nullptr, drect, tRenderTexRectArray(src_tex));
    return true;
}

bool tTVPNativeBaseBitmap::InternalDrawText(tTVPCharacterData* data,
                                            tjs_int x,
                                            tjs_int y,
                                            tjs_uint32 color,
                                            tTVPDrawTextData* dtdata,
                                            tTVPRect& drect)
{
    // setup destination and source rectangle
    drect.left = x + data->OriginX;
    drect.top = y + data->OriginY;
    drect.right = drect.left + data->BlackBoxX;
    drect.bottom = drect.top + data->BlackBoxY;

    tTVPRect srect;
    srect.left = srect.top = 0;
    srect.right = data->BlackBoxX;
    srect.bottom = data->BlackBoxY;

    // check boundary
    if (drect.left < dtdata->rect.left)
    {
        srect.left += (dtdata->rect.left - drect.left);
        drect.left = dtdata->rect.left;
    }

    if (drect.right > dtdata->rect.right)
    {
        srect.right -= (drect.right - dtdata->rect.right);
        drect.right = dtdata->rect.right;
    }

    if (srect.left >= srect.right)
        return false; // not drawable

    if (drect.top < dtdata->rect.top)
    {
        srect.top += (dtdata->rect.top - drect.top);
        drect.top = dtdata->rect.top;
    }

    if (drect.bottom > dtdata->rect.bottom)
    {
        srect.bottom -= (drect.bottom - dtdata->rect.bottom);
        drect.bottom = dtdata->rect.bottom;
    }

    if (srect.top >= srect.bottom)
        return false; // not drawable

    return InternalBlendText(data, dtdata, color, srect, drect);
}
//---------------------------------------------------------------------------
void tTVPNativeBaseBitmap::DrawGlyph(iTJSDispatch2* glyph,
                                     const tTVPRect& destrect,
                                     tjs_int x,
                                     tjs_int y,
                                     tjs_uint32 color,
                                     tTVPBBBltMethod bltmode,
                                     tjs_int opa,
                                     bool holdalpha,
                                     bool aa,
                                     tjs_int shlevel,
                                     tjs_uint32 shadowcolor,
                                     tjs_int shwidth,
                                     tjs_int shofsx,
                                     tjs_int shofsy,
                                     tTVPComplexRect* updaterects)
{
    if (!Is32BPP())
        TVPThrowExceptionMessage(TVPInvalidOperationFor8BPP);

    if (bltmode == bmAlphaOnAlpha)
    {
        if (opa < -255)
            opa = -255;
        if (opa > 255)
            opa = 255;
    }
    else
    {
        if (opa < 0)
            opa = 0;
        if (opa > 255)
            opa = 255;
    }

    if (opa == 0)
        return; // nothing to do

    tjs_int itemcount;
    tTJSVariant tmp;
    if (TJS_SUCCEEDED(glyph->PropGet(TJS_MEMBERMUSTEXIST, TJS_N("count"), 0, &tmp, glyph)))
        itemcount = tmp;
    else
        itemcount = 0;

    if (itemcount < 8)
        TVPThrowExceptionMessage(TVPFaildGlyphForDrawGlyph);

    enum
    {
        GLYPH_WIDTH,
        GLYPH_HEIGHT,
        GLYPH_ORIGINX,
        GLYPH_ORIGINY,
        GLYPH_INCX,
        GLYPH_INCY,
        GLYPH_INC,
        GLYPH_BITMAP,
        GLYPH_COLORS,
        GLYPH_EOT
    };
    tjs_int glyphitem[7];
    for (tjs_int i = 0; i < 7; i++)
    {
        if (TJS_FAILED(glyph->PropGetByNum(TJS_MEMBERMUSTEXIST, i, &tmp, glyph)))
            TVPThrowExceptionMessage(TVPFaildGlyphForDrawGlyph);
        glyphitem[i] = tmp;
    }

    if (TJS_FAILED(glyph->PropGetByNum(TJS_MEMBERMUSTEXIST, GLYPH_BITMAP, &tmp, glyph)))
        TVPThrowExceptionMessage(TVPFaildGlyphForDrawGlyph);

    tjs_int numcolor = 256;
    if (itemcount >= 9)
    {
        if (TJS_FAILED(glyph->PropGetByNum(TJS_MEMBERMUSTEXIST, GLYPH_COLORS, &tmp, glyph)))
            TVPThrowExceptionMessage(TVPFaildGlyphForDrawGlyph);
        numcolor = tmp;
    }
    tTJSVariantOctet* o = tmp.AsOctetNoAddRef();

    Independ();
    ApplyFont();

    tTVPDrawTextData dtdata;
    dtdata.rect = destrect;
    dtdata.bmppitch = GetPitchBytes();
    dtdata.bltmode = bltmode;
    dtdata.opa = opa;
    dtdata.holdalpha = holdalpha;

    tTVPCharacterData* data = NULL;
    tTVPCharacterData* shadow = NULL;
    try
    {
        tGlyphMetrics metrics;
        metrics.CellIncX = glyphitem[GLYPH_INCX];
        metrics.CellIncY = glyphitem[GLYPH_INCY];
        data = new tTVPCharacterData(o->GetData(), glyphitem[GLYPH_WIDTH],
                                     glyphitem[GLYPH_ORIGINX] + AscentOfsX,
                                     -glyphitem[GLYPH_ORIGINY] + AscentOfsY, glyphitem[GLYPH_WIDTH],
                                     glyphitem[GLYPH_HEIGHT], metrics, numcolor > 256);

        data->Antialiased = aa;
        data->Blured = false;
        data->BlurWidth = shwidth;
        data->BlurLevel = shlevel;
        data->Gray = numcolor;
        if (shlevel != 0)
        {
            if (shlevel == 255 && shwidth == 0)
            {
                // normal shadow
                shadow = data;
                shadow->AddRef();
            }
            else
            {
                // blured shadow
                shadow = new tTVPCharacterData(
                    o->GetData(), glyphitem[GLYPH_WIDTH], glyphitem[GLYPH_ORIGINX] + AscentOfsX,
                    -glyphitem[GLYPH_ORIGINY] + AscentOfsY, glyphitem[GLYPH_WIDTH],
                    glyphitem[GLYPH_HEIGHT], metrics, numcolor > 256);
                shadow->Antialiased = aa;
                shadow->Blured = true;
                shadow->BlurWidth = shwidth;
                shadow->BlurLevel = shlevel;
                shadow->Gray = numcolor;
                if (!shadow->FullColored)
                    shadow->Blur();
            }
        }

        if (data)
        {

            if (data->BlackBoxX != 0 && data->BlackBoxY != 0)
            {
                tTVPRect drect;
                tTVPRect shadowdrect;

                bool shadowdrawn = false;

                if (shadow)
                {
                    shadowdrawn = InternalDrawText(shadow, x + shofsx, y + shofsy, shadowcolor,
                                                   &dtdata, shadowdrect);
                }

                bool drawn = InternalDrawText(data, x, y, color, &dtdata, drect);
                if (updaterects)
                {
                    if (!shadowdrawn)
                    {
                        if (drawn)
                            updaterects->Or(drect);
                    }
                    else
                    {
                        if (drawn)
                        {
                            tTVPRect d;
                            TVPUnionRect(&d, drect, shadowdrect);
                            updaterects->Or(d);
                        }
                        else
                        {
                            updaterects->Or(shadowdrect);
                        }
                    }
                }
            }
        }
    }
    catch (...)
    {
        if (data)
            data->Release();
        if (shadow)
            shadow->Release();
        throw;
    }

    if (data)
        data->Release();
    if (shadow)
        shadow->Release();
}
//---------------------------------------------------------------------------
void tTVPNativeBaseBitmap::DrawTextSingle(const tTVPRect& destrect,
                                          tjs_int x,
                                          tjs_int y,
                                          const ttstr& text,
                                          tjs_uint32 color,
                                          tTVPBBBltMethod bltmode,
                                          tjs_int opa,
                                          bool holdalpha,
                                          bool aa,
                                          tjs_int shlevel,
                                          tjs_uint32 shadowcolor,
                                          tjs_int shwidth,
                                          tjs_int shofsx,
                                          tjs_int shofsy,
                                          tTVPComplexRect* updaterects)
{
    // text drawing function for single character

    if (!Is32BPP())
        TVPThrowExceptionMessage(TVPInvalidOperationFor8BPP);

    if (bltmode == bmAlphaOnAlpha)
    {
        if (opa < -255)
            opa = -255;
        if (opa > 255)
            opa = 255;
    }
    else
    {
        if (opa < 0)
            opa = 0;
        if (opa > 255)
            opa = 255;
    }

    if (opa == 0)
        return; // nothing to do

    Independ();

    ApplyFont();

    std::vector<tjs_wchar> pList = decodeUTF8ToTTFe(text.c_str());
    tTVPDrawTextData dtdata;
    dtdata.rect = destrect;
    dtdata.bmppitch = GetPitchBytes();
    dtdata.bltmode = bltmode;
    dtdata.opa = opa;
    dtdata.holdalpha = holdalpha;

    tTVPFontAndCharacterData font;
    font.Font = Font;
    font.Antialiased = aa;
    font.Hinting = true;
    font.BlurLevel = shlevel;
    font.BlurWidth = shwidth;
    font.FontHash = FontHash;

    font.Character = pList.size() ? pList.at(0) : 0;

    font.Blured = false;
    tTVPCharacterData* shadow = NULL;
    tTVPCharacterData* data = NULL;

    try
    {
        data = TVPGetCharacter(font, this, PrerenderedFont, AscentOfsX, AscentOfsY);

        if (shlevel != 0)
        {
            if (shlevel == 255 && shwidth == 0)
            {
                // normal shadow
                shadow = data;
                shadow->AddRef();
            }
            else
            {
                // blured shadow
                font.Blured = true;
                shadow = TVPGetCharacter(font, this, PrerenderedFont, AscentOfsX, AscentOfsY);
            }
        }

        if (data)
        {

            if (data->BlackBoxX != 0 && data->BlackBoxY != 0)
            {
                tTVPRect drect;
                tTVPRect shadowdrect;

                bool shadowdrawn = false;

                if (shadow)
                {
                    shadowdrawn = InternalDrawText(shadow, x + shofsx, y + shofsy, shadowcolor,
                                                   &dtdata, shadowdrect);
                }

                bool drawn = InternalDrawText(data, x, y, color, &dtdata, drect);
                if (updaterects)
                {
                    if (!shadowdrawn)
                    {
                        if (drawn)
                            updaterects->Or(drect);
                    }
                    else
                    {
                        if (drawn)
                        {
                            tTVPRect d;
                            TVPUnionRect(&d, drect, shadowdrect);
                            updaterects->Or(d);
                        }
                        else
                        {
                            updaterects->Or(shadowdrect);
                        }
                    }
                }
            }
        }
    }
    catch (...)
    {
        if (data)
            data->Release();
        if (shadow)
            shadow->Release();
        throw;
    }

    if (data)
        data->Release();
    if (shadow)
        shadow->Release();
}
//---------------------------------------------------------------------------
// structure for holding data for a character
struct tTVPCharacterDrawData
{
    tTVPCharacterData* Data;   // main character data
    tTVPCharacterData* Shadow; // shadow character data
    tjs_int X, Y;
    tTVPRect ShadowRect;
    bool ShadowDrawn;

    tTVPCharacterDrawData(tTVPCharacterData* data, tTVPCharacterData* shadow, tjs_int x, tjs_int y)
    {
        Data = data;
        Shadow = shadow;
        X = x;
        Y = y;
        ShadowDrawn = false;

        if (Data)
            Data->AddRef();
        if (Shadow)
            Shadow->AddRef();
    }

    ~tTVPCharacterDrawData()
    {
        if (Data)
            Data->Release();
        if (Shadow)
            Shadow->Release();
    }

    tTVPCharacterDrawData(const tTVPCharacterDrawData& rhs)
    {
        Data = Shadow = NULL;
        *this = rhs;
    }

    void operator=(const tTVPCharacterDrawData& rhs)
    {
        X = rhs.X;
        Y = rhs.Y;
        ShadowRect = rhs.ShadowRect;
        ShadowDrawn = rhs.ShadowDrawn;

        if (Data != rhs.Data)
        {
            if (Data)
                Data->Release();
            Data = rhs.Data;
            if (Data)
                Data->AddRef();
        }
        if (Shadow != rhs.Shadow)
        {
            if (Shadow)
                Shadow->Release();
            Shadow = rhs.Shadow;
            if (Shadow)
                Shadow->AddRef();
        }
    }
};
//---------------------------------------------------------------------------
void tTVPNativeBaseBitmap::DrawTextMultiple(const tTVPRect& destrect,
                                            tjs_int x,
                                            tjs_int y,
                                            const ttstr& text,
                                            tjs_uint32 color,
                                            tTVPBBBltMethod bltmode,
                                            tjs_int opa,
                                            bool holdalpha,
                                            bool aa,
                                            tjs_int shlevel,
                                            tjs_uint32 shadowcolor,
                                            tjs_int shwidth,
                                            tjs_int shofsx,
                                            tjs_int shofsy,
                                            tTVPComplexRect* updaterects)
{
    // text drawing function for multiple characters

    if (!Is32BPP())
        TVPThrowExceptionMessage(TVPInvalidOperationFor8BPP);

    if (bltmode == bmAlphaOnAlpha)
    {
        if (opa < -255)
            opa = -255;
        if (opa > 255)
            opa = 255;
    }
    else
    {
        if (opa < 0)
            opa = 0;
        if (opa > 255)
            opa = 255;
    }

    if (opa == 0)
        return; // nothing to do

    Independ();

    ApplyFont();

    std::vector<tjs_wchar> pList = decodeUTF8ToTTFe(text.c_str());
    tTVPDrawTextData dtdata;
    dtdata.rect = destrect;
    dtdata.bmppitch = GetPitchBytes();
    dtdata.bltmode = bltmode;
    dtdata.opa = opa;
    dtdata.holdalpha = holdalpha;

    tTVPFontAndCharacterData font;
    font.Font = Font;
    font.Antialiased = aa;
    font.Hinting = true;
    font.BlurLevel = shlevel;
    font.BlurWidth = shwidth;
    font.FontHash = FontHash;

    std::vector<tTVPCharacterDrawData> drawdata;
    drawdata.reserve(pList.size());

    // prepare all drawn characters
    for (std::vector<tjs_wchar>::iterator p = pList.begin(); p != pList.end();
         p++) // while input string is remaining
    {
        font.Character = *p;

        font.Blured = false;
        tTVPCharacterData* data = NULL;
        tTVPCharacterData* shadow = NULL;
        try
        {
            data = TVPGetCharacter(font, this, PrerenderedFont, AscentOfsX, AscentOfsY);

            if (data)
            {
                if (shlevel != 0)
                {
                    if (shlevel == 255 && shwidth == 0)
                    {
                        // normal shadow
                        // shadow is the same as main character data
                        shadow = data;
                        shadow->AddRef();
                    }
                    else
                    {
                        // blured shadow
                        font.Blured = true;
                        shadow =
                            TVPGetCharacter(font, this, PrerenderedFont, AscentOfsX, AscentOfsY);
                    }
                }

                if (data->BlackBoxX != 0 && data->BlackBoxY != 0)
                {
                    // append to array
                    drawdata.push_back(tTVPCharacterDrawData(data, shadow, x, y));
                }

                // step to the next character position
                x += data->Metrics.CellIncX;
                if (data->Metrics.CellIncY != 0)
                {
                    // Windows 9x returns negative CellIncY.
                    // so we must verify whether CellIncY is proper.
                    if (Font.Angle < 1800)
                    {
                        if (data->Metrics.CellIncY > 0)
                            data->Metrics.CellIncY = -data->Metrics.CellIncY;
                    }
                    else
                    {
                        if (data->Metrics.CellIncY < 0)
                            data->Metrics.CellIncY = -data->Metrics.CellIncY;
                    }
                    y += data->Metrics.CellIncY;
                }
            }
        }
        catch (...)
        {
            if (data)
                data->Release();
            if (shadow)
                shadow->Release();
            throw;
        }
        if (data)
            data->Release();
        if (shadow)
            shadow->Release();
    }

    // draw shadows first
    if (shlevel != 0)
    {
        for (std::vector<tTVPCharacterDrawData>::iterator i = drawdata.begin(); i != drawdata.end();
             i++)
        {
            tTVPCharacterData* shadow = i->Shadow;

            if (shadow)
            {
                i->ShadowDrawn = InternalDrawText(shadow, i->X + shofsx, i->Y + shofsy, shadowcolor,
                                                  &dtdata, i->ShadowRect);
            }
        }
    }

    // then draw main characters
    // and compute returning update rectangle
    for (std::vector<tTVPCharacterDrawData>::iterator i = drawdata.begin(); i != drawdata.end();
         i++)
    {
        tTVPCharacterData* data = i->Data;
        tTVPRect drect;

        bool drawn = InternalDrawText(data, i->X, i->Y, color, &dtdata, drect);
        if (updaterects)
        {
            if (!i->ShadowDrawn)
            {
                if (drawn)
                    updaterects->Or(drect);
            }
            else
            {
                if (drawn)
                {
                    tTVPRect d;
                    TVPUnionRect(&d, drect, i->ShadowRect);
                    updaterects->Or(d);
                }
                else
                {
                    updaterects->Or(i->ShadowRect);
                }
            }
        }
    }
}
//---------------------------------------------------------------------------
void tTVPNativeBaseBitmap::GetTextSize(const ttstr& text)
{
    ApplyFont();

    if (text != CachedText)
    {
        CachedText = text;

        if (PrerenderedFont)
        {
            tjs_uint width = 0;
            std::vector<tjs_wchar> pList = decodeUTF8ToTTFe(text.c_str());
            for (std::vector<tjs_wchar>::iterator buf = pList.begin(); buf != pList.end(); buf++)
            {
                const tTVPPrerenderedCharacterItem* item = PrerenderedFont->Find(*buf);
                if (item != NULL)
                {
                    width += item->Inc;
                }
                else
                {
                    tjs_int w, h;
                    GetCurrentRasterizer()->GetTextExtent(*buf, w, h);
                    width += w;
                }
            }
            TextWidth = width;
            TextHeight = std::abs(Font.Height);
        }
        else
        {
            tjs_uint width = 0;
            std::vector<tjs_wchar> pList = decodeUTF8ToTTFe(text.c_str());
            for (std::vector<tjs_wchar>::iterator buf = pList.begin(); buf != pList.end(); buf++)
            {
                tjs_int w, h;
                GetCurrentRasterizer()->GetTextExtent(*buf, w, h);
                width += w;
            }
            TextWidth = width;
            TextHeight = std::abs(Font.Height);
        }
    }
}
//---------------------------------------------------------------------------
tjs_int tTVPNativeBaseBitmap::GetTextWidth(const ttstr& text)
{
    GetTextSize(text);
    return TextWidth;
}
//---------------------------------------------------------------------------
tjs_int tTVPNativeBaseBitmap::GetTextHeight(const ttstr& text)
{
    GetTextSize(text);
    return TextHeight;
}
//---------------------------------------------------------------------------
double tTVPNativeBaseBitmap::GetEscWidthX(const ttstr& text)
{
    GetTextSize(text);
    return cos(RadianAngle) * TextWidth;
}
//---------------------------------------------------------------------------
double tTVPNativeBaseBitmap::GetEscWidthY(const ttstr& text)
{
    GetTextSize(text);
    return sin(RadianAngle) * (-TextWidth);
}
//---------------------------------------------------------------------------
double tTVPNativeBaseBitmap::GetEscHeightX(const ttstr& text)
{
    GetTextSize(text);
    return sin(RadianAngle) * TextHeight;
}
//---------------------------------------------------------------------------
double tTVPNativeBaseBitmap::GetEscHeightY(const ttstr& text)
{
    GetTextSize(text);
    return cos(RadianAngle) * TextHeight;
}
//---------------------------------------------------------------------------
void tTVPNativeBaseBitmap::GetFontGlyphDrawRect(const ttstr& text, struct tTVPRect& area)
{
    ApplyFont();
    GetCurrentRasterizer()->GetGlyphDrawRect(decodeUTF8ToTTFe(text.c_str()), area);
}
iTVPTexture2D* tTVPNativeBaseBitmap::GetTextureForRender(bool isBlendTarget, const tTVPRect* rc)
{
    if (isBlendTarget || !rc)
        Independ();
    else
    {
        int w = Bitmap->GetWidth(), h = Bitmap->GetHeight();
        if (rc->left == 0 && rc->top == 0 && rc->right >= w && rc->bottom >= h)
        {
            IndependNoCopy();
        }
        else
        {
            Independ();
        }
    }
    return GetTexture();
}
//---------------------------------------------------------------------------
