#include "tjsCommHead.h"
#include "cpu_types.h"
#include "transhandler.h"
#include "tjsErrorDefs.h"
#include "TVPMsg.h"
#include "TVPTrans.h"
#include "tjsVariant.h"
#include "tjsConfig.h"
#include "zoomfade.h"
#include "common.h"

//---------------------------------------------------------------------------
// zoomfade トランジション
//   元画像 (src1) を拡大しつつ、先画像 (src2) に切り替えるトランジション。
//   ・src1 は等倍(100%)から zoom1% へ拡大していく (画面中心を基準)。
//   ・src2 は zoom2% から等倍(100%)へ縮小してくる (画面中心を基準)。
//   ・両者を経過時間に応じた不透明度 phase(0..255) でクロスフェード合成する。
//   最近傍サンプリングでズーム後の座標から画素を取得する。
//   (extNagano.dll: tTVPZoomFadeTransHandler の復元)
//
//   復元元 (Ghidra FUN_):
//     Provider::GetName          = FUN_10018ff0  (-> L"zoomfade")
//     Provider::StartTransition  = FUN_10019190  (time / zoom1 / zoom2 読取+既定値)
//     Handler ctor               = FUN_10019090  (幅高/ズーム値の保持、x マップ確保)
//     Handler::StartProcess      = FUN_10018b70  (phase と拡大/縮小スケールの算出)
//     Handler::EndProcess        = FUN_10018c20  (phase==255 で終了)
//     Handler::Process           = FUN_10018c80  (ズームサンプリング+アルファ合成)
//     Handler::MakeFinalImage    = FUN_10006e90  (*dest = src2)
//     AddRef=FUN_10007730, Release=FUN_10016840, SetOption=FUN_10016870(no-op)
//     オプション取得ヘルパ FUN_100019c0 = GetIntWithDefault(options, name, &out)
//     アルファ合成 (else 経路) FUN_10018af0 = 各成分 src1->src2 を phase で線形補間
//     浮動小数->整数丸め FUN_10019af0 (固定小数点 <<10 でのサンプル座標生成)
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
static inline tjs_int ClampI(tjs_int v, tjs_int lo, tjs_int hi)
{
	if(v < lo) v = lo;
	if(v > hi) v = hi;
	return v;
}
//---------------------------------------------------------------------------
class tTVPZoomFadeTransHandler : public iTVPDivisibleTransHandler
{
	tjs_int RefCount;                         //                          (+0x04)

protected:
	tjs_uint64 StartTick;    // 開始 tick                                (+0x08)
	tjs_uint64 Time;         // 所要時間                                 (+0x10)
	tjs_uint64 CurElapsed;   // 現在の経過時間(0..Time)                  (+0x18)
	tjs_int Width;           // 画像幅                                   (+0x20)
	tjs_int Height;          // 画像高                                   (+0x24)
	double Zoom1;            // 元画像の目標ズーム倍率 (zoom1/100)       (+0x28)
	double Zoom2;            // 先画像の開始ズーム倍率 (zoom2/100)       (+0x30)
	tjs_int Phase;           // 混合比/経過率 0..255                     (+0x38)
	double SrcScale;         // src1 サンプル用スケール = 1/現在拡大率   (+0x40)
	double DestScale;        // src2 サンプル用スケール = 1/現在縮小率   (+0x48)
	bool First;              // 最初の呼び出しか                         (+0x50)
	tjs_int * XMap1;         // src1 の列サンプルマップ (幅 Width)       (+0x54)
	tjs_int * XMap2;         // src2 の列サンプルマップ (幅 Width)       (+0x58)
	tjs_int FrameCount;      // フレーム数 (fps ログ用)                  (+0x5c)

public:
	tTVPZoomFadeTransHandler(tjs_uint64 time, tjs_int width, tjs_int height,
			tjs_int zoom1, tjs_int zoom2)
		: StartTick(0), Time(time), CurElapsed(0), Width(width), Height(height),
		  Zoom1(zoom1 / 100.0), Zoom2(zoom2 / 100.0), Phase(0),
		  SrcScale(1.0), DestScale(1.0), First(true),
		  XMap1(0), XMap2(0), FrameCount(0)
	{
		RefCount = 1;
		// 列サンプルマップを確保 (FUN_10019090 の this+0x54 / this+0x58)
		if(Width > 0)
		{
			XMap1 = new tjs_int[Width];
			XMap2 = new tjs_int[Width];
		}
	}
	virtual ~tTVPZoomFadeTransHandler()
	{
		if(XMap1) delete [] XMap1;
		if(XMap2) delete [] XMap2;
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
tjs_error tTVPZoomFadeTransHandler::StartProcess(tjs_uint64 tick)
{
	// FUN_10018b70
	FrameCount++;
	if(First)
	{
		First = false;
		StartTick = tick;
	}

	tjs_uint64 elapsed = tick - StartTick;
	if(elapsed > Time) elapsed = Time;  // 経過時間を Time でクランプ
	CurElapsed = elapsed;

	// 経過率を 0..255 の phase に変換 (elapsed*255/Time)
	tjs_int phase = (tjs_int)((elapsed * 255) / Time);
	if(phase > 255) phase = 255;
	Phase = phase;

	// 現在の拡大/縮小率を求め、サンプリングに使う逆数スケールを算出する。
	//   src1 の拡大率 : 1.0 -> Zoom1   (phase 0..255)
	//   src2 の縮小率 : Zoom2 -> 1.0    (phase 0..255)
	// SrcScale/DestScale は「出力座標 -> 元画像座標」の倍率 (= 1/表示倍率)。
	SrcScale  = 1.0 / (((Zoom1 - 1.0) * phase) / 255.0 + 1.0);
	DestScale = 1.0 / (Zoom2 - ((Zoom2 - 1.0) * phase) / 255.0);

	return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
tjs_error tTVPZoomFadeTransHandler::EndProcess()
{
	// FUN_10018c20: phase が 255 に達したら終了
	if(Phase == 255) return TJS_S_FALSE;
	return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
tjs_error tTVPZoomFadeTransHandler::Process(tTVPDivisibleData *data)
{
	// FUN_10018c80
	const tjs_int W = Width;
	const tjs_int H = Height;
	const double cx = W * 0.5;   // ズーム中心 X
	const double cy = H * 0.5;   // ズーム中心 Y
	const double invS = SrcScale;    // src1 用スケール
	const double invD = DestScale;   // src2 用スケール
	const tjs_int phase = Phase;

	const tjs_int left  = data->Left;
	const tjs_int right = data->Left + data->Width;

	if(!XMap1 || !XMap2) return TJS_E_FAIL;

	// --- 列サンプルマップの構築 -----------------------------------------
	//   中心 cx を基準に、出力列 c を元画像列へ写像する。
	//     src1 : sx = cx + (c - cx) * invS
	//     src2 : sx = cx + (c - cx) * invD
	//   (元DLL は列ループを画像左端 0 起点で回すが、ここでは data->Left を
	//    考慮して分割領域でも正しく写像されるよう一般化している)
	for(tjs_int c = left; c < right; c++)
	{
		tjs_int idx = c - left;
		tjs_int xs = (tjs_int)(cx + (c - cx) * invS);
		tjs_int xd = (tjs_int)(cx + (c - cx) * invD);
		XMap1[idx] = ClampI(xs, 0, W - 1);
		XMap2[idx] = ClampI(xd, 0, W - 1);
	}

	// --- 行ごとに合成 ---------------------------------------------------
	for(tjs_int n = 0; n < data->Height; n++)
	{
		tjs_int outY = data->Top + n;

		// ズーム後の元画像行 (最近傍)。中心 cy を基準に写像する。
		tjs_int ys1 = ClampI((tjs_int)(cy + (outY - cy) * invS), 0, H - 1);
		tjs_int ys2 = ClampI((tjs_int)(cy + (outY - cy) * invD), 0, H - 1);

		tjs_uint32 *dest;
		const tjs_uint32 *src1;
		const tjs_uint32 *src2;
		if(TJS_FAILED(data->Dest->GetScanLineForWrite(data->DestTop + n, (void**)&dest)))
			return TJS_E_FAIL;
		if(TJS_FAILED(data->Src1->GetScanLine(ys1, (const void**)&src1)))
			return TJS_E_FAIL;
		if(TJS_FAILED(data->Src2->GetScanLine(ys2, (const void**)&src2)))
			return TJS_E_FAIL;

		// dest の絶対列 c は dp[c] に書き込む (scanline.cpp と同じ基点)
		tjs_uint32 *dp = dest + data->DestLeft - data->Left;

		for(tjs_int c = left; c < right; c++)
		{
			tjs_int idx = c - left;
			tjs_uint32 s1 = src1[XMap1[idx]];
			tjs_uint32 s2 = src2[XMap2[idx]];

			tjs_uint32 s1a = s1 >> 24;
			tjs_uint32 s2a = s2 >> 24;

			tjs_uint32 col;
			if(s2a == 0)
			{
				// src2 が完全透明の画素: src1 の RGB を保ちアルファをフェードアウト。
				// (元DLL: 減衰後アルファ = src1α*(255-phase)/256、RGB は src1 のまま)
				tjs_uint32 na = (s1a * (tjs_uint32)(255 - phase)) >> 8;
				col = (s1 & 0x00ffffff) | (na << 24);
			}
			else if(s1a == 0)
			{
				// src1 が完全透明の画素: src2 の RGB を保ちアルファをフェードイン。
				// (元DLL: 増加後アルファ = src2α*phase/256、RGB は src2 のまま)
				tjs_uint32 na = (s2a * (tjs_uint32)phase) >> 8;
				col = (s2 & 0x00ffffff) | (na << 24);
			}
			else
			{
				// 通常経路: 各成分(A 含む)を phase で src1 -> src2 に線形補間。
				// (FUN_10018af0 と等価。common.h の Blend: opa=0->s1, 255->s2)
				col = Blend(s1, s2, phase);
			}
			dp[c] = col;
		}
	}
	return TJS_S_OK;
}
//---------------------------------------------------------------------------
class tTVPZoomFadeTransHandlerProvider : public iTVPTransHandlerProvider
{
	tjs_uint RefCount;
public:
	tTVPZoomFadeTransHandlerProvider() { RefCount = 1; }
	~tTVPZoomFadeTransHandlerProvider() {}

	tjs_error AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }

	tjs_error GetName(const tjs_char ** name)
	{
		if(name) *name = TJS_N("zoomfade");
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
		// FUN_10019190
		if(type) *type = ttExchange;
		if(updatetype) *updatetype = tutDivisible;
		if(!handler) return TJS_E_FAIL;
		if(!options) return TJS_E_FAIL;
		if(src1w != src2w || src1h != src2h) return TJS_E_FAIL;

		tTJSVariant tmp;

		// time は必須
		if(TJS_FAILED(options->GetValue(TJS_N("time"), &tmp))) return TJS_E_FAIL;
		if(tmp.Type() == tvtVoid) return TJS_E_FAIL;
		tjs_uint64 time = (tjs_int64)tmp;
		if(time < 2) time = 2;

		// zoom1 / zoom2 は任意。既定値は元DLL より zoom1=100(%), zoom2=200(%)。
		tjs_int zoom1 = 100;  // 元画像の目標ズーム値 (FUN_10019190 の 0x64)
		tjs_int zoom2 = 200;  // 先画像の開始ズーム値 (FUN_10019190 の 0xc8)
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("zoom1"), &tmp)) && tmp.Type() != tvtVoid)
			zoom1 = (tjs_int)(tjs_int64)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("zoom2"), &tmp)) && tmp.Type() != tvtVoid)
			zoom2 = (tjs_int)(tjs_int64)tmp;

		*handler = new tTVPZoomFadeTransHandler(time, src1w, src1h, zoom1, zoom2);
		return TJS_S_OK;
	}

} static * ZoomFadeTransHandlerProvider;
//---------------------------------------------------------------------------
void RegisterZoomFadeTransHandlerProvider()
{
	ZoomFadeTransHandlerProvider = new tTVPZoomFadeTransHandlerProvider();
	TVPAddTransHandlerProvider(ZoomFadeTransHandlerProvider);
}
//---------------------------------------------------------------------------
void UnregisterZoomFadeTransHandlerProvider()
{
	TVPRemoveTransHandlerProvider(ZoomFadeTransHandlerProvider);
	ZoomFadeTransHandlerProvider->Release();
}
//---------------------------------------------------------------------------
