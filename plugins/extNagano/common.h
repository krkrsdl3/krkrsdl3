#ifndef extNagano_commonH
#define extNagano_commonH

#include "tjsCommHead.h"
#include "cpu_types.h"
#include "transhandler.h"
#include "tjsErrorDefs.h"
#include "TVPMsg.h"
#include "TVPTrans.h"
#include "tjsVariant.h"
#include "tjsConfig.h"

//---------------------------------------------------------------------------
// extNagano 共通ヘルパ (extrans/common.h 由来 + 追加分)
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
static inline tjs_uint32 Blend(tjs_uint32 a, tjs_uint32 b, tjs_int opa)
{
	// a と b を混合比 opa で混合 ( opa = 0..255, 0 = a, 255 = b )
	tjs_uint32 ret;
	tjs_uint32 tmp;
	tmp = a & 0x000000ff;  ret   = 0x000000ff & (tmp + (( (b & 0x000000ff) - tmp ) * opa >> 8));
	tmp = a & 0x0000ff00;  ret  |= 0x0000ff00 & (tmp + (( (b & 0x0000ff00) - tmp ) * opa >> 8));
	tmp = a & 0x00ff0000;  ret  |= 0x00ff0000 & (tmp + (( (b & 0x00ff0000) - tmp ) * opa >> 8));
	tmp = a >> 24;
	ret  |= (0x000000ff & (tmp + (( (b >> 24) - tmp ) * opa >> 8))) << 24;
	return ret;
}
//---------------------------------------------------------------------------

#endif
