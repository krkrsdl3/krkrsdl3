#pragma once

#include "tjsNative.h"
#include "tjsNativeLayer.h"

//---------------------------------------------------------------------------
// tTJSNI_Font : Font Native Object
//---------------------------------------------------------------------------
class tTJSNI_Font : public tTJSNativeInstance {
    typedef tTJSNativeInstance inherited;

    tTVPFont Font;
    tTJSNI_BaseLayer *Layer;

    tjs_int GetTextWidthDirect(const ttstr &text);

public:
    tTJSNI_Font();
    ~tTJSNI_Font();
    tjs_error TJS_INTF_METHOD Construct(tjs_int numparams, tTJSVariant **param,
                                        iTJSDispatch2 *tjs_obj);
    void TJS_INTF_METHOD Invalidate();

    tTJSNI_BaseLayer *GetLayer() const { return Layer; }

    void SetFontFace(const ttstr &face);
    ttstr GetFontFace() const;
    void SetFontHeight(tjs_int height);
    tjs_int GetFontHeight() const;
    void SetFontAngle(tjs_int angle);
    tjs_int GetFontAngle() const;
    void SetFontBold(bool b);
    bool GetFontBold() const;
    void SetFontItalic(bool b);
    bool GetFontItalic() const;
    void SetFontStrikeout(bool b);
    bool GetFontStrikeout() const;
    void SetFontUnderline(bool b);
    bool GetFontUnderline() const;
    void SetFontFaceIsFileName(bool b);
    bool GetFontFaceIsFileName() const;

    tjs_int GetTextWidth(const ttstr &text);
    tjs_int GetTextHeight(const ttstr &text);
    double GetEscWidthX(const ttstr &text);
    double GetEscWidthY(const ttstr &text);
    double GetEscHeightX(const ttstr &text);
    double GetEscHeightY(const ttstr &text);
    void GetFontGlyphDrawRect(const ttstr &text, tTVPRect &area);

    void GetFontList(tjs_uint32 flags, std::vector<ttstr> &list);

    void MapPrerenderedFont(const ttstr &storage);
    void UnmapPrerenderedFont();

    const tTVPFont &GetFont() const;
};
//---------------------------------------------------------------------------

extern iTJSDispatch2 *TVPCreateFontObject(iTJSDispatch2 *layer);

//---------------------------------------------------------------------------
// tTJSNC_Font : TJS Font class
//---------------------------------------------------------------------------
class tTJSNC_Font : public tTJSNativeClass
{
public:
	tTJSNC_Font();
	static tjs_uint32 ClassID;

protected:
	tTJSNativeInstance* CreateNativeInstance();
};
//---------------------------------------------------------------------------
extern tTJSNativeClass* TVPCreateNativeClass_Font();
//---------------------------------------------------------------------------

