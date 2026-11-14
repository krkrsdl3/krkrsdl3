#include "tjsCommHead.h"
#include "cpu_types.h"
#include "transhandler.h"
#include "tjsErrorDefs.h"
#include "TVPMsg.h"
#include "TVPTrans.h"
#include "tjsVariant.h"
#include "tjsConfig.h"
#include "flutter.h"
#include "common.h"

//---------------------------------------------------------------------------
// flutter トランジション  (作者: ヤマモト)
//   紙をめくりとる (めくり上げる) ようなトランジション。
//   画面を斜め (右下→左上) にめくり上げていき、めくれた紙の背面には
//   前面画像を 90°回転させた「裏模様」を描画し、めくれ目にハイライトと影を
//   付ける。周囲は終了時に slip 分だけ右下へずれる。
//
//   extNagano.dll (吉里吉里2 版) の tTVPFlutterTransHandler を Ghidra
//   デコンパイル結果から復元したもの。復元元 FUN_:
//     Provider::GetName          = FUN_10006040  (-> L"flutter")
//     Provider::Release          = FUN_10006060
//     Provider::StartTransition  = FUN_100061a0
//     Handler コンストラクタ     = FUN_100060e0
//     Handler::StartProcess      = FUN_100056a0
//     Handler::EndProcess        = FUN_100057a0
//     Handler::Process           = FUN_100058d0
//     裏バッファ生成 (Process 補助) = FUN_10005810
//     裏バッファ 1画素ブレンド     = FUN_10018af0  (= common.h Blend)
//     オプション読取 (int)         = FUN_100019c0 / FUN_10005f40
//     クランプ                     = FUN_10001ae0
//
//   オプション (StartTransition):
//     time  : 切り替えに要する時間 (ms)。既定なし (必須)。time<2 は 2。
//     back  : 裏面の色 0xAARRGGBB。既定 0。
//             裏バッファ = Blend(前面画素, (back|0xff000000), AA)。
//               AA=0x00 → 裏模様は前面模様 (を 90°回転したもの)。
//               AA=0xff → 裏模様は 0xffRRGGBB の一色。
//     alpha : 裏面の不透明度 0..255。既定 0xff。
//     slip  : 紙のズレ量。既定 8。[0, min(w,h)] にクランプ。
//---------------------------------------------------------------------------
class tTVPFlutterTransHandler : public iTVPDivisibleTransHandler
{
	tjs_int RefCount; // 参照カウンタ (+0x04)

protected:
	// メンバオフセットは元 DLL のものをコメントで併記
	tjs_uint64 StartTick;   // 開始 tick               (+0x08 / +0x0c)
	tjs_uint64 Time;        // 所要時間                (+0x10 / +0x14)
	tjs_uint64 CurElapsed;  // 現在の経過 (Time でクランプ) (+0x18 / +0x1c)
	tjs_int Width;          // 画像幅                  (+0x20)
	tjs_int Height;         // 画像高                  (+0x24)
	tjs_int Phase;          // めくれ量 (0..Width+Height) (+0x28)
	tjs_int CurSlip;        // 現在のズレ量 (0..Slip)  (+0x2c)
	tjs_int Slip;           // 最終ズレ量              (+0x30)
	tjs_int Alpha;          // 裏面の不透明度 0..255   (+0x34)
	tjs_uint32 BackColor;   // 裏面色 back|0xff000000  (+0x38)
	tjs_int BackAlpha;      // back の AA (裏模様混合比)(+0x3c)
	tjs_uint32 *BackBuf;    // 裏模様バッファ Width*Height (+0x40)
	bool First;             // 最初の呼び出しか        (+0x44)
	bool Second;            // 2 回目の呼び出しか      (+0x45)
	bool BackBuilt;         // 裏バッファ生成済みか    (+0x46)
	tjs_int FrameCount;     // フレーム数 (fps ログ用) (+0x48)

	void BuildBack(tTVPDivisibleData *data); // FUN_10005810

public:
	tTVPFlutterTransHandler(tjs_uint64 time, tjs_int width, tjs_int height,
		tjs_uint32 back, tjs_int alpha, tjs_int slip)
		: StartTick(0), Time(time), CurElapsed(0), Width(width), Height(height),
		  Phase(0), CurSlip(0), Slip(slip), Alpha(alpha & 0xff),
		  BackColor(back | 0xff000000), BackAlpha((back >> 24) & 0xff),
		  First(true), Second(true), BackBuilt(false), FrameCount(0)
	{
		RefCount = 1;
		BackBuf = new tjs_uint32[(size_t)width * height];
	}
	virtual ~tTVPFlutterTransHandler()
	{
		delete [] BackBuf;
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
		*dest = src2; // 最終画像は常に src2
		return TJS_S_OK;
	}
};
//---------------------------------------------------------------------------
// 裏模様バッファの生成 (FUN_10005810)
//   前面画像 (Src1) を 90°回転 (転置 + 上下反転) しつつ、各画素を
//   BackColor へ BackAlpha の比で寄せて格納する。
//   格納位置: BackBuf[ x*Height + (Height-1-y) ]  (転置)
//---------------------------------------------------------------------------
void tTVPFlutterTransHandler::BuildBack(tTVPDivisibleData *data)
{
	for(tjs_int y = 0; y < Height; y++)
	{
		const tjs_uint32 *s1;
		if(TJS_FAILED(data->Src1->GetScanLine(y, (const void**)&s1)))
			return;
		tjs_uint32 *col = BackBuf + (Height - 1 - y); // 転置先の縦位置
		for(tjs_int x = 0; x < Width; x++)
			col[x * Height] = Blend(s1[x], BackColor, BackAlpha);
	}
}
//---------------------------------------------------------------------------
tjs_error tTVPFlutterTransHandler::StartProcess(tjs_uint64 tick)
{
	FrameCount++;
	if(First)
	{
		// 最初の実行: StartTick を確定し、めくれ量を 0 に
		First = false;
		StartTick = tick;
		Phase = 0;
		CurSlip = 0;
	}
	else if(Second)
	{
		// 2 回目: 経過が最低 1 になるよう StartTick を 1 引く
		// (元 DLL FUN_100056a0 の挙動をそのまま再現)
		Second = false;
		StartTick = tick - 1;
	}

	tjs_uint64 elapsed = tick - StartTick;
	if(elapsed > Time) elapsed = Time; // Time でクランプ
	CurElapsed = elapsed;

	// めくれ量 Phase = (Width+Height) * (elapsed/Time)^2   (2乗のイーズイン)
	tjs_int L = Width + Height;
	tjs_uint64 t = (tjs_uint64)L * elapsed;
	t = t * elapsed;
	t = t / Time;
	t = t / Time;
	tjs_int phase = (tjs_int)t;
	if(phase > L) phase = L;
	Phase = phase;

	// ズレ量 CurSlip = Slip * (elapsed/Time)^3   (3乗)
	tjs_uint64 s = (tjs_uint64)Slip * elapsed;
	s = s * elapsed;
	s = s * elapsed;
	s = s / Time;
	s = s / Time;
	s = s / Time;
	CurSlip = (tjs_int)s;

	return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
tjs_error tTVPFlutterTransHandler::EndProcess()
{
	if(CurElapsed == Time) return TJS_S_FALSE; // トランジション終了
	return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
tjs_error tTVPFlutterTransHandler::Process(tTVPDivisibleData *data)
{
	tjs_int W = Width;
	tjs_int H = Height;
	tjs_int phase = Phase;

	if(phase == 0)
	{
		// まだめくれていない: 出力は Src1 のまま
		// (元 DLL は data->Dest を Src1 に差し替えるが、ここでは領域をコピー)
		if(!BackBuilt)
		{
			BackBuilt = true;
			BuildBack(data); // 裏模様バッファを一度だけ生成
		}
		for(tjs_int n = 0; n < data->Height; n++)
		{
			tjs_uint32 *dest;
			const tjs_uint32 *src1;
			if(TJS_FAILED(data->Dest->GetScanLineForWrite(data->DestTop + n, (void**)&dest)))
				return TJS_E_FAIL;
			if(TJS_FAILED(data->Src1->GetScanLine(data->Top + n, (const void**)&src1)))
				return TJS_E_FAIL;
			tjs_uint32 *dp = dest + data->DestLeft;
			const tjs_uint32 *sp = src1 + data->Left;
			for(tjs_int j = 0; j < data->Width; j++) dp[j] = sp[j];
		}
		return TJS_S_OK;
	}

	if(phase == W + H)
	{
		// 完全にめくり終わった: 出力は Src2
		for(tjs_int n = 0; n < data->Height; n++)
		{
			tjs_uint32 *dest;
			const tjs_uint32 *src2;
			if(TJS_FAILED(data->Dest->GetScanLineForWrite(data->DestTop + n, (void**)&dest)))
				return TJS_E_FAIL;
			if(TJS_FAILED(data->Src2->GetScanLine(data->Top + n, (const void**)&src2)))
				return TJS_E_FAIL;
			tjs_uint32 *dp = dest + data->DestLeft;
			const tjs_uint32 *sp = src2 + data->Left;
			for(tjs_int j = 0; j < data->Width; j++) dp[j] = sp[j];
		}
		return TJS_S_OK;
	}

	// --- めくれ中 (部分描画) -------------------------------------------------
	tjs_int CS   = CurSlip;
	tjs_int Left = data->Left;
	tjs_int Top  = data->Top;
	tjs_int RW   = data->Width;   // 領域幅
	tjs_int RH   = data->Height;  // 領域高

	// めくれ目 (斜め) の基準位置。負なら 0 にクランプ。
	tjs_int base       = H - phase - CS;          // 生の折り目位置
	tjs_int foldMid    = base + 8;                // ハイライト帯の開始
	tjs_int foldEnd    = foldMid + RW;            // 裏面帯の終端
	tjs_int baseClamp  = (base < 0) ? 0 : base;

	for(tjs_int n = 0; n < RH; n++)
	{
		tjs_int y = Top + n;

		// このラインで新画像 (Src2) が右から現れる境界
		tjs_int rbound = (H - Left) - y - phase + W;
		if(RW < rbound) rbound = RW;

		tjs_uint32 *destline;
		if(TJS_FAILED(data->Dest->GetScanLineForWrite(data->DestTop + n, (void**)&destline)))
			return TJS_E_FAIL;
		tjs_uint32 *dp = destline + data->DestLeft; // 領域先頭 (列 j でアクセス)

		// -- 下地の合成 (Src2 / ずらした Src1 / Src2) --
		if(y < CS)
		{
			// 上端の帯: 全て Src2 (ズレによりすでに新画像)
			const tjs_uint32 *s2;
			if(TJS_FAILED(data->Src2->GetScanLine(y, (const void**)&s2))) return TJS_E_FAIL;
			s2 += Left;
			for(tjs_int j = 0; j < RW; j++) dp[j] = s2[j];
		}
		else
		{
			const tjs_uint32 *s2;
			if(TJS_FAILED(data->Src2->GetScanLine(y, (const void**)&s2))) return TJS_E_FAIL;
			s2 += Left;

			tjs_int j = 0;
			tjs_int lim = CS; if(lim > RW) lim = RW;
			for(; j < lim; j++) dp[j] = s2[j];        // 左端 CS 列: Src2

			if(j < rbound)
			{
				// 中央: 旧画像 Src1 を (CS,CS) だけ右下にずらして転送
				const tjs_uint32 *s1;
				if(TJS_FAILED(data->Src1->GetScanLine(y - CS, (const void**)&s1))) return TJS_E_FAIL;
				s1 += Left;
				for(; j < rbound; j++) dp[j] = s1[j - CS];
			}
			for(; j < RW; j++) dp[j] = s2[j];         // 右側: 現れた Src2
		}

		// -- めくれ目の陰影 --
		//   帯1: baseClamp <  y <= foldMid  折り目根元の淡い影
		//   帯2: foldMid   <  y <= foldEnd  めくれた紙の裏面 + ハイライト + 影
		//   (元 DLL の goto 制御を等価な排他条件に整理。帯2 は baseClamp に
		//    依らず y が foldMid..foldEnd のとき実行される)
		if(y > baseClamp && y <= foldMid)
		{
			// 帯1: 折り目根元のうっすらした影 (黒へ寄せる)
			tjs_int col0 = (W - phase) + 12;
			if(col0 < 0) col0 = 0;
			tjs_int shade = (((y - baseClamp) * 63) / 8) & 0xff;
			for(tjs_int j = col0; j < rbound; j++)
				dp[j] = Blend(dp[j], 0xff000000, shade);
		}
		else if(y > foldMid && y <= foldEnd)
		{
				// 帯2: めくれた紙の裏面 + ハイライト + 影
				tjs_int local_c  = (H - CS) - 1;
				tjs_int colbase  = (W - phase) + 8;
				tjs_int istk24   = colbase;
				tjs_int backrow  = y - foldMid;         // 裏バッファの行 (= 元 x)
				if(backrow < 0) backrow = 0;
				if(backrow > W - 1) backrow = W - 1;
				tjs_int backbase = backrow * H - colbase; // BackBuf 上の基点
				tjs_int start    = colbase;
				if(colbase < 0)
				{
					istk24 = 0;
					local_c += colbase;
					start = 0;
				}
				tjs_int rb = rbound;
				if((local_c - rb) + start < 0) rb = local_c + start;

				// 裏面画像 (BackBuf) を Alpha で重ねる
				for(tjs_int j = start; j < rb; j++)
					dp[j] = Blend(dp[j], BackBuf[backbase + j], Alpha);

				// ハイライト立ち上がり (rb-24 .. rb-16) 白へ寄せる
				{
					tjs_int j = rb - 0x18;
					if(j < istk24) j = istk24;
					tjs_int end = rb - 0x10;
					tjs_int ratio = (j - end) * 0x7f;
					for(; j < end; j++)
					{
						tjs_int s = (ratio / 8) + 0x7f;
						dp[j] = Blend(dp[j], 0xffffffff, s);
						ratio += 0x7f;
					}
				}
				// ハイライト立ち下がり (rb-16 .. rb-8) 白へ寄せる
				{
					tjs_int j = rb - 0x10;
					if(j < istk24) j = istk24;
					tjs_int end = rb - 8;
					tjs_int ratio = (end - j) * 0x7f;
					for(; j < end; j++)
					{
						tjs_int s = ratio / 8;
						dp[j] = Blend(dp[j], 0xffffffff, s);
						ratio -= 0x7f;
					}
				}
				// めくれ端の影 (rb-8 .. rb) 黒へ寄せる
				{
					tjs_int j = istk24;
					if(j < rb - 8) j = rb - 8;
					tjs_int ratio = (j - rb) * 0xff;
					for(; j < rb; j++)
					{
						tjs_int s = (ratio / 8) + 0xff;
						dp[j] = Blend(dp[j], 0xff000000, s);
						ratio += 0xff;
					}
				}
		}
	}

	return TJS_S_OK;
}
//---------------------------------------------------------------------------
class tTVPFlutterTransHandlerProvider : public iTVPTransHandlerProvider
{
	tjs_uint RefCount;
public:
	tTVPFlutterTransHandlerProvider() { RefCount = 1; }
	~tTVPFlutterTransHandlerProvider() {}

	tjs_error AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }

	tjs_error GetName(const tjs_char ** name)
	{
		if(name) *name = TJS_N("flutter");
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

		// time (必須)
		tTJSVariant tmp;
		if(TJS_FAILED(options->GetValue(TJS_N("time"), &tmp))) return TJS_E_FAIL;
		if(tmp.Type() == tvtVoid) return TJS_E_FAIL;
		tjs_uint64 time = (tjs_int64)tmp;
		if(time < 2) time = 2;

		// back (既定 0), slip (既定 8), alpha (既定 0xff)
		tjs_uint32 back = 0;
		tjs_int slip = 8;
		tjs_int alpha = 0xff;
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("back"), &tmp)) && tmp.Type() != tvtVoid)
			back = (tjs_int)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("slip"), &tmp)) && tmp.Type() != tvtVoid)
			slip = (tjs_int)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_N("alpha"), &tmp)) && tmp.Type() != tvtVoid)
			alpha = (tjs_int)tmp;

		// slip を [0, min(w,h)] にクランプ
		tjs_int maxslip = (src1h <= src1w) ? (tjs_int)src1h : (tjs_int)src1w;
		if(slip < 0) slip = 0;
		if(slip > maxslip) slip = maxslip;

		*handler = new tTVPFlutterTransHandler(time, src1w, src1h, back, alpha, slip);
		return TJS_S_OK;
	}

} static * FlutterTransHandlerProvider;
//---------------------------------------------------------------------------
void RegisterFlutterTransHandlerProvider()
{
	FlutterTransHandlerProvider = new tTVPFlutterTransHandlerProvider();
	TVPAddTransHandlerProvider(FlutterTransHandlerProvider);
}
//---------------------------------------------------------------------------
void UnregisterFlutterTransHandlerProvider()
{
	TVPRemoveTransHandlerProvider(FlutterTransHandlerProvider);
	FlutterTransHandlerProvider->Release();
}
//---------------------------------------------------------------------------
