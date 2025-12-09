#pragma once

#include "tjsNative.h"
#include "tjsDictionary.h"

#include "psbMedia.h"
#include "psbData.h"

//---------------------------------------------------------------------------
// tTJSNI_PsbFile
//---------------------------------------------------------------------------
class tTJSNI_PsbFile : public tTJSNativeInstance
{
    typedef tTJSNativeInstance inherited;

public:
    tTJSNI_PsbFile();
    tjs_error Construct(tjs_int numparams, tTJSVariant** param, iTJSDispatch2* tjs_obj);
    void Invalidate();

    bool load(const ttstr& filePath);
    tTJSVariant root();
    tjs_uint32 readListInfo(std::vector<tjs_uint32>* target);
    void refreshListInfo(std::vector<tjs_uint32>* target1, std::vector<tjs_uint32>* target2);
    tTJSVariant readAllObjs(const ttstr& key, tjs_uint32 _objOffset);

private:
    iTJSDispatch2* Owner;
    tTJSBinaryStream* filePtr = nullptr;

    PSB::PSBHeader _header;
    std::vector<tjs_uint32> stringsOffset;
    std::vector<tjs_uint32> namesData;
    std::vector<tjs_uint32> charset;
    std::vector<tjs_uint32> nameIndexes;
    std::vector<ttstr> namesCache;

    std::vector<tjs_uint32> chunkOffsets;
    std::vector<tjs_uint32> chunkLengths;

    std::vector<tjs_uint32> extraChunkOffsets;
    std::vector<tjs_uint32> extraChunkLengths;

    PSB::PSBMediaInfo _resources;
    tTJSVariant _root;
};

//---------------------------------------------------------------------------
// tTJSNC_PsbFile
//---------------------------------------------------------------------------
class tTJSNC_PsbFile : public tTJSNativeClass
{
    typedef tTJSNativeClass inherited;

public:
    tTJSNC_PsbFile();

    static tjs_uint32 ClassID;

protected:
    tTJSNativeInstance* CreateNativeInstance();
};
//---------------------------------------------------------------------------
tTJSNativeClass* TVPCreateNativeClass_PsbFile();