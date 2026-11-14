#include "tjsCommHead.h"
#include "cpu_types.h"
#include "transhandler.h"
#include "tjsErrorDefs.h"
#include "TVPMsg.h"
#include "TVPTrans.h"
#include "tjsVariant.h"
#include "tjsConfig.h"
#include "rgbfade.h"
#include "common.h"

//---------------------------------------------------------------------------
// rgbfade トランジション
//   RGB(+A) の色成分ごとにタイミングを変えてクロスフェードする。
//   delayR/G/B/A (各 0..255) で各成分のフェード開始を遅らせ、
//   遅延の大きい成分ほど遅れて混合が始まり、全成分が終了時刻に揃う。
//   (extNagano.dll: tTVPRGBFadeTransHandler の復元)
//
//   復元元 (Ghidra FUN_):
//     Provider::GetName          = FUN_10017480  (-> L"rgbfade")
//     Provider::StartTransition  = FUN_10017680  (time, delayR/G/B/A 読取+既定値)
//     Handler ctor               = FUN_10017520  (遅延->時間量/実効時間の算出)
//     Handler::StartProcess      = FUN_10017340  (成分ごとの不透明度算出)
//     Handler::EndProcess        = FUN_100170a0
//     Handler::Process           = FUN_10017110  (成分別 opa で合成)
//     Handler::MakeFinalImage    = FUN_10006e90  (*dest = src2)
//     AddRef=FUN_10007730, Release=FUN_10016840, SetOption=FUN_10016870
//     clamp ヘルパ FUN_10001ae0 = clamp(v, lo, hi)
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
static inline tjs_int ClampI(tjs_int v, tjs_int lo, tjs_int hi)
{
	// FUN_10001ae0 相当: v を [lo, hi] にクランプ
	if(v < lo) v = lo;
	if(v > hi) v = hi;
	return v;
}
//---------------------------------------------------------------------------
class tTVPRGBFadeTransHandler : public iTVPDivisibleTransHandler
{
	tjs_int RefCount;

protected:
	tjs_uint64 StartTick;    // 開始 tick               (+0x08)
	tjs_uint64 Time;         // 所要時間                (+0x10)
	tjs_uint64 CurElapsed;   // 現在の経過時間(0..Time) (+0x18)
	tjs_int Width;           // 画像幅 (未使用・保持のみ)(+0x20)
	tjs_int Height;          // 画像高 (未使用・保持のみ)(+0x24)
	tjs_int OpaR;            // R 成分の混合比 0..255   (+0x28)
	tjs_int OpaG;            // G 成分の混合比 0..255   (+0x2c)
	tjs_int OpaB;            // B 成分の混合比 0..255   (+0x30)
	tjs_int OpaA;            // A 成分の混合比 0..255   (+0x34)
	tjs_int64 DelayRTime;    // delayR*Time/255         (+0x38)
	tjs_int64 DelayGTime;    // delayG*Time/255         (+0x3c) ※元は32bit
	tjs_int64 DelayBTime;    // delayB*Time/255         (+0x40)
	tjs_int64 DelayATime;    // delayA*Time/255         (+0x44)
	tjs_int64 EffDuration;   // (255-maxDelay)*Time/255 (+0x48) 最低 1
	bool First;              // 最初の呼び出しか        (+0x4c)
	tjs_int FrameCount;      // フレーム数 (fps ログ用) (+0x50)

public:
	tTVPRGBFadeTransHandler(tjs_uint64 time, tjs_int width, tjs_int height,
			tjs_int delayR, tjs_int delayG, tjs_int delayB, tjs_int delayA)
		: StartTick(0), Time(time), CurElapsed(0), Width(width), Height(height),
		  OpaR(0), OpaG(0), OpaB(0), OpaA(0), First(true), FrameCount(0)
	{
		RefCount = 1;

		// 各成分の遅延 [0..255] を時間量へ換算 (constructor FUN_10017520)
		DelayRTime = (tjs_int64)delayR * (tjs_int64)time / 255;
		DelayGTime = (tjs_int64)delayG * (tjs_int64)time / 255;
		DelayBTime = (tjs_int64)delayB * (tjs_int64)time / 255;
		DelayATime = (tjs_int64)delayA * (tjs_int64)time / 255;

		// 全成分中の最大遅延を求め、実効フェード時間を算出する。
		// (最も遅い成分が終了時刻ちょうどに混合完了するようスケールする)
		tjs_int maxDelay = delayR;
		if(delayG > maxDelay) maxDelay = delayG;
		if(delayB > maxDelay) maxDelay = delayB;
		if(delayA > maxDelay) maxDelay = delayA;

		EffDuration = (tjs_int64)(255 - maxDelay) * (tjs_int64)time / 255;
		if(EffDuration < 1) EffDuration = 1;
	}
	virtual ~tTVPRGBFadeTransHandler() {}

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
tjs_error tTVPRGBFadeTransHandler::StartProcess(tjs_uint64 tick)
{
	// FUN_10017340
	FrameCount++;
	if(First)
	{
		First = false;
		StartTick = tick;
	}

	tjs_uint64 elapsed = tick - StartTick;
	if(elapsed > Time) elapsed = Time;
	CurElapsed = elapsed;

	// 各成分の不透明度: (elapsed - 遅延時間) * 255 / 実効時間 を [0,255] にクランプ。
	// 遅延の大きい成分ほど負値になる期間が長く、フェード開始が遅れる。
	tjs_int64 e = (tjs_int64)elapsed;
	OpaR = ClampI((tjs_int)(((e - DelayRTime) * 255) / EffDuration), 0, 255);
	OpaG = ClampI((tjs_int)(((e - DelayGTime) * 255) / EffDuration), 0, 255);
	OpaB = ClampI((tjs_int)(((e - DelayBTime) * 255) / EffDuration), 0, 255);
	OpaA = ClampI((tjs_int)(((e - DelayATime) * 255) / EffDuration), 0, 255);

	return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
tjs_error tTVPRGBFadeTransHandler::EndProcess()
{
	// FUN_100170a0: 経過時間が所要時間に達したら終了
	if(CurElapsed == Time) return TJS_S_FALSE;
	return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
tjs_error tTVPRGBFadeTransHandler::Process(tTVPDivisibleData *data)
{
	// FUN_10017110
	tjs_int left  = data->Left;
	tjs_int right = data->Left + data->Width;

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

		// dest の絶対列 c は dp[c] に書き込む (scanline.cpp と同じ基点)
		tjs_uint32 *dp = dest + data->DestLeft - data->Left;

		if(CurElapsed == 0)
		{
			// 開始直後: src1 をそのまま出力
			for(tjs_int c = left; c < right; c++) dp[c] = src1[c];
			continue;
		}
		if(CurElapsed == Time)
		{
			// 完了: src2 をそのまま出力
			for(tjs_int c = left; c < right; c++) dp[c] = src2[c];
			continue;
		}

		const tjs_int opaR = OpaR;
		const tjs_int opaG = OpaG;
		const tjs_int opaB = OpaB;
		const tjs_int opaA = OpaA;

		for(tjs_int c = left; c < right; c++)
		{
			tjs_uint32 s1 = src1[c];
			tjs_uint32 s2 = src2[c];

			tjs_int s1b =  s1        & 0xff;
			tjs_int s1g = (s1 >> 8)  & 0xff;
			tjs_int s1r = (s1 >> 16) & 0xff;
			tjs_int s1a = (s1 >> 24) & 0xff;
			tjs_int s2b =  s2        & 0xff;
			tjs_int s2g = (s2 >> 8)  & 0xff;
			tjs_int s2r = (s2 >> 16) & 0xff;
			tjs_int s2a = (s2 >> 24) & 0xff;

			tjs_int db, dg, dr, da;

			if(s2a == 0)
			{
				// src2 が完全透明の画素: src1 のアルファをフェードアウトさせる。
				// ※RGB は元DLL通り「成分 * opa」(下位8bit)。プリマルチプライ透明時の
				//   縮退処理と思われ通常の不透明トランジションでは通らない。要確認。
				db = (s1b * opaB) & 0xff;
				dg = (s1g * opaG) & 0xff;
				dr = (s1r * opaR) & 0xff;
				da = s1a - ((s1a * opaA) >> 8);
			}
			else if(s1a == 0)
			{
				// src1 が完全透明の画素: src2 のアルファをフェードインさせる。
				// ※RGB は上と同様、元DLL通りの「成分 * opa」。要確認。
				db = (s1b * opaB) & 0xff;
				dg = (s1g * opaG) & 0xff;
				dr = (s1r * opaR) & 0xff;
				da = (s2a * opaA) >> 8;
			}
			else
			{
				// 通常経路: 成分ごとに別 opa で src1->src2 を線形補間
				db = s1b + (((s2b - s1b) * opaB) >> 8);
				dg = s1g + (((s2g - s1g) * opaG) >> 8);
				dr = s1r + (((s2r - s1r) * opaR) >> 8);
				da = s1a + (((s2a - s1a) * opaA) >> 8);
			}

			dp[c] = ((tjs_uint32)(da & 0xff) << 24) |
			        ((tjs_uint32)(dr & 0xff) << 16) |
			        ((tjs_uint32)(dg & 0xff) << 8)  |
			         (tjs_uint32)(db & 0xff);
		}
	}
	return TJS_S_OK;
}
//---------------------------------------------------------------------------
class tTVPRGBFadeTransHandlerProvider : public iTVPTransHandlerProvider
{
	tjs_uint RefCount;
public:
	tTVPRGBFadeTransHandlerProvider() { RefCount = 1; }
	~tTVPRGBFadeTransHandlerProvider() {}

	tjs_error AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }

	tjs_error GetName(const tjs_char ** name)
	{
		if(name) *name = TJS_N("rgbfade");
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
		// FUN_10017680
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

		// delayR/G/B/A は任意 (既定 0)。各 [0..255] にクランプ。
		tjs_int delayR = 0, delayG = 0, delayB = 0, delayA = 0;
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("delayR"), &tmp)) && tmp.Type() != tvtVoid)
			delayR = ClampI((tjs_int)(tjs_int64)tmp, 0, 255);
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("delayG"), &tmp)) && tmp.Type() != tvtVoid)
			delayG = ClampI((tjs_int)(tjs_int64)tmp, 0, 255);
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("delayB"), &tmp)) && tmp.Type() != tvtVoid)
			delayB = ClampI((tjs_int)(tjs_int64)tmp, 0, 255);
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("delayA"), &tmp)) && tmp.Type() != tvtVoid)
			delayA = ClampI((tjs_int)(tjs_int64)tmp, 0, 255);

		*handler = new tTVPRGBFadeTransHandler(time, src1w, src1h,
				delayR, delayG, delayB, delayA);
		return TJS_S_OK;
	}

} static * RGBFadeTransHandlerProvider;
//---------------------------------------------------------------------------
void RegisterRGBFadeTransHandlerProvider()
{
	RGBFadeTransHandlerProvider = new tTVPRGBFadeTransHandlerProvider();
	TVPAddTransHandlerProvider(RGBFadeTransHandlerProvider);
}
//---------------------------------------------------------------------------
void UnregisterRGBFadeTransHandlerProvider()
{
	TVPRemoveTransHandlerProvider(RGBFadeTransHandlerProvider);
	RGBFadeTransHandlerProvider->Release();
}
//---------------------------------------------------------------------------
