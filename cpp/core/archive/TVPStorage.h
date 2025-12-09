//---------------------------------------------------------------------------
/*
    TVP2 ( T Visual Presenter 2 )  A script authoring tool
    Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

    See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Universal Storage System
//---------------------------------------------------------------------------
#ifndef StorageIntfH
#define StorageIntfH

#include "tjsNative.h"
#include "tjsHashSearch.h"

#include "TVPArchive.h"

//---------------------------------------------------------------------------
// archive delimiter
//---------------------------------------------------------------------------
extern tjs_char TVPArchiveDelimiter; //  = '>';


//---------------------------------------------------------------------------
class iTVPStorageLister // callback class for GetListAt
{
public:
    virtual void TJS_INTF_METHOD Add(const ttstr &file) = 0;
};
//---------------------------------------------------------------------------
class iTVPStorageMedia {
public:
    virtual ~iTVPStorageMedia() {} // add by ZeaS
    virtual void TJS_INTF_METHOD AddRef() = 0;
    virtual void TJS_INTF_METHOD Release() = 0;

    virtual void TJS_INTF_METHOD GetName(ttstr &name) = 0;
    // returns media name like "file", "http" etc.

    //	virtual bool TJS_INTF_METHOD IsCaseSensitive() = 0;
    // returns whether this media is case sensitive or not

    virtual void TJS_INTF_METHOD NormalizeDomainName(ttstr &name) = 0;
    // normalize domain name according with the media's rule

    virtual void TJS_INTF_METHOD NormalizePathName(ttstr &name) = 0;
    // normalize path name according with the media's rule

    // "name" below is normalized but does not contain media, eg.
    // not "media://domain/path" but "domain/path"

    virtual bool TJS_INTF_METHOD CheckExistentStorage(const ttstr &name) = 0;
    // check file existence

    virtual tTJSBinaryStream *TJS_INTF_METHOD Open(const ttstr &name,
                                                   tjs_uint32 flags) = 0;
    // open a storage and return a tTJSBinaryStream instance.
    // name does not contain in-archive storage name but
    // is normalized.

    virtual void TJS_INTF_METHOD GetListAt(const ttstr &name,
                                           iTVPStorageLister *lister) = 0;
    // list files at given place

    virtual void TJS_INTF_METHOD GetLocallyAccessibleName(ttstr &name) = 0;
    // basically the same as above,
    // check wether given name is easily accessible from local OS filesystem.
    // if true, returns local OS native name. otherwise returns an empty string.
};
//---------------------------------------------------------------------------
/*]*/

class tTVPStorageMedia : public iTVPStorageMedia {
protected:
    tjs_int refCount;
    tTVPStorageMedia() : refCount(1) {}

    virtual void TJS_INTF_METHOD AddRef() override { refCount++; }
    virtual void TJS_INTF_METHOD Release() override {
        if(refCount == 1) {
            delete this;
        } else {
            refCount--;
        }
    }
    virtual void TJS_INTF_METHOD NormalizeDomainName(ttstr &name) override {}
    virtual void TJS_INTF_METHOD NormalizePathName(ttstr &name) override {}
    virtual void TJS_INTF_METHOD
    GetLocallyAccessibleName(ttstr &name) override {}
};

struct tTVPLocalFileInfo {
    const char *NativeName;
    unsigned short Mode; // S_IFMT
    tjs_uint64 Size;
    time_t AccessTime;
    time_t ModifyTime;
    time_t CreationTime;
};

//---------------------------------------------------------------------------
// implementation in this unit
//---------------------------------------------------------------------------
void TVPRegisterStorageMedia(iTVPStorageMedia *media);
// register storage media
void TVPUnregisterStorageMedia(iTVPStorageMedia *media);
// remove storage media


ttstr TVPNormalizeStorageName(const ttstr &name);
void TVPSetCurrentDirectory(const ttstr &name);
// set system current directory.
// directory must end with path delimiter '/',
// or archive delimiter '>'.
void TVPGetLocalName(ttstr &name);
ttstr TVPGetLocallyAccessibleName(const ttstr &name);

//---------------------------------------------------------------------------
// tTVPArchiveCache
//---------------------------------------------------------------------------
void TVPClearArchiveCache();
//---------------------------------------------------------------------------

bool TVPIsExistentStorageNoSearchNoNormalize(const ttstr &name);

bool TVPIsExistentStorageNoSearch(const ttstr &name);
// if "name" is exists, return true. otherwise return false.
// this does not search any auto search path.

ttstr TVPExtractStorageExt(const ttstr &name);
// extract "name"'s extension and return it.


ttstr TVPExtractStorageName(const ttstr &name);
// extract "name"'s storage name ( excluding path ) and return it.

ttstr TVPExtractStoragePath(const ttstr &name);
// extract "name"'s path ( including last delimiter ) and return it.

ttstr TVPChopStorageExt(const ttstr &name);
// chop storage's extension and return it.
// extensition delimiter '.' will not be held.

//---------------------------------------------------------------------------
// Auto search path support
//---------------------------------------------------------------------------
void TVPAddAutoPath(const ttstr &name);
// add given path to auto search path

void TVPRemoveAutoPath(const ttstr &name);
// remove given path from auto search path
//---------------------------------------------------------------------------

ttstr TVPGetPlacedPath(const ttstr &name);
// search path and return the path which the "name" is placed.

ttstr TVPSearchPlacedPath(const ttstr &name);
// the same as TVPGetPlacedPath, except for rising exception when the storage
// is not found.

bool TVPIsExistentStorage(const ttstr &name);
// if "name" is exists, return true. otherwise return false.
// this searches auto search path.

tTJSBinaryStream *TVPCreateStream(const ttstr &name,
                                         tjs_uint32 flags = 0);
// open "name" and return tTJSBinaryStream instance.
// name will be local storage, network storage, in-archive storage, etc...


void TVPClearStorageCaches();
// clear all internal storage related caches.
void TVPRemoveFromStorageCache(const ttstr &name);

ttstr TVPGetTemporaryName();
// retrieve file name to store temporary data ( must be unique, local name )
bool TVPRemoveFile(const ttstr &name);
// remove local file ( "name" is a local *native* name )
// this must not throw an exception ( return false if error )
bool TVPRemoveFolder(const ttstr &name);
// remove local directory ( "name" is a local *native* name )
// this must not throw an exception ( return false if error )

ttstr TVPGetAppPath();
// retrieve program path, in normalized storage name

//---------------------------------------------------------------------------
bool TVPCheckExistentLocalFolder(const ttstr &name);
/* name must be an OS's NATIVE folder name */

bool TVPCheckExistentLocalFile(const ttstr &name);
/* name must be an OS's NATIVE file name */

//---------------------------------------------------------------------------
// utilities
//---------------------------------------------------------------------------
ttstr TVPStringFromBMPUnicode(const tjs_uint16 *src, tjs_int maxlen = -1);
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// must be implemented in each platform
//---------------------------------------------------------------------------
tTVPArchive *TVPOpenArchive(const ttstr &name, bool normalizeFileName);
// open archive and return tTVPArchive instance.

int TVPCheckArchive(const ttstr &localname);

//---------------------------------------------------------------------------
// BinaryStream Functions
//---------------------------------------------------------------------------
tTJSBinaryStream *TVPCreateBinaryStreamForRead(const ttstr &name,
                                                      const ttstr &modestr);
tTJSBinaryStream *TVPCreateBinaryStreamForWrite(const ttstr &name,
                                                       const ttstr &modestr);
//---------------------------------------------------------------------------


void TVPPreNormalizeStorageName(ttstr &name);
// called by TVPNormalizeStorageName before it process the storage name.
// user may pass the OS's native filename to the TVP storage system,
// so that this function must convert it to the TVP storage name rules.


//---------------------------------------------------------------------------





bool TVPSelectFile(iTJSDispatch2 *params);

ttstr ReplaceStringAll(ttstr src, const ttstr &target,
                              const ttstr &dest);
ttstr GetDataPathDirectory(const ttstr &exename);

#endif
