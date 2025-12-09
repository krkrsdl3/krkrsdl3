#ifndef __TVP_COLOR_H__
#define __TVP_COLOR_H__


#include "tjsTypes.h"
#include "gl/tvpgl.h"

enum {
	clScrollBar = 0x80000000,
	clBackground = 0x80000001,
	clActiveCaption = 0x80000002,
	clInactiveCaption = 0x80000003,
	clMenu = 0x80000004,
	clWindow = 0x80000005,
	clWindowFrame = 0x80000006,
	clMenuText = 0x80000007,
	clWindowText = 0x80000008,
	clCaptionText = 0x80000009,
	clActiveBorder = 0x8000000a,
	clInactiveBorder = 0x8000000b,
	clAppWorkSpace = 0x8000000c,
	clHighlight = 0x8000000d,
	clHighlightText = 0x8000000e,
	clBtnFace = 0x8000000f,
	clBtnShadow = 0x80000010,
	clGrayText = 0x80000011,
	clBtnText = 0x80000012,
	clInactiveCaptionText = 0x80000013,
	clBtnHighlight = 0x80000014,
	cl3DDkShadow = 0x80000015,
	cl3DLight = 0x80000016,
	clInfoText = 0x80000017,
	clInfoBk = 0x80000018,
	clNone = 0x1fffffff,
	clAdapt= 0x01ffffff,
	clPalIdx = 0x3000000,
	clAlphaMat = 0x4000000,
	clUnknown = 0x80000019,
	clHotLight = 0x8000001a,
	clGradientActiveCaption = 0x8000001b,
	clGradientInactiveCaption = 0x8000001c,
	clMenuLight = 0x8000001d,
	clMenuBar = 0x8000001e,
};

unsigned long ColorToRGB(unsigned int col);

//---------------------------------------------------------------------------
template<typename base_type>
struct tTVPARGB
{
    union
    {
        struct
        {
#if TJS_HOST_IS_LITTLE_ENDIAN
            base_type b;
            base_type g;
            base_type r;
            base_type a;
#endif
#if TJS_HOST_IS_BIG_ENDIAN
            base_type a;
            base_type r;
            base_type g;
            base_type b;
#endif
        };
        struct
        {
            base_type packed;
        };
    };

    typedef base_type base_int_type;

    tTVPARGB() { ; }

    //	tTVPARGB(const tTVPARGB & rhs)
    //		{ this->operator =(rhs); }

    void Zero() { b = g = r = a = 0; }

    void operator+=(const tTVPARGB& rhs)
    {
        b += rhs.b;
        g += rhs.g;
        r += rhs.r;
        a += rhs.a;
    }

    void operator-=(const tTVPARGB& rhs)
    {
        b -= rhs.b;
        g -= rhs.g;
        r -= rhs.r;
        a -= rhs.a;
    }

    void operator+=(tjs_uint32 v)
    {
        b += v & 0xff;
        g += (v >> 8) & 0xff;
        r += (v >> 16) & 0xff;
        a += (v >> 24);
    }

    void operator-=(tjs_uint32 v)
    {
        b -= v & 0xff;
        g -= (v >> 8) & 0xff;
        r -= (v >> 16) & 0xff;
        a -= (v >> 24);
    }

    // void operator = (const tTVPARGB& rhs)
    //{ *this = rhs; }

    void average(tjs_int n)
    {
        tjs_int half_n = n >> 1;

        b /= n;
        g /= n;
        r /= n;
        a /= n;
    }

    void operator=(tjs_uint32 v)
    {
        a = v >> 24, r = (v >> 16) & 0xff, g = (v >> 8) & 0xff, b = v & 0xff;
    }

    operator tjs_uint32() const { return b + (g << 8) + (r << 16) + (a << 24); }
};
//---------------------------------------------------------------------------
// special member functions for tjs_uint8
template<>
void tTVPARGB<tjs_uint8>::Zero();

template<>
void tTVPARGB<tjs_uint8>::operator=(tjs_uint32 v);

template<>
tTVPARGB<tjs_uint8>::operator tjs_uint32() const;

template<>
void tTVPARGB<tjs_uint8>::average(tjs_int n);

//---------------------------------------------------------------------------
// special member functions for tjs_uint16
template<>
void tTVPARGB<tjs_uint16>::average(tjs_int n);

//---------------------------------------------------------------------------
// special member functions for tjs_uint32
template<>
void tTVPARGB<tjs_uint32>::average(tjs_int n);

//---------------------------------------------------------------------------
// a structure delivered from tTVPARGB, using Additive-Alpha expression
// for tjs_uint32 (packed ARGB) input/output.
template<typename base_type>
struct tTVPARGB_AA : public tTVPARGB<base_type>
{
    void operator+=(const tTVPARGB_AA& rhs)
    {
        this->b += rhs.b;
        this->g += rhs.g;
        this->r += rhs.r;
        this->a += rhs.a;
    }

    void operator-=(const tTVPARGB_AA& rhs)
    {
        this->b -= rhs.b;
        this->g -= rhs.g;
        this->r -= rhs.r;
        this->a -= rhs.a;
    }

    // Four methods, which convert itself from/to tjs_uint32 (packed ARGB),
    // are overrided.

    void operator+=(tjs_uint32 v)
    {
        tjs_int aadj;
        this->a += (aadj = (v >> 24));
        aadj += aadj >> 7;
        this->b += (v & 0xff) * aadj >> 8;
        this->g += ((v >> 8) & 0xff) * aadj >> 8;
        this->r += ((v >> 16) & 0xff) * aadj >> 8;
    }

    void operator-=(tjs_uint32 v)
    {
        tjs_int aadj;
        this->a -= (aadj = (v >> 24));
        aadj += aadj >> 7;
        this->b -= (v & 0xff) * aadj >> 8;
        this->g -= ((v >> 8) & 0xff) * aadj >> 8;
        this->r -= ((v >> 16) & 0xff) * aadj >> 8;
    }

    void operator=(tjs_uint32 v)
    {
        this->a = v >> 24;
        tjs_int aadj = this->a + (this->a >> 7); // adjusted alpha
        this->r = ((v >> 16) & 0xff) * aadj >> 8;
        this->g = ((v >> 8) & 0xff) * aadj >> 8;
        this->b = (v & 0xff) * aadj >> 8;
    }

    operator tjs_uint32() const
    {
        tjs_uint8* t = TVPDivTable + (this->a << 8);
        return t[this->b] + (t[this->g] << 8) + (t[this->r] << 16) + (this->a << 24);
    }
};

//---------------------------------------------------------------------------

#endif

