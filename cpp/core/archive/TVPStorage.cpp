//---------------------------------------------------------------------------
/*
    TVP2 ( T Visual Presenter 2 )  A script authoring tool
    Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

    See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Universal Storage System
//---------------------------------------------------------------------------
#include "tjsCommHead.h"
#include "TVPStorage.h"

#include "tjsUtils.h"
#include "TVPMsg.h"
#include "TVPDebug.h"
#include "TVPSystem.h"
#include "TVPEvent.h"
#include "Platform.h"
#include "TickCount.h"
#include "Random.h"
#include "XP3Archive.h"
#include "Platform.h"

#define TVP_DEFAULT_ARCHIVE_CACHE_NUM 64
#define TVP_DEFAULT_AUTOPATH_CACHE_NUM 256


//---------------------------------------------------------------------------
// global variables
//---------------------------------------------------------------------------
// archive delimiter
// this changes '>' from '#' since 2.19 beta 14
tjs_char TVPArchiveDelimiter = '>';

//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// statics
//---------------------------------------------------------------------------
static ttstr TVPCurrentMedia; // current media ( ex. "http" "ftp" "file" )
static tTJSStaticCriticalSection TVPCreateStreamCS;
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// tTVPStorageMediaManager
//---------------------------------------------------------------------------
class tTVPStorageMediaManager {
    class tMediaNameString : public tTJSString {
    public:
        bool operator==(const tMediaNameString &rhs) const {
            const tjs_char *l_p = c_str();
            const tjs_char *r_p = rhs.c_str();

            while(*l_p && *r_p) {
                if(*l_p == TJS_W(':'))
                    break;
                if(*r_p == TJS_W(':'))
                    break;
                if(*l_p != *r_p)
                    break;
                l_p++;
                r_p++;
            }
            if((*l_p == TJS_W(':') || *l_p == 0) &&
               (*r_p == TJS_W(':') || *r_p == 0))
                return true;
            return false;
        }
    };

    class tHashFunc {
    public:
        static tjs_uint32 Make(const tMediaNameString &key) {
            if(key.IsEmpty())
                return 0;
            const tjs_char *str = key.c_str();
            tjs_uint32 ret = 0;
            while(*str && *str != ':') {
                ret += *str;
                ret += (ret << 10);
                ret ^= (ret >> 6);
                str++;
            }
            ret += (ret << 3);
            ret ^= (ret >> 11);
            ret += (ret << 15);
            if(!ret)
                ret = (tjs_uint32)-1;
            return ret;
        }
    };

    class tMediaRecord {
    public:
        ttstr CurrentDomain;
        ttstr CurrentPath;
        tTJSRefHolder<iTVPStorageMedia> MediaIntf;
        tjs_int MediaNameLen;
        //		bool IsCaseSensitive;

        tMediaRecord(iTVPStorageMedia *media) :
            MediaIntf(media), CurrentDomain("."), CurrentPath("/") {
            ttstr name;
            media->GetName(name);
            MediaNameLen = name.GetLen();
            /*IsCaseSensitive = media->IsCaseSensitive();*/
        }

        const tjs_char *GetDomainAndPath(const ttstr &name) {
            return name.c_str() + MediaNameLen + 3;
            // 3 = strlen("://")
        }
    };

    typedef tTJSHashTable<tMediaNameString, tMediaRecord, tHashFunc, 16>
        tHashTable;

    tHashTable HashTable;

public:
    tTVPStorageMediaManager();
    ~tTVPStorageMediaManager();

private:
    static void ThrowUnsupportedMediaType(const ttstr &name);
    tMediaRecord *GetMediaRecord(const ttstr &name);

public:
    void Register(iTVPStorageMedia *media);
    void Unregister(iTVPStorageMedia *media);

    ttstr NormalizeStorageName(const ttstr &name, ttstr *ret_media = NULL,
                               ttstr *ret_domain = NULL,
                               ttstr *ret_path = NULL);

    void SetCurrentDirectory(const ttstr &name);

    static ttstr ExtractMediaName(const ttstr &name);

    bool CheckExistentStorage(const ttstr &name);
    tTJSBinaryStream *Open(const ttstr &name, tjs_uint32 flags);
    void GetListAt(const ttstr &name, iTVPStorageLister *lister);
    ttstr GetLocallyAccessibleName(const ttstr &name);
} TVPStorageMediaManager;
//---------------------------------------------------------------------------
tTVPStorageMediaManager::tTVPStorageMediaManager() {
    iTVPStorageMedia *filemedia = TVPCreateFileMedia();
    Register(filemedia);
    filemedia->Release();
}
//---------------------------------------------------------------------------
tTVPStorageMediaManager::~tTVPStorageMediaManager() {}
//---------------------------------------------------------------------------
void tTVPStorageMediaManager::ThrowUnsupportedMediaType(const ttstr &name) {
    TVPThrowExceptionMessage(TVPUnsupportedMediaName, ExtractMediaName(name));
}
//---------------------------------------------------------------------------
tTVPStorageMediaManager::tMediaRecord *
tTVPStorageMediaManager::GetMediaRecord(const ttstr &name) {
    tMediaRecord *rec = HashTable.Find(*(tMediaNameString *)&name);
    if(!rec)
        ThrowUnsupportedMediaType(name);
    return rec;
}
//---------------------------------------------------------------------------
void tTVPStorageMediaManager::Register(iTVPStorageMedia *media) {
    ttstr medianame;
    media->GetName(medianame);

    tMediaRecord *rec = HashTable.Find(*(tMediaNameString *)&medianame);
    if(rec)
        TVPThrowExceptionMessage(TVPMediaNameHadAlreadyBeenRegistered,
                                 medianame);

    tMediaRecord new_rec(media);

    HashTable.Add(*(tMediaNameString *)&medianame, new_rec);
}
//---------------------------------------------------------------------------
void tTVPStorageMediaManager::Unregister(iTVPStorageMedia *media) {
    ttstr medianame;
    media->GetName(medianame);

    tMediaRecord *rec = HashTable.Find(*(tMediaNameString *)&medianame);
    if(!rec)
        TVPThrowExceptionMessage(TVPMediaNameIsNotRegistered, medianame);
    HashTable.Delete(*(tMediaNameString *)&medianame);
}
//---------------------------------------------------------------------------
ttstr tTVPStorageMediaManager::NormalizeStorageName(const ttstr &name,
                                                    ttstr *ret_media,
                                                    ttstr *ret_domain,
                                                    ttstr *ret_path) {
    // Normalize storage name.

    // storage name is basically in following form:
    // media://domain/path

    // media is sort of access method, like "file", "http" ...etc.
    // domain represents in which computer the data is.
    // path is where the data is in the computer.

    // empty check
    if(name.IsEmpty())
        return name; // empty name is empty name

    // pre-normalize
    const tjs_char *pca; //, *pcb, *pcc;
    tjs_char *pa, *pb, *pc;

    ttstr tmp(name);
    TVPPreNormalizeStorageName(tmp);

    // unify path delimiter
    pa = tmp.Independ();
    while(*pa) {
        if(*pa == TJS_W('\\'))
            *pa = TJS_W('/');
        pa++;
    }

    // save in-archive storage name and normalize it
    ttstr inarchive_name;
    bool inarc_name_found = false;
    pca = tmp.c_str();
    pa = const_cast<tjs_char *>(TJS_strchr(pca, TVPArchiveDelimiter));
    if(pa) {
        inarchive_name = ttstr(pa + 1);
        tTVPArchive::NormalizeInArchiveStorageName(inarchive_name);
        inarc_name_found = true;
        tmp = ttstr(pca, (int)(pa - pca));
    }
    if(tmp.IsEmpty())
        TVPThrowExceptionMessage(TVPInvalidPathName, name);


    // split the name into media, domain, path
    // (and guess what component is omitted)
    ttstr media, domain, path;

    // - find media name
    //   media name is: /^[A-Za-z]+:/
    pa = pb = tmp.Independ();
    while(*pa) {
        if(!((*pa >= TJS_W('A') && *pa <= TJS_W('Z')) ||
             (*pa >= TJS_W('a') && *pa <= TJS_W('z'))))
            break;
        pa++;
    }

    if(*pa == TJS_W(':')) {
        // media name found
        media = ttstr(pb, (int)(pa - pb));
        pa++;
    } else {
        pa = pb;
    }

    // - find domain name
    // at this place, pa may point one of following:
    //  ///path        (domain is omitted)
    //  //domain/path  (none is omitted)
    //  /path          (domain is omitted)
    //  relative-path  (domain and current path are omitted)

    if(pa[0] == TJS_W('/')) {
        if(pa[1] == TJS_W('/')) {
            if(pa[2] == TJS_W('/')) {
                // slash count 3: domain is ommited
                pa += 2;
            } else {
                // slash count 2: none is omitted
                pa += 2;
                // find '/' as a domain delimiter
                pc = TJS_strchr(pa, TJS_W('/'));
                if(!pc)
                    TVPThrowExceptionMessage(TVPInvalidPathName, name);
                domain = ttstr(pa, (int)(pc - pa));
                pa = pc;
            }
        } else {
            // slash count 1: domain is omitted
            ;
            //
        }
    }

    // - get path name
    path = pa;

    // supply omitted and normalize
    if(media.IsEmpty()) {
        media = TVPCurrentMedia;
    } else {
        // normalize media name ( make them all small )
        tjs_char *p = media.Independ();
        while(*p) {
            if(*p >= TJS_W('A') && *p <= TJS_W('Z'))
                *p += (TJS_W('a') - TJS_W('A'));
            p++;
        }
    }

    tMediaRecord *mediarec = GetMediaRecord(media);

    if(domain.IsEmpty())
        domain = mediarec->CurrentDomain;
    mediarec->MediaIntf.GetObjectNoAddRef()->NormalizeDomainName(domain);

    if(path.IsEmpty()) {
        path = TJS_W("/");
    } else if(path.c_str()[0] != TJS_W('/')) {
        path = mediarec->CurrentPath + path;
    }
    mediarec->MediaIntf.GetObjectNoAddRef()->NormalizePathName(path);

    // compress redudant path accesses
    if(inarc_name_found) {
        tjs_char tmp[2];
        tmp[0] = TVPArchiveDelimiter;
        tmp[1] = 0;
        path += tmp + inarchive_name;
    }

    pa = pb = pc =
        path.Independ(); // pa = read pointer, pb = write pointer, pc = start
    tjs_int dot_count = -1;

    while(true) {
        if(*pa == TVPArchiveDelimiter || *pa == TJS_W('/') || *pa == 0) {
            tjs_char delim = 0;

            if(*pa && dot_count == 0) {
                // duplicated slashes
                pb--;
            } else if(dot_count > 0) {
                pb--;
                while(pb >= pc) {
                    if(*pb == TJS_W('/') || *pb == TVPArchiveDelimiter) {
                        dot_count--;
                        if(dot_count == 0) {
                            delim = *pb;
                            break;
                        }
                        if(*pb == TVPArchiveDelimiter)
                            TVPThrowExceptionMessage(TVPInvalidPathName, name);
                    }
                    pb--;
                }
                if(pb < pc)
                    TVPThrowExceptionMessage(TVPInvalidPathName, name);
            }

            if(!delim)
                *pb = *pa;
            else
                *pb = delim;
            if(*pa == 0)
                break;
            pb++;
            pa++;
            dot_count = 0;
        } else if(*pa == TJS_W('.')) {
            *(pb++) = *(pa++);
            if(dot_count != -1)
                dot_count++;
        } else {
            *(pb++) = *(pa++);
            dot_count = -1;
        }
    }

    path.FixLen();

    // merge and return normalize storage name
    if(ret_media)
        *ret_media = media;
    if(ret_domain)
        *ret_domain = domain;
    if(ret_path)
        *ret_path = path;

    tmp = media + TJS_W("://") + domain + path;

    return tmp;
}
//---------------------------------------------------------------------------
void tTVPStorageMediaManager::SetCurrentDirectory(const ttstr &name) {
    tjs_char ch = name.GetLastChar();
    if(ch != TJS_W('/') && ch != TJS_W('\\') && ch != TVPArchiveDelimiter)
        TVPThrowExceptionMessage(TVPMissingPathDelimiterAtLast);

    ttstr media, domain, path;
    NormalizeStorageName(name, &media, &domain, &path);

    tMediaRecord *rec = GetMediaRecord(media);
    rec->CurrentDomain = domain;
    rec->CurrentPath = path;
    TVPCurrentMedia = media;
}
//---------------------------------------------------------------------------
ttstr tTVPStorageMediaManager::ExtractMediaName(const ttstr &name) {
    // extract media name from normalized storage named "name".
    // returned media name does not contain colon.

    const tjs_char *p = name.c_str();
    const tjs_char *po = p;
    while(*p && *p != TJS_W(':'))
        p++;
    return ttstr(po, (int)(p - po));
}
//---------------------------------------------------------------------------
bool tTVPStorageMediaManager::CheckExistentStorage(const ttstr &name) {
    // gateway for CheckExistentStorage
    // name must not be an in-archive storage name
    tMediaRecord *rec = GetMediaRecord(name);
    return rec->MediaIntf.GetObjectNoAddRef()->CheckExistentStorage(
        rec->GetDomainAndPath(name));
}
//---------------------------------------------------------------------------
tTJSBinaryStream *tTVPStorageMediaManager::Open(const ttstr &name,
                                                tjs_uint32 flags) {
    // gateway for Open
    // name must not be an in-archive storage name
    tMediaRecord *rec = GetMediaRecord(name);
    return rec->MediaIntf.GetObjectNoAddRef()->Open(rec->GetDomainAndPath(name),
                                                    flags);
}
//---------------------------------------------------------------------------
void tTVPStorageMediaManager::GetListAt(const ttstr &name,
                                        iTVPStorageLister *lister) {
    // gateway for GetListAt
    // name must not be an in-archive storage name
    tMediaRecord *rec = GetMediaRecord(name);
    /*return */ rec->MediaIntf.GetObjectNoAddRef()->GetListAt(
        rec->GetDomainAndPath(name), lister);
}
//---------------------------------------------------------------------------
ttstr tTVPStorageMediaManager::GetLocallyAccessibleName(const ttstr &name) {
    // gateway for GetLocallyAccessibleName
    // name must not be an in-archive storage name
    tMediaRecord *rec = GetMediaRecord(name);
    ttstr dname = rec->GetDomainAndPath(name);
    rec->MediaIntf.GetObjectNoAddRef()->GetLocallyAccessibleName(dname);
    return dname;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void TVPRegisterStorageMedia(iTVPStorageMedia *media) {
    TVPStorageMediaManager.Register(media);
}
//---------------------------------------------------------------------------
void TVPUnregisterStorageMedia(iTVPStorageMedia *media) {
    TVPStorageMediaManager.Unregister(media);
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TVPNormalizeStorgeName : storage name normalization
//---------------------------------------------------------------------------
ttstr TVPNormalizeStorageName(const ttstr &_name)
// TODO: check what is done in TVPNormalizeStorageName
{
    return TVPStorageMediaManager.NormalizeStorageName(_name);
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TVPSetCurrentDirectory
//---------------------------------------------------------------------------
void TVPSetCurrentDirectory(const ttstr &_name) {
    TVPStorageMediaManager.SetCurrentDirectory(_name);
    TVPClearStorageCaches();
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TVPGetLocalName and TVPGetLocallyAccessibleName
//---------------------------------------------------------------------------
void TVPGetLocalName(ttstr &name) {
    ttstr tmp = TVPGetLocallyAccessibleName(name);
    if(tmp.IsEmpty())
        TVPThrowExceptionMessage(TVPCannotGetLocalName, name);
    name = tmp;
}
//---------------------------------------------------------------------------
ttstr TVPGetLocallyAccessibleName(const ttstr &name) {
    if(TJS_strchr(name.c_str(), TVPArchiveDelimiter))
        return TJS_W("");
    // in-archive storage is always not accessible from local file system
    return TVPStorageMediaManager.GetLocallyAccessibleName(name);
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// tTVPArchiveCache
//---------------------------------------------------------------------------
class tTVPArchiveCache {
    typedef tTJSRefHolder<tTVPArchive> tHolder;
    tTJSHashCache<ttstr, tHolder> ArchiveCache;
    tTJSCriticalSection CS;


public:
    tTVPArchiveCache() : ArchiveCache(TVP_DEFAULT_ARCHIVE_CACHE_NUM) {}

    ~tTVPArchiveCache() {}

    void SetMaxCount(tjs_int maxcount) { ArchiveCache.SetMaxCount(maxcount); }

    void Clear() {
        // releases all elements
        ArchiveCache.Clear();
    }

    tTVPArchive *Get(ttstr name) {
        name = TVPNormalizeStorageName(name);
        tTJSCSH csh(CS);
        tjs_uint32 hash = tTJSHashCache<ttstr, tHolder>::MakeHash(name);
        tHolder *ptr = ArchiveCache.FindAndTouchWithHash(name, hash);
        if(ptr) {
            // exist in the cache
            return ptr->GetObject();
        }

        if(!TVPIsExistentStorageNoSearch(name)) {
            // storage not found
            TVPThrowExceptionMessage(TVPCannotFindStorage, name);
        }

        // not exist in the cache
        tTVPArchive *arc = TVPOpenArchive(name, true);
        tHolder holder(arc);
        ArchiveCache.AddWithHash(name, hash, holder);
        return arc;
    }

private:
} TVPArchiveCache;
void TVPClearArchiveCache() { TVPArchiveCache.Clear(); }
static tTVPAtExit TVPClearArchiveCacheAtExit(TVP_ATEXIT_PRI_SHUTDOWN,
                                             TVPClearArchiveCache);
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TVPIsExistentStorageNoSearch
//---------------------------------------------------------------------------
bool TVPIsExistentStorageNoSearchNoNormalize(const ttstr &name) {
    // does name contain > ?
    tTJSCriticalSectionHolder cs_holder(TVPCreateStreamCS);

    const tjs_char *sharp_pos = TJS_strchr(name.c_str(), TVPArchiveDelimiter);
    if(sharp_pos) {
        // this storagename indicates a file in an archive

        ttstr arcname(name, (int)(sharp_pos - name.c_str()));

        tTVPArchive *arc;
        arc = TVPArchiveCache.Get(arcname);
        bool ret;
        try {
            ttstr in_arc_name(sharp_pos + 1);
            tTVPArchive::NormalizeInArchiveStorageName(in_arc_name);
            ret = arc->IsExistent(in_arc_name);
        } catch(...) {
            arc->Release();
            throw;
        }
        arc->Release();
        return ret;
    }

    return TVPStorageMediaManager.CheckExistentStorage(name);
}
//---------------------------------------------------------------------------
bool TVPIsExistentStorageNoSearch(const ttstr &_name) {
    return TVPIsExistentStorageNoSearchNoNormalize(
        TVPNormalizeStorageName(_name));
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TVPExtractStorageExt
//---------------------------------------------------------------------------
ttstr TVPExtractStorageExt(const ttstr &name) {
    // extract an extension from name.
    // returned string will contain extension delimiter ( '.' ), except for
    // missing extension of the input string.
    // ( returns null string when input string does not have an extension )

    const tjs_char *s = name.c_str();
    tjs_int slen = name.GetLen();
    const tjs_char *p = s + slen;
    p--;
    while(p >= s) {
        if(*p == TJS_W('\\'))
            break;
        if(*p == TJS_W('/'))
            break;
        if(*p == TVPArchiveDelimiter)
            break;
        if(*p == TJS_W('.')) {
            // found extension delimiter
            tjs_int extlen = (tjs_int)(slen - (p - s));
            return ttstr(p, extlen);
        }

        p--;
    }

    // not found
    return ttstr();
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TVPExtractStorageName
//---------------------------------------------------------------------------
ttstr TVPExtractStorageName(const ttstr &name) {
    // extract "name"'s storage name ( excluding path ) and return it.
    const tjs_char *s = name.c_str();
    tjs_int slen = name.GetLen();
    const tjs_char *p = s + slen;
    p--;
    while(p >= s) {
        if(*p == TJS_W('\\'))
            break;
        if(*p == TJS_W('/'))
            break;
        if(*p == TVPArchiveDelimiter)
            break;

        p--;
    }

    p++;
    if(p == s)
        return name;
    else
        return ttstr(p, (int)(slen - (p - s)));
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TVPExtractStoragePath
//---------------------------------------------------------------------------
ttstr TVPExtractStoragePath(const ttstr &name) {
    // extract "name"'s path ( including last delimiter ) and return it.
    const tjs_char *s = name.c_str();
    tjs_int slen = name.GetLen();
    const tjs_char *p = s + slen;
    p--;
    while(p >= s) {
        if(*p == TJS_W('\\'))
            break;
        if(*p == TJS_W('/'))
            break;
        if(*p == TVPArchiveDelimiter)
            break;

        p--;
    }

    p++;
    return ttstr(s, (int)(p - s));
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TVPChopStorageExt
//---------------------------------------------------------------------------
ttstr TVPChopStorageExt(const ttstr &name) {
    // chop storage's extension and return it.
    const tjs_char *s = name.c_str();
    tjs_int slen = name.GetLen();
    const tjs_char *p = s + slen;
    p--;
    while(p >= s) {
        if(*p == TJS_W('\\'))
            break;
        if(*p == TJS_W('/'))
            break;
        if(*p == TVPArchiveDelimiter)
            break;
        if(*p == TJS_W('.')) {
            // found extension delimiter
            return ttstr(s, (int)(p - s));
        }

        p--;
    }

    // not found
    return name;
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// Auto search path support
//---------------------------------------------------------------------------
#define TVP_AUTO_PATH_HASH_SIZE 1024
std::vector<ttstr> TVPAutoPathList;
tTJSHashCache<ttstr, ttstr> TVPAutoPathCache(TVP_DEFAULT_AUTOPATH_CACHE_NUM);
tTJSHashTable<ttstr, ttstr, tTJSHashFunc<ttstr>, TVP_AUTO_PATH_HASH_SIZE>
    TVPAutoPathTable;
static bool AutoPathTableInit = false;
//---------------------------------------------------------------------------
static void TVPClearAutoPathCache() {
    TVPAutoPathCache.Clear();
    TVPAutoPathTable.Clear();
    AutoPathTableInit = false;
}
//---------------------------------------------------------------------------
struct tTVPClearAutoPathCacheCallback : public tTVPCompactEventCallbackIntf {
    virtual void TJS_INTF_METHOD OnCompact(tjs_int level) {
        if(level >= TVP_COMPACT_LEVEL_DEACTIVATE) {
            // clear the auto search path cache on application deactivate
            tTJSCriticalSectionHolder cs_holder(TVPCreateStreamCS);
            TVPClearAutoPathCache();
        }
    }
} static TVPClearAutoPathCacheCallback;
static bool TVPClearAutoPathCacheCallbackInit = false;
//---------------------------------------------------------------------------
void TVPAddAutoPath(const ttstr &name) {
    tTJSCriticalSectionHolder cs_holder(TVPCreateStreamCS);

    tjs_char lastchar = name.GetLastChar();
    if(lastchar != TVPArchiveDelimiter && lastchar != TJS_W('/') &&
       lastchar != TJS_W('\\'))
        TVPThrowExceptionMessage(TVPMissingPathDelimiterAtLast);

    ttstr normalized = TVPNormalizeStorageName(name);

    std::vector<ttstr>::iterator i =
        std::find(TVPAutoPathList.begin(), TVPAutoPathList.end(), normalized);
    if(i == TVPAutoPathList.end())
        TVPAutoPathList.push_back(normalized);

    TVPClearAutoPathCache();
}
//---------------------------------------------------------------------------
void TVPRemoveAutoPath(const ttstr &name) {
    tTJSCriticalSectionHolder cs_holder(TVPCreateStreamCS);

    tjs_char lastchar = name.GetLastChar();
    if(lastchar != TVPArchiveDelimiter && lastchar != TJS_W('/') &&
       lastchar != TJS_W('\\'))
        TVPThrowExceptionMessage(TVPMissingPathDelimiterAtLast);

    ttstr normalized = TVPNormalizeStorageName(name);

    std::vector<ttstr>::iterator i =
        std::find(TVPAutoPathList.begin(), TVPAutoPathList.end(), normalized);
    if(i != TVPAutoPathList.end())
        TVPAutoPathList.erase(i);

    TVPClearAutoPathCache();
}
//---------------------------------------------------------------------------
static tjs_uint TVPRebuildAutoPathTable() {
    // rebuild auto path table
    if(AutoPathTableInit)
        return 0;

    tTJSCriticalSectionHolder cs_holder(TVPCreateStreamCS);

    TVPAutoPathTable.Clear();

    tjs_uint64 tick = TVPGetTickCount();
    TVPAddLog((const tjs_char *)TVPInfoRebuildingAutoPath);

    tjs_uint totalcount = 0;

    std::vector<ttstr>::iterator it;
    for(it = TVPAutoPathList.begin(); it != TVPAutoPathList.end(); it++) {
        const ttstr &path = *it;
        tjs_uint count = 0;

        const tjs_char *sharp_pos =
            TJS_strchr(path.c_str(), TVPArchiveDelimiter);
        if(sharp_pos) {
            // this storagename indicates a file in an archive

            ttstr arcname(path, (int)(sharp_pos - path.c_str()));
            ttstr in_arc_name(sharp_pos + 1);
            tTVPArchive::NormalizeInArchiveStorageName(in_arc_name);
            tjs_int in_arc_name_len = in_arc_name.GetLen();

            tTVPArchive *arc;
            arc = TVPArchiveCache.Get(arcname);

            try {
                tjs_uint storagecount = arc->GetCount();

                // get first index which the item has 'in_arc_name' as its start
                // of the string.
                tjs_int i = arc->GetFirstIndexStartsWith(in_arc_name);
                if(i != -1) {
                    for(; i < (tjs_int)storagecount; i++) {
                        ttstr name = arc->GetName(i);

                        if(name.StartsWith(in_arc_name)) {
                            if(!TJS_strchr(name.c_str() + in_arc_name_len,
                                           TJS_W('/'))) {
                                ttstr sname = TVPExtractStorageName(name);
                                TVPAutoPathTable.Add(sname, path);
                                count++;
                            }
                        } else {
                            // no need to check more;
                            // because the list is sorted by the name.
                            break;
                        }
                    }
                }
            } catch(...) {
                arc->Release();
                throw;
            }
            arc->Release();
        } else {
            // normal folder
            class tLister : public iTVPStorageLister {
            public:
                std::vector<ttstr> list;
                void TJS_INTF_METHOD Add(const ttstr &file) {
                    list.push_back(file);
                }
            } lister;

            TVPStorageMediaManager.GetListAt(path, &lister);
            for(std::vector<ttstr>::iterator i = lister.list.begin();
                i != lister.list.end(); i++) {
                TVPAutoPathTable.Add(*i, path);
                count++;
            }
        }

        totalcount += count;
    }

    tjs_uint64 endtick = TVPGetTickCount();

    TVPAddLog(ttstr(TJS_W("(info) Total ")) + ttstr((tjs_int)totalcount) +
              TJS_W(" file(s) found, ") +
              ttstr((tjs_int)TVPAutoPathTable.GetCount()) +
              TJS_W(" file(s) activated.") + TJS_W(" (") +
              ttstr((tjs_int)(endtick - tick)) + TJS_W("ms)"));

    AutoPathTableInit = true;

    return totalcount;
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TVPGetPlacedPath
//---------------------------------------------------------------------------
ttstr TVPGetPlacedPath(const ttstr &name) {
    // search path and return the path which the "name" is placed.
    // returned name is normalized. returns empty string if the storage is not
    // found.

    ttstr *incache = TVPAutoPathCache.FindAndTouch(name);
    if(incache)
        return *incache; // found in cache

    tTJSCriticalSectionHolder cs_holder(TVPCreateStreamCS);

    ttstr normalized(TVPNormalizeStorageName(name));

    bool found = TVPIsExistentStorageNoSearchNoNormalize(normalized);
    if(found) {
        // found in current folder
        TVPAutoPathCache.Add(name, normalized);
        return normalized;
    }

    // not found in current folder
    // search through auto path table

    ttstr storagename = TVPExtractStorageName(normalized);

    TVPRebuildAutoPathTable(); // ensure auto path table
    ttstr *result = TVPAutoPathTable.Find(storagename);
    if(result) {
        // found in table
        ttstr found = *result + storagename;
        TVPAutoPathCache.Add(name, found);
        return found;
    }

    // not found
    // TVPAutoPathCache.Add(name, ttstr()); // do not cache now
    return ttstr();
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TVPSearchPlacedPath
//---------------------------------------------------------------------------
ttstr TVPSearchPlacedPath(const ttstr &name) {
    ttstr place = TVPGetPlacedPath(name);
    if(place.IsEmpty())
        TVPThrowExceptionMessage(TVPCannotFindStorage, name);
    return place;
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TVPIsExistentStorage
//---------------------------------------------------------------------------
bool TVPIsExistentStorage(const ttstr &name) {
    return !TVPGetPlacedPath(name).IsEmpty();
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TVPCreateStream
//---------------------------------------------------------------------------
static tTJSBinaryStream *_TVPCreateStream(const ttstr &_name,
                                          tjs_uint32 flags) {
    tTJSCriticalSectionHolder cs_holder(TVPCreateStreamCS);

    ttstr name;

    tjs_uint32 access = flags & TJS_BS_ACCESS_MASK;
    if(access == TJS_BS_WRITE)
        name = TVPNormalizeStorageName(_name);
    else
        name = TVPGetPlacedPath(_name); // file must exist

    if(name.IsEmpty()) {
        if(access >= 1)
            TVPRemoveFromStorageCache(_name);
        TVPThrowExceptionMessage(TVPCannotOpenStorage, _name);
    }

    // does name contain > ?
    const tjs_char *sharp_pos = TJS_strchr(name.c_str(), TVPArchiveDelimiter);
    if(sharp_pos) {
        // this storagename indicates a file in an archive
        if((flags & TJS_BS_ACCESS_MASK) != TJS_BS_READ)
            TVPThrowExceptionMessage(TVPCannotWriteToArchive);

        ttstr arcname(name, (int)(sharp_pos - name.c_str()));

        tTVPArchive *arc;
        tTJSBinaryStream *stream;
        arc = TVPArchiveCache.Get(arcname);
        try {
            ttstr in_arc_name(sharp_pos + 1);
            tTVPArchive::NormalizeInArchiveStorageName(in_arc_name);
            stream = arc->CreateStream(in_arc_name);
        } catch(...) {
            arc->Release();
            if(access >= 1)
                TVPRemoveFromStorageCache(_name);
            throw;
        }
        if(access >= 1)
            TVPRemoveFromStorageCache(_name);
        arc->Release();
        return stream;
    }

    tTJSBinaryStream *stream;
    try {
        stream = TVPStorageMediaManager.Open(name, flags);
    } catch(...) {
        if(access >= 1)
            TVPRemoveFromStorageCache(_name);
        throw;
    }
    if(access >= 1)
        TVPRemoveFromStorageCache(_name);
    return stream;
}

tTJSBinaryStream *TVPCreateStream(const ttstr &_name, tjs_uint32 flags) {
    try {
        return _TVPCreateStream(_name, flags);
    } catch(eTJSScriptException &e) {
        if(TJS_strchr(_name.c_str(), '#'))
            e.AppendMessage(
                TJS_W("[") +
                TVPFormatMessage(TVPFilenameContainsSharpWarn, _name) +
                TJS_W("]"));
        throw e;
    } catch(eTJSScriptError &e) {
        if(TJS_strchr(_name.c_str(), '#'))
            e.AppendMessage(
                TJS_W("[") +
                TVPFormatMessage(TVPFilenameContainsSharpWarn, _name) +
                TJS_W("]"));
        throw e;
    } catch(eTJSError &e) {
        if(TJS_strchr(_name.c_str(), '#'))
            e.AppendMessage(
                TJS_W("[") +
                TVPFormatMessage(TVPFilenameContainsSharpWarn, _name) +
                TJS_W("]"));
        throw e;
    } catch(...) {
        // check whether the filename contains '#' (former delimiter for archive
        // filename before 2.19 beta 14)
        if(TJS_strchr(_name.c_str(), '#'))
            TVPAddLog(TVPFormatMessage(TVPFilenameContainsSharpWarn, _name));
        throw;
    }
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TVPClearStorageCaches
//---------------------------------------------------------------------------
void TVPClearStorageCaches() {
    // clear all storage related caches
    TVPClearAutoPathCache();
}
//---------------------------------------------------------------------------

void TVPRemoveFromStorageCache(const ttstr &name) {
    TVPAutoPathCache.Delete(name);
}


//---------------------------------------------------------------------------
// TVPGetTemporaryName
//---------------------------------------------------------------------------
static tjs_int TVPTempUniqueNum = 0;
static tTJSCriticalSection TVPTempUniqueNumCS;
static ttstr TVPTempPath;
static bool TVPTempPathInit = false;
static tjs_int TVPProcessID;
ttstr TVPGetTemporaryName() {
    static tjs_int TVPTempUniqueNum = (tjs_int)TVPGetRoughTickCount32();
    tjs_int num = TVPTempUniqueNum++;
    ttstr TVPTempPath = TVPGetAppPath();
    unsigned char buf[16];
    TVPGetRandomBits128(buf);
    tjs_char random[128];
    TJS_snprintf(random, sizeof(random) / sizeof(tjs_char),
                 TJS_W("%02x%02x%02x%02x%02x%02x"), buf[0], buf[1], buf[2],
                 buf[3], buf[4], buf[5]);

    return TVPTempPath + TJS_W("krkr_") + ttstr(random) + TJS_W("_") +
        ttstr(num) + TJS_W("_") + ttstr(TVPProcessID);
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TVPRemoveFile
//---------------------------------------------------------------------------
bool TVPRemoveFile(const ttstr &name) {
    tTJSNarrowStringHolder holder(name.c_str());
    return !remove(holder);
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TVPRemoveFolder
//---------------------------------------------------------------------------
bool TVPRemoveFolder(const ttstr &name) {
    tTJSNarrowStringHolder holder(name.c_str());
    return !TVPDeleteFile(holder.Buf);
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TVPGetAppPath
//---------------------------------------------------------------------------
ttstr TVPGetAppPath() {
    static ttstr apppath(TVPExtractStoragePath(TVPProjectDir));
    return apppath;
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TVPCheckExistantLocalFile
//---------------------------------------------------------------------------
bool TVPCheckExistentLocalFile(const ttstr &name) {
    tTVP_stat s;
    if(!TVP_stat(name.c_str(), s)) {
        return false; // not exist
    }
    return s.tvp_mode & S_IFREG;
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TVPCheckExistantLocalFolder
//---------------------------------------------------------------------------
bool TVPCheckExistentLocalFolder(const ttstr &name) {
    tTVP_stat s;
    if(!TVP_stat(name.c_str(), s)) {
        return false; // not exist
    }

    return s.tvp_mode & S_IFDIR;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// utilities
//---------------------------------------------------------------------------
ttstr TVPStringFromBMPUnicode(const tjs_uint16 *src, tjs_int maxlen) {
    // convert to ttstr from BMP unicode
    if(sizeof(tjs_char) == 2) {
        // sizeof(tjs_char) is 2 (windows native)
        if(maxlen == -1)
            return ttstr((const tjs_char *)src);
        else
            return ttstr((const tjs_char *)src, maxlen);
    } else if(sizeof(tjs_char) == 4) {
        // sizeof(tjs_char) is 4 (UCS32)
        // FIXME: NOT TESTED CODE
        tjs_int len = 0;
        const tjs_uint16 *p = src;
        while(*p)
            len++, p++;
        if(maxlen != -1 && len > maxlen)
            len = maxlen;
        ttstr ret((tTJSStringBufferLength)(len));
        tjs_char *dest = ret.Independ();
        p = src;
        while(len && *p) {
            *dest = *p;
            dest++;
            p++;
            len--;
        }
        *dest = 0;
        ret.FixLen();
        return ret;
    }
    return (const tjs_char *)TVPTjsCharMustBeTwoOrFour;
}
//---------------------------------------------------------------------------


tTVPArchive *TVPOpenZIPArchive(const ttstr &name, tTJSBinaryStream *st,
                               bool normalizeFileName);
tTVPArchive *TVPOpen7ZArchive(const ttstr &name, tTJSBinaryStream *st,
                              bool normalizeFileName);
tTVPArchive *TVPOpenTARArchive(const ttstr &name, tTJSBinaryStream *st,
                               bool normalizeFileName);
static tTVPArchive *(*ArchiveCreators[])(
    const ttstr &name, tTJSBinaryStream *st,
    bool normalizeFileName) = { TVPOpenZIPArchive, TVPOpen7ZArchive,
                                TVPOpenTARArchive, tTVPXP3Archive::Create };

//---------------------------------------------------------------------------
// TVPOpenArchive
//---------------------------------------------------------------------------
tTVPArchive *TVPOpenArchive(const ttstr &name, bool normalizeFileName) {
    tTJSBinaryStream *st = TVPCreateStream(name);
    if(!st)
        return nullptr;
    for(int i = 0; i < sizeof(ArchiveCreators) / sizeof(ArchiveCreators[0]);
        ++i) {
        tTVPArchive *(*creator)(const ttstr &, tTJSBinaryStream *, bool) =
            ArchiveCreators[i];
        tTVPArchive *archive = creator(name, st, normalizeFileName);
        if(archive)
            return archive;
        st->SetPosition(0);
    }
    delete st;
    return nullptr;
}
//---------------------------------------------------------------------------
int TVPCheckArchive(const ttstr &localname) {
    tTVPArchive *arc = nullptr;
    int validArchive = 2; // archive but no startup.tjs
    try {
        arc = TVPOpenArchive(TVPNormalizeStorageName(localname), false);
        if(arc) {
            tjs_uint count = arc->GetCount();
            ttstr str_startup_tjs = TJS_W("startup.tjs");
            // ttstr str_sys_init_tjs = TJS_W("system/initialize.tjs");
            for(int i = 0; i < count; ++i) {
                ttstr name = arc->GetName(i);
                if(name.length() == str_startup_tjs.length()) {
                    arc->NormalizeInArchiveStorageName(name);
                    if(name == str_startup_tjs) {
                        validArchive = 1;
                        break;
                    }
                }
                // 				else if (name.length() ==
                // str_sys_init_tjs.length()) {
                // 					arc->NormalizeInArchiveStorageName(name);
                // 					if (name == str_sys_init_tjs) {
                // 						validArchive = true;
                // 						break;
                // 					}
                // 				}
            }
        }
    } catch(eTJSError e) {
        // arc = nullptr;
    }
    if(arc) {
        delete arc;
        return validArchive;
    }
    return 0; // not archive
}

/*
    Text stream is used by TJS's Array.saveStruct, Dictionary.saveStruct etc.
    to input/output binary files.
*/

//---------------------------------------------------------------------------
tTJSBinaryStream *TVPCreateBinaryStreamForRead(const ttstr &name,
                                               const ttstr &modestr) {
    // check o mode
    tTJSBinaryStream *stream = TVPCreateStream(name, TJS_BS_READ);

    const tjs_char *o_ofs = TJS_strchr(modestr.c_str(), TJS_W('o'));
    if(o_ofs != NULL) {
        // seek to offset
        o_ofs++;
        tjs_char buf[256];
        int i;
        for(i = 0; i < 255; i++) {
            if(o_ofs[i] >= TJS_W('0') && o_ofs[i] <= TJS_W('9'))
                buf[i] = o_ofs[i];
            else
                break;
        }
        buf[i] = 0;
        tjs_uint64 ofs = ttstr(buf).AsInteger();
        stream->SetPosition(ofs);
    }
    return stream;
}
//---------------------------------------------------------------------------
tTJSBinaryStream *TVPCreateBinaryStreamForWrite(const ttstr &name,
                                                const ttstr &modestr) {
    tTJSBinaryStream *stream;
    // check o mode
    const tjs_char *o_ofs;
    o_ofs = TJS_strchr(modestr.c_str(), TJS_W('o'));
    if(o_ofs != NULL) {
        // seek to offset
        o_ofs++;
        tjs_char buf[256];
        int i;
        for(i = 0; i < 255; i++) {
            if(o_ofs[i] >= TJS_W('0') && o_ofs[i] <= TJS_W('9'))
                buf[i] = o_ofs[i];
            else
                break;
        }
        buf[i] = 0;
        tjs_uint64 ofs = ttstr(buf).AsInteger();
        stream = TVPCreateStream(name, TJS_BS_UPDATE);
        stream->SetPosition(ofs);
    } else {
        stream = TVPCreateStream(name, TJS_BS_WRITE);
    }
    return stream;
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// TVPSelectFile related
//---------------------------------------------------------------------------
#define TVP_OLD_OFN_STRUCT_SIZE 76
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
bool TVPSelectFile(iTJSDispatch2 *params) {
    // show open dialog box
    // NOTE: currently this only shows ANSI version of file open dialog.
    tTJSVariant val;
    std::string initialdir;
    std::string title;
    std::string defaultext;

    std::string filename;
    std::string result;

    // get filter
    if(TJS_SUCCEEDED(params->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("filter"), 0,
                                     &val, params))) {
    }

    // initial dir
    if(TJS_SUCCEEDED(params->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("initialDir"),
                                     0, &val, params))) {
        ttstr lname(val);
        if(!lname.IsEmpty()) {
            TVPGetLocalName(lname);
            initialdir = tTJSNarrowStringHolder(lname.c_str());
        }
    }

    // default extension
    if(TJS_SUCCEEDED(params->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("defaultExt"),
                                     0, &val, params))) {
        defaultext = tTJSNarrowStringHolder(
            val.AsStringNoAddRef()->operator const tjs_char *());
    }

    // filenames
    if(TJS_SUCCEEDED(params->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("name"), 0,
                                     &val, params))) {
        ttstr lname(val);
        if(!lname.IsEmpty()) {
            if(lname.IndexOf('/') >= 0) {
                lname = TVPNormalizeStorageName(lname);
                TVPGetLocalName(lname);
                ttstr path = TVPExtractStoragePath(lname);
                ttstr name = TVPExtractStorageName(lname);
                lname = name;
                initialdir = path.AsStdString();
            } else {
            }

            if(!defaultext.empty() && TVPExtractStorageExt(lname).IsEmpty()) {
                if(defaultext[0] != '.')
                    lname += TJS_W(".");
                lname += defaultext.c_str();
            }
            filename = tTJSNarrowStringHolder(lname.c_str());
        }
    }

    // title
    if(TJS_SUCCEEDED(params->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("title"), 0,
                                     &val, params))) {
        title = tTJSNarrowStringHolder(
            val.AsStringNoAddRef()->operator const tjs_char *());
    }

    // flags
    bool issave = false;
    if(TJS_SUCCEEDED(params->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("save"), 0,
                                     &val, params)))
        issave = val.operator bool();

    // show dialog box
    result = TVPShowFileSelector(title, filename, initialdir, issave);

    if(!result.empty()) {
        // returns some informations

        // filter index
        val = (tjs_int)0;
        params->PropSet(TJS_MEMBERENSURE, TJS_W("filterIndex"), 0, &val,
                        params);

        // file name
        ttstr tresult = TVPNormalizeStorageName(ttstr(result.c_str()));
        val = tresult;
        params->PropSet(TJS_MEMBERENSURE, TJS_W("name"), 0, &val, params);
        return true;
    }

    return false;
}
//---------------------------------------------------------------------------


ttstr ReplaceStringAll(ttstr src, const ttstr &target, const ttstr &dest) {
    src.Replace(target, dest);
    return src;
}

ttstr GetDataPathDirectory(const ttstr &exename) {
    ttstr nativeDataPath = TVPGetAppPath();
    TVPGetLocalName(nativeDataPath);
    nativeDataPath += "/savedata/";
    return nativeDataPath;
}
