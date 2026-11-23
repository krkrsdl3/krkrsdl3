//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Graphics Loader ( loads graphic format from storage )
//---------------------------------------------------------------------------

#include "tjsCommHead.h"

#include "TVPGraphicsLoader.h"

#include "TVPMsg.h"
#include "TVPScript.h"
#include "LayerBitmap.h"
#include "TVPSystem.h"
#include "UtilStreams.h"
#include "RenderManager.h"
#include "tjsDictionary.h"
#include "TVPEvent.h"
#include "TVPApplication.h"
#include "TVPDebug.h"
#include "GraphicsLoadThread.h"
#include "Platform.h"

#include "tjsNativeBitmap.h"
#include "tjsNativeLayer.h"

//---------------------------------------------------------------------------
// default handlers
//---------------------------------------------------------------------------
extern tTVPRegisterGraphicInfo _bmpGraphicInfo;
extern tTVPRegisterGraphicInfo _tlgGraphicInfo;
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Graphics Format Management
//---------------------------------------------------------------------------

class tTVPGraphicType
{
public:
    std::vector<tTVPRegisterGraphicInfo*> _handlers;

    static bool Avail;

    tTVPGraphicType()
    {
        // register some native-supported formats
        _handlers.push_back(&_bmpGraphicInfo);
        _handlers.push_back(&_tlgGraphicInfo);
        Avail = true;
    }

    ~tTVPGraphicType() { Avail = false; }

    void Register(tTVPRegisterGraphicInfo* rgi)
    {
        // register graphic format to the table.
        for (auto& h : _handlers)
        {
            if (h->id == rgi->id)
                return;
        }
        _handlers.push_back(rgi);
    }

    void Unregister(tTVPRegisterGraphicInfo* rgi)
    {
        // unregister format from table.
        for (auto it = _handlers.begin(); it != _handlers.end(); ++it)
        {
            if ((*it)->id == rgi->id)
            {
                _handlers.erase(it);
                return;
            }
        }
    }

    tTVPRegisterGraphicInfo* QuickTest(tTJSBinaryStream* src)
    {
        for (auto handler : _handlers)
        {
            if (handler->TestHandler(src))
            {
                return handler;
            }
        }
        return nullptr;
    }
} static TVPGraphicType;
bool tTVPGraphicType::Avail = false;
//---------------------------------------------------------------------------
void tTVPRegisterGraphicInfo::Load(void* formatdata,
                                  void* callbackdata,
                                  tTVPGraphicSizeCallback sizecallback,
                                  tTVPGraphicScanLineCallback scanlinecallback,
                                  tTVPMetaInfoPushCallback metainfopushcallback,
                                  tTJSBinaryStream* src,
                                  tjs_int32 keyidx,
                                  tTVPGraphicLoadMode mode)
{
    if (LoadHandler == NULL)
        TVPThrowExceptionMessage(TVPUnknownGraphicFormat, TJS_N("unknown"));
    {
        LoadHandler(formatdata, callbackdata, sizecallback, scanlinecallback, metainfopushcallback,
                    src, keyidx, mode);
    }
}
void tTVPRegisterGraphicInfo::Save(const ttstr& storagename,
                                  const ttstr& mode,
                                  const iTVPBaseBitmap* image,
                                  iTJSDispatch2* meta)
{
    if (SaveHandler == NULL)
        TVPThrowExceptionMessage(TVPUnknownGraphicFormat, mode);

    tTJSBinaryStream* stream = TVPCreateStream(TVPNormalizeStorageName(storagename), TJS_BS_WRITE);
    {
        try
        {
            SaveHandler(FormatData, stream, image, mode, meta);
        }
        catch (...)
        {
            delete stream;
            throw;
        }
        delete stream;
    }
}
void tTVPRegisterGraphicInfo::Header(tTJSBinaryStream* src, iTJSDispatch2** dic)
{
    if (HeaderHandler == NULL)
        TVPThrowExceptionMessage(TVPUnknownGraphicFormat, TJS_N("unknown"));
    {
        HeaderHandler(FormatData, src, dic);
    }
}
//---------------------------------------------------------------------------
void TVPRegisterGraphicLoadingHandler(tTVPRegisterGraphicInfo* rgi)
{
    // name must be un-capitalized
    if (TVPGraphicType.Avail)
    {
        TVPGraphicType.Register(rgi);
    }
}
//---------------------------------------------------------------------------
void TVPUnregisterGraphicLoadingHandler(tTVPRegisterGraphicInfo* rgi)
{
    // name must be un-capitalized
    if (TVPGraphicType.Avail)
    {
        TVPGraphicType.Unregister(rgi);
    }
}
//---------------------------------------------------------------------------

/*
        loading handlers return whether the image contains an alpha channel.
*/
//---------------------------------------------------------------------------
void TVPLoadImageHeader(const ttstr& storagename, iTJSDispatch2** dic)
{
    if (dic == NULL)
        return;

    tTVPStreamHolder holder(storagename); // open a storage named "storagename"
    tTVPRegisterGraphicInfo* handler = TVPGraphicType.QuickTest(holder.Get());
    if (!handler)
        TVPThrowExceptionMessage(TVPUnknownGraphicFormat, storagename);

    handler->Header(holder.Get(), dic);
}
//---------------------------------------------------------------------------
void TVPSaveImage(const ttstr& storagename,
                  const ttstr& mode,
                  const iTVPBaseBitmap* image,
                  iTJSDispatch2* meta)
{
    if (!image->Is32BPP())
        TVPThrowInternalError;

    tTVPRegisterGraphicInfo* handler;
    for (auto itm : TVPGraphicType._handlers)
    {
        handler = itm;
        if (handler->AcceptSave(mode, NULL))
        {
            break;
        }
        else
        {
            handler = NULL;
        }
    }
    if (handler)
        handler->Save(storagename, mode, image, meta);
    else
        TVPThrowExceptionMessage(TVPUnknownGraphicFormat, mode);
}
//---------------------------------------------------------------------------
bool TVPSaveImageToMemory(const ttstr& mode,
                          const iTVPBaseBitmap* image,
                          iTJSDispatch2* meta,
                          std::vector<unsigned char>& out)
{
    out.clear();
    if (!image || !image->Is32BPP())
        return false;

    tTVPRegisterGraphicInfo* handler = NULL;
    for (auto itm : TVPGraphicType._handlers)
    {
        if (itm->AcceptSave(mode, NULL))
        {
            handler = itm;
            break;
        }
    }
    if (!handler || !handler->SaveHandler)
        return false;

    try
    {
        // 有些保存器（例如 JPEG）会无条件遍历 meta 字典，NULL 会直接崩，
        // 这里补一个空字典。
        iTJSDispatch2* ownMeta = NULL;
        if (!meta)
        {
            ownMeta = TJSCreateDictionaryObject();
            meta = ownMeta;
        }

        tTVPMemoryStream stream;
        try
        {
            handler->SaveHandler(handler->FormatData, &stream, image, mode, meta);
        }
        catch (...)
        {
            if (ownMeta)
                ownMeta->Release();
            throw;
        }
        if (ownMeta)
            ownMeta->Release();

        const tjs_uint size = (tjs_uint)stream.GetSize();
        if (!size)
            return false;
        const unsigned char* p = (const unsigned char*)stream.GetInternalBuffer();
        out.assign(p, p + size);
    }
    catch (...)
    {
        out.clear();
        return false;
    }

    return !out.empty();
}
//---------------------------------------------------------------------------
bool TVPGetSaveOption(const ttstr& type, iTJSDispatch2** dic)
{
    tTVPRegisterGraphicInfo* handler;
    for (auto itm : TVPGraphicType._handlers)
    {
        handler = itm;
        if (handler->AcceptSave(type, dic))
        {
            return true;
        }
    }
    return false;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPLoadGraphic related
//---------------------------------------------------------------------------
enum tTVPLoadGraphicType
{
    lgtFullColor, // full 32bit color
    lgtPalGray,   // palettized or grayscale
    lgtMask       // mask
};
struct tTVPLoadGraphicData
{
    ttstr Name;
    tTVPBitmap* Dest;
    tTVPLoadGraphicType Type;
    tjs_int ColorKey;
    tjs_uint8* Buffer;
    tjs_uint ScanLineNum;
    tjs_uint DesW;
    tjs_uint DesH;
    tjs_uint OrgW;
    tjs_uint OrgH;
    tjs_uint BufW;
    tjs_uint BufH;
    bool NeedMetaInfo;
    std::vector<tTVPGraphicMetaInfoPair>* MetaInfo;
};
//---------------------------------------------------------------------------
static int TVPLoadGraphic_SizeCallback(void* callbackdata,
                                       tjs_uint w,
                                       tjs_uint h,
                                       tTVPGraphicPixelFormat fmt)
{
    tTVPLoadGraphicData* data = (tTVPLoadGraphicData*)callbackdata;

    // check size
    data->OrgW = w;
    data->OrgH = h;
    if (data->DesW && w < data->DesW)
        w = data->DesW;
    if (data->DesH && h < data->DesH)
        h = data->DesH;
    data->BufW = w;
    data->BufH = h;

    // create buffer
    if (data->Type == lgtMask)
    {
        // mask ( _m ) load

        // check the image previously loaded
        if (data->Dest->GetWidth() != w || data->Dest->GetHeight() != h)
            TVPThrowExceptionMessage(TVPMaskSizeMismatch);

        // allocate line buffer
        data->Buffer = new tjs_uint8[w];
        data->Dest->IsOpaque = false;
        return w;
    }
    else
    {
        // normal load or province load
        if (!data->Dest)
        {
            data->Dest = new tTVPBitmap(w, h, data->Type == lgtFullColor ? 32 : 8);
        }
        else if (data->Dest->GetWidth() != w || data->Dest->GetHeight() != h)
        {
            data->Dest->Release();
            data->Dest = new tTVPBitmap(w, h, data->Type == lgtFullColor ? 32 : 8);
        }
        switch (fmt)
        {
            case gpfLuminance:
            case gpfRGB:
                data->Dest->IsOpaque = true;
                break;
            case gpfPalette:
            case gpfRGBA:
                data->Dest->IsOpaque = false;
                break;
        }
        return data->Dest->GetPitch();
    }
}
//---------------------------------------------------------------------------
static void* TVPLoadGraphic_ScanLineCallback(void* callbackdata, tjs_int y)
{
    tTVPLoadGraphicData* data = (tTVPLoadGraphicData*)callbackdata;

    if (y >= 0)
    {
        // query of line buffer

        data->ScanLineNum = y;
        if (data->Type == lgtMask)
        {
            // mask
            return data->Buffer;
        }
        else
        {
            // return the scanline for writing
            return data->Dest->GetScanLine(y);
        }
    }
    else
    {
        // y==-1 indicates the buffer previously returned was written

        if (data->Type == lgtMask)
        {
            // mask

            // tile for horizontal direction
            tjs_uint i;
            for (i = data->OrgW; i < data->BufW; i += data->OrgW)
            {
                tjs_uint w = data->BufW - i;
                w = w > data->OrgW ? data->OrgW : w;
                memcpy(data->Buffer + i, data->Buffer, w);
            }

            // bind mask buffer to main image buffer ( and tile for vertical )
            for (i = data->ScanLineNum; i < data->BufH; i += data->OrgH)
            {
                TVPBindMaskToMain((tjs_uint32*)data->Dest->GetScanLine(i), data->Buffer,
                                  data->BufW);
            }
            return NULL;
        }
        else if (data->Type == lgtFullColor)
        {
            tjs_uint32* sl = (tjs_uint32*)data->Dest->GetScanLine(data->ScanLineNum);
            if ((data->ColorKey & 0xff000000) == 0x00000000)
            {
                // make alpha from color key
                TVPMakeAlphaFromKey(sl, data->BufW, data->ColorKey);
            }

            // tile for horizontal direction
            tjs_uint i;
            for (i = data->OrgW; i < data->BufW; i += data->OrgW)
            {
                tjs_uint w = data->BufW - i;
                w = w > data->OrgW ? data->OrgW : w;
                memcpy(sl + i, sl, w * sizeof(tjs_uint32));
            }

            // tile for vertical direction
            for (i = data->ScanLineNum + data->OrgH; i < data->BufH; i += data->OrgH)
            {
                memcpy((tjs_uint32*)data->Dest->GetScanLine(i), sl,
                       data->BufW * sizeof(tjs_uint32));
            }

            return NULL;
        }
        else if (data->Type == lgtPalGray)
        {
            // nothing to do
            if (data->OrgW < data->BufW || data->OrgH < data->BufH)
            {
                tjs_uint8* sl = (tjs_uint8*)data->Dest->GetScanLine(data->ScanLineNum);
                tjs_uint i;

                // tile for horizontal direction
                for (i = data->OrgW; i < data->BufW; i += data->OrgW)
                {
                    tjs_uint w = data->BufW - i;
                    w = w > data->OrgW ? data->OrgW : w;
                    memcpy(sl + i, sl, w * sizeof(tjs_uint8));
                }

                // tile for vertical direction
                for (i = data->ScanLineNum + data->OrgH; i < data->BufH; i += data->OrgH)
                {
                    memcpy((tjs_uint8*)data->Dest->GetScanLine(i), sl,
                           data->BufW * sizeof(tjs_uint8));
                }
            }

            return NULL;
        }
    }
    return NULL;
}
//---------------------------------------------------------------------------
static void TVPLoadGraphic_MetaInfoPushCallback(void* callbackdata,
                                                const ttstr& name,
                                                const ttstr& value)
{
    tTVPLoadGraphicData* data = (tTVPLoadGraphicData*)callbackdata;

    if (data->NeedMetaInfo)
    {
        if (!data->MetaInfo)
            data->MetaInfo = new std::vector<tTVPGraphicMetaInfoPair>();
        data->MetaInfo->emplace_back(name, value);
    }
}
//---------------------------------------------------------------------------
// static int _USERENTRY TVPColorCompareFunc(const void *_a, const void *_b)
static int TVPColorCompareFunc(const void* _a, const void* _b)
{
    tjs_uint32 a = *(const tjs_uint32*)_a;
    tjs_uint32 b = *(const tjs_uint32*)_b;

    if (a < b)
        return -1;
    if (a == b)
        return 0;
    return 1;
}
//---------------------------------------------------------------------------
static void TVPMakeAlphaFromAdaptiveColor(tTVPBitmap* dest)
{
    // make adaptive color key and make alpha from it.
    // adaptive color key is most used(popular) color at first line of the
    // graphic.
    if (!dest->Is32bit())
        return;

    // copy first line to buffer
    tjs_int w = dest->GetWidth();
    tjs_int pitch = std::abs(dest->GetPitch());
    tjs_uint32* buffer = new tjs_uint32[pitch];

    try
    {
        const void* d = dest->GetScanLine(0);

        memcpy(buffer, d, pitch);
        tjs_int i;
        for (i = w - 1; i >= 0; i--)
            buffer[i] &= 0xffffff;
        buffer[w] = (tjs_uint32)-1; // a sentinel

        // sort by color
        qsort(buffer, dest->GetWidth(), sizeof(tjs_uint32), TVPColorCompareFunc);

        // find most used color
        tjs_int maxlen = 0;
        tjs_uint32 maxlencolor = 0;
        tjs_uint32 pcolor = (tjs_uint32)-1;
        tjs_int l = 0;
        for (i = 0; i < w + 1; i++)
        {
            if (buffer[i] != pcolor)
            {
                if (maxlen < l)
                {
                    maxlen = l;
                    maxlencolor = pcolor;
                    l = 0;
                }
            }
            else
            {
                l++;
            }
            pcolor = buffer[i];
        }

        if (maxlencolor == (tjs_uint32)-1)
        {
            // may color be not found...
            maxlencolor = 0; // black is a default colorkey
        }

        // make alpha from maxlencolor
        tjs_int h;
        for (h = dest->GetHeight() - 1; h >= 0; h--)
        {
            TVPMakeAlphaFromKey((tjs_uint32*)dest->GetScanLine(h), w, maxlencolor);
        }
    }
    catch (...)
    {
        delete[] buffer;
        throw;
    }

    delete[] buffer;
}
//---------------------------------------------------------------------------
static void TVPDoAlphaColorMat(tTVPBitmap* dest, tjs_uint32 color)
{
    // Do alpha matting.
    // 'mat' means underlying color of the image. This function piles
    // specified color under the image, then blend. The output image
    // will be totally opaque. This function always assumes the image
    // has pixel value for alpha blend mode, not additive alpha blend mode.
    if (!dest->Is32bit())
        return;

    tjs_int w = dest->GetWidth();
    tjs_int h = dest->GetHeight();

    for (tjs_int y = 0; y < h; y++)
    {
        tjs_uint32* buffer = (tjs_uint32*)dest->GetScanLine(y);
        TVPAlphaColorMat(buffer, color, w);
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
iTJSDispatch2* TVPMetaInfoPairsToDictionary(std::vector<tTVPGraphicMetaInfoPair>* vec)
{
    if (!vec)
        return NULL;
    std::vector<tTVPGraphicMetaInfoPair>::iterator i;
    iTJSDispatch2* dic = TJSCreateDictionaryObject();
    try
    {
        for (i = vec->begin(); i != vec->end(); i++)
        {
            tTJSVariant val(i->Value);
            dic->PropSet(TJS_MEMBERENSURE, i->Name.c_str(), 0, &val, dic);
        }
    }
    catch (...)
    {
        dic->Release();
        throw;
    }
    return dic;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Graphics Cache Management
//---------------------------------------------------------------------------
bool TVPAllocGraphicCacheOnHeap = false;
// this allocates graphic cache's store memory on heap, rather than
// sharing bitmap object. ( since sucking win9x cannot have so many bitmap
// object at once, WinNT/2000 is ok. )
// this will take more time for memory copying.
//---------------------------------------------------------------------------
struct tTVPGraphicsSearchData
{
    ttstr Name;
    tjs_int32 KeyIdx;         // color key index
    tTVPGraphicLoadMode Mode; // image mode
    tjs_uint DesW;            // desired width ( 0 for original size )
    tjs_uint DesH;            // desired height ( 0 for original size )

    bool operator==(const tTVPGraphicsSearchData& rhs) const
    {
        return KeyIdx == rhs.KeyIdx && Mode == rhs.Mode && Name == rhs.Name && DesW == rhs.DesW &&
               DesH == rhs.DesH;
    }
};
//---------------------------------------------------------------------------
class tTVPGraphicsSearchHashFunc
{
public:
    static tjs_uint32 Make(const tTVPGraphicsSearchData& val)
    {
        tjs_uint32 v = tTJSHashFunc<ttstr>::Make(val.Name);

        v ^= val.KeyIdx + (val.KeyIdx >> 23);
        v ^= (val.Mode << 30);
        v ^= val.DesW + (val.DesW >> 8);
        v ^= val.DesH + (val.DesH >> 8);
        return v;
    }
};
//---------------------------------------------------------------------------
class tTVPGraphicImageData
{
private:
    tTVPBitmap* Bitmap;
    iTVPTexture2D* Texture = nullptr;
    tjs_uint8* RawData;
    tjs_int Width;
    tjs_int Height;
    tjs_int PixelSize;

public:
    ttstr ProvinceName;

    std::vector<tTVPGraphicMetaInfoPair>* MetaInfo;

private:
    tjs_int RefCount;
    tjs_uint Size;

public:
    tTVPGraphicImageData()
    {
        RefCount = 1;
        Size = 0;
        Bitmap = NULL;
        RawData = NULL;
        MetaInfo = nullptr;
    }
    ~tTVPGraphicImageData()
    {
        if (Bitmap)
            Bitmap->Release();
        if (Texture)
            Texture->Release();
        if (RawData)
            delete[] RawData;
        if (MetaInfo)
            delete MetaInfo;
    }

    void AssignBitmap(tTVPBitmap* bmp)
    {
        if (Bitmap)
            delete Bitmap, Bitmap = NULL;
        if (RawData)
            delete[] RawData, RawData = NULL;
        if (Texture)
            Texture->Release(), Texture = nullptr;

        Width = bmp->GetWidth();
        Height = bmp->GetHeight();
        PixelSize = bmp->GetBPP() / 8;
        Size = Width * Height * PixelSize;

        if (!TVPAllocGraphicCacheOnHeap)
        {
            // simply assin to Bitmap
            Bitmap = bmp;
            Bitmap->AddRef();
        }
        else
        {
            // allocate heap and copy to it
            tjs_int h = Height;
            RawData = new tjs_uint8[Size];
            tjs_uint8* p = RawData;
            tjs_int rawpitch = Width * PixelSize;
            for (h--; h >= 0; h--)
            {
                memcpy(p, bmp->GetScanLine(h), rawpitch);
                p += rawpitch;
            }
        }
    }

    void AssignTexture(iTVPTexture2D* tex)
    {
        if (Bitmap)
            delete Bitmap, Bitmap = NULL;
        if (RawData)
            delete[] RawData, RawData = NULL;
        if (Texture)
            Texture->Release(), Texture = nullptr;

        Width = tex->GetWidth();
        Height = tex->GetHeight();
        PixelSize = (int)tex->GetFormat();
        Size = Width * Height * PixelSize;

        Texture = tex;
        Texture->AddRef();
    }

    void AssignToBitmap(tTVPBaseBitmap* bmp) const
    {
        if (!TVPAllocGraphicCacheOnHeap)
        {
            // simply assign to Bitmap
            if (Bitmap)
                bmp->AssignBitmap(Bitmap);
        }
        else
        {
            // copy from the rawdata heap
            if (RawData)
            {
                bmp->Recreate(Width, Height, PixelSize == 4 ? 32 : 8);
                tjs_int h = Height;
                tjs_uint8* p = RawData;
                tjs_int rawpitch = Width * PixelSize;
                for (h--; h >= 0; h--)
                {
                    memcpy(bmp->GetScanLineForWrite(h), p, rawpitch);
                    p += rawpitch;
                }
            }
        }
    }

    void AssignToTexture(iTVPBaseBitmap* dst)
    {
        if (!Texture && Bitmap)
        {
            int bmpw = Bitmap->GetWidth(), bmph = Bitmap->GetHeight();
            TVPTextureFormat::e format;
            switch (PixelSize)
            {
                case 1:
                    format = TVPTextureFormat::Gray;
                    break;
                case 3:
                    format = TVPTextureFormat::RGB;
                    break;
                case 4:
                    format = TVPTextureFormat::RGBA;
                    break;
                default:
                    return;
            }
            Texture = TVPGetRenderManager()->CreateTexture2D(Bitmap);
            if (Bitmap)
                Bitmap->Release(), Bitmap = nullptr;
            if (RawData)
                delete[] RawData, RawData = nullptr;
        }

        if (Texture)
            dst->AssignTexture(Texture);
    }

    tjs_uint GetSize() const { return Size; }

    void AddRef() { RefCount++; }
    void Release()
    {
        if (RefCount == 1)
        {
            delete this;
        }
        else
        {
            RefCount--;
        }
    }
};
//---------------------------------------------------------------------------
typedef tTJSRefHolder<tTVPGraphicImageData> tTVPGraphicImageHolder;

typedef tTJSHashTable<tTVPGraphicsSearchData, tTVPGraphicImageHolder, tTVPGraphicsSearchHashFunc>
    tTVPGraphicCache;
tTVPGraphicCache TVPGraphicCache;
static bool TVPGraphicCacheEnabled = false;
static tjs_uint64 TVPGraphicCacheLimit = 0;
static tjs_uint64 TVPGraphicCacheTotalBytes = 0;
tjs_uint64 TVPGraphicCacheSystemLimit = 0; // maximum possible value of  TVPGraphicCacheLimit
//---------------------------------------------------------------------------
tjs_uint64 TVPGetGraphicCacheTotalBytes()
{
    return TVPGraphicCacheTotalBytes;
}
//---------------------------------------------------------------------------
static void TVPCheckGraphicCacheLimit()
{
    while (TVPGraphicCacheTotalBytes > TVPGraphicCacheLimit)
    {
        // chop last graphics
        tTVPGraphicCache::tIterator i;
        i = TVPGraphicCache.GetLast();
        if (!i.IsNull())
        {
            tjs_uint size = i.GetValue().GetObjectNoAddRef()->GetSize();
            TVPGraphicCacheTotalBytes -= size;
            TVPGraphicCache.ChopLast(1);
        }
        else
        {
            break;
        }
    }
}
//---------------------------------------------------------------------------
void TVPClearGraphicCache()
{
    TVPGraphicCache.Clear();
    TVPGraphicCacheTotalBytes = 0;
}
static tTVPAtExit TVPUninitMessageLoad(TVP_ATEXIT_PRI_RELEASE, TVPClearGraphicCache);
//---------------------------------------------------------------------------
struct tTVPClearGraphicCacheCallback : public tTVPCompactEventCallbackIntf
{
    virtual void OnCompact(tjs_int level)
    {
        if (level >= TVP_COMPACT_LEVEL_MINIMIZE)
        {
            // clear the font cache on application minimize
            TVPClearGraphicCache();
        }
    }
} static TVPClearGraphicCacheCallback;
static bool TVPClearGraphicCacheCallbackInit = false;
//---------------------------------------------------------------------------
void TVPPushGraphicCache(const ttstr& nname,
                         tTVPBitmap* bmp,
                         std::vector<tTVPGraphicMetaInfoPair>* meta)
{
    if (TVPGraphicCacheEnabled)
    {
        // graphic compact initialization
        if (!TVPClearGraphicCacheCallbackInit)
        {
            TVPAddCompactEventHook(&TVPClearGraphicCacheCallback);
            TVPClearGraphicCacheCallbackInit = true;
        }

        tTVPGraphicImageData* data = NULL;
        try
        {
            tjs_uint32 hash;
            tTVPGraphicsSearchData searchdata;

            searchdata.Name = nname;
            searchdata.KeyIdx = TVP_clNone;
            searchdata.Mode = glmNormal;
            searchdata.DesW = 0;
            searchdata.DesH = 0;

            hash = tTVPGraphicCache::MakeHash(searchdata);

            data = new tTVPGraphicImageData();
            data->AssignBitmap(bmp);
            data->ProvinceName = TJS_N("");
            data->MetaInfo = meta;
            meta = NULL;

            // check size limit
            TVPCheckGraphicCacheLimit();

            // push into hash table
            tjs_uint datasize = data->GetSize();
            TVPGraphicCacheTotalBytes += datasize;
            tTVPGraphicImageHolder holder(data);
            TVPGraphicCache.AddWithHash(searchdata, hash, holder);
        }
        catch (...)
        {
            if (meta)
                delete meta;
            if (data)
                data->Release();
            throw;
        }
        if (data)
            data->Release();
    }
    else
    {
        if (meta)
            delete meta;
    }
}
//---------------------------------------------------------------------------
bool TVPCheckImageCache(const ttstr& nname,
                        tTVPBaseBitmap* dest,
                        tTVPGraphicLoadMode mode,
                        tjs_uint dw,
                        tjs_uint dh,
                        tjs_int32 keyidx,
                        iTJSDispatch2** metainfo)
{
    tjs_uint32 hash;
    tTVPGraphicsSearchData searchdata;
    if (TVPGraphicCacheEnabled)
    {
        searchdata.Name = nname;
        searchdata.KeyIdx = keyidx;
        searchdata.Mode = mode;
        searchdata.DesW = dw;
        searchdata.DesH = dh;

        hash = tTVPGraphicCache::MakeHash(searchdata);

        tTVPGraphicImageHolder* ptr = TVPGraphicCache.FindAndTouchWithHash(searchdata, hash);
        if (ptr)
        {
            // found in cache
            ptr->GetObjectNoAddRef()->AssignToBitmap(dest);
            if (metainfo)
                *metainfo = TVPMetaInfoPairsToDictionary(ptr->GetObjectNoAddRef()->MetaInfo);
            return true;
        }
    }
    return false;
}
//---------------------------------------------------------------------------
// 検索だけする
bool TVPHasImageCache(
    const ttstr& nname, tTVPGraphicLoadMode mode, tjs_uint dw, tjs_uint dh, tjs_int32 keyidx)
{
    tjs_uint32 hash;
    tTVPGraphicsSearchData searchdata;
    if (TVPGraphicCacheEnabled)
    {
        searchdata.Name = nname;
        searchdata.KeyIdx = keyidx;
        searchdata.Mode = mode;
        searchdata.DesW = dw;
        searchdata.DesH = dh;

        hash = tTVPGraphicCache::MakeHash(searchdata);

        tTVPGraphicImageHolder* ptr = TVPGraphicCache.FindAndTouchWithHash(searchdata, hash);
        if (ptr)
        {
            return true;
        }
    }
    return false;
}
//---------------------------------------------------------------------------
static std::vector<ttstr> _graphicAutoExtList = {
    ".pvr", ".webp", ".bmp", ".dib",  ".jpeg", ".jpg",
    ".jif", ".png",  ".tlg", ".tlg5", ".tlg6"}; // 用于后缀自动填充
static void TVPNormalizeGraphicNames(ttstr& _name, ttstr* maskname, ttstr* provincename)
{
    // graphic compact initialization
    if (!TVPClearGraphicCacheCallbackInit)
    {
        TVPAddCompactEventHook(&TVPClearGraphicCacheCallback);
        TVPClearGraphicCacheCallbackInit = true;
    }

    // search according with its extension
    tjs_int namelen = _name.GetLen();
    ttstr name(_name);

    ttstr ext = TVPExtractStorageExt(name);
    int extlen = ext.GetLen();

    if (ext.IsEmpty())
    {
        // missing extension
        // suggest registered extensions
        bool hasGet = false;
        for (auto extItem : _graphicAutoExtList)
        {
            ttstr newname = name + extItem;
            if (TVPIsExistentStorage(newname))
            {
                // file found
                name = newname;
                hasGet = true;
                break;
            }
        }
        if (!hasGet)
        {
            // not found
            TVPThrowExceptionMessage(TVPUnknownGraphicFormat, name);
        }
    }

    ttstr retname(name);

    if (maskname)
    {
        // mask image handling ( addding _m suffix with the filename )
        while (true)
        {
            name = ttstr(_name, namelen - extlen) + TJS_N("_m") + ext;
            if (ext.IsEmpty())
            {
                // missing extension
                // suggest registered extensions
                bool hasGet = false;
                for (auto extItem : _graphicAutoExtList)
                {
                    ttstr newname = name;
                    newname += extItem;
                    if (TVPIsExistentStorage(newname))
                    {
                        // file found
                        name = newname;
                        hasGet = true;
                        break;
                    }
                }
                if (!hasGet)
                {
                    // not found
                    maskname->Clear();
                    break;
                }
                *maskname = name;
                break;
            }
            else
            {
                if (!TVPIsExistentStorage(name))
                {
                    // not found
                    ext.Clear();
                    continue; // retry searching
                }
                *maskname = name;
                break;
            }
        }
    }
    if (provincename)
    {
        // set province name
        *provincename = ttstr(_name, namelen - extlen) + TJS_N("_p");

        // search extensions
        bool hasGet = false;
        for (auto extItem : _graphicAutoExtList)
        {
            ttstr newname = *provincename + extItem;
            if (TVPIsExistentStorage(newname))
            {
                // file found
                *provincename = newname;
                hasGet = true;
                break;
            }
        }
        if (!hasGet)
        {
            // not found
            provincename->Clear();
        }
    }

    _name = retname;
}
//---------------------------------------------------------------------------
static tTVPBitmap* TVPInternalLoadBitmap(const ttstr& _name,
                                         tjs_uint32 keyidx,
                                         tjs_uint desw,
                                         tjs_int desh,
                                         std::vector<tTVPGraphicMetaInfoPair>** MetaInfo,
                                         tTVPGraphicLoadMode mode,
                                         ttstr* provincename)
{
    // name must be normalized.
    // if "provincename" is non-null, this function set it to province storage
    // name ( with _p suffix ) for convinience.
    // desw and desh are desired size. if the actual picture is smaller than
    // the given size, the graphic is to be tiled. give 0,0 to obtain default
    // size graphic.

    ttstr name(_name), maskname;
    TVPNormalizeGraphicNames(name, &maskname, mode == glmNormal ? provincename : nullptr);
    tTVPStreamHolder holder(name); // open a storage named "name"
    tTVPRegisterGraphicInfo* handler = TVPGraphicType.QuickTest(holder.Get());
    if (!handler)
        TVPThrowExceptionMessage(TVPImageLoadError, TJS_N("Invalid image"));

    // load the image
    tTVPLoadGraphicData data;
    data.Dest = nullptr;
    data.ColorKey = keyidx;
    data.Type = mode == glmNormal ? lgtFullColor : lgtPalGray;
    data.Name = name;
    data.DesW = desw;
    data.DesH = desh;
    data.NeedMetaInfo = true;
    data.MetaInfo = NULL;

    bool keyadapt = (keyidx == TVP_clAdapt);
    bool doalphacolormat = TVP_Is_clAlphaMat(keyidx);
    tjs_uint32 alphamatcolor = TVP_get_clAlphaMat(keyidx);

    if (TVP_Is_clPalIdx(keyidx))
    {
        // pass the palette index number to the handler.
        // ( since only Graphic Loading Handler can process the palette information )
        keyidx = TVP_get_clPalIdx(keyidx);
    }
    else
    {
        keyidx = -1;
    }

    handler->Load(handler->FormatData, (void*)&data, TVPLoadGraphic_SizeCallback,
                  TVPLoadGraphic_ScanLineCallback, TVPLoadGraphic_MetaInfoPushCallback,
                  holder.Get(), keyidx, mode);

    *MetaInfo = data.MetaInfo;

    if (keyadapt && mode == glmNormal)
    {
        // adaptive color key
        TVPMakeAlphaFromAdaptiveColor(data.Dest);
    }

    if (mode != glmNormal)
        return data.Dest;

    if (!maskname.IsEmpty())
    {
        // open the mask file
        holder.Open(maskname);
        handler = TVPGraphicType.QuickTest(holder.Get());

        // fill "data"'s member
        data.Type = lgtMask;
        //	    data.Name = name;
        data.Buffer = NULL;
        data.DesW = desw;
        data.DesH = desh;
        data.NeedMetaInfo = false;

        try
        {
            // load image via handler
            handler->Load(handler->FormatData, (void*)&data, TVPLoadGraphic_SizeCallback,
                          TVPLoadGraphic_ScanLineCallback, NULL, holder.Get(), -1, glmGrayscale);
        }
        catch (...)
        {
            if (data.Buffer)
                delete[] data.Buffer;
            if (data.Dest)
                data.Dest->Release();
            throw;
        }

        if (data.Buffer)
            delete[] data.Buffer;
    }

    // do color matting
    if (doalphacolormat)
    {
        // alpha color mat
        TVPDoAlphaColorMat(data.Dest, alphamatcolor);
    }

    return data.Dest;
}
//---------------------------------------------------------------------------
static iTVPTexture2D* TVPInternalLoadTexture(const ttstr& _name,
                                             std::vector<tTVPGraphicMetaInfoPair>** MetaInfo,
                                             ttstr* provincename)
{
    ttstr name(_name), maskname;
    TVPNormalizeGraphicNames(name, &maskname, provincename);
    if (!maskname.IsEmpty())
    {
        // mask merge is not supported
        return nullptr;
    }
    tTVPStreamHolder holder(name);
    return NULL;
}
//---------------------------------------------------------------------------
tTVPRegisterGraphicInfo* TVPGetGraphicLoadHandler(const ttstr& fileName)
{
    ttstr name(fileName);
    TVPNormalizeGraphicNames(name, nullptr, nullptr);
    tTVPStreamHolder holder(name);
    return TVPGraphicType.QuickTest(holder.Get());
}
//---------------------------------------------------------------------------
void TVPLoadGraphicProvince(
    tTVPBaseBitmap* dest, const ttstr& name, tjs_int keyidx, tjs_uint desw, tjs_uint desh)
{
    tjs_uint32 hash;
    ttstr nname = TVPNormalizeStorageName(name);
    tTVPGraphicsSearchData searchdata;
    if (TVPGraphicCacheEnabled)
    {
        searchdata.Name = nname;
        searchdata.KeyIdx = keyidx;
        searchdata.Mode = glmPalettized;
        searchdata.DesW = desw;
        searchdata.DesH = desh;

        hash = tTVPGraphicCache::MakeHash(searchdata);

        tTVPGraphicImageHolder* ptr = TVPGraphicCache.FindAndTouchWithHash(searchdata, hash);
        if (ptr)
        {
            // found in cache
            ptr->GetObjectNoAddRef()->AssignToBitmap(dest);
            return;
        }
    }
    // not found

    // load into dest
    tTVPGraphicImageData* data = nullptr;

    ttstr pn;
    std::vector<tTVPGraphicMetaInfoPair>* mi = nullptr;
    try
    {
        tTVPBitmap* bmp = TVPInternalLoadBitmap(nname, keyidx, desw, desh, &mi, glmPalettized, &pn);
        dest->AssignBitmap(bmp);
        if (TVPGraphicCacheEnabled)
        {
            data = new tTVPGraphicImageData();
            data->AssignBitmap(bmp);
            data->ProvinceName = pn;
            data->MetaInfo = mi; // now mi is managed under tTVPGraphicImageData
            mi = nullptr;

            // check size limit
            TVPCheckGraphicCacheLimit();

            // push into hash table
            tjs_uint datasize = data->GetSize();
            TVPGraphicCacheTotalBytes += datasize;
            tTVPGraphicImageHolder holder(data);
            TVPGraphicCache.AddWithHash(searchdata, hash, holder);
        }
        bmp->Release();
    }
    catch (...)
    {
        if (mi)
            delete mi;
        if (data)
            data->Release();
        throw;
    }

    if (mi)
        delete mi;
    if (data)
        data->Release();
}

//---------------------------------------------------------------------------
// TVPLoadGraphic (to texture), return size
//---------------------------------------------------------------------------
int TVPLoadGraphic(iTVPBaseBitmap* dest,
                   const ttstr& name,
                   tjs_int32 keyidx,
                   tjs_uint desw,
                   tjs_uint desh,
                   tTVPGraphicLoadMode mode,
                   ttstr* provincename,
                   iTJSDispatch2** metainfo)
{
    // loading with cache management
    ttstr nname = TVPNormalizeStorageName(name);
    tjs_uint32 hash;
    tTVPGraphicsSearchData searchdata;

    if (TVPGraphicCacheEnabled)
    {
        searchdata.Name = nname;
        searchdata.KeyIdx = keyidx;
        searchdata.Mode = mode;
        searchdata.DesW = desw;
        searchdata.DesH = desh;

        hash = tTVPGraphicCache::MakeHash(searchdata);

        tTVPGraphicImageHolder* ptr = TVPGraphicCache.FindAndTouchWithHash(searchdata, hash);
        if (ptr)
        {
            // found in cache
            if (dest)
                ptr->GetObjectNoAddRef()->AssignToTexture(dest);
            if (provincename)
                *provincename = ptr->GetObjectNoAddRef()->ProvinceName;
            if (metainfo)
                *metainfo = TVPMetaInfoPairsToDictionary(ptr->GetObjectNoAddRef()->MetaInfo);
            return ptr->GetObjectNoAddRef()->GetSize();
        }
    }

    // load into dest
    tTVPGraphicImageData* data = NULL;

    ttstr pn;
    std::vector<tTVPGraphicMetaInfoPair>* mi = nullptr;
    int ret = 0;
    try
    {
        tTVPBitmap* bmp = nullptr;
        iTVPTexture2D* texture = nullptr;
        if (mode == glmNormal && keyidx == TVP_clNone && !desw && !desh)
        {
            texture = TVPInternalLoadTexture(nname, &mi, &pn);
        }
        if (!texture)
        {
            bmp = TVPInternalLoadBitmap(nname, keyidx, desw, desh, &mi, mode, &pn);
        }

        if (provincename)
            *provincename = pn;
        if (metainfo)
            *metainfo = TVPMetaInfoPairsToDictionary(mi);

        if (TVPGraphicCacheEnabled)
        {
            data = new tTVPGraphicImageData();
            if (texture)
            {
                data->AssignTexture(texture);
            }
            else
            {
                data->AssignBitmap(bmp);
            }
            if (dest)
            {
                data->AssignToTexture(dest);
            }
            data->ProvinceName = pn;
            data->MetaInfo = mi; // now mi is managed under tTVPGraphicImageData
            mi = NULL;

            // check size limit
            TVPCheckGraphicCacheLimit();

            // push into hash table
            tjs_uint datasize = data->GetSize();
            TVPGraphicCacheTotalBytes += datasize;
            tTVPGraphicImageHolder holder(data);
            TVPGraphicCache.AddWithHash(searchdata, hash, holder);
        }
        else if (dest)
        {
            tTVPGraphicImageData data;
            if (texture)
            {
                data.AssignTexture(texture);
            }
            else
            {
                data.AssignBitmap(bmp);
            }
            data.AssignToTexture(dest);
        }
        if (texture)
        {
            ret = texture->GetInternalWidth() * texture->GetInternalHeight() *
                  4; // assume that always RGBA
            texture->Release();
        }
        else
        {
            ret = bmp->GetWidth() * bmp->GetHeight() * bmp->GetBPP() / 8;
            bmp->Release();
        }
    }
    catch (...)
    {
        if (mi)
            delete mi;
        if (data)
            data->Release();
        throw;
    }

    if (mi)
        delete mi;
    if (data)
        data->Release();

    return ret;
}
//---------------------------------------------------------------------------

class tBitmapForAsyncTouch : public tTJSNI_Bitmap
{
    typedef tTJSNI_Bitmap inherit;

public:
    tBitmapForAsyncTouch() { Construct(0, nullptr, nullptr); }
    virtual void SetLoading(bool load) override
    {
        inherit::SetLoading(load);
        if (!load)
        {
            ::Application->PostUserMessage(
                [this]()
                {
                    Invalidate();
                    Destruct();
                });
        }
    }
};

//---------------------------------------------------------------------------
// TVPTouchImages
//---------------------------------------------------------------------------
void TVPTouchImages(const std::vector<ttstr>& storages, tjs_int64 limit, tjs_uint64 timeout)
{
    // preload graphic files into the cache.
    // "limit" is a limit memory for preload, in bytes.
    // this function gives up when "timeout" (in ms) expired.
    // currently this function only loads normal graphics.
    // (univ.trans rule graphics nor province image may not work properly)

    if (!TVPGraphicCacheLimit || !TVPGraphicCacheEnabled)
        return;

    tjs_uint64 limitbytes;
    if (limit >= 0)
    {
        if ((tjs_uint64)limit > TVPGraphicCacheLimit || limit == 0)
            limitbytes = TVPGraphicCacheLimit;
        else
            limitbytes = limit;
    }
    else
    {
        // negative value of limit indicates remaining bytes after loading
        if ((tjs_uint64)-limit >= TVPGraphicCacheLimit)
            return;
        limitbytes = TVPGraphicCacheLimit + limit;
    }
    if (/*!timeout &&*/ storages.size() /* > 1*/)
    { // using async touching for multi images
        for (const ttstr& name : storages)
        {
            ttstr nname = TVPNormalizeStorageName(name);
            tjs_uint32 hash;
            tTVPGraphicsSearchData searchdata;
            if (TVPGraphicCacheEnabled)
            {
                searchdata.Name = nname;
                searchdata.KeyIdx = TVP_clNone;
                searchdata.Mode = glmNormal;
                searchdata.DesW = 0;
                searchdata.DesH = 0;

                hash = tTVPGraphicCache::MakeHash(searchdata);

                tTVPGraphicImageHolder* ptr =
                    TVPGraphicCache.FindAndTouchWithHash(searchdata, hash);
                if (ptr)
                {
                    // found in cache
                    continue;
                }
            }
            Application->GetAsyncImageLoader()->PushLoadQueue(nullptr, new tBitmapForAsyncTouch(),
                                                              nname);
        }
        return;
    }

    tjs_int count = 0;
    tjs_uint64 bytes = 0;
    tjs_uint64 starttime = TVPGetTickCount();
    tjs_uint64 limittime = starttime + timeout;
    // tTVPBaseBitmap tmp(32, 32, 32);
    ttstr statusstr((const tjs_char*)TVPInfoTouching);
    bool first = true;
    while ((tjs_uint)count < storages.size())
    {
        if (timeout && TVPGetTickCount() >= limittime)
        {
            statusstr += (const tjs_char*)TVPAbortedTimeOut;
            break;
        }
        if (bytes >= limitbytes)
        {
            statusstr += (const tjs_char*)TVPAbortedLimitByte;
            break;
        }

        try
        {
            if (!first)
                statusstr += TJS_N(", ");
            first = false;
            statusstr += storages[count];

            bytes += TVPLoadGraphic(nullptr, storages[count++], TVP_clNone, 0, 0, glmNormal,
                                    NULL); // load image
        }
        catch (eTJS& e)
        {
            statusstr += TJS_N("(error!:");
            statusstr += e.GetMessage();
            statusstr += TJS_N(")");
        }
        catch (...)
        {
            // ignore all errors
        }
    }

    // re-touch graphic cache to ensure that more earlier graphics in storages
    // array can get more priority in cache order.
    count--;
    for (; count >= 0; count--)
    {
        tTVPGraphicsSearchData searchdata;
        searchdata.Name = TVPNormalizeStorageName(storages[count]);
        searchdata.KeyIdx = TVP_clNone;
        searchdata.Mode = glmNormal;
        searchdata.DesW = 0;
        searchdata.DesH = 0;

        tjs_uint32 hash = tTVPGraphicCache::MakeHash(searchdata);

        TVPGraphicCache.FindAndTouchWithHash(searchdata, hash);
    }

    statusstr += TJS_N(" (elapsed ");
    statusstr += ttstr((tjs_int)(TVPGetTickCount() - starttime));
    statusstr += TJS_N("ms)");

    TVPAddLog(statusstr);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPSetGraphicCacheLimit
//---------------------------------------------------------------------------
void TVPSetGraphicCacheLimit(tjs_uint64 limit)
{
    // set limit of graphic cache by total bytes.
    if (limit == 0)
    {
        TVPGraphicCacheLimit = limit;
        TVPGraphicCacheEnabled = false;
    }
    else if (limit == -1)
    {
        TVPGraphicCacheLimit = TVPGraphicCacheSystemLimit;
        TVPGraphicCacheEnabled = true;
    }
    else
    {
        if (limit > TVPGraphicCacheSystemLimit)
            limit = TVPGraphicCacheSystemLimit;
        TVPGraphicCacheLimit = limit;
        TVPGraphicCacheEnabled = true;
    }

    if (TVPGraphicCacheLimit > 512 * 1024 * 1024) // max for 512M
        TVPGraphicCacheLimit = 512 * 1024 * 1024;

    TVPCheckGraphicCacheLimit();
}
//---------------------------------------------------------------------------
tjs_uint64 TVPGetGraphicCacheLimit()
{
    return TVPGraphicCacheLimit;
}
//---------------------------------------------------------------------------