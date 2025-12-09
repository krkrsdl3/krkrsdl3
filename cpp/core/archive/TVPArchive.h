//---------------------------------------------------------------------------
/*
    TVP2 ( T Visual Presenter 2 )  A script authoring tool
    Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

    See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Universal Storage System
//---------------------------------------------------------------------------
#ifndef StorageImplH
#define StorageImplH
//---------------------------------------------------------------------------

#include "tjsHashSearch.h"

//---------------------------------------------------------------------------
// tTVPArchive base archive class
//---------------------------------------------------------------------------
class tTVPArchive {
private:
    tjs_uint RefCount;

public:
    //-- constructor
    tTVPArchive(const ttstr &name) {
        ArchiveName = name;
        Init = false;
        RefCount = 1;
    }
    virtual ~tTVPArchive() { ; }

    //-- AddRef and Release
    void AddRef() { RefCount++; }
    void Release() {
        if(RefCount == 1)
            delete this;
        else
            RefCount--;
    }

    //-- must be implemented by delivered class
    virtual tjs_uint GetCount() = 0;
    virtual ttstr GetName(tjs_uint idx) = 0;
    // returned name must be already normalized using
    // NormalizeInArchiveStorageName and the index must be sorted by its name,
    // using ttstr::operator < . this is needed by fast directory search.

    virtual tTJSBinaryStream *CreateStreamByIndex(tjs_uint idx) = 0;

    //-- others, implemented in this class
private:
    tTJSHashTable<ttstr, tjs_uint, tTJSHashFunc<ttstr>, 1024> Hash;
    bool Init;

public:
    ttstr ArchiveName;

public:
    static void NormalizeInArchiveStorageName(ttstr &name);

private:
    void AddToHash();

public:
    tTJSBinaryStream *CreateStream(const ttstr &name);
    bool IsExistent(const ttstr &name);

    tjs_int GetFirstIndexStartsWith(const ttstr &prefix);
    // returns first index which have 'prefix' at start of the name.
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
tTJSBinaryStream *TVPGetCachedArchiveHandle(void *pointer, const ttstr &name);
void TVPReleaseCachedArchiveHandle(void *pointer, tTJSBinaryStream *stream);
void TVPFreeArchiveHandlePoolByPointer(void *pointer);
void TVPFreeArchiveHandlePool();
void TVPShutdownArchiveHandleCache();
//---------------------------------------------------------------------------

class TArchiveStream : public tTJSBinaryStream {
    tTVPArchive *Owner;
    tjs_int64 CurrentPos;
    tjs_uint64 StartPos, DataLength;
    tTJSBinaryStream *_instr;

public:
    TArchiveStream(tTVPArchive *owner, tjs_uint64 off, tjs_uint64 len);
    virtual ~TArchiveStream();
    virtual tjs_uint64 TJS_INTF_METHOD Seek(tjs_int64 offset, tjs_int whence) {
        switch(whence) {
            case TJS_BS_SEEK_SET:
                CurrentPos = offset;
                break;

            case TJS_BS_SEEK_CUR:
                CurrentPos = offset + CurrentPos;
                break;

            case TJS_BS_SEEK_END:
                CurrentPos = offset + DataLength;
                break;
        }
        if(CurrentPos < 0)
            CurrentPos = 0;
        else if(CurrentPos > (tjs_int64)DataLength)
            CurrentPos = DataLength;
        _instr->SetPosition(CurrentPos + StartPos);
        return CurrentPos;
    }
    virtual tjs_uint TJS_INTF_METHOD Read(void *buffer, tjs_uint read_size) {
        if(CurrentPos + read_size >= (tjs_int64)DataLength) {
            read_size = (tjs_uint)(DataLength - CurrentPos);
        }

        _instr->ReadBuffer(buffer, read_size);

        CurrentPos += read_size;

        return read_size;
    }
    virtual tjs_uint TJS_INTF_METHOD Write(const void *buffer,
                                           tjs_uint write_size) {
        return 0;
    }
    virtual const std::string &TJS_INTF_METHOD GetFileName() { return ""; }
    virtual tjs_uint64 TJS_INTF_METHOD GetSize() { return DataLength; }
};

//---------------------------------------------------------------------------
void storeFilename(ttstr &name, const char *narrowName, const ttstr &filename);

#endif
