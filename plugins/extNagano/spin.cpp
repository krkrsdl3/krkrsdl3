#include "tjsCommHead.h"
#include "cpu_types.h"
#include "transhandler.h"
#include "tjsErrorDefs.h"
#include "TVPMsg.h"
#include "TVPTrans.h"
#include "tjsVariant.h"
#include "tjsConfig.h"
#include "spin.h"
#include "common.h"
#include <math.h>

//---------------------------------------------------------------------------
// spin トランジション
//   二枚のレイヤ(画像)を垂直軸まわりに回転させながら切り替える。
//   type1(変化前画像) / type2(変化後画像) で回転タイプを個別に指定する。
//
//   (extNagano.dll: tTVPSpinFadeTransHandler / ...Provider の復元)
//
//   復元元 Ghidra 関数:
//     Provider::GetName          = FUN_10018630  (-> L"spin")
//     Provider::Release          = FUN_10018650
//     Provider::StartTransition  = FUN_100188d0  (time,type1,type2 読取+既定値)
//     Provider::AddRef           = FUN_10007730
//     Handler ctor               = FUN_100186d0  (メンバ初期化+配列確保)
//     Handler::StartProcess      = FUN_10018270  (進捗計算+列変換テーブル生成+可視判定)
//     Handler::EndProcess        = FUN_10018000
//     Handler::Process           = FUN_10018070  (列テーブルを使った描画)
//     Handler::AddRef            = FUN_10007730
//     Handler::Release           = FUN_10016840
//     Handler::SetOption         = FUN_10016870  (return 0)
//     Handler::MakeFinalImage    = FUN_10006e90  (*dest = src2)
//     回転係数計算 (角度は FPU 上で失われ本復元では再構成):
//       FUN_10017e00 = Static     (type -1  : 動かない)
//       FUN_10017e30 = Box        (type 0,1 および予約2,3,8-11の既定 : 直方体回転)
//       FUN_10017f10 = DoorLeft   (type 4,5 : 左端を軸にしたドア回転)
//       FUN_10017f80 = DoorRight  (type 6,7 : 右端を軸にしたドア回転)
//
//   ■ 復元の確信度について
//   ・メンバ構成 / Process / StartProcess の制御フロー / 可視(select)判定 /
//     列テーブル(源X・縦圧縮率)の意味と Provider 周りは、デコンパイル結果から
//     ほぼ確実に復元できた (高確信度)。
//   ・各回転タイプの「座標変換式そのもの」は、デコンパイルでは回転角が FPU
//     スタック上に載っており逆アセンブルで失われている。原DLLの係数式
//     (D=2*W を焦点/視点距離とする射影, π 除算, +0.5 丸め 等)と整合する
//     クリーンな透視回転モデルとして再構成した (中～低確信度)。
//     左右・奥手前の符号割当ては doc の記述に沿った最善推定 (要実機確認)。
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// 回転タイプ番号 (doc / FUN_10018270 の switch より)
//   -1 : 動かない (Static)
//    0 : 直方体の回転 (Box)  ... type1=左 / type2=右
//    1 : 直方体の回転 (Box)  ... type1=右 / type2=左
//    2,3      : 予約 (未実装。原DLLでは default=Box にフォールバック)
//    4 : ドア回転, 左端が軸 (DoorLeft)   type1=奥 / type2=手前
//    5 : ドア回転, 左端が軸 (DoorLeft)   type1=手前 / type2=奥
//    6 : ドア回転, 右端が軸 (DoorRight)  type1=奥 / type2=手前
//    7 : ドア回転, 右端が軸 (DoorRight)  type1=手前 / type2=奥
//    8,9,10,11: 予約 (未実装。原DLLでは default=Box にフォールバック)
//---------------------------------------------------------------------------

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline tjs_int RoundToInt(double v)
{
	// FUN_10019af0 (ROUND) の等価。0.5 丸め。
	return (tjs_int)floor(v + 0.5);
}

//---------------------------------------------------------------------------
class tTVPSpinFadeTransHandler : public iTVPDivisibleTransHandler
{
	tjs_int RefCount;              // +0x04

protected:
	// --- 原DLL のメンバオフセットを対応コメントで併記 ---
	tjs_uint64 StartTick;         // +0x08 開始 tick
	tjs_uint64 Time;              // +0x10 所要時間
	tjs_uint64 CurPos;            // +0x18 現在進捗 (1..Time にクランプ)
	tjs_int    Width;             // +0x20 画像幅  W
	tjs_int    Height;            // +0x24 画像高  H
	double     Dist;              // +0x28 D = 2*W (透視の焦点/視点距離として使用)

	// +0x30 / +0x68 に原DLLは各画像の変換係数7個(double)を持つが、
	// 本復元では列テーブルを直接生成するため係数配列は保持しない。

	tjs_int    Type1;             // +0xa0 変化前画像の回転タイプ
	tjs_int    Type2;             // +0xa4 変化後画像の回転タイプ

	tjs_int   *Col1;              // +0xa8 image1: 各 dest 列 -> 源X (無効列は範囲外値)
	tjs_int   *Col2;              // +0xac image2: 各 dest 列 -> 源X
	tjs_int   *Slope1;            // +0xb0 image1: 各 dest 列 -> 縦圧縮率 (.8 固定小数, 256=等倍)
	tjs_int   *Slope2;            // +0xb4 image2: 各 dest 列 -> 縦圧縮率
	tjs_int   *Select;            // +0xb8 各 dest 列 -> 0:image1 / 1:image2 / -1:黒

	bool       First;             // +0xbc 最初の呼び出しか
	tjs_int    FrameCount;        // +0xc0 フレーム数 (fps ログ用)

private:
	void ComputeColumns(tjs_int type, double theta, tjs_int *col, tjs_int *slope);

public:
	tTVPSpinFadeTransHandler(tjs_uint64 time, tjs_int width, tjs_int height,
			tjs_int type1, tjs_int type2)
		: StartTick(0), Time(time), CurPos(0), Width(width), Height(height),
		  Type1(type1), Type2(type2), First(true), FrameCount(0)
	{
		RefCount = 1;
		Dist = (double)(width * 2);   // D = 2W (FUN_100186d0: this+0x28)
		Col1   = new tjs_int[width];
		Col2   = new tjs_int[width];
		Slope1 = new tjs_int[width];
		Slope2 = new tjs_int[width];
		Select = new tjs_int[width];
	}
	virtual ~tTVPSpinFadeTransHandler()
	{
		// FUN_10018870 / Catch_10018806 に対応
		delete [] Col1;
		delete [] Col2;
		delete [] Slope1;
		delete [] Slope2;
		delete [] Select;
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
		*dest = src2;             // FUN_10006e90
		return TJS_S_OK;
	}
};
//---------------------------------------------------------------------------
// 1 枚の画像について、回転角 theta の透視回転による列テーブルを生成する。
//   type で回転軸位置を選ぶ:
//     Static(-1)   : 恒等 (回転しない)
//     Box(0,1,他)  : 画像中心 (源X=W/2) まわりに回転
//     DoorLeft(4,5): 左端 (源X=0) を軸に回転
//     DoorRight(6,7): 右端 (源X=W) を軸に回転
//
//   透視モデル (D = 2W = 焦点/視点距離):
//     源列 sx の軸からの水平距離 X0 = sx - axisSrc を Y 軸まわりに theta 回転し
//       画面X = axisScreen + D * (X0*cosθ) / (X0*sinθ + D)
//       縦倍率(画面/源) scale = D / (X0*sinθ + D)
//     dest 列 x について逆算し、源列 sx と縦圧縮率 slope(=1/scale, .8固定) を得る。
//   ※原DLL FUN_10017e30/f10/f80 の係数式(cosθ,sinθ,π,+0.5)と整合するよう
//     再構成した近似。厳密なピクセル一致は保証しない。
//---------------------------------------------------------------------------
void tTVPSpinFadeTransHandler::ComputeColumns(tjs_int type, double theta,
		tjs_int *col, tjs_int *slope)
{
	const tjs_int W = Width;
	const double  D = Dist;

	if(type == -1)
	{
		// Static (FUN_10017e00): 動かない = 恒等変換
		for(tjs_int x = 0; x < W; x++) { col[x] = x; slope[x] = 256; }
		return;
	}

	// 回転軸位置 (源座標 axisSrc / 画面座標 axisScreen)
	double axisSrc, axisScreen;
	if(type == 4 || type == 5)      { axisSrc = 0.0;          axisScreen = 0.0;          } // DoorLeft
	else if(type == 6 || type == 7) { axisSrc = (double)W;    axisScreen = (double)W;    } // DoorRight
	else                            { axisSrc = W * 0.5;      axisScreen = W * 0.5;      } // Box(0,1,予約)

	const double c = cos(theta);
	const double s = sin(theta);

	for(tjs_int x = 0; x < W; x++)
	{
		// 画面X から源オフセット X0 を逆算:
		//   u = x - axisScreen,  u*(X0*s + D) = D*X0*c
		//   X0 = u*D / (D*c - u*s)
		const double u = (double)x - axisScreen;
		const double denom = D * c - u * s;
		bool valid = (fabs(denom) > 1e-6);
		tjs_int sxi = W;   // 既定は範囲外 (無効)
		tjs_int slp = 256;
		if(valid)
		{
			const double X0  = u * D / denom;
			const double sx  = axisSrc + X0;
			const double zpd = X0 * s + D;    // 深度 + 視点距離
			if(zpd > 1e-6 && sx >= 0.0 && sx < (double)W)
			{
				sxi = RoundToInt(sx);
				if(sxi < 0) sxi = 0; else if(sxi >= W) sxi = W - 1;
				// slope = 源行/dest行 = 1/scale = (X0*sinθ + D)/D * 256
				double sl = 256.0 * zpd / D;
				slp = RoundToInt(sl);
				if(slp < 1) slp = 1;
			}
			else
			{
				sxi = W;   // 視点の裏 / 源範囲外 -> 無効
			}
		}
		col[x]   = sxi;
		slope[x] = slp;
	}
}
//---------------------------------------------------------------------------
tjs_error tTVPSpinFadeTransHandler::StartProcess(tjs_uint64 tick)
{
	// FUN_10018270
	FrameCount++;
	if(First)
	{
		First = false;
		StartTick = tick;
	}

	tjs_uint64 elapsed = tick - StartTick;
	if(elapsed >= Time) elapsed = Time;
	if(elapsed == 0)    elapsed = 1;   // 原DLL: 0 は 1 に繰り上げ
	CurPos = elapsed;

	// 進捗率 r = 0..1
	const double r = (double)CurPos / (double)Time;

	// 回転スケジュール (再構成; 要実機確認):
	//   image1(変化前)は角度 0 -> ±90° へ回って退場、
	//   image2(変化後)は ∓90° -> 0 へ回って入場する (exchange)。
	//   符号: type が偶数なら +、奇数なら - (0/1, 4/5, 6/7 の左右・奥手前の別)。
	//   image2 は image1 と逆符号 (doc: 同じ番号でも type2 は奥手前が逆)。
	const double sign1 = ((Type1 & 1) == 0) ? 1.0 : -1.0;
	const double sign2 = -(((Type2 & 1) == 0) ? 1.0 : -1.0);
	const double theta1 = (Type1 == -1) ? 0.0 : sign1 * (M_PI * 0.5) * r;
	const double theta2 = (Type2 == -1) ? 0.0 : sign2 * (M_PI * 0.5) * (r - 1.0);

	ComputeColumns(Type1, theta1, Col1, Slope1);
	ComputeColumns(Type2, theta2, Col2, Slope2);

	// 可視判定 (FUN_10018270 後半): 列ごとにどちらの画像を出すか決める。
	//   image1 が範囲外なら image2 (それも範囲外なら黒)。
	//   両方有効なら slope の小さい(=手前/圧縮の少ない)方を採用。原DLLと同じ。
	for(tjs_int x = 0; x < Width; x++)
	{
		tjs_int v1 = Col1[x];  bool ok1 = (v1 >= 0 && v1 < Width);
		tjs_int v2 = Col2[x];  bool ok2 = (v2 >= 0 && v2 < Width);
		if(!ok1)
			Select[x] = ok2 ? 1 : -1;
		else if(ok2 && Slope2[x] < Slope1[x])
			Select[x] = 1;
		else
			Select[x] = 0;
	}
	return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
tjs_error tTVPSpinFadeTransHandler::EndProcess()
{
	// FUN_10018000: 進捗が所要時間に達したら終了
	if(CurPos == Time) return TJS_S_FALSE;
	return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
tjs_error tTVPSpinFadeTransHandler::Process(tTVPDivisibleData *data)
{
	// FUN_10018070
	// 端の特殊ケース: 進捗 0 なら src1、進捗満了なら src2 をそのまま採用する。
	// (StartProcess で CurPos は 1..Time にクランプされるため通常は満了側のみ発生)
	if(CurPos == 0)
	{
		data->Dest     = data->Src1;
		data->DestLeft = data->Src1Left;
		data->DestTop  = data->Src1Top;
		return TJS_S_OK;
	}
	if(CurPos == Time)
	{
		data->Dest     = data->Src2;
		data->DestLeft = data->Src2Left;
		data->DestTop  = data->Src2Top;
		return TJS_S_OK;
	}

	const tjs_int H    = Height;
	const tjs_int hc   = H >> 1;          // 縦回転軸 = 画像中央
	const tjs_int left = data->Left;
	const tjs_int right= data->Left + data->Width;

	for(tjs_int n = 0; n < data->Height; n++)
	{
		const tjs_int y = data->Top + n;

		tjs_uint32 *dest;
		if(TJS_FAILED(data->Dest->GetScanLineForWrite(data->DestTop + n, (void**)&dest)))
			return TJS_E_FAIL;

		// dest の絶対列 c は dp[c] に書き込む (scanline.cpp と同じ基点)
		tjs_uint32 *dp = dest + data->DestLeft - data->Left;

		const tjs_int yc = y - hc;   // 中央からの縦距離

		for(tjs_int c = left; c < right; c++)
		{
			const tjs_int sel = Select[c];
			if(sel == 0)
			{
				// image1 (src1) を採用
				const tjs_int srow = ((Slope1[c] * yc) >> 8) + hc;
				if(srow >= 0 && srow < H)
				{
					const tjs_uint32 *src1;
					if(TJS_FAILED(data->Src1->GetScanLine(srow, (const void**)&src1)))
						return TJS_E_FAIL;
					dp[c] = src1[ Col1[c] ];
				}
				else dp[c] = 0;
			}
			else if(sel == 1)
			{
				// image2 (src2) を採用
				const tjs_int srow = ((Slope2[c] * yc) >> 8) + hc;
				if(srow >= 0 && srow < H)
				{
					const tjs_uint32 *src2;
					if(TJS_FAILED(data->Src2->GetScanLine(srow, (const void**)&src2)))
						return TJS_E_FAIL;
					dp[c] = src2[ Col2[c] ];
				}
				else dp[c] = 0;
			}
			else
			{
				dp[c] = 0;    // どちらも不可視 -> 黒
			}
		}
	}
	return TJS_S_OK;
}
//---------------------------------------------------------------------------
class tTVPSpinFadeTransHandlerProvider : public iTVPTransHandlerProvider
{
	tjs_uint RefCount;
public:
	tTVPSpinFadeTransHandlerProvider() { RefCount = 1; }
	~tTVPSpinFadeTransHandlerProvider() {}

	tjs_error AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }

	tjs_error GetName(const tjs_char ** name)
	{
		if(name) *name = TJS_N("spin");   // FUN_10018630
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
		// FUN_100188d0
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

		// type1 既定 0 / type2 既定 1 (FUN_100188d0: uVar3=0, uVar4=1)
		tjs_int type1 = 0;
		tjs_int type2 = 1;
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("type1"), &tmp)) && tmp.Type() != tvtVoid)
			type1 = (tjs_int)(tjs_int64)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("type2"), &tmp)) && tmp.Type() != tvtVoid)
			type2 = (tjs_int)(tjs_int64)tmp;

		*handler = new tTVPSpinFadeTransHandler(time, src1w, src1h, type1, type2);
		return TJS_S_OK;
	}

} static * SpinFadeTransHandlerProvider;
//---------------------------------------------------------------------------
void RegisterSpinFadeTransHandlerProvider()
{
	SpinFadeTransHandlerProvider = new tTVPSpinFadeTransHandlerProvider();
	TVPAddTransHandlerProvider(SpinFadeTransHandlerProvider);
}
//---------------------------------------------------------------------------
void UnregisterSpinFadeTransHandlerProvider()
{
	TVPRemoveTransHandlerProvider(SpinFadeTransHandlerProvider);
	SpinFadeTransHandlerProvider->Release();
}
//---------------------------------------------------------------------------
