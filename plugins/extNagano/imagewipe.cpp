#include "tjsCommHead.h"
#include "cpu_types.h"
#include "transhandler.h"
#include "tjsErrorDefs.h"
#include "TVPMsg.h"
#include "TVPTrans.h"
#include "tjsVariant.h"
#include "tjsConfig.h"
#include "imagewipe.h"
#include "common.h"

//---------------------------------------------------------------------------
// imagewipe トランジション
//   任意の画像 (ルール画像) を使えるワイプ。
//   ルール画像 (32bpp) の各行について「アルファが十分高い一番右の列」を
//   その行の切り替え端 (エッジ) とし、時間経過で左右に掃引する掃き位置
//   (threshold) にこのエッジを足した所を境界として src1/src2 を切り替える。
//   境界の手前 (ルール画像のエッジまでの帯) にはルール画像自身をアルファ
//   合成して装飾的な縁取りを描く。
//
//   復元元 (extNagano.dll / Ghidra):
//     Provider::GetName          FUN_10007620  (-> L"imagewipe")
//     Provider::AddRef           FUN_10007730
//     Provider::Release          FUN_10007640
//     Provider::StartTransition  FUN_10007780
//     Handler ctor               FUN_100076c0
//     Handler::Setup(エッジ表)   FUN_100070a0
//     Handler::AddRef            FUN_10007730
//     Handler::Release           FUN_10007740
//     Handler::SetOption         FUN_10016870 (return 0)
//     Handler::StartProcess      FUN_10007140
//     Handler::EndProcess        FUN_10007220
//     Handler::Process           FUN_100072b0
//     Handler::MakeFinalImage    FUN_10006e90 (*dest = src2)
//     ルール画像帯の合成          FUN_10007290 -> FUN_10018af0
//     レイヤ→スキャンライン供給   tLayerScanLineProvider
//        AddRef FUN_100018a0 / Release FUN_10001c20 / GetWidth FUN_10001c50 /
//        GetHeight FUN_10001d30 / GetPixelFormat FUN_10001e10 /
//        GetPitchBytes FUN_10001e20 / GetScanLine FUN_10002060 /
//        GetScanLineForWrite FUN_10002160 (いずれも Layer の PropGet を利用)
//
//   ※ デコンパイルされた StartTransition (FUN_10007780) は rule を
//      GetAsString + imagepro->LoadImage() で読み込む「パス文字列」経路のみを
//      持つ。ドキュメントが謳う「レイヤオブジェクト指定」に対応するため、
//      本復元では rule の型を判定し、オブジェクトなら下記の
//      tLayerScanLineProvider (元 DLL 内の同名ヘルパを本ファイルに自己完結で
//      再現したもの) でラップする分岐を追加している。
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// tLayerScanLineProvider (自己完結・静的ヘルパ)
//   TJS2 の Layer オブジェクトを iTVPScanLineProvider として見せる。
//   幅/高さ/ピッチ/バッファ先頭を PropGet で取得する。
//   (元 DLL: FUN_100018a0 系。imagewipe / 3duniversal が rule=レイヤ の際に使用)
//   確信度: 中。scanline の base+line*pitch は full-size レイヤ前提で、元コードに
//   あった imageLeft/imageTop 補正 (FUN_10001f00) は省略している (要実機確認)。
//---------------------------------------------------------------------------
class tTVPImageWipeLayerScanLineProvider : public iTVPScanLineProvider
{
	tjs_int RefCount;
	tTJSVariantClosure Closure; // ラップ対象 Layer オブジェクト (AddRef 済)

	tjs_int GetIntProp(const tjs_char *name)
	{
		tTJSVariant v;
		if(TJS_FAILED(Closure.PropGet(0, name, NULL, &v, NULL))) return 0;
		if(v.Type() == tvtVoid) return 0;
		return (tjs_int)(tjs_int64)v;
	}

	tjs_uint8 * GetBufferPtr(const tjs_char *name, tjs_int line)
	{
		// mainImageBuffer(ForWrite) の先頭アドレス + line*pitch を返す。
		// アドレスは 64bit ポインタを整数プロパティとして受け取る。
		tjs_int64 base = 0;
		{
			tTJSVariant v;
			if(TJS_FAILED(Closure.PropGet(0, name, NULL, &v, NULL))) return NULL;
			if(v.Type() == tvtVoid) return NULL;
			base = (tjs_int64)v;
		}
		tjs_int pitch = GetIntProp(TJS_N("mainImageBufferPitch"));
		return (tjs_uint8 *)(tjs_intptr_t)base + line * pitch;
	}

public:
	tTVPImageWipeLayerScanLineProvider(const tTJSVariant &layer)
	{
		RefCount = 1;
		Closure = layer.AsObjectClosureNoAddRef();
		Closure.AddRef();
	}
	virtual ~tTVPImageWipeLayerScanLineProvider()
	{
		Closure.Release();
	}

	tjs_error AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }

	tjs_error GetWidth(tjs_int *width)
		{ if(width) *width = GetIntProp(TJS_N("width")); return TJS_S_OK; }
	tjs_error GetHeight(tjs_int *height)
		{ if(height) *height = GetIntProp(TJS_N("height")); return TJS_S_OK; }
	tjs_error GetPixelFormat(tjs_int *bpp)
		{ if(bpp) *bpp = 32; return TJS_S_OK; } // 元コード FUN_10001e10 は常に 0x20
	tjs_error GetPitchBytes(tjs_int *pitch)
		{ if(pitch) *pitch = GetIntProp(TJS_N("mainImageBufferPitch")); return TJS_S_OK; }

	tjs_error GetScanLine(tjs_int line, const void ** scanline)
	{
		tjs_uint8 *p = GetBufferPtr(TJS_N("mainImageBuffer"), line);
		if(!p) return TJS_E_FAIL;
		if(scanline) *scanline = p;
		return TJS_S_OK;
	}
	tjs_error GetScanLineForWrite(tjs_int line, void ** scanline)
	{
		tjs_uint8 *p = GetBufferPtr(TJS_N("mainImageBufferForWrite"), line);
		if(!p) return TJS_E_FAIL;
		if(scanline) *scanline = p;
		return TJS_S_OK;
	}
	iTVPTexture2D * GetTexture() { return 0; }
	iTVPTexture2D * GetTextureForRender() { return 0; }
};
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// imagewipe ハンドラ
//---------------------------------------------------------------------------
class tTVPImageWipeTransHandler : public iTVPDivisibleTransHandler
{
	tjs_int RefCount;

protected:
	tjs_uint64 StartTick;             // 開始 tick        (+0x08)
	tjs_uint64 Time;                  // 所要時間          (+0x10)
	tjs_int Width;                    // 画像幅            (+0x20)
	tjs_int Height;                   // 画像高            (+0x24)
	tjs_uint64 Elapsed;              // 経過時間(Timeでクランプ) (+0x18)
	tjs_int Threshold;                // 現在の掃き位置(絶対X) (+0x28)
	iTVPScanLineProvider *Rule;       // ルール画像        (+0x34)
	tjs_int Dir;                      // 方向 0:左→右 1:右→左 (+0x38)
	bool First;                       // 最初の呼び出しか   (+0x3c)
	tjs_int FrameCount;               // フレーム数         (+0x40)

	tjs_int RuleWidth;                // ルール画像幅
	tjs_int RuleHeight;               // ルール画像高
	tjs_int *RuleEdge;                // 行ごとの切り替えエッジ (+0x30, size=Height)

	void Setup(); // RuleEdge を構築 (元 FUN_100070a0)

public:
	tTVPImageWipeTransHandler(tjs_uint64 time, tjs_int width, tjs_int height,
			iTVPScanLineProvider *rule, tjs_int dir)
		: StartTick(0), Time(time), Width(width), Height(height),
		  Elapsed(0), Threshold(0), Rule(rule), Dir(dir), First(true),
		  FrameCount(0), RuleWidth(0), RuleHeight(0), RuleEdge(NULL)
	{
		RefCount = 1;
		if(Rule) Rule->AddRef();
		Setup();
	}
	virtual ~tTVPImageWipeTransHandler()
	{
		if(RuleEdge) delete[] RuleEdge;
		if(Rule) Rule->Release();
	}

	tjs_error AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }

	tjs_error SetOption(iTVPSimpleOptionProvider *options) { return TJS_S_OK; }

	tjs_error StartProcess(tjs_uint64 tick);
	tjs_error EndProcess();
	tjs_error Process(tTVPDivisibleData *data);

	tjs_error MakeFinalImage(
			iTVPScanLineProvider ** dest,
			iTVPScanLineProvider * src1,
			iTVPScanLineProvider * src2)
	{
		*dest = src2;
		return TJS_S_OK;
	}
};
//---------------------------------------------------------------------------
void tTVPImageWipeTransHandler::Setup()
{
	// ルール画像の各行について「アルファ(MSB) が 0xf0 を超える一番右の列」を
	// その行の切り替えエッジとする。存在しなければ幅/2。(元 FUN_100070a0)
	RuleEdge = new tjs_int[Height > 0 ? Height : 1];

	RuleWidth = 0;
	RuleHeight = 0;
	if(Rule)
	{
		Rule->GetWidth(&RuleWidth);
		Rule->GetHeight(&RuleHeight);
	}

	for(tjs_int row = 0; row < Height; row++)
	{
		RuleEdge[row] = RuleWidth / 2; // 既定値

		// ルール高がソース高と異なる場合に備えクランプ
		tjs_int rrow = row;
		if(RuleHeight > 0 && rrow >= RuleHeight) rrow = RuleHeight - 1;

		const tjs_uint32 *rule;
		if(!Rule || TJS_FAILED(Rule->GetScanLine(rrow, (const void**)&rule)))
			continue;

		for(tjs_int col = 0; col < RuleWidth; col++)
		{
			// (byte)[col*4 + 3] = ARGB のアルファ
			if((rule[col] >> 24) > 0xf0)
				RuleEdge[row] = col;
		}
	}
}
//---------------------------------------------------------------------------
tjs_error tTVPImageWipeTransHandler::StartProcess(tjs_uint64 tick)
{
	// 元 FUN_10007140
	FrameCount++;
	if(First)
	{
		First = false;
		StartTick = tick;
	}
	tjs_uint64 elapsed = tick - StartTick;
	if(elapsed >= Time) elapsed = Time;
	Elapsed = elapsed;

	// 掃き位置 threshold: (Width+RuleWidth) を Time で按分し、RuleWidth 分だけ
	// 左へずらす。dir0 は経過に従い増加、dir1 は減少 (左右反転)。
	//   dir0: elapsed=0 で -RuleWidth, elapsed=Time で Width
	//   dir1: elapsed=0 で  Width,     elapsed=Time で -RuleWidth
	tjs_int64 span = (tjs_int64)(Width + RuleWidth);
	tjs_int64 num = (Dir == 0) ? (tjs_int64)elapsed : (tjs_int64)(Time - elapsed);
	Threshold = (tjs_int)(span * num / (tjs_int64)Time) - RuleWidth;

	return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
tjs_error tTVPImageWipeTransHandler::EndProcess()
{
	// 元 FUN_10007220 (fps ログは省略)
	if(Elapsed >= Time) return TJS_S_FALSE; // 終了
	return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
tjs_error tTVPImageWipeTransHandler::Process(tTVPDivisibleData *data)
{
	// 元 FUN_100072b0。
	//   dir0: 左領域=src2(新), 右領域=src1(旧)  … 新画像を左から現す
	//   dir1: 左領域=src1(旧), 右領域=src2(新)  … 新画像を右から現す
	// 帯 [threshold, threshold+RuleEdge[y]) では「左領域ソース」の上に
	// ルール画像 (自身のアルファで) を合成する (元 FUN_10018af0)。
	const tjs_int th = Threshold;
	const tjs_int left  = data->Left;
	const tjs_int right = data->Left + data->Width;

	for(tjs_int n = 0; n < data->Height; n++)
	{
		tjs_int y = data->Top + n; // src / rule の行 (scanline.cpp と同じ流儀)

		tjs_uint32 *dest;
		const tjs_uint32 *src1;
		const tjs_uint32 *src2;
		const tjs_uint32 *rule = NULL;
		if(TJS_FAILED(data->Dest->GetScanLineForWrite(data->DestTop + n, (void**)&dest)))
			return TJS_E_FAIL;
		if(TJS_FAILED(data->Src1->GetScanLine(y, (const void**)&src1)))
			return TJS_E_FAIL;
		if(TJS_FAILED(data->Src2->GetScanLine(y, (const void**)&src2)))
			return TJS_E_FAIL;

		// この行の切り替えエッジとルール scanline
		tjs_int edge = (y >= 0 && y < Height) ? RuleEdge[y] : (RuleWidth / 2);
		if(Rule)
		{
			tjs_int rrow = y;
			if(RuleHeight > 0 && rrow >= RuleHeight) rrow = RuleHeight - 1;
			if(rrow < 0) rrow = 0;
			Rule->GetScanLine(rrow, (const void**)&rule);
		}

		// dir に応じ左右のソースを割り当てる
		const tjs_uint32 *leftSrc  = (Dir == 0) ? src2 : src1;
		const tjs_uint32 *rightSrc = (Dir == 0) ? src1 : src2;

		// dest の絶対列 c は dp[c] に書き込む (scanline.cpp と同一方式)
		tjs_uint32 *dp = dest + data->DestLeft - data->Left;

		tjs_int bandEnd = th + edge; // 境界 (この列以降は右領域ソース)

		for(tjs_int c = left; c < right; c++)
		{
			if(c < th)
			{
				dp[c] = leftSrc[c];
			}
			else if(c < bandEnd)
			{
				tjs_int rcol = c - th; // 0..edge-1 ( < RuleWidth )
				if(rule && rcol >= 0 && rcol < RuleWidth)
				{
					tjs_uint32 rp = rule[rcol];
					tjs_int opa = (tjs_int)(rp >> 24); // ルール画像自身のアルファで合成
					// Blend(base, top, opa) = base + (top-base)*opa>>8
					dp[c] = Blend(leftSrc[c], rp, opa);
				}
				else
				{
					dp[c] = leftSrc[c];
				}
			}
			else
			{
				dp[c] = rightSrc[c];
			}
		}
	}
	return TJS_S_OK;
}
//---------------------------------------------------------------------------
class tTVPImageWipeTransHandlerProvider : public iTVPTransHandlerProvider
{
	tjs_uint RefCount;
public:
	tTVPImageWipeTransHandlerProvider() { RefCount = 1; }
	~tTVPImageWipeTransHandlerProvider() {}

	tjs_error AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }

	tjs_error GetName(const tjs_char ** name)
	{
		if(name) *name = TJS_N("imagewipe"); // 元 FUN_10007620
		return TJS_S_OK;
	}

	tjs_error StartTransition(
			iTVPSimpleOptionProvider *options,
			iTVPSimpleImageProvider *imagepro,
			tTVPLayerType layertype,
			tjs_uint src1w, tjs_uint src1h,
			tjs_uint src2w, tjs_uint src2h,
			tTVPTransType *type,
			tTVPTransUpdateType * updatetype,
			iTVPBaseTransHandler ** handler)
	{
		// 元 FUN_10007780
		if(type) *type = ttExchange;            // *param_9 = 1
		if(updatetype) *updatetype = tutDivisibleFade; // *param_10 = 0
		if(!handler) return TJS_E_FAIL;
		if(!options) return TJS_E_FAIL;
		if(src1w != src2w || src1h != src2h) return TJS_E_FAIL;

		// time
		tTJSVariant tmp;
		if(TJS_FAILED(options->GetValue(TJS_N("time"), &tmp))) return TJS_E_FAIL;
		if(tmp.Type() == tvtVoid) return TJS_E_FAIL;
		tjs_uint64 time = (tjs_int64)tmp;
		if(time < 2) time = 2;

		// rule: 文字列(パス) なら imagepro->LoadImage、オブジェクトならレイヤをラップ
		tTJSVariant rulevar;
		if(TJS_FAILED(options->GetValue(TJS_N("rule"), &rulevar))) return TJS_E_FAIL;
		if(rulevar.Type() == tvtVoid) return TJS_E_FAIL;

		iTVPScanLineProvider *scpro = NULL;
		if(rulevar.Type() == tvtObject)
		{
			// レイヤオブジェクト指定 (自己完結ヘルパでラップ)
			scpro = new tTVPImageWipeLayerScanLineProvider(rulevar);
		}
		else
		{
			// パス文字列指定 (元コードの経路: GetAsString + LoadImage)
			//   bpp=32, colorkey=0x02ffffff, w=0(自然幅), h=src1h
			const tjs_char *rulename;
			if(TJS_FAILED(options->GetAsString(TJS_N("rule"), &rulename)))
				return TJS_E_FAIL;
			if(TJS_FAILED(imagepro->LoadImage(rulename, 32, 0x02ffffff,
					0, src1h, &scpro)))
				return TJS_E_FAIL;
		}
		if(!scpro) return TJS_E_FAIL;

		// dir (既定 0)
		tjs_int dir = 0;
		tTJSVariant dirvar;
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("dir"), &dirvar)) &&
		   dirvar.Type() != tvtVoid)
			dir = (tjs_int)(tjs_int64)dirvar;

		*handler = new tTVPImageWipeTransHandler(time, src1w, src1h, scpro, dir);
		scpro->Release(); // ハンドラ側で AddRef 済 (元コードと同じ)
		return TJS_S_OK;
	}

} static * ImageWipeTransHandlerProvider;
//---------------------------------------------------------------------------
void RegisterImageWipeTransHandlerProvider()
{
	ImageWipeTransHandlerProvider = new tTVPImageWipeTransHandlerProvider();
	TVPAddTransHandlerProvider(ImageWipeTransHandlerProvider);
}
//---------------------------------------------------------------------------
void UnregisterImageWipeTransHandlerProvider()
{
	TVPRemoveTransHandlerProvider(ImageWipeTransHandlerProvider);
	ImageWipeTransHandlerProvider->Release();
}
//---------------------------------------------------------------------------
