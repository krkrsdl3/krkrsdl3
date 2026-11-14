#include "tjsCommHead.h"
#include "cpu_types.h"
#include "transhandler.h"
#include "tjsErrorDefs.h"
#include "TVPMsg.h"
#include "TVPTrans.h"
#include "tjsVariant.h"
#include "tjsConfig.h"
#include "scanline.h"
#include "common.h"

//---------------------------------------------------------------------------
// scanline トランジション
//   スキャンラインごとに左右交互に画像をスライド (push) させて切り替える。
//   偶数ラインは src2 が左から押し込み、奇数ラインは右から押し込む。
//   (extNagano.dll: tTVPScanLineTransHandler の復元)
//---------------------------------------------------------------------------
class tTVPScanLineTransHandler : public iTVPDivisibleTransHandler
{
	tjs_int RefCount;

protected:
	tjs_uint64 StartTick; // 開始 tick
	tjs_uint64 Time;      // 所要時間
	tjs_int Width;        // 画像幅
	tjs_int Height;       // 画像高
	tjs_int CurPos;       // 現在のスライド量 (0..Width)
	tjs_int FrameCount;   // フレーム数 (fps ログ用)
	bool First;           // 最初の呼び出しか

public:
	tTVPScanLineTransHandler(tjs_uint64 time, tjs_int width, tjs_int height)
		: Time(time), Width(width), Height(height), CurPos(0), FrameCount(0), First(true)
	{
		RefCount = 1;
	}
	virtual ~tTVPScanLineTransHandler() {}

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
tjs_error tTVPScanLineTransHandler::StartProcess(tjs_uint64 tick)
{
	FrameCount++;
	if(First)
	{
		First = false;
		StartTick = tick;
	}
	tjs_uint64 elapsed = tick - StartTick;
	if(elapsed >= Time)
		CurPos = Width;
	else
		CurPos = (tjs_int)(Width * elapsed / Time);
	return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
tjs_error tTVPScanLineTransHandler::EndProcess()
{
	if(CurPos == Width) return TJS_S_FALSE; // 終了
	return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
tjs_error tTVPScanLineTransHandler::Process(tTVPDivisibleData *data)
{
	tjs_int pos = CurPos;
	tjs_int w   = Width;
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

		// dest の絶対列 c は dp[c] に書き込む
		tjs_uint32 *dp = dest + data->DestLeft - data->Left;

		if((y & 1) == 0)
		{
			// 偶数ライン: src2 を左から push、src1 は右へ押し出される
			for(tjs_int c = left; c < right; c++)
			{
				if(c < pos) dp[c] = src2[c + (w - pos)];
				else        dp[c] = src1[c - pos];
			}
		}
		else
		{
			// 奇数ライン: src2 を右から push、src1 は左へ押し出される
			for(tjs_int c = left; c < right; c++)
			{
				if(c < w - pos) dp[c] = src1[c + pos];
				else            dp[c] = src2[c - (w - pos)];
			}
		}
	}
	return TJS_S_OK;
}
//---------------------------------------------------------------------------
class tTVPScanLineTransHandlerProvider : public iTVPTransHandlerProvider
{
	tjs_uint RefCount;
public:
	tTVPScanLineTransHandlerProvider() { RefCount = 1; }
	~tTVPScanLineTransHandlerProvider() {}

	tjs_error AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }

	tjs_error GetName(const tjs_char ** name)
	{
		if(name) *name = TJS_N("scanline");
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

		tTJSVariant tmp;
		if(TJS_FAILED(options->GetValue(TJS_N("time"), &tmp))) return TJS_E_FAIL;
		if(tmp.Type() == tvtVoid) return TJS_E_FAIL;
		tjs_uint64 time = (tjs_int64)tmp;
		if(time < 2) time = 2;

		*handler = new tTVPScanLineTransHandler(time, src1w, src1h);
		return TJS_S_OK;
	}

} static * ScanLineTransHandlerProvider;
//---------------------------------------------------------------------------
void RegisterScanLineTransHandlerProvider()
{
	ScanLineTransHandlerProvider = new tTVPScanLineTransHandlerProvider();
	TVPAddTransHandlerProvider(ScanLineTransHandlerProvider);
}
//---------------------------------------------------------------------------
void UnregisterScanLineTransHandlerProvider()
{
	TVPRemoveTransHandlerProvider(ScanLineTransHandlerProvider);
	ScanLineTransHandlerProvider->Release();
}
//---------------------------------------------------------------------------
