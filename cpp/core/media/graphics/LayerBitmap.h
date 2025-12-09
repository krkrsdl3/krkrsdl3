//--------------------------------------------------------------------------- 
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//--------------------------------------------------------------------------- 
// Base Layer Bitmap implementation 
//--------------------------------------------------------------------------- 
#ifndef LayerBitmapIntfH
#define LayerBitmapIntfH

#include "gl/tvpgl.h"
#include "TVPColor.h"
#include "ComplexRect.h"
#include "BitmapInfomation.h"
#include "TVPfontstruc.h"
#include "drawable.h"
#include "FontRasterizer.h"

#ifndef TVP_REVRGB
#define TVP_REVRGB(v) ((v & 0xFF00FF00) | ((v >> 16) & 0xFF) | ((v & 0xFF) << 16))
#endif

/*[*/
//--------------------------------------------------------------------------- 
// tTVPBBBltMethod and tTVPBBStretchType 
//--------------------------------------------------------------------------- 
enum tTVPBBBltMethod
{
	bmCopy,
	bmCopyOnAlpha,
	bmAlpha,
	bmAlphaOnAlpha,
	bmAdd,
	bmSub,
	bmMul,
	bmDodge,
	bmDarken,
	bmLighten,
	bmScreen,
	bmAddAlpha,
	bmAddAlphaOnAddAlpha,
	bmAddAlphaOnAlpha,
	bmAlphaOnAddAlpha,
	bmCopyOnAddAlpha,
	bmPsNormal,
	bmPsAdditive,
	bmPsSubtractive,
	bmPsMultiplicative,
	bmPsScreen,
	bmPsOverlay,
	bmPsHardLight,
	bmPsSoftLight,
	bmPsColorDodge,
	bmPsColorDodge5,
	bmPsColorBurn,
	bmPsLighten,
	bmPsDarken,
	bmPsDifference,
	bmPsDifference5,
	bmPsExclusion
};

enum tTVPBBStretchType
{
	stNearest = 0, // primal method; nearest neighbor method 
	stFastLinear = 1, // fast linear interpolation (does not have so much precision) 
	stLinear = 2,  // (strict) linear interpolation 
	stCubic = 3,    // cubic interpolation 
	stSemiFastLinear = 4,
	stFastCubic = 5,
	stLanczos2 = 6,    // Lanczos 2 interpolation 
	stFastLanczos2 = 7,
	stLanczos3 = 8,    // Lanczos 3 interpolation 
	stFastLanczos3 = 9,
	stSpline16 = 10,	// Spline16 interpolation 
	stFastSpline16 = 11,
	stSpline36 = 12,	// Spline36 interpolation 
	stFastSpline36 = 13,
	stAreaAvg = 14,	// Area average interpolation 
	stFastAreaAvg = 15,
	stGaussian = 16,
	stFastGaussian = 17,
	stBlackmanSinc = 18,
	stFastBlackmanSinc = 19,

	stTypeMask = 0x0000ffff, // stretch type mask 
	stFlagMask = 0xffff0000, // flag mask 

	stRefNoClip = 0x10000 // referencing source is not limited by the given rectangle 
						// (may allow to see the border pixel to interpolate) 
};
/*]*/



//--------------------------------------------------------------------------- 
// FIXME: for including order problem 
//--------------------------------------------------------------------------- 
//---------------------------------------------------------------------------
extern void TVPSetFontCacheForLowMem();
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// tTVPBitmap : internal bitmap object
//---------------------------------------------------------------------------
class tTVPBitmap
{
public:
	static const tjs_int DEFAULT_PALETTE_COUNT = 256;
private:
	tjs_int RefCount;

	void* Bits; // pointer to bitmap bits
	BitmapInfomation* BitmapInfo; // DIB information

	tjs_int PitchBytes; // bytes required in a line
	tjs_int PitchStep; // step bytes to next(below) line
	tjs_int Width; // actual width
	tjs_int Height; // actual height

	tjs_int ActualPalCount;
	tjs_uint* Palette;

public:
	tTVPBitmap(tjs_uint width, tjs_uint height, tjs_uint bpp);
	// for async load
	// @param bits : tTVPBitmapBitsAlloc::Allocで確保したものを使用すること
	tTVPBitmap(tjs_uint width, tjs_uint height, tjs_uint bpp, void* bits);

	tTVPBitmap(const tTVPBitmap& r);

	~tTVPBitmap();

	void Allocate(tjs_uint width, tjs_uint height, tjs_uint bpp);

	void AddRef(void)
	{
		RefCount++;
	}

	void Release(void)
	{
		if (RefCount == 1)
			delete this;
		else
			RefCount--;
	}

	tjs_uint GetWidth() const { return Width; }
	tjs_uint GetHeight() const { return Height; }

	tjs_uint GetBPP() const { return BitmapInfo->GetBPP(); }
	bool Is32bit() const { return BitmapInfo->Is32bit(); }
	bool Is8bit() const { return BitmapInfo->Is8bit(); }


	void* GetScanLine(tjs_uint l) const;

	tjs_int GetPitch() const { return PitchStep; }

	bool IsIndependent() const { return RefCount == 1; }

	const BitmapInfomation* GetBitmapInfomation() const { return BitmapInfo; }

	const void* GetBits() const { return Bits; }
	const tjs_uint* GetPalette() const { return Palette; };
	tjs_uint* GetPalette() { return Palette; };
	tjs_uint GetPaletteCount() const { return ActualPalCount; };
	void SetPaletteCount(tjs_uint count);

	bool IsOpaque = false;
};
//---------------------------------------------------------------------------




//---------------------------------------------------------------------------
// tTVPNativeBaseBitmap
//---------------------------------------------------------------------------
class iTVPTexture2D;
class tTVPComplexRect;
class tTVPCharacterData;
struct tTVPDrawTextData;
class tTVPPrerenderedFont;
class tTVPNativeBaseBitmap
{
public:
	tTVPNativeBaseBitmap(/*tjs_uint w, tjs_uint h, tjs_uint bpp*/);
	tTVPNativeBaseBitmap(const tTVPNativeBaseBitmap& r);
	virtual ~tTVPNativeBaseBitmap();

	/* metrics */
	tjs_uint GetWidth() const;
	void SetWidth(tjs_uint w);
	tjs_uint GetHeight() const;
	void SetHeight(tjs_uint h);

	void SetSize(tjs_uint w, tjs_uint h, bool keepimage = true);
	// for async load
	// @param bits : tTVPBitmapBitsAlloc::Allocで確保したものを使用すること
	void SetSizeAndImageBuffer(tTVPBitmap* bmp);

	/* color depth */
	tjs_uint GetBPP() const;

	bool Is32BPP() const;
	bool Is8BPP() const;
	bool IsOpaque() const;

	/* assign */
	bool Assign(const tTVPNativeBaseBitmap& rhs);
	bool AssignBitmap(const tTVPNativeBaseBitmap& rhs); // assigns only bitmap
	bool AssignTexture(iTVPTexture2D* tex);

	/* scan line */
	const void* GetScanLine(tjs_uint l) const;
	void* GetScanLineForWrite(tjs_uint l);
	tjs_int GetPitchBytes() const;


	/* object lifetime management */
	void Independ();
	void IndependNoCopy();
	void Recreate();
	void Recreate(tjs_uint w, tjs_uint h, tjs_uint bpp);

	bool IsIndependent() const;

	/* other utilities */
	iTVPTexture2D* GetTexture() const { return Bitmap; }
	virtual iTVPTexture2D* GetTextureForRender(bool isBlendTarget, const tTVPRect* rc);
	/* font and text functions */
private:
	tTVPFont Font;
	bool FontChanged;
	tjs_int GlobalFontState;

	// v--- these can be recreated in ApplyFont if FontChanged flag is set
	tTVPPrerenderedFont* PrerenderedFont;
	tjs_int AscentOfsX;
	tjs_int AscentOfsY;
	double RadianAngle;
	tjs_uint32 FontHash;
	// ^---

	void ApplyFont();

public:
	void SetFont(const tTVPFont& font);
	const tTVPFont& GetFont() const { return Font; };

	void GetFontList(tjs_uint32 flags, std::vector<ttstr>& list);

	void MapPrerenderedFont(const ttstr& storage);
	void UnmapPrerenderedFont();

private:
	bool InternalBlendText(tTVPCharacterData* data, tTVPDrawTextData* dtdata, tjs_uint32 color, const tTVPRect& srect, tTVPRect& drect);

	bool InternalDrawText(tTVPCharacterData* data, tjs_int x,
		tjs_int y, tjs_uint32 shadowcolor, tTVPDrawTextData* dtdata, tTVPRect& drect);

public:
	void DrawTextSingle(const tTVPRect& destrect, tjs_int x, tjs_int y, const ttstr& text,
		tjs_uint32 color, tTVPBBBltMethod bltmode, tjs_int opa = 255,
		bool holdalpha = true, bool aa = true, tjs_int shlevel = 0,
		tjs_uint32 shadowcolor = 0,
		tjs_int shwidth = 0, tjs_int shofsx = 0, tjs_int shofsy = 0,
		tTVPComplexRect* updaterects = NULL);
	void DrawTextMultiple(const tTVPRect& destrect, tjs_int x, tjs_int y, const ttstr& text,
		tjs_uint32 color, tTVPBBBltMethod bltmode, tjs_int opa = 255,
		bool holdalpha = true, bool aa = true, tjs_int shlevel = 0,
		tjs_uint32 shadowcolor = 0,
		tjs_int shwidth = 0, tjs_int shofsx = 0, tjs_int shofsy = 0,
		tTVPComplexRect* updaterects = NULL);
	void DrawText(const tTVPRect& destrect, tjs_int x, tjs_int y, const ttstr& text,
		tjs_uint32 color, tTVPBBBltMethod bltmode, tjs_int opa = 255,
		bool holdalpha = true, bool aa = true, tjs_int shlevel = 0,
		tjs_uint32 shadowcolor = 0,
		tjs_int shwidth = 0, tjs_int shofsx = 0, tjs_int shofsy = 0,
		tTVPComplexRect* updaterects = NULL)
	{
		tjs_int len = text.GetLen();
		if (len == 0) return;
		if (len >= 2)
			DrawTextMultiple(
				destrect, x, y, text,
				color, bltmode, opa,
				holdalpha, aa, shlevel,
				shadowcolor, shwidth, shofsx, shofsy,
				updaterects);
		else    /* if len == 1 */
			DrawTextSingle(
				destrect, x, y, text,
				color, bltmode, opa,
				holdalpha, aa, shlevel,
				shadowcolor, shwidth, shofsx, shofsy,
				updaterects);
	}
	void DrawGlyph(iTJSDispatch2* glyph, const tTVPRect& destrect, tjs_int x, tjs_int y,
		tjs_uint32 color, tTVPBBBltMethod bltmode, tjs_int opa = 255,
		bool holdalpha = true, bool aa = true, tjs_int shlevel = 0,
		tjs_uint32 shadowcolor = 0,
		tjs_int shwidth = 0, tjs_int shofsx = 0, tjs_int shofsy = 0,
		tTVPComplexRect* updaterects = NULL);


private:
	tjs_int TextWidth;
	tjs_int TextHeight;
	ttstr CachedText;

	void GetTextSize(const ttstr& text);

public:
	tjs_int GetTextWidth(const ttstr& text);
	tjs_int GetTextHeight(const ttstr& text);
	double GetEscWidthX(const ttstr& text);
	double GetEscWidthY(const ttstr& text);
	double GetEscHeightX(const ttstr& text);
	double GetEscHeightY(const ttstr& text);
	void GetFontGlyphDrawRect(const ttstr& text, struct tTVPRect& area);

protected:
	//tTVPBitmap *Bitmap;
	iTVPTexture2D* Bitmap;
public:
	void operator =(const tTVPNativeBaseBitmap& rhs) { Assign(rhs); }
	virtual class iTVPRenderManager* GetRenderManager() = 0;
};
//--------------------------------------------------------------------------- 



//--------------------------------------------------------------------------- 
// t2DAffineMatrix 
//--------------------------------------------------------------------------- 
struct t2DAffineMatrix
{
	/* structure for subscribing following 2D affine transformation matrix */
	/*
	|                          | a  b  0 |
	| [x', y', 1] =  [x, y, 1] | c  d  0 |
	|                          | tx ty 1 |
	|  thus,
	|
	|  x' =  ax + cy + tx
	|  y' =  bx + dy + ty
	*/

	double a;
	double b;
	double c;
	double d;
	double tx;
	double ty;
};
//--------------------------------------------------------------------------- 



//--------------------------------------------------------------------------- 
#define TVP_BB_COPY_MAIN 1
#define TVP_BB_COPY_MASK 2
//--------------------------------------------------------------------------- 



//--------------------------------------------------------------------------- 
extern tTVPGLGammaAdjustData TVPIntactGammaAdjustData;
extern tjs_int TVPDrawThreadNum;
extern tjs_int TVPGetProcessorNum(void);
//--------------------------------------------------------------------------- 



//--------------------------------------------------------------------------- 
// iTVPBaseBitmap 
//--------------------------------------------------------------------------- 
class iTVPBaseBitmap : public tTVPNativeBaseBitmap
{
public:

	//void operator =(const iTVPBaseBitmap & rhs) { Assign(rhs); } 

	// metrics 
	void SetSizeWithFill(tjs_uint w, tjs_uint h, tjs_uint32 fillvalue);

	// point access 
	tjs_uint32 GetPoint(tjs_int x, tjs_int y) const;
	bool SetPoint(tjs_int x, tjs_int y, tjs_uint32 value);
	bool SetPointMain(tjs_int x, tjs_int y, tjs_uint32 color); // for 32bpp 
	bool SetPointMask(tjs_int x, tjs_int y, tjs_int mask); // for 32bpp 

	// drawing stuff 
	virtual bool Fill(tTVPRect rect, tjs_uint32 value);

	bool FillColor(tTVPRect rect, tjs_uint32 color, tjs_int opa);

private:
	bool BlendColor(tTVPRect rect, tjs_uint32 color, tjs_int opa, bool additive);

public:
	bool FillColorOnAlpha(tTVPRect rect, tjs_uint32 color, tjs_int opa)
	{
		return BlendColor(rect, color, opa, false);
	}

	bool FillColorOnAddAlpha(tTVPRect rect, tjs_uint32 color, tjs_int opa)
	{
		return BlendColor(rect, color, opa, true);
	}

	bool RemoveConstOpacity(tTVPRect rect, tjs_int level);

	bool FillMask(tTVPRect rect, tjs_int value);

	virtual bool CopyRect(tjs_int x, tjs_int y, const iTVPBaseBitmap *ref,
		tTVPRect refrect, tjs_int plane = (TVP_BB_COPY_MAIN|TVP_BB_COPY_MASK));

    /**
     * @param ref : コピー元画像(9patch形式)
     * @param margin : 9patchの右下にある描画領域指定を取得する
     */
    bool Copy9Patch( const iTVPBaseBitmap *ref, tTVPRect& margin );

	bool Blt(tjs_int x, tjs_int y, const iTVPBaseBitmap *ref,
		tTVPRect refrect, tTVPBBBltMethod method, tjs_int opa,
			bool hda = true);
	bool Blt(tjs_int x, tjs_int y, const iTVPBaseBitmap *ref,
		const tTVPRect &refrect, tTVPLayerType type, tjs_int opa, bool hda = true);

public:
	bool StretchBlt(tTVPRect cliprect, tTVPRect destrect, const iTVPBaseBitmap *ref,
		tTVPRect refrect, tTVPBBBltMethod method, tjs_int opa,
			bool hda = true, tTVPBBStretchType type = stNearest, tjs_real typeopt=0.0);

public:
	bool AffineBlt(tTVPRect destrect, const iTVPBaseBitmap *ref,
		tTVPRect refrect, const tTVPPointD * points,
			tTVPBBBltMethod method, tjs_int opa,
			tTVPRect * updaterect = NULL,
			bool hda = true, tTVPBBStretchType mode = stNearest, bool clear = false,
				tjs_uint32 clearcolor = 0);

	bool AffineBlt(tTVPRect destrect, const iTVPBaseBitmap *ref,
		tTVPRect refrect, const t2DAffineMatrix & matrix,
			tTVPBBBltMethod method, tjs_int opa,
			tTVPRect * updaterect = NULL,
			bool hda = true, tTVPBBStretchType mode = stNearest, bool clear = false,
				tjs_uint32 clearcolor = 0);

private:
	bool InternalDoBoxBlur(tTVPRect rect, tTVPRect area, bool hasalpha);

public:
	bool DoBoxBlur(const tTVPRect & rect, const tTVPRect & area);
	bool DoBoxBlurForAlpha(const tTVPRect & rect, const tTVPRect & area);

	void UDFlip(const tTVPRect &rect);
	void LRFlip(const tTVPRect &rect);

	void DoGrayScale(tTVPRect rect);

	void AdjustGamma(tTVPRect rect, const tTVPGLGammaAdjustData & data);
	void AdjustGammaForAdditiveAlpha(tTVPRect rect, const tTVPGLGammaAdjustData & data);


	void ConvertAddAlphaToAlpha();
	void ConvertAlphaToAddAlpha();

	// font and text related functions are implemented in each platform. 
};
//--------------------------------------------------------------------------- 
class iTVPRenderManager;
class tTVPBaseBitmap : public iTVPBaseBitmap // for ProvinceImage 
{
public:
	tTVPBaseBitmap(tjs_uint w, tjs_uint h, tjs_uint bpp = 32);
	tTVPBaseBitmap(const iTVPBaseBitmap& r) : iTVPBaseBitmap(r) {}
	virtual bool AssignBitmap(tTVPBitmap *bmp);
	virtual iTVPRenderManager* GetRenderManager();
	bool Fill(tTVPRect rect, tjs_uint32 value) override;
	virtual bool CopyRect(tjs_int x, tjs_int y, const iTVPBaseBitmap *ref,
		tTVPRect refrect, tjs_int plane = (TVP_BB_COPY_MAIN | TVP_BB_COPY_MASK));
	void UDFlip(const tTVPRect &rect);
	void LRFlip(const tTVPRect &rect);
};
//--------------------------------------------------------------------------- 
class tTVPBaseTexture : public iTVPBaseBitmap
{
public:
	tTVPBaseTexture(tjs_uint w, tjs_uint h, tjs_uint bpp = 32);
	tTVPBaseTexture(const iTVPBaseBitmap& r) : iTVPBaseBitmap(r) {}
	virtual bool AssignBitmap(tTVPBitmap *bmp);
	virtual iTVPRenderManager* GetRenderManager();
	void Update(const void *pixel, unsigned int pitch, int x, int y, int w, int h);
};

//---------------------------------------------------------------------------
void TVPSetFontRasterizer(tjs_int index);
tjs_int TVPGetFontRasterizer();
FontRasterizer *GetCurrentRasterizer();
void TVPMapPrerenderedFont(const tTVPFont &font, const ttstr &storage);
void TVPUnmapPrerenderedFont(const tTVPFont &font);
tjs_int TVPGetCursor(const ttstr &name);

#endif
