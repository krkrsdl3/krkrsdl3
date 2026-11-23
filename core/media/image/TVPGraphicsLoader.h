//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Graphics Loader ( loads graphic format from storage )
//---------------------------------------------------------------------------

#ifndef GraphicsLoaderIntfH
#define GraphicsLoaderIntfH

#include <vector>

class tTVPBaseBitmap;
namespace TJS
{
class tTJSBinaryStream;
}

enum tTVPGraphicPixelFormat
{
    gpfLuminance,
    gpfPalette,
    gpfRGB,
    gpfRGBA
};

/*[*/
//---------------------------------------------------------------------------
// Graphic Loading Handler Type
//---------------------------------------------------------------------------
typedef int(*tTVPGraphicSizeCallback) // return line pitch
    (void* callbackdata, tjs_uint w, tjs_uint h, tTVPGraphicPixelFormat fmt);
/*
        callback type to inform the image's size.
        call this once before TVPGraphicScanLineCallback.
*/

typedef void* (*tTVPGraphicScanLineCallback)(void* callbackdata, tjs_int y);
/*
        callback type to ask the scanline buffer for the decoded image, per a line.
        returning null can stop the processing.

        passing of y=-1 notifies the scan line image had been written to the buffer that
        was given by previous calling of TVPGraphicScanLineCallback. in this time,
        this callback function must return NULL.
*/

typedef const void* (*tTVPGraphicSaveScanLineCallback)(void* callbackdata, tjs_int y);

typedef void (*tTVPMetaInfoPushCallback)(void* callbackdata, const ttstr& name, const ttstr& value);
/*
        callback type to push meta-information of the image.
        this can be null.
*/

enum tTVPGraphicLoadMode
{
    glmNormal,     // normal, ie. 32bit ARGB graphic
    glmPalettized, // palettized 8bit mode
    glmGrayscale   // grayscale 8bit mode
};
/*]*/

/*[*/
typedef bool (*tTVPGraphicQuickTestHandler)(tTJSBinaryStream* src);
/*]*/

typedef void (*tTVPGraphicLoadingHandler)(void* formatdata,
                                          void* callbackdata,
                                          tTVPGraphicSizeCallback sizecallback,
                                          tTVPGraphicScanLineCallback scanlinecallback,
                                          tTVPMetaInfoPushCallback metainfopushcallback,
                                          tTJSBinaryStream* src,
                                          tjs_int32 keyidx,
                                          tTVPGraphicLoadMode mode);
/*
        format = format specific data given at TVPRegisterGraphicLoadingHandler
        dest = destination callback function
        src = source stream
        keyidx = color key for less than or equal to 8 bit image
        mode = if glmPalettized, the output image must be an 8bit color (for province
           image. so the color is not important. color index must be preserved).
           if glmGrayscale, the output image must be an 8bit grayscale image.
           otherwise the output image must be a 32bit full-color with opacity.

        color key does not overrides image's alpha channel ( if the image has )

        the function may throw an exception if error.
*/

typedef void (*tTVPGraphicHeaderLoadingHandler)(void* formatdata,
                                                tTJSBinaryStream* src,
                                                class iTJSDispatch2** dic);
typedef void (*tTVPGraphicSaveHandler)(void* formatdata,
                                       tTJSBinaryStream* dst,
                                       const class iTVPBaseBitmap* image,
                                       const ttstr& mode,
                                       class iTJSDispatch2* meta);

/*[*/
typedef bool (*tTVPGraphicAcceptSaveHandler)(void* formatdata,
                                             const ttstr& type,
                                             class iTJSDispatch2** dic);
/*]*/

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Graphics Format Management
//---------------------------------------------------------------------------
/*[*/
struct tTVPRegisterGraphicInfo
{
    ttstr id;
    tTVPGraphicQuickTestHandler TestHandler;
    tTVPGraphicLoadingHandler LoadHandler;
    tTVPGraphicHeaderLoadingHandler HeaderHandler;
    tTVPGraphicSaveHandler SaveHandler;
    tTVPGraphicAcceptSaveHandler AcceptHandler;
    void* FormatData;

    tTVPRegisterGraphicInfo(const ttstr& ext,
                           tTVPGraphicQuickTestHandler quicktest,
                           tTVPGraphicLoadingHandler loading,
                           tTVPGraphicHeaderLoadingHandler header,
                           tTVPGraphicSaveHandler save,
                           tTVPGraphicAcceptSaveHandler accept,
                           void* data)
    {
        id = ext;
        TestHandler = quicktest;
        LoadHandler = loading;
        HeaderHandler = header;
        SaveHandler = save;
        AcceptHandler = accept;
        FormatData = data;
    }
    void Load(void* formatdata,
              void* callbackdata,
              tTVPGraphicSizeCallback sizecallback,
              tTVPGraphicScanLineCallback scanlinecallback,
              tTVPMetaInfoPushCallback metainfopushcallback,
              tTJSBinaryStream* src,
              tjs_int32 keyidx,
              tTVPGraphicLoadMode mode);
    void Save(const ttstr& storagename,
              const ttstr& mode,
              const iTVPBaseBitmap* image,
              iTJSDispatch2* meta);
    void Header(tTJSBinaryStream* src, iTJSDispatch2** dic);
    bool AcceptSave(const ttstr& type, iTJSDispatch2** dic)
    {
        if (AcceptHandler == NULL)
            return false;
        return AcceptHandler(FormatData, type, dic);
    }
};
/*]*/

extern void TVPRegisterGraphicLoadingHandler(tTVPRegisterGraphicInfo* rgi);

extern void TVPUnregisterGraphicLoadingHandler(tTVPRegisterGraphicInfo* rgi);
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Graphics cache management
//---------------------------------------------------------------------------
extern bool TVPAllocGraphicCacheOnHeap;
// this allocates graphic cache's store memory on heap, rather than
// shareing bitmap object. ( since sucking win9x cannot have so many bitmap
// object at once, WinNT/2000 is ok. )
// this will take more time for memory copying.
extern void TVPSetGraphicCacheLimit(tjs_uint64 limit);
// set graphic cache size limit by bytes.
// limit == 0 disables the cache system.
// limit == -1 sets the limit to TVPGraphicCacheSystemLimit
extern tjs_uint64 TVPGetGraphicCacheLimit();

extern tjs_uint64 TVPGraphicCacheSystemLimit;
// maximum possible value of Graphic Cache Limit

extern void TVPClearGraphicCache();
// clear graphic cache

extern void TVPTouchImages(const std::vector<ttstr>& storages, tjs_int64 limit, tjs_uint64 timeout);

//---------------------------------------------------------------------------

class iTVPBaseBitmap;
//---------------------------------------------------------------------------
// TVPLoadGraphic
//---------------------------------------------------------------------------
extern int TVPLoadGraphic(iTVPBaseBitmap* dest,
                          const ttstr& name,
                          tjs_int keyidx,
                          tjs_uint desw,
                          tjs_uint desh,
                          tTVPGraphicLoadMode mode,
                          ttstr* provincename = NULL,
                          iTJSDispatch2** metainfo = NULL);
// throws exception when this function can not handle the file
//---------------------------------------------------------------------------
extern tTVPRegisterGraphicInfo* TVPGetGraphicLoadHandler(const ttstr& fileName);
extern void TVPLoadGraphicProvince(
    tTVPBaseBitmap* dest, const ttstr& name, tjs_int keyidx, tjs_uint desw, tjs_uint desh);

struct tTVPGraphicMetaInfoPair
{
    ttstr Name;
    ttstr Value;
    tTVPGraphicMetaInfoPair(const ttstr& name, const ttstr& value) : Name(name), Value(value) { ; }
};

extern iTJSDispatch2* TVPMetaInfoPairsToDictionary(std::vector<tTVPGraphicMetaInfoPair>* vec);
extern void TVPPushGraphicCache(const ttstr& nname,
                                class tTVPBitmap* bmp,
                                std::vector<tTVPGraphicMetaInfoPair>* meta);
extern bool TVPCheckImageCache(const ttstr& nname,
                               tTVPBaseBitmap* dest,
                               tTVPGraphicLoadMode mode,
                               tjs_uint dw,
                               tjs_uint dh,
                               tjs_int32 keyidx,
                               iTJSDispatch2** metainfo);
extern bool TVPHasImageCache(
    const ttstr& nname, tTVPGraphicLoadMode mode, tjs_uint dw, tjs_uint dh, tjs_int32 keyidx);
extern void TVPLoadImageHeader(const ttstr& storagename, iTJSDispatch2** dic);
extern void TVPSaveImage(const ttstr& storagename,
                         const ttstr& mode,
                         const iTVPBaseBitmap* image,
                         iTJSDispatch2* meta);
// 把图像编码到内存（不落盘）。mode 为 bmp/png/jpg 等已注册的保存格式。
extern bool TVPSaveImageToMemory(const ttstr& mode,
                                 const iTVPBaseBitmap* image,
                                 iTJSDispatch2* meta,
                                 std::vector<unsigned char>& out);
extern bool TVPGetSaveOption(const ttstr& type, iTJSDispatch2** dic);
//---------------------------------------------------------------------------

#endif
