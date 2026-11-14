#include "tjsCommHead.h"
#include "cpu_types.h"
#include "transhandler.h"
#include "tjsErrorDefs.h"
#include "TVPMsg.h"
#include "TVPTrans.h"
#include "tjsVariant.h"
#include "tjsConfig.h"
#include "blurfade.h"
#include "common.h"
#include <math.h>

//---------------------------------------------------------------------------
// blurfade トランジション  (ぼかしながらクロスフェード)
//   元画像(Src1)をぼかしつつ透明にしていき、先画像(Src2)をぼかした状態から
//   ピントを戻しつつ不透明にしていく、ぼかし付きクロスフェード。
//   ぼかし量と合成比は exponent で時間カーブを付けられる。
//   (extNagano.dll: tTVPBlurFadeTransHandler の復元)
//
//   復元元 (Ghidra FUN_):
//     Provider::GetName          = FUN_10003d00  (-> L"blurfade")
//     Provider::StartTransition  = FUN_10004160  (time/exponent/blur*/type/prerender 読取+既定値)
//     Handler ctor               = FUN_10003ef0  (メンバ初期化・ぼかし量クランプ・prerender確保)
//     Handler::StartProcess      = FUN_100035d0  (経過->合成比 alpha と各源の現ぼかし量を算出)
//     Handler::EndProcess        = FUN_10003730  (alpha==0xff で終了)
//     Handler::Process           = FUN_10003800  (ぼかし生成 + 成分別線形補間で合成)
//     Handler::MakeFinalImage    = FUN_10006e90  (*dest = src2)
//     ぼかし生成 dispatch         = FUN_100037f0  (type で box/bilinear を切替)
//       平均値(box)ぼかし          = FUN_10002a80
//       バイリニアぼかし           = FUN_100031a0
//     ぼかしバッファ確保           = FUN_10003c40 / FUN_10003da0
//     AddRef=FUN_10007730, Release=FUN_10016840, SetOption=FUN_10016870
//
//   [簡略化・確信度の低い箇所]
//   1. exponent による時間カーブは、元の x87 FP 列 (FUN_10019d10=pow / FUN_10019af0=round)
//      を完全に復元できていない。ここでは仕様書の記述
//        「1 より大きいと前半が遅く後半が速い」
//      に合致する alpha = 255 * pow(t, exponent) を採用した。t = 経過/所要時間 [0,1]。
//   2. 各源のぼかし量の時間変化は、Src1 は f=pow(t,exponent) で増加、
//      Src2 は (1-f) で減少、と解釈した(仕様の「元をぼかしつつ / 先をぼかした状態から戻す」
//      に一致)。デコンパイルはレジスタ欠落で厳密には追えず、この向きは推定。
//   3. prerender(1/2: 開始前一括生成でキャッシュ)は、忠実な事前生成バッファ群
//      (FUN_10003da0 の可変個バッファ + FUN_10003550 の最近傍選択)を実装せず、
//      prerender=0 相当(毎フレーム生成)に統一した。見た目はほぼ同等で、
//      開始時の生成停止や高速化が無いだけ。type/prerender の値は読取・保持する。
//   4. box ぼかしは元の 1 パス走査ではなく水平→垂直の分離 box blur で近似
//      (平均・端クランプは等価。ピクセル完全一致はしない)。
//   5. Src の完全透明画素に対する元DLLの縮退合成(alpha 特殊経路)は行わず、
//      全画素を成分ごと線形補間 Blend() で合成する。
//   6. ぼかし幅クランプは仕様書通り x<幅/2, y<高さ/2 とした
//      (デコンパイルは y も 幅/2 で判定しており、これは原作のミスと思われる)。
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
class tTVPBlurFadeTransHandler : public iTVPDivisibleTransHandler
{
	tjs_int RefCount;

protected:
	tjs_uint64 StartTick;   // 開始 tick                    (+0x08)
	tjs_uint64 Time;        // 所要時間                     (+0x10)
	tjs_int Width;          // 画像幅                       (+0x20)
	tjs_int Height;         // 画像高                       (+0x24)
	double Exponent;        // 時間カーブ指数 (既定 1)      (+0x50)

	tjs_int Blur1x, Blur1y; // 元画像(Src1)ぼかし幅設定     (+0x28,+0x2c)
	tjs_int Blur2x, Blur2y; // 先画像(Src2)ぼかし幅設定     (+0x30,+0x34)
	tjs_int Type;           // 0:平均(box) / 1:バイリニア   (type)
	tjs_int PreRender;      // 0/1/2 (本実装では 0 相当で動作)

	tjs_int Alpha;          // 合成比 0..255 (Src1->Src2)   (+0x4c)
	tjs_int CurBlur1x, CurBlur1y; // 現フレームの元ぼかし量 (+0x3c,+0x40)
	tjs_int CurBlur2x, CurBlur2y; // 現フレームの先ぼかし量 (+0x44,+0x48)

	bool First;             // 最初の呼び出しか             (+0x58 状態相当)
	tjs_int FrameCount;     // フレーム数 (fps ログ用)      (+0x7c)

	// ぼかし結果バッファ(全画像 W*H)。遅延確保。
	tjs_uint32 *Buffer1;    // Src1 のぼかし画像
	tjs_uint32 *Buffer2;    // Src2 のぼかし画像
	tjs_uint32 *Scratch;    // 分離 box blur 用の中間バッファ
	bool NeedGen1;          // 今フレーム Src1 のぼかしを再生成すべきか
	bool NeedGen2;          // 今フレーム Src2 のぼかしを再生成すべきか

	void EnsureBuffers();
	void GenerateBlur(iTVPScanLineProvider *src, tjs_uint32 *dst, tjs_int rx, tjs_int ry);
	void BoxBlur(iTVPScanLineProvider *src, tjs_uint32 *dst, tjs_int rx, tjs_int ry);
	void BilinearBlur(iTVPScanLineProvider *src, tjs_uint32 *dst, tjs_int rx, tjs_int ry);

public:
	tTVPBlurFadeTransHandler(tjs_uint64 time, tjs_int width, tjs_int height,
			double exponent, tjs_int blur1x, tjs_int blur1y,
			tjs_int blur2x, tjs_int blur2y, tjs_int type, tjs_int prerender)
		: StartTick(0), Time(time), Width(width), Height(height), Exponent(exponent),
		  Blur1x(blur1x), Blur1y(blur1y), Blur2x(blur2x), Blur2y(blur2y),
		  Type(type), PreRender(prerender),
		  Alpha(0), CurBlur1x(0), CurBlur1y(0), CurBlur2x(0), CurBlur2y(0),
		  First(true), FrameCount(0),
		  Buffer1(0), Buffer2(0), Scratch(0), NeedGen1(false), NeedGen2(false)
	{
		RefCount = 1;

		// ぼかし幅クランプ (FUN_10003ef0)。負や過大は 0 に落とす。
		if(Blur1x < 0 || Blur1x >= Width  / 2) Blur1x = 0;
		if(Blur1y < 0 || Blur1y >= Height / 2) Blur1y = 0;
		if(Blur2x < 0 || Blur2x >= Width  / 2) Blur2x = 0;
		if(Blur2y < 0 || Blur2y >= Height / 2) Blur2y = 0;

		if(Exponent <= 0.0) Exponent = 1.0; // 0以下は無効なので既定へ
	}
	virtual ~tTVPBlurFadeTransHandler()
	{
		if(Buffer1) delete [] Buffer1;
		if(Buffer2) delete [] Buffer2;
		if(Scratch) delete [] Scratch;
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
void tTVPBlurFadeTransHandler::EnsureBuffers()
{
	tjs_int size = Width * Height;
	if(!Buffer1) Buffer1 = new tjs_uint32[size];
	if(!Buffer2) Buffer2 = new tjs_uint32[size];
	if(!Scratch) Scratch = new tjs_uint32[size];
}
//---------------------------------------------------------------------------
// 分離 box blur (平均値ぼかし, type==0)。端は窓を縮めて実効画素数で平均する。
//   FUN_10002a80 の平均処理の等価版 (走査順は簡略化)。
//---------------------------------------------------------------------------
void tTVPBlurFadeTransHandler::BoxBlur(
		iTVPScanLineProvider *src, tjs_uint32 *dst, tjs_int rx, tjs_int ry)
{
	const tjs_int W = Width, H = Height;

	// --- 水平方向 (src -> Scratch) ---
	for(tjs_int y = 0; y < H; y++)
	{
		const tjs_uint32 *in;
		if(TJS_FAILED(src->GetScanLine(y, (const void**)&in))) return;
		tjs_uint32 *out = Scratch + y * W;

		if(rx <= 0)
		{
			for(tjs_int x = 0; x < W; x++) out[x] = in[x];
			continue;
		}

		// 移動窓の初期化 [0, min(W-1, rx)]
		tjs_int lo = 0, hi = rx; if(hi > W - 1) hi = W - 1;
		tjs_int sa = 0, sr = 0, sg = 0, sb = 0;
		for(tjs_int i = lo; i <= hi; i++)
		{
			tjs_uint32 p = in[i];
			sb += p & 0xff; sg += (p >> 8) & 0xff; sr += (p >> 16) & 0xff; sa += (p >> 24) & 0xff;
		}
		tjs_int cnt = hi - lo + 1;

		for(tjs_int x = 0; x < W; x++)
		{
			out[x] = ((tjs_uint32)(sa / cnt) << 24) | ((tjs_uint32)(sr / cnt) << 16) |
			         ((tjs_uint32)(sg / cnt) << 8)  |  (tjs_uint32)(sb / cnt);

			// 窓を x+1 の [max(0,x+1-rx), min(W-1,x+1+rx)] へ進める
			tjs_int nlo = x + 1 - rx; if(nlo < 0) nlo = 0;
			tjs_int nhi = x + 1 + rx; if(nhi > W - 1) nhi = W - 1;
			while(hi < nhi) { hi++; tjs_uint32 p = in[hi];
				sb += p & 0xff; sg += (p >> 8) & 0xff; sr += (p >> 16) & 0xff; sa += (p >> 24) & 0xff; }
			while(lo < nlo) { tjs_uint32 p = in[lo];
				sb -= p & 0xff; sg -= (p >> 8) & 0xff; sr -= (p >> 16) & 0xff; sa -= (p >> 24) & 0xff; lo++; }
			cnt = hi - lo + 1;
		}
	}

	// --- 垂直方向 (Scratch -> dst) ---
	if(ry <= 0)
	{
		for(tjs_int i = 0; i < W * H; i++) dst[i] = Scratch[i];
		return;
	}

	// 列ごとの走行和を全列同時に持って下方向へスライドする。
	tjs_int *sumA = new tjs_int[W];
	tjs_int *sumR = new tjs_int[W];
	tjs_int *sumG = new tjs_int[W];
	tjs_int *sumB = new tjs_int[W];
	for(tjs_int x = 0; x < W; x++) { sumA[x] = sumR[x] = sumG[x] = sumB[x] = 0; }

	tjs_int lo = 0, hi = ry; if(hi > H - 1) hi = H - 1;
	for(tjs_int y = lo; y <= hi; y++)
	{
		const tjs_uint32 *row = Scratch + y * W;
		for(tjs_int x = 0; x < W; x++)
		{
			tjs_uint32 p = row[x];
			sumB[x] += p & 0xff; sumG[x] += (p >> 8) & 0xff;
			sumR[x] += (p >> 16) & 0xff; sumA[x] += (p >> 24) & 0xff;
		}
	}
	tjs_int cnt = hi - lo + 1;

	for(tjs_int y = 0; y < H; y++)
	{
		tjs_uint32 *out = dst + y * W;
		for(tjs_int x = 0; x < W; x++)
		{
			out[x] = ((tjs_uint32)(sumA[x] / cnt) << 24) | ((tjs_uint32)(sumR[x] / cnt) << 16) |
			         ((tjs_uint32)(sumG[x] / cnt) << 8)  |  (tjs_uint32)(sumB[x] / cnt);
		}

		tjs_int nlo = y + 1 - ry; if(nlo < 0) nlo = 0;
		tjs_int nhi = y + 1 + ry; if(nhi > H - 1) nhi = H - 1;
		while(hi < nhi) { hi++; const tjs_uint32 *row = Scratch + hi * W;
			for(tjs_int x = 0; x < W; x++) { tjs_uint32 p = row[x];
				sumB[x] += p & 0xff; sumG[x] += (p >> 8) & 0xff;
				sumR[x] += (p >> 16) & 0xff; sumA[x] += (p >> 24) & 0xff; } }
		while(lo < nlo) { const tjs_uint32 *row = Scratch + lo * W;
			for(tjs_int x = 0; x < W; x++) { tjs_uint32 p = row[x];
				sumB[x] -= p & 0xff; sumG[x] -= (p >> 8) & 0xff;
				sumR[x] -= (p >> 16) & 0xff; sumA[x] -= (p >> 24) & 0xff; } lo++; }
		cnt = hi - lo + 1;
	}

	delete [] sumA; delete [] sumR; delete [] sumG; delete [] sumB;
}
//---------------------------------------------------------------------------
// バイリニアぼかし (type==1)。stride = 2*r+1 で間引きサンプルし線形補間する。
//   FUN_100031a0 の「粗いが高速」なぼかしの簡略版。
//---------------------------------------------------------------------------
void tTVPBlurFadeTransHandler::BilinearBlur(
		iTVPScanLineProvider *src, tjs_uint32 *dst, tjs_int rx, tjs_int ry)
{
	const tjs_int W = Width, H = Height;
	const tjs_int sx = rx * 2 + 1;
	const tjs_int sy = ry * 2 + 1;

	// --- 水平方向 (src -> Scratch) ---
	for(tjs_int y = 0; y < H; y++)
	{
		const tjs_uint32 *in;
		if(TJS_FAILED(src->GetScanLine(y, (const void**)&in))) return;
		tjs_uint32 *out = Scratch + y * W;

		if(rx <= 0) { for(tjs_int x = 0; x < W; x++) out[x] = in[x]; continue; }

		for(tjs_int x = 0; x < W; x++)
		{
			tjs_int x0 = (x / sx) * sx;
			tjs_int x1 = x0 + sx; if(x1 > W - 1) x1 = W - 1;
			tjs_int frac = (x1 > x0) ? ((x - x0) * 255) / (x1 - x0) : 0;
			out[x] = Blend(in[x0], in[x1], frac);
		}
	}

	// --- 垂直方向 (Scratch -> dst) ---
	if(ry <= 0) { for(tjs_int i = 0; i < W * H; i++) dst[i] = Scratch[i]; return; }

	for(tjs_int y = 0; y < H; y++)
	{
		tjs_int y0 = (y / sy) * sy;
		tjs_int y1 = y0 + sy; if(y1 > H - 1) y1 = H - 1;
		tjs_int frac = (y1 > y0) ? ((y - y0) * 255) / (y1 - y0) : 0;
		const tjs_uint32 *r0 = Scratch + y0 * W;
		const tjs_uint32 *r1 = Scratch + y1 * W;
		tjs_uint32 *out = dst + y * W;
		for(tjs_int x = 0; x < W; x++) out[x] = Blend(r0[x], r1[x], frac);
	}
}
//---------------------------------------------------------------------------
void tTVPBlurFadeTransHandler::GenerateBlur(
		iTVPScanLineProvider *src, tjs_uint32 *dst, tjs_int rx, tjs_int ry)
{
	// FUN_100037f0: type で box / bilinear を切替
	if(Type == 1) BilinearBlur(src, dst, rx, ry);
	else          BoxBlur(src, dst, rx, ry);
}
//---------------------------------------------------------------------------
tjs_error tTVPBlurFadeTransHandler::StartProcess(tjs_uint64 tick)
{
	// FUN_100035d0
	FrameCount++;
	if(First)
	{
		First = false;
		StartTick = tick;
	}

	tjs_uint64 elapsed = tick - StartTick;
	if(elapsed > Time) elapsed = Time;

	// 時間カーブ f = pow(t, exponent) ( t = 経過/所要, 0..1 )。
	// alpha は Src1->Src2 の合成比。f が上がるほど Src2 が濃くなる。
	double t = (double)elapsed / (double)Time;
	if(t < 0.0) t = 0.0; else if(t > 1.0) t = 1.0;
	double f = pow(t, Exponent);

	tjs_int alpha = (tjs_int)(f * 255.0 + 0.5);
	if(alpha < 0) alpha = 0; else if(alpha > 255) alpha = 255;
	Alpha = alpha;

	// 各源の現ぼかし量:
	//   Src1(元)  … f      で増加 (ぼけていく)
	//   Src2(先)  … (1-f)  で減少 (ピントが戻る)
	double g = 1.0 - f;
	CurBlur1x = (tjs_int)(Blur1x * f + 0.5);
	CurBlur1y = (tjs_int)(Blur1y * f + 0.5);
	CurBlur2x = (tjs_int)(Blur2x * g + 0.5);
	CurBlur2y = (tjs_int)(Blur2y * g + 0.5);

	// prerender=0 相当: ぼかしを使う源は毎フレーム生成する。
	// (Process の最初の呼び出しで生成し、同フレーム内の後続呼び出しは再利用)
	NeedGen1 = (alpha > 0)   && (CurBlur1x > 0 || CurBlur1y > 0);
	NeedGen2 = (alpha < 255) && (CurBlur2x > 0 || CurBlur2y > 0);

	return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
tjs_error tTVPBlurFadeTransHandler::EndProcess()
{
	// FUN_10003730: 合成比が最大 (0xff) になったら終了
	if(Alpha == 0xff) return TJS_S_FALSE;
	return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
tjs_error tTVPBlurFadeTransHandler::Process(tTVPDivisibleData *data)
{
	// FUN_10003800
	tjs_int left  = data->Left;
	tjs_int right = data->Left + data->Width;

	// 今フレームで必要なぼかし画像を(未生成なら)生成する。
	if(NeedGen1) { EnsureBuffers(); GenerateBlur(data->Src1, Buffer1, CurBlur1x, CurBlur1y); NeedGen1 = false; }
	if(NeedGen2) { EnsureBuffers(); GenerateBlur(data->Src2, Buffer2, CurBlur2x, CurBlur2y); NeedGen2 = false; }

	const bool useBlur1 = (CurBlur1x > 0 || CurBlur1y > 0) && Buffer1;
	const bool useBlur2 = (CurBlur2x > 0 || CurBlur2y > 0) && Buffer2;
	const tjs_int alpha = Alpha;

	for(tjs_int n = 0; n < data->Height; n++)
	{
		tjs_int y = data->Top + n;

		tjs_uint32 *dest;
		if(TJS_FAILED(data->Dest->GetScanLineForWrite(data->DestTop + n, (void**)&dest)))
			return TJS_E_FAIL;

		// dest の絶対列 c は dp[c] に書き込む (scanline.cpp と同じ基点)
		tjs_uint32 *dp = dest + data->DestLeft - data->Left;

		// alpha==0/255 は片側のみ表示。ぼかし量も 0 なので原画像をそのまま出す。
		if(alpha == 0)
		{
			const tjs_uint32 *src1;
			if(TJS_FAILED(data->Src1->GetScanLine(y, (const void**)&src1))) return TJS_E_FAIL;
			for(tjs_int c = left; c < right; c++) dp[c] = src1[c];
			continue;
		}
		if(alpha == 255)
		{
			const tjs_uint32 *src2;
			if(TJS_FAILED(data->Src2->GetScanLine(y, (const void**)&src2))) return TJS_E_FAIL;
			for(tjs_int c = left; c < right; c++) dp[c] = src2[c];
			continue;
		}

		// 各源のこのラインの先頭 (ぼかし済みバッファ or 原画像プロバイダ)
		const tjs_uint32 *s1;
		const tjs_uint32 *s2;
		if(useBlur1) s1 = Buffer1 + y * Width;
		else { if(TJS_FAILED(data->Src1->GetScanLine(y, (const void**)&s1))) return TJS_E_FAIL; }
		if(useBlur2) s2 = Buffer2 + y * Width;
		else { if(TJS_FAILED(data->Src2->GetScanLine(y, (const void**)&s2))) return TJS_E_FAIL; }

		// 成分ごとに src1 -> src2 を alpha で線形補間 (アルファ含む)
		for(tjs_int c = left; c < right; c++)
			dp[c] = Blend(s1[c], s2[c], alpha);
	}
	return TJS_S_OK;
}
//---------------------------------------------------------------------------
class tTVPBlurFadeTransHandlerProvider : public iTVPTransHandlerProvider
{
	tjs_uint RefCount;
public:
	tTVPBlurFadeTransHandlerProvider() { RefCount = 1; }
	~tTVPBlurFadeTransHandlerProvider() {}

	tjs_error AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }

	tjs_error GetName(const tjs_char ** name)
	{
		if(name) *name = TJS_N("blurfade");
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
		// FUN_10004160
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

		// blur1 -> blur1x/blur1y 両方の既定、blur2 -> blur2x/blur2y 両方の既定。
		// blur1x/blur1y (blur2x/blur2y) が個別指定されればそちらを優先。
		tjs_int blur1x = 0, blur1y = 0, blur2x = 0, blur2y = 0;
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("blur1"), &tmp)) && tmp.Type() != tvtVoid)
			{ blur1x = blur1y = (tjs_int)(tjs_int64)tmp; }
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("blur2"), &tmp)) && tmp.Type() != tvtVoid)
			{ blur2x = blur2y = (tjs_int)(tjs_int64)tmp; }
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("blur1x"), &tmp)) && tmp.Type() != tvtVoid)
			blur1x = (tjs_int)(tjs_int64)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("blur1y"), &tmp)) && tmp.Type() != tvtVoid)
			blur1y = (tjs_int)(tjs_int64)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("blur2x"), &tmp)) && tmp.Type() != tvtVoid)
			blur2x = (tjs_int)(tjs_int64)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("blur2y"), &tmp)) && tmp.Type() != tvtVoid)
			blur2y = (tjs_int)(tjs_int64)tmp;

		// type (0:平均 / 1:バイリニア, 既定 0)
		tjs_int type_opt = 0;
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("type"), &tmp)) && tmp.Type() != tvtVoid)
			type_opt = (tjs_int)(tjs_int64)tmp;

		// prerender (0/1/2, 既定 0)。本実装では読取・保持のみ (0 相当で動作)。
		tjs_int prerender = 0;
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("prerender"), &tmp)) && tmp.Type() != tvtVoid)
			prerender = (tjs_int)(tjs_int64)tmp;

		// exponent (時間カーブ, 既定 1.0)
		double exponent = 1.0;
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("exponent"), &tmp)) && tmp.Type() != tvtVoid)
			exponent = (double)(tTVReal)tmp;

		*handler = new tTVPBlurFadeTransHandler(time, src1w, src1h,
				exponent, blur1x, blur1y, blur2x, blur2y, type_opt, prerender);
		return TJS_S_OK;
	}

} static * BlurFadeTransHandlerProvider;
//---------------------------------------------------------------------------
void RegisterBlurFadeTransHandlerProvider()
{
	BlurFadeTransHandlerProvider = new tTVPBlurFadeTransHandlerProvider();
	TVPAddTransHandlerProvider(BlurFadeTransHandlerProvider);
}
//---------------------------------------------------------------------------
void UnregisterBlurFadeTransHandlerProvider()
{
	TVPRemoveTransHandlerProvider(BlurFadeTransHandlerProvider);
	BlurFadeTransHandlerProvider->Release();
}
//---------------------------------------------------------------------------
