// WASM platform core implementation
// Follows the same pattern as windows_core.cpp / android_core.cpp / linux_core.cpp

#include "tjsCommHead.h"

#include "Platform.h"
#include "PlatformFile.h"
#include "PlatformVideo.h"
#include "TVPMsg.h"
#include "TVPApplication.h"
#include "tjsError.h"

#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/threading.h>
#include <sys/stat.h>
#include <unistd.h>

//---------------------------------------------------------------------------
// IDBFS: sync save data to IndexedDB for persistence
//---------------------------------------------------------------------------
EM_JS(void, wasmSyncIDBFS_JS, (), {
    try {
        if (typeof Module === 'undefined' || !Module.FS) return;
        Module.FS.syncfs(false, function(err) {
            if (err) console.log('[idbfs] sync error:', err);
        });
    } catch(e) {
        console.log('[idbfs] sync exception:', e);
    }
});

void wasmSyncSaveData()
{
    // Only sync from the main browser thread (where IDBFS is mounted)
    if (emscripten_is_main_browser_thread())
        wasmSyncIDBFS_JS();
}

//---------------------------------------------------------------------------
// tTVPFileMedia is now in wasm_file.cpp
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
static tjs_int TVPCPUType = 0;
static tjs_int TVPCPUFeatures = 0;
static bool TVPCPUChecked = false;
//---------------------------------------------------------------------------
void TVPGetCPUInfo(tjs_int& cpuType, tjs_int& cpuFeatures)
{
    cpuType = TVPCPUType;
    cpuFeatures = TVPCPUFeatures;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
bool TVP_utime(const char* name, time_t modtime)
{
    // WASM: virtual filesystem does not support utime well; skip.
    return true;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void TVPGetMemoryInfo(TVPMemoryInfo& m)
{
    m.MemTotal = 256 * 1024;     // KB, approximate
    m.MemFree = 128 * 1024;
    m.SwapTotal = 0;
    m.SwapFree = 0;
    m.VirtualTotal = 512 * 1024;
    m.VirtualUsed = 0;
}
tjs_int TVPGetSystemFreeMemory()
{
    return 128;
}
tjs_int TVPGetSelfUsedMemory()
{
    return 0;
}
void TVPRelinquishCPU()
{
#if defined(__EMSCRIPTEN_PTHREADS__)
    sched_yield();
#else
    emscripten_sleep(0);
#endif
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
std::string TVPGetPackageVersionString()
{
    return "wasm";
}
ttstr TVPGetOSName()
{
    return TJS_N("WASM/Emscripten");
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void TVPPreNormalizeStorageName(ttstr& name)
{
    // if the name is an OS's native expression, change it according
    // with the TVP storage system naming rule.
    tjs_int namelen = name.GetLen();
    if (namelen == 0)
        return;
    if (namelen >= 1)
    {
        if (name[0] == TJS_N('/'))
        {
            name = ttstr(TJS_N("file://.")) + name;
            return;
        }
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void TVPInvokeMenu(int x, int y, void* menu)
{
    // WASM: no native menu support
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
tTVPMemoryStream* GetResourceStream(const ttstr& filename)
{
    // WASM: /Res/ files are preloaded into MEMFS via --preload-file.
    // Read directly with fopen/fread instead of going through HTTP Range.
    ttstr path(TJS_N("/Res/"));
    path += filename;
    std::string p = path.AsStdString();
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return nullptr;
    fseeko(f, 0, SEEK_END);
    off_t sz = ftello(f);
    fseeko(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return nullptr; }
    tTVPMemoryStream* ret = new tTVPMemoryStream(nullptr, (tjs_uint64)sz);
    fread(ret->GetInternalBuffer(), 1, (size_t)sz, f);
    fclose(f);
    return ret;
}
//---------------------------------------------------------------------------