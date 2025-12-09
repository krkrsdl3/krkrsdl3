//---------------------------------------------------------------------------
/*
	Risa [傝偝]      alias 媑棦媑棦3 [kirikiri-3]
	 stands for "Risa Is a Stagecraft Architecture"
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
//! @file
//! @brief FreeType 僼僅儞僩僪儔僀僶
//---------------------------------------------------------------------------

#include "tjsCommHead.h"
#include "FreeTypeImp.h"

#include "TVPMsg.h"
#include "TVPSystem.h"
#include "ComplexRect.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4819)
#endif
#include <ft2build.h>
#include FT_TRUETYPE_UNPATENTED_H
#include FT_SYNTHESIS_H
#include FT_BITMAP_H
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include "TVPFont.h"

extern bool TVPEncodeUTF8ToUTF16(ttstr &output, const std::string &source);

//---------------------------------------------------------------------------

FT_Library FreeTypeLibrary = NULL;	//!< FreeType 儔僀僽儔儕
void TVPInitializeFont() {
	if( FreeTypeLibrary == NULL ) {
		FT_Error err = FT_Init_FreeType( &FreeTypeLibrary );
	}
}
void TVPUninitializeFreeFont() {
	if( FreeTypeLibrary ) {
		FT_Done_FreeType( FreeTypeLibrary );
		FreeTypeLibrary = NULL;
	}
}

//---------------------------------------------------------------------------
/**
 * 僼傽僀儖僔僗僥儉宱桼偱偺FreeType Face 僋儔僗
 */
class tGenericFreeTypeFace : public tBaseFreeTypeFace
{
protected:
	FT_Face Face;	//!< FreeType face 僆僽僕僃僋僩
	tTJSBinaryStream* File;	 //!< tTJSBinaryStream 僆僽僕僃僋僩
	std::vector<ttstr> FaceNames; //!< Face柤傪楍嫇偟偨攝楍

private:
	FT_StreamRec Stream;

public:
	tGenericFreeTypeFace(const ttstr &fontname, tjs_uint32 options);
	virtual ~tGenericFreeTypeFace();

	virtual FT_Face GetFTFace() const;
	virtual void GetFaceNameList(std::vector<ttstr> & dest) const;
	virtual tjs_char GetDefaultChar() const { return L' '; }

private:
	void Clear();
	static unsigned long IoFunc( FT_Stream stream, unsigned long offset, unsigned char* buffer, unsigned long count );
	static void CloseFunc( FT_Stream  stream );

	bool OpenFaceByIndex(tjs_uint index, FT_Face & face);
};
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
/**
 * 僐儞僗僩儔僋僞
 * @param fontname	僼僅儞僩柤
 * @param options	僆僾僔儑儞(TVP_TF_XXXX 掕悢偐TVP_FACE_OPTIONS_XXXX掕悢偺慻傒崌傢偣)
 */
tGenericFreeTypeFace::tGenericFreeTypeFace(const ttstr &fontname, tjs_uint32 options) : File(NULL)
{
	// 僼傿乕儖僪偺弶婜壔
	Face = NULL;
	memset(&Stream, 0, sizeof(Stream));

	try {
		if(File) {
			delete File;
			File = NULL;
		} 

		// 僼傽僀儖傪奐偔
		File = TVPCreateFontStream(fontname);
		if( File == NULL ) {
			TVPThrowExceptionMessage( TVPCannotOpenFontFile, fontname );
		}

		// FT_StreamRec 偺奺僼傿乕儖僪傪杽傔傞
		FT_StreamRec * fsr = &Stream;
		fsr->base = 0;
		fsr->size = static_cast<unsigned long>(File->GetSize());
		fsr->pos = 0;
		fsr->descriptor.pointer = this;
		fsr->pathname.pointer = NULL;
		fsr->read = IoFunc;
		fsr->close = CloseFunc;

		// Face 傪偦傟偧傟奐偒丄Face柤傪庢摼偟偰 FaceNames 偵奿擺偡傞
		tjs_uint face_num = 1;

		FT_Face face = NULL;

		for(tjs_uint i = 0; i < face_num; i++)
		{
			if(!OpenFaceByIndex(i, face))
			{
				FaceNames.push_back(ttstr());
			}
			else
			{
				const char * name = face->family_name;
				ttstr wname;
				TVPEncodeUTF8ToUTF16( wname, std::string(name) );
				FaceNames.push_back( wname );
				face_num = face->num_faces;
			}
		}

		if(face) FT_Done_Face(face), face = NULL;


		// FreeType 僄儞僕儞偱僼傽僀儖傪奐偙偆偲偟偰傒傞
		tjs_uint index = TVP_GET_FACE_INDEX_FROM_OPTIONS(options);
		if(!OpenFaceByIndex(index, Face)) {
			// 僼僅儞僩傪奐偗側偐偭偨
			TVPThrowExceptionMessage(TVPFontCannotBeUsed, fontname );
		}
	}
	catch(...)
	{
		throw;
	}
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
/**
 * 僨僗僩儔僋僞
 */
tGenericFreeTypeFace::~tGenericFreeTypeFace()
{
	if(Face) FT_Done_Face(Face), Face = NULL;
	if(File) {
		delete File;
		File = NULL;
	}
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
/**
 * FreeType 偺 Face 僆僽僕僃僋僩傪曉偡
 */
FT_Face tGenericFreeTypeFace::GetFTFace() const
{
	return Face;
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
/**
 * 偙偺僼僅儞僩僼傽僀儖偑帩偭偰偄傞僼僅儞僩傪攝楍偲偟偰曉偡
 */
void tGenericFreeTypeFace::GetFaceNameList(std::vector<ttstr> & dest) const
{
	dest = FaceNames;
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
/**
 * FreeType 梡 僗僩儕乕儉撉傒崬傒娭悢
 */
unsigned long tGenericFreeTypeFace::IoFunc( FT_Stream stream, unsigned long offset, unsigned char* buffer, unsigned long count )
{
	tGenericFreeTypeFace * _this =
		static_cast<tGenericFreeTypeFace*>(stream->descriptor.pointer);

	size_t result;
	if(count == 0)
	{
		// seek
		result = 0;
		_this->File->SetPosition( offset );
	}
	else
	{
		// read
		_this->File->SetPosition( offset );
		_this->File->ReadBuffer(buffer, count);
		result = count;
	}

	return (unsigned long)result;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
/**
 * FreeType 梡 僗僩儕乕儉嶍彍娭悢
 */
void tGenericFreeTypeFace::CloseFunc( FT_Stream  stream )
{
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
/**
 * 巜掕僀儞僨僢僋僗偺Face傪奐偔
 * @param index	奐偔index
 * @param face	FT_Face 曄悢傊偺嶲徠
 * @return	Face傪奐偗傟偽 true 偦偆偱側偗傟偽 false
 * @note	弶傔偰 Face 傪奐偔応崌偼 face 偱巜掕偡傞曄悢偵偼 null 傪擖傟偰偍偔偙偲
 */
bool tGenericFreeTypeFace::OpenFaceByIndex(tjs_uint index, FT_Face & face)
{
	if(face) FT_Done_Face(face), face = NULL;

	FT_Parameter parameters[1];
	parameters[0].tag = FT_PARAM_TAG_UNPATENTED_HINTING; // Apple偺摿嫋夞旔傪峴偆
	parameters[0].data = NULL;

	FT_Open_Args args;
	memset(&args, 0, sizeof(args));
	args.flags = FT_OPEN_STREAM;
	args.stream = &Stream;
	args.driver = 0;
	args.num_params = 1;
	args.params = parameters;

	FT_Error err = FT_Open_Face( FreeTypeLibrary, &args, index, &face);

	return err == 0;
}
//---------------------------------------------------------------------------





//---------------------------------------------------------------------------
/**
 * 僐儞僗僩儔僋僞
 * @param fontname	僼僅儞僩柤
 * @param options	僆僾僔儑儞
 */
tFreeTypeFace::tFreeTypeFace(const ttstr &fontname, tjs_uint32 options)
	: FontName(fontname)
{
	TVPInitializeFont();

	// 僼傿乕儖僪傪僋儕傾
	Face = NULL;
	GlyphIndexToCharcodeVector = NULL;
	UnicodeToLocalChar = NULL;
	LocalCharToUnicode = NULL;
	Options = options;
	Height = 10;


	// 僼僅儞僩傪奐偔
	//if(options & TVP_FACE_OPTIONS_FILE)
	{
		// 僼傽僀儖傪奐偔
		Face = new tGenericFreeTypeFace(fontname, options);
			// 椺奜偑偙偙偱敪惗偡傞壜擻惈偑偁傞偺偱拲堄
	}
	//else
	{
		// 僱僀僥傿僽偺僼僅儞僩柤偵傛傞巜掕 (僾儔僢僩僼僅乕儉埶懚)
		//Face = new tNativeFreeTypeFace(fontname, options);
			// 椺奜偑偙偙偱敪惗偡傞壜擻惈偑偁傞偺偱拲堄
	}
	FTFace = Face->GetFTFace();

	// 儅僢僺儞僌傪妋擣偡傞
	if(FTFace->charmap == NULL)
	{
		// FreeType 偼帺摦揑偵 UNICODE 儅僢僺儞僌傪巊梡偡傞偑丄
		// 僼僅儞僩偑 UNICODE 儅僢僺儞僌偺忣曬傪娷傫偱偄側偄応崌偼
		// 帺摦揑側暥帤儅僢僺儞僌偺慖戰偼峴傢傟側偄丅
		// 偲傝偁偊偢(擔杮岅娐嫬偵尷偭偰尵偊偽) SJIS 儅僢僺儞僌偟偐傕偭偰側偄
		// 僼僅儞僩偑懡偄偺偱SJIS傪慖戰偝偣偰傒傞丅
		FT_Error err;
		{
			int numcharmap = FTFace->num_charmaps;
			for( int i = 0; i < numcharmap; i++ )
			{
				FT_Encoding enc = FTFace->charmaps[i]->encoding;
				if( enc != FT_ENCODING_NONE && enc != FT_ENCODING_APPLE_ROMAN )
				{
					err = FT_Select_Charmap(FTFace, enc);
					if(!err) {
						break;
					}
				}
			}
		}
	}
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
/**
 * 僨僗僩儔僋僞
 */
tFreeTypeFace::~tFreeTypeFace()
{
	if(GlyphIndexToCharcodeVector) delete GlyphIndexToCharcodeVector;
	if(Face) delete Face;
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
/**
 * 偙偺Face偑曐帩偟偰偄傞glyph偺悢傪摼傞
 * @return	偙偺Face偑曐帩偟偰偄傞glyph偺悢
 */
tjs_uint tFreeTypeFace::GetGlyphCount()
{
	if(!FTFace) return 0;

	// FreeType 偑曉偟偰偔傞僌儕僼偺悢偼丄幚嵺偵暥帤僐乕僪偑妱傝摉偰傜傟偰偄側偄
	// 僌儕僼傪傕娷傫偩悢偲側偭偰偄傞
	// 偙偙偱丄幚嵺偵僼僅儞僩偵娷傑傟偰偄傞僌儕僼傪庢摼偡傞
	// TODO:僗儗僢僪曐岇偝傟偰偄側偄偺偱拲堄両両両両両両
	if(!GlyphIndexToCharcodeVector)
	{
		// 儅僢僾偑嶌惉偝傟偰偄側偄偺偱嶌惉偡傞
		GlyphIndexToCharcodeVector = new tGlyphIndexToCharcodeVector;
		FT_ULong  charcode;
		FT_UInt   gindex;
		charcode = FT_Get_First_Char( FTFace, &gindex );
		while ( gindex != 0 )
		{
			FT_ULong code;
			if(LocalCharToUnicode)
				code = LocalCharToUnicode(charcode);
			else
				code = charcode;
			GlyphIndexToCharcodeVector->push_back(code);
			charcode = FT_Get_Next_Char( FTFace, charcode, &gindex );
		}
		std::sort(
			GlyphIndexToCharcodeVector->begin(),
			GlyphIndexToCharcodeVector->end()); // 暥帤僐乕僪弴偱暲傃懼偊
	}

	return (tjs_uint)GlyphIndexToCharcodeVector->size();
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
/**
 * Glyph 僀儞僨僢僋僗偐傜懳墳偡傞暥帤僐乕僪傪摼傞
 * @param index	僀儞僨僢僋僗(FreeType偺娗棟偟偰偄傞暥帤index偲偼堘偆偺偱拲堄)
 * @return	懳墳偡傞暥帤僐乕僪(懳墳偡傞僐乕僪偑柍偄応崌偼 0)
 */
tjs_char tFreeTypeFace::GetCharcodeFromGlyphIndex(tjs_uint index)
{
	tjs_uint size = GetGlyphCount(); // 僌儕僼悢傪摼傞偮偄偱偵儅僢僾傪嶌惉偡傞

	if(!GlyphIndexToCharcodeVector) return 0;
	if(index >= size) return 0;

	return static_cast<tjs_char>((*GlyphIndexToCharcodeVector)[index]);
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
/**
 * 偙偺僼僅儞僩偵娷傑傟傞Face柤偺儕僗僩傪摼傞
 * @param dest	奿擺愭攝楍
 */
void tFreeTypeFace::GetFaceNameList(std::vector<ttstr> &dest)
{
	Face->GetFaceNameList(dest);
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
/**
 * 僼僅儞僩偺崅偝傪愝掕偡傞
 * @param height	僼僅儞僩偺崅偝(僺僋僙儖扨埵)
 */
void tFreeTypeFace::SetHeight(int height)
{
	Height = height;
	FT_Error err = FT_Set_Pixel_Sizes(FTFace, 0, Height);
	if(err)
	{
		// TODO: Error 僴儞僪儕儞僌
	}
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
/**
 * 巜掕偟偨暥帤僐乕僪偵懳偡傞僌儕僼價僢僩儅僢僾傪摼傞
 * @param code	暥帤僐乕僪
 * @return	怴婯嶌惉偝傟偨僌儕僼價僢僩儅僢僾僆僽僕僃僋僩傊偺億僀儞僞
 *			NULL 偺応崌偼曄姺偵幐攕偟偨応崌
 */
tTVPCharacterData * tFreeTypeFace::GetGlyphFromCharcode(tjs_char code)
{
	// 僌儕僼僗儘僢僩偵僌儕僼傪撉傒崬傒丄悺朄傪庢摼偡傞
	tGlyphMetrics metrics;
	if(!GetGlyphMetricsFromCharcode(code, metrics))
		return NULL;

	// 暥帤傪儗儞僟儕儞僌偡傞
	FT_Error err;

	if(FTFace->glyph->format != FT_GLYPH_FORMAT_BITMAP)
	{
		FT_Render_Mode mode;
		if(!(Options & TVP_FACE_OPTIONS_NO_ANTIALIASING))
			mode = FT_RENDER_MODE_NORMAL;
		else
			mode = FT_RENDER_MODE_MONO;
		err = FT_Render_Glyph(FTFace->glyph, mode);
			// note: 僨僼僅儖僩偺儗儞僟儕儞僌儌乕僪偼 FT_RENDER_MODE_NORMAL (256怓僌儗乕僗働乕儖)
			//       FT_RENDER_MODE_MONO 偼 1bpp 儌僲僋儘乕儉
		if(err) return NULL;
	}

	// 堦墳價僢僩儅僢僾宍幃傪僠僃僢僋
	FT_Bitmap *ft_bmp = &(FTFace->glyph->bitmap);
	FT_Bitmap new_bmp;
	bool release_ft_bmp = false;
	tTVPCharacterData * glyph_bmp = NULL;
	try
	{
		if(ft_bmp->rows && ft_bmp->width)
		{
			// 價僢僩儅僢僾偑僒僀僘傪帩偭偰偄傞応崌
			if(ft_bmp->pixel_mode != ft_pixel_mode_grays)
			{
				// ft_pixel_mode_grays 偱偼側偄偺偱 ft_pixel_mode_grays 宍幃偵曄姺偡傞
				FT_Bitmap_New(&new_bmp);
				release_ft_bmp = true;
				ft_bmp = &new_bmp;
				err = FT_Bitmap_Convert(FTFace->glyph->library,
					&(FTFace->glyph->bitmap),
					&new_bmp, 1);
					// 寢嬊 tGlyphBitmap 宍幃偵曄姺偡傞嵺偵傾儔僀儞儊儞僩傪偟捈偡偺偱
					// 偙偙偱巜掕偡傞 alignment 偼 1 偱傛偄
				if(err)
				{
					if(release_ft_bmp) FT_Bitmap_Done(FTFace->glyph->library, ft_bmp);
					return NULL;
				}
			}

			if(ft_bmp->num_grays != 256)
			{
				// gray 儗儀儖偑 256 偱偼側偄
				// 256 偵側傞傛偆偵忔嶼傪峴偆
				tjs_int32 multiply =
					static_cast<tjs_int32>((static_cast<tjs_int32> (1) << 30) - 1) /
						(ft_bmp->num_grays - 1);
				for(tjs_int y = ft_bmp->rows - 1; y >= 0; y--)
				{
					unsigned char * p = ft_bmp->buffer + y * ft_bmp->pitch;
					for(tjs_int x = ft_bmp->width - 1; x >= 0; x--)
					{
						tjs_int32 v = static_cast<tjs_int32>((*p * multiply)  >> 22);
						*p = static_cast<unsigned char>(v);
						p++;
					}
				}
			}
		}
		// 64攞偝傟偰偄傞傕偺傪夝彍偡傞
		metrics.CellIncX = FT_PosToInt( metrics.CellIncX );
		metrics.CellIncY = FT_PosToInt( metrics.CellIncY );

		// tGlyphBitmap 傪嶌惉偟偰曉偡
		//int baseline = (int)(FTFace->height + FTFace->descender) * FTFace->size->metrics.y_ppem / FTFace->units_per_EM;
		int baseline = (int)( FTFace->ascender ) * FTFace->size->metrics.y_ppem / FTFace->units_per_EM;

		glyph_bmp = new tTVPCharacterData(
			ft_bmp->buffer,
			ft_bmp->pitch,
			  FTFace->glyph->bitmap_left,
			  baseline - FTFace->glyph->bitmap_top,
			  ft_bmp->width,
			  ft_bmp->rows,
			metrics);
		glyph_bmp->Gray = 256;

		
		if( Options & TVP_TF_UNDERLINE ) {
			tjs_int pos = -1, thickness = -1;
			GetUnderline( pos, thickness );
			if( pos >= 0 && thickness > 0 ) {
				glyph_bmp->AddHorizontalLine( pos, thickness, 255 );
			}
		}
		if( Options & TVP_TF_STRIKEOUT ) {
			tjs_int pos = -1, thickness = -1;
			GetStrikeOut( pos, thickness );
			if( pos >= 0 && thickness > 0 ) {
				glyph_bmp->AddHorizontalLine( pos, thickness, 255 );
			}
		}
	}
	catch(...)
	{
		if(release_ft_bmp) FT_Bitmap_Done(FTFace->glyph->library, ft_bmp);
		throw;
	}
	if(release_ft_bmp) FT_Bitmap_Done(FTFace->glyph->library, ft_bmp);

	return glyph_bmp;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
/**
 * 巜掕偟偨暥帤僐乕僪偵懳偡傞昤夋椞堟傪摼傞
 * @param code	暥帤僐乕僪
 * @return	儗儞僟儕儞僌椞堟嬮宍傊偺億僀儞僞
 *			NULL 偺応崌偼曄姺偵幐攕偟偨応崌
 */
bool tFreeTypeFace::GetGlyphRectFromCharcode( tTVPRect& rt, tjs_char code, tjs_int& advancex, tjs_int& advancey )
{
	advancex = advancey = 0;
	if( !LoadGlyphSlotFromCharcode(code) )
		return false;

	int baseline = (int)( FTFace->ascender ) * FTFace->size->metrics.y_ppem / FTFace->units_per_EM;
	/*
	FT_Render_Glyph 偱儗儞僟儕儞僌偟側偄偲埲壓偺奺抣偼庢摼偱偒側偄
	tjs_int t = baseline - FTFace->glyph->bitmap_top;
	tjs_int l = FTFace->glyph->bitmap_left;
	tjs_int w = FTFace->glyph->bitmap.width;
	tjs_int h = FTFace->glyph->bitmap.rows;
	*/
	tjs_int t = baseline - FT_PosToInt( FTFace->glyph->metrics.horiBearingY );
	tjs_int l = FT_PosToInt( FTFace->glyph->metrics.horiBearingX );
	tjs_int w = FT_PosToInt( FTFace->glyph->metrics.width );
	tjs_int h = FT_PosToInt( FTFace->glyph->metrics.height );
	advancex = FT_PosToInt( FTFace->glyph->advance.x );
	advancey = FT_PosToInt( FTFace->glyph->advance.y );
	rt = tTVPRect(l,t,l+w,t+h);
	if( Options & TVP_TF_UNDERLINE ) {
		tjs_int pos = -1, thickness = -1;
		GetUnderline( pos, thickness );
		if( pos >= 0 && thickness > 0 ) {
			if( rt.left > 0 ) rt.left = 0;
			if( rt.right < advancex ) rt.right = advancex;
			if( pos < rt.top ) rt.top = pos;
			if( (pos+thickness) >= rt.bottom ) rt.bottom = pos+thickness+1;

		}
	}
	if( Options & TVP_TF_STRIKEOUT ) {
		tjs_int pos = -1, thickness = -1;
		GetStrikeOut( pos, thickness );
		if( pos >= 0 && thickness > 0 ) {
			if( rt.left > 0 ) rt.left = 0;
			if( rt.right < advancex ) rt.right = advancex;
			if( pos < rt.top ) rt.top = pos;
			if( (pos+thickness) >= rt.bottom ) rt.bottom = pos+thickness+1;
		}
	}
	return true;
}

//---------------------------------------------------------------------------
/**
 * 巜掕偟偨暥帤僐乕僪偵懳偡傞僌儕僼偺悺朄傪摼傞(暥帤傪恑傔傞偨傔偺僒僀僘)
 * @param code		暥帤僐乕僪
 * @param metrics	悺朄
 * @return	惉岟偺応崌恀丄幐攕偺応崌婾
 */
bool tFreeTypeFace::GetGlyphMetricsFromCharcode(tjs_char code,
	tGlyphMetrics & metrics)
{
	if(!LoadGlyphSlotFromCharcode(code)) return false;

	// 儊僩儕僢僋峔憿懱傪嶌惉
	// CellIncX 傗 CellIncY 偼 僺僋僙儖抣偑 64 攞偝傟偨抣側偺偱拲堄
	// 偙傟偼傕偲傕偲 FreeType 偺巇條偩偗傟偳傕丄Risa偱傕撪晹揑偵偼
	// 偙偺惛搙偱 CellIncX 傗 CellIncY 傪埖偆
	metrics.CellIncX =  FTFace->glyph->advance.x;
	metrics.CellIncY =  FTFace->glyph->advance.y;

	return true;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
/**
 * 巜掕偟偨暥帤僐乕僪偵懳偡傞僌儕僼偺僒僀僘傪摼傞(暥帤偺戝偒偝)
 * @param code		暥帤僐乕僪
 * @param metrics	僒僀僘
 * @return	惉岟偺応崌恀丄幐攕偺応崌婾
 */
bool tFreeTypeFace::GetGlyphSizeFromCharcode(tjs_char code, tGlyphMetrics & metrics)
{
	if(!LoadGlyphSlotFromCharcode(code)) return false;

	// 儊僩儕僢僋峔憿懱傪嶌惉
	metrics.CellIncX = FT_PosToInt( FTFace->glyph->metrics.horiAdvance );
	metrics.CellIncY = FT_PosToInt( FTFace->glyph->metrics.vertAdvance );

	return true;
}

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
/**
 * 巜掕偟偨暥帤僐乕僪偵懳偡傞僌儕僼傪僌儕僼僗儘僢僩偵愝掕偡傞
 * @param code	暥帤僐乕僪
 * @return	惉岟偺応崌恀丄幐攕偺応崌婾
 */
bool tFreeTypeFace::LoadGlyphSlotFromCharcode(tjs_char code)
{
	// TODO: 僗儗僢僪曐岇

	// 暥帤僐乕僪傪摼傞
	FT_ULong localcode;
	if(UnicodeToLocalChar == NULL)
		localcode = code;
	else
		localcode = UnicodeToLocalChar(code);

	// 暥帤僐乕僪偐傜 index 傪摼傞
	FT_UInt glyph_index = FT_Get_Char_Index(FTFace, localcode);
	if(glyph_index == 0)
		return false;

	// 僌儕僼僗儘僢僩偵暥帤傪撉傒崬傓
	FT_Int32 load_glyph_flag = 0;
	if(!(Options & TVP_FACE_OPTIONS_NO_ANTIALIASING))
		load_glyph_flag |= FT_LOAD_NO_BITMAP;
	else
		load_glyph_flag |= FT_LOAD_TARGET_MONO;
			// note: 價僢僩儅僢僾僼僅儞僩傪撉傒崬傒偨偔側偄応崌偼 FT_LOAD_NO_BITMAP 傪巜掕

	if(Options & TVP_FACE_OPTIONS_NO_HINTING)
		load_glyph_flag |= FT_LOAD_NO_HINTING|FT_LOAD_NO_AUTOHINT;
	if(Options & TVP_FACE_OPTIONS_FORCE_AUTO_HINTING)
		load_glyph_flag |= FT_LOAD_FORCE_AUTOHINT;

	FT_Error err;
	err = FT_Load_Glyph(FTFace, glyph_index, load_glyph_flag);

	if(err) return false;

	// 僼僅儞僩偺曄宍傪峴偆
	if( Options & TVP_TF_BOLD ) FT_GlyphSlot_Embolden(FTFace->glyph);
	if( Options & TVP_TF_ITALIC ) FT_GlyphSlot_Oblique( FTFace->glyph );

	return true;
}
//---------------------------------------------------------------------------

float FT_fixed26p6_to_float(long fixP) {
	const unsigned long fractional_base = 1 << 6;
	const unsigned long fractional_mask = fractional_base - 1;
	return (float)(abs(fixP) & fractional_mask) / fractional_base + (fixP >> 6);
}

const FT_Outline* tFreeTypeFace::GetOulineData(tjs_char code, float &w, float &h)
{
	FT_UInt glyph_index = FT_Get_Char_Index(FTFace, code);
	if (glyph_index == 0) return GetOulineData(GetDefaultChar(), w, h);
	if (FT_Load_Glyph(FTFace, glyph_index, FT_LOAD_DEFAULT)) {
		return nullptr;
	}
	w = FT_fixed26p6_to_float(FTFace->glyph->advance.x);
	h = FT_fixed26p6_to_float(FTFace->glyph->advance.y);
	FT_GlyphSlot pGlyphSlot = FTFace->glyph;
	return &pGlyphSlot->outline;
}
