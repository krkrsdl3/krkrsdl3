#include "ncbind/ncbind.hpp"
#include "LayerExDraw.hpp"
#include "TVPStorage.h"
#include <vector>
#include <stdio.h>


#pragma comment(lib, "GdiPlus.lib")

/*
this class provides COM's IStream adapter for tTJSBinaryStream
*/
class tTVPIStreamAdapter : public IStream
{
    tTJSBinaryStream* Stream;
    ULONG RefCount;

public:
    tTVPIStreamAdapter(tTJSBinaryStream* ref);

    /*
    the stream passed by argument here is freed by this instance'
    destruction.
    */

    virtual ~tTVPIStreamAdapter();

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;

    ULONG STDMETHODCALLTYPE AddRef() override;

    ULONG STDMETHODCALLTYPE Release() override;

    // ISequentialStream
    HRESULT STDMETHODCALLTYPE Read(void* pv, ULONG cb, ULONG* pcbRead) override;

    HRESULT STDMETHODCALLTYPE Write(const void* pv, ULONG cb, ULONG* pcbWritten) override;

    // IStream
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER dlibMove,
                                   DWORD dwOrigin,
                                   ULARGE_INTEGER* plibNewPosition) override;

    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER libNewSize) override;

    HRESULT STDMETHODCALLTYPE CopyTo(IStream* pstm,
                                     ULARGE_INTEGER cb,
                                     ULARGE_INTEGER* pcbRead,
                                     ULARGE_INTEGER* pcbWritten) override;

    HRESULT STDMETHODCALLTYPE Commit(DWORD grfCommitFlags) override;

    HRESULT STDMETHODCALLTYPE Revert() override;

    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER libOffset,
                                         ULARGE_INTEGER cb,
                                         DWORD dwLockType) override;

    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER libOffset,
                                           ULARGE_INTEGER cb,
                                           DWORD dwLockType) override;

    HRESULT STDMETHODCALLTYPE Stat(STATSTG* pstatstg, DWORD grfStatFlag) override;

    HRESULT STDMETHODCALLTYPE Clone(IStream** ppstm) override;

    void ClearStream() { Stream = nullptr; }
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPIStreamAdapter
//---------------------------------------------------------------------------
/*
        this class provides adapter for COM's IStream
*/
tTVPIStreamAdapter::tTVPIStreamAdapter(tTJSBinaryStream* ref)
{
    Stream = ref;
    RefCount = 1;
}

//---------------------------------------------------------------------------
tTVPIStreamAdapter::~tTVPIStreamAdapter()
{
    delete Stream;
}
//---------------------------------------------------------------------------
extern "C" const IID IID_IUnknown;
extern "C" const IID IID_IStream;
extern "C" const IID IID_ISequentialStream;

HRESULT STDMETHODCALLTYPE tTVPIStreamAdapter::QueryInterface(REFIID riid, void** ppvObject)
{
    if (!ppvObject)
        return E_INVALIDARG;

    *ppvObject = nullptr;
    if (!memcmp(&riid, &IID_IUnknown, 16))
        *ppvObject = reinterpret_cast<IUnknown*>(this);
    else if (!memcmp(&riid, &IID_ISequentialStream, 16))
        *ppvObject = reinterpret_cast<ISequentialStream*>(this);
    else if (!memcmp(&riid, &IID_IStream, 16))
        *ppvObject = reinterpret_cast<IStream*>(this);

    if (*ppvObject)
    {
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

//---------------------------------------------------------------------------
ULONG STDMETHODCALLTYPE tTVPIStreamAdapter::AddRef()
{
    return ++RefCount;
}

//---------------------------------------------------------------------------
ULONG STDMETHODCALLTYPE tTVPIStreamAdapter::Release()
{
    if (RefCount == 1)
    {
        delete this;
        return 0;
    }
    return --RefCount;
}

//---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE tTVPIStreamAdapter::Read(void* pv, ULONG cb, ULONG* pcbRead)
{
    try
    {
        ULONG read = Stream->Read(pv, cb);
        if (pcbRead)
            *pcbRead = read;
    }
    catch (...)
    {
        return E_FAIL;
    }
    return S_OK;
}

//---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE tTVPIStreamAdapter::Write(const void* pv, ULONG cb, ULONG* pcbWritten)
{
    try
    {
        ULONG written = Stream->Write(pv, cb);
        if (pcbWritten)
            *pcbWritten = written;
    }
    catch (...)
    {
        return E_FAIL;
    }
    return S_OK;
}

//---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE tTVPIStreamAdapter::Seek(LARGE_INTEGER dlibMove,
                                                   DWORD dwOrigin,
                                                   ULARGE_INTEGER* plibNewPosition)
{
    try
    {
        switch (dwOrigin)
        {
            case STREAM_SEEK_SET:
                if (plibNewPosition)
                    (*plibNewPosition).QuadPart = Stream->Seek(dlibMove.QuadPart, TJS_BS_SEEK_SET);
                else
                    Stream->Seek(dlibMove.QuadPart, TJS_BS_SEEK_SET);
                break;
            case STREAM_SEEK_CUR:
                if (plibNewPosition)
                    (*plibNewPosition).QuadPart = Stream->Seek(dlibMove.QuadPart, TJS_BS_SEEK_CUR);
                else
                    Stream->Seek(dlibMove.QuadPart, TJS_BS_SEEK_CUR);
                break;
            case STREAM_SEEK_END:
                if (plibNewPosition)
                    (*plibNewPosition).QuadPart = Stream->Seek(dlibMove.QuadPart, TJS_BS_SEEK_END);
                else
                    Stream->Seek(dlibMove.QuadPart, TJS_BS_SEEK_END);
                break;
            default:
                return E_FAIL;
        }
    }
    catch (...)
    {
        return E_FAIL;
    }
    return S_OK;
}

//---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE tTVPIStreamAdapter::SetSize(ULARGE_INTEGER libNewSize)
{
    return E_NOTIMPL;
}

//---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE tTVPIStreamAdapter::CopyTo(IStream* pstm,
                                                     ULARGE_INTEGER cb,
                                                     ULARGE_INTEGER* pcbRead,
                                                     ULARGE_INTEGER* pcbWritten)
{
    return E_NOTIMPL;
}

//---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE tTVPIStreamAdapter::Commit(DWORD grfCommitFlags)
{
    return E_NOTIMPL;
}

//---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE tTVPIStreamAdapter::Revert()
{
    return E_NOTIMPL;
}

//---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE tTVPIStreamAdapter::LockRegion(ULARGE_INTEGER libOffset,
                                                         ULARGE_INTEGER cb,
                                                         DWORD dwLockType)
{
    return E_NOTIMPL;
}

//---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE tTVPIStreamAdapter::UnlockRegion(ULARGE_INTEGER libOffset,
                                                           ULARGE_INTEGER cb,
                                                           DWORD dwLockType)
{
    return E_NOTIMPL;
}

//---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE tTVPIStreamAdapter::Stat(STATSTG* pstatstg, DWORD grfStatFlag)
{
    // This method imcompletely fills the target structure, because
    // some informations like access mode or stream name are already
    // lost at this point.

    if (pstatstg)
    {
        memset(pstatstg, 0, sizeof(*pstatstg));

        // pwcsName
        // this object's storage pointer does not have a name ...
        if (!(grfStatFlag & STATFLAG_NONAME))
        {
            // anyway returns an empty string
            pstatstg->pwcsName = (LPOLESTR)TJS_W("");
        }

        // type
        pstatstg->type = STGTY_STREAM;

        // cbSize
        pstatstg->cbSize.QuadPart = Stream->GetSize();

        // mtime, ctime, atime unknown

        // grfMode unknown
        pstatstg->grfMode = STGM_DIRECT | STGM_READWRITE | STGM_SHARE_DENY_WRITE;
        // Note that this method always returns flags above,
        // regardless of the actual mode. In the return value, the
        // stream is to be indicated that the stream can be written,
        // but of cource, the Write method will fail if the stream is
        // read-only.

        // grfLockSuppoted
        pstatstg->grfLocksSupported = 0;

        // grfStatBits unknown
    }
    else
    {
        return E_INVALIDARG;
    }

    return S_OK;
}

//---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE tTVPIStreamAdapter::Clone(IStream** ppstm)
{
    return E_NOTIMPL;
}

//---------------------------------------------------------------------------

IStream* TVPCreateIStream(tTJSBinaryStream* s)
{
    return new tTVPIStreamAdapter(s);
}
//---------------------------------------------------------------------------
// IStream creator
//---------------------------------------------------------------------------
IStream* TVPCreateIStream(const ttstr& name, tjs_uint32 flags)
{
    // convert tTJSBinaryStream to IStream thru TStream

    tTJSBinaryStream* stream0 = nullptr;
    try
    {
        stream0 = TVPCreateStream(name, flags);
    }
    catch (...)
    {
        delete stream0;
        return nullptr;
    }

    IStream* istream = new tTVPIStreamAdapter(stream0);

    return istream;
}
//---------------------------------------------------------------------------

#define NCB_MODULE_NAME TJS_W("layerExDraw.dll")

// GDI+ 基本情報
static GdiplusStartupInput gdiplusStartupInput;
static ULONG_PTR gdiplusToken;

// GDI+ 基本情報
static PrivateFontCollection *privateFontCollection = NULL;
static vector<void*> fontDatas;

inline static float ToFloat(FIXED& pfx)
{
  LONG l = *(LONG *)&pfx;

  return l / 65536.0f;
}

inline static PointF ToPointF(POINTFX *p)
{
  return PointF(ToFloat(p->x), -ToFloat(p->y));
}

// GDI+ 初期化
void initGdiPlus()
{
	// Initialize GDI+.
	GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
}

// GDI+ 初期化
void deInitGdiPlus()
{
    // フォントデータの解放
	delete privateFontCollection;
	vector<void*>::const_iterator i = fontDatas.begin();
	while (i != fontDatas.end()) {
		delete[] *i;
		i++;
	}
	fontDatas.clear();
	GdiplusShutdown(gdiplusToken);
}

/**
 * 画像読み込み処理
 * @param name ファイル名
 * @return 画像情報
 */
Image *loadImage(const tjs_char *name)
{
	Image *image = NULL;
	ttstr filename = TVPGetPlacedPath(name);
	if (filename.length()) {
		IStream *in = TVPCreateIStream(filename, TJS_BS_READ);
		if (in) {
			STATSTG stat;
			in->Stat(&stat, STATFLAG_NONAME);
			ULONG size = (ULONG)stat.cbSize.QuadPart;
			HGLOBAL hBuffer = ::GlobalAlloc(GMEM_MOVEABLE, size);
			if (hBuffer)	{
				void* pBuffer = ::GlobalLock(hBuffer);
				if (pBuffer) {
					if (in->Read(pBuffer, size, &size) == S_OK) {
						IStream* pStream = NULL;
						if(::CreateStreamOnHGlobal(hBuffer, FALSE, &pStream) == S_OK) 	{
							image = Image::FromStream(pStream,false);
							pStream->Release();
						}
					}
					::GlobalUnlock(hBuffer);
				}
				::GlobalFree(hBuffer);
			}
			in->Release();
		}
	}
	if (image && image->GetLastStatus() != Ok) {
		delete image;
		image = NULL;
	}
	return image;
}

RectF *getBounds(Image *image)
{
	RectF srcRect;
	Unit srcUnit;
	image->GetBounds(&srcRect, &srcUnit);
	REAL dpix = image->GetHorizontalResolution();
	REAL dpiy = image->GetVerticalResolution();

	// ピクセルに変換
	REAL x, y, width, height;
	switch (srcUnit) {
	case UnitPoint:		// 3 -- Each unit is a printer's point, or 1/72 inch.
		x = srcRect.X * dpix / 72;
		y = srcRect.Y * dpiy / 72;
		width  = srcRect.Width * dpix / 72;
		height = srcRect.Height * dpix / 72;
		break;
	case UnitInch:       // 4 -- Each unit is 1 inch.
		x = srcRect.X * dpix;
		y = srcRect.Y * dpiy;
		width  = srcRect.Width * dpix;
		height = srcRect.Height * dpix;
		break;
	case UnitDocument:   // 5 -- Each unit is 1/300 inch.
		x = srcRect.X * dpix / 300;
		y = srcRect.Y * dpiy / 300;
		width  = srcRect.Width * dpix / 300;
		height = srcRect.Height * dpix / 300;
		break;
	case UnitMillimeter: // 6 -- Each unit is 1 millimeter.
		x = srcRect.X * dpix / 25.4F;
		y = srcRect.Y * dpiy / 25.4F;
		width  = srcRect.Width * dpix / 25.4F;
		height = srcRect.Height * dpix / 25.4F;
		break;
	default:
		x = srcRect.X;
		y = srcRect.Y;
		width  = srcRect.Width;
		height = srcRect.Height;
		break;
	}
	return new RectF(x, y, width, height);
}

// --------------------------------------------------------
// フォント情報
// --------------------------------------------------------

/**
 * プライベートフォントの追加
 * @param fontFileName フォントファイル名
 */
void
GdiPlus::addPrivateFont(const tjs_char *fontFileName)
{
	if (!privateFontCollection) {
		privateFontCollection = new PrivateFontCollection();
	}
	ttstr filename = TVPGetPlacedPath(fontFileName);
	if (filename.length()) {
		if (!wcschr(filename.c_str(), '>')) {
			TVPGetLocalName(filename);
			privateFontCollection->AddFontFile(filename.c_str());
			return;
		} else {
			IStream *in = TVPCreateIStream(filename, TJS_BS_READ);
			if (in) {
				STATSTG stat;
				in->Stat(&stat, STATFLAG_NONAME);
				ULONG size = (ULONG)stat.cbSize.QuadPart;
				char *data = new char[size];
				if (in->Read(data, size, &size) == S_OK) {
					privateFontCollection->AddMemoryFont(data, size);
					fontDatas.push_back(data);					
				} else {
					delete[] data;
				}
				in->Release();
				return;
			}
		}
	}
	TVPThrowExceptionMessage(L"cannot open:%1", fontFileName);
}

/**
 * 配列にフォントのファミリー名を格納
 * @param array 格納先配列
 * @param fontCollection フォント名を取得する元の FontCollection
 */
static void addFontFamilyName(iTJSDispatch2 *array, FontCollection *fontCollection)
{
	int count = fontCollection->GetFamilyCount();
	FontFamily *families = new FontFamily[count];
	if (families) {
		fontCollection->GetFamilies(count, families, &count);
		for (int i=0;i<count;i++) {
			WCHAR familyName[LF_FACESIZE];
			if (families[i].GetFamilyName(familyName) == Ok) {
				tTJSVariant name(familyName), *param = &name;
				array->FuncCall(0, TJS_W("add"), NULL, 0, 1, &param, array);
			}
		}
		delete families;
	}
}

/**
 * フォント一覧の取得
 * @param privateOnly true ならプライベートフォントのみ取得
 */
tTJSVariant
GdiPlus::getFontList(bool privateOnly)
{
	iTJSDispatch2 *array = TJSCreateArrayObject();
	if (privateFontCollection)	{
		addFontFamilyName(array, privateFontCollection);
	}
	if (!privateOnly) {
		InstalledFontCollection installedFontCollection;
		addFontFamilyName(array, &installedFontCollection);
	}
	tTJSVariant ret(array,array);
	array->Release();
	return ret;
}

// --------------------------------------------------------
// フォント情報
// --------------------------------------------------------

/**
 * コンストラクタ
 */
FontInfo::FontInfo() : fontFamily(NULL), emSize(12), style(0), gdiPlusUnsupportedFont(false), forceSelfPathDraw(false), propertyModified(true) {}

/**
 * コンストラクタ
 * @param familyName フォントファミリー
 * @param emSize フォントのサイズ
 * @param style フォントスタイル
 */
FontInfo::FontInfo(const tjs_char *familyName, REAL emSize, INT style) : fontFamily(NULL), gdiPlusUnsupportedFont(false), forceSelfPathDraw(false), propertyModified(true)
{
	setFamilyName(familyName);
	setEmSize(emSize);
	setStyle(style);
}

/**
 * コピーコンストラクタ
 */
FontInfo::FontInfo(const FontInfo &orig)
{
	fontFamily = orig.fontFamily ? orig.fontFamily->Clone() : NULL;
	emSize = orig.emSize;
	style = orig.style;
}

/**
 * デストラクタ
 */
FontInfo::~FontInfo()
{
	clear();
}

/**
 * フォント情報のクリア
 */
void
FontInfo::clear()
{
	delete fontFamily;
	fontFamily = NULL;
	familyName = "";
        gdiPlusUnsupportedFont = false;
        propertyModified = true;
}


/**
 * フォントの指定
 */
void
FontInfo::setFamilyName(const tjs_char *familyName)
{
  propertyModified = true;

  if (forceSelfPathDraw) {
    clear();
    gdiPlusUnsupportedFont = true;
    this->familyName = familyName;
    return;
  }

	if (familyName) {
		clear();
		if (privateFontCollection) {
			fontFamily = new FontFamily(familyName, privateFontCollection);
			if (fontFamily->IsAvailable()) {
				this->familyName = familyName;
				return;
			} else {
				clear();
			}
		}
		fontFamily = new FontFamily(familyName);
		if (fontFamily->IsAvailable()) {
			this->familyName = familyName;
			return;
		} else {
                  clear();
                  gdiPlusUnsupportedFont = true;
                  this->familyName = familyName;
		}
	}
}

void
FontInfo::setForceSelfPathDraw(bool state)
{
  forceSelfPathDraw = state;
  this->setFamilyName(familyName.c_str());
}

bool
FontInfo::getForceSelfPathDraw(void) const
{
  return forceSelfPathDraw;
}

bool
FontInfo::getSelfPathDraw(void) const
{
  return forceSelfPathDraw || gdiPlusUnsupportedFont;
}

OUTLINETEXTMETRIC *
FontInfo::createFontMetric(void) const
{
  HDC dc = ::CreateCompatibleDC(NULL);
  if (dc == NULL)
    return NULL;
  LOGFONT font;
  memset(&font, 0, sizeof(font));
  font.lfHeight = LONG(-emSize);
  font.lfWeight = (style & 1) ? FW_BOLD : FW_REGULAR;
  font.lfItalic = style & 2;
  font.lfUnderline = style & 4;
  font.lfStrikeOut = style & 8;
  font.lfCharSet = DEFAULT_CHARSET;
  wcscpy_s(font.lfFaceName, familyName.c_str());
  HFONT hFont = CreateFontIndirect(&font);
  if (hFont == NULL) {
    DeleteObject(dc);
    return NULL;
  }
  HGDIOBJ hOldFont = SelectObject(dc, hFont);

  int size = ::GetOutlineTextMetrics(dc, 0, NULL);
  if (size > 0) {
    char *buf = new char[size];
    if (::GetOutlineTextMetrics(dc, size, reinterpret_cast<OUTLINETEXTMETRIC*>(buf))) {
      SelectObject(dc, hOldFont);
      DeleteObject(hFont);
      DeleteObject(dc);

      return reinterpret_cast<OUTLINETEXTMETRIC*>(buf);
    }
    delete[] buf;
  }

  SelectObject(dc, hOldFont);
  DeleteObject(hFont);
  DeleteObject(dc);
  return NULL;
}

void
FontInfo::updateSizeParams(void) const
{
  if (! propertyModified)
    return;

  propertyModified = false;
  ascent = 0;
  descent = 0;
  ascentLeading = 0;
  descentLeading = 0;
  lineSpacing = 0;

  OUTLINETEXTMETRIC *otm = createFontMetric();
  if (otm) {
    ascent = REAL(otm->otmTextMetrics.tmAscent);
    descent = REAL(otm->otmTextMetrics.tmDescent);
    ascentLeading = ascent - REAL(otm->otmAscent);
    descentLeading = descent - REAL(- otm->otmDescent);
    lineSpacing = REAL(otm->otmTextMetrics.tmHeight);
    delete otm;
  }
}

REAL 
FontInfo::getAscent() const
{
  this->updateSizeParams();
  return ascent;
}


REAL 
FontInfo::getDescent() const
{
  this->updateSizeParams();
  return descent;
}

REAL 
FontInfo::getAscentLeading() const
{
  this->updateSizeParams();
  return ascentLeading;
}


REAL 
FontInfo::getDescentLeading() const
{
  this->updateSizeParams();
  return descentLeading;
}

REAL 
FontInfo::getLineSpacing() const
{
  this->updateSizeParams();
  return lineSpacing;
}

// --------------------------------------------------------
// アピアランス情報
// --------------------------------------------------------

Appearance::Appearance() {}

Appearance::~Appearance()
{
	clear();
}

/**
 * 情報のクリア
 */
void
Appearance::clear()
{
	drawInfos.clear();

	// customLineCapsも削除
	vector<CustomLineCap*>::const_iterator i = customLineCaps.begin();
	while (i != customLineCaps.end()) {
		delete *i;
		i++;
	}
	customLineCaps.clear();
}


// --------------------------------------------------------
// 各型変換処理
// --------------------------------------------------------

extern bool IsArray(const tTJSVariant &var);

/**
 * 嵗昗忣曬偺惗惉
 */
extern PointF getPoint(const tTJSVariant &var);

/**
 * 揰偺攝楍傪庢摼
 */
static void getPoints(const tTJSVariant &var, vector<PointF> &points)
{
	ncbPropAccessor info(var);
	int c = info.GetArrayCount();
	for (int i=0;i<c;i++) {
		tTJSVariant p;
		if (info.checkVariant(i, p)) {
			points.push_back(getPoint(p));
		}
	}
}

static void getPoints(ncbPropAccessor &info, int n, vector<PointF> &points)
{
	tTJSVariant var;
	if (info.checkVariant(n, var)) {
		getPoints(var, points);
	}
}

static void getPoints(ncbPropAccessor &info, const tjs_char *n, vector<PointF> &points)
{
	tTJSVariant var;
	if (info.checkVariant(n, var)) {
		getPoints(var, points);
	}
}

// -----------------------------

/**
 * 嬮宍忣曬偺惗惉
 */
extern RectF getRect(const tTJSVariant &var);

/**
 * 嬮宍偺攝楍傪庢摼
 */
static void getRects(const tTJSVariant &var, vector<RectF> &rects)
{
	ncbPropAccessor info(var);
	int c = info.GetArrayCount();
	for (int i=0;i<c;i++) {
		tTJSVariant p;
		if (info.checkVariant(i, p)) {
			rects.push_back(getRect(p));
		}
	}
}

// -----------------------------

/**
 * 幚悢偺攝楍傪庢摼
 */
static void getReals(const tTJSVariant &var, vector<REAL> &points)
{
	ncbPropAccessor info(var);
	int c = info.GetArrayCount();
	for (int i=0;i<c;i++) {
		points.push_back((REAL)info.getRealValue(i));
	}
}

static void getReals(ncbPropAccessor &info, int n, vector<REAL> &points)
{
	tTJSVariant var;
	if (info.checkVariant(n, var)) {
		getReals(var, points);
	}
}

static void getReals(ncbPropAccessor &info, const tjs_char *n, vector<REAL> &points)
{
	tTJSVariant var;
	if (info.checkVariant(n, var)) {
		getReals(var, points);
	}
}

// -----------------------------

/**
 * 怓偺攝楍傪庢摼
 */
static void getColors(const tTJSVariant &var, vector<Color> &colors)
{
	ncbPropAccessor info(var);
	int c = info.GetArrayCount();
	for (int i=0;i<c;i++) {
		colors.push_back(Color((ARGB)info.getIntValue(i)));
	}
}

static void getColors(ncbPropAccessor &info, int n, vector<Color> &colors)
{
	tTJSVariant var;
	if (info.checkVariant(n, var)) {
		getColors(var, colors);
	}
}

static void getColors(ncbPropAccessor &info, const tjs_char *n, vector<Color> &colors)
{
	tTJSVariant var;
	if (info.checkVariant(n, var)) {
		getColors(var, colors);
	}
}

template <class T>
void commonBrushParameter(ncbPropAccessor &info, T *brush)
{
	tTJSVariant var;
	// SetBlend
	if (info.checkVariant(L"blend", var)) {
		vector<REAL> factors;
		vector<REAL> positions;
		ncbPropAccessor binfo(var);
		if (IsArray(var)) {
			getReals(binfo, 0, factors);
			getReals(binfo, 1, positions);
		} else {
			getReals(binfo, L"blendFactors", factors);
			getReals(binfo, L"blendPositions", positions);
		}
		int count = (int)factors.size();
		if ((int)positions.size() > count) {
			count = (int)positions.size();
		}
		if (count > 0) {
			brush->SetBlend(&factors[0], &positions[0], count);
		}
	}
	// SetBlendBellShape
	if (info.checkVariant(L"blendBellShape", var)) {
		ncbPropAccessor sinfo(var);
		if (IsArray(var)) {
			brush->SetBlendBellShape((REAL)sinfo.getRealValue(0),
									 (REAL)sinfo.getRealValue(1));
		} else {
			brush->SetBlendBellShape((REAL)info.getRealValue(L"focus"),
									 (REAL)info.getRealValue(L"scale"));
		}
	}
	// SetBlendTriangularShape
	if (info.checkVariant(L"blendTriangularShape", var)) {
		ncbPropAccessor sinfo(var);
		if (IsArray(var)) {
			brush->SetBlendTriangularShape((REAL)sinfo.getRealValue(0),
										   (REAL)sinfo.getRealValue(1));
		} else {
			brush->SetBlendTriangularShape((REAL)info.getRealValue(L"focus"),
										   (REAL)info.getRealValue(L"scale"));
		}
	}
	// SetGammaCorrection
	if (info.checkVariant(L"useGammaCorrection", var)) {
		brush->SetGammaCorrection((BOOL)var);
	}
	// SetInterpolationColors
	if (info.checkVariant(L"interpolationColors", var)) {
		vector<Color> colors;
		vector<REAL> positions;
		ncbPropAccessor binfo(var);
		if (IsArray(var)) {
			getColors(binfo, 0, colors);
			getReals(binfo, 1, positions);
		} else {
			getColors(binfo, L"presetColors", colors);
			getReals(binfo, L"blendPositions", positions);
		}
		int count = (int)colors.size();
		if ((int)positions.size() > count) {
			count = (int)positions.size();
		}
		if (count > 0) {
			brush->SetInterpolationColors(&colors[0], &positions[0], count);
		}
	}
}

/**
 * 僽儔僔偺惗惉
 */
Brush* createBrush(const tTJSVariant colorOrBrush)
{
	Brush *brush;
	if (colorOrBrush.Type() != tvtObject) {
		brush = new SolidBrush(Color((tjs_int)colorOrBrush));
	} else {
		// 庬暿偛偲偵嶌傝暘偗傞
		ncbPropAccessor info(colorOrBrush);
		BrushType type = (BrushType)info.getIntValue(L"type", BrushTypeSolidColor);
		switch (type) {
		case BrushTypeSolidColor:
			brush = new SolidBrush(Color((ARGB)info.getIntValue(L"color", 0xffffffff)));
			break;
		case BrushTypeHatchFill:
			brush = new HatchBrush((HatchStyle)info.getIntValue(L"hatchStyle", HatchStyleHorizontal),
								   Color((ARGB)info.getIntValue(L"foreColor", 0xffffffff)),
								   Color((ARGB)info.getIntValue(L"backColor", 0xff000000)));
			break;
		case BrushTypeTextureFill:
			{
				ttstr imgname = info.GetValue(L"image", ncbTypedefs::Tag<ttstr>());
				Image *image = loadImage(imgname.c_str());
				if (image) {
					WrapMode wrapMode = (WrapMode)info.getIntValue(L"wrapMode", WrapModeTile);
					tTJSVariant dstRect;
					if (info.checkVariant(L"dstRect", dstRect)) {
						brush = new TextureBrush(image, wrapMode, getRect(dstRect));
					} else {
						brush = new TextureBrush(image, wrapMode);
					}
					delete image;
				}
				break;
			}
		case BrushTypePathGradient:
			{
				PathGradientBrush *pbrush;
				vector<PointF> points;
				getPoints(info, L"points", points);
				if ((int)points.size() == 0) TVPThrowExceptionMessage(L"must set poins");
				WrapMode wrapMode = (WrapMode)info.getIntValue(L"wrapMode", WrapModeTile);
				pbrush = new PathGradientBrush(&points[0], (int)points.size(), wrapMode);

				// 嫟捠僷儔儊乕僞
				commonBrushParameter(info, pbrush);

				tTJSVariant var;
				//SetCenterColor
				if (info.checkVariant(L"centerColor", var)) {
					pbrush->SetCenterColor(Color((ARGB)(tjs_int)var));
				}
				//SetCenterPoint
				if (info.checkVariant(L"centerPoint", var)) {
					pbrush->SetCenterPoint(getPoint(var));
				}
				//SetFocusScales
				if (info.checkVariant(L"focusScales", var)) {
					ncbPropAccessor sinfo(var);
					if (IsArray(var)) {
						pbrush->SetFocusScales((REAL)sinfo.getRealValue(0),
											   (REAL)sinfo.getRealValue(1));
					} else {
						pbrush->SetFocusScales((REAL)info.getRealValue(L"xScale"),
											   (REAL)info.getRealValue(L"yScale"));
					}
				}
				//SetSurroundColors
				if (info.checkVariant(L"surroundColors", var)) {
					vector<Color> colors;
					getColors(var, colors);
					int size = (int)colors.size();
					pbrush->SetSurroundColors(&colors[0], &size);
				}
				brush = pbrush;
			}
			break;
		case BrushTypeLinearGradient:
			{
				LinearGradientBrush *lbrush;
				Color color1((ARGB)info.getIntValue(L"color1", 0));
				Color color2((ARGB)info.getIntValue(L"color2", 0));

				tTJSVariant var;
				if (info.checkVariant(L"point1", var)) {
					PointF point1 = getPoint(var);
					info.checkVariant(L"point2", var);
					PointF point2 = getPoint(var);
					lbrush = new LinearGradientBrush(point1, point2, color1, color2);
				} else if (info.checkVariant(L"rect", var)) {
					RectF rect = getRect(var);
					if (info.HasValue(L"angle")) {
						// 傾儞僌儖巜掕偑偁傞応崌
						lbrush = new LinearGradientBrush(rect, color1, color2,
														 (REAL)info.getRealValue(L"angle", 0),
														 (BOOL)info.getIntValue(L"isAngleScalable", 0));
					} else {
						// 柍偄応崌偼儌乕僪傪嶲徠
						lbrush = new LinearGradientBrush(rect, color1, color2,
														 (LinearGradientMode)info.getIntValue(L"mode", LinearGradientModeHorizontal));
					}
				} else {
					TVPThrowExceptionMessage(L"must set point1,2 or rect");
				}

				// 嫟捠僷儔儊乕僞
				commonBrushParameter(info, lbrush);

				// SetWrapMode
				if (info.checkVariant(L"wrapMode", var)) {
					lbrush->SetWrapMode((WrapMode)(tjs_int)var);
				}
				brush = lbrush;
			}
			break;
		default:
			TVPThrowExceptionMessage(L"invalid brush type");
			break;
		}
	}
	return brush;
}

/**
 * 僽儔僔偺捛壛
 * @param colorOrBrush ARGB怓巜掕傑偨偼僽儔僔忣曬乮帿彂乯
 * @param ox 昞帵僆僼僙僢僩X
 * @param oy 昞帵僆僼僙僢僩Y
 */
void
Appearance::addBrush(tTJSVariant colorOrBrush, REAL ox, REAL oy)
{
	drawInfos.push_back(DrawInfo(ox, oy, createBrush(colorOrBrush)));
}

/**
 * 儁儞偺捛壛
 * @param colorOrBrush ARGB怓巜掕傑偨偼僽儔僔忣曬乮帿彂乯
 * @param widthOrOption 儁儞暆傑偨偼儁儞忣曬乮帿彂乯
 * @param ox 昞帵僆僼僙僢僩X
 * @param oy 昞帵僆僼僙僢僩Y
 */
void
Appearance::addPen(tTJSVariant colorOrBrush, tTJSVariant widthOrOption, REAL ox, REAL oy)
{
	Pen *pen;
	REAL width = 1.0;
	if (colorOrBrush.Type() == tvtObject) {
		Brush *brush = createBrush(colorOrBrush);
		pen = new Pen(brush, width);
		delete brush;
	} else {
		pen = new Pen(Color((ARGB)(tjs_int)colorOrBrush), width);
	}
	if (widthOrOption.Type() != tvtObject) {
		pen->SetWidth((REAL)(tjs_real)widthOrOption);
	} else {
		ncbPropAccessor info(widthOrOption);
		REAL penWidth = 1.0;
		tTJSVariant var;

		// SetWidth
		if (info.checkVariant(L"width", var)) {
			penWidth = (REAL)(tjs_real)var;
		}
		pen->SetWidth(penWidth);

		// SetAlignment
		if (info.checkVariant(L"alignment", var)) {
			pen->SetAlignment((PenAlignment)(tjs_int)var);
		}
		// SetCompoundArray
		if (info.checkVariant(L"compoundArray", var)) {
			vector<REAL> reals;
			getReals(var, reals);
			pen->SetCompoundArray(&reals[0], (int)reals.size());
		}

		// SetDashCap
		if (info.checkVariant(L"dashCap", var)) {
			pen->SetDashCap((DashCap)(tjs_int)var);
		}
		// SetDashOffset
		if (info.checkVariant(L"dashOffset", var)) {
			pen->SetDashOffset((REAL)(tjs_real)var);
		}

		// SetDashStyle
		// SetDashPattern
		if (info.checkVariant(L"dashStyle", var)) {
			if (IsArray(var)) {
				vector<REAL> reals;
				getReals(var, reals);
				pen->SetDashStyle(DashStyleCustom);
				pen->SetDashPattern(&reals[0], (int)reals.size());
			} else {
				pen->SetDashStyle((DashStyle)(tjs_int)var);
			}
		}

		// SetStartCap
		// SetCustomStartCap
		if (info.checkVariant(L"startCap", var)) {
			LineCap cap = LineCapFlat;
			CustomLineCap *custom = NULL;
			if (getLineCap(var, cap, custom, penWidth)) {
				if (custom != NULL) pen->SetCustomStartCap(custom);
				else                pen->SetStartCap(cap);
			}
		}

		// SetEndCap
		// SetCustomEndCap
		if (info.checkVariant(L"endCap", var)) {
			LineCap cap = LineCapFlat;
			CustomLineCap *custom = NULL;
			if (getLineCap(var, cap, custom, penWidth)) {
				if (custom != NULL) pen->SetCustomEndCap(custom);
				else                pen->SetEndCap(cap);
			}
		}

		// SetLineJoin
		if (info.checkVariant(L"lineJoin", var)) {
			pen->SetLineJoin((LineJoin)(tjs_int)var);
		}
		
		// SetMiterLimit
		if (info.checkVariant(L"miterLimit", var)) {
			pen->SetMiterLimit((REAL)(tjs_real)var);
		}
	}
	drawInfos.push_back(DrawInfo(ox, oy, pen));
}

bool
Appearance::getLineCap(tTJSVariant &in, LineCap &cap, CustomLineCap* &custom, REAL pw)
{
	switch (in.Type()) {
	case tvtVoid:
	case tvtInteger:
		cap = (LineCap)(tjs_int)in;
		break;
	case tvtObject:
		{
			ncbPropAccessor info(in);
			REAL width = pw, height = pw;
			tTJSVariant var;
			if (info.checkVariant(L"width",  var)) width  = (REAL)(tjs_real)var;
			if (info.checkVariant(L"height", var)) height = (REAL)(tjs_real)var;
			BOOL filled = (BOOL)info.getIntValue(L"filled", 1);
			AdjustableArrowCap *arrow = new AdjustableArrowCap(height, width, filled);
			if (info.checkVariant(L"middleInset", var))
				arrow->SetMiddleInset((REAL)(tjs_real)var);
			customLineCaps.push_back((custom = static_cast<CustomLineCap*>(arrow)));
		}
		break;
	default: return false;
	}
	return true;
}


extern void getPoints(const tTJSVariant& var, vector<PointF>& points);
extern void getRects(const tTJSVariant& var, vector<RectF>& rects);

Path::Path()
{
}

Path::~Path()
{
}

/**
 * 現在の図形を閉じずに次の図形を開始します
 */
void Path::startFigure()
{
    path.StartFigure();
}

/**
 * 現在の図形を閉じます
 */
void Path::closeFigure()
{
    path.CloseFigure();
}

/**
 * 円弧の描画
 * @param x 左上座標
 * @param y 左上座標
 * @param width 横幅
 * @param height 縦幅
 * @param startAngle 時計方向円弧開始位置
 * @param sweepAngle 描画角度
 */
void Path::drawArc(REAL x, REAL y, REAL width, REAL height, REAL startAngle, REAL sweepAngle)
{
    path.AddArc(x, y, width, height, startAngle, sweepAngle);
}

/**
 * ベジェ曲線の描画
 * @param app アピアランス
 * @param x1
 * @param y1
 * @param x2
 * @param y2
 * @param x3
 * @param y3
 * @param x4
 * @param y4
 */
void Path::drawBezier(REAL x1, REAL y1, REAL x2, REAL y2, REAL x3, REAL y3, REAL x4, REAL y4)
{
    path.AddBezier(x1, y1, x2, y2, x3, y3, x4, y4);
}

/**
 * 連続ベジェ曲線の描画
 * @param app アピアランス
 * @param points 点の配列
 */
void Path::drawBeziers(tTJSVariant points)
{
    vector<PointF> ps;
    getPoints(points, ps);
    path.AddBeziers(&ps[0], (int)ps.size());
}

/**
 * Closed cardinal spline の描画
 * @param app アピアランス
 * @param points 点の配列
 */
void Path::drawClosedCurve(tTJSVariant points)
{
    vector<PointF> ps;
    getPoints(points, ps);
    path.AddClosedCurve(&ps[0], (int)ps.size());
}

/**
 * Closed cardinal spline の描画
 * @param app アピアランス
 * @param points 点の配列
 * @pram tension tension
 */
void Path::drawClosedCurve2(tTJSVariant points, REAL tension)
{
    vector<PointF> ps;
    getPoints(points, ps);
    path.AddClosedCurve(&ps[0], (int)ps.size(), tension);
}

/**
 * cardinal spline の描画
 * @param app アピアランス
 * @param points 点の配列
 */
void Path::drawCurve(tTJSVariant points)
{
    vector<PointF> ps;
    getPoints(points, ps);
    path.AddCurve(&ps[0], (int)ps.size());
}

/**
 * cardinal spline の描画
 * @param app アピアランス
 * @param points 点の配列
 * @parma tension tension
 */
void Path::drawCurve2(tTJSVariant points, REAL tension)
{
    vector<PointF> ps;
    getPoints(points, ps);
    path.AddCurve(&ps[0], (int)ps.size(), tension);
}

/**
 * cardinal spline の描画
 * @param app アピアランス
 * @param points 点の配列
 * @param offset
 * @param numberOfSegments
 * @param tension tension
 */
void Path::drawCurve3(tTJSVariant points, int offset, int numberOfSegments, REAL tension)
{
    vector<PointF> ps;
    getPoints(points, ps);
    path.AddCurve(&ps[0], (int)ps.size(), offset, numberOfSegments, tension);
}

/**
 * 円錐の描画
 * @param x 左上座標
 * @param y 左上座標
 * @param width 横幅
 * @param height 縦幅
 * @param startAngle 時計方向円弧開始位置
 * @param sweepAngle 描画角度
 */
void Path::drawPie(REAL x, REAL y, REAL width, REAL height, REAL startAngle, REAL sweepAngle)
{
    path.AddPie(x, y, width, height, startAngle, sweepAngle);
}

/**
 * 楕円の描画
 * @param app アピアランス
 * @param x
 * @param y
 * @param width
 * @param height
 */
void Path::drawEllipse(REAL x, REAL y, REAL width, REAL height)
{
    path.AddEllipse(x, y, width, height);
}

/**
 * 線分の描画
 * @param app アピアランス
 * @param x1 始点X座標
 * @param y1 始点Y座標
 * @param x2 終点X座標
 * @param y2 終点Y座標
 */
void Path::drawLine(REAL x1, REAL y1, REAL x2, REAL y2)
{
    path.AddLine(x1, y1, x2, y2);
}

/**
 * 連続線分の描画
 * @param app アピアランス
 * @param points 点の配列
 */
void Path::drawLines(tTJSVariant points)
{
    vector<PointF> ps;
    getPoints(points, ps);
    path.AddLines(&ps[0], (int)ps.size());
}

/**
 * 多角形の描画
 * @param app アピアランス
 * @param points 点の配列

 */
void Path::drawPolygon(tTJSVariant points)
{
    vector<PointF> ps;
    getPoints(points, ps);
    path.AddPolygon(&ps[0], (int)ps.size());
}

/**
 * 矩形の描画
 * @param app アピアランス
 * @param x
 * @param y
 * @param width
 * @param height
 */
void Path::drawRectangle(REAL x, REAL y, REAL width, REAL height)
{
    RectF rect(x, y, width, height);
    path.AddRectangle(rect);
}

/**
 * 複数矩形の描画
 * @param app アピアランス
 * @param rects 矩形情報の配列
 */
void Path::drawRectangles(tTJSVariant rects)
{
    vector<RectF> rs;
    getRects(rects, rs);
    path.AddRectangles(&rs[0], (int)rs.size());
}

// --------------------------------------------------------
// 僼僅儞僩昤夋宯
// --------------------------------------------------------

void
LayerExDraw::updateRect(RectF &rect)
{
	if (updateWhenDraw) {
		// 峏怴張棟
		//tTJSVariant  vars [4] = { rect.X, rect.Y, rect.Width, rect.Height };
		//tTJSVariant *varsp[4] = { vars, vars+1, vars+2, vars+3 };
		//_pUpdate(4, varsp);
		tTVPRect rc(rect.X, rect.Y, rect.X + rect.Width, rect.Y + rect.Height);
		_this->Update(rc);
	}
}

/**
 * 僐儞僗僩儔僋僞
 */
LayerExDraw::LayerExDraw(DispatchT obj)
	: layerExBase_GL(obj), width(-1), height(-1), pitch(0), buffer(NULL), bitmap(NULL), graphics(NULL),
	  //_pClipLeft(  obj, TJS_W("clipLeft")),
	  //_pClipTop(   obj, TJS_W("clipTop")),
	  //_pClipWidth( obj, TJS_W("clipWidth")),
	  //_pClipHeight(obj, TJS_W("clipHeight")),
	  clipLeft(-1), clipTop(-1), clipWidth(-1), clipHeight(-1),
	  smoothingMode(SmoothingModeAntiAlias), textRenderingHint(TextRenderingHintAntiAlias),
	  metaHDC(NULL), metaBuffer(NULL), metaStream(NULL), metafile(NULL), metaGraphics(NULL),
	  updateWhenDraw(true)
{
	metaHDC = ::CreateCompatibleDC(NULL);
}

/**
 * 僨僗僩儔僋僞
 */
LayerExDraw::~LayerExDraw()
{
	destroyRecord();
	delete graphics;
	delete bitmap;
	if (metaHDC) {
		DeleteObject(metaHDC);
		metaHDC = NULL;
	}
}

void
LayerExDraw::reset()
{
	layerExBase_GL::reset();
	if (!(graphics &&
		  width  == _width &&
		  height == _height &&
		  pitch  == _pitch &&
		  buffer == _buffer)) {
		delete graphics;
		delete bitmap;
		width  = _width;
		height = _height;
		pitch  = _pitch;
		buffer = _buffer;
		bitmap = new Bitmap(width, height, pitch, PixelFormat32bppARGB, (unsigned char*)buffer);
		graphics = new Graphics(bitmap);
		graphics->SetCompositingMode(CompositingModeSourceOver);
		graphics->SetTransform(&calcTransform);
		clipWidth = clipHeight = -1;
	}
	if (_clipLeft != clipLeft ||
		_clipTop  != clipTop  ||
		_clipWidth != clipWidth ||
		_clipHeight != clipHeight) {
		clipLeft = _clipLeft;
		clipTop  = _clipTop;
		clipWidth = _clipWidth;
		clipHeight = _clipHeight;
		Region clip(Rect(clipLeft, clipTop, clipWidth, clipHeight));
		graphics->SetClip(&clip);
	}
}

void
LayerExDraw::updateViewTransform()
{
	calcTransform.Reset();
	calcTransform.Multiply(&transform, MatrixOrderAppend);
	calcTransform.Multiply(&viewTransform, MatrixOrderAppend);
	graphics->SetTransform(&calcTransform);
	redrawRecord();
}

/**
 * 昞帵僩儔儞僗僼僅乕儉偺巜掕
 * @param matrix 僩儔儞僗僼僅乕儉儅僩儕僢僋僗
 */
void
LayerExDraw::setViewTransform(const Matrix *trans)
{
	if (!viewTransform.Equals(trans)) {
		viewTransform.Reset();
		viewTransform.Multiply(trans);
		updateViewTransform();
	}
}

void
LayerExDraw::resetViewTransform()
{
	viewTransform.Reset();
	updateViewTransform();
}

void
LayerExDraw::rotateViewTransform(REAL angle)
{
	viewTransform.Rotate(angle, MatrixOrderAppend);
	updateViewTransform();
}

void
LayerExDraw::scaleViewTransform(REAL sx, REAL sy)
{
	viewTransform.Scale(sx, sy, MatrixOrderAppend);
	updateViewTransform();
}

void
LayerExDraw::translateViewTransform(REAL dx, REAL dy)
{
	viewTransform.Scale(dx, dy, MatrixOrderAppend);
	updateViewTransform();
}

void
LayerExDraw::updateTransform()
{
	calcTransform.Reset();
	calcTransform.Multiply(&transform, MatrixOrderAppend);
	calcTransform.Multiply(&viewTransform, MatrixOrderAppend);
	graphics->SetTransform(&calcTransform);
	if (metaGraphics) {
		metaGraphics->SetTransform(&transform);
	}
}

/**
 * 僩儔儞僗僼僅乕儉偺巜掕
 * @param matrix 僩儔儞僗僼僅乕儉儅僩儕僢僋僗
 */
void
LayerExDraw::setTransform(const Matrix *trans)
{
	if (!transform.Equals(trans)) {
		transform.Reset();
		transform.Multiply(trans);
		updateTransform();
	}
}

void
LayerExDraw::resetTransform()
{
	transform.Reset();
	updateTransform();
}

void
LayerExDraw::rotateTransform(REAL angle)
{
	transform.Rotate(angle, MatrixOrderAppend);
	updateTransform();
}

void
LayerExDraw::scaleTransform(REAL sx, REAL sy)
{
	transform.Scale(sx, sy, MatrixOrderAppend);
	updateTransform();
}

void
LayerExDraw::translateTransform(REAL dx, REAL dy)
{
	transform.Scale(dx, dy, MatrixOrderAppend);
	updateTransform();
}

/**
 * 夋柺偺徚嫀
 * @param argb 徚嫀怓
 */
void
LayerExDraw::clear(ARGB argb)
{
	graphics->Clear(Color(argb));
	if (metaGraphics) {
		createRecord();
		metaGraphics->Clear(Color(argb));
	}
	//_pUpdate(0, NULL);
	_this->Update();
}


/**
 * パスの描画
 * @param app アピアランス
 * @param path パス
 */
RectF LayerExDraw::drawPath(const Appearance* app, const Path* path)
{
    return _drawPath(app, &path->path);
}

/**
 * 僷僗偺椞堟忣曬傪庢摼
 * @param app 昞帵昞尰
 * @param path 昤夋偡傞僷僗
 */
RectF
LayerExDraw::getPathExtents(const Appearance *app, const GraphicsPath *path)
{
	// 椞堟婰榐梡
	RectF rect;

	// 昤夋忣曬傪巊偭偰師乆昤夋
	bool first = true;
	vector<Appearance::DrawInfo>::const_iterator i = app->drawInfos.begin();
	while (i != app->drawInfos.end()) {
		if (i->info) {
			Matrix matrix(1,0,0,1,i->ox,i->oy);
			matrix.Multiply(&calcTransform, MatrixOrderAppend);
			switch (i->type) {
			case 0:
				{
					Pen *pen = (Pen*)i->info;
					if (first) {
						path->GetBounds(&rect, &matrix, pen);
						first = false;
					} else {
						RectF r;
						path->GetBounds(&r, &matrix, pen);
						rect.Union(rect, rect, r);
					}
				}
				break;
			case 1:
				if (first) {
					path->GetBounds(&rect, &matrix, NULL);
					first = false;
				} else {
					RectF r;
					path->GetBounds(&r, &matrix, NULL);
					rect.Union(rect, rect, r);
				}
				break;
			}
		}
		i++;
	}
	return rect;
}

void
LayerExDraw::draw(Graphics *graphics, const Pen *pen, const Matrix *matrix, const GraphicsPath *path)
{
	GraphicsContainer container = graphics->BeginContainer();
	graphics->MultiplyTransform(matrix);
	graphics->SetSmoothingMode(smoothingMode);
	graphics->DrawPath(pen, path);
	graphics->EndContainer(container);
}

void
LayerExDraw::fill(Graphics *graphics, const Brush *brush, const Matrix *matrix, const GraphicsPath *path)
{
	GraphicsContainer container = graphics->BeginContainer();
	graphics->MultiplyTransform(matrix);
	graphics->SetSmoothingMode(smoothingMode);
	graphics->FillPath(brush, path);
	graphics->EndContainer(container);
}

/**
 * 僷僗傪昤夋偡傞
 * @param app 昞帵昞尰
 * @param path 昤夋偡傞僷僗
 * @return 峏怴椞堟忣曬
 */
RectF
LayerExDraw::_drawPath(const Appearance *app, const GraphicsPath *path)
{
	// 椞堟婰榐梡
	RectF rect;

	// 昤夋忣曬傪巊偭偰師乆昤夋
	bool first = true;
	vector<Appearance::DrawInfo>::const_iterator i = app->drawInfos.begin();
	while (i != app->drawInfos.end()) {
		if (i->info) {
			Matrix matrix(1,0,0,1,i->ox,i->oy);
			switch (i->type) {
			case 0:
				{
					Pen *pen = (Pen*)i->info;
					draw(graphics, pen, &matrix, path);
					if (metaGraphics) {
						draw(metaGraphics, pen, &matrix, path);
					}
					matrix.Multiply(&calcTransform, MatrixOrderAppend);
					if (first) {
						path->GetBounds(&rect, &matrix, pen);
						first = false;
					} else {
						RectF r;
						path->GetBounds(&r, &matrix, pen);
						rect.Union(rect, rect, r);
					}
				}
				break;
			case 1:
				fill(graphics, (Brush*)i->info, &matrix, path);
				if (metaGraphics) {
					fill(metaGraphics, (Brush*)i->info, &matrix, path);
				}
				matrix.Multiply(&calcTransform, MatrixOrderAppend);
				if (first) {
					path->GetBounds(&rect, &matrix, NULL);
					first = false;
				} else {
					RectF r;
					path->GetBounds(&r, &matrix, NULL);
					rect.Union(rect, rect, r);
				}
				break;
			}
		}
		i++;
	}
	updateRect(rect);
	return rect;
}

/**
 * 墌屖偺昤夋
 * @param x 嵍忋嵗昗
 * @param y 嵍忋嵗昗
 * @param width 墶暆
 * @param height 廲暆
 * @param startAngle 帪寁曽岦墌屖奐巒埵抲
 * @param sweepAngle 昤夋妏搙
 * @return 峏怴椞堟忣曬
 */
RectF
LayerExDraw::drawArc(const Appearance *app, REAL x, REAL y, REAL width, REAL height, REAL startAngle, REAL sweepAngle)
{
	GraphicsPath path;
	path.AddArc(x, y, width, height, startAngle, sweepAngle);
	return _drawPath(app, &path);
}

/**
 * 儀僕僃嬋慄偺昤夋
 * @param app 傾僺傾儔儞僗
 * @param x1
 * @param y1
 * @param x2
 * @param y2
 * @param x3
 * @param y3
 * @param x4
 * @param y4
 * @return 峏怴椞堟忣曬
 */
RectF
LayerExDraw::drawBezier(const Appearance *app, REAL x1, REAL y1, REAL x2, REAL y2, REAL x3, REAL y3, REAL x4, REAL y4)
{
	GraphicsPath path;
	path.AddBezier(x1, y1, x2, y2, x3, y3, x4, y4);
        return _drawPath(app, &path);
}

/**
 * 楢懕儀僕僃嬋慄偺昤夋
 * @param app 傾僺傾儔儞僗
 * @param points 揰偺攝楍
 * @return 峏怴椞堟忣曬
 */
RectF
LayerExDraw::drawBeziers(const Appearance *app, tTJSVariant points)
{
	vector<PointF> ps;
	getPoints(points, ps);
	GraphicsPath path;
	path.AddBeziers(&ps[0], (int)ps.size());
        return _drawPath(app, &path);
}

/**
 * Closed cardinal spline 偺昤夋
 * @param app 傾僺傾儔儞僗
 * @param points 揰偺攝楍
 * @return 峏怴椞堟忣曬
 */
RectF
LayerExDraw::drawClosedCurve(const Appearance *app, tTJSVariant points)
{
	vector<PointF> ps;
	getPoints(points, ps);
	GraphicsPath path;
	path.AddClosedCurve(&ps[0], (int)ps.size());
        return _drawPath(app, &path);
}

/**
 * Closed cardinal spline 偺昤夋
 * @param app 傾僺傾儔儞僗
 * @param points 揰偺攝楍
 * @pram tension tension
 * @return 峏怴椞堟忣曬
 */
RectF
LayerExDraw::drawClosedCurve2(const Appearance *app, tTJSVariant points, REAL tension)
{
	vector<PointF> ps;
	getPoints(points, ps);
	GraphicsPath path;
	path.AddClosedCurve(&ps[0], (int)ps.size(), tension);
        return _drawPath(app, &path);
}

/**
 * cardinal spline 偺昤夋
 * @param app 傾僺傾儔儞僗
 * @param points 揰偺攝楍
 * @return 峏怴椞堟忣曬
 */
RectF
LayerExDraw::drawCurve(const Appearance *app, tTJSVariant points)
{
	vector<PointF> ps;
	getPoints(points, ps);
	GraphicsPath path;
	path.AddCurve(&ps[0], (int)ps.size());
        return _drawPath(app, &path);
}

/**
 * cardinal spline 偺昤夋
 * @param app 傾僺傾儔儞僗
 * @param points 揰偺攝楍
 * @parma tension tension
 * @return 峏怴椞堟忣曬
 */
RectF
LayerExDraw::drawCurve2(const Appearance *app, tTJSVariant points, REAL tension)
{
	vector<PointF> ps;
	getPoints(points, ps);
	GraphicsPath path;
	path.AddCurve(&ps[0], (int)ps.size(), tension);
        return _drawPath(app, &path);
}

/**
 * cardinal spline 偺昤夋
 * @param app 傾僺傾儔儞僗
 * @param points 揰偺攝楍
 * @param offset
 * @param numberOfSegments
 * @param tension tension
 * @return 峏怴椞堟忣曬
 */
RectF
LayerExDraw::drawCurve3(const Appearance *app, tTJSVariant points, int offset, int numberOfSegments, REAL tension)
{
	vector<PointF> ps;
	getPoints(points, ps);
	GraphicsPath path;
	path.AddCurve(&ps[0], (int)ps.size(), offset, numberOfSegments, tension);
        return _drawPath(app, &path);
}

/**
 * 墌悕偺昤夋
 * @param x 嵍忋嵗昗
 * @param y 嵍忋嵗昗
 * @param width 墶暆
 * @param height 廲暆
 * @param startAngle 帪寁曽岦墌屖奐巒埵抲
 * @param sweepAngle 昤夋妏搙
 * @return 峏怴椞堟忣曬
 */
RectF
LayerExDraw::drawPie(const Appearance *app, REAL x, REAL y, REAL width, REAL height, REAL startAngle, REAL sweepAngle)
{
	GraphicsPath path;
	path.AddPie(x, y, width, height, startAngle, sweepAngle);
        return _drawPath(app, &path);
}

/**
 * 懭墌偺昤夋
 * @param app 傾僺傾儔儞僗
 * @param x
 * @param y
 * @param width
 * @param height
 * @return 峏怴椞堟忣曬
 */
RectF
LayerExDraw::drawEllipse(const Appearance *app, REAL x, REAL y, REAL width, REAL height)
{
	GraphicsPath path;
	path.AddEllipse(x, y, width, height);
        return _drawPath(app, &path);
}

/**
 * 慄暘偺昤夋
 * @param app 傾僺傾儔儞僗
 * @param x1 巒揰X嵗昗
 * @param y1 巒揰Y嵗昗
 * @param x2 廔揰X嵗昗
 * @param y2 廔揰Y嵗昗
 * @return 峏怴椞堟忣曬
 */
RectF
LayerExDraw::drawLine(const Appearance *app, REAL x1, REAL y1, REAL x2, REAL y2)
{
	GraphicsPath path;
	path.AddLine(x1, y1, x2, y2);
        return _drawPath(app, &path);
}

/**
 * 楢懕慄暘偺昤夋
 * @param app 傾僺傾儔儞僗
 * @param points 揰偺攝楍
 * @return 峏怴椞堟忣曬
 */
RectF
LayerExDraw::drawLines(const Appearance *app, tTJSVariant points)
{
	vector<PointF> ps;
	getPoints(points, ps);
	GraphicsPath path;
	path.AddLines(&ps[0], (int)ps.size());
        return _drawPath(app, &path);
}

/**
 * 懡妏宍偺昤夋
 * @param app 傾僺傾儔儞僗
 * @param points 揰偺攝楍
 * @return 峏怴椞堟忣曬
 */
RectF
LayerExDraw::drawPolygon(const Appearance *app, tTJSVariant points)
{
	vector<PointF> ps;
	getPoints(points, ps);
	GraphicsPath path;
	path.AddPolygon(&ps[0], (int)ps.size());
        return _drawPath(app, &path);
}


/**
 * 嬮宍偺昤夋
 * @param app 傾僺傾儔儞僗
 * @param x
 * @param y
 * @param width
 * @param height
 * @return 峏怴椞堟忣曬
 */
RectF
LayerExDraw::drawRectangle(const Appearance *app, REAL x, REAL y, REAL width, REAL height)
{
	GraphicsPath path;
	RectF rect(x, y, width, height);
	path.AddRectangle(rect);
        return _drawPath(app, &path);
}

/**
 * 暋悢嬮宍偺昤夋
 * @param app 傾僺傾儔儞僗
 * @param rects 嬮宍忣曬偺攝楍
 * @return 峏怴椞堟忣曬
 */
RectF
LayerExDraw::drawRectangles(const Appearance *app, tTJSVariant rects)
{
	vector<RectF> rs;
	getRects(rects, rs);
	GraphicsPath path;
	path.AddRectangles(&rs[0], (int)rs.size());
        return _drawPath(app, &path);
}

/**
 * 暥帤楍偺僷僗儀乕僗偱偺昤夋
 * @param font 僼僅儞僩
 * @param app 傾僺傾儔儞僗
 * @param x 昤夋埵抲X
 * @param y 昤夋埵抲Y
 * @param text 昤夋僥僉僗僩
 * @return 峏怴椞堟忣曬
 */
RectF
LayerExDraw::drawPathString(const FontInfo *font, const Appearance *app, REAL x, REAL y, const tjs_char *text)
{
  if (font->getSelfPathDraw())
    return drawPathString2(font, app, x, y, text);

	// 暥帤楍偺僷僗傪弨旛
	GraphicsPath path;
	path.AddString(text, -1, font->fontFamily, font->style, font->emSize, PointF(x, y), StringFormat::GenericDefault());
        return _drawPath(app, &path);
}

static void transformRect(Matrix &calcTransform, RectF &rect)
{
	PointF points[4]; // 尦嵗昗抣
	points[0].X = rect.X;
	points[0].Y = rect.Y;
	points[1].X = rect.X + rect.Width;
	points[1].Y = rect.Y;
	points[2].X = rect.X;
	points[2].Y = rect.Y + rect.Height;
	points[3].X = rect.X + rect.Width;
	points[3].Y = rect.Y + rect.Height;
	// 昤夋椞堟傪嵞寁嶼
	calcTransform.TransformPoints(points, 4);
	REAL minx = points[0].X;
	REAL maxx = points[0].X;
	REAL miny = points[0].Y;
	REAL maxy = points[0].Y;
	for (int i=1;i<4;i++) {
		if (points[i].X < minx) { minx = points[i].X; }
		if (points[i].X > maxx) { maxx = points[i].X; }
		if (points[i].Y < miny) { miny = points[i].Y; }
		if (points[i].Y > maxy) { maxy = points[i].Y; }
	}
	rect.X = minx;
	rect.Y = miny;
	rect.Width = maxx - minx;
	rect.Height = maxy - miny;
}

/**
 * 暥帤楍偺昤夋
 * @param font 僼僅儞僩
 * @param app 傾僺傾儔儞僗乮僽儔僔偺傒嶲徠偝傟傑偡乯
 * @param x 昤夋埵抲X
 * @param y 昤夋埵抲Y
 * @param text 昤夋僥僉僗僩
 * @return 峏怴椞堟忣曬
 */
RectF
LayerExDraw::drawString(const FontInfo *font, const Appearance *app, REAL x, REAL y, const tjs_char *text)
{
  if (font->getSelfPathDraw())
    return drawPathString2(font, app, x, y, text);

	graphics->SetTextRenderingHint(textRenderingHint);
	if (metaGraphics) {
		metaGraphics->SetTextRenderingHint(textRenderingHint);
	}

	// 椞堟婰榐梡
	RectF rect;
	// 昤夋僼僅儞僩
	Font f(font->fontFamily, font->emSize, font->style, UnitPixel);

	// 昤夋忣曬傪巊偭偰師乆昤夋
	bool first = true;
	vector<Appearance::DrawInfo>::const_iterator i = app->drawInfos.begin();
	while (i != app->drawInfos.end()) {
		if (i->info) {
			if (i->type == 1) { // 僽儔僔偺傒
				Brush *brush = (Brush*)i->info;
				PointF p(x + i->ox, y + i->oy);
				graphics->DrawString(text, -1, &f, p, StringFormat::GenericDefault(), brush);
				if (metaGraphics) {
					metaGraphics->DrawString(text, -1, &f, p, StringFormat::GenericDefault(), brush);
				}
				// 峏怴椞堟寁嶼
				if (first) {
					graphics->MeasureString(text, -1, &f, p, StringFormat::GenericDefault(), &rect);
					transformRect(calcTransform, rect);
					first = false;
				} else {
					RectF r;
					graphics->MeasureString(text, -1, &f, p, StringFormat::GenericDefault(), &r);
					transformRect(calcTransform, r);
					rect.Union(rect, rect, r);
				}
				break;
			}
		}
		i++;
	}
	updateRect(rect);
	return rect;
}

/**
 * 暥帤楍偺昤夋椞堟忣曬偺庢摼
 * @param font 僼僅儞僩
 * @param text 昤夋僥僉僗僩
 * @return 昤夋椞堟忣曬
 */
RectF
LayerExDraw::measureString(const FontInfo *font, const tjs_char *text)
{
  if (font->getSelfPathDraw())
    return measureString2(font, text);

	RectF rect;
	graphics->SetTextRenderingHint(textRenderingHint);
	Font f(font->fontFamily, font->emSize, font->style, UnitPixel);
	graphics->MeasureString(text, -1, &f, PointF(0,0), StringFormat::GenericDefault(), &rect);
	return rect;
}

/**
 * 暥帤楍偵奜愙偡傞椞堟忣曬偺庢摼
 * @param font 僼僅儞僩
 * @param text 昤夋僥僉僗僩
 * @return 椞堟忣曬偺帿彂 left, top, width, height
 */
RectF
LayerExDraw::measureStringInternal(const FontInfo *font, const tjs_char *text)
{
  if (font->getSelfPathDraw())
    return measureStringInternal2(font, text);
  
  RectF rect;
  graphics->SetTextRenderingHint(textRenderingHint);
  Font f(font->fontFamily, font->emSize, font->style, UnitPixel);
  graphics->MeasureString(text, -1, &f, PointF(0,0), StringFormat::GenericDefault(), &rect);
  CharacterRange charRange(0, INT(wcslen(text)));
  StringFormat stringFormat = StringFormat::GenericDefault();
  stringFormat.SetMeasurableCharacterRanges(1, &charRange);
  Region region;
  graphics->MeasureCharacterRanges(text, -1, &f, rect, &stringFormat, 1, &region);
  RectF regionBounds;
  region.GetBounds(&regionBounds, graphics);
  return regionBounds;
}

/**
 * 夋憸偺昤夋丅僐僺乕愭偼尦夋憸偺 Bounds 傪攝椂偟偨埵抲丄僒僀僘偼 Pixel 巜掕偵側傝傑偡丅
 * @param x 僐僺乕愭尨揰
 * @param y  僐僺乕愭尨揰
 * @param src 僐僺乕尦夋憸
 * @return 峏怴椞堟忣曬
 */
RectF
LayerExDraw::drawImage(REAL x, REAL y, Image *src) 
{
	RectF rect;
	if (src) {
		RectF *bounds = getBounds(src);
		rect = drawImageRect(x + bounds->X, y + bounds->Y, src, 0, 0, bounds->Width, bounds->Height);
		delete bounds;
		updateRect(rect);
	}
	return rect;
}

/**
 * 夋憸偺嬮宍僐僺乕
 * @param dleft 僐僺乕愭嵍抂
 * @param dtop  僐僺乕愭忋抂
 * @param src 僐僺乕尦夋憸
 * @param sleft 尦嬮宍偺嵍抂
 * @param stop  尦嬮宍偺忋抂
 * @param swidth 尦嬮宍偺墶暆
 * @param sheight  尦嬮宍偺廲暆
 * @return 峏怴椞堟忣曬
 */
RectF
LayerExDraw::drawImageRect(REAL dleft, REAL dtop, Image *src, REAL sleft, REAL stop, REAL swidth, REAL sheight)
{
	return drawImageAffine(src, sleft, stop, swidth, sheight, true, 1, 0, 0, 1, dleft, dtop);
}

/**
 * 夋憸偺奼戝弅彫僐僺乕
 * @param dleft 僐僺乕愭嵍抂
 * @param dtop  僐僺乕愭忋抂
 * @param dwidth 僐僺乕愭偺墶暆
 * @param dheight  僐僺乕愭偺廲暆
 * @param src 僐僺乕尦夋憸
 * @param sleft 尦嬮宍偺嵍抂
 * @param stop  尦嬮宍偺忋抂
 * @param swidth 尦嬮宍偺墶暆
 * @param sheight  尦嬮宍偺廲暆
 * @return 峏怴椞堟忣曬
 */
RectF
LayerExDraw::drawImageStretch(REAL dleft, REAL dtop, REAL dwidth, REAL dheight, Image *src, REAL sleft, REAL stop, REAL swidth, REAL sheight)
{
	return drawImageAffine(src, sleft, stop, swidth, sheight, true, dwidth/swidth, 0, 0, dheight/sheight, dleft, dtop);
}

/**
 * 夋憸偺傾僼傿儞曄姺僐僺乕
 * @param sleft 尦嬮宍偺嵍抂
 * @param stop  尦嬮宍偺忋抂
 * @param swidth 尦嬮宍偺墶暆
 * @param sheight  尦嬮宍偺廲暆
 * @param affine 傾僼傿儞僷儔儊乕僞偺庬椶(true:曄姺峴楍, false:嵗昗巜掕), 
 * @return 峏怴椞堟忣曬
 */
RectF
LayerExDraw::drawImageAffine(Image *src, REAL sleft, REAL stop, REAL swidth, REAL sheight, bool affine, REAL A, REAL B, REAL C, REAL D, REAL E, REAL F)
{
	RectF rect;
	if (src) {
		PointF points[4]; // 尦嵗昗抣
		if (affine) {
#define AFFINEX(x,y) A*x+C*y+E
#define AFFINEY(x,y) B*x+D*y+F
			points[0].X = AFFINEX(0,0);
			points[0].Y = AFFINEY(0,0);
			points[1].X = AFFINEX(swidth,0);
			points[1].Y = AFFINEY(swidth,0);
			points[2].X = AFFINEX(0,sheight);
			points[2].Y = AFFINEY(0,sheight);
			points[3].X = AFFINEX(swidth,sheight);
			points[3].Y = AFFINEY(swidth,sheight);
		} else {
			points[0].X = A;
			points[0].Y = B;
			points[1].X = C;
			points[1].Y = D;
			points[2].X = E;
			points[2].Y = F;
			points[3].X = C-A+E;
			points[3].Y = D-B+F;
		}
		graphics->DrawImage(src, points, 3, sleft, stop, swidth, sheight, UnitPixel, NULL, NULL, NULL);
		if (metaGraphics) {
			metaGraphics->DrawImage(src, points, 3, sleft, stop, swidth, sheight, UnitPixel, NULL, NULL, NULL);
		}

		// 昤夋椞堟傪庢摼
		calcTransform.TransformPoints(points, 4);
		REAL minx = points[0].X;
		REAL maxx = points[0].X;
		REAL miny = points[0].Y;
		REAL maxy = points[0].Y;
		for (int i=1;i<4;i++) {
			if (points[i].X < minx) { minx = points[i].X; }
			if (points[i].X > maxx) { maxx = points[i].X; }
			if (points[i].Y < miny) { miny = points[i].Y; }
			if (points[i].Y > maxy) { maxy = points[i].Y; }
		}
		rect.X = minx;
		rect.Y = miny;
		rect.Width = maxx - minx;
		rect.Height = maxy - miny;

		updateRect(rect);
	}
	return rect;
}

void
LayerExDraw::createRecord()
{
	destroyRecord();
	if ((metaBuffer = ::GlobalAlloc(GMEM_MOVEABLE, 0))){
		if (::CreateStreamOnHGlobal(metaBuffer, FALSE, &metaStream) == S_OK) 	{
			metafile = new Metafile(metaStream, metaHDC, EmfTypeEmfPlusOnly);
			metaGraphics = new Graphics(metafile);
			metaGraphics->SetCompositingMode(CompositingModeSourceOver);
			metaGraphics->SetTransform(&transform);
		}
	}
}

/**
 * 婰榐忣曬偺攋婞
 */
void
LayerExDraw::destroyRecord()
{
	if (metaGraphics) {
		delete metaGraphics;
		metaGraphics = NULL;
	}
	if (metafile) {
		delete metafile;
		metafile = NULL;
	}
	if (metaStream) {
		metaStream->Release();
		metaStream = NULL;
	}
	if (metaBuffer) {
		::GlobalFree(metaBuffer);
		metaBuffer = NULL;
	}
}


/**
 * @param record 昤夋撪梕傪婰榐偡傞偐偳偆偐
 */
void
LayerExDraw::setRecord(bool record)
{
	if (record) {
		if (!metafile) {
			createRecord();
		}
	} else {
		if (metafile) {
			destroyRecord();
		}
	}
}

bool
LayerExDraw::redraw(Image *image)
{
	if (image) {
		RectF *bounds = getBounds(image);
		if (metaGraphics) {
			metaGraphics->Clear(Color(0));
			metaGraphics->ResetTransform();
			metaGraphics->DrawImage(image, bounds->X, bounds->Y, bounds->Width, bounds->Height);
			metaGraphics->SetTransform(&transform);
		}
		graphics->Clear(Color(0));
		graphics->SetTransform(&viewTransform);
		graphics->DrawImage(image, bounds->X, bounds->Y, bounds->Width, bounds->Height);
		graphics->SetTransform(&calcTransform);
		delete bounds;
		//_pUpdate(0, NULL);
		_this->Update();
		return true;
	}
	return false;
}

/**
 * 婰榐撪梕傪 Image 偲偟偰庢摼
 * @return 惉岟偟偨傜 true
 */
Image *
LayerExDraw::getRecordImage()
{
	Image *image = NULL;
	if (metafile) {
		// 儊僞忣曬傪庢摼偡傞偵偼堦搙暵偠傞昁梫偑偁傞
		if (metaGraphics) {
			delete metaGraphics;
			metaGraphics = NULL;
		}

		//暵偠偨偁偲宲懕偡傞偨傔偺嵞昤夋愭傪暿搑峔抸
		HGLOBAL oldBuffer = metaBuffer;
		metaBuffer = NULL;
		createRecord();
		
		// 嵞昤夋
		if (oldBuffer) {
			IStream* pStream = NULL;
			if(::CreateStreamOnHGlobal(oldBuffer, FALSE, &pStream) == S_OK) 	{
				image = Image::FromStream(pStream,false);
				if (image) {
					redraw(image);
				}
				pStream->Release();
			}
			::GlobalFree(oldBuffer);
		}
	}
	return image;
}

/**
 * 婰榐撪梕偺尰嵼偺夝憸搙偱偺嵞昤夋
 */
bool
LayerExDraw::redrawRecord()
{
	// 嵞昤夋張棟
	Image *image = getRecordImage();
	if (image) {
		delete image;
		return true;
	}
	return false;
}

/**
 * 婰榐撪梕偺曐懚
 * @param filename 曐懚僼傽僀儖柤
 * @return 惉岟偟偨傜 true
 */
bool
LayerExDraw::saveRecord(const tjs_char *filename)
{
	bool ret = false;
	if (metafile) {		
		// 儊僞忣曬傪庢摼偡傞偵偼堦搙暵偠傞昁梫偑偁傞
		delete metaGraphics;
		metaGraphics = NULL;
		ULONG size;
		// 僼傽僀儖偵彂偒弌偡
		if (metaBuffer && (size = (ULONG)::GlobalSize(metaBuffer)) > 0) {
			IStream *out = TVPCreateIStream(filename, TJS_BS_WRITE);
			if (out) {
				void* pBuffer = ::GlobalLock(metaBuffer);
				if (pBuffer) {
					ret = (out->Write(pBuffer, size, &size) == S_OK);
					::GlobalUnlock(metaBuffer);
				}
				out->Release();
			}
		}
		// 嵞昤夋張棟
		Image *image = getRecordImage();
		if (image) {
			delete image;
		}
	}
	return ret;
}


/**
 * 婰榐撪梕偺撉傒崬傒
 * @param filename 撉傒崬傒僼傽僀儖柤
 * @return 惉岟偟偨傜 true
 */
bool
LayerExDraw::loadRecord(const tjs_char *filename)
{
	bool ret = false;
	Image *image;
	if (filename && (image = loadImage(filename))) {
		createRecord();
		ret =  redraw(image);
		delete image;
	}
	return false;
}

/**
 * 僌儕僼傾僂僩儔僀儞偺庢摼
 * @param font 僼僅儞僩
 * @param offset 僆僼僙僢僩
 * @param path 僌儕僼傪彂偒弌偡僷僗
 * @param glyph 昤夋偡傞僌儕僼
 */
void
LayerExDraw::getGlyphOutline(const FontInfo *fontInfo, PointF &offset, GraphicsPath *path, UINT glyph)
{
  static const MAT2 mat = { { 0, 1 }, { 0, 0 }, { 0, 0 }, { 0, 1 } };

  GLYPHMETRICS gm;

  int size = GetGlyphOutlineW(metaHDC,
                              glyph,
                              GGO_BEZIER, //  | GGO_GLYPH_INDEX,
                              &gm, 
                              0, 
                              NULL, 
                              &mat 
                              );
  char *buffer = NULL;
  if (size > 0) {
	  buffer = new char[size];
	  int result = GetGlyphOutlineW(metaHDC,
									glyph,
									GGO_BEZIER, //  | GGO_GLYPH_INDEX,
									&gm, 
									size, 
									buffer, 
									&mat 
									);
	  if (result <= 0) {
		  delete[] buffer;
		  return;
	  }
  } else {
	  GetGlyphOutlineW(metaHDC,
					   glyph,
					   GGO_METRICS,
					   &gm, 
					   0, 
					   NULL, 
					   &mat 
					   );
  }

  int index = 0;
  PointF aOffset = offset;
  aOffset.Y += fontInfo->getAscent();

  while(index < size) {
    TTPOLYGONHEADER * header = (TTPOLYGONHEADER *)(buffer + index);
    int endCurve = index + header->cb;
    index += sizeof(TTPOLYGONHEADER);
    PointF p0 = ToPointF(&header->pfxStart) + aOffset;
    while(index < endCurve) {
      TTPOLYCURVE * hcurve = (TTPOLYCURVE *)(buffer + index);
      index += 2 * sizeof(WORD);
      POINTFX * points = (POINTFX *)(buffer + index);
      index += hcurve->cpfx * sizeof(POINTFX);
      std::vector<PointF> pts(1 + hcurve->cpfx);
      pts[0] = p0;
      for(int i = 0; i < hcurve->cpfx; i++)
        pts[i + 1] = ToPointF(points + i) + aOffset;
      p0 = pts[pts.size() - 1];
      switch(hcurve->wType) {
      case TT_PRIM_LINE:
        path->AddLines(&pts[0], int(pts.size()));
        break;

      case TT_PRIM_QSPLINE:
        TVPAddLog(ttstr(L"qspline"));
        break;

      case TT_PRIM_CSPLINE:
        path->AddBeziers(&pts[0], int(pts.size()));
        break;
      }
    }

    path->CloseFigure();
  }

  offset.X += gm.gmCellIncX;

  delete[] buffer;
}

/*
 * 僥僉僗僩傾僂僩儔僀儞偺庢摼
 * @param font 僼僅儞僩
 * @param offset 僆僼僙僢僩
 * @param path 僌儕僼傪彂偒弌偡僷僗
 * @param text 昤夋偡傞僥僉僗僩
 */
void
LayerExDraw::getTextOutline(const FontInfo *fontInfo, PointF &offset, GraphicsPath *path, ttstr text)
{
  if (metaHDC == NULL) 
    return;

  if (text.IsEmpty())
    return;

  LOGFONT font;
  memset(&font, 0, sizeof(font));
  font.lfHeight = -(LONG(fontInfo->emSize));
  font.lfWeight = (fontInfo->style & 1) ? FW_BOLD : FW_REGULAR;
  font.lfItalic = fontInfo->style & 2;
  font.lfUnderline = fontInfo->style & 4;
  font.lfStrikeOut = fontInfo->style & 8;
  font.lfCharSet = DEFAULT_CHARSET;
  wcscpy_s(font.lfFaceName, fontInfo->familyName.c_str());

  HFONT hFont = CreateFontIndirect(&font);
  HGDIOBJ hOldFont = SelectObject(metaHDC, hFont);

  for (tjs_int i = 0; i < text.GetLen(); i++) {
    this->getGlyphOutline(fontInfo, offset, path, text[i]);
  }

  SelectObject(metaHDC, hOldFont);
  DeleteObject(hFont);
}

/**
 * 暥帤楍偺昤夋峏怴椞堟忣曬偺庢摼(OpenType僼僅儞僩懳墳)
 * @param font 僼僅儞僩
 * @param text 昤夋僥僉僗僩
 * @return 峏怴椞堟忣曬偺帿彂 left, top, width, height
 */
RectF 
LayerExDraw::measureString2(const FontInfo *font, const tjs_char *text)
{
  // 暥帤楍偺僷僗傪弨旛
  GraphicsPath path;
  PointF offset(0, 0);
  this->getTextOutline(font, offset, &path, text);
  RectF result;
  path.GetBounds(&result, NULL, NULL);
  result.X = 0;
  result.Y = 0;
  result.Width += REAL(0.167 * font->emSize * 2);
  result.Height = REAL(font->getLineSpacing() * 1.124);
  return result;
}

/**
 * 暥帤楍偵奜愙偡傞椞堟忣曬偺庢摼(OpenType偺PostScript僼僅儞僩懳墳)
 * @param font 僼僅儞僩
 * @param text 昤夋僥僉僗僩
 * @return 峏怴椞堟忣曬偺帿彂 left, top, width, height
 */
RectF 
LayerExDraw::measureStringInternal2(const FontInfo *font, const tjs_char *text)
{
  // 暥帤楍偺僷僗傪弨旛
  GraphicsPath path;
  PointF offset(0, 0);
  this->getTextOutline(font, offset, &path, text);
  RectF result;
  path.GetBounds(&result, NULL, NULL);
  result.X = REAL(LONG(0.167 * font->emSize));
  result.Y = 0;
  result.Height = font->getLineSpacing();
  return result;
}

/**
 * 暥帤楍偺昤夋(OpenType僼僅儞僩懳墳)
 * @param font 僼僅儞僩
 * @param app 傾僺傾儔儞僗
 * @param x 昤夋埵抲X
 * @param y 昤夋埵抲Y
 * @param text 昤夋僥僉僗僩
 * @return 峏怴椞堟忣曬
 */
RectF 
LayerExDraw::drawPathString2(const FontInfo *font, const Appearance *app, REAL x, REAL y, const tjs_char *text)
{
  // 暥帤楍偺僷僗傪弨旛
  GraphicsPath path;
  PointF offset(x + LONG(0.167 * font->emSize) - 0.5, y - 0.5);
  this->getTextOutline(font, offset, &path, text);
  RectF result = _drawPath(app, &path);
  result.X = x;
  result.Y = y;
  result.Width += REAL(0.167 * font->emSize * 2);
  result.Height = REAL(font->getLineSpacing() * 1.124);
  return result;
}

// ----------------------------------- クラスの登録 
/**
 * ���O�o�͗p
 */
void
message_log(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	char msg[1024];
	_vsnprintf_s(msg, 1024, _TRUNCATE, format, args);
	TVPAddLog(ttstr(msg));
	va_end(args);
}

/**
 * �G���[���O�o�͗p
 */
void
error_log(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	char msg[1024];
	_vsnprintf_s(msg, 1024, _TRUNCATE, format, args);
	TVPAddImportantLog(ttstr(msg));
	va_end(args);
}

extern void initGdiPlus();
extern void deInitGdiPlus();
extern Image* loadImage(const tjs_char* name);
extern RectF* getBounds(Image* image);

// ----------------------------------------------------------------
// ���̌^�̓o�^
// ���l�p�����[�^�n�͔z�񂩎������g����悤�ȓ���R���o�[�^���\�z
// ----------------------------------------------------------------

// �������O�R���o�[�^
#define NCB_SET_CONVERTOR_BOTH(type, convertor)\
NCB_TYPECONV_SRCMAP_SET(type, convertor<type>, true);\
NCB_TYPECONV_DSTMAP_SET(type, convertor<type>, true)

// SRC�������O�R���o�[�^
#define NCB_SET_CONVERTOR_SRC(type, convertor)\
NCB_TYPECONV_SRCMAP_SET(type, convertor<type>, true);\
NCB_TYPECONV_DSTMAP_SET(type, ncbNativeObjectBoxing::Unboxing, true)

// DST�������O�R���o�[�^
#define NCB_SET_CONVERTOR_DST(type, convertor)\
NCB_TYPECONV_SRCMAP_SET(type, ncbNativeObjectBoxing::Boxing,   true); \
NCB_TYPECONV_DSTMAP_SET(type, convertor<type>, true)

/**
 * �z�񂩂ǂ����̔���
 * @param var VARIANT
 * @return �z��Ȃ� true
 */
bool IsArray(const tTJSVariant& var)
{
	if (var.Type() == tvtObject) {
		iTJSDispatch2* obj = var.AsObjectNoAddRef();
		return obj->IsInstanceOf(0, NULL, NULL, L"Array", obj) == TJS_S_TRUE;
	}
	return false;
}

// �����o�ϐ����v���p�e�B�Ƃ��ēo�^
#define NCB_MEMBER_PROPERTY(name, type, membername) \
	struct AutoProp_ ## name { \
		static void ProxySet(Class *inst, type value) { inst->membername = value; } \
		static type ProxyGet(Class *inst) {      return inst->membername; } }; \
	NCB_PROPERTY_PROXY(name,AutoProp_ ## name::ProxyGet, AutoProp_ ## name::ProxySet)

// �|�C���^�����^�� getter ��ϊ��o�^
#define NCB_ARG_PROPERTY_RO(name, type, methodname) \
	struct AutoProp_ ## name { \
		static type ProxyGet(Class *inst) { type var; inst->methodname(&var); return var; } }; \
	Property(TJS_W(# name), &AutoProp_ ## name::ProxyGet, (int)0, Proxy)

// ------------------------------------------------------
// �^�R���o�[�^�o�^
// ------------------------------------------------------

NCB_TYPECONV_CAST_INTEGER(Status);
NCB_TYPECONV_CAST_INTEGER(MatrixOrder);
NCB_TYPECONV_CAST_INTEGER(ImageType);
NCB_TYPECONV_CAST_INTEGER(RotateFlipType);
NCB_TYPECONV_CAST_INTEGER(SmoothingMode);
NCB_TYPECONV_CAST_INTEGER(TextRenderingHint);

// ------------------------------------------------------- PointF
template <class T>
struct PointFConvertor {
	typedef ncbInstanceAdaptor<T> AdaptorT;
	template <typename ANYT>
	void operator ()(ANYT& adst, const tTJSVariant& src) {
		if (src.Type() == tvtObject) {
			T* obj = AdaptorT::GetNativeInstance(src.AsObjectNoAddRef());
			if (obj) {
				dst = *obj;
			}
			else {
				ncbPropAccessor info(src);
				if (IsArray(src)) {
					dst = PointF((REAL)info.getRealValue(0),
						(REAL)info.getRealValue(1));
				}
				else {
					dst = PointF((REAL)info.getRealValue(L"x"),
						(REAL)info.getRealValue(L"y"));
				}
			}
		}
		else {
			dst = T();
		}
		adst = ncbTypeConvertor::ToTarget<ANYT>::Get(&dst);
	}
private:
	T dst;
};

NCB_SET_CONVERTOR_DST(PointF, PointFConvertor);
NCB_REGISTER_SUBCLASS_DELAY(PointF) {
	NCB_CONSTRUCTOR((REAL, REAL));
	NCB_MEMBER_PROPERTY(x, REAL, X);
	NCB_MEMBER_PROPERTY(y, REAL, Y);
	NCB_METHOD(Equals);
};

PointF getPoint(const tTJSVariant& var)
{
	PointFConvertor<PointF> conv;
	PointF ret;
	conv(ret, var);
	return ret;
}

// ------------------------------------------------------- RectF
template <class T>
struct RectFConvertor {
	typedef ncbInstanceAdaptor<T> AdaptorT;
	template <typename ANYT>
	void operator ()(ANYT& adst, const tTJSVariant& src) {
		if (src.Type() == tvtObject) {
			T* obj = AdaptorT::GetNativeInstance(src.AsObjectNoAddRef());
			if (obj) {
				dst = *obj;
			}
			else {
				ncbPropAccessor info(src);
				if (IsArray(src)) {
					dst = RectF((REAL)info.getRealValue(0),
						(REAL)info.getRealValue(1),
						(REAL)info.getRealValue(2),
						(REAL)info.getRealValue(3));
				}
				else {
					dst = RectF((REAL)info.getRealValue(L"x"),
						(REAL)info.getRealValue(L"y"),
						(REAL)info.getRealValue(L"width"),
						(REAL)info.getRealValue(L"height"));
				}
			}
		}
		else {
			dst = T();
		}
		adst = ncbTypeConvertor::ToTarget<ANYT>::Get(&dst);
	}
private:
	T dst;
};

NCB_SET_CONVERTOR_DST(RectF, RectFConvertor);
NCB_REGISTER_SUBCLASS_DELAY(RectF) {
	NCB_CONSTRUCTOR((REAL, REAL, REAL, REAL));
	NCB_MEMBER_PROPERTY(x, REAL, X);
	NCB_MEMBER_PROPERTY(y, REAL, Y);
	NCB_MEMBER_PROPERTY(width, REAL, Width);
	NCB_MEMBER_PROPERTY(height, REAL, Height);
	NCB_PROPERTY_RO(left, GetLeft);
	NCB_PROPERTY_RO(top, GetTop);
	NCB_PROPERTY_RO(right, GetRight);
	NCB_PROPERTY_RO(bottom, GetBottom);
	NCB_ARG_PROPERTY_RO(location, PointF, GetLocation);
	NCB_ARG_PROPERTY_RO(bounds, RectF, GetBounds);
	NCB_METHOD(Clone);
	// XXX	NCB_METHOD_DETAIL(Contains, Class, BOOL, Class::Contains, (REAL,REAL));
	//	NCB_METHOD_DETAIL(ContainsPoint, Class, BOOL, Class::Contains, (const PointF&) const);
	//	NCB_METHOD_DETAIL(ContainsRect, Class, BOOL, Class::Contains, (const RectF&));
	NCB_METHOD(Equals);
	NCB_METHOD_DETAIL(Inflate, Class, void, Class::Inflate, (REAL, REAL));
	NCB_METHOD_DETAIL(InflatePoint, Class, void, Class::Inflate, (const PointF&));
	//XXX	NCB_METHOD_DETAIL(Intersect, Class, BOOL, Class::Intersect, (const Rect&));
	NCB_METHOD(IntersectsWith);
	NCB_METHOD(IsEmptyArea);
	NCB_METHOD_DETAIL(Offset, Class, void, Class::Offset, (REAL, REAL));
	//XXX	NCB_METHOD_DETAIL(OffsetPoint, Class, void, Class::Offset, (const Point&));
	NCB_METHOD(Union);
};

RectF getRect(const tTJSVariant& var)
{
	RectFConvertor<RectF> conv;
	RectF ret;
	conv(ret, var);
	return ret;
}

// --------------------------------------------------------------------
// GDI+�̃f�t�H���g�R���X�g���N�^/�R�s�[�R���X�g���N�^�������Ȃ��^�̓o�^
// --------------------------------------------------------------------

/**
 * GDI+�I�u�W�F�N�g�̃��b�s���O�p�e���v���[�g�N���X
 */
template <class T>
class GdipWrapper {
	typedef T GdipClassT;
	typedef GdipWrapper<GdipClassT> WrapperT;
protected:
	GdipClassT* obj;
public:
	// �f�t�H���g�R���X�g���N�^
	GdipWrapper() : obj(NULL) {
	}

	// �֐��̋A��l�Ƃ��ẴI�u�W�F�N�g�������p�B
	// ���̂܂ܓn���ꂽ�|�C���^���g��
	GdipWrapper(GdipClassT* obj) : obj(obj) {
	}

	// �R�s�[�R���X�g���N�^
	// �����I�u�W�F�N�g�� Clone����
	GdipWrapper(const GdipWrapper& orig) : obj(NULL) {
		if (orig.obj) {
			obj = orig.obj->Clone();
		}
	}

	// �f�X�g���N�^
	~GdipWrapper() {
		if (obj) {
			delete obj;
		}
	}

	GdipClassT* getGdipObject() { return obj; }

	void setGdipObject(GdipClassT* src) {
		if (obj) {
			delete obj;
		}
		obj = src;
	}

	struct BridgeFunctor {
		GdipClassT* operator()(WrapperT* p) const {
			return p->getGdipObject();
		}
	};

	template <class CastT>
	struct CastBridgeFunctor {
		CastT* operator()(WrapperT* p) const {
			return (CastT*)p->getGdipObject();
		}
	};

};

/**
 * GDI+�I�u�W�F�N�g�����b�s���O�����N���X�p�̃R���o�[�^�i�ėp�j
 */
template <class T>
struct GdipTypeConvertor {
	typedef typename ncbTypeConvertor::Stripper<T>::Type GdipClassT;
	typedef T* GdipClassP;
	typedef GdipWrapper<GdipClassT> WrapperT;
	typedef ncbInstanceAdaptor<WrapperT> AdaptorT;
protected:
	GdipClassT* result; // ���ʂ̈ꎞ�ێ��p
public:
	GdipTypeConvertor() : result(NULL) {}
	~GdipTypeConvertor() { delete result; }

	void operator ()(GdipClassP& dst, const tTJSVariant& src) {
		WrapperT* obj;
		if (src.Type() == tvtObject && (obj = AdaptorT::GetNativeInstance(src.AsObjectNoAddRef()))) {
			dst = obj->getGdipObject();
		}
		else {
			dst = NULL;
		}
	}

	void operator ()(tTJSVariant& dst, const GdipClassP& src) {
		if (src != NULL) {
			iTJSDispatch2* adpobj = AdaptorT::CreateAdaptor(new WrapperT(src));
			if (adpobj) {
				dst = tTJSVariant(adpobj, adpobj);
				adpobj->Release();
			}
			else {
				dst = NULL;
			}
		}
		else {
			dst.Clear();
		}
	}
};

// �R���o�[�^�o�^�p�o�^�p�}�N��

#define NCB_GDIP_CONVERTOR(type) \
NCB_SET_CONVERTOR(type*, GdipTypeConvertor<type>);\
NCB_SET_CONVERTOR(const type*, GdipTypeConvertor<const type>)

#define NCB_GDIP_CONVERTOR2(type, convertor) \
NCB_SET_CONVERTOR(type*, convertor<type>);\
NCB_SET_CONVERTOR(const type*, convertor<const type>)

// ���b�s���O�����p
#define NCB_REGISTER_GDIP_SUBCLASS(Class) NCB_GDIP_CONVERTOR(Class);NCB_REGISTER_SUBCLASS(GdipWrapper<Class>) { typedef Class GdipClass;
#define NCB_REGISTER_GDIP_SUBCLASS2(Class, Convertor) NCB_GDIP_CONVERTOR2(Class, Convertor);NCB_REGISTER_SUBCLASS(GdipWrapper<Class>) { typedef Class GdipClass;
#define NCB_GDIP_METHOD(name)  Method(TJS_W(# name), &GdipClass::name, Bridge<GdipWrapper<GdipClass>::BridgeFunctor>())
#define NCB_GDIP_MCAST(ret, method, args) static_cast<ret (GdipClass::*) args>(&GdipClass::method)
#define NCB_GDIP_METHOD2(name, ret, method, args) Method(TJS_W(# name), NCB_GDIP_MCAST(ret, method, args), Bridge<GdipWrapper<GdipClass>::BridgeFunctor>())
#define NCB_GDIP_PROPERTY(name,get,set)  Property(TJS_W(# name), &GdipClass::get, &GdipClass::set, Bridge<GdipWrapper<GdipClass>::BridgeFunctor>())
// XXX ���܂��������Ȃ�
#define NCB_GDIP_PROPERTY_RO(name,get)  Property(TJS_W(# name), &GdipClass::get, (int)0, Bridge<GdipWrapper<GdipClass>::BridgeFunctor>())
#define NCB_GDIP_MEMBER_PROPERTY(name, type, membername) \
	struct AutoProp_ ## name { \
		static void ProxySet(GdipClass *inst, type value) { inst->membername = value; } \
		static type ProxyGet(GdipClass *inst) {      return inst->membername; } }; \
	Property(TJS_W(#name), AutoProp_ ## name::ProxyGet, AutoProp_ ## name::ProxySet, Bridge<GdipWrapper<GdipClass>::BridgeFunctor>())


// ------------------------------------------------------- Matrix

template <class T>
struct MatrixConvertor : public GdipTypeConvertor<T> {
	void operator ()(GdipClassP& dst, const tTJSVariant& src) {
		WrapperT* obj;
		if (src.Type() == tvtObject) {
			if ((obj = AdaptorT::GetNativeInstance(src.AsObjectNoAddRef()))) {
				dst = obj->getGdipObject();
			}
			else {
				ncbPropAccessor info(src);
				if (IsArray(src)) {
					result = new Matrix((REAL)info.getRealValue(0),
						(REAL)info.getRealValue(1),
						(REAL)info.getRealValue(2),
						(REAL)info.getRealValue(3),
						(REAL)info.getRealValue(4),
						(REAL)info.getRealValue(5));
				}
				else {
					result = new Matrix((REAL)info.getRealValue(L"m11"),
						(REAL)info.getRealValue(L"m12"),
						(REAL)info.getRealValue(L"m21"),
						(REAL)info.getRealValue(L"m22"),
						(REAL)info.getRealValue(L"dx"),
						(REAL)info.getRealValue(L"dy"));
				}
				dst = result;
			}
		}
		else {
			dst = NULL;
		}
	}
};

static tjs_error
MatrixFactory(GdipWrapper<Matrix>** result, tjs_int numparams, tTJSVariant** params, iTJSDispatch2* objthis)
{
	Matrix* matrix = NULL;
	RectF* rect = NULL;
	PointF* point = NULL;
	if (numparams == 0) {
		matrix = new Matrix();
	}
	else if (numparams == 2 &&
		(params[0]->Type() == tvtObject && (rect = ncbInstanceAdaptor<RectF>::GetNativeInstance(params[0]->AsObjectNoAddRef()))) &&
		(params[1]->Type() == tvtObject && (point = ncbInstanceAdaptor<PointF>::GetNativeInstance(params[0]->AsObjectNoAddRef())))) {
		matrix = new Matrix(*rect, point);
	}
	else if (numparams == 6) {
		matrix = new Matrix((REAL)params[0]->AsReal(),
			(REAL)params[1]->AsReal(),
			(REAL)params[2]->AsReal(),
			(REAL)params[3]->AsReal(),
			(REAL)params[4]->AsReal(),
			(REAL)params[5]->AsReal());
	}
	else {
		return TJS_E_INVALIDPARAM;
	}
	*result = new GdipWrapper<Matrix>(matrix);
	return TJS_S_OK;
}

NCB_REGISTER_GDIP_SUBCLASS2(Matrix, MatrixConvertor)
Factory(MatrixFactory);
NCB_GDIP_METHOD(OffsetX);
NCB_GDIP_METHOD(OffsetY);
NCB_GDIP_METHOD(Equals);
// NCB_GDIP_METHOD(getElements); // �z���Ԃ�
NCB_GDIP_METHOD(SetElements);
NCB_GDIP_METHOD(GetLastStatus);
NCB_GDIP_METHOD(Invert);
NCB_GDIP_METHOD(IsIdentity);
NCB_GDIP_METHOD(IsInvertible);
NCB_GDIP_METHOD(Multiply);
NCB_GDIP_METHOD(Reset);
NCB_GDIP_METHOD(Rotate);
NCB_GDIP_METHOD(RotateAt);
NCB_GDIP_METHOD(Scale);
NCB_GDIP_METHOD(Shear);
//	NCB_GDIP_METHOD_DETAIL(TransformPoints, Class, Status, TransformPoints, (PointF*, INT)); XXX �������z��
//	NCB_GDIP_METHOD_DETAIL(TransformVectors, Class, Status, TransformVectors, (PointF*, INT)); XXX �������z��
NCB_GDIP_METHOD(Translate);
};

// ------------------------------------------------------- Image

/**
 * �C���[�W�p�R���o�[�^
 * �����񂩂���ύX�\
 */
template <class T>
struct ImageConvertor : public GdipTypeConvertor<T> {
	void operator ()(GdipClassP& dst, const tTJSVariant& src) {
		if (src.Type() == tvtObject) {
			WrapperT* obj;
			if ((obj = AdaptorT::GetNativeInstance(src.AsObjectNoAddRef()))) {
				dst = obj->getGdipObject();
			}
			else {
				LayerExDraw* layer = ncbInstanceAdaptor<LayerExDraw>::GetNativeInstance(src.AsObjectNoAddRef());
				if (layer) {
					dst = *layer;
				}
				else {
					dst = NULL;
				}
			}
		}
		else if (src.Type() == tvtString) { // �����񂩂琶��
			dst = result = loadImage(src.GetString());
		}
		else {
			dst = NULL;
		}
	}
};


static tjs_error
ImageFactory(GdipWrapper<Image>** result, tjs_int numparams, tTJSVariant** params, iTJSDispatch2* objthis)
{
	if (numparams == 0) {
		*result = new GdipWrapper<Image>();
		return TJS_S_OK;
	}
	else if (numparams > 0 && params[0]->Type() == tvtString) {
		Image* image = loadImage(params[0]->GetString());
		if (image) {
			*result = new GdipWrapper<Image>(image);
			return TJS_S_OK;
		}
		else {
			TVPThrowExceptionMessage(TJS_W("cannot open:%1"), *params[0]);
		}
	}
	return TJS_E_INVALIDPARAM;
}

static void ImageLoad(GdipWrapper<Image>* obj, const tjs_char* filename)
{
	Image* image = loadImage(filename);
	if (image) {
		obj->setGdipObject(image);
	}
	else {
		TVPThrowExceptionMessage(TJS_W("cannot open:%1"), ttstr(filename));
	}
}

static tTJSVariant ImageClone(GdipWrapper<Image>* obj)
{
	typedef GdipWrapper<Image> WrapperT;
	typedef ncbInstanceAdaptor<WrapperT> AdaptorT;
	tTJSVariant ret;
	Image* src = obj->getGdipObject();
	if (src) {
		Image* newimage = src->Clone();
		iTJSDispatch2* adpobj = AdaptorT::CreateAdaptor(new WrapperT(newimage));
		if (adpobj) {
			ret = tTJSVariant(adpobj, adpobj);
			adpobj->Release();
		}
		else {
			delete newimage;
		}
	}
	return ret;
}

static tTJSVariant ImageBounds(GdipWrapper<Image>* obj)
{
	typedef ncbInstanceAdaptor<RectF> AdaptorT;
	tTJSVariant ret;
	Image* src = obj->getGdipObject();
	if (src) {
		RectF* bounds = getBounds(src);
		iTJSDispatch2* adpobj = AdaptorT::CreateAdaptor(bounds);
		if (adpobj) {
			ret = tTJSVariant(adpobj, adpobj);
			adpobj->Release();
		}
		else {
			delete bounds;
		}
	}
	return ret;
}

NCB_REGISTER_GDIP_SUBCLASS2(Image, ImageConvertor)
Factory(ImageFactory);
NCB_METHOD_PROXY(load, ImageLoad);
NCB_METHOD_PROXY(Clone, ImageClone);
NCB_METHOD_PROXY(GetBounds, ImageBounds);
//GetAllPropertyItems
//NCB_GDIP_METHOD(GetBounds);
//GetEncoderParameterList
//GetEncoderParameterListSize
NCB_GDIP_METHOD(GetFlags);
//NCB_GDIP_METHOD(GetFrameCount);
//NCB_GDIP_METHOD(GetFrameDimensionCount);
//NCB_GDIP_METHOD(GetFrameDimensionList);
NCB_GDIP_METHOD(GetHeight);
NCB_GDIP_METHOD(GetHorizontalResolution);
NCB_GDIP_METHOD(GetLastStatus);
//NCB_GDIP_PROPERTY(palette, GetPalette, SetPalette);
//NCB_GDIP_METHOD(GetPaletteSize);
//GetPhysicalDimension
NCB_GDIP_METHOD(GetPixelFormat);
//NCB_GDIP_METHOD(GetPropertyCount);
//GetPropertyItemList
//GetPropertyItem
//SetPropertyItem
//GetPropertyItemSize
//GetPropertySize
//GetRawFormat
//GetThumbnailImage
NCB_GDIP_METHOD(GetType);
NCB_GDIP_METHOD(GetVerticalResolution);
NCB_GDIP_METHOD(GetWidth);
//RemovePropertyItem
NCB_GDIP_METHOD(RotateFlip);
//SelectActiveFrame
};

// ------------------------------------------------------
// ���O�L�q�N���X�o�^
// ------------------------------------------------------

NCB_REGISTER_SUBCLASS(FontInfo) {
	NCB_CONSTRUCTOR((const tjs_char*, REAL, INT));
	NCB_PROPERTY(familyName, getFamilyName, setFamilyName);
	NCB_PROPERTY(emSize, getEmSize, setEmSize);
	NCB_PROPERTY(style, getStyle, setStyle);
	NCB_PROPERTY(forceSelfPathDraw, getForceSelfPathDraw, setForceSelfPathDraw);
	NCB_PROPERTY_RO(ascent, getAscent);
	NCB_PROPERTY_RO(descent, getDescent);
	NCB_PROPERTY_RO(ascentLeading, getAscentLeading);
	NCB_PROPERTY_RO(descentLeading, getDescentLeading);
	NCB_PROPERTY_RO(lineSpacing, getLineSpacing);
};

NCB_REGISTER_SUBCLASS(Appearance) {
	NCB_CONSTRUCTOR(());
	NCB_METHOD(clear);
	NCB_METHOD(addBrush);
	NCB_METHOD(addPen);
};

NCB_REGISTER_SUBCLASS(Path)
{
    NCB_CONSTRUCTOR(());
    NCB_METHOD(startFigure);
    NCB_METHOD(closeFigure);
    NCB_METHOD(drawArc);
    NCB_METHOD(drawPie);
    NCB_METHOD(drawBezier);
    NCB_METHOD(drawBeziers);
    NCB_METHOD(drawClosedCurve);
    NCB_METHOD(drawClosedCurve2);
    NCB_METHOD(drawCurve);
    NCB_METHOD(drawCurve2);
    NCB_METHOD(drawCurve3);
    NCB_METHOD(drawEllipse);
    NCB_METHOD(drawLine);
    NCB_METHOD(drawLines);
    NCB_METHOD(drawPolygon);
    NCB_METHOD(drawRectangle);
    NCB_METHOD(drawRectangles);
};

#define ENUM(n) Variant(#n, (int)n)
#define NCB_SUBCLASS_NAME(name) NCB_SUBCLASS(name, name)
#define NCB_GDIP_SUBCLASS(Class) NCB_SUBCLASS(Class, GdipWrapper<Class>)

NCB_REGISTER_CLASS(GdiPlus)
{
	// enums

	// Status
	ENUM(Ok);
	ENUM(GenericError);
	ENUM(InvalidParameter);
	ENUM(OutOfMemory);
	ENUM(ObjectBusy);
	ENUM(InsufficientBuffer);
	ENUM(NotImplemented);
	ENUM(Win32Error);
	ENUM(WrongState);
	ENUM(Aborted);
	ENUM(FileNotFound);
	ENUM(ValueOverflow);
	ENUM(AccessDenied);
	ENUM(UnknownImageFormat);
	ENUM(FontFamilyNotFound);
	ENUM(FontStyleNotFound);
	ENUM(NotTrueTypeFont);
	ENUM(UnsupportedGdiplusVersion);
	ENUM(GdiplusNotInitialized);
	ENUM(PropertyNotFound);
	ENUM(PropertyNotSupported);
	//	ENUM(ProfileNotFound);

	ENUM(FontStyleRegular);
	ENUM(FontStyleBold);
	ENUM(FontStyleItalic);
	ENUM(FontStyleBoldItalic);
	ENUM(FontStyleUnderline);
	ENUM(FontStyleStrikeout);

	ENUM(BrushTypeSolidColor);
	ENUM(BrushTypeHatchFill);
	ENUM(BrushTypeTextureFill);
	ENUM(BrushTypePathGradient);
	ENUM(BrushTypeLinearGradient);

	ENUM(DashCapFlat);
	ENUM(DashCapRound);
	ENUM(DashCapTriangle);

	ENUM(DashStyleSolid);
	ENUM(DashStyleDash);
	ENUM(DashStyleDot);
	ENUM(DashStyleDashDot);
	ENUM(DashStyleDashDotDot);

	ENUM(HatchStyleHorizontal);
	ENUM(HatchStyleVertical);
	ENUM(HatchStyleForwardDiagonal);
	ENUM(HatchStyleBackwardDiagonal);
	ENUM(HatchStyleCross);
	ENUM(HatchStyleDiagonalCross);
	ENUM(HatchStyle05Percent);
	ENUM(HatchStyle10Percent);
	ENUM(HatchStyle20Percent);
	ENUM(HatchStyle25Percent);
	ENUM(HatchStyle30Percent);
	ENUM(HatchStyle40Percent);
	ENUM(HatchStyle50Percent);
	ENUM(HatchStyle60Percent);
	ENUM(HatchStyle70Percent);
	ENUM(HatchStyle75Percent);
	ENUM(HatchStyle80Percent);
	ENUM(HatchStyle90Percent);
	ENUM(HatchStyleLightDownwardDiagonal);
	ENUM(HatchStyleLightUpwardDiagonal);
	ENUM(HatchStyleDarkDownwardDiagonal);
	ENUM(HatchStyleDarkUpwardDiagonal);
	ENUM(HatchStyleWideDownwardDiagonal);
	ENUM(HatchStyleWideUpwardDiagonal);
	ENUM(HatchStyleLightVertical);
	ENUM(HatchStyleLightHorizontal);
	ENUM(HatchStyleNarrowVertical);
	ENUM(HatchStyleNarrowHorizontal);
	ENUM(HatchStyleDarkVertical);
	ENUM(HatchStyleDarkHorizontal);
	ENUM(HatchStyleDashedDownwardDiagonal);
	ENUM(HatchStyleDashedUpwardDiagonal);
	ENUM(HatchStyleDashedHorizontal);
	ENUM(HatchStyleDashedVertical);
	ENUM(HatchStyleSmallConfetti);
	ENUM(HatchStyleLargeConfetti);
	ENUM(HatchStyleZigZag);
	ENUM(HatchStyleWave);
	ENUM(HatchStyleDiagonalBrick);
	ENUM(HatchStyleHorizontalBrick);
	ENUM(HatchStyleWeave);
	ENUM(HatchStylePlaid);
	ENUM(HatchStyleDivot);
	ENUM(HatchStyleDottedGrid);
	ENUM(HatchStyleDottedDiamond);
	ENUM(HatchStyleShingle);
	ENUM(HatchStyleTrellis);
	ENUM(HatchStyleSphere);
	ENUM(HatchStyleSmallGrid);
	ENUM(HatchStyleSmallCheckerBoard);
	ENUM(HatchStyleLargeCheckerBoard);
	ENUM(HatchStyleOutlinedDiamond);
	ENUM(HatchStyleSolidDiamond);
	ENUM(HatchStyleTotal);
	ENUM(HatchStyleLargeGrid);
	ENUM(HatchStyleMin);
	ENUM(HatchStyleMax);

	ENUM(LinearGradientModeHorizontal);
	ENUM(LinearGradientModeVertical);
	ENUM(LinearGradientModeForwardDiagonal);
	ENUM(LinearGradientModeBackwardDiagonal);

	ENUM(LineCapFlat);
	ENUM(LineCapSquare);
	ENUM(LineCapRound);
	ENUM(LineCapTriangle);
	ENUM(LineCapNoAnchor);
	ENUM(LineCapSquareAnchor);
	ENUM(LineCapRoundAnchor);
	ENUM(LineCapDiamondAnchor);
	ENUM(LineCapArrowAnchor);

	ENUM(LineJoinMiter);
	ENUM(LineJoinBevel);
	ENUM(LineJoinRound);
	ENUM(LineJoinMiterClipped);

	ENUM(PenAlignmentCenter);
	ENUM(PenAlignmentInset);

	ENUM(WrapModeTile);
	ENUM(WrapModeTileFlipX);
	ENUM(WrapModeTileFlipY);
	ENUM(WrapModeTileFlipXY);
	ENUM(WrapModeClamp);

	ENUM(MatrixOrderPrepend);
	ENUM(MatrixOrderAppend);

	ENUM(ImageTypeUnknown);
	ENUM(ImageTypeBitmap);
	ENUM(ImageTypeMetafile);

	ENUM(RotateNoneFlipNone);
	ENUM(Rotate90FlipNone);
	ENUM(Rotate180FlipNone);
	ENUM(Rotate270FlipNone);
	ENUM(RotateNoneFlipX);
	ENUM(Rotate90FlipX);
	ENUM(Rotate180FlipX);
	ENUM(Rotate270FlipX);
	ENUM(RotateNoneFlipY);
	ENUM(Rotate90FlipY);
	ENUM(Rotate180FlipY);
	ENUM(Rotate270FlipY);
	ENUM(RotateNoneFlipXY);
	ENUM(Rotate90FlipXY);
	ENUM(Rotate180FlipXY);
	ENUM(Rotate270FlipXY);

	ENUM(SmoothingModeInvalid);
	ENUM(SmoothingModeDefault);
	ENUM(SmoothingModeHighSpeed);
	ENUM(SmoothingModeHighQuality);
	ENUM(SmoothingModeNone);
	ENUM(SmoothingModeAntiAlias);

	ENUM(TextRenderingHintSystemDefault);
	ENUM(TextRenderingHintSingleBitPerPixelGridFit);
	ENUM(TextRenderingHintSingleBitPerPixel);
	ENUM(TextRenderingHintAntiAliasGridFit);
	ENUM(TextRenderingHintAntiAlias);
	ENUM(TextRenderingHintClearTypeGridFit);

	// statics
	NCB_METHOD(addPrivateFont);
	NCB_METHOD(getFontList);

	// classes
	NCB_SUBCLASS_NAME(PointF);
	NCB_SUBCLASS_NAME(RectF);

	NCB_GDIP_SUBCLASS(Image);
	NCB_GDIP_SUBCLASS(Matrix);

	NCB_SUBCLASS(Font, FontInfo);
	NCB_SUBCLASS(Appearance, Appearance);
    NCB_SUBCLASS(Path, Path);
}

NCB_GET_INSTANCE_HOOK(LayerExDraw)
{
	// �C���X�^���X�Q�b�^
	NCB_INSTANCE_GETTER(objthis) { // objthis �� iTJSDispatch2* �^�̈����Ƃ���
		ClassT* obj = GetNativeInstance(objthis);	// �l�C�e�B�u�C���X�^���X�|�C���^�擾
		if (!obj) {
			obj = new ClassT(objthis);				// �Ȃ��ꍇ�͐�������
			SetNativeInstance(objthis, obj);		// objthis �� obj ���l�C�e�B�u�C���X�^���X�Ƃ��ēo�^����
		}
		obj->reset();
		return obj;
	}
	// �f�X�g���N�^�i���ۂ̃��\�b�h���Ă΂ꂽ��ɌĂ΂��j
	~NCB_GET_INSTANCE_HOOK_CLASS() {
	}
};

#define LAYEREX_METHOD(type,name)  Method(TJS_W(# name), &Type::name, Bridge<LayerExDraw::BridgeFunctor<type>>())

/**
 * Image �̓��b�s���O����K�v������̂� rawcallback �őΉ�
 */
static tjs_error TJS_INTF_METHOD
GetRecordImage(tTJSVariant* result, tjs_int numparams,
	tTJSVariant** param, iTJSDispatch2* objthis)
{
	LayerExDraw* obj = ncbInstanceAdaptor<LayerExDraw>::GetNativeInstance(objthis, true);
	if (result) result->Clear();
	if (obj) {
		Image* image = obj->getRecordImage();
		if (image) {
			typedef GdipWrapper<Image> WrapperT;
			WrapperT* wrap = new WrapperT(image);
			iTJSDispatch2* adpobj = ncbInstanceAdaptor<WrapperT>::CreateAdaptor(wrap);
			if (adpobj) {
				if (result) *result = tTJSVariant(adpobj, adpobj);
				adpobj->Release();
			}
			else {
				delete wrap;
				delete image;
			}
		}
	}
	return TJS_S_OK;
}

// �t�b�N���A�^�b�`
NCB_ATTACH_CLASS_WITH_HOOK(LayerExDraw, Layer) {
	NCB_PROPERTY(updateWhenDraw, getUpdateWhenDraw, setUpdateWhenDraw);
	NCB_PROPERTY(smoothingMode, getSmoothingMode, setSmoothingMode);
	NCB_PROPERTY(textRenderingHint, getTextRenderingHint, setTextRenderingHint);

	NCB_METHOD(setViewTransform);
	NCB_METHOD(resetViewTransform);
	NCB_METHOD(rotateViewTransform);
	NCB_METHOD(scaleViewTransform);
	NCB_METHOD(translateViewTransform);

	NCB_METHOD(setTransform);
	NCB_METHOD(resetTransform);
	NCB_METHOD(rotateTransform);
	NCB_METHOD(scaleTransform);
	NCB_METHOD(translateTransform);

	NCB_METHOD(clear);
    NCB_METHOD(drawPath);
	NCB_METHOD(drawArc);
	NCB_METHOD(drawPie);
	NCB_METHOD(drawBezier);
	NCB_METHOD(drawBeziers);
	NCB_METHOD(drawClosedCurve);
	NCB_METHOD(drawClosedCurve2);
	NCB_METHOD(drawCurve);
	NCB_METHOD(drawCurve2);
	NCB_METHOD(drawCurve3);
	NCB_METHOD(drawEllipse);
	NCB_METHOD(drawLine);
	NCB_METHOD(drawLines);
	NCB_METHOD(drawPolygon);
	NCB_METHOD(drawRectangle);
	NCB_METHOD(drawRectangles);
	NCB_METHOD(drawPathString);
	NCB_METHOD(drawString);
	NCB_METHOD(measureString);
	NCB_METHOD(measureStringInternal);

	NCB_METHOD(drawImage);
	NCB_METHOD(drawImageRect);
	NCB_METHOD(drawImageStretch);
	NCB_METHOD(drawImageAffine);

	NCB_PROPERTY(record, getRecord, setRecord);
	NCB_METHOD_RAW_CALLBACK(getRecordImage, GetRecordImage, 0);
	NCB_METHOD(redrawRecord);
	NCB_METHOD(saveRecord);
	NCB_METHOD(loadRecord);
}

// ----------------------------------- �N���E�J������

NCB_PRE_REGIST_CALLBACK(initGdiPlus);
NCB_POST_UNREGIST_CALLBACK(deInitGdiPlus);

