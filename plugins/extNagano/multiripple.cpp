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
#include "multiripple.h"
#include "common.h"

//---------------------------------------------------------------------------
// multiripple トランジション
//   複数の波紋が広がり、波紋が通過した跡で画像が src1 -> src2 へ切り替わる。
//   extrans の単一波紋 'ripple' を count 個の波源へ拡張したもの。
//
//   復元元 (extNagano.dll Ghidra デコンパイル):
//     Provider::GetName        = FUN_100161d0  ( -> L"multiripple" )
//     Provider::AddRef         = FUN_10007730
//     Provider::Release        = FUN_100161f0
//     Provider::StartTransition= FUN_10016e70   (オプション読取 + 既定値)
//     Handler  ctor            = FUN_10016350   (テーブル生成 + 波源スケジュール)
//     Handler::AddRef          = (共通) / Release = FUN_10016840
//     Handler::SetOption       = FUN_10016870   ( return 0 )
//     Handler::StartProcess    = FUN_10016940   (各波源の phase / drift 計算)
//     Handler::EndProcess      = FUN_10015590
//     Handler::Process         = FUN_10015640   (画素演算)
//     Handler::MakeFinalImage  = FUN_10006e90   ( *dest = src2 )
//     Handler dtor             = FUN_10016880
//     距離マップ生成           = FUN_10015fd0   (DirTable, sqrt ベース)
//     波形/位相テーブル生成    = FUN_100160d0   (DriftTable A[]=波形, B[]=ブレンド)
//     角の最大距離取得         = FUN_10016270
//
//   NOTE: 元は 32bit x87 FPU を多用しており Ghidra が浮動小数点の
//         セットアップを大半ドロップしている。整数・構造部分はデコンパイルに
//         忠実に、浮動小数点で作られるテーブル(距離マップ・波形)は
//         extrans/ripple.cpp の設計思想から再構成した (該当箇所にコメント)。
//         MMX/EMMX asm 版は使わず C 参照ロジックのみ (x64 で動作)。
//---------------------------------------------------------------------------

#ifndef M_PI
	#define M_PI (3.14159265358979323846)
#endif

//---------------------------------------------------------------------------
// RGB のみをブレンドして α を不透明にする ( デコンパイルは常に | 0xff000000 )
//   opa = 0..255 ,  0 = a , 255 = b
static inline tjs_uint32 BlendRGBOpaque(tjs_uint32 a, tjs_uint32 b, tjs_int opa)
{
	tjs_uint32 ret;
	tjs_uint32 tmp;
	tmp = a & 0x000000ff;  ret  = 0x000000ff & (tmp + (((b & 0x000000ff) - tmp) * opa >> 8));
	tmp = a & 0x0000ff00;  ret |= 0x0000ff00 & (tmp + (((b & 0x0000ff00) - tmp) * opa >> 8));
	tmp = a & 0x00ff0000;  ret |= 0x00ff0000 & (tmp + (((b & 0x00ff0000) - tmp) * opa >> 8));
	return ret | 0xff000000;
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// 距離マップ ( FUN_10015fd0 が生成する param_1[10] 相当 )
//   Map[y*Width + x] = 中心からのオフセット (|dx|,|dy|) に対する波の距離。
//   デコンパイルでは round(sqrt(x*x + y*y)) を格納 ( x87 部はドロップ )。
//   roundness は元の FPU セットアップに含まれていたと推測し、扁平率として
//   y 側にスケールを掛けて楕円状の波を作る ( ripple.cpp の Roundness に相当 )。
//---------------------------------------------------------------------------
class tTVPMRDistMap
{
public:
	tjs_int Width;
	tjs_int Height;
	tjs_uint16 *Map;

	tTVPMRDistMap(tjs_int width, tjs_int height, float roundness)
	{
		Width = width;
		Height = height;
		Map = new tjs_uint16[width * height];
		tjs_uint16 *p = Map;
		for(tjs_int y = 0; y < height; y++)
		{
			double yy = (double)y * roundness; // 扁平率 (再構成)
			for(tjs_int x = 0; x < width; x++)
			{
				double d = sqrt((double)x * x + yy * yy);
				tjs_int v = (tjs_int)(d + 0.5);
				if(v > 65535) v = 65535;
				*(p++) = (tjs_uint16)v;
			}
		}
	}
	~tTVPMRDistMap() { if(Map) delete [] Map; }

	// (dx,dy) は絶対値で渡す ( 0<=dx<Width, 0<=dy<Height )
	tjs_int Get(tjs_int dx, tjs_int dy) const { return Map[dy * Width + dx]; }

	// 中心 (cx,cy) から画像四隅までの最大距離 ( FUN_10016270 相当 )
	tjs_int MaxCornerDist(tjs_int cx, tjs_int cy) const
	{
		tjs_int rx = Width  - cx - 1; if(rx < 0) rx = 0;
		tjs_int ry = Height - cy - 1; if(ry < 0) ry = 0;
		if(cx < 0) cx = 0; else if(cx >= Width)  cx = Width  - 1;
		if(cy < 0) cy = 0; else if(cy >= Height) cy = Height - 1;
		tjs_int m = Get(cx, cy);
		tjs_int t;
		t = Get(rx, cy); if(t > m) m = t;
		t = Get(cx, ry); if(t > m) m = t;
		t = Get(rx, ry); if(t > m) m = t;
		return m;
	}
};
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// 波形/ブレンドテーブル ( FUN_100160d0 が生成する param_1[0xb] 相当 )
//   A[i] : 波形 ( 縦方向のズレ量 )   = round(maxdrift * waveform(i))   ( i < Travel )
//   B[i] : ブレンド寄与             = i * BlendUnit / Travel           ( i < Travel )
//   i >= Travel ( 波が通り過ぎた領域 ) は A=0, B=BlendUnit で一定。
//   waveform は x87 部がドロップされているため ripple.cpp の波形思想で再構成:
//   波長 rwidth の正弦波を wavecount 回、後端へ向けて減衰させる。
//---------------------------------------------------------------------------
class tTVPMRDriftTable
{
public:
	tjs_int *A; // 波形 (縦ズレ)
	tjs_int *B; // ブレンド寄与
	tjs_int Len;

	tTVPMRDriftTable(tjs_int len, tjs_int travel, tjs_int rwidth,
		tjs_int maxdrift, tjs_int blendunit)
	{
		if(len < travel) len = travel;
		if(len < 1) len = 1;
		Len = len;
		A = new tjs_int[len];
		B = new tjs_int[len];
		double rcp_rw = (rwidth > 0) ? (1.0 / (double)rwidth) : 0.0;
		for(tjs_int i = 0; i < len; i++)
		{
			if(i < travel)
			{
				// 波長 rwidth の正弦波 ( 後端に向けて減衰 = 実際には maxdrift ほどズレない )
				double rad = (double)i * rcp_rw * (M_PI * 2.0);
				double env = 1.0 - (double)i / (double)travel; // 減衰包絡 (再構成)
				double s = sin(rad) * env;
				double v = (double)maxdrift * s;
				A[i] = (tjs_int)(v < 0 ? v - 0.5 : v + 0.5);
				B[i] = (tjs_int)((tjs_int64)i * blendunit / travel);
			}
			else
			{
				A[i] = 0;
				B[i] = blendunit;
			}
		}
	}
	~tTVPMRDriftTable()
	{
		if(A) delete [] A;
		if(B) delete [] B;
	}
};
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// 波源 1 個 ( Ripples[] の 0x70 バイト構造体に対応 )
//---------------------------------------------------------------------------
struct tTVPMRRipple
{
	tjs_int CenterX;   // r[0]
	tjs_int CenterY;   // r[1]
	tjs_int Phase;     // r[2] : 現在位相 ( = 進んだ距離 )  StartProcess で更新
	tjs_int Speed;     // r[3] : phase = (CurTime-StartTime)*Speed >> 10   ( .10 固定小数 )
	tjs_int StartTime; // r[4] : 開始 tick(ms)
	tjs_int Duration;  // r[5]
	tjs_int EndTime;   // r[6] : StartTime + Duration
	tjs_int Drift;     // r[7] : 揺れ包絡 0..255   StartProcess で更新
	tjs_int State;     // 0=未開始 1=活動中 2=終了       StartProcess で更新
};
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// tTVPMultiRippleTransHandler
//---------------------------------------------------------------------------
class tTVPMultiRippleTransHandler : public iTVPDivisibleTransHandler
{
	tjs_int RefCount;

protected:
	tjs_uint64 StartTick;
	tjs_uint64 Time;
	tjs_int Width;
	tjs_int Height;
	tjs_int64 CurTime;

	tjs_int Count;         // 波源数 (1..20)
	tjs_int BlendUnit;     // = 765 / Count   ( 波源 1 個通過ごとのブレンド増分 )
	tjs_int MaxDist;       // = round(hypot(Width,Height))  ( 波が進む最大距離 )
	tjs_int RippleTravel;  // = rwidth * wavecount           ( 波の全長 )
	tjs_int MaxDrift;

	tjs_int FinishedCount; // 既に通り過ぎた波源の数 ( StartProcess で更新 )
	bool First;
	tjs_int FrameCount;

	tTVPMRDistMap    *DistMap;
	tTVPMRDriftTable *DriftTable;
	tTVPMRRipple     *Ripples;

public:
	tTVPMultiRippleTransHandler(tjs_uint64 time, tjs_int width, tjs_int height,
		tjs_int count, tjs_int rwidth, tjs_int wavecount, tjs_int maxdrift,
		float roundness, double delaylast)
	{
		RefCount = 1;
		First = true;
		FrameCount = 0;
		CurTime = 0;
		FinishedCount = 0;

		Time = time;
		Width = width;
		Height = height;

		// count は 1..20 にクランプ ( FUN_10016350 )
		if(count < 1) count = 1;
		else if(count > 20) count = 20;
		Count = count;

		if(rwidth < 1) rwidth = 1;
		if(wavecount < 1) wavecount = 1;
		if(maxdrift < 0) maxdrift = 0;
		MaxDrift = maxdrift;
		RippleTravel = rwidth * wavecount;               // param_1[0x13]
		BlendUnit = (tjs_int)(0x2fd / (tjs_int64)Count);  // param_1[0x11] = 765/count
		MaxDist = (tjs_int)(sqrt((double)width * width +
			(double)height * height) + 0.5);              // param_1[0x10]

		// テーブル生成
		DistMap = new tTVPMRDistMap(width, height, roundness);
		tjs_int driftlen = MaxDist * 2;                   // FUN_100160d0: max(2*MaxDist, Travel)
		DriftTable = new tTVPMRDriftTable(driftlen, RippleTravel, rwidth, maxdrift, BlendUnit);

		Ripples = new tTVPMRRipple[Count];

		BuildRipples(delaylast);
	}

	virtual ~tTVPMultiRippleTransHandler()
	{
		if(DistMap)    delete DistMap;
		if(DriftTable) delete DriftTable;
		if(Ripples)    delete [] Ripples;
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
	void BuildRipples(double delaylast);
};
//---------------------------------------------------------------------------
void tTVPMultiRippleTransHandler::BuildRipples(double delaylast)
{
	// 波源の座標と時間スケジュールを決める ( FUN_10016350 後半 )
	//   ・最後のインデックス(Count-1)は画面中央に置かれ、最後に(遅らせて)開始する。
	//   ・それ以外は乱数座標で、dt_min 間隔でずらして開始する。
	tjs_int cx = Width >> 1;
	tjs_int cy = Height >> 1;

	if(Count < 2)
	{
		// 単一波源 ( 中央、遅延なし )
		tjs_int speed = (tjs_int)(((tjs_int64)(MaxDist + RippleTravel) * 0x400) / (tjs_int64)Time);
		if(speed < 1) speed = 1;
		tjs_int dirval = DistMap->MaxCornerDist(cx, cy);
		tTVPMRRipple &r = Ripples[0];
		r.CenterX   = cx;
		r.CenterY   = cy;
		r.Phase     = 0;
		r.Speed     = speed;
		r.StartTime = 0;
		r.Duration  = (tjs_int)(((tjs_int64)(dirval + RippleTravel) * 0x400) / (tjs_int64)speed);
		r.EndTime   = r.Duration;
		r.Drift     = 0;
		r.State     = 0;
		return;
	}

	// 複数波源: 速度は単一時の 2 倍 ( time/2 で割る )
	tjs_int speed = (tjs_int)(((tjs_int64)(MaxDist + RippleTravel) * 0x400) / (tjs_int64)(Time >> 1));
	if(speed < 1) speed = 1;
	tjs_int last = Count - 1;

	// 中央波源 ( 最後のインデックス )
	{
		tTVPMRRipple &r = Ripples[last];
		r.CenterX  = cx;
		r.CenterY  = cy;
		r.Phase    = 0;
		r.Speed    = speed;
		tjs_int dirval = DistMap->MaxCornerDist(cx, cy);
		r.Duration = (tjs_int)(((tjs_int64)(dirval + RippleTravel) * 0x400) / (tjs_int64)speed);
		r.Drift    = 0;
		r.State    = 0;
	}

	// 乱数波源 ( インデックス 0 .. Count-2 )
	//   互いに近すぎないように、最小距離 (Width+Height)/20 を満たすまで振り直す。
	tjs_int mindist = (Width + Height) / 20;
	for(tjs_int j = last - 1; j >= 0; j--)
	{
		tjs_int rx, ry;
		for(;;)
		{
			rx = rand() % Width;
			ry = rand() % Height;
			// 直前 ( インデックス j+1 ) との市街地距離
			tjs_int ddx = rx - Ripples[j + 1].CenterX; if(ddx < 0) ddx = -ddx;
			tjs_int ddy = ry - Ripples[j + 1].CenterY; if(ddy < 0) ddy = -ddy;
			if(ddx + ddy >= mindist) break;
		}
		tTVPMRRipple &r = Ripples[j];
		r.CenterX  = rx;
		r.CenterY  = ry;
		r.Phase    = 0;
		r.Speed    = speed;
		tjs_int dirval = DistMap->MaxCornerDist(rx, ry);
		r.Duration = (tjs_int)(((tjs_int64)(dirval + RippleTravel) * 0x400) / (tjs_int64)speed);
		r.Drift    = 0;
		r.State    = 0;
	}

	// dt_min : 全波源が Time 以内に終わるように選ぶ最小ずらし間隔
	//   dt_min = min( Time, min_{j>=1} (Time - Duration[j]) / j )   ( FUN_10016350 )
	tjs_int64 dtmin = (tjs_int64)Time;
	for(tjs_int j = 1; j <= last - 1; j++)
	{
		tjs_int64 v = ((tjs_int64)Time - Ripples[j].Duration) / j;
		if(v < dtmin) dtmin = v;
	}
	if(dtmin < 0) dtmin = 0;

	// 乱数波源のスケジュール ( start = j * dt_min )
	tjs_int64 acc = 0;
	for(tjs_int j = 0; j < last; j++)
	{
		Ripples[j].StartTime = (tjs_int)acc;
		Ripples[j].EndTime   = (tjs_int)(acc + Ripples[j].Duration);
		acc += dtmin;
	}

	// 中央波源 ( 最後 ) は delaylast 分だけ遅らせる
	//   start = round( (last + (delaylast - 1.0)) * dt_min )   ( 1.0 で遅れ無し )
	{
		double startd = ((double)last + (delaylast - 1.0)) * (double)dtmin;
		tjs_int st = (tjs_int)(startd < 0 ? startd - 0.5 : startd + 0.5);
		if(st < 0) st = 0;
		Ripples[last].StartTime = st;
		Ripples[last].EndTime   = st + Ripples[last].Duration;
	}
}
//---------------------------------------------------------------------------
tjs_error tTVPMultiRippleTransHandler::StartProcess(tjs_uint64 tick)
{
	FrameCount++;
	if(First)
	{
		First = false;
		StartTick = tick;
	}

	CurTime = (tjs_int64)(tick - StartTick);
	if(CurTime > (tjs_int64)Time) CurTime = (tjs_int64)Time; // クランプ

	// 各波源の phase / drift / 状態を更新 ( FUN_10016940 )
	FinishedCount = 0;
	for(tjs_int i = 0; i < Count; i++)
	{
		tTVPMRRipple &r = Ripples[i];
		if(CurTime >= r.EndTime)
		{
			r.State = 2; // 終了 ( 波が通り過ぎた )
			FinishedCount++;
		}
		else if(CurTime >= r.StartTime)
		{
			r.State = 1; // 活動中
			tjs_int64 dt = CurTime - r.StartTime;
			// phase = (CurTime-StartTime) * Speed >> 10
			r.Phase = (tjs_int)((dt * r.Speed) >> 10);
			// drift 包絡 = 255 - (経過 * 256 / Duration)   ( 序盤ほど揺れが強い )
			tjs_int dur = r.Duration > 0 ? r.Duration : 1;
			tjs_int prog = (tjs_int)((dt * 0x100) / dur);
			r.Drift = (~prog) & 0xff;
		}
		else
		{
			r.State = 0; // 未開始
		}
	}

	return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
tjs_error tTVPMultiRippleTransHandler::EndProcess()
{
	if(CurTime >= (tjs_int64)Time) return TJS_S_FALSE; // トランジション終了
	return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
tjs_error tTVPMultiRippleTransHandler::Process(tTVPDivisibleData *data)
{
	// 縦方向の変位を伴うため、変位先の行 (y+vdisp) のスキャンラインを
	// 遅延取得してキャッシュする。
	const tjs_uint32 **s1rows = new const tjs_uint32*[Height];
	const tjs_uint32 **s2rows = new const tjs_uint32*[Height];
	for(tjs_int i = 0; i < Height; i++) { s1rows[i] = 0; s2rows[i] = 0; }

	tjs_error result = TJS_S_OK;

	const tjs_int left  = data->Left;
	const tjs_int right = data->Left + data->Width;
	const tjs_int base  = FinishedCount * BlendUnit; // 通過済み波源によるブレンド基準値

	for(tjs_int n = 0; n < data->Height; n++)
	{
		const tjs_int y = data->Top + n;

		tjs_uint32 *dest;
		if(TJS_FAILED(data->Dest->GetScanLineForWrite(data->DestTop + n, (void**)&dest)))
		{ result = TJS_E_FAIL; break; }
		tjs_uint32 *dp = dest + data->DestLeft - data->Left;

		for(tjs_int x = left; x < right; x++)
		{
			tjs_int blend = base;
			tjs_int vdisp = 0;

			for(tjs_int i = 0; i < Count; i++)
			{
				const tTVPMRRipple &r = Ripples[i];
				if(r.State != 1) continue; // 活動中のみ

				tjs_int dx = x - r.CenterX; if(dx < 0) dx = -dx;
				tjs_int dy = y - r.CenterY; if(dy < 0) dy = -dy;
				tjs_int dist = DistMap->Get(dx, dy);
				tjs_int wc = r.Phase - dist; // 波の座標 ( 負なら波未到達 )
				if(wc >= 0)
				{
					if(wc >= DriftTable->Len) wc = DriftTable->Len - 1;
					blend += DriftTable->B[wc];
					vdisp -= (DriftTable->A[wc] * r.Drift) >> 8;
				}
			}

			// 変位先の行を [0,Height-1] にクランプ
			tjs_int row = y + vdisp;
			if(row < 0) row = 0; else if(row >= Height) row = Height - 1;

			const tjs_uint32 *s1 = s1rows[row];
			if(!s1)
			{
				if(TJS_FAILED(data->Src1->GetScanLine(row, (const void**)&s1)))
				{ result = TJS_E_FAIL; break; }
				s1rows[row] = s1;
			}
			const tjs_uint32 *s2 = s2rows[row];
			if(!s2)
			{
				if(TJS_FAILED(data->Src2->GetScanLine(row, (const void**)&s2)))
				{ result = TJS_E_FAIL; break; }
				s2rows[row] = s2;
			}

			tjs_int ratio = blend & 0xff;
			tjs_uint32 a, b;
			if((blend & 0x100) == 0) { a = s1[x]; b = s2[x]; } // 通常: src1 -> src2
			else                     { a = s2[x]; b = s1[x]; } // 反転 ( ブレンドが 256 を跨ぐ度に入れ替わる )

			dp[x] = BlendRGBOpaque(a, b, ratio);
		}
		if(result != TJS_S_OK) break;
	}

	delete [] s1rows;
	delete [] s2rows;
	return result;
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
class tTVPMultiRippleTransHandlerProvider : public iTVPTransHandlerProvider
{
	tjs_uint RefCount;
public:
	tTVPMultiRippleTransHandlerProvider() { RefCount = 1; }
	~tTVPMultiRippleTransHandlerProvider() {}

	tjs_error AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }

	tjs_error GetName(const tjs_char ** name)
	{
		if(name) *name = TJS_N("multiripple");
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

		// time は必須
		tTJSVariant tmp;
		if(TJS_FAILED(options->GetValue(TJS_N("time"), &tmp))) return TJS_E_FAIL;
		if(tmp.Type() == tvtVoid) return TJS_E_FAIL;
		tjs_uint64 time = (tjs_int64)tmp;
		if(time < 2) time = 2;

		// 以降は任意 ( 既定値は元 DLL では x87 経由で不明瞭なため妥当値を採用 )
		tjs_int count     = 1;
		tjs_int wavecount = 2;
		tjs_int rwidth    = 32;
		tjs_int maxdrift  = 24;
		float   roundness = 1.0f;
		double  delaylast = 1.0;

		if(TJS_SUCCEEDED(options->GetValue(TJS_N("count"), &tmp)) && tmp.Type() != tvtVoid)
			count = (tjs_int)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("wavecount"), &tmp)) && tmp.Type() != tvtVoid)
			wavecount = (tjs_int)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("rwidth"), &tmp)) && tmp.Type() != tvtVoid)
			rwidth = (tjs_int)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("maxdrift"), &tmp)) && tmp.Type() != tvtVoid)
			maxdrift = (tjs_int)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("roundness"), &tmp)) && tmp.Type() != tvtVoid)
			roundness = (float)(double)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("delaylast"), &tmp)) && tmp.Type() != tvtVoid)
			delaylast = (double)tmp;

		if(roundness <= 0.0f) roundness = 1.0f;

		*handler = new tTVPMultiRippleTransHandler(time, src1w, src1h,
			count, rwidth, wavecount, maxdrift, roundness, delaylast);
		return TJS_S_OK;
	}

} static * MultiRippleTransHandlerProvider;
//---------------------------------------------------------------------------
void RegisterMultiRippleTransHandlerProvider()
{
	MultiRippleTransHandlerProvider = new tTVPMultiRippleTransHandlerProvider();
	TVPAddTransHandlerProvider(MultiRippleTransHandlerProvider);
}
//---------------------------------------------------------------------------
void UnregisterMultiRippleTransHandlerProvider()
{
	TVPRemoveTransHandlerProvider(MultiRippleTransHandlerProvider);
	MultiRippleTransHandlerProvider->Release();
}
//---------------------------------------------------------------------------
