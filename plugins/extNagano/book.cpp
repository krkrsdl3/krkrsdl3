#include "tjsCommHead.h"
#include "cpu_types.h"
#include "transhandler.h"
#include "tjsErrorDefs.h"
#include "TVPMsg.h"
#include "TVPTrans.h"
#include "tjsVariant.h"
#include "tjsConfig.h"
#include <math.h>
#include <stdlib.h>
#include "book.h"
#include "common.h"

//---------------------------------------------------------------------------
// book トランジション
//   本のページをめくるように画像を切り替えるトランジション。(作成者: ヤマモト)
//
//   復元元 (extNagano.dll / Ghidra デコンパイル):
//     Provider::GetName          = FUN_10004600  (→ L"book")
//     Provider::StartTransition  = FUN_10005420  (time, dir 読み取り+dir=-1 乱数化)
//     Handler::StartProcess      = FUN_10004430  (二次イージングで Phase を計算)
//     Handler::EndProcess        = FUN_100044d0
//     Handler::Process           = FUN_10004580  (Phase により src1/src2 直結 or 描画へ分岐)
//     Handler(dir=0/RL) 描画本体  = FUN_100046f0  (tTVPBookTransHandlerRL::vftable +0x20)
//     Handler(dir=1/LR) 描画本体  = FUN_10004db0  (tTVPBookTransHandlerLR::vftable +0x20)
//     コンストラクタ             = FUN_100046a0 (dir=0), FUN_10004d60 (dir=1)
//
//   オリジナルは方向ごとに派生クラス (tTVPBookTransHandlerRL / LR) を持ち、
//   共通の Process から vtable の +0x20 スロット (方向別の描画関数) を呼んでいた。
//   ここでは 1 クラス + Dir メンバで表現する (AGENTS_REF の指示どおり)。
//
//   dir:
//     -1  ランダム (rand()&1 で 0/1 を選ぶ … オリジナルは rand()&0x80000001 を 0/1 に正規化)
//      0  左側のページを右へめくる (RL: 左に src1 が残り、右から src2 が現れる)
//      1  右側のページを左へめくる (LR: 右に src1 が残り、左から src2 が現れる)
//
//   ※ めくれた紙 (折り目) の陰影は、整数演算部分はデコンパイルに忠実。
//     折り目の座標 (境界) は x87 の FP 演算から復元した:
//       Q    = round( sqrt( (((Width/2 - Phase)<<16) / Width) / 2 ) )  … 折り目の暗さ/スケール
//       K    = 1.5 (定数 0x1002b690。折り目の中央。seg 間の陰影連続性より確定)
//     境界 (RL, 絶対座標):
//       b1 = Width-2*Phase-min(Phase,16), b2 = Width-2*Phase,
//       b3 = Width-1.5*Phase,             b4 = Width-Phase
//     境界 (LR) はこれを左右反転した位置だが、参照する画像内容は反転しない
//     (陰影の向きは方向によらず一定にするため、LR は単純ミラーではなく別関数)。
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// 画素演算ヘルパ (デコンパイルのビット演算そのまま)
//---------------------------------------------------------------------------
static inline tjs_int ClampI(tjs_int v, tjs_int lo, tjs_int hi)
{
	if(v < lo) v = lo;
	if(v > hi) v = hi;
	return v;
}
//---------------------------------------------------------------------------
static inline tjs_uint32 Darken(tjs_uint32 c, tjs_uint32 f)
{
	// 各チャンネルを (256-f)/256 倍して暗くする。アルファは保持。
	tjs_uint32 r, t;
	t = c & 0x0000ff; r  = ((t - ((t * f) >> 8)) & 0x0000ff);
	t = c & 0x00ff00; r |= ((t - ((t * f) >> 8)) & 0x00ff00);
	t = c & 0xff0000; r |= ((t - ((t * f) >> 8)) & 0xff0000);
	r |= (c & 0xff000000);
	return r;
}
//---------------------------------------------------------------------------
static inline tjs_uint32 Lighten(tjs_uint32 c, tjs_uint32 f)
{
	// 各チャンネルを f/256 の割合で白 (0xff) 方向へ寄せる。アルファは保持。
	tjs_uint32 r, t;
	t = c & 0x0000ff; r  = ((((0x0000ff - t) * f) >> 8) + t) & 0x0000ff;
	t = c & 0x00ff00; r |= ((((0x00ff00 - t) * f) >> 8) + t) & 0x00ff00;
	t = c & 0xff0000; r |= ((((0xff0000 - t) * f) >> 8) + t) & 0xff0000;
	r |= (c & 0xff000000);
	return r;
}
//---------------------------------------------------------------------------
static inline tjs_uint32 BlendAlpha(tjs_uint32 a, tjs_uint32 b, tjs_uint32 alpha)
{
	// a と b を alpha(0..255) で混合 (チャンネルごとに a + (b-a)*alpha/256)。
	// アルファ成分は出力しない (呼び出し側で付与する)。
	tjs_uint32 r, ta, tb;
	ta = a & 0x0000ff; tb = b & 0x0000ff; r  = (((tb - ta) * alpha >> 8) + ta) & 0x0000ff;
	ta = a & 0x00ff00; tb = b & 0x00ff00; r |= (((tb - ta) * alpha >> 8) + ta) & 0x00ff00;
	ta = a & 0xff0000; tb = b & 0xff0000; r |= (((tb - ta) * alpha >> 8) + ta) & 0xff0000;
	return r;
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
class tTVPBookTransHandler : public iTVPDivisibleTransHandler
{
	tjs_int RefCount; // 参照カウンタ

protected:
	tjs_uint64 StartTick; // トランジション開始 tick
	tjs_uint64 Time;      // 所要時間
	tjs_uint64 CurTime;   // 現在の経過時間 (Time でクランプ)
	tjs_int Width;        // 画像幅
	tjs_int Height;       // 画像高
	tjs_int Phase;        // めくり進行度 (0 .. Width/2)
	tjs_int Dir;          // 0 = RL (dir:0), 1 = LR (dir:1)
	tjs_int FrameCount;   // フレーム数 (元コードの fps ログ用)
	bool First;           // 最初の呼び出しか

	void PaintLineRL(tjs_uint32 *dp, const tjs_uint32 *src1, const tjs_uint32 *src2,
			tjs_int left, tjs_int right);
	void PaintLineLR(tjs_uint32 *dp, const tjs_uint32 *src1, const tjs_uint32 *src2,
			tjs_int left, tjs_int right);

public:
	tTVPBookTransHandler(tjs_uint64 time, tjs_int width, tjs_int height, tjs_int dir)
		: Time(time), CurTime(0), Width(width), Height(height),
		  Phase(0), Dir(dir), FrameCount(0), First(true)
	{
		RefCount = 1;
	}
	virtual ~tTVPBookTransHandler() {}

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
		*dest = src2; // 常に最終画像は src2 (FUN_10006e90: *dest = src2)
		return TJS_S_OK;
	}
};
//---------------------------------------------------------------------------
// FUN_10004430 相当
tjs_error tTVPBookTransHandler::StartProcess(tjs_uint64 tick)
{
	FrameCount++;
	if(First)
	{
		First = false;
		StartTick = tick;
		Phase = 0;
	}

	// 経過時間を Time でクランプ
	CurTime = tick - StartTick;
	if(CurTime > Time) CurTime = Time;

	// 二次イージング: Phase = (Width/2) * (CurTime/Time)^2 を Width/2 でクランプ
	//   オリジナルは (Width/2)*CurTime*CurTime を Time で 2 回除算していた
	tjs_uint64 half = (tjs_uint64)(Width / 2);
	tjs_uint64 v = half * CurTime;
	v = v * CurTime;
	v = v / Time;
	v = v / Time;
	Phase = (v < half) ? (tjs_int)v : (tjs_int)(Width / 2);

	return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
// FUN_100044d0 相当
tjs_error tTVPBookTransHandler::EndProcess()
{
	if(CurTime == Time) return TJS_S_FALSE; // トランジション終了
	return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
// FUN_10004580 相当
tjs_error tTVPBookTransHandler::Process(tTVPDivisibleData *data)
{
	if(Phase == 0)
	{
		// 完全に src1。Dest を Src1 に差し替えるだけ (コピー不要)
		data->Dest     = data->Src1;
		data->DestLeft = data->Src1Left;
		data->DestTop  = data->Src1Top;
		return TJS_S_OK;
	}
	if(Phase == Width / 2)
	{
		// 完全に src2
		data->Dest     = data->Src2;
		data->DestLeft = data->Src2Left;
		data->DestTop  = data->Src2Top;
		return TJS_S_OK;
	}

	// 折り目を描画する
	tjs_int left  = data->Left;
	tjs_int right = data->Left + data->Width;

	for(tjs_int n = 0; n < data->Height; n++)
	{
		tjs_uint32 *dest;
		const tjs_uint32 *src1;
		const tjs_uint32 *src2;
		if(TJS_FAILED(data->Dest->GetScanLineForWrite(data->DestTop + n, (void**)&dest)))
			return TJS_E_FAIL;
		if(TJS_FAILED(data->Src1->GetScanLine(data->Top + n, (const void**)&src1)))
			return TJS_E_FAIL;
		if(TJS_FAILED(data->Src2->GetScanLine(data->Top + n, (const void**)&src2)))
			return TJS_E_FAIL;

		// dp[c] (c は絶対列) に書き込めるよう基点をずらす
		tjs_uint32 *dp = dest + data->DestLeft - data->Left;

		if(Dir == 0)
			PaintLineRL(dp, src1, src2, left, right);
		else
			PaintLineLR(dp, src1, src2, left, right);
	}
	return TJS_S_OK;
}
//---------------------------------------------------------------------------
// FUN_100046f0 相当 (dir=0 / RL : 左に src1 が残り、右から src2 がめくれ出る)
void tTVPBookTransHandler::PaintLineRL(tjs_uint32 *dp,
		const tjs_uint32 *src1, const tjs_uint32 *src2, tjs_int left, tjs_int right)
{
	const tjs_int W = Width;
	const tjs_int P = Phase;
	const tjs_int MINP = (P < 16) ? P : 16;              // min(Phase,16)
	// A = (((W/2 - P)<<16) / W) / 2 → Q = round(sqrt(A))
	tjs_int A = ((((W / 2) - P) << 16) / W) / 2;
	if(A < 0) A = 0;
	const tjs_int Q  = (tjs_int)(sqrt((double)A) + 0.5); // 折り目の暗さ/スケール
	const tjs_int HG = Q / 2;                            // 折り目裏の src1 陰影量

	// 折り目の境界 (絶対座標, [0,W] でクランプ)
	const tjs_int b1 = ClampI(W - 2 * P - MINP, 0, W);   // src1 平坦 / 陰影帯
	const tjs_int b2 = ClampI(W - 2 * P,        0, W);   // 陰影帯 / 折り目
	const tjs_int b3 = ClampI(W - (3 * P) / 2,  0, W);   // 折り目 (発光←→陰影)  K=1.5
	const tjs_int b4 = ClampI(W - P,            0, W);   // 折り目 / src2 陰影

	for(tjs_int c = left; c < right; c++)
	{
		if(c < b1)
		{
			// seg1: 平坦な src1
			dp[c] = src1[c];
		}
		else if(c < b2)
		{
			// seg2: 折り目付け根の陰影帯 (src1 を暗くする, 0→Q/2)
			tjs_int f = (MINP > 0) ? ((((c - b2) + MINP) * Q) / MINP) / 2 : 0;
			if(f < 0) f = 0;
			dp[c] = Darken(src1[c], (tjs_uint32)f);
		}
		else if(c < b3)
		{
			// seg3: 折り目 (暗い src1 の上に、白方向へ発光させた src2 を α合成)
			tjs_int f = ((b3 - c) * Q * 2) / P;
			if(f < 0) f = 0;
			tjs_int sc = c + (2 * P - W);
			tjs_uint32 s2 = (sc >= 0 && sc < W) ? src2[sc] : 0;
			tjs_uint32 a = Darken(src1[c], (tjs_uint32)HG);
			tjs_uint32 b = Lighten(s2, (tjs_uint32)f);
			dp[c] = BlendAlpha(a, b, s2 >> 24) | (src1[c] & 0xff000000);
		}
		else if(c < b4)
		{
			// seg4: 折り目 (暗い src1 の上に、暗くした src2 を α合成)
			tjs_int f = (((c - b4) * 2 + P) * Q) / P;
			if(f < 0) f = 0;
			tjs_int sc = c + (2 * P - W);
			tjs_uint32 s2 = (sc >= 0 && sc < W) ? src2[sc] : 0;
			tjs_uint32 a = Darken(src1[c], (tjs_uint32)HG);
			tjs_uint32 b = Darken(s2, (tjs_uint32)f);
			dp[c] = BlendAlpha(a, b, s2 >> 24) | (src1[c] & 0xff000000);
		}
		else
		{
			// seg5: 現れた src2。折り目付近は暗く (Q→0)、右端で通常
			tjs_int f = ((W - c) * Q) / P;
			if(f < 0) f = 0;
			dp[c] = Darken(src2[c], (tjs_uint32)f);
		}
	}
}
//---------------------------------------------------------------------------
// FUN_10004db0 相当 (dir=1 / LR : 右に src1 が残り、左から src2 がめくれ出る)
void tTVPBookTransHandler::PaintLineLR(tjs_uint32 *dp,
		const tjs_uint32 *src1, const tjs_uint32 *src2, tjs_int left, tjs_int right)
{
	const tjs_int W = Width;
	const tjs_int P = Phase;
	const tjs_int MINP = (P < 16) ? P : 16;
	tjs_int A = ((((W / 2) - P) << 16) / W) / 2;
	if(A < 0) A = 0;
	const tjs_int Q  = (tjs_int)(sqrt((double)A) + 0.5);
	const tjs_int HG = Q / 2;

	// 折り目の境界 (RL を左右反転した位置)
	const tjs_int b1 = ClampI(P,             0, W);      // src2 陰影 / 折り目
	const tjs_int b2 = ClampI((3 * P) / 2,   0, W);      // 折り目 (陰影←→発光)
	const tjs_int b3 = ClampI(2 * P,         0, W);      // 折り目 / 陰影帯
	const tjs_int b4 = ClampI(2 * P + MINP,  0, W);      // 陰影帯 / src1 平坦

	for(tjs_int c = left; c < right; c++)
	{
		if(c < b1)
		{
			// segL1: 現れた src2。左端で暗く (Q→0)、折り目で通常
			tjs_int f = ((b1 - c) * Q) / P;
			if(f < 0) f = 0;
			dp[c] = Darken(src2[c], (tjs_uint32)f);
		}
		else if(c < b2)
		{
			// segL2: 折り目 (暗い src1 の上に、暗くした src2 を α合成)
			tjs_int f = ((b2 - c) * Q * 2) / P;
			if(f < 0) f = 0;
			tjs_int sc = c + (W - 2 * P);
			tjs_uint32 s2 = (sc >= 0 && sc < W) ? src2[sc] : 0;
			tjs_uint32 a = Darken(src1[c], (tjs_uint32)HG);
			tjs_uint32 b = Darken(s2, (tjs_uint32)f);
			dp[c] = BlendAlpha(a, b, s2 >> 24) | (src1[c] & 0xff000000);
		}
		else if(c < b3)
		{
			// segL3: 折り目 (暗い src1 の上に、白方向へ発光させた src2 を α合成)
			tjs_int f = ((P / 2 + (c - b3) * 2) * Q) / P;
			if(f < 0) f = 0;
			tjs_int sc = c + (W - 2 * P);
			tjs_uint32 s2 = (sc >= 0 && sc < W) ? src2[sc] : 0;
			tjs_uint32 a = Darken(src1[c], (tjs_uint32)HG);
			tjs_uint32 b = Lighten(s2, (tjs_uint32)f);
			dp[c] = BlendAlpha(a, b, s2 >> 24) | (src1[c] & 0xff000000);
		}
		else if(c < b4)
		{
			// segL4: 折り目付け根の陰影帯 (src1 を暗くする, Q/2→0)
			tjs_int f = (MINP > 0) ? (((b4 - c) * Q) / MINP) / 2 : 0;
			if(f < 0) f = 0;
			dp[c] = Darken(src1[c], (tjs_uint32)f);
		}
		else
		{
			// segL5: 平坦な src1
			dp[c] = src1[c];
		}
	}
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
class tTVPBookTransHandlerProvider : public iTVPTransHandlerProvider
{
	tjs_uint RefCount;
public:
	tTVPBookTransHandlerProvider() { RefCount = 1; }
	~tTVPBookTransHandlerProvider() {}

	tjs_error AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }

	// FUN_10004600
	tjs_error GetName(const tjs_char ** name)
	{
		if(name) *name = TJS_N("book");
		return TJS_S_OK;
	}

	// FUN_10005420
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
		if(type) *type = ttExchange;
		if(updatetype) *updatetype = tutDivisible;
		if(!handler) return TJS_E_FAIL;
		if(!options) return TJS_E_FAIL;
		if(src1w != src2w || src1h != src2h) return TJS_E_FAIL;

		// time (必須)
		tTJSVariant tmp;
		if(TJS_FAILED(options->GetValue(TJS_N("time"), &tmp))) return TJS_E_FAIL;
		if(tmp.Type() == tvtVoid) return TJS_E_FAIL;
		tjs_uint64 time = (tjs_int64)tmp;
		if(time < 2) time = 2;

		// dir (省略時は -1 = ランダム)
		tjs_int dir = -1;
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("dir"), &tmp)))
			if(tmp.Type() != tvtVoid) dir = (tjs_int)tmp;

		if(dir == -1)
			dir = rand() & 1; // オリジナル: rand()&0x80000001 を 0/1 に正規化 (= rand()&1)
		if(dir != 1) dir = 0; // 0/1 以外は 0 (RL) 扱い

		*handler = new tTVPBookTransHandler(time, src1w, src1h, dir);
		return TJS_S_OK;
	}

} static * BookTransHandlerProvider;
//---------------------------------------------------------------------------
void RegisterBookTransHandlerProvider()
{
	BookTransHandlerProvider = new tTVPBookTransHandlerProvider();
	TVPAddTransHandlerProvider(BookTransHandlerProvider);
}
//---------------------------------------------------------------------------
void UnregisterBookTransHandlerProvider()
{
	TVPRemoveTransHandlerProvider(BookTransHandlerProvider);
	BookTransHandlerProvider->Release();
}
//---------------------------------------------------------------------------
