#include "tjsNativeFont.h"

#include "tjsArray.h"
#include "FontRasterizer.h"
#include "TVPFont.h"
#include "TVPMsg.h"

#include "tjsNativeRect.h"

//---------------------------------------------------------------------------
// tTJSNI_Font : Font Native Object
//---------------------------------------------------------------------------
tTJSNI_Font::tTJSNI_Font() { Layer = NULL; }
//---------------------------------------------------------------------------
tTJSNI_Font::~tTJSNI_Font() {}
//---------------------------------------------------------------------------
tjs_error TJS_INTF_METHOD tTJSNI_Font::Construct(tjs_int numparams,
                                                 tTJSVariant **param,
                                                 iTJSDispatch2 *tjs_obj) {
    if(numparams >= 1) {
        iTJSDispatch2 *dsp = param[0]->AsObjectNoAddRef();

        tTJSNI_Layer *lay = NULL;
        if(TJS_FAILED(dsp->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
                                                 tTJSNC_Layer::ClassID,
                                                 (iTJSNativeInstance **)&lay)))
            TVPThrowExceptionMessage(TVPSpecifyLayer);

        Layer = lay;
    } else {
        Layer = NULL;
        Font = TVPGetDefaultFont();
    }

    return TJS_S_OK;
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTJSNI_Font::Invalidate() {
    Layer = NULL;

    inherited::Invalidate();
}
//---------------------------------------------------------------------------
void tTJSNI_Font::SetFontFace(const ttstr &face) {
    if(Layer)
        Layer->SetFontFace(face);
    else {
        if(Font.Face != face) {
            Font.Face = face;
        }
    }
}
//---------------------------------------------------------------------------
ttstr tTJSNI_Font::GetFontFace() const {
    if(Layer)
        return Layer->GetFontFace();
    else
        return Font.Face;
}
//---------------------------------------------------------------------------
void tTJSNI_Font::SetFontHeight(tjs_int height) {
    if(Layer)
        Layer->SetFontHeight(height);
    else {
        if(height < 0)
            height = -height; // TVP2 does not support negative value of height
        if(Font.Height != height) {
            Font.Height = height;
        }
    }
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_Font::GetFontHeight() const {
    if(Layer)
        return Layer->GetFontHeight();
    else
        return Font.Height;
}
//---------------------------------------------------------------------------
void tTJSNI_Font::SetFontAngle(tjs_int angle) {
    if(Layer)
        Layer->SetFontAngle(angle);
    else {
        if(Font.Angle != angle) {
            angle = angle % 3600;
            if(angle < 0)
                angle += 3600;
            Font.Angle = angle;
        }
    }
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_Font::GetFontAngle() const {
    if(Layer)
        return Layer->GetFontAngle();
    else
        return Font.Angle;
}
//---------------------------------------------------------------------------
void tTJSNI_Font::SetFontBold(bool b) {
    if(Layer)
        Layer->SetFontBold(b);
    else {
        if((0 != (Font.Flags & TVP_TF_BOLD)) != b) {
            Font.Flags &= ~TVP_TF_BOLD;
            if(b)
                Font.Flags |= TVP_TF_BOLD;
        }
    }
}
//---------------------------------------------------------------------------
bool tTJSNI_Font::GetFontBold() const {
    if(Layer)
        return Layer->GetFontBold();
    else
        return 0 != (Font.Flags & TVP_TF_BOLD);
}
//---------------------------------------------------------------------------
void tTJSNI_Font::SetFontItalic(bool b) {
    if(Layer)
        Layer->SetFontItalic(b);
    else {
        if((0 != (Font.Flags & TVP_TF_ITALIC)) != b) {
            Font.Flags &= ~TVP_TF_ITALIC;
            if(b)
                Font.Flags |= TVP_TF_ITALIC;
        }
    }
}
//---------------------------------------------------------------------------
bool tTJSNI_Font::GetFontItalic() const {
    if(Layer)
        return Layer->GetFontItalic();
    else
        return 0 != (Font.Flags & TVP_TF_ITALIC);
}
//---------------------------------------------------------------------------
void tTJSNI_Font::SetFontStrikeout(bool b) {
    if(Layer)
        Layer->SetFontStrikeout(b);
    else {
        if((0 != (Font.Flags & TVP_TF_STRIKEOUT)) != b) {
            Font.Flags &= ~TVP_TF_STRIKEOUT;
            if(b)
                Font.Flags |= TVP_TF_STRIKEOUT;
        }
    }
}
//---------------------------------------------------------------------------
bool tTJSNI_Font::GetFontStrikeout() const {
    if(Layer)
        return Layer->GetFontStrikeout();
    else
        return 0 != (Font.Flags & TVP_TF_STRIKEOUT);
}
//---------------------------------------------------------------------------
void tTJSNI_Font::SetFontUnderline(bool b) {
    if(Layer)
        Layer->SetFontUnderline(b);
    else {
        if((0 != (Font.Flags & TVP_TF_UNDERLINE)) != b) {
            Font.Flags &= ~TVP_TF_UNDERLINE;
            if(b)
                Font.Flags |= TVP_TF_UNDERLINE;
        }
    }
}
//---------------------------------------------------------------------------
bool tTJSNI_Font::GetFontUnderline() const {
    if(Layer)
        return Layer->GetFontUnderline();
    else
        return 0 != (Font.Flags & TVP_TF_UNDERLINE);
}
//---------------------------------------------------------------------------
void tTJSNI_Font::SetFontFaceIsFileName(bool b) {
    if(Layer)
        Layer->SetFontFaceIsFileName(b);
    else {
        if((0 != (Font.Flags & TVP_TF_FONTFILE)) != b) {
            Font.Flags &= ~TVP_TF_FONTFILE;
            if(b)
                Font.Flags |= TVP_TF_FONTFILE;
        }
    }
}
//---------------------------------------------------------------------------
bool tTJSNI_Font::GetFontFaceIsFileName() const {
    if(Layer)
        return Layer->GetFontFaceIsFileName();
    else
        return 0 != (Font.Flags & TVP_TF_FONTFILE);
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_Font::GetTextWidthDirect(const ttstr &text) {
    GetCurrentRasterizer()->ApplyFont(Font);
    tjs_uint width = 0;
    const tjs_char *buf = text.c_str();
    while(*buf) {
        tjs_int w, h;
        GetCurrentRasterizer()->GetTextExtent(*buf, w, h);
        width += w;
        buf++;
    }
    return width;
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_Font::GetTextWidth(const ttstr &text) {
    if(Layer)
        return Layer->GetTextWidth(text);
    else
        return GetTextWidthDirect(text);
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_Font::GetTextHeight(const ttstr &text) {
    if(Layer)
        return Layer->GetTextHeight(text);
    else
        return std::abs(Font.Height);
}
//---------------------------------------------------------------------------
double tTJSNI_Font::GetEscWidthX(const ttstr &text) {
    if(Layer)
        return Layer->GetEscWidthX(text);
    else
        return std::cos(Font.Angle * (M_PI / 1800)) * GetTextWidthDirect(text);
}
//---------------------------------------------------------------------------
double tTJSNI_Font::GetEscWidthY(const ttstr &text) {
    if(Layer)
        return Layer->GetEscWidthY(text);
    else
        return std::sin(Font.Angle * (M_PI / 1800)) *
            (-GetTextWidthDirect(text));
}
//---------------------------------------------------------------------------
double tTJSNI_Font::GetEscHeightX(const ttstr &text) {
    if(Layer)
        return Layer->GetEscHeightX(text);
    else
        return std::sin(Font.Angle * (M_PI / 1800)) * std::abs(Font.Height);
}
//---------------------------------------------------------------------------
double tTJSNI_Font::GetEscHeightY(const ttstr &text) {
    if(Layer)
        return Layer->GetEscHeightY(text);
    else
        return std::cos(Font.Angle * (M_PI / 1800)) * std::abs(Font.Height);
}
//---------------------------------------------------------------------------
void tTJSNI_Font::GetFontGlyphDrawRect(const ttstr &text, tTVPRect &area) {
    if(Layer) {
        Layer->GetFontGlyphDrawRect(text, area);
    } else {
        GetCurrentRasterizer()->ApplyFont(Font);
        GetCurrentRasterizer()->GetGlyphDrawRect(text, area);
    }
}
//---------------------------------------------------------------------------
void tTJSNI_Font::GetFontList(tjs_uint32 flags, std::vector<ttstr> &list) {
    if(Layer)
        Layer->GetFontList(flags, list);
    else {
        std::vector<ttstr> ansilist;
        TVPGetAllFontList(ansilist);
        for(std::vector<ttstr>::iterator i = ansilist.begin();
            i != ansilist.end(); i++)
            list.push_back(i->c_str());
    }
}
//---------------------------------------------------------------------------
void tTJSNI_Font::MapPrerenderedFont(const ttstr &storage) {
    if(Layer)
        Layer->MapPrerenderedFont(storage);
    else
        TVPMapPrerenderedFont(Font, storage);
}
//---------------------------------------------------------------------------
void tTJSNI_Font::UnmapPrerenderedFont() {
    if(Layer)
        Layer->UnmapPrerenderedFont();
    else
        TVPUnmapPrerenderedFont(Font);
}
//---------------------------------------------------------------------------
const tTVPFont &tTJSNI_Font::GetFont() const {
    if(Layer)
        return Layer->GetFont();
    else
        return Font;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_Font : TJS Font class
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_Font::ClassID = -1;
tTJSNC_Font::tTJSNC_Font() : tTJSNativeClass(TJS_W("Font"))
{
	TJS_BEGIN_NATIVE_MEMBERS(Font) // constructor
		TJS_DECL_EMPTY_FINALIZE_METHOD
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(/*var.name*/_this, /*var.type*/tTJSNI_Font,
			/*TJS class name*/Font)
	{
		return TJS_S_OK;
	}
	TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/Font)
		//----------------------------------------------------------------------

		//-- methods

		//---------------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/getTextWidth)
	{
		TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_Font);
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		if (result) *result = _this->GetTextWidth(*param[0]);

		return TJS_S_OK;
	}
	TJS_END_NATIVE_METHOD_DECL(/*func. name*/getTextWidth)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/getTextHeight)
	{
		TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_Font);
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		if (result) *result = _this->GetTextHeight(*param[0]);

		return TJS_S_OK;
	}
	TJS_END_NATIVE_METHOD_DECL(/*func. name*/getTextHeight)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/getEscWidthX)
	{
		TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_Font);
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		if (result) *result = _this->GetEscWidthX(*param[0]);

		return TJS_S_OK;
	}
	TJS_END_NATIVE_METHOD_DECL(/*func. name*/getEscWidthX)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/getEscWidthY)
	{
		TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_Font);
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		if (result) *result = _this->GetEscWidthY(*param[0]);

		return TJS_S_OK;
	}
	TJS_END_NATIVE_METHOD_DECL(/*func. name*/getEscWidthY)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/getEscHeightX)
	{
		TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_Font);
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		if (result) *result = _this->GetEscHeightX(*param[0]);

		return TJS_S_OK;
	}
	TJS_END_NATIVE_METHOD_DECL(/*func. name*/getEscHeightX)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/getEscHeightY)
	{
		TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_Font);
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		if (result) *result = _this->GetEscHeightY(*param[0]);

		return TJS_S_OK;
	}
	TJS_END_NATIVE_METHOD_DECL(/*func. name*/getEscHeightY)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/getGlyphDrawRect)
	{
		TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_Font);
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		if (result) {
			tTVPRect rt;
			_this->GetFontGlyphDrawRect(*param[0], rt);
			iTJSDispatch2* disp = TVPCreateRectObject(rt.left, rt.top, rt.right, rt.bottom);
			*result = tTJSVariant(disp, disp);
			disp->Release();
		}

		return TJS_S_OK;
	}
	TJS_END_NATIVE_METHOD_DECL(/*func. name*/getGlyphDrawRect)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/doUserSelect)
	{
		TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_Font);

		if (numparams < 4) return TJS_E_BADPARAMCOUNT;

		tjs_uint32 flags = (tjs_int64)*param[0];
		ttstr caption = *param[1];
		ttstr prompt = *param[2];
		ttstr samplestring = *param[3];

		tjs_int ret = // TODO: implement it ?
			0;

		if (result) *result = ret;

		return TJS_S_OK;
	}
	TJS_END_NATIVE_METHOD_DECL(/*func. name*/doUserSelect)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/getList)
	{
		TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_Font);

		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		tjs_uint32 flags = static_cast<tjs_uint32>((tjs_int64)*param[0]);

		std::vector<ttstr> list;
		_this->GetFontList(flags, list);

		if (result)
		{
			iTJSDispatch2* dsp;
			dsp = TJSCreateArrayObject();
			tTJSVariant tmp(dsp, dsp);
			*result = tmp;
			dsp->Release();

			for (tjs_uint i = 0; i < list.size(); i++)
			{
				tmp = list[i];
				dsp->PropSetByNum(TJS_MEMBERENSURE, i, &tmp, dsp);
			}
		}

		return TJS_S_OK;
	}
	TJS_END_NATIVE_METHOD_DECL(/*func. name*/getList)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/mapPrerenderedFont)
	{
		TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_Font);
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		_this->MapPrerenderedFont(*param[0]);

		return TJS_S_OK;
	}
	TJS_END_NATIVE_METHOD_DECL(/*func. name*/mapPrerenderedFont)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/unmapPrerenderedFont)
	{
		TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_Font);
		if (numparams < 0) return TJS_E_BADPARAMCOUNT;

		_this->UnmapPrerenderedFont();

		return TJS_S_OK;
	}
	TJS_END_NATIVE_METHOD_DECL(/*func. name*/unmapPrerenderedFont)
		//----------------------------------------------------------------------

		//-- properties

		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_PROP_DECL(face)
	{
		TJS_BEGIN_NATIVE_PROP_GETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_Font);
			*result = _this->GetFontFace();
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_GETTER

			TJS_BEGIN_NATIVE_PROP_SETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_Font);
			_this->SetFontFace(*param);
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_SETTER
	}
	TJS_END_NATIVE_PROP_DECL(face)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_PROP_DECL(height)
	{
		TJS_BEGIN_NATIVE_PROP_GETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_Font);
			*result = _this->GetFontHeight();
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_GETTER

			TJS_BEGIN_NATIVE_PROP_SETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_Font);
			_this->SetFontHeight(*param);
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_SETTER
	}
	TJS_END_NATIVE_PROP_DECL(height)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_PROP_DECL(bold)
	{
		TJS_BEGIN_NATIVE_PROP_GETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_Font);
			*result = _this->GetFontBold();
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_GETTER

			TJS_BEGIN_NATIVE_PROP_SETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_Font);
			_this->SetFontBold(*param);
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_SETTER
	}
	TJS_END_NATIVE_PROP_DECL(bold)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_PROP_DECL(italic)
	{
		TJS_BEGIN_NATIVE_PROP_GETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_Font);
			*result = _this->GetFontItalic();
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_GETTER

			TJS_BEGIN_NATIVE_PROP_SETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_Font);
			_this->SetFontItalic(*param);
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_SETTER
	}
	TJS_END_NATIVE_PROP_DECL(italic)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_PROP_DECL(strikeout)
	{
		TJS_BEGIN_NATIVE_PROP_GETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_Font);
			*result = _this->GetFontStrikeout();
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_GETTER

			TJS_BEGIN_NATIVE_PROP_SETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_Font);
			_this->SetFontStrikeout(*param);
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_SETTER
	}
	TJS_END_NATIVE_PROP_DECL(strikeout)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_PROP_DECL(underline)
	{
		TJS_BEGIN_NATIVE_PROP_GETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_Font);
			*result = _this->GetFontUnderline();
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_GETTER

			TJS_BEGIN_NATIVE_PROP_SETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_Font);
			_this->SetFontUnderline(*param);
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_SETTER
	}
	TJS_END_NATIVE_PROP_DECL(underline)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_PROP_DECL(angle)
	{
		TJS_BEGIN_NATIVE_PROP_GETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_Font);
			*result = _this->GetFontAngle();
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_GETTER

			TJS_BEGIN_NATIVE_PROP_SETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_Font);
			_this->SetFontAngle(*param);
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_SETTER
	}
	TJS_END_NATIVE_PROP_DECL(angle)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_PROP_DECL(faceIsFileName)
	{
		// Face柤傪僼傽僀儖柤偲偟偰奐偔丄FreeType偱偺傒桳岠丅偨偩偟丄偦偺儗僀儎乕偱IME傪桳岠偟偨応崌摦嶌偼晄掕
		TJS_BEGIN_NATIVE_PROP_GETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_Font);
			*result = _this->GetFontFaceIsFileName();
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_GETTER

			TJS_BEGIN_NATIVE_PROP_SETTER
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_Font);
			_this->SetFontFaceIsFileName(*param);
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_SETTER
	}
	TJS_END_NATIVE_PROP_DECL(faceIsFileName)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_PROP_DECL(rasterizer)
	{
		TJS_BEGIN_NATIVE_PROP_GETTER
		{
			*result = TVPGetFontRasterizer();
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_GETTER

			TJS_BEGIN_NATIVE_PROP_SETTER
		{
			TVPSetFontRasterizer(*param);
			return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_SETTER
	}
	TJS_END_NATIVE_STATIC_PROP_DECL(rasterizer)
		//----------------------------------------------------------------------
		TJS_BEGIN_NATIVE_PROP_DECL(defaultFaceName)
	{
		TJS_BEGIN_NATIVE_PROP_GETTER
		{
			*result = TVPGetDefaultFontName();
		// *result = ttstr(TVPFontSystem->GetDefaultFontName());
		return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_GETTER

			TJS_BEGIN_NATIVE_PROP_SETTER
		{
			ttstr name(*param);
		// don't override, specified by preference
		// TVPFontSystem->SetDefaultFontName( name.c_str() );
		return TJS_S_OK;
		}
			TJS_END_NATIVE_PROP_SETTER
	}
	TJS_END_NATIVE_STATIC_PROP_DECL(defaultFaceName)
		//----------------------------------------------------------------------

		TJS_END_NATIVE_MEMBERS
}

//---------------------------------------------------------------------------
// TVPCreateNativeClass_Font
//---------------------------------------------------------------------------
struct tFontClassHolder {
	tTJSNativeClass* Obj;
	tFontClassHolder() : Obj(NULL) {}
	void Set(tTJSNativeClass* obj) {
		if (Obj) {
			Obj->Release();
			Obj = NULL;
		}
		Obj = obj;
		Obj->AddRef();
	}
	~tFontClassHolder() { if (Obj) Obj->Release(), Obj = NULL; }
} static fontclassholder;
//---------------------------------------------------------------------------
tTJSNativeClass* TVPCreateNativeClass_Font()
{
	if (fontclassholder.Obj) {
		tTJSNativeClass* fontclass = fontclassholder.Obj;
		fontclass->AddRef();
		return fontclass;
	}
	tTJSNativeClass* fontclass = new tTJSNC_Font();
	fontclassholder.Set(fontclass);
	return fontclass;
}
//---------------------------------------------------------------------------
iTJSDispatch2* TVPCreateFontObject(iTJSDispatch2* layer)
{
	if (fontclassholder.Obj == NULL) {
		TVPThrowInternalError;
	}
	iTJSDispatch2* out;
	tTJSVariant param(layer);
	tTJSVariant* pparam = &param;
	if (TJS_FAILED(fontclassholder.Obj->CreateNew(0, NULL, NULL, &out, 1, &pparam, fontclassholder.Obj)))
		TVPThrowInternalError;

	return out;
}
//---------------------------------------------------------------------------

tTJSNativeInstance* tTJSNC_Font::CreateNativeInstance()
{
	return new tTJSNI_Font();
}

//---------------------------------------------------------------------------
iTJSDispatch2* TVPGetObjectFrom_NI_BaseLayer(tTJSNI_BaseLayer* layer)
{
	iTJSDispatch2* disp = layer->Owner;
	if (disp) disp->AddRef();
	return disp;
}
//---------------------------------------------------------------------------
