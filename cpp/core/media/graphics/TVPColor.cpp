#include "TVPColor.h"

unsigned long ColorToRGB(unsigned int col)
{
    // 0xBBGGRR
    switch (col)
    {
        case clScrollBar:
            return 0xc8c8c8;
        case clBackground:
            return 0;
        case clActiveCaption:
            return 0xd1b499;
        case clInactiveCaption:
            return 0xdbcdbf;
        case clMenu:
            return 0xf0f0f0;
        case clWindow:
            return 0xffffff;
        case clWindowFrame:
            return 0x646464;
        case clMenuText:
            return 0;
        case clWindowText:
            return 0;
        case clCaptionText:
            return 0;
        case clActiveBorder:
            return 0xb4b4b4;
        case clInactiveBorder:
            return 0xfcf7f4;
        case clAppWorkSpace:
            return 0xababab;
        case clHighlight:
            return 0xff9933;
        case clHighlightText:
            return 0xffffff;
        case clBtnFace:
            return 0xf0f0f0;
        case clBtnShadow:
            return 0xa0a0a0;
        case clGrayText:
            return 0x6d6d6d;
        case clBtnText:
            return 0;
        case clInactiveCaptionText:
            return 0x544e43;
        case clBtnHighlight:
            return 0xffffff;
        case cl3DDkShadow:
            return 0x696969;
        case cl3DLight:
            return 0xe3e3e3;
        case clInfoText:
            return 0;
        case clInfoBk:
            return 0xe1ffff;
        case clUnknown:
            return 0;
        case clHotLight:
            return 0xcc6600;
        case clGradientActiveCaption:
            return 0xead1b9;
        case clGradientInactiveCaption:
            return 0xf2e4d7;
        case clMenuLight:
            return 0xff9933;
        case clMenuBar:
            return 0xf0f0f0;
        default:
            return col & 0xFFFFFF;
    }
}

template<>
void tTVPARGB<tjs_uint8>::Zero()
{
    *(tjs_uint32*)this = 0;
}

template<>
void tTVPARGB<tjs_uint8>::operator=(tjs_uint32 v)
{
    *(tjs_uint32*)this = v;
}

template<>
tTVPARGB<tjs_uint8>::operator tjs_uint32() const
{
    return *(const tjs_uint32*)this;
}

template<>
void tTVPARGB<tjs_uint8>::average(tjs_int n)
{
    tjs_int half_n = n >> 1;

    tjs_int recip = (1L << 23) / n;

    b = (b + half_n) * recip >> 23;
    g = (g + half_n) * recip >> 23;
    r = (r + half_n) * recip >> 23;
    a = (a + half_n) * recip >> 23;
}

template<>
void tTVPARGB<tjs_uint16>::average(tjs_int n)
{
    tjs_int half_n = n >> 1;

    tjs_int recip = (1L << 16) / n;

    b = (b + half_n) * recip >> 16;
    g = (g + half_n) * recip >> 16;
    r = (r + half_n) * recip >> 16;
    a = (a + half_n) * recip >> 16;
}

template<>
void tTVPARGB<tjs_uint32>::average(tjs_int n)
{
    tjs_int half_n = n >> 1;

    b = (b + half_n) / n;
    g = (g + half_n) / n;
    r = (r + half_n) / n;
    a = (a + half_n) / n;
}

// avoid bug for VC compiler
void __argb_hack_for_vc()
{
    tTVPARGB<tjs_uint8> a;
    a = 0;
    a.Zero();
    a.operator tjs_uint32();
    a.average(0);
    tTVPARGB<tjs_uint16> b;
    b.average(0);
    tTVPARGB<tjs_uint32> c;
    c.average(0);
}