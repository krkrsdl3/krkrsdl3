#include "tjsCommHead.h"

#include "TVPGraphicsLoader.h"
#include "LayerBitmap.h"
#include "TVPStorage.h"
#include "TVPMsg.h"

#include "TVPDebug.h"
#include "tjsDictionary.h"
#include "TVPScript.h"

static tjs_uint32 GetStride( const tjs_uint32 width, const tjs_uint32 bitCount) {
	const tjs_uint32 byteCount = bitCount / 8;
	const tjs_uint32 stride = (width * byteCount + 3) & ~3;
	return stride;
}

bool TVPAcceptSaveAsJXR(void* formatdata, const ttstr & type, class iTJSDispatch2** dic ) {
	bool result = false;
	if( type.StartsWith(TJS_W("jxr")) ) result = true;
	else if( type == TJS_W(".jxr") ) result = true;
	if( result && dic ) {
		tTJSVariant result;
		TVPExecuteExpression(
			TJS_W("(const)%[")
			TJS_W("\"quality\"=>(const)%[\"type\"=>\"range\",\"min\"=>1,\"max\"=>255,\"desc\"=>\"1 is lossless, 2 - 255 lossy : 2 is high quality, 255 is low quality\",\"default\"=>1],")
			TJS_W("\"alphaQuality\"=>(const)%[\"type\"=>\"range\",\"min\"=>1,\"max\"=>255,\"desc\"=>\"1 is lossless, 2 - 255 lossy : 2 is high quality, 255 is low quality\",\"default\"=>1],")
			TJS_W("\"subsampling\"=>(const)%[\"type\"=>\"select\",\"items\"=>(const)[\"4:0:0\",\"4:2:0\",\"4:2:2\",\"4:4:4\"],\"desc\"=>\"subsampling\",\"default\"=>1],")
			TJS_W("\"alpha\"=>(const)%[\"type\"=>\"boolean\",\"desc\"=>\"alpha channel\",\"default\"=>true]")
			TJS_W("]"),
			NULL, &result );
		if( result.Type() == tvtObject ) {
			*dic = result.AsObject();
		}
	}
	return result;
}
// jxrlib を使うバージョンでは書き込みがうまくできていない

#ifndef INITGUID
#define FAR
#endif
#include <jxrlib/JXRGlue.h>

/*
jxrlibで読み込みを行う場合は
external/jxrlib/image/vc11projects/CommonLib_vc11.vcxproj
external/jxrlib/image/vc11projects/DecodeLib_vc11.vcxproj
external/jxrlib/image/vc11projects/EncodeLib_vc11.vcxproj
external/jxrlib/jxrgluelib/JXRGlueLib_vc11.vcxproj
をソリューションに加えてビルドすること
*/

static ERR JXR_close( WMPStream** ppWS ) {
	/* 何もしない */
	return WMP_errSuccess;
}
static Bool JXR_EOS(struct WMPStream* pWS) {
	tTJSBinaryStream* src = ((tTJSBinaryStream*)(pWS->state.pvObj));
	return src->GetPosition() >= src->GetSize();
}
static ERR JXR_read(struct WMPStream* pWS, void* pv, size_t cb) {
	tTJSBinaryStream* src = ((tTJSBinaryStream*)(pWS->state.pvObj));
	tjs_uint size = src->Read( pv, (tjs_uint)cb );
	return size == cb ? WMP_errSuccess : WMP_errFileIO;
}
static ERR JXR_write(struct WMPStream* pWS, const void* pv, size_t cb) {
	ERR err = WMP_errSuccess;
	if( 0 != cb ) {
		tTJSBinaryStream* src = ((tTJSBinaryStream*)(pWS->state.pvObj));
		tjs_uint size = src->Write( pv, (tjs_uint)cb );
		err = size == cb ? WMP_errSuccess : WMP_errFileIO;
	}
	return err;
}
static ERR JXR_set_pos( struct WMPStream* pWS, size_t offPos ) {
	tTJSBinaryStream* src = ((tTJSBinaryStream*)(pWS->state.pvObj));
	tjs_uint64 pos = src->Seek(  offPos, TJS_BS_SEEK_SET );
	return pos == offPos ? WMP_errSuccess : WMP_errFileIO;
}
static ERR JXR_get_pos( struct WMPStream* pWS, size_t* poffPos ) {
	tTJSBinaryStream* src = ((tTJSBinaryStream*)(pWS->state.pvObj));
	*poffPos = (size_t)src->GetPosition();
	return WMP_errSuccess;
}


#define SAFE_CALL( func ) if( Failed(err = (func)) ) { TVPThrowExceptionMessage( TJS_W("JPEG XR read error/%1"), err ); }
//---------------------------------------------------------------------------
void TVPLoadJXR(void* formatdata, void *callbackdata, tTVPGraphicSizeCallback sizecallback,
	tTVPGraphicScanLineCallback scanlinecallback, tTVPMetaInfoPushCallback metainfopushcallback,
	tTJSBinaryStream *src, tjs_int keyidx,  tTVPGraphicLoadMode mode)
{
	if( glmNormal != mode ) {
		// not supprted yet.
		TVPThrowExceptionMessage( TJS_W("JPEG XR read error/%1"), TJS_W("not supprted yet.") );
	}
	
	//PKFactory* pFactory = NULL;
	PKImageDecode* pDecoder = NULL;
	//PKFormatConverter* pConverter = NULL;
	WMPStream* pStream = NULL;
	//PKCodecFactory* pCodecFactory = NULL;
	try {
		ERR err;
		//SAFE_CALL( PKCreateFactory(&pFactory, PK_SDK_VERSION) );
		//SAFE_CALL( PKCreateCodecFactory(&pCodecFactory, WMP_SDK_VERSION) );
		//SAFE_CALL(pCodecFactory->CreateDecoderFromFile("test.jxr", &pDecoder));

		const PKIID* pIID = NULL;
		SAFE_CALL( GetImageDecodeIID(".jxr", &pIID) );
		// create stream
		pStream = (WMPStream*)calloc(1, sizeof(WMPStream));
		pStream->state.pvObj = (void*)src;
		pStream->Close = JXR_close;
		pStream->EOS = JXR_EOS;
		pStream->Read = JXR_read;
		pStream->Write = JXR_write;
		pStream->SetPos = JXR_set_pos;
		pStream->GetPos = JXR_get_pos;
		// Create decoder
		SAFE_CALL( PKCodecFactory_CreateCodec(pIID, (void **)&pDecoder) );
		// attach stream to decoder
		SAFE_CALL( pDecoder->Initialize(pDecoder, pStream) );
		pDecoder->fStreamOwner = !0;

		PKPixelFormatGUID srcFormat;
		pDecoder->GetPixelFormat( pDecoder, &srcFormat );
		PKPixelInfo PI;
		PI.pGUIDPixFmt = &srcFormat;
		PixelFormatLookup(&PI, LOOKUP_FORWARD);

		pDecoder->WMP.wmiSCP.bfBitstreamFormat = SPATIAL;
        if(!!(PI.grBit & PK_pixfmtHasAlpha))
            pDecoder->WMP.wmiSCP.uAlphaMode = 2;
        else
            pDecoder->WMP.wmiSCP.uAlphaMode = 0;
		pDecoder->WMP.wmiSCP.sbSubband = SB_ALL;
		pDecoder->WMP.bIgnoreOverlap = FALSE;
		pDecoder->WMP.wmiI.cfColorFormat = PI.cfColorFormat;
		pDecoder->WMP.wmiI.bdBitDepth = PI.bdBitDepth;
		pDecoder->WMP.wmiI.cBitsPerUnit = PI.cbitUnit;
		pDecoder->WMP.wmiI.cThumbnailWidth = pDecoder->WMP.wmiI.cWidth;
		pDecoder->WMP.wmiI.cThumbnailHeight = pDecoder->WMP.wmiI.cHeight;
		pDecoder->WMP.wmiI.bSkipFlexbits = FALSE;
		pDecoder->WMP.wmiI.cROILeftX = 0;
		pDecoder->WMP.wmiI.cROITopY = 0;
		pDecoder->WMP.wmiI.cROIWidth = pDecoder->WMP.wmiI.cThumbnailWidth;
		pDecoder->WMP.wmiI.cROIHeight = pDecoder->WMP.wmiI.cThumbnailHeight;
		pDecoder->WMP.wmiI.oOrientation = O_NONE;
		pDecoder->WMP.wmiI.cPostProcStrength = 0;
		pDecoder->WMP.wmiSCP.bVerbose = FALSE;

		int width = 0;
		int height = 0;
		pDecoder->GetSize( pDecoder, &width, &height );
		if( width == 0 || height == 0 ) {
			TVPThrowExceptionMessage( TJS_W("JPEG XR read error/%1"), TJS_W("width or height is zero.") );
		}
		sizecallback(callbackdata, width, height, pDecoder->WMP.wmiSCP.uAlphaMode ? gpfRGBA : gpfRGB);
		const tjs_uint32 stride = GetStride( (tjs_uint32)width, (tjs_uint32)32 );
		PKRect rect = {0, 0, width, height};
#ifdef _DEBUG
		std::vector<tjs_uint8> buff(stride*height*sizeof(tjs_uint8));
#else
		std::vector<tjs_uint8> buff;
		buff.reserve(stride*height*sizeof(tjs_uint8));
#endif
		// rect で1ラインずつ指定してデコードする方法はjxrlibではうまくいかない様子
		int offset = 0;
		if( !IsEqualGUID( srcFormat, GUID_PKPixelFormat32bppRGBA ) ) {
			if( IsEqualGUID( srcFormat, GUID_PKPixelFormat24bppRGB ) ) {
				pDecoder->Copy( pDecoder, &rect, (U8*)&buff[0], stride );
				for( int i = 0; i < height; i++) {
					void *scanline = scanlinecallback(callbackdata, i);
					tjs_uint8* d = (tjs_uint8*)scanline;
					tjs_uint8* s = (tjs_uint8*)&buff[offset];
					for( int x = 0; x < width; x++ ) {
						d[0] = s[2];
						d[1] = s[1];
						d[2] = s[0];
						d[3] = 0xff;
						d+=4;
						s+=3;
					}
					offset += stride;
					scanlinecallback(callbackdata, -1);
				}
			} else {
				TVPThrowExceptionMessage( TJS_W("JPEG XR read error/%1"), TJS_W("Not supported this file format yet.") );
			}
		} else {
			// アルファチャンネルが入っている時メモリリークしている(jxrlib直した)
			pDecoder->Copy( pDecoder, &rect, (U8*)&buff[0], stride );
			for( int i = 0; i < height; i++) {
				void *scanline = scanlinecallback(callbackdata, i);
				memcpy( scanline, &buff[offset], width*sizeof(tjs_uint32));
				offset += stride;
				scanlinecallback(callbackdata, -1);
			} 
		}
		//if( pConverter ) pConverter->Release(&pConverter);
		if( pDecoder ) pDecoder->Release(&pDecoder);
		//if( pCodecFactory ) pCodecFactory->Release(&pCodecFactory);
		if( pStream ) free( pStream );
	} catch(...) {
		//if( pConverter ) pConverter->Release(&pConverter);
		if( pDecoder ) pDecoder->Release(&pDecoder);
		//if( pCodecFactory ) pCodecFactory->Release(&pCodecFactory);
		if( pStream ) free( pStream );
		throw;
	}
}

// Y, U, V, YHP, UHP, VHP
static int DPK_QPS_420[12][6] = {      // for 8 bit only
    { 66, 65, 70, 72, 72, 77 },
    { 59, 58, 63, 64, 63, 68 },
    { 52, 51, 57, 56, 56, 61 },
    { 48, 48, 54, 51, 50, 55 },
    { 43, 44, 48, 46, 46, 49 },
    { 37, 37, 42, 38, 38, 43 },
    { 26, 28, 31, 27, 28, 31 },
    { 16, 17, 22, 16, 17, 21 },
    { 10, 11, 13, 10, 10, 13 },
    {  5,  5,  6,  5,  5,  6 },
    {  2,  2,  3,  2,  2,  2 }
};
static int DPK_QPS_8[12][6] = {
    { 67, 79, 86, 72, 90, 98 },
    { 59, 74, 80, 64, 83, 89 },
    { 53, 68, 75, 57, 76, 83 },
    { 49, 64, 71, 53, 70, 77 },
    { 45, 60, 67, 48, 67, 74 },
    { 40, 56, 62, 42, 59, 66 },
    { 33, 49, 55, 35, 51, 58 },
    { 27, 44, 49, 28, 45, 50 },
    { 20, 36, 42, 20, 38, 44 },
    { 13, 27, 34, 13, 28, 34 },
    {  7, 17, 21,  8, 17, 21 }, // Photoshop 100%
    {  2,  5,  6,  2,  5,  6 }
};
// TODO : 以下の処理ではうまく書き込めていない。どうも設定が不味い様子
void TVPSaveAsJXR(void* formatdata, tTJSBinaryStream* dst, const iTVPBaseBitmap* image, const ttstr & mode, class iTJSDispatch2* meta )
{
	PKImageEncode* pEncoder = NULL;
	WMPStream* pStream = NULL;
	try {
		ERR err;

		const PKIID* pIID = NULL;
		SAFE_CALL( GetImageEncodeIID(".jxr", &pIID) );
		// create stream
		pStream = (WMPStream*)calloc(1, sizeof(WMPStream));
		pStream->state.pvObj = (void*)dst;
		pStream->Close = JXR_close;
		pStream->EOS = JXR_EOS;
		pStream->Read = JXR_read;
		pStream->Write = JXR_write;
		pStream->SetPos = JXR_set_pos;
		pStream->GetPos = JXR_get_pos;
		// Create encoder
		SAFE_CALL( PKCodecFactory_CreateCodec(pIID, (void **)&pEncoder) );
		
		const tjs_uint width = image->GetWidth();
		const tjs_uint height = image->GetHeight();
		const tjs_uint stride = width * sizeof(tjs_uint32);
		const tjs_uint buffersize = stride * height;
		CWMIStrCodecParam wmiSCP;
		
		wmiSCP.bVerbose = FALSE;
		wmiSCP.cfColorFormat = YUV_444;	// Y_ONLY / YUV_420 / YUV_422 / YUV_444
		wmiSCP.bdBitDepth = BD_SHORT; // BD_LONG;
		//wmiSCP.bfBitstreamFormat = SPATIAL;
		wmiSCP.bfBitstreamFormat = FREQUENCY;
		//wmiSCP.uAlphaMode = 2;	// alpha + color
		wmiSCP.uAlphaMode = 0;	// color

		wmiSCP.bProgressiveMode = TRUE;
		wmiSCP.olOverlap = OL_ONE;
		wmiSCP.cNumOfSliceMinus1H = wmiSCP.cNumOfSliceMinus1V = 0;
		wmiSCP.sbSubband = SB_ALL;

		wmiSCP.uiDefaultQPIndex = 1;
		wmiSCP.uiDefaultQPIndexAlpha = 1;
		
		U32 uTileY = 1 * MB_HEIGHT_PIXEL;
        wmiSCP.cNumOfSliceMinus1H = (U32)height < (uTileY >> 1) ? 0 : (height + (uTileY >> 1)) / uTileY - 1;
		U32 uTileX = 1 * MB_HEIGHT_PIXEL;
		wmiSCP.cNumOfSliceMinus1V = (U32)width < (uTileX >> 1) ? 0 : (width + (uTileX >> 1)) / uTileX - 1;

		// attach stream to encoder
		SAFE_CALL( pEncoder->Initialize(pEncoder, pStream, &wmiSCP, sizeof(wmiSCP)) );
		
		/*
		float quality = 0.99f;
		int qi;
		float qf;
		qi = (int) (10.f * quality);
        qf = 10.f * quality - (float) qi;
		int* pQPs = (pEncoder->WMP.wmiSCP.cfColorFormat == YUV_420 || pEncoder->WMP.wmiSCP.cfColorFormat == YUV_422) ? DPK_QPS_420[qi] : DPK_QPS_8[qi];
		pEncoder->WMP.wmiSCP.uiDefaultQPIndex = (U8) (0.5f + (float) pQPs[0] * (1.f - qf) + (float) (pQPs + 6)[0] * qf);
		pEncoder->WMP.wmiSCP.uiDefaultQPIndexU = (U8) (0.5f + (float) pQPs[1] * (1.f - qf) + (float) (pQPs + 6)[1] * qf);
		pEncoder->WMP.wmiSCP.uiDefaultQPIndexV = (U8) (0.5f + (float) pQPs[2] * (1.f - qf) + (float) (pQPs + 6)[2] * qf);
		pEncoder->WMP.wmiSCP.uiDefaultQPIndexYHP = (U8) (0.5f + (float) pQPs[3] * (1.f - qf) + (float) (pQPs + 6)[3] * qf);
		pEncoder->WMP.wmiSCP.uiDefaultQPIndexUHP = (U8) (0.5f + (float) pQPs[4] * (1.f - qf) + (float) (pQPs + 6)[4] * qf);
		pEncoder->WMP.wmiSCP.uiDefaultQPIndexVHP = (U8) (0.5f + (float) pQPs[5] * (1.f - qf) + (float) (pQPs + 6)[5] * qf);
		*/
		
		pEncoder->WMP.wmiSCP.uiDefaultQPIndex = 1;
		if(pEncoder->WMP.wmiSCP.uAlphaMode == 2)
			pEncoder->WMP.wmiSCP_Alpha.uiDefaultQPIndex = wmiSCP.uiDefaultQPIndexAlpha;

		pEncoder->WMP.wmiSCP.olOverlap = OL_ONE;

		// 以下でアルファの設定か
		// pEncoder->WMP.wmiI_Alpha;
        // pEncoder->WMP.wmiSCP_Alpha;
        pEncoder->SetPixelFormat(pEncoder, GUID_PKPixelFormat32bppRGB);
		pEncoder->SetSize(pEncoder, image->GetWidth(), image->GetHeight() );
		//Float rX = 98.0, rY = 98.0;
		//pEncoder->SetResolution(pEncoder, rX, rY);
		
#ifdef _DEBUG
		std::vector<tjs_uint8> buff(buffersize);
#else
		std::vector<tjs_uint8> buff;
		buff.reserve(buffersize);
#endif
		for(tjs_uint i = 0; i < height; i++ ) {
			memcpy( &buff[i*stride], image->GetScanLine(i), stride );
		}
		pEncoder->WritePixels( pEncoder, height, &buff[0], stride );

		pEncoder->Release(&pEncoder);
		if( pStream ) free( pStream );
	} catch(...) {
		if( pEncoder ) pEncoder->Release(&pEncoder);
		if( pStream ) free( pStream );
		throw;
	}
}
void TVPLoadHeaderJXR(void* formatdata, tTJSBinaryStream *src, iTJSDispatch2** dic )
{
	PKImageDecode* pDecoder = NULL;
	WMPStream* pStream = NULL;
	try {
		ERR err;

		const PKIID* pIID = NULL;
		SAFE_CALL( GetImageDecodeIID(".jxr", &pIID) );
		// create stream
		pStream = (WMPStream*)calloc(1, sizeof(WMPStream));
		pStream->state.pvObj = (void*)src;
		pStream->Close = JXR_close;
		pStream->EOS = JXR_EOS;
		pStream->Read = JXR_read;
		pStream->Write = JXR_write;
		pStream->SetPos = JXR_set_pos;
		pStream->GetPos = JXR_get_pos;
		// Create decoder
		SAFE_CALL( PKCodecFactory_CreateCodec(pIID, (void **)&pDecoder) );
		// attach stream to decoder
		SAFE_CALL( pDecoder->Initialize(pDecoder, pStream) );
		pDecoder->fStreamOwner = !0;

		PKPixelFormatGUID srcFormat;
		pDecoder->GetPixelFormat( pDecoder, &srcFormat );
		PKPixelInfo PI;
		PI.pGUIDPixFmt = &srcFormat;
		PixelFormatLookup(&PI, LOOKUP_FORWARD);

		pDecoder->WMP.wmiSCP.bfBitstreamFormat = SPATIAL;
        if(!!(PI.grBit & PK_pixfmtHasAlpha))
            pDecoder->WMP.wmiSCP.uAlphaMode = 2;
        else
            pDecoder->WMP.wmiSCP.uAlphaMode = 0;
		pDecoder->WMP.wmiSCP.sbSubband = SB_ALL;
		pDecoder->WMP.bIgnoreOverlap = FALSE;
		pDecoder->WMP.wmiI.cfColorFormat = PI.cfColorFormat;
		pDecoder->WMP.wmiI.bdBitDepth = PI.bdBitDepth;
		pDecoder->WMP.wmiI.cBitsPerUnit = PI.cbitUnit;
		pDecoder->WMP.wmiI.cThumbnailWidth = pDecoder->WMP.wmiI.cWidth;
		pDecoder->WMP.wmiI.cThumbnailHeight = pDecoder->WMP.wmiI.cHeight;
		pDecoder->WMP.wmiI.bSkipFlexbits = FALSE;
		pDecoder->WMP.wmiI.cROILeftX = 0;
		pDecoder->WMP.wmiI.cROITopY = 0;
		pDecoder->WMP.wmiI.cROIWidth = pDecoder->WMP.wmiI.cThumbnailWidth;
		pDecoder->WMP.wmiI.cROIHeight = pDecoder->WMP.wmiI.cThumbnailHeight;
		pDecoder->WMP.wmiI.oOrientation = O_NONE;
		pDecoder->WMP.wmiI.cPostProcStrength = 0;
		pDecoder->WMP.wmiSCP.bVerbose = FALSE;

		int width = 0;
		int height = 0;
		pDecoder->GetSize( pDecoder, &width, &height );
	
		*dic = TJSCreateDictionaryObject();
		tTJSVariant val(width);
		(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("width"), 0, &val, (*dic) );
		val = tTJSVariant(height);
		(*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("height"), 0, &val, (*dic) );

		if( pDecoder ) pDecoder->Release(&pDecoder);
		if( pStream ) free( pStream );
	} catch(...) {
		if( pDecoder ) pDecoder->Release(&pDecoder);
		if( pStream ) free( pStream );
		throw;
	}
}