#include "tjsCommHead.h"
#include "cpu_types.h"
#include "transhandler.h"
#include "tjsErrorDefs.h"
#include "TVPMsg.h"
#include "TVPTrans.h"
#include "tjsVariant.h"
#include "tjsConfig.h"
#include "3duniversal.h"
#include "common.h"
#include <math.h>
#include <string.h>

//---------------------------------------------------------------------------
// 3duniversal トランジション
//   ルール画像(RGB あるいは HSB)の各画素値を「移動開始時間・移動スピード・
//   移動方向」として使い、変化前(src1)/変化後(src2)の画像の画素を個別に
//   移動させながら切り替える、extNagano 中で最も複雑なトランジション。
//
//   ・変化前画像の画素移動量は時間の 2 次式で決まる:
//        t = 255 * 経過時間 / time
//        移動量 = ( a1 * t^2 / 2 + s1 * t ) * (画素毎の移動スピード)
//     変化後画像も同形の式(原作ドキュメントでは "T.B.D" と明記)。
//   ・ルール画像各成分の意味(原バイナリ準拠。後述の注意も参照):
//        RGB: R=移動開始時間 / G=移動スピード / B=移動方向
//        HSB: H=(方向) / S=移動スピード / B(明度)=(開始時間) ※ドキュメントとは
//             H と B の役割が入れ替わっている。詳細は LoadRulePixelHSB のコメント参照。
//        移動方向: 0-255 で一周。0:→ / 63:↓ / 127:← / 191:↑
//
//   復元元 (Ghidra FUN_):
//     Provider::GetName          = FUN_10001ac0  (-> L"3duniversal")
//     Provider::StartTransition  = FUN_100026e0  (全オプション+別名の読取, rule取得, type分岐)
//     Handler ctor / init        = FUN_10002390  (メンバ確保, 三角/移動量テーブル生成)
//     Handler::StartProcess      = FUN_100010e0  (phase 算出, 不透明度テーブル生成, dirty 判定)
//     Handler::EndProcess        = FUN_100012b0  (phase==255 で終了)
//     Handler::Process           = FUN_100015f0  (dirty 時に OutBuf 再構築 + 領域転送)
//     Handler::MakeFinalImage    = FUN_10006e90  (*dest = src2)
//     AddRef=FUN_10007730, Release=FUN_10016840, SetOption=FUN_10016870(no-op)
//     ルール画素→OutBuf 散布      = FUN_10001380
//     ルール画像→RuleBuf 取込     = FUN_100014d0 (RGBそのまま / HSB は FUN_10001000 で変換)
//     src そのまま OutBuf コピー   = FUN_10001310
//     RGB->HSB 変換               = FUN_10001000
//     色ブレンド                  = FUN_10018af0 (lerp)
//     rule 取得ヘルパ             = FUN_100022e0 / tLayerScanLineProvider(FUN_10001b10 他)
//     double/int オプション取得   = FUN_100018b0 / FUN_100019c0 (既定値付き)
//     0-127 クランプ              = FUN_10001ae0
//
//   ■ 復元の忠実度メモ (objdump 逆アセンブルで再検証済み) -------------------
//   当初は x87 FPU 中間値が追えず 2 点を近似としていたが、objdump で
//   FUN_10002390(ctor) / FUN_100010e0(StartProcess) / FUN_10001380(Scatter) を
//   直接トレースし、大半を確定した:
//     ・不透明度: OpaTableA/B の値は StartProcess のバイト完全一致で確定。
//       Scatter が引くインデクスも byte2(=開始時間 start) と確定 → 旧「近似」は
//       結果的に忠実だった (OpaTable[start])。
//     ・移動量 MoveTable[t]: bound==0 では round(0.5*accel*t^2 + speed*t) と
//       ctor の x87 に完全一致 (t はテーブル添字そのもの)。
//     ・方向テーブル: cos/sin に固定小数スケール 1024 (_DAT_1002b300) を掛けて
//       丸めるのが原仕様。旧実装はこの ×1024 を落としており、これが唯一の実質
//       バグ(パーツが動かず一瞬で切替に見える原因)だった → 修正済み。
//   残る近似点は【bound>0(ハネ返り)時の MoveTable の折り返し波形】のみ。原は
//   [0,bound)/[bound,255) の 2 区間で別式を持つが、ここは剰余近似のまま
//   (BuildMoveTable のコメント参照)。>>19 スケール・3 通り合成分岐・領域転送は忠実。
//---------------------------------------------------------------------------

//===========================================================================
// ルール画像スキャンライン供給ヘルパ (原: tLayerScanLineProvider)
//   原バイナリは rule に指定されたレイヤ/画像パスをラップして GetScanLine で
//   ルール画素を供給していた。ここでは移植性のため、レイヤ/画像パスいずれの
//   場合も一旦 ARGB バッファへスナップショットし、それを保持する自己完結の
//   iTVPScanLineProvider として実装する(このファイル内に静的にとどめる)。
//===========================================================================
class tRuleScanLineProvider : public iTVPScanLineProvider
{
	tjs_int RefCount;
	tjs_int W, H;
	tjs_uint32 *Buf; // 所有 (W*H の ARGB)
public:
	tRuleScanLineProvider(tjs_int w, tjs_int h)
		: RefCount(1), W(w), H(h), Buf(0)
	{
		if(W > 0 && H > 0) Buf = (tjs_uint32*)malloc(sizeof(tjs_uint32) * W * H);
	}
	~tRuleScanLineProvider() { if(Buf) free(Buf); }

	tjs_uint32 * GetBuffer() { return Buf; }
	tjs_int GetW() const { return W; }
	tjs_int GetH() const { return H; }

	tjs_error AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }

	tjs_error GetWidth(tjs_int *width)   { if(width)  *width  = W; return TJS_S_OK; }
	tjs_error GetHeight(tjs_int *height) { if(height) *height = H; return TJS_S_OK; }
	tjs_error GetPixelFormat(tjs_int *bpp) { if(bpp) *bpp = 32; return TJS_S_OK; } // 原 FUN_10001e10 = 0x20
	tjs_error GetPitchBytes(tjs_int *pitch) { if(pitch) *pitch = W * 4; return TJS_S_OK; }

	tjs_error GetScanLine(tjs_int line, const void ** scanline)
	{
		if(!Buf || !scanline) return TJS_E_FAIL;
		if(line < 0) line = 0; else if(line >= H) line = H - 1;
		*scanline = Buf + line * W;
		return TJS_S_OK;
	}
	tjs_error GetScanLineForWrite(tjs_int line, void ** scanline)
	{
		if(!Buf || !scanline) return TJS_E_FAIL;
		if(line < 0) line = 0; else if(line >= H) line = H - 1;
		*scanline = Buf + line * W;
		return TJS_S_OK;
	}
	iTVPTexture2D * GetTexture() { return 0; }
	iTVPTexture2D * GetTextureForRender() { return 0; }
};
//---------------------------------------------------------------------------
// rule オプションからルール画像プロバイダを生成する (原 FUN_100026e0 の rule 分岐 +
// FUN_100022e0 / imagepro->LoadImage を統合)。
//   ・rule がオブジェクト(Layer / Bitmap) の場合:
//       mainImageBuffer 等のプロパティ経由で画素をスナップショット。
//       (原 tLayerScanLineProvider が width/height/mainImageBuffer/pitch を読んでいた)
//   ・rule が文字列(画像パス) の場合:
//       imagepro->LoadImage で読み込み、スナップショット。
//   dw,dh は転送画像サイズ(不足時のフォールバック用)。
//---------------------------------------------------------------------------
static tjs_error CreateRuleProvider(iTVPSimpleOptionProvider *options,
		iTVPSimpleImageProvider *imagepro, tjs_int dw, tjs_int dh,
		tRuleScanLineProvider **out)
{
	*out = 0;
	if(!options) return TJS_E_FAIL;

	tTJSVariant rule;
	if(TJS_FAILED(options->GetValue(TJS_N("rule"), &rule))) return TJS_E_FAIL;
	if(rule.Type() == tvtVoid) return TJS_E_FAIL;

	if(rule.Type() == tvtObject)
	{
		// --- レイヤ/ビットマップオブジェクト -------------------------------
		iTJSDispatch2 *obj = rule.AsObjectNoAddRef();
		if(!obj) return TJS_E_FAIL;

		tTJSVariant val;
		tjs_int rw = 0, rh = 0, pitch = 0;
		const tjs_uint8 *buffer = 0;
		if(TJS_FAILED(obj->PropGet(0, TJS_N("imageWidth"), 0, &val, obj))) return TJS_E_FAIL;
		rw = (tjs_int)val;
		if(TJS_FAILED(obj->PropGet(0, TJS_N("imageHeight"), 0, &val, obj))) return TJS_E_FAIL;
		rh = (tjs_int)val;
		if(TJS_FAILED(obj->PropGet(0, TJS_N("mainImageBufferPitch"), 0, &val, obj))) return TJS_E_FAIL;
		pitch = (tjs_int)val;
		if(TJS_FAILED(obj->PropGet(0, TJS_N("mainImageBuffer"), 0, &val, obj))) return TJS_E_FAIL;
		buffer = reinterpret_cast<const tjs_uint8 *>(
			static_cast<tjs_intptr_t>((tjs_int64)val));
		if(rw <= 0 || rh <= 0 || !buffer) return TJS_E_FAIL;

		tRuleScanLineProvider *p = new tRuleScanLineProvider(rw, rh);
		if(!p->GetBuffer()) { p->Release(); return TJS_E_FAIL; }
		tjs_uint32 *dst = p->GetBuffer();
		for(tjs_int y = 0; y < rh; y++)
		{
			const tjs_uint32 *src = reinterpret_cast<const tjs_uint32 *>(buffer + y * pitch);
			memcpy(dst + y * rw, src, sizeof(tjs_uint32) * rw);
		}
		*out = p;
		return TJS_S_OK;
	}
	else
	{
		// --- 画像パス文字列 ------------------------------------------------
		// 原バイナリは options->GetAsString("rule") + imagepro->LoadImage を使用。
		const tjs_char *name = 0;
		if(TJS_FAILED(options->GetAsString(TJS_N("rule"), &name)) || !name)
			return TJS_E_FAIL;
		if(!imagepro) return TJS_E_FAIL;

		iTVPScanLineProvider *loaded = 0;
		// key=0x02ffffff (カラーキー無効), w/h=0 で元サイズ読み込み
		if(TJS_FAILED(imagepro->LoadImage(name, 32, 0x02ffffff, 0, 0, &loaded)) || !loaded)
			return TJS_E_FAIL;

		tjs_int rw = 0, rh = 0;
		loaded->GetWidth(&rw);
		loaded->GetHeight(&rh);
		if(rw <= 0 || rh <= 0) { loaded->Release(); return TJS_E_FAIL; }

		tRuleScanLineProvider *p = new tRuleScanLineProvider(rw, rh);
		if(!p->GetBuffer()) { p->Release(); loaded->Release(); return TJS_E_FAIL; }
		tjs_uint32 *dst = p->GetBuffer();
		for(tjs_int y = 0; y < rh; y++)
		{
			const void *src = 0;
			if(TJS_SUCCEEDED(loaded->GetScanLine(y, &src)) && src)
				memcpy(dst + y * rw, src, sizeof(tjs_uint32) * rw);
			else
				memset(dst + y * rw, 0, sizeof(tjs_uint32) * rw);
		}
		loaded->Release();
		*out = p;
		return TJS_S_OK;
	}
}
//---------------------------------------------------------------------------
// RGB -> HSB(ルール画素) 変換 (原 FUN_10001000)
//   返す ARGB のバイト配置を、RGB 時と同じ [0]=方向 / [1]=スピード /
//   [2]=開始時間 になるよう詰める。
//   原バイナリの詰め方は:
//     byte0 = Hue(0-255)         -> 方向インデクス
//     byte1 = Saturation*2(clamp) -> スピード
//     byte2 = 255 - V(明度)       -> 開始時間
//   ※ ドキュメントは「H=開始時間 / B(明度)=方向」と記すが、実バイナリは上記の
//     とおり Hue が方向・(255-明度)が開始時間に割り当たっている(役割が逆)。
//     ここではバイナリ(=実挙動)に合わせる。
//---------------------------------------------------------------------------
static inline tjs_uint32 LoadRulePixelHSB(tjs_uint32 argb)
{
	tjs_int R = (argb >> 16) & 0xff;
	tjs_int G = (argb >> 8) & 0xff;
	tjs_int B = argb & 0xff;

	tjs_int mx = R; if(G > mx) mx = G; if(B > mx) mx = B;
	tjs_int mn = R; if(G < mn) mn = G; if(B < mn) mn = B;
	tjs_int chroma = mx - mn;

	// Hue (0-360 相当を整数演算で) → 0-255 へ正規化
	tjs_int hue = 0;
	if(chroma != 0)
	{
		tjs_int h6; // 0-6 セクタ
		if(G == mx)      h6 = (B - R) * 60 / chroma + 120;
		else if(B == mx) h6 = (R - G) * 60 / chroma + 240;
		else if(G < B)   h6 = (G - B) * 60 / chroma + 360;
		else             h6 = (G - B) * 60 / chroma;       // R==mx, G>=B
		// h6 は 0-360 度。0-255 へ
		if(h6 < 0) h6 += 360;
		hue = h6 * 255 / 360;
		if(hue > 255) hue = 255;
	}
	// Saturation
	tjs_int sat = (mx == 0) ? 0 : (chroma * 255 / mx);
	sat = sat * 2; if(sat > 255) sat = 255;
	// Brightness (原コードは 255 - V を開始時間に使う)
	tjs_int inv_v = 255 - mx;

	// byte0=方向(Hue) / byte1=スピード(Sat) / byte2=開始時間(255-V)
	return (tjs_uint32)hue | ((tjs_uint32)sat << 8) | ((tjs_uint32)inv_v << 16);
}
//---------------------------------------------------------------------------
static inline tjs_int ClampI(tjs_int v, tjs_int lo, tjs_int hi)
{
	// 原 FUN_10001ae0 相当 (0-0x7f クランプに使用)
	if(v < lo) v = lo;
	if(v > hi) v = hi;
	return v;
}
//---------------------------------------------------------------------------
static inline tjs_int LRound(double x)
{
	return (tjs_int)floor(x + 0.5);
}
//===========================================================================
// ハンドラ本体
//===========================================================================
class tTVP3duniversalTransHandler : public iTVPDivisibleTransHandler
{
	tjs_int RefCount;

protected:
	tjs_uint64 StartTick;   // 開始 tick
	tjs_uint64 Time;        // 所要時間
	tjs_int Width;          // 画像幅
	tjs_int Height;         // 画像高
	tjs_uint32 *OutBuf;     // 作業用画像 (Width*Height)                     (原 +0x28)
	tjs_uint32 *RuleBuf;    // 分解済ルール画素 (Width*Height, [0]dir/[1]spd/[2]start) (原 +0x834)
	tjs_int Phase;          // 経過率 0..255                                 (原 +0x2c)
	tjs_int PrevPhase;      // 前回 Phase (dirty 判定用)                     (原 +0x30)
	bool First;             // 最初の StartProcess か                        (原 +0x838)
	bool Dirty;             // OutBuf 再構築が必要か                         (原 +0x839)
	tjs_int FrameCount;     // フレーム数 (fps ログ用)                       (原 +0x1840)

	bool Src1Static;        // 変化前画像が静止 (accel1==0 && speed1==0)     (原 +0x60f)
	bool Src2Static;        // 変化後画像が静止 (accel2==0 && speed2==0)     (原 +0x183d)
	double Accel1, Speed1;  // 変化前ベース加速度/スピード
	double Accel2, Speed2;  // 変化後ベース加速度/スピード
	tjs_int Bound1, Bound2; // ハネっ返り時間 (0-127)

	tjs_int CosTable[256];  // 方向インデクス→dx 符号 (round(cos))          (原 +0x83c)
	tjs_int SinTable[256];  // 方向インデクス→dy 符号 (round(sin))          (原 +0xc3c)
	tjs_int MoveTable1[256];// 変化前の移動量テーブル(t=0..255)             (原 +0x103c)
	tjs_int MoveTable2[256];// 変化後の移動量テーブル(t=0..255)             (原 +0x143c)
	tjs_int OpaTableA[256]; // 変化前の不透明度テーブル(StartProcess で更新) (原 +0x34)
	tjs_int OpaTableB[256]; // 変化後の不透明度テーブル                       (原 +0x434)

public:
	tTVP3duniversalTransHandler(tjs_uint64 time, tjs_int width, tjs_int height,
			tRuleScanLineProvider *rule, bool hsb,
			double accel1, double speed1, tjs_int bound1,
			double accel2, double speed2, tjs_int bound2)
		: RefCount(1), StartTick(0), Time(time), Width(width), Height(height),
		  OutBuf(0), RuleBuf(0), Phase(0), PrevPhase(0), First(true), Dirty(true),
		  FrameCount(0),
		  Accel1(accel1), Speed1(speed1), Accel2(accel2), Speed2(speed2),
		  Bound1(ClampI(bound1, 0, 0x7f)), Bound2(ClampI(bound2, 0, 0x7f))
	{
		// FUN_10002390
		Src1Static = (accel1 == 0.0 && speed1 == 0.0);
		Src2Static = (accel2 == 0.0 && speed2 == 0.0);

		if(Width > 0 && Height > 0)
		{
			OutBuf  = (tjs_uint32*)malloc(sizeof(tjs_uint32) * Width * Height);
			RuleBuf = (tjs_uint32*)malloc(sizeof(tjs_uint32) * Width * Height);
		}

		// --- ルール画像を RuleBuf へ取り込む (FUN_100014d0) ----------------
		if(RuleBuf && rule && rule->GetBuffer())
		{
			tjs_int rw = rule->GetW();
			tjs_int rh = rule->GetH();
			const tjs_uint32 *rbuf = rule->GetBuffer();
			for(tjs_int y = 0; y < Height; y++)
			{
				tjs_int sy = (y < rh) ? y : (rh - 1);
				const tjs_uint32 *src = rbuf + sy * rw;
				tjs_uint32 *dst = RuleBuf + y * Width;
				for(tjs_int x = 0; x < Width; x++)
				{
					tjs_int sx = (x < rw) ? x : (rw - 1);
					if(hsb) dst[x] = LoadRulePixelHSB(src[sx]); // HSB へ変換
					else    dst[x] = src[sx];                   // RGB はそのまま
				}
			}
		}

		// --- 方向テーブル (FUN_10002390 前段) ------------------------------
		//   angle = i * 2π / 255。cos/sin に固定小数スケール 1024 を掛けて丸める。
		//   ※原バイナリの逆アセンブル(objdump)で確定: 定数 _DAT_1002b300 = 1024.0
		//     を cos/sin に乗じてから丸めている(FUN_10002390 の fmul QWORD 0x1002b300)。
		//     旧実装はこの ×1024 を落として {-1,0,1} に量子化していたため、
		//     移動量 dx=(Cos*spd*mv)>>19 が約 1/1024 になり、パーツがほとんど動かず
		//     (数px)「一瞬で切り替わる」ように見えていた。1024 倍が正しい(>>19 と対)。
		const double DENOM = 255.0;          // 原 _DAT_1002b308
		const double TWO_PI = 6.283185307179586; // 原 _DAT_1002b310 = 2π
		const double TRIG_SCALE = 1024.0;    // 原 _DAT_1002b300 (固定小数 1<<10)
		for(tjs_int i = 0; i < 256; i++)
		{
			double angle = (double)i * TWO_PI / DENOM;
			CosTable[i] = LRound(cos(angle) * TRIG_SCALE); // dx 成分(固定小数×1024)
			SinTable[i] = LRound(sin(angle) * TRIG_SCALE); // dy 成分(固定小数×1024)
		}

		// --- 移動量テーブル (FUN_10002390 後段) ----------------------------
		BuildMoveTable(MoveTable1, Accel1, Speed1, Bound1);
		BuildMoveTable(MoveTable2, Accel2, Speed2, Bound2);

		for(tjs_int i = 0; i < 256; i++) { OpaTableA[i] = 0; OpaTableB[i] = 0; }
	}

	virtual ~tTVP3duniversalTransHandler()
	{
		if(OutBuf)  free(OutBuf);
		if(RuleBuf) free(RuleBuf);
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
		*dest = src2; // 原 FUN_10006e90
		return TJS_S_OK;
	}

private:
	void BuildMoveTable(tjs_int *table, double accel, double speed, tjs_int bound);
	void CopySrcToOut(iTVPScanLineProvider *src);
	void ScatterMove(iTVPScanLineProvider *src, tjs_int phase,
			const tjs_int *moveTable, const tjs_int *opaTable);
};
//---------------------------------------------------------------------------
// 移動量テーブル生成 (FUN_10002390 後段の 2 次式)
//   MoveTable[t] = round( accel * te^2 / 2 + speed * te )
//   ドキュメントの式:「移動量 = a * t^2 / 2 + s * t」に忠実。
//   bound(ハネっ返り時間, 0-127) は原コードで剰余 `t % (255 - bound)` として
//   現れるため、有効時間 te を周期 (255-bound) で折り返す近似とした。
//   ※【簡略化点(1)】原バイナリの厳密な係数スケール/折り返し波形は x87 中間値
//     が復元できず不明。ここはドキュメント式 + 剰余での近似実装である。
//   実際の画素移動量は FUN_10001380 側で
//       dx = (CosTable[dir] * ルールスピード(0-255) * MoveTable[t]) >> 19
//   と >>19 でスケールされる(この >>19 はバイナリに忠実)。
//---------------------------------------------------------------------------
void tTVP3duniversalTransHandler::BuildMoveTable(tjs_int *table, double accel, double speed, tjs_int bound)
{
	tjs_int period = 255 - bound;
	if(period < 1) period = 1;
	for(tjs_int t = 0; t < 256; t++)
	{
		tjs_int te = (bound > 0) ? (t % period) : t;
		double v = accel * (double)te * (double)te / 2.0 + speed * (double)te;
		table[t] = LRound(v);
	}
}
//---------------------------------------------------------------------------
// src 画像をそのまま OutBuf にコピー (静止側の背景描画, 原 FUN_10001310)
//---------------------------------------------------------------------------
void tTVP3duniversalTransHandler::CopySrcToOut(iTVPScanLineProvider *src)
{
	if(!OutBuf || !src) return;
	for(tjs_int y = 0; y < Height; y++)
	{
		const void *p = 0;
		if(TJS_FAILED(src->GetScanLine(y, &p)) || !p) continue;
		memcpy(OutBuf + y * Width, p, sizeof(tjs_uint32) * Width);
	}
}
//---------------------------------------------------------------------------
// ルール画素に従い src の画素を移動させて OutBuf に散布 (原 FUN_10001380)
//   ・rule[start] > phase の画素は「まだ動き始めていない」ので、その位置に
//     そのままコピー(原: (phase - start) < 0 の分岐)。
//   ・動き始めた画素は方向/スピード/移動量から (dx,dy) を求め、移動先へ
//     不透明度付きで合成する。
//   ・不透明度は【簡略化点(2)】: 原コードは StartProcess 生成の不透明度
//     テーブル(opaTable)を参照するがインデクスが復元できないため、
//     「開始時間で決まるフェードイン」opa = opaTable[start] を採用した。
//---------------------------------------------------------------------------
void tTVP3duniversalTransHandler::ScatterMove(iTVPScanLineProvider *src, tjs_int phase,
		const tjs_int *moveTable, const tjs_int *opaTable)
{
	if(!OutBuf || !RuleBuf || !src) return;

	for(tjs_int y = 0; y < Height; y++)
	{
		const tjs_uint32 *sp = 0;
		if(TJS_FAILED(src->GetScanLine(y, (const void**)&sp)) || !sp) continue;
		const tjs_uint32 *rp = RuleBuf + y * Width;

		for(tjs_int x = 0; x < Width; x++)
		{
			tjs_uint32 rpix = rp[x];
			tjs_int dir   = rpix & 0xff;          // 方向
			tjs_int spd   = (rpix >> 8) & 0xff;   // スピード
			tjs_int start = (rpix >> 16) & 0xff;  // 開始時間

			if(phase - start < 0)
			{
				// 開始前: その場に配置
				OutBuf[y * Width + x] = sp[x];
				continue;
			}

			tjs_int mv = moveTable[phase - start];        // 移動量(2次式)
			// dx,dy: CosTable/SinTable は {-1,0,1}。>>19 は原コードに忠実。
			tjs_int dx = (CosTable[dir] * spd * mv) >> 19;
			tjs_int dy = (SinTable[dir] * spd * mv) >> 19;

			tjs_int nx = x + dx;
			tjs_int ny = y + dy;
			if(nx < 0 || nx >= Width || ny < 0 || ny >= Height) continue;

			// フェードイン不透明度 (0..255)
			tjs_int opa = opaTable[start];
			if(opa < 0) opa = 0; else if(opa > 255) opa = 255;

			// 移動先の既存画素(背景 or クリア)に対して合成
			tjs_uint32 *dstp = &OutBuf[ny * Width + nx];
			*dstp = Blend(*dstp, sp[x], opa); // common.h Blend: opa=0->背景, 255->移動画素
		}
	}
}
//---------------------------------------------------------------------------
tjs_error tTVP3duniversalTransHandler::StartProcess(tjs_uint64 tick)
{
	// FUN_100010e0
	FrameCount++;
	if(First)
	{
		First = false;
		StartTick = tick;
	}

	tjs_uint64 elapsed = tick - StartTick;
	tjs_int phase;
	if(elapsed >= Time) phase = 0xff;
	else                phase = (tjs_int)(elapsed * 0xff / Time);
	Phase = phase;

	if(PrevPhase != Phase)
	{
		Dirty = true;      // phase が変化したので OutBuf 再構築が必要
		PrevPhase = Phase;
	}

	// 不透明度テーブル生成 (原 FUN_100010e0 の +0x34 / +0x434 テーブル)
	//   OpaTableA[j] = clamp>=0( (phase - j) * 255 / (256 - j) )
	//   OpaTableB[j] = clamp>=0( ((255 - phase) - j) * 255 / (256 - j) )
	//   j でゆるやかにフェードするカーブ。ScatterMove では start をインデクスに使う。
	for(tjs_int j = 0; j < 256; j++)
	{
		tjs_int a = (phase - j) * 0xff / (256 - j);
		if(a < 0) a = 0;
		OpaTableA[j] = a;
		tjs_int b = ((0xff - phase) - j) * 0xff / (256 - j);
		if(b < 0) b = 0;
		OpaTableB[j] = b;
	}

	return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
tjs_error tTVP3duniversalTransHandler::EndProcess()
{
	// FUN_100012b0: phase==255 で終了
	if(Phase == 0xff) return TJS_S_FALSE;
	return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
tjs_error tTVP3duniversalTransHandler::Process(tTVPDivisibleData *data)
{
	// FUN_100015f0
	if(!OutBuf) return TJS_E_FAIL;

	// --- dirty 時に OutBuf 全体を再構築 --------------------------------------
	if(Dirty)
	{
		if(Src2Static)
		{
			// 変化後(src2)が静止 → src2 を背景にし、src1 を動かして上に散布
			CopySrcToOut(data->Src2);
			ScatterMove(data->Src1, Phase, MoveTable1, OpaTableA);
		}
		else if(Src1Static)
		{
			// 変化前(src1)が静止 → src1 を背景にし、src2 を動かして上に散布
			CopySrcToOut(data->Src1);
			ScatterMove(data->Src2, 0xff - Phase, MoveTable2, OpaTableB);
		}
		else
		{
			// 両方動く → クリアしてから src1, src2 の順に散布
			memset(OutBuf, 0, sizeof(tjs_uint32) * Width * Height);
			ScatterMove(data->Src1, Phase, MoveTable1, OpaTableA);
			ScatterMove(data->Src2, 0xff - Phase, MoveTable2, OpaTableB);
		}
		Dirty = false;
	}

	// --- 要求領域を OutBuf から Dest へ転送 ---------------------------------
	tjs_int left  = data->Left;
	tjs_int width = data->Width;
	for(tjs_int n = 0; n < data->Height; n++)
	{
		tjs_int srcY = data->Top + n;
		tjs_uint32 *dest;
		if(TJS_FAILED(data->Dest->GetScanLineForWrite(data->DestTop + n, (void**)&dest)))
			return TJS_E_FAIL;

		// scanline.cpp/zoomfade.cpp と同じ基点: dest + DestLeft - Left, 絶対列参照
		tjs_uint32 *dp = dest + data->DestLeft - data->Left;
		const tjs_uint32 *op = OutBuf + srcY * Width;
		for(tjs_int c = left; c < left + width; c++)
			dp[c] = op[c];
	}
	return TJS_S_OK;
}
//---------------------------------------------------------------------------
class tTVP3duniversalTransHandlerProvider : public iTVPTransHandlerProvider
{
	tjs_uint RefCount;
public:
	tTVP3duniversalTransHandlerProvider() { RefCount = 1; }
	~tTVP3duniversalTransHandlerProvider() {}

	tjs_error AddRef()  { RefCount++; return TJS_S_OK; }
	tjs_error Release() { if(RefCount == 1) delete this; else RefCount--; return TJS_S_OK; }

	tjs_error GetName(const tjs_char ** name)
	{
		if(name) *name = TJS_N("3duniversal"); // 原 FUN_10001ac0
		return TJS_S_OK;
	}

	// オプションを double で取得(既定値付き)。primary が無ければ alias、
	// どちらも無ければ def。 原 FUN_100018b0 + 別名読取ロジック相当。
	static double GetOptDouble(iTVPSimpleOptionProvider *opt,
			const tjs_char *primary, const tjs_char *alias, double def)
	{
		double v = def;
		tTJSVariant tmp;
		if(TJS_SUCCEEDED(opt->GetValue(primary, &tmp)) && tmp.Type() != tvtVoid)
			v = (double)(tTVReal)tmp;
		// alias は primary の後に読み、存在すれば上書き(原コードの読み順に一致)
		if(alias && TJS_SUCCEEDED(opt->GetValue(alias, &tmp)) && tmp.Type() != tvtVoid)
			v = (double)(tTVReal)tmp;
		return v;
	}
	static tjs_int GetOptInt(iTVPSimpleOptionProvider *opt, const tjs_char *name, tjs_int def)
	{
		tTJSVariant tmp;
		if(TJS_SUCCEEDED(opt->GetValue(name, &tmp)) && tmp.Type() != tvtVoid)
			return (tjs_int)(tjs_int64)tmp;
		return def;
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
		// FUN_100026e0
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

		// type: "HSB" のとき HSB、それ以外(既定)は RGB
		bool hsb = false;
		const tjs_char *typestr = 0;
		if(TJS_SUCCEEDED(options->GetAsString(TJS_N("type"), &typestr)) && typestr)
		{
			if(TJS_strcmp(typestr, TJS_N("HSB")) == 0) hsb = true;
		}

		// ベーススピード/加速度/ハネ返り (別名: speed1/s1, accel1/a1, ...)
		double accel1 = GetOptDouble(options, TJS_N("accel1"), TJS_N("a1"), 0.0);
		double speed1 = GetOptDouble(options, TJS_N("speed1"), TJS_N("s1"), 0.0);
		double accel2 = GetOptDouble(options, TJS_N("accel2"), TJS_N("a2"), 0.0);
		double speed2 = GetOptDouble(options, TJS_N("speed2"), TJS_N("s2"), 0.0);
		tjs_int bound1 = GetOptInt(options, TJS_N("bound1"), 0);
		tjs_int bound2 = GetOptInt(options, TJS_N("bound2"), 0);

		// rule 画像プロバイダ生成 (パス文字列 or レイヤ/ビットマップオブジェクト)
		tRuleScanLineProvider *rule = 0;
		if(TJS_FAILED(CreateRuleProvider(options, imagepro, (tjs_int)src1w, (tjs_int)src1h, &rule)))
			return TJS_E_FAIL;

		*handler = new tTVP3duniversalTransHandler(time, src1w, src1h, rule, hsb,
				accel1, speed1, bound1, accel2, speed2, bound2);

		// ルール画素は RuleBuf にスナップショット済み。プロバイダは解放してよい。
		if(rule) rule->Release();
		return TJS_S_OK;
	}

} static * TDUniversalTransHandlerProvider;
//---------------------------------------------------------------------------
void Register3duniversalTransHandlerProvider()
{
	TDUniversalTransHandlerProvider = new tTVP3duniversalTransHandlerProvider();
	TVPAddTransHandlerProvider(TDUniversalTransHandlerProvider);
}
//---------------------------------------------------------------------------
void Unregister3duniversalTransHandlerProvider()
{
	TVPRemoveTransHandlerProvider(TDUniversalTransHandlerProvider);
	TDUniversalTransHandlerProvider->Release();
}
//---------------------------------------------------------------------------
