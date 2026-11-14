#include "tjsCommHead.h"
#include "cpu_types.h"
#include "transhandler.h"
#include "tjsErrorDefs.h"
#include "TVPMsg.h"
#include "TVPTrans.h"
#include "tjsVariant.h"
#include "tjsConfig.h"
#include "morphing.h"
#include "common.h"
#include <vector>
#include <math.h>

//---------------------------------------------------------------------------
// morphing トランジション  (作者: ヤマモト)
//   三角形パッチを変形させながら src1 -> src2 をクロスフェードする。
//
//   before[] で「変形前の三角形群」を、after[] で「変形後の三角形群」を
//   数値配列で指定する。三角形 1 つにつき 6 個 (ax1,ay1,ax2,ay2,ax3,ay3)、
//   n 個で 6n 個。before は src1(自レイヤ画像)上のサンプリング座標、
//   after は src2(相手画像)上のサンプリング座標を表す。
//   画面に表示される三角形(dest 形状)は before と after の線形補間:
//       current = before + (after - before) * elapsed / Time
//   各 dest 三角形を、src1 は before 三角形から、src2 は after 三角形から
//   アフィンワープ(=重心座標補間)してサンプリングし、混合率(alpha)で
//   クロスフェードして書き込む。三角形が敷き詰められていない隙間は
//   src1/src2 のストレートなクロスフェードで埋める。
//
//   ※仕様書の "befor" は誤記で、DLL の実キーは "before"。
//
// 復元元 (extNagano.dll / Ghidra):
//   Provider::GetName        = FUN_10014ff0  (-> L"morphing")
//   Provider::StartTransition= FUN_100152d0  (time/before/after 配列の読取り)
//   Provider ctor(handler)   = FUN_10015090 / FUN_10015166 (パッチ配列構築)
//   Handler::StartProcess    = FUN_10014d00  (alpha 算出 + 中間形状の補間)
//   Handler::EndProcess      = FUN_10007f90
//   Handler::Process         = FUN_10008010  (三角形ラスタライズ + クロスフェード)
//     - FUN_10007bc0 : dest 三角形 -> src 三角形 のアフィン係数算出
//     - FUN_10007ae0 / FUN_10007b60 : アフィンの評価/スパン方向の増分
//     - FUN_10007db0 : スキャンラインでの三角形左右端 x の算出
//     - FUN_10018af0 : 2 色の alpha 混合
//   AddRef=FUN_10007730, Release=FUN_10016840, SetOption=FUN_10016870,
//   MakeFinalImage=FUN_10006e90
//
//   [簡略化] オリジナルはパッチ毎に固定小数点のエッジ DDA と、符号別に
//   展開された多数の内側ループ(アフィンテクスチャ走査)で高速化していたが、
//   x64 でも安全に動くよう、ここでは重心座標(バリセントリック)による
//   自前スキャンライン・ラスタライザで等価に再実装している。ソース座標は
//   重心補間 = アフィンワープと数学的に等価。
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// 三角形パッチ 1 個分 (before=src1 座標, after=src2 座標)。頂点順は入力どおり。
//---------------------------------------------------------------------------
struct tTVPMorphPatch
{
	tjs_int BeforeX[3], BeforeY[3]; // 変形前(src1 サンプリング座標)
	tjs_int AfterX[3],  AfterY[3];  // 変形後(src2 サンプリング座標)
};
//---------------------------------------------------------------------------
static inline tjs_int Min3(tjs_int a, tjs_int b, tjs_int c)
{
	tjs_int m = a; if(b < m) m = b; if(c < m) m = c; return m;
}
static inline tjs_int Max3(tjs_int a, tjs_int b, tjs_int c)
{
	tjs_int m = a; if(b > m) m = b; if(c > m) m = c; return m;
}
//---------------------------------------------------------------------------
class tTVPMorphingTransHandler : public iTVPDivisibleTransHandler
{
	tjs_int RefCount;

protected:
	tjs_uint64 StartTick; // 開始 tick
	tjs_uint64 Time;      // 所要時間
	tjs_int Width;        // 画像幅
	tjs_int Height;       // 画像高
	tjs_int Alpha;        // 現在の混合率 0..255 (0=src1, 255=src2) (元 this+0x28)
	tjs_int FrameCount;   // フレーム数 (fps ログ用)
	bool First;           // 最初の呼び出しか

	tjs_int NumTri;               // 三角形パッチ数
	tTVPMorphPatch *Patches;      // before/after 形状 (元 this+0x30)
	tjs_int *CurX;                // 中間(dest)形状 x  [NumTri*3]
	tjs_int *CurY;                // 中間(dest)形状 y  [NumTri*3]

public:
	tTVPMorphingTransHandler(tjs_uint64 time, tjs_int width, tjs_int height,
			tTVPMorphPatch *patches, tjs_int numtri)
		: Time(time), Width(width), Height(height), Alpha(0), FrameCount(0),
		  First(true), NumTri(numtri), Patches(patches)
	{
		RefCount = 1;
		CurX = new tjs_int[NumTri > 0 ? NumTri * 3 : 1];
		CurY = new tjs_int[NumTri > 0 ? NumTri * 3 : 1];
	}
	virtual ~tTVPMorphingTransHandler()
	{
		delete [] Patches;
		delete [] CurX;
		delete [] CurY;
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

private:
	tjs_uint32 SamplePixel(iTVPScanLineProvider *src, tjs_int u, tjs_int v);
	void RasterizePatch(tTVPDivisibleData *data, tjs_int idx);
};
//---------------------------------------------------------------------------
tjs_error tTVPMorphingTransHandler::StartProcess(tjs_uint64 tick)
{
	FrameCount++;
	if(First)
	{
		First = false;
		StartTick = tick;
	}
	tjs_uint64 elapsed = tick - StartTick;
	if(elapsed >= Time) elapsed = Time;

	// 混合率 alpha = elapsed * 255 / Time  (0..255)
	Alpha = (tjs_int)(elapsed * 255 / Time);
	if(Alpha > 255) Alpha = 255;

	// 中間(dest)形状 = before + (after-before)*elapsed/Time  (元 FUN_10014d00)
	for(tjs_int i = 0; i < NumTri; i++)
	{
		const tTVPMorphPatch &p = Patches[i];
		for(tjs_int v = 0; v < 3; v++)
		{
			tjs_int k = i * 3 + v;
			CurX[k] = p.BeforeX[v] + (tjs_int)((tjs_int64)(p.AfterX[v] - p.BeforeX[v]) * (tjs_int64)elapsed / (tjs_int64)Time);
			CurY[k] = p.BeforeY[v] + (tjs_int)((tjs_int64)(p.AfterY[v] - p.BeforeY[v]) * (tjs_int64)elapsed / (tjs_int64)Time);
		}
	}
	return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
tjs_error tTVPMorphingTransHandler::EndProcess()
{
	if(Alpha == 255) return TJS_S_FALSE; // 終了 (元 FUN_10007f90)
	return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
tjs_uint32 tTVPMorphingTransHandler::SamplePixel(iTVPScanLineProvider *src, tjs_int u, tjs_int v)
{
	// 画像範囲へクランプしてサンプリング (敷き詰め漏れ/丸め誤差対策の保険)
	if(u < 0) u = 0; else if(u >= Width)  u = Width  - 1;
	if(v < 0) v = 0; else if(v >= Height) v = Height - 1;
	const tjs_uint32 *line;
	if(TJS_FAILED(src->GetScanLine(v, (const void**)&line))) return 0;
	return line[u];
}
//---------------------------------------------------------------------------
void tTVPMorphingTransHandler::RasterizePatch(tTVPDivisibleData *data, tjs_int idx)
{
	const tTVPMorphPatch &p = Patches[idx];

	// dest(画面)三角形の頂点
	tjs_int dx0 = CurX[idx*3+0], dy0 = CurY[idx*3+0];
	tjs_int dx1 = CurX[idx*3+1], dy1 = CurY[idx*3+1];
	tjs_int dx2 = CurX[idx*3+2], dy2 = CurY[idx*3+2];

	// 面積の 2 倍 (符号は巻き方向)。0 は退化三角形なのでスキップ。
	tjs_int den = (dy1 - dy2) * (dx0 - dx2) + (dx2 - dx1) * (dy0 - dy2);
	if(den == 0) return;

	// このバンド [Left,Left+Width) x [Top,Top+Height) との交差範囲
	tjs_int bandL = data->Left,  bandR = data->Left + data->Width;   // [bandL, bandR)
	tjs_int bandT = data->Top,   bandB = data->Top  + data->Height;  // [bandT, bandB)

	tjs_int minx = Min3(dx0, dx1, dx2); if(minx < bandL) minx = bandL;
	tjs_int maxx = Max3(dx0, dx1, dx2); if(maxx > bandR - 1) maxx = bandR - 1;
	tjs_int miny = Min3(dy0, dy1, dy2); if(miny < bandT) miny = bandT;
	tjs_int maxy = Max3(dy0, dy1, dy2); if(maxy > bandB - 1) maxy = bandB - 1;
	if(minx > maxx || miny > maxy) return;

	double invden = 1.0 / (double)den;

	for(tjs_int py = miny; py <= maxy; py++)
	{
		tjs_int n = py - data->Top;

		tjs_uint32 *dest;
		if(TJS_FAILED(data->Dest->GetScanLineForWrite(data->DestTop + n, (void**)&dest)))
			return;
		// 絶対列 c は dp[c] に書き込む (scanline.cpp と同じ流儀)
		tjs_uint32 *dp = dest + data->DestLeft - data->Left;

		for(tjs_int px = minx; px <= maxx; px++)
		{
			// dest 三角形における重心座標 (整数)
			tjs_int w0 = (dy1 - dy2) * (px - dx2) + (dx2 - dx1) * (py - dy2);
			tjs_int w1 = (dy2 - dy0) * (px - dx2) + (dx0 - dx2) * (py - dy2);
			tjs_int w2 = den - w0 - w1;

			// den の符号にそろえて内外判定 (境界含む)
			if(den > 0) { if(w0 < 0 || w1 < 0 || w2 < 0) continue; }
			else        { if(w0 > 0 || w1 > 0 || w2 > 0) continue; }

			double fw0 = (double)w0 * invden;
			double fw1 = (double)w1 * invden;
			double fw2 = (double)w2 * invden;

			// src1 は before 三角形、src2 は after 三角形からサンプリング
			tjs_int u1 = (tjs_int)floor(fw0 * p.BeforeX[0] + fw1 * p.BeforeX[1] + fw2 * p.BeforeX[2] + 0.5);
			tjs_int v1 = (tjs_int)floor(fw0 * p.BeforeY[0] + fw1 * p.BeforeY[1] + fw2 * p.BeforeY[2] + 0.5);
			tjs_int u2 = (tjs_int)floor(fw0 * p.AfterX[0]  + fw1 * p.AfterX[1]  + fw2 * p.AfterX[2]  + 0.5);
			tjs_int v2 = (tjs_int)floor(fw0 * p.AfterY[0]  + fw1 * p.AfterY[1]  + fw2 * p.AfterY[2]  + 0.5);

			tjs_uint32 s1 = SamplePixel(data->Src1, u1, v1);
			tjs_uint32 s2 = SamplePixel(data->Src2, u2, v2);
			dp[px] = Blend(s1, s2, Alpha); // Alpha=0 -> s1, 255 -> s2
		}
	}
}
//---------------------------------------------------------------------------
tjs_error tTVPMorphingTransHandler::Process(tTVPDivisibleData *data)
{
	// 端点は単純コピー (元 FUN_10008010: alpha==0 は src1, ==0xff は src2)
	if(Alpha == 0 || Alpha == 255)
	{
		iTVPScanLineProvider *src = (Alpha == 0) ? data->Src1 : data->Src2;
		for(tjs_int n = 0; n < data->Height; n++)
		{
			tjs_int y = data->Top + n;
			tjs_uint32 *dest;
			const tjs_uint32 *sp;
			if(TJS_FAILED(data->Dest->GetScanLineForWrite(data->DestTop + n, (void**)&dest)))
				return TJS_E_FAIL;
			if(TJS_FAILED(src->GetScanLine(y, (const void**)&sp)))
				return TJS_E_FAIL;
			tjs_uint32 *dp = dest + data->DestLeft - data->Left;
			for(tjs_int c = data->Left; c < data->Left + data->Width; c++)
				dp[c] = sp[c];
		}
		return TJS_S_OK;
	}

	// (1) まず隙間用に、バンド全体を src1/src2 のストレートなクロスフェードで塗る。
	//     三角形が覆う画素は後段で上書きされる。
	//     (オリジナルは未被覆の隙間だけを塗るが、視覚的な結果は等価。)
	for(tjs_int n = 0; n < data->Height; n++)
	{
		tjs_int y = data->Top + n;
		tjs_uint32 *dest;
		const tjs_uint32 *src1;
		const tjs_uint32 *src2;
		if(TJS_FAILED(data->Dest->GetScanLineForWrite(data->DestTop + n, (void**)&dest)))
			return TJS_E_FAIL;
		if(TJS_FAILED(data->Src1->GetScanLine(y, (const void**)&src1)))
			return TJS_E_FAIL;
		if(TJS_FAILED(data->Src2->GetScanLine(y, (const void**)&src2)))
			return TJS_E_FAIL;
		tjs_uint32 *dp = dest + data->DestLeft - data->Left;
		for(tjs_int c = data->Left; c < data->Left + data->Width; c++)
			dp[c] = Blend(src1[c], src2[c], Alpha);
	}

	// (2) 各三角形パッチをワープ + クロスフェードして上書き。
	for(tjs_int i = 0; i < NumTri; i++)
		RasterizePatch(data, i);

	return TJS_S_OK;
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// options から整数配列を読み取る。
//   元 FUN_100152d0: options->GetValue(name,&var) で tTJSVariant を得て、
//   その中の iTJSDispatch2*(Array) から "count" と PropGetByNum で要素を得る。
//   戻り値は取得できた要素数 (失敗時 -1)。
//---------------------------------------------------------------------------
static tjs_int ReadIntArray(iTVPSimpleOptionProvider *options,
		const tjs_char *name, std::vector<tjs_int> &out)
{
	tTJSVariant var;
	if(TJS_FAILED(options->GetValue(name, &var))) return -1;
	if(var.Type() != tvtObject) return -1;
	iTJSDispatch2 *arr = var.AsObjectNoAddRef();
	if(!arr) return -1;

	// 要素数 ( Array.count )
	tTJSVariant cntvar;
	if(TJS_FAILED(arr->PropGet(0, TJS_N("count"), NULL, &cntvar, arr))) return -1;
	tjs_int count = (tjs_int)cntvar;
	if(count < 0) count = 0;

	out.clear();
	out.reserve(count);
	for(tjs_int i = 0; i < count; i++)
	{
		tTJSVariant e;
		if(TJS_FAILED(arr->PropGetByNum(0, i, &e, arr)))
			out.push_back(0);
		else
			out.push_back((tjs_int)e);
	}
	return count;
}
//---------------------------------------------------------------------------
class tTVPMorphingTransHandlerProvider : public iTVPTransHandlerProvider
{
	tjs_uint RefCount;
public:
	tTVPMorphingTransHandlerProvider() { RefCount = 1; }
	~tTVPMorphingTransHandlerProvider() {}

	tjs_error AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }

	tjs_error GetName(const tjs_char ** name)
	{
		if(name) *name = TJS_N("morphing");
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
		if(type) *type = ttExchange;
		if(updatetype) *updatetype = tutDivisible;
		if(!handler) return TJS_E_FAIL;
		if(!options) return TJS_E_FAIL;
		if(src1w != src2w || src1h != src2h) return TJS_E_FAIL;

		// time
		tTJSVariant tmp;
		if(TJS_FAILED(options->GetValue(TJS_N("time"), &tmp))) return TJS_E_FAIL;
		if(tmp.Type() == tvtVoid) return TJS_E_FAIL;
		tjs_uint64 time = (tjs_int64)tmp;
		if(time < 2) time = 2;

		// before / after 配列 (※ 実キーは "before"。仕様書の "befor" は誤記)
		std::vector<tjs_int> before, after;
		tjs_int nb = ReadIntArray(options, TJS_N("before"), before);
		tjs_int na = ReadIntArray(options, TJS_N("after"),  after);

		tjs_int tb = (nb > 0) ? nb / 6 : 0;
		tjs_int ta = (na > 0) ? na / 6 : 0;
		tjs_int numtri = (tb < ta) ? tb : ta; // 両者の少ない方
		if(numtri > 256) numtri = 256;        // 元 DLL は 0x100 で上限
		if(numtri < 0) numtri = 0;

		tTVPMorphPatch *patches = new tTVPMorphPatch[numtri > 0 ? numtri : 1];
		for(tjs_int i = 0; i < numtri; i++)
		{
			for(tjs_int v = 0; v < 3; v++)
			{
				patches[i].BeforeX[v] = before[i*6 + v*2 + 0];
				patches[i].BeforeY[v] = before[i*6 + v*2 + 1];
				patches[i].AfterX[v]  = after [i*6 + v*2 + 0];
				patches[i].AfterY[v]  = after [i*6 + v*2 + 1];
			}
		}

		*handler = new tTVPMorphingTransHandler(time, src1w, src1h, patches, numtri);
		return TJS_S_OK;
	}

} static * MorphingTransHandlerProvider;
//---------------------------------------------------------------------------
void RegisterMorphingTransHandlerProvider()
{
	MorphingTransHandlerProvider = new tTVPMorphingTransHandlerProvider();
	TVPAddTransHandlerProvider(MorphingTransHandlerProvider);
}
//---------------------------------------------------------------------------
void UnregisterMorphingTransHandlerProvider()
{
	TVPRemoveTransHandlerProvider(MorphingTransHandlerProvider);
	MorphingTransHandlerProvider->Release();
}
//---------------------------------------------------------------------------
