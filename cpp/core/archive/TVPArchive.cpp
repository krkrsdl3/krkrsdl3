#include "tjsCommHead.h"
#include "TVPArchive.h"

#include "TVPMsg.h"
#include "TVPSystem.h"
#include "tjsUtils.h"
#include "TVPStorage.h"
#include "Platform.h"

//---------------------------------------------------------------------------
// tTVPArchive
//---------------------------------------------------------------------------
void tTVPArchive::NormalizeInArchiveStorageName(ttstr& name)
{
    // normalization of in-archive storage name does :
    if (name.IsEmpty())
        return;

    // make all characters small
    // change '\\' to '/'
    tjs_char* ptr = name.Independ();
    while (*ptr)
    {
        if (*ptr >= TJS_N('A') && *ptr <= TJS_N('Z'))
            *ptr += TJS_N('a') - TJS_N('A');
        else if (*ptr == TJS_N('\\'))
            *ptr = TJS_N('/');
        ptr++;
    }

    // eliminate duplicated slashes
    ptr = name.Independ();
    tjs_char* org_ptr = ptr;
    tjs_char* dest = ptr;
    while (*ptr)
    {
        if (*ptr != TJS_N('/'))
        {
            *dest = *ptr;
            ptr++;
            dest++;
        }
        else
        {
            if (ptr != org_ptr)
            {
                *dest = *ptr;
                ptr++;
                dest++;
            }
            while (*ptr == TJS_N('/'))
                ptr++;
        }
    }
    *dest = 0;

    name.FixLen();
}
//---------------------------------------------------------------------------
void tTVPArchive::AddToHash()
{
    // enter all names to the hash table
    tjs_uint Count = GetCount();
    tjs_uint i;
    for (i = 0; i < Count; i++)
    {
        ttstr name = GetName(i);
        NormalizeInArchiveStorageName(name);
        Hash.Add(name, i);
    }
}
//---------------------------------------------------------------------------
tTJSBinaryStream* tTVPArchive::CreateStream(const ttstr& name)
{
    if (name.IsEmpty())
        return NULL;

    if (!Init)
    {
        Init = true;
        AddToHash();
    }

    tjs_uint* p = Hash.Find(name);
    if (!p)
        TVPThrowExceptionMessage(TVPStorageInArchiveNotFound, name, ArchiveName);

    return CreateStreamByIndex(*p);
}
//---------------------------------------------------------------------------
bool tTVPArchive::IsExistent(const ttstr& name)
{
    if (name.IsEmpty())
        return false;

    if (!Init)
    {
        Init = true;
        AddToHash();
    }

    return Hash.Find(name) != NULL;
}
//---------------------------------------------------------------------------
tjs_int tTVPArchive::GetFirstIndexStartsWith(const ttstr& prefix)
{
    // returns first index which have 'prefix' at start of the name.
    // returns -1 if the target is not found.
    // the item must be sorted by ttstr::operator < , otherwise this function
    // will not work propertly.
    tjs_uint total_count = GetCount();
    tjs_int s = 0, e = total_count;
    while (e - s > 1)
    {
        tjs_int m = (e + s) / 2;
        if (!(GetName(m) < prefix))
        {
            // m is after or at the target
            e = m;
        }
        else
        {
            // m is before the target
            s = m;
        }
    }

    // at this point, s or s+1 should point the target.
    // be certain.
    if (s >= (tjs_int)total_count)
        return -1; // out of the index
    if (GetName(s).StartsWith(prefix))
        return s;
    s++;
    if (s >= (tjs_int)total_count)
        return -1; // out of the index
    if (GetName(s).StartsWith(prefix))
        return s;
    return -1;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPXP3ArchiveHandleCache
//---------------------------------------------------------------------------
#define TVP_MAX_ARCHIVE_HANDLE_CACHE 8
static tjs_uint TVPArchiveHandleCacheAge = 0;
struct tTVPArchiveHandleCacheItem
{
    void* Pointer;
    tTJSBinaryStream* Stream;
    tjs_uint Age;
};
//---------------------------------------------------------------------------
static tTVPArchiveHandleCacheItem* TVPArchiveHandleCachePool = NULL;
static bool TVPArchiveHandleCacheInit = false;
static bool TVPArchiveHandleCacheShutdown = false;
static tTJSCriticalSection TVPArchiveHandleCacheCS;
//---------------------------------------------------------------------------
tTJSBinaryStream* TVPGetCachedArchiveHandle(void* pointer, const ttstr& name)
{
    // get cached archive file handle from the pool
    if (TVPArchiveHandleCacheShutdown)
    {
        // the pool has shutdown
        return TVPCreateStream(name);
    }

    tTJSCriticalSectionHolder cs_holder(TVPArchiveHandleCacheCS);

    if (!TVPArchiveHandleCacheInit)
    {
        // initialize the pool
        TVPArchiveHandleCachePool = new tTVPArchiveHandleCacheItem[TVP_MAX_ARCHIVE_HANDLE_CACHE];
        for (tjs_int i = 0; i < TVP_MAX_ARCHIVE_HANDLE_CACHE; i++)
        {
            TVPArchiveHandleCachePool[i].Pointer = NULL;
            TVPArchiveHandleCachePool[i].Stream = NULL;
            TVPArchiveHandleCachePool[i].Age = 0;
        }
        TVPArchiveHandleCacheInit = true;
    }

    // linear search wiil be enough here because the
    // TVP_MAX_ARCHIVE_HANDLE_CACHE is relatively small
    for (tjs_int i = 0; i < TVP_MAX_ARCHIVE_HANDLE_CACHE; i++)
    {
        tTVPArchiveHandleCacheItem* item = TVPArchiveHandleCachePool + i;
        if (item->Stream && item->Pointer == pointer)
        {
            // found in the pool
            tTJSBinaryStream* stream = item->Stream;
            item->Stream = NULL;
            return stream;
        }
    }

    // not found in the pool
    // simply create a stream and return it
    return TVPCreateStream(name);
}
//---------------------------------------------------------------------------
void TVPReleaseCachedArchiveHandle(void* pointer, tTJSBinaryStream* stream)
{
    // release archive file handle
    if (TVPArchiveHandleCacheShutdown)
        return;
    if (!TVPArchiveHandleCacheInit)
        return;

    tTJSCriticalSectionHolder cs_holder(TVPArchiveHandleCacheCS);

    // search empty cell in the pool
    tjs_uint oldest_age = 0;
    tjs_int oldest = 0;
    for (tjs_int i = 0; i < TVP_MAX_ARCHIVE_HANDLE_CACHE; i++)
    {
        tTVPArchiveHandleCacheItem* item = TVPArchiveHandleCachePool + i;
        if (item->Stream == NULL)
        {
            // found the empty cell; fill it
            item->Pointer = pointer;
            item->Stream = stream;
            item->Age = ++TVPArchiveHandleCacheAge;
            // counter overflow in TVPArchiveHandleCacheAge
            // is not so a big problem.
            // counter overflow can worsen the cache performance,
            // but it occurs only when the counter is overflowed
            // (it's too far less than usual)
            return;
        }

        if (i == 0 || oldest_age > item->Age)
        {
            oldest_age = item->Age;
            oldest = i;
        }
    }

    // empty cell not found
    // free oldest cell and fill it
    tTVPArchiveHandleCacheItem* oldest_item = TVPArchiveHandleCachePool + oldest;
    delete oldest_item->Stream, oldest_item->Stream = NULL;
    oldest_item->Pointer = pointer;
    oldest_item->Stream = stream;
    oldest_item->Age = ++TVPArchiveHandleCacheAge;
}
//---------------------------------------------------------------------------
void TVPFreeArchiveHandlePoolByPointer(void* pointer)
{
    // free all streams which have specified pointer
    if (TVPArchiveHandleCacheShutdown)
        return;
    if (!TVPArchiveHandleCacheInit)
        return;

    tTJSCriticalSectionHolder cs_holder(TVPArchiveHandleCacheCS);

    for (tjs_int i = 0; i < TVP_MAX_ARCHIVE_HANDLE_CACHE; i++)
    {
        tTVPArchiveHandleCacheItem* item = TVPArchiveHandleCachePool + i;
        if (item->Stream && item->Pointer == pointer)
        {
            delete item->Stream, item->Stream = NULL;
            item->Pointer = NULL;
            item->Age = 0;
        }
    }
}
//---------------------------------------------------------------------------
void TVPFreeArchiveHandlePool()
{
    // free all streams
    if (TVPArchiveHandleCacheShutdown)
        return;
    if (!TVPArchiveHandleCacheInit)
        return;

    tTJSCriticalSectionHolder cs_holder(TVPArchiveHandleCacheCS);

    for (tjs_int i = 0; i < TVP_MAX_ARCHIVE_HANDLE_CACHE; i++)
    {
        tTVPArchiveHandleCacheItem* item = TVPArchiveHandleCachePool + i;
        if (item->Stream)
        {
            delete item->Stream, item->Stream = NULL;
            item->Pointer = NULL;
            item->Age = 0;
        }
    }
}
//---------------------------------------------------------------------------
void TVPShutdownArchiveHandleCache()
{
    // free all stream and shutdown the pool
    tTJSCriticalSectionHolder cs_holder(TVPArchiveHandleCacheCS);

    TVPArchiveHandleCacheShutdown = true;
    if (!TVPArchiveHandleCacheInit)
        return;

    for (tjs_int i = 0; i < TVP_MAX_ARCHIVE_HANDLE_CACHE; i++)
    {
        if (TVPArchiveHandleCachePool[i].Stream)
            delete TVPArchiveHandleCachePool[i].Stream;
    }
    delete[] TVPArchiveHandleCachePool;
    TVPArchiveHandleCachePool = NULL;
    TVPArchiveHandleCacheInit = false;
}
//---------------------------------------------------------------------------
static tTVPAtExit TVPShutdownArchiveCacheAtExit(TVP_ATEXIT_PRI_CLEANUP,
                                                TVPShutdownArchiveHandleCache);
//---------------------------------------------------------------------------
#ifdef _KRKRSDL3_OHOS
void TVPResetArchiveHandleCacheForReentry()
{
    // Reset the cache state flags so the pool is freshly allocated on re-entry.
    // TVPShutdownArchiveHandleCache already deleted the pool and set Init=false;
    // we just need to re-enable the cache by clearing the shutdown flag.
    tTJSCriticalSectionHolder cs_holder(TVPArchiveHandleCacheCS);
    TVPArchiveHandleCacheShutdown = false;
    TVPArchiveHandleCacheInit = false;
    TVPArchiveHandleCachePool = NULL;
}
#endif
//---------------------------------------------------------------------------

TArchiveStream::TArchiveStream(tTVPArchive* owner, tjs_uint64 off, tjs_uint64 len)
  : Owner(owner),
    StartPos(off),
    DataLength(len)
{
    Owner->AddRef();
    _instr = TVPGetCachedArchiveHandle(Owner, Owner->ArchiveName);
    CurrentPos = 0;
    _instr->SetPosition(off);
}

TArchiveStream::~TArchiveStream()
{
    TVPReleaseCachedArchiveHandle(Owner, _instr);
    Owner->Release();
}

void storeFilename(ttstr& name, const char* narrowName, const ttstr& filename)
{
    tjs_int len = TJS_narrowtowidelen(narrowName);
    if (len == -1)
    {
        ttstr msg("Filename is not encoded in UTF8 in archive:\n");
        TVPShowSimpleMessageBox(msg + filename, TJS_N("Error"));
        TVPThrowExceptionMessage(TJS_N("Invalid archive entry name"));
    }
    tjs_char* p = name.AllocBuffer(len);
    name = narrowName;
    name.FixLen();
}