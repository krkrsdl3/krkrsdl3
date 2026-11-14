#include "tjsCommHead.h"
#include "cpu_types.h"
#include "transhandler.h"
#include "tjsErrorDefs.h"
#include "TVPMsg.h"
#include "TVPTrans.h"
#include "tjsVariant.h"
#include "tjsConfig.h"
#include <math.h>
#include "honeyturn.h"
#include "common.h"

//---------------------------------------------------------------------------
// honeyturn トランジション  (元 DLL 表記どおり「未完」)
//   標準トランジション 'turn' (正方形タイルの回転めくり) を六角形 (ハニカム)
//   タイルに拡張したもの。各六角形が縦軸まわりにカードのようにめくれ、
//   src1 -> src2 へ切り替わる。六角形ごとに order 方向へ時間差でめくれる。
//
//   復元元 (extNagano.dll Ghidra デコンパイル):
//     Provider::GetName         = FUN_10006ca0   ( -> L"honeyturn" )
//     Provider::AddRef          = FUN_10007730
//     Provider::Release         = FUN_10006cc0
//     Provider::StartTransition = FUN_10006ec0    (time/size/twist/order 読取)
//     Handler  ctor             = FUN_10006d40    (テーブル生成)
//       六角形マップ生成        = FUN_10006330    (per-pixel Map 生成)
//       位相テーブル生成        = FUN_10006600    (PhaseTable[64] 生成)
//     Handler::AddRef           = FUN_10007730 (共通)
//     Handler::Release          = FUN_10016840
//     Handler::SetOption        = FUN_10016870    ( return 0 )
//     Handler::StartProcess     = FUN_100069f0    (Phase = 経過*255/Time)
//     Handler::EndProcess       = FUN_10006a80
//     Handler::Process          = FUN_10006af0    (画素演算)
//     Handler::MakeFinalImage   = FUN_10006e90    ( *dest = src2 )
//     Handler dtor              = FUN_10006e50
//
//   == 復元にあたっての重要事項 (確信度・簡略化) ==
//   元コードは 32bit x87 FPU で六角形の幾何 (sqrt(3) を用いた行ごとの六角形幅・
//   斜辺インデント量) を計算しており、Ghidra はその浮動小数点セットアップの
//   大半をドロップしている (FUN_10019af0 は「FPU の ST0 を round して int 化」
//   するだけの変換関数として現れ、肝心の入力 float が失われている)。
//   このため元 DLL が生成していた per-pixel Map (offset 0x120) と
//   PhaseTable[64] (offset 0x20..0x11f) の *正確な数値* はデコンパイルからは
//   復元不能。
//
//   そこで本実装は:
//     * 実行時の構造 (Time / StartTick / CurTime / Phase[0..255] / FrameCount /
//       First / Width / Height / size / twist / order のメンバ構成、
//       Phase=経過*255/Time、六角形ごとの遅延 delay を Phase から引いて
//       各六角形のローカル位相 [0..63] を得る流れ) はデコンパイルに忠実に。
//     * 六角形タイル分割と各六角形のカードめくり幾何 (sqrt(3) ベース) は
//       ドキュメント記載の意図 (turn の六角形拡張) から再構成した。
//   したがって「見た目」は原典と完全一致はしないが、仕様 (六角形分割 +
//   order による順次めくり + twist ひねり) は満たす。原典自体が未完である点に
//   留意。該当する再構成箇所には個別にコメントを付す。
//
//   dir オプション: ドキュメントには「各六角形の回転方向 (テンキー方向)」と
//   あるが、デコンパイルされた StartTransition (FUN_10006ec0) は time/size/
//   twist/order のみを読み取り dir を参照していない (未完のため未実装)。
//   本実装では API 互換のため dir を読むが、水平めくりの向き (左先行/右先行) を
//   変える軽い近似に留める。
//
//   32bit インラインアセンブラ (x87/MMX) は使わず、すべて C の等価ロジックで
//   実装している (x64 で動作する)。
//---------------------------------------------------------------------------

#ifndef M_SQRT3
	#define M_SQRT3 (1.7320508075688772)
#endif
#ifndef M_SQRT2
	#define M_SQRT2 (1.4142135623730951)
#endif

//---------------------------------------------------------------------------
// per-pixel の前計算セル
//   各画素が属する六角形の中心 (cx,cy) と、その走査線 y における六角形の
//   水平半幅 half、および六角形のめくり遅延 delay を保持する。
//   ( 元 DLL の Map(0x120) 6byte/pixel + PhaseTable(0x20) を、実行時に
//     直接計算しやすい形へ再構成したもの )
struct tTVPHoneyCell
{
	tjs_int cx;     // 所属六角形の中心 X ( 絶対座標 )
	tjs_int cy;     // 所属六角形の中心 Y ( 絶対座標 )
	tjs_int half;   // その走査線での六角形水平半幅 ( px )
	tjs_int delay;  // めくり開始遅延 ( 0..HONEY_MAX_DELAY , Phase から減算 )
};
//---------------------------------------------------------------------------
// Phase(0..255) から delay を引いた値をローカル位相 [0..63] にクランプして
// 使う ( turn と同じ 64 段 )。最後の六角形が Phase=255 付近でめくり終える
// ように、最大遅延を 255-63=192 とする。
static const tjs_int HONEY_MAX_DELAY = 192;
static const tjs_int HONEY_STEPS     = 64;   // ローカル位相段数 (turn 由来)
//---------------------------------------------------------------------------
class tTVPHoneyTurnTransHandler : public iTVPDivisibleTransHandler
{
	tjs_int RefCount; // 参照カウンタ

protected:
	tjs_uint64 StartTick; // 開始 tick          ( 元 offset 0x08 )
	tjs_uint64 Time;      // 所要時間           ( 元 offset 0x10 )
	tjs_uint64 CurTime;   // 現在の経過時間     ( 元 offset 0x18 )
	tjs_int Width;        // 画像幅             ( 元 offset 0x124 )
	tjs_int Height;       // 画像高             ( 元 offset 0x128 )
	tjs_int Size;         // 六角形の一辺の長さ ( 元 offset 0x12c )
	tjs_int Twist;        // ひねり量           ( 元 offset 0x130 )
	tjs_int Order;        // めくり順テンキー   ( 元 offset 0x134 )
	tjs_int Dir;          // 回転方向テンキー   ( 元では未読取・API 互換用 )
	tjs_int Phase;        // 全体位相 0..255    ( 元 offset 0x138 )
	tjs_int FrameCount;   // フレーム数         ( 元 offset 0x140 )
	bool First;           // 最初の呼び出しか   ( 元 offset 0x13c )

	tTVPHoneyCell *Map;   // per-pixel 前計算   ( 元 offset 0x120 の役割 )

	// 六角形タイル分割 + delay を前計算する ( 元 FUN_10006330 / FUN_10006600 に相当。
	// ただし幾何は sqrt(3) ベースで再構成 )
	void BuildMap();

public:
	tTVPHoneyTurnTransHandler(tjs_uint64 time, tjs_int width, tjs_int height,
			tjs_int size, tjs_int twist, tjs_int order, tjs_int dir)
		: Time(time), Width(width), Height(height), Size(size),
		  Twist(twist), Order(order), Dir(dir),
		  Phase(0), FrameCount(0), First(true), Map(0)
	{
		RefCount = 1;
		if(Size < 4) Size = 4; // 極端に小さいと分割が破綻するので下限
		BuildMap();
	}

	virtual ~tTVPHoneyTurnTransHandler()
	{
		if(Map) delete [] Map;
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
		*dest = src2; // 常に最終画像は src2 ( FUN_10006e90 )
		return TJS_S_OK;
	}
};
//---------------------------------------------------------------------------
// 六角形マップの前計算
//   pointy-top ( 上下が頂点、左右が垂直辺 ) の正六角形でハニカム充填し、
//   各画素の所属六角形中心 (cx,cy) を求める。所属判定は redblobgames の
//   pixel->hex ( axial + cube round ) を用いる。
//   half は走査線 y における六角形の水平半幅で、垂直中央帯 (|dy|<=s/2) では
//   最大 (s*sqrt(3)/2)、そこから上下頂点へ線形に 0 へ細る。
//   delay は六角形中心を order 方向へ射影した値を [0,HONEY_MAX_DELAY] に
//   正規化したもの ( = めくり順 )。
//
//   NOTE: 元 DLL は flat/pointy いずれの向きだったか、行スペーシング等の
//         定数も FPU 部が失われ確定できない。ここは再構成 (確信度低)。
void tTVPHoneyTurnTransHandler::BuildMap()
{
	Map = new tTVPHoneyCell[(size_t)Width * (size_t)Height];

	const double s   = (double)Size;          // 一辺の長さ
	const double sq3 = M_SQRT3;
	const double halfW = s * sq3 * 0.5;       // 中央帯での半幅

	// order 方向ベクトル ( テンキー配列 : 1=左下 ... 9=右上 、5=中央放射 )
	// 画面座標は Y 下向きなので、テンキーの「上」= -Y 。
	double odx = 0.0, ody = 0.0;
	bool   radial = false;
	switch(Order)
	{
	case 1: odx = -1; ody = +1; break;
	case 2: odx =  0; ody = +1; break;
	case 3: odx = +1; ody = +1; break;
	case 4: odx = -1; ody =  0; break;
	case 5: radial = true;      break; // 中央から放射状にめくる
	case 6: odx = +1; ody =  0; break;
	case 7: odx = -1; ody = -1; break;
	case 8: odx =  0; ody = -1; break;
	case 9: odx = +1; ody = -1; break;
	default: odx = 0; ody = +1; break; // 既定は上から下 (2)
	}
	double naxis = ((odx != 0.0) ? 1.0 : 0.0) + ((ody != 0.0) ? 1.0 : 0.0);
	if(naxis < 1.0) naxis = 1.0;

	const double invW = (Width  > 1) ? 1.0 / (double)(Width  - 1) : 0.0;
	const double invH = (Height > 1) ? 1.0 / (double)(Height - 1) : 0.0;

	for(tjs_int y = 0; y < Height; y++)
	{
		tTVPHoneyCell *row = Map + (size_t)y * (size_t)Width;
		for(tjs_int x = 0; x < Width; x++)
		{
			// --- pixel -> hex ( pointy-top axial 座標 ) ---
			double aq = (sq3 / 3.0 * (double)x - 1.0 / 3.0 * (double)y) / s;
			double ar = (2.0 / 3.0 * (double)y) / s;

			// cube round
			double cxq = aq;
			double czq = ar;
			double cyq = -cxq - czq;
			double rx = floor(cxq + 0.5);
			double ry = floor(cyq + 0.5);
			double rz = floor(czq + 0.5);
			double dx = fabs(rx - cxq);
			double dy2 = fabs(ry - cyq);
			double dz = fabs(rz - czq);
			if(dx > dy2 && dx > dz)      rx = -ry - rz;
			else if(dy2 > dz)            ry = -rx - rz;
			else                         rz = -rx - ry;
			double fq = rx;   // 丸めた axial q
			double fr = rz;   // 丸めた axial r

			// hex 中心 ( 絶対座標 )
			double dcx = s * sq3 * (fq + fr * 0.5);
			double dcy = s * 1.5 * fr;
			tjs_int cx = (tjs_int)(dcx + 0.5);
			tjs_int cy = (tjs_int)(dcy + 0.5);

			// --- 走査線 y における六角形水平半幅 ---
			double ady = fabs((double)y - dcy);
			double h;
			if(ady <= s * 0.5)      h = halfW;
			else if(ady <= s)       h = halfW * (s - ady) / (s * 0.5);
			else                    h = 0.0; // 丸め誤差での外側 ( 通常起きない )
			tjs_int half = (tjs_int)(h + 0.5);
			if(half < 1) half = 1;

			// --- めくり遅延 ( order ) ---
			double nx = (double)cx * invW; // 0..1
			double ny = (double)cy * invH; // 0..1
			double p;
			if(radial)
			{
				double ex = (nx - 0.5) * 2.0;
				double ey = (ny - 0.5) * 2.0;
				p = sqrt(ex * ex + ey * ey) / M_SQRT2; // 中央=0, 角=1
				if(p > 1.0) p = 1.0;
			}
			else
			{
				double px = (odx > 0) ? nx : (odx < 0) ? (1.0 - nx) : 0.0;
				double py = (ody > 0) ? ny : (ody < 0) ? (1.0 - ny) : 0.0;
				p = (px + py) / naxis; // 0..1
			}
			tjs_int delay = (tjs_int)(p * (double)HONEY_MAX_DELAY + 0.5);
			if(delay < 0) delay = 0;
			if(delay > HONEY_MAX_DELAY) delay = HONEY_MAX_DELAY;

			tTVPHoneyCell &c = row[x];
			c.cx = cx;
			c.cy = cy;
			c.half = half;
			c.delay = delay;
		}
	}
}
//---------------------------------------------------------------------------
tjs_error tTVPHoneyTurnTransHandler::StartProcess(tjs_uint64 tick)
{
	// FUN_100069f0 相当
	FrameCount++;
	if(First)
	{
		First = false;
		StartTick = tick;
	}

	CurTime = tick - StartTick;
	if(CurTime >= Time)
	{
		CurTime = Time;
		Phase = 255; // 元コードでは 0xff
	}
	else
	{
		// Phase = 経過 * 255 / Time  ( 0..255 )
		Phase = (tjs_int)(CurTime * 255 / Time);
	}
	return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
tjs_error tTVPHoneyTurnTransHandler::EndProcess()
{
	// FUN_10006a80 相当 : CurTime == Time で終了
	if(CurTime == Time) return TJS_S_FALSE;
	return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
tjs_error tTVPHoneyTurnTransHandler::Process(tTVPDivisibleData *data)
{
	// FUN_10006af0 相当。
	// data->Left,Top,Width,Height の矩形についてのみ転送する。
	// 元コードは per-pixel Map(offset 0x120) と PhaseTable(0x20) を参照して
	// 単純な水平シフト ( dest[x] = src[x + sx] , 範囲外は背景 0 ) を行っていた。
	// ここでは同等の「各六角形を縦軸まわりに水平めくり」を Map から直接計算。

	const tjs_int left   = data->Left;
	const tjs_int right  = data->Left + data->Width;
	const tjs_int phaseG = Phase;

	// twist によるひねり量 ( 走査線 dy に比例した水平シアー )。
	// 元 DLL では PhaseTable 生成時に size 倍の項として入っていたが数値は
	// 失われているため、控えめなシアーとして近似 ( 確信度低 )。
	const double twistScale = (double)Twist / (double)Size;

	// dir : 水平めくりの向き。左方向 (1,4,7) のとき先行方向を反転する近似。
	const bool dirLeft = (Dir == 1 || Dir == 4 || Dir == 7);

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

		// dest の絶対列 c は dp[c] へ書き込む ( scanline.cpp と同じ基点 )
		tjs_uint32 *dp = dest + data->DestLeft - data->Left;

		const tTVPHoneyCell *mrow = Map + (size_t)y * (size_t)Width;

		for(tjs_int c = left; c < right; c++)
		{
			const tTVPHoneyCell &cell = mrow[c];

			// この六角形のローカル位相 ( 0..63 )
			tjs_int lp = phaseG - cell.delay;

			if(lp <= 0)
			{
				// まだめくられていない -> src1
				dp[c] = src1[c];
				continue;
			}
			if(lp >= HONEY_STEPS - 1)
			{
				// めくり完了 -> src2
				dp[c] = src2[c];
				continue;
			}

			// カードめくり ( 縦軸まわり回転の水平近似 )
			//   前半 lp<32 : src1 が中央へ細っていく
			//   後半 lp>=32: src2 が中央から広がる
			const tjs_uint32 *src;
			double vis; // 見かけの半幅比 ( 0..1 )
			if(lp < HONEY_STEPS / 2)
			{
				src = src1;
				vis = (double)(HONEY_STEPS / 2 - lp) / (double)(HONEY_STEPS / 2);
			}
			else
			{
				src = src2;
				vis = (double)(lp - (HONEY_STEPS / 2 - 1)) / (double)(HONEY_STEPS / 2);
			}

			double visHalf = (double)cell.half * vis;
			if(visHalf < 0.5)
			{
				dp[c] = 0; // カードが真横 -> 背景 ( 元コードも背景 = 0 )
				continue;
			}

			double rel = (double)(c - cell.cx); // 中心からの水平距離
			if(dirLeft) rel = -rel;              // dir 近似 : 先行方向反転
			if(rel < -visHalf || rel > visHalf)
			{
				dp[c] = 0; // 六角形カードの外 -> 背景
				continue;
			}

			// カード内: 縮んだカードへ元画像全幅を圧入 ( 遠近圧縮 )
			double srcRel = rel * (double)cell.half / visHalf;
			// twist : 走査線位置に応じた水平シアー
			srcRel += twistScale * ((double)y - (double)cell.cy) * (1.0 - vis);

			tjs_int srcX = cell.cx + (tjs_int)(srcRel + (srcRel >= 0 ? 0.5 : -0.5));
			if(srcX < 0 || srcX >= Width)
				dp[c] = 0; // 範囲外 -> 背景 ( 元コードと同じ )
			else
				dp[c] = src[srcX];
		}
	}
	return TJS_S_OK;
}
//---------------------------------------------------------------------------
class tTVPHoneyTurnTransHandlerProvider : public iTVPTransHandlerProvider
{
	tjs_uint RefCount; // 参照カウンタ
public:
	tTVPHoneyTurnTransHandlerProvider() { RefCount = 1; }
	~tTVPHoneyTurnTransHandlerProvider() {}

	tjs_error AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }

	tjs_error GetName(const tjs_char ** name)
	{
		if(name) *name = TJS_N("honeyturn"); // FUN_10006ca0
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
		// FUN_10006ec0 相当
		if(type) *type = ttExchange;
		if(updatetype) *updatetype = tutDivisible;
		if(!handler) return TJS_E_FAIL;
		if(!options) return TJS_E_FAIL;
		if(src1w != src2w || src1h != src2h) return TJS_E_FAIL;

		tTJSVariant tmp;

		// time : 必須 ( 元コードも void なら失敗 )
		if(TJS_FAILED(options->GetValue(TJS_N("time"), &tmp))) return TJS_E_FAIL;
		if(tmp.Type() == tvtVoid) return TJS_E_FAIL;
		tjs_uint64 time = (tjs_int64)tmp;
		if(time < 2) time = 2;

		// size / twist / order / dir :
		//   元 DLL (FUN_10006ec0) は size/twist/order を必須として読むが、
		//   実用上は既定値を与えた方が堅牢なので、未指定時は既定値を用いる
		//   ( この点のみデコンパイルから緩和。dir は元では未読取で本実装の追加 )。
		tjs_int size  = 40; // 六角形の一辺 ( 既定 )
		tjs_int twist = 0;
		tjs_int order = 2;  // 上->下
		tjs_int dir   = 6;  // 右 ( 参考値・効果は近似 )

		if(TJS_SUCCEEDED(options->GetValue(TJS_N("size"), &tmp)) && tmp.Type() != tvtVoid)
			size = (tjs_int)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("twist"), &tmp)) && tmp.Type() != tvtVoid)
			twist = (tjs_int)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("order"), &tmp)) && tmp.Type() != tvtVoid)
			order = (tjs_int)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("dir"), &tmp)) && tmp.Type() != tvtVoid)
			dir = (tjs_int)tmp;

		if(size < 1) size = 1;

		*handler = new tTVPHoneyTurnTransHandler(time, src1w, src1h, size, twist, order, dir);
		return TJS_S_OK;
	}

} static * HoneyTurnTransHandlerProvider;
//---------------------------------------------------------------------------
void RegisterHoneyTurnTransHandlerProvider()
{
	HoneyTurnTransHandlerProvider = new tTVPHoneyTurnTransHandlerProvider();
	TVPAddTransHandlerProvider(HoneyTurnTransHandlerProvider);
}
//---------------------------------------------------------------------------
void UnregisterHoneyTurnTransHandlerProvider()
{
	TVPRemoveTransHandlerProvider(HoneyTurnTransHandlerProvider);
	HoneyTurnTransHandlerProvider->Release();
}
//---------------------------------------------------------------------------
