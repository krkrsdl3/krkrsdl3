#include "tjsCommHead.h"
#include "TVPSystem.h"

#include "TVPScript.h"
#include "TVPDebug.h"
#include "TVPMsg.h"
#include "TVPStorage.h"
#include "TVPEvent.h"
#include "TVPApplication.h"
#include "TVPGraphicsLoader.h"

#include "TickCount.h"
#include "Random.h"
#include "tvpinputdefs.h"
#include "SystemControl.h"
#include "FilePathUtil.h"
#include "Platform.h"
#include "TVPConfig.h"
#include "DetectCPU.h"
#include "XP3Archive.h"
#include <thread>
#ifdef _KRKRSDL3_OHOS
#include <SDL2/SDL.h>
#endif

#include "tjsLex.h"
#include "tjsNativeLayer.h"

//---------------------------------------------------------------------------
// TVPFireOnApplicationActivateEvent
//---------------------------------------------------------------------------
void TVPFireOnApplicationActivateEvent(bool activate_or_deactivate)
{
    // get the script engine
    tTJS* engine = TVPGetScriptEngine();
    if (!engine)
        return; // the script engine had been shutdown

    // get System.onActivate or System.onDeactivate
    // and call it.
    iTJSDispatch2* global = TVPGetScriptEngine()->GetGlobalNoAddRef();
    if (!global)
        return;

    tTJSVariant val;
    tTJSVariant val2;
    tTJSVariantClosure clo;
    tTJSVariantClosure func;

    try
    {
        tjs_error er;
        er = global->PropGet(TJS_MEMBERMUSTEXIST, TJS_N("System"), NULL, &val, global);
        if (TJS_FAILED(er))
            return;

        if (val.Type() != tvtObject)
            return;

        clo = val.AsObjectClosureNoAddRef();

        if (clo.Object == NULL)
            return;

        clo.PropGet(TJS_MEMBERMUSTEXIST,
                    activate_or_deactivate ? TJS_N("onActivate") : TJS_N("onDeactivate"), NULL,
                    &val2, NULL);

        if (val2.Type() != tvtObject)
            return;

        func = val2.AsObjectClosureNoAddRef();
    }
    catch (const eTJS& e)
    {
        // the system should not throw exceptions during retrieving the function
        TVPAddLog(
            TVPFormatMessage(TVPErrorInRetrievingSystemOnActivateOnDeactivate, e.GetMessage()));
        return;
    }

    if (func.Object != NULL)
        func.FuncCall(0, NULL, NULL, NULL, 0, NULL, NULL);
}
//---------------------------------------------------------------------------

bool TVPGetKeyMouseAsyncState(tjs_uint keycode, bool getcurrent);
//---------------------------------------------------------------------------
// TVPGetAsyncKeyState
//---------------------------------------------------------------------------
bool TVPGetAsyncKeyState(tjs_uint keycode, bool getcurrent)
{
    // get keyboard state asynchronously.
    // return current key state if getcurrent is true.
    // otherwise, return whether the key is pushed during previous call of
    // TVPGetAsyncKeyState at the same keycode.

    if (keycode >= VK_PAD_FIRST && keycode <= VK_PAD_LAST)
    {
        // JoyPad related keys are treated in DInputMgn.cpp
        return TVPGetJoyPadAsyncState(keycode, getcurrent);
    }

    return TVPGetKeyMouseAsyncState(keycode, getcurrent);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPGetOSBits
//---------------------------------------------------------------------------
tjs_int TVPGetOSBits()
{
    return 64;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPShellExecute
//---------------------------------------------------------------------------
bool TVPShellExecute(const ttstr& target, const ttstr& param)
{
    // open or execute target file
    //	ttstr file = TVPGetNativeName(TVPNormalizeStorageName(target));
    return true;
}
//---------------------------------------------------------------------------

static tTJSVariant RegisterData;
ttstr TVPGetAppDataPath();
void TVPExecuteStorage(const ttstr& name,
                       tTJSVariant* result,
                       bool isexpression,
                       const tjs_char* modestr);
static void InitRegisterData()
{
    static bool dataInited = false;
    if (!dataInited)
    {
        ttstr regfile = TVPGetAppDataPath() + TJS_N("RegisterData.tjs");
        if (TVPIsExistentStorageNoSearch(regfile))
        {
            TVPExecuteStorage(regfile, &RegisterData, true, TJS_N(""));
        }
    }
}

//---------------------------------------------------------------------------
// TVPReadRegValue
//---------------------------------------------------------------------------
void TVPReadRegValue(tTJSVariant& result, const ttstr& key)
{
    // open specified registry key
    if (key.IsEmpty())
    {
        result.Clear();
        return;
    }

    // check whether the key contains root key name
    // HKEY root = HKEY_CURRENT_USER;
    const tjs_char* key_p = key.c_str();

    InitRegisterData();
    // search value name
    tTJSVariant CurrentNode = RegisterData;
    const tjs_char* start = key_p;
    while (*start && CurrentNode.Type() != tvtObject)
    {
        iTJSDispatch2* pObj;

        switch (*key_p)
        {
            case '\\':
            case '/':
                ++key_p;
            case '\0':
                start = key_p;
                if (CurrentNode.Type() != tvtObject)
                {
                    CurrentNode.Clear();
                    break;
                }
                pObj = CurrentNode.AsObject();
                if (!pObj)
                {
                    CurrentNode.Clear();
                    break;
                }
                if (!TJS_SUCCEEDED(pObj->PropGet(TJS_MEMBERMUSTEXIST,
                                                 ttstr(start, key_p - start - 1).c_str(), 0,
                                                 &CurrentNode, pObj)))
                {
                    CurrentNode.Clear();
                    break;
                }
                start = key_p;
                continue;
            default:
                ++key_p;
                continue;
        }
    }
    if (*start)
    {
        CurrentNode.Clear();
        return;
    }
    result = CurrentNode;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPGetPersonalPath
//---------------------------------------------------------------------------
ttstr TVPGetPersonalPath()
{
    return TVPGetAppPath();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPGetAppDataPath
//---------------------------------------------------------------------------
ttstr TVPGetAppDataPath()
{
    return TVPGetAppPath();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPGetSavedGamesPath
//---------------------------------------------------------------------------
ttstr TVPGetSavedGamesPath()
{
    return TVPGetAppPath();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPCreateAppLock
//---------------------------------------------------------------------------
bool TVPCreateAppLock(const ttstr& lockname)
{

    // No need to release the mutex object because the mutex is automatically
    // released when the calling thread exits.

    return true;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
enum tTVPTouchDevice
{
    tdNone = 0,
    tdIntegratedTouch = 0x00000001,
    tdExternalTouch = 0x00000002,
    tdIntegratedPen = 0x00000004,
    tdExternalPen = 0x00000008,
    tdMultiInput = 0x00000040,
    tdDigitizerReady = 0x00000080,
    tdMouse = 0x00000100,
    tdMouseWheel = 0x00000200
};
/**
 * �^�b�`�f�o�C�X(�ƃ}�E�X)�̐ڑ���Ԃ��擾����
 **/
int TVPGetSupportTouchDevice()
{
    int result = 0;
    result |= tdMouse;
    result |= tdMouseWheel;
    return result;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// System.onActivate and System.onDeactivate related
//---------------------------------------------------------------------------
static void TVPOnApplicationActivate(bool activate_or_deactivate);
//---------------------------------------------------------------------------
class tTVPOnApplicationActivateEvent : public tTVPBaseInputEvent
{
    static tTVPUniqueTagForInputEvent Tag;
    bool ActivateOrDeactivate; // true for activate; otherwise deactivate
public:
    tTVPOnApplicationActivateEvent(bool activate_or_deactivate)
      : tTVPBaseInputEvent(Application, Tag),
        ActivateOrDeactivate(activate_or_deactivate){};
    void Deliver() const { TVPOnApplicationActivate(ActivateOrDeactivate); }
};
tTVPUniqueTagForInputEvent tTVPOnApplicationActivateEvent::Tag;
//---------------------------------------------------------------------------
void TVPPostApplicationActivateEvent()
{
    TVPPostInputEvent(new tTVPOnApplicationActivateEvent(true), TVP_EPT_REMOVE_POST);
}
//---------------------------------------------------------------------------
void TVPPostApplicationDeactivateEvent()
{
    TVPPostInputEvent(new tTVPOnApplicationActivateEvent(false), TVP_EPT_REMOVE_POST);
}
//---------------------------------------------------------------------------
static void TVPOnApplicationActivate(bool activate_or_deactivate)
{
    // called by event system, to fire System.onActivate or
    // System.onDeactivate event
    if (!TVPSystemControlAlive)
        return;

    // check the state again (because the state may change during the event delivering).
    // but note that this implementation might fire activate events even in the application
    // is already activated (the same as deactivation).
    if (activate_or_deactivate != Application->GetActivating())
        return;

    // fire the event
    TVPFireOnApplicationActivateEvent(activate_or_deactivate);
}
//---------------------------------------------------------------------------

bool TVPAutoSaveBookMark = false;
extern void TVPDoSaveSystemVariables()
{
    try
    {
        // hack for save system variable
        iTJSDispatch2* global = TVPGetScriptDispatch();
        if (!global)
            return;
        tTJSVariant var;
        if (global->PropGet(0, TJS_N("kag"), nullptr, &var, global) == TJS_S_OK &&
            var.Type() == tvtObject)
        {
            iTJSDispatch2* kag = var.AsObjectNoAddRef();
            if (kag->PropGet(0, TJS_N("saveSystemVariables"), nullptr, &var, kag) == TJS_S_OK)
            {
                iTJSDispatch2* fn = var.AsObjectNoAddRef();
                if (fn->IsInstanceOf(0, 0, 0, TJS_N("Function"), fn))
                {
                    tTJSVariant* args = nullptr;
                    fn->FuncCall(0, nullptr, nullptr, nullptr, 0, &args, kag);
                }
            }
            if (TVPAutoSaveBookMark &&
                kag->PropGet(0, TJS_N("saveBookMark"), nullptr, &var, kag) == TJS_S_OK &&
                var.Type() == tvtObject)
            {
                iTJSDispatch2* fn = var.AsObjectNoAddRef();
                if (fn->IsInstanceOf(0, 0, 0, TJS_N("Function"), fn))
                {
                    tTJSVariant num((tjs_int32)0);
                    tTJSVariant* args = &num;
                    fn->FuncCall(0, nullptr, nullptr, nullptr, 1, &args, kag);
                }
            }
        }
    }
    catch (...)
    {
        ;
    }
}

//---------------------------------------------------------------------------
// global data
//---------------------------------------------------------------------------
ttstr TVPProjectDir; // project directory (in unified storage name)
ttstr TVPDataPath;   // data directory (in unified storage name)
//---------------------------------------------------------------------------

extern void TVPGL_C_Init();
//---------------------------------------------------------------------------
// TVPSystemInit : Entire System Initialization
//---------------------------------------------------------------------------
void TVPSystemInit(void)
{
#ifdef _KRKRSDL3_OHOS
    SDL_Log("[krkr2-diag] >>> TVPSystemInit entered");
#endif
    TVPBeforeSystemInit();

    TVPInitScriptEngine();

    TVPInitTVPGL();

    TVPAfterSystemInit();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPSystemUninit : System shutdown, cleanup, etc...
//---------------------------------------------------------------------------
static void TVPCauseAtExit();
bool TVPSystemUninitCalled = false;
void TVPSystemUninit(void)
{
    if (TVPSystemUninitCalled)
        return;
    TVPSystemUninitCalled = true;

    TVPBeforeSystemUninit();

    TVPUninitTVPGL();

    try
    {
        TVPUninitScriptEngine();
    }
    catch (...)
    {
        // ignore errors
    }

    TVPAfterSystemUninit();

    TVPCauseAtExit();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPAddAtExitHandler related
//---------------------------------------------------------------------------
struct tTVPAtExitInfo
{
    tTVPAtExitInfo(tjs_int pri, void (*handler)()) { Priority = pri, Handler = handler; }

    tjs_int Priority;
    void (*Handler)();

    bool operator<(const tTVPAtExitInfo& r) const { return this->Priority < r.Priority; }
    bool operator>(const tTVPAtExitInfo& r) const { return this->Priority > r.Priority; }
    bool operator==(const tTVPAtExitInfo& r) const { return this->Priority == r.Priority; }
};
static std::vector<tTVPAtExitInfo>* TVPAtExitInfos = NULL;
static bool TVPAtExitShutdown = false;
//---------------------------------------------------------------------------
void TVPAddAtExitHandler(tjs_int pri, void (*handler)())
{
    if (TVPAtExitShutdown)
        return;

    if (!TVPAtExitInfos)
        TVPAtExitInfos = new std::vector<tTVPAtExitInfo>();
    TVPAtExitInfos->push_back(tTVPAtExitInfo(pri, handler));
}
//---------------------------------------------------------------------------
static void TVPCauseAtExit()
{
    if (TVPAtExitShutdown)
        return;
    TVPAtExitShutdown = true;

    if (!TVPAtExitInfos)
        return;

    std::sort(TVPAtExitInfos->begin(), TVPAtExitInfos->end()); // ascending sort by priority

    std::vector<tTVPAtExitInfo>::iterator i;
    for (i = TVPAtExitInfos->begin(); i != TVPAtExitInfos->end(); i++)
    {
#ifdef _KRKRSDL3_OHOS
        // On HarmonyOS, isolate each handler so that one crash does not
        // prevent subsequent thread-join handlers from running.
        // Without this, a throw in an early handler (e.g. PREPARE priority)
        // would skip SHUTDOWN handlers that join background threads,
        // leaving dangling pthreads that SIGSEGV when the .so is unmapped.
        try {
            i->Handler();
        } catch (...) {
            // Ignore — best-effort cleanup
        }
#else
        i->Handler();
#endif
    }

    // Do NOT delete TVPAtExitInfos here: on HarmonyOS the .so stays loaded
    // across game launches and static tTVPAtExit constructors only run once.
    // Keeping the vector alive allows the same handlers to fire on re-entry.
}

#ifdef _KRKRSDL3_OHOS
// Exposed so runner_main can call this as a safety net after TVPSystemUninit.
// Safe to call multiple times — guarded by TVPAtExitShutdown flag.
void TVPForceCauseAtExit()
{
    TVPCauseAtExit();
}
#endif
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// global data
//---------------------------------------------------------------------------
ttstr TVPNativeProjectDir;
ttstr TVPNativeDataPath;
bool TVPProjectDirSelected = false;
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPResetSystemForReentry - reset all one-shot flags for OHOS re-entry
//---------------------------------------------------------------------------
#ifdef _KRKRSDL3_OHOS
void TVPResetSystemForReentry()
{
    TVPSystemUninitCalled = false;
    TVPAtExitShutdown = false;
    // TVPAtExitInfos is intentionally kept — static tTVPAtExit constructors
    // only run once per .so load, so the registered handlers must survive.
    TVPTerminated = false;
    TVPTerminateCode = 0;
    TVPTerminateOnWindowClose = true;
    TVPTerminateOnNoWindowStartup = true;
    TVPProjectDir.Clear();
    TVPDataPath.Clear();
    TVPNativeProjectDir.Clear();
    TVPNativeDataPath.Clear();
    TVPProjectDirSelected = false;
}
#endif
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// System security options
//---------------------------------------------------------------------------
// system security options are held inside the executable, where
// signature checker will refer. This enables the signature checker
// (or other security modules like XP3 encryption module) to check
// the changes which is not intended by the contents author.
const static char TVPSystemSecurityOptions[] =
    "-- TVPSystemSecurityOptions disablemsgmap(0):forcedataxp3(0):acceptfilenameargument(0) --";
//---------------------------------------------------------------------------
int GetSystemSecurityOption(const char* name)
{
    size_t namelen = TJS_nstrlen(name);
    const char* p = TJS_nstrstr(TVPSystemSecurityOptions, name);
    if (!p)
        return 0;
    if (p[namelen] == '(' && p[namelen + 2] == ')')
        return p[namelen + 1] - '0';
    return 0;
}
//---------------------------------------------------------------------------

#ifdef TVP_REPORT_HW_EXCEPTION
//---------------------------------------------------------------------------
// Hardware Exception Report Related
//---------------------------------------------------------------------------
// TVP's Hardware Exception Report comes with hacking RTL source.
// insert following code into rtl/soruce/except/xx.cpp
/*
typedef void __cdecl (*__dee_hacked_getExceptionObjectHook_type)(int ErrorCode,
                EXCEPTION_RECORD *P, unsigned long osEsp, unsigned long osERR, PCONTEXT ctx);
static __dee_hacked_getExceptionObjectHook_type __dee_hacked_getExceptionObjectHook = NULL;

extern "C"
{
        __dee_hacked_getExceptionObjectHook_type
                __cdecl __dee_hacked_set_getExceptionObjectHook(
                __dee_hacked_getExceptionObjectHook_type handler)
        {
                __dee_hacked_getExceptionObjectHook_type oldhandler;
                oldhandler = __dee_hacked_getExceptionObjectHook;
                __dee_hacked_getExceptionObjectHook = handler;
                return oldhandler;
        }
}
*/
// and insert following code into getExceptionObject
/*
        if(__dee_hacked_getExceptionObjectHook)
                __dee_hacked_getExceptionObjectHook(ErrorCode, P, osEsp, osERR, ctx);
*/
//---------------------------------------------------------------------------
/*
typedef void __cdecl (*__dee_hacked_getExceptionObjectHook_type)(int ErrorCode,
                EXCEPTION_RECORD *P, unsigned long osEsp, unsigned long osERR, PCONTEXT ctx);
extern "C"
{
        extern __dee_hacked_getExceptionObjectHook_type
                __cdecl __dee_hacked_set_getExceptionObjectHook(
                __dee_hacked_getExceptionObjectHook_type handler);
}
*/

//---------------------------------------------------------------------------
// data
#define TVP_HWE_MAX_CODES_AT_EIP 96
#define TVP_HWE_MAX_STACK_AT_ESP 80
#define TVP_HWE_MAX_STACK_DATA_DUMP 16
#define TVP_HWE_MAX_CALL_TRACE 32
#define TVP_HWE_MAX_CALL_CODE_DUMP 26
static bool TVPHWExcRaised = false;
struct tTVPHWExceptionData
{
    tjs_int Code;
    tjs_uint8* EIP;
    tjs_uint32* ESP;
    ULONG_PTR AccessFlag;     // for EAccessViolation (0=read, 1=write, 8=execute)
    void* AccessTarget;       // for EAccessViolation
    CONTEXT Context;          // OS exception context
    wchar_t Module[MAX_PATH]; // module name which caused the exception

    tjs_uint8 CodesAtEIP[TVP_HWE_MAX_CODES_AT_EIP];
    tjs_int CodesAtEIPLen;
    void* StackAtESP[TVP_HWE_MAX_STACK_AT_ESP];
    tjs_int StackAtESPLen;
    tjs_uint8 StackDumps[TVP_HWE_MAX_STACK_AT_ESP][TVP_HWE_MAX_STACK_DATA_DUMP];
    tjs_int StackDumpsLen[TVP_HWE_MAX_STACK_AT_ESP];

    void* CallTrace[TVP_HWE_MAX_CALL_TRACE];
    tjs_int CallTraceLen;
    tjs_uint8 CallTraceDumps[TVP_HWE_MAX_CALL_TRACE][TVP_HWE_MAX_CALL_CODE_DUMP];
    tjs_int CallTraceDumpsLen[TVP_HWE_MAX_CALL_TRACE];
};
static tTVPHWExceptionData TVPLastHWExceptionData;

HANDLE TVPHWExceptionLogHandle = NULL;
//---------------------------------------------------------------------------
static wchar_t TVPHWExceptionLogFilename[MAX_PATH];

static void TVPWriteHWELogFile()
{
    TVPEnsureDataPathDirectory();
    TJS_strcpy(TVPHWExceptionLogFilename, TVPNativeDataPath.c_str());
    TJS_strcat(TVPHWExceptionLogFilename, L"hwexcept.log");
    TVPHWExceptionLogHandle = CreateFile(TVPHWExceptionLogFilename, GENERIC_WRITE, FILE_SHARE_READ,
                                         NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (TVPHWExceptionLogHandle == INVALID_HANDLE_VALUE)
        return;
    DWORD filesize;
    filesize = GetFileSize(TVPHWExceptionLogHandle, NULL);
    SetFilePointer(TVPHWExceptionLogHandle, filesize, NULL, FILE_BEGIN);

    // write header
    const wchar_t headercomment[] =
        L"THIS IS A HARDWARE EXCEPTION LOG FILE OF KIRIKIRI. "
        L"PLEASE SEND THIS FILE TO THE AUTHOR WITH *.console.log FILE. ";
    DWORD written = 0;
    for (int i = 0; i < 4; i++)
        WriteFile(TVPHWExceptionLogHandle, L"----", 4 * sizeof(wchar_t), &written, NULL);
    WriteFile(TVPHWExceptionLogHandle, headercomment, sizeof(headercomment) - 1, &written, NULL);
    for (int i = 0; i < 4; i++)
        WriteFile(TVPHWExceptionLogHandle, L"----", 4 * sizeof(wchar_t), &written, NULL);

    // write version
    WriteFile(TVPHWExceptionLogHandle, &TVPVersionMajor, sizeof(TVPVersionMajor), &written, NULL);
    WriteFile(TVPHWExceptionLogHandle, &TVPVersionMinor, sizeof(TVPVersionMinor), &written, NULL);
    WriteFile(TVPHWExceptionLogHandle, &TVPVersionRelease, sizeof(TVPVersionRelease), &written,
              NULL);
    WriteFile(TVPHWExceptionLogHandle, &TVPVersionBuild, sizeof(TVPVersionBuild), &written, NULL);

    // write tTVPHWExceptionData
    WriteFile(TVPHWExceptionLogHandle, &TVPLastHWExceptionData, sizeof(TVPLastHWExceptionData),
              &written, NULL);

    // close the handle
    if (TVPHWExceptionLogHandle != INVALID_HANDLE_VALUE)
        CloseHandle(TVPHWExceptionLogHandle);
}
//---------------------------------------------------------------------------
// void __cdecl TVP__dee_hacked_getExceptionObjectHook(int ErrorCode,
//		EXCEPTION_RECORD *P, unsigned long osEsp, unsigned long osERR, PCONTEXT ctx)
#ifdef TJS_64BIT_OS
void TVPHandleSEHException(int ErrorCode,
                           EXCEPTION_RECORD* P,
                           unsigned long long osEsp,
                           PCONTEXT ctx)
#else
void TVPHandleSEHException(int ErrorCode, EXCEPTION_RECORD* P, unsigned long osEsp, PCONTEXT ctx)
#endif
{
    // exception hook function
    int len;
    tTVPHWExceptionData* d = &TVPLastHWExceptionData;

    d->Code = ErrorCode;

    // get AccessFlag and AccessTarget
    if (d->Code == 11) // EAccessViolation
    {
        d->AccessFlag = P->ExceptionInformation[0];
        d->AccessTarget = (void*)P->ExceptionInformation[1];
    }

    // get OS context
    if (!IsBadReadPtr(ctx, sizeof(*ctx)))
    {
        memcpy(&(d->Context), ctx, sizeof(*ctx));
    }
    else
    {
        memset(&(d->Context), 0, sizeof(*ctx));
    }

    // get codes at eip
    d->EIP = (tjs_uint8*)P->ExceptionAddress;
    len = TVP_HWE_MAX_CODES_AT_EIP;

    while (len)
    {
        if (!IsBadReadPtr(d->EIP, len))
        {
            memcpy(d->CodesAtEIP, d->EIP, len);
            d->CodesAtEIPLen = len;
            break;
        }
        len--;
    }

    // get module name
    MEMORY_BASIC_INFORMATION mbi;
    VirtualQuery(d->EIP, &mbi, sizeof(mbi));
    if (mbi.State == MEM_COMMIT)
    {
        if (!GetModuleFileName((HMODULE)mbi.AllocationBase, d->Module, MAX_PATH))
        {
            d->Module[0] = 0;
        }
    }
    else
    {
        d->Module[0] = 0;
    }

    // get stack at esp
    d->ESP = (tjs_uint32*)osEsp;
    len = TVP_HWE_MAX_STACK_AT_ESP;

    while (len)
    {
        if (!IsBadReadPtr(d->ESP, len * sizeof(tjs_uint32)))
        {
            memcpy(d->StackAtESP, d->ESP, len * sizeof(tjs_uint32));
            d->StackAtESPLen = len;
            break;
        }
        len--;
    }

    // get data pointed by each stack data
    for (tjs_int i = 0; i < d->StackAtESPLen; i++)
    {
        void* base = d->StackAtESP[i];
        len = TVP_HWE_MAX_STACK_DATA_DUMP;
        while (len)
        {
            if (!IsBadReadPtr(base, len))
            {
                memcpy(d->StackDumps[i], base, len);
                d->StackDumpsLen[i] = len;
                break;
            }
            len--;
        }
    }

    // get call trace at esp
    d->CallTraceLen = 0;
    tjs_int p = 0;
    while (d->CallTraceLen < TVP_HWE_MAX_CALL_TRACE)
    {
        if (IsBadReadPtr(d->ESP + p, sizeof(tjs_uint32)))
            break;

        if (!IsBadReadPtr((void*)d->ESP[p], 4))
        {
            VirtualQuery((void*)d->ESP[p], &mbi, sizeof(mbi));
            if (mbi.State == MEM_COMMIT)
            {
                wchar_t module[MAX_PATH];
                if (::GetModuleFileName((HMODULE)mbi.AllocationBase, module, MAX_PATH))
                {
                    tjs_uint8 buf[16];
                    if ((DWORD)d->ESP[p] >= 16 && !IsBadReadPtr((void*)((DWORD)d->ESP[p] - 16), 16))
                    {
                        memcpy(buf, (void*)((DWORD)d->ESP[p] - 16), 16);
                        bool flag = false;
                        if (buf[11] == 0xe8)
                            flag = true;
                        if (!flag)
                        {
                            for (tjs_int i = 0; i < 15; i++)
                            {
                                if (buf[i] == 0xff && (buf[i + 1] & 0x38) == 0x10)
                                {
                                    flag = true;
                                    break;
                                }
                            }
                        }
                        if (flag)
                        {
                            // this seems to be a call code
                            d->CallTrace[d->CallTraceLen] = (void*)d->ESP[p];
                            d->CallTraceLen++;
                        }
                    }
                }
            }
        }

        p++;
    }

    // get data pointed by each call trace data
    for (tjs_int i = 0; i < d->CallTraceLen; i++)
    {
        void* base = d->CallTrace[i];
        len = TVP_HWE_MAX_STACK_DATA_DUMP;
        while (len)
        {
            if (!IsBadReadPtr(base, len))
            {
                memcpy(d->CallTraceDumps[i], base, len);
                d->CallTraceDumpsLen[i] = len;
                break;
            }
            len--;
        }
    }

    TVPHWExcRaised = true;

    TVPWriteHWELogFile();
}
//---------------------------------------------------------------------------
static void TVPDumpCPUFlags(ttstr& line, DWORD flags, DWORD bit, tjs_char* name)
{
    line += name;
    if (flags & bit)
        line += TJS_N("+ ");
    else
        line += TJS_N("- ");
}
//---------------------------------------------------------------------------
void TVPDumpOSContext(const CONTEXT& ctx)
{
    // dump OS context block
    static const int BUF_SIZE = 256;
    tjs_char buf[BUF_SIZE];

    // mask FP exception
    TJSSetFPUE();

    // - context flags
    ttstr line;
    TJS_snprintf(buf, BUF_SIZE, TJS_N("Context Flags : 0x%08X [ "), ctx.ContextFlags);
    line += buf;
    if (ctx.ContextFlags & CONTEXT_DEBUG_REGISTERS)
        line += TJS_N("CONTEXT_DEBUG_REGISTERS ");
    if (ctx.ContextFlags & CONTEXT_FLOATING_POINT)
        line += TJS_N("CONTEXT_FLOATING_POINT ");
    if (ctx.ContextFlags & CONTEXT_SEGMENTS)
        line += TJS_N("CONTEXT_SEGMENTS ");
    if (ctx.ContextFlags & CONTEXT_INTEGER)
        line += TJS_N("CONTEXT_INTEGER ");
    if (ctx.ContextFlags & CONTEXT_CONTROL)
        line += TJS_N("CONTEXT_CONTROL ");
#ifndef TJS_64BIT_OS
    if (ctx.ContextFlags & CONTEXT_EXTENDED_REGISTERS)
        line += TJS_N("CONTEXT_EXTENDED_REGISTERS ");
#endif
    line += TJS_N("]");

    TVPAddLog(line);

    // - debug registers
#ifndef TJS_64BIT_OS
    TJS_snprintf(buf, BUF_SIZE,
                 TJS_N("Debug Registers   : ") TJS_N("0:0x%08X  ") TJS_N("1:0x%08X  ")
                     TJS_N("2:0x%08X  ") TJS_N("3:0x%08X  ") TJS_N("6:0x%08X  ")
                         TJS_N("7:0x%08X  "),
                 ctx.Dr0, ctx.Dr1, ctx.Dr2, ctx.Dr3, ctx.Dr6, ctx.Dr7);
#else
    TJS_snprintf(buf, BUF_SIZE,
                 TJS_N("Debug Registers   : ") TJS_N("0:0x%016lx  ") TJS_N("1:0x%016lx  ")
                     TJS_N("2:0x%016lx  ") TJS_N("3:0x%016lx  ") TJS_N("6:0x%016lx  ")
                         TJS_N("7:0x%016lx  "),
                 ctx.Dr0, ctx.Dr1, ctx.Dr2, ctx.Dr3, ctx.Dr6, ctx.Dr7);
#endif
    TVPAddLog(buf);

    // - Segment registers
    TJS_snprintf(
        buf, BUF_SIZE,
        TJS_N(
            "Segment Registers : GS:0x%04X  FS:0x%04X  ES:0x%04X  DS:0x%04X  CS:0x%04X  SS:0x%04X"),
        ctx.SegGs, ctx.SegFs, ctx.SegEs, ctx.SegDs, ctx.SegCs, ctx.SegSs);
    TVPAddLog(buf);

    // - Generic Integer Registers
#ifdef TJS_64BIT_OS
    TJS_snprintf(
        buf, BUF_SIZE,
        TJS_N("Integer Registers : RAX:0x%016lx  RBX:0x%016lx  RCX:0x%016lx  RDX:0x%016lx"),
        ctx.Rax, ctx.Rbx, ctx.Rcx, ctx.Rdx);
    TVPAddLog(buf);

    TJS_snprintf(buf, BUF_SIZE, TJS_N("R8 :0x%016lx  R9 :0x%016lx  R10:0x%016lx  R11:0x%016lx"),
                 ctx.R8, ctx.R9, ctx.R10, ctx.R11);
    TVPAddLog(buf);
    TJS_snprintf(buf, BUF_SIZE, TJS_N("R12:0x%016lx  R13:0x%016lx  R14:0x%016lx  R15:0x%016lx"),
                 ctx.R12, ctx.R13, ctx.R14, ctx.R15);
    TVPAddLog(buf);
#else
    TJS_snprintf(buf, BUF_SIZE,
                 TJS_N("Integer Registers : EAX:0x%08X  EBX:0x%08X  ECX:0x%08X  EDX:0x%08X"),
                 ctx.Eax, ctx.Ebx, ctx.Ecx, ctx.Edx);
    TVPAddLog(buf);
#endif

    // - Index Registers
#ifdef TJS_64BIT_OS
    TJS_snprintf(buf, BUF_SIZE, TJS_N("Index Registers   : RSI:0x%016lx  RDI:0x%016lx"), ctx.Rsi,
                 ctx.Rdi);
#else
    TJS_snprintf(buf, BUF_SIZE, TJS_N("Index Registers   : ESI:0x%08X  EDI:0x%08X"), ctx.Esi,
                 ctx.Edi);
#endif
    TVPAddLog(buf);

    // - Pointer Registers
#ifdef TJS_64BIT_OS
    TJS_snprintf(buf, BUF_SIZE,
                 TJS_N("Pointer Registers : RBP:0x%016lx  RSP:0x%016lx  RIP:0x%016lx"), ctx.Rbp,
                 ctx.Rsp, ctx.Rip);
#else
    TJS_snprintf(buf, BUF_SIZE, TJS_N("Pointer Registers : EBP:0x%08X  ESP:0x%08X  EIP:0x%08X"),
                 ctx.Ebp, ctx.Esp, ctx.Eip);
#endif
    TVPAddLog(buf);

    // - Flag Register
    TJS_snprintf(buf, BUF_SIZE, TJS_N("Flag Register     : 0x%08X [ "), ctx.EFlags);
    line = buf;
    TVPDumpCPUFlags(line, ctx.EFlags, (1 << 0), TJS_N("CF"));
    TVPDumpCPUFlags(line, ctx.EFlags, (1 << 2), TJS_N("PF"));
    TVPDumpCPUFlags(line, ctx.EFlags, (1 << 4), TJS_N("AF"));
    TVPDumpCPUFlags(line, ctx.EFlags, (1 << 6), TJS_N("ZF"));
    TVPDumpCPUFlags(line, ctx.EFlags, (1 << 7), TJS_N("SF"));
    TVPDumpCPUFlags(line, ctx.EFlags, (1 << 8), TJS_N("TF"));
    TVPDumpCPUFlags(line, ctx.EFlags, (1 << 9), TJS_N("IF"));
    TVPDumpCPUFlags(line, ctx.EFlags, (1 << 10), TJS_N("DF"));
    TVPDumpCPUFlags(line, ctx.EFlags, (1 << 11), TJS_N("OF"));
    TJS_snprintf(buf, BUF_SIZE, TJS_N("IO%d "), (ctx.EFlags >> 12) & 0x03);
    line += buf;
    TVPDumpCPUFlags(line, ctx.EFlags, (1 << 14), TJS_N("NF"));
    TVPDumpCPUFlags(line, ctx.EFlags, (1 << 16), TJS_N("RF"));
    TVPDumpCPUFlags(line, ctx.EFlags, (1 << 17), TJS_N("VM"));
    TVPDumpCPUFlags(line, ctx.EFlags, (1 << 18), TJS_N("AC"));
    TVPDumpCPUFlags(line, ctx.EFlags, (1 << 19), TJS_N("VF"));
    TVPDumpCPUFlags(line, ctx.EFlags, (1 << 20), TJS_N("VP"));
    TVPDumpCPUFlags(line, ctx.EFlags, (1 << 21), TJS_N("ID"));
    line += TJS_N("]");
    TVPAddLog(line);

    // - FP registers

    // -- control words
#ifdef TJS_64BIT_OS
    TJS_snprintf(buf, BUF_SIZE,
                 TJS_N("FP Control Word : 0x%08X   FP Status Word : 0x%08X   FP Tag Word : 0x%08X"),
                 ctx.FltSave.ControlWord, ctx.FltSave.StatusWord, ctx.FltSave.TagWord);
    TVPAddLog(buf);

    // -- offsets/selectors
    TJS_snprintf(buf, BUF_SIZE, TJS_N("FP Error Offset : 0x%08X   FP Error Selector : 0x%08X"),
                 ctx.FltSave.ErrorOffset, ctx.FltSave.ErrorSelector);
    TJS_snprintf(buf, BUF_SIZE, TJS_N("FP Data Offset  : 0x%08X   FP Data Selector  : 0x%08X"),
                 ctx.FltSave.DataOffset, ctx.FltSave.DataSelector);

    // -- registers
    long double* ptr = (long double*)&(ctx.FltSave.FloatRegisters[0]);
    for (tjs_int i = 0; i < 8; i++)
    {
        TJS_snprintf(buf, BUF_SIZE, TJS_N("FP ST(%d) : %28.20Lg 0x%04X%016I64X"), i, ptr[i],
                     (unsigned int)*(tjs_uint16*)(((tjs_uint8*)(ptr + i)) + 8),
                     *(tjs_uint64*)(ptr + i));
        TVPAddLog(buf);
    }

    // -- Cr0NpxState
    TJS_snprintf(buf, BUF_SIZE, TJS_N("FP MX CSR   : 0x%08X"), ctx.FltSave.MxCsr); //
    TVPAddLog(buf);
#else
    TJS_snprintf(buf, BUF_SIZE,
                 TJS_N("FP Control Word : 0x%08X   FP Status Word : 0x%08X   FP Tag Word : 0x%08X"),
                 ctx.FloatSave.ControlWord, ctx.FloatSave.StatusWord, ctx.FloatSave.TagWord);
    TVPAddLog(buf);

    // -- offsets/selectors
    TJS_snprintf(buf, BUF_SIZE, TJS_N("FP Error Offset : 0x%08X   FP Error Selector : 0x%08X"),
                 ctx.FloatSave.ErrorOffset, ctx.FloatSave.ErrorSelector);
    TJS_snprintf(buf, BUF_SIZE, TJS_N("FP Data Offset  : 0x%08X   FP Data Selector  : 0x%08X"),
                 ctx.FloatSave.DataOffset, ctx.FloatSave.DataSelector);

    // -- registers
    long double* ptr = (long double*)&(ctx.FloatSave.RegisterArea[0]);
    for (tjs_int i = 0; i < 8; i++)
    {
        TJS_snprintf(buf, BUF_SIZE, TJS_N("FP ST(%d) : %28.20Lg 0x%04X%016I64X"), i, ptr[i],
                     (unsigned int)*(tjs_uint16*)(((tjs_uint8*)(ptr + i)) + 8),
                     *(tjs_uint64*)(ptr + i));
        TVPAddLog(buf);
    }

    // -- Cr0NpxState
    TJS_snprintf(buf, BUF_SIZE, TJS_N("FP CR0 NPX State  : 0x%08X"), ctx.FloatSave.Cr0NpxState);
    TVPAddLog(buf);
#endif

    // -- SSE/SSE2 registers
#ifdef TJS_64BIT_OS
    TJS_snprintf(buf, BUF_SIZE, TJS_N("XMM  0 : 0x%016lx 0x%016lx"), ctx.Xmm0.High, ctx.Xmm0.Low);
    TVPAddLog(buf);
    TJS_snprintf(buf, BUF_SIZE, TJS_N("XMM  1 : 0x%016lx 0x%016lx"), ctx.Xmm1.High, ctx.Xmm1.Low);
    TVPAddLog(buf);
    TJS_snprintf(buf, BUF_SIZE, TJS_N("XMM  2 : 0x%016lx 0x%016lx"), ctx.Xmm2.High, ctx.Xmm2.Low);
    TVPAddLog(buf);
    TJS_snprintf(buf, BUF_SIZE, TJS_N("XMM  3 : 0x%016lx 0x%016lx"), ctx.Xmm3.High, ctx.Xmm3.Low);
    TVPAddLog(buf);
    TJS_snprintf(buf, BUF_SIZE, TJS_N("XMM  4 : 0x%016lx 0x%016lx"), ctx.Xmm4.High, ctx.Xmm4.Low);
    TVPAddLog(buf);
    TJS_snprintf(buf, BUF_SIZE, TJS_N("XMM  5 : 0x%016lx 0x%016lx"), ctx.Xmm5.High, ctx.Xmm5.Low);
    TVPAddLog(buf);
    TJS_snprintf(buf, BUF_SIZE, TJS_N("XMM  6 : 0x%016lx 0x%016lx"), ctx.Xmm6.High, ctx.Xmm6.Low);
    TVPAddLog(buf);
    TJS_snprintf(buf, BUF_SIZE, TJS_N("XMM  7 : 0x%016lx 0x%016lx"), ctx.Xmm7.High, ctx.Xmm7.Low);
    TVPAddLog(buf);
    TJS_snprintf(buf, BUF_SIZE, TJS_N("XMM  8 : 0x%016lx 0x%016lx"), ctx.Xmm8.High, ctx.Xmm8.Low);
    TVPAddLog(buf);
    TJS_snprintf(buf, BUF_SIZE, TJS_N("XMM  9 : 0x%016lx 0x%016lx"), ctx.Xmm9.High, ctx.Xmm9.Low);
    TVPAddLog(buf);
    TJS_snprintf(buf, BUF_SIZE, TJS_N("XMM 10 : 0x%016lx 0x%016lx"), ctx.Xmm10.High, ctx.Xmm10.Low);
    TVPAddLog(buf);
    TJS_snprintf(buf, BUF_SIZE, TJS_N("XMM 11 : 0x%016lx 0x%016lx"), ctx.Xmm11.High, ctx.Xmm11.Low);
    TVPAddLog(buf);
    TJS_snprintf(buf, BUF_SIZE, TJS_N("XMM 12 : 0x%016lx 0x%016lx"), ctx.Xmm12.High, ctx.Xmm12.Low);
    TVPAddLog(buf);
    TJS_snprintf(buf, BUF_SIZE, TJS_N("XMM 13 : 0x%016lx 0x%016lx"), ctx.Xmm13.High, ctx.Xmm13.Low);
    TVPAddLog(buf);
    TJS_snprintf(buf, BUF_SIZE, TJS_N("XMM 14 : 0x%016lx 0x%016lx"), ctx.Xmm14.High, ctx.Xmm14.Low);
    TVPAddLog(buf);
    TJS_snprintf(buf, BUF_SIZE, TJS_N("XMM 15 : 0x%016lx 0x%016lx"), ctx.Xmm15.High, ctx.Xmm15.Low);
    TVPAddLog(buf);

    TJS_snprintf(buf, BUF_SIZE, TJS_N("MXCSR : 0x%08x"), ctx.MxCsr);
    TVPAddLog(buf);
#else
    if (ctx.ContextFlags & CONTEXT_EXTENDED_REGISTERS)
    {
        // ExtendedRegisters is a area which meets fxsave and fxrstor instruction?
#pragma pack(push, 1)
        union xmm_t
        {
            struct
            {
                float sA;
                float sB;
                float sC;
                float sD;
            };
            struct
            {
                double dA;
                double dB;
            };
            struct
            {
                tjs_uint64 i64A;
                tjs_uint64 i64B;
            };
        };
#pragma pack(pop)
        for (tjs_int i = 0; i < 8; i++)
        {
            xmm_t* xmm = (xmm_t*)(ctx.ExtendedRegisters + i * 16 + 0xa0);
            TJS_snprintf(buf, BUF_SIZE,
                         TJS_N("XMM %d : [ %15.8g %15.8g %15.8g %15.8g ] [ %24.16lg %24.16lg ] [ "
                               "0x%016I64X-0x%016I64X ]"),
                         i, xmm->sD, xmm->sC, xmm->sB, xmm->sA, xmm->dB, xmm->dA, xmm->i64B,
                         xmm->i64A);
            TVPAddLog(buf);
        }
        TJS_snprintf(buf, BUF_SIZE, TJS_N("MXCSR : 0x%08X"),
                     *(DWORD*)(ctx.ExtendedRegisters + 0x18));
        TVPAddLog(buf);
    }
#endif
}
//---------------------------------------------------------------------------
void TVPDumpHWException()
{
    // dump latest hardware exception if it exists

    if (!TVPHWExcRaised)
        return;
    TVPHWExcRaised = false;

    TVPOnError();

    static const int BUF_SIZE = 256;
    tjs_char buf[BUF_SIZE];
    tTVPHWExceptionData* d = &TVPLastHWExceptionData;

    TVPAddLog(ttstr(TVPHardwareExceptionRaised));

    ttstr line;

    line = TJS_N("Exception : ");

    tjs_char* p = NULL;
    switch (d->Code)
    {
        case 3:
            p = TJS_N("Divide By Zero");
            break;
        case 4:
            p = TJS_N("Range Error");
            break;
        case 5:
            p = TJS_N("Integer Overflow");
            break;
        case 6:
            p = TJS_N("Invalid Operation");
            break;
        case 7:
            p = TJS_N("Zero Divide");
            break;
        case 8:
            p = TJS_N("Overflow");
            break;
        case 9:
            p = TJS_N("Underflow");
            break;
        case 10:
            p = TJS_N("Invalid Cast");
            break;
        case 11:
            p = TJS_N("Access Violation");
            break;
        case 12:
            p = TJS_N("Privilege Violation");
            break;
        case 13:
            p = TJS_N("Control C");
            break;
        case 14:
            p = TJS_N("Stack Overflow");
            break;
    }

    if (p)
        line += p;

    if (d->Code == 11)
    {
        // EAccessViolation
        const tjs_char* mode = TJS_N("unknown");
        if (d->AccessFlag == 0)
            mode = TJS_N("read");
        else if (d->AccessFlag == 1)
            mode = TJS_N("write");
        else if (d->AccessFlag == 8)
            mode = TJS_N("execute");
        TJS_snprintf(buf, BUF_SIZE, TJS_N("(%ls access to 0x%p)"), mode, d->AccessTarget);
        line += buf;
    }

    TJS_snprintf(buf, BUF_SIZE, TJS_N("  at  EIP = 0x%p   ESP = 0x%p"), d->EIP, d->ESP);
    line += buf;
    if (d->Module[0])
    {
        line += TJS_N("   in ") + ttstr(d->Module);
    }

    TVPAddLog(line);

    // dump OS context
    TVPDumpOSContext(d->Context);

    // dump codes at EIP
    line = TJS_N("Codes at EIP : ");
    for (tjs_int i = 0; i < d->CodesAtEIPLen; i++)
    {
        TJS_snprintf(buf, BUF_SIZE, TJS_N("0x%02X "), d->CodesAtEIP[i]);
        line += buf;
    }
    TVPAddLog(line);

    TVPAddLog(TJS_N("Stack data and data pointed by each stack data :"));

    // dump stack and data
    for (tjs_int s = 0; s < d->StackAtESPLen; s++)
    {
        TJS_snprintf(buf, BUF_SIZE, TJS_N("0x%p (ESP+%3d) : 0x%p : "),
                     (DWORD)d->ESP + s * sizeof(tjs_uint32), s * sizeof(tjs_uint32),
                     d->StackAtESP[s]);
        line = buf;

        for (tjs_int i = 0; i < d->StackDumpsLen[s]; i++)
        {
            TJS_snprintf(buf, BUF_SIZE, TJS_N("0x%02X "), d->StackDumps[s][i]);
            line += buf;
        }
        TVPAddLog(line);
    }

    // dump call trace
    TVPAddLog(TJS_N("Call Trace :"));
    for (tjs_int s = 0; s < d->CallTraceLen; s++)
    {
        TJS_snprintf(buf, BUF_SIZE, TJS_N("0x%p : "), d->CallTrace[s]);
        line = buf;

        for (tjs_int i = 0; i < d->CallTraceDumpsLen[s]; i++)
        {
            TJS_snprintf(buf, BUF_SIZE, TJS_N("0x%02X "), d->CallTraceDumps[s][i]);
            line += buf;
        }
        MEMORY_BASIC_INFORMATION mbi;
        VirtualQuery((void*)d->CallTrace[s], &mbi, sizeof(mbi));
        if (mbi.State == MEM_COMMIT)
        {
            wchar_t module[MAX_PATH];
            if (::GetModuleFileName((HMODULE)mbi.AllocationBase, module, MAX_PATH))
            {
                line += ttstr(ExtractFileName(module).c_str());
                TJS_snprintf(buf, BUF_SIZE, TJS_N(" base 0x%p"), mbi.AllocationBase);
                line += buf;
            }
        }
        TVPAddLog(line);
    }
}
//---------------------------------------------------------------------------
#else
void TVPDumpHWException(void)
{
    // dummy
}
#endif
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
static void TVPInitRandomGenerator()
{
    tjs_uint32 tick = TVPGetRoughTickCount32();
    TVPPushEnvironNoise(&tick, sizeof(tick));
    std::thread::id tid = std::this_thread::get_id();
    TVPPushEnvironNoise(&tid, sizeof(tid));
    time_t curtime = time(NULL);
    TVPPushEnvironNoise(&curtime, sizeof(curtime));
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPInitializeBaseSystems
//---------------------------------------------------------------------------
void TVPInitializeBaseSystems()
{
#ifdef _KRKRSDL3_OHOS
    SDL_Log("[krkr2-diag] >>> TVPInitializeBaseSystems entered");
#endif
    // set system archive delimiter
    tTJSVariant v;
    if (TVPGetCommandLine(TJS_N("-arcdelim"), &v))
        TVPArchiveDelimiter = ttstr(v)[0];

    // set default current directory
    {
#ifdef _KRKRSDL3_OHOS
        ttstr ep = IncludeTrailingBackslash(ExePath());
        SDL_Log("[krkr2-diag] TVPInitializeBaseSystems setting CurDir to: '%s'", ep.AsStdString().c_str());
        TVPSetCurrentDirectory(ep);
#else
        TVPSetCurrentDirectory(IncludeTrailingBackslash(ExePath()));
#endif
    }

    // load message map file
    bool load_msgmap = GetSystemSecurityOption("disablemsgmap") == 0;

    if (load_msgmap)
    {
        const tjs_char name_msgmap[] = TJS_N("msgmap.tjs");
#ifdef _KRKRSDL3_OHOS
        SDL_Log("[krkr2-diag] checking msgmap.tjs existence...");
        bool exists = TVPIsExistentStorage(name_msgmap);
        SDL_Log("[krkr2-diag] msgmap.tjs exists=%d", exists);
        if (exists)
#else
        if (TVPIsExistentStorage(name_msgmap))
#endif
            TVPExecuteStorage(name_msgmap, NULL, false, TJS_N(""));
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// system initializer / uninitializer
//---------------------------------------------------------------------------
static tjs_uint64 TVPTotalPhysMemory = 0;
static void TVPInitProgramArgumentsAndDataPath(bool stop_after_datapath_got);
void TVPBeforeSystemInit()
{
#ifdef _KRKRSDL3_OHOS
    SDL_Log("[krkr2-diag] >>> TVPBeforeSystemInit entered");
#endif
    // RegisterDllLoadHook();
    //  register DLL delayed import hook to support _inmm.dll

    TVPInitProgramArgumentsAndDataPath(false); // ensure command line

    // set system archive delimiter after patch.tjs specified
    tTJSVariant v;
    if (TVPGetCommandLine(TJS_N("-arcdelim"), &v))
        TVPArchiveDelimiter = ttstr(v)[0];

#ifdef _KRKRSDL3_OHOS
    SDL_Log("[krkr2-diag] TVPProjectDir before check: '%s'", TVPProjectDir.AsStdString().c_str());
    bool isExistent = TVPIsExistentStorageNoSearchNoNormalize(TVPProjectDir);
    SDL_Log("[krkr2-diag] TVPIsExistentStorageNoSearchNoNormalize = %d", isExistent);
    if (isExistent)
#else
    if (TVPIsExistentStorageNoSearchNoNormalize(TVPProjectDir))
#endif
    {
        // Register parent directory so files next to the archive are searchable
        // (e.g. Config.tjs which resides in the game folder, not inside the XP3)
        ttstr parentDir = TVPExtractStoragePath(TVPProjectDir);
        if (!parentDir.IsEmpty())
            TVPAddAutoPath(parentDir);
        TVPProjectDir += TVPArchiveDelimiter;
        // Register all top-level subdirectories inside the archive so that
        // files stored in subdirs (e.g. main/Config.tjs) can be found by name.
        TVPAddArchiveSubDirAutoPath(TVPNormalizeStorageName(TVPProjectDir));
#ifdef _KRKRSDL3_OHOS
        // Scan the parent directory for sibling XP3 archives so that
        // startup.tjs (typically in data.xp3) is discoverable even when
        // TVPProjectDir points at a specific archive (e.g. patch.xp3).
        if (!parentDir.IsEmpty()) {
            static const char* xp3Names[] = {
                "data.xp3", "run.xp3", "plugin.xp3",
                "patch.xp3", "patch2.xp3", "patch3.xp3",
                "patch4.xp3", "patch5.xp3", nullptr
            };
            // TVPProjectDir now has '>' at end; extract the base name for comparison
            ttstr currentArc = TVPExtractStorageName(parentDir.IsEmpty() ? TVPProjectDir : 
                ttstr(TVPProjectDir, TVPProjectDir.GetLen() - 1));
            for (int i = 0; xp3Names[i]; ++i) {
                ttstr siblingArc = parentDir + xp3Names[i];
                if (TVPIsExistentStorageNoSearchNoNormalize(siblingArc)) {
                    ttstr siblingAutoPath = siblingArc + TVPArchiveDelimiter;
                    TVPAddAutoPath(siblingAutoPath);
                    TVPAddArchiveSubDirAutoPath(TVPNormalizeStorageName(siblingAutoPath));
                    SDL_Log("[krkr2-diag] TVPBeforeSystemInit: sibling XP3 '%s'",
                            siblingAutoPath.AsStdString().c_str());
                }
            }
        }
#endif
    }
    else
    {
        if (TVPProjectDir.GetLastChar() != TJS_N('/') &&
            TVPProjectDir.GetLastChar() != TJS_N('\\'))
            TVPProjectDir += TJS_N("/");
    }
#ifdef _KRKRSDL3_OHOS
    SDL_Log("[krkr2-diag] TVPProjectDir after check: '%s'", TVPProjectDir.AsStdString().c_str());
#endif
    TVPSetCurrentDirectory(TVPProjectDir);

#ifdef TVP_REPORT_HW_EXCEPTION
    // __dee_hacked_set_getExceptionObjectHook(TVP__dee_hacked_getExceptionObjectHook);
    // register hook function for hardware exceptions
#endif

    // randomize
    TVPInitRandomGenerator();

    // memory usage
    {
        TVPMemoryInfo meminf;
        TVPGetMemoryInfo(meminf);
        TVPPushEnvironNoise(&meminf, sizeof(meminf));

        TVPTotalPhysMemory = meminf.MemTotal * 1024;
        if (TVPTotalPhysMemory > 768 * 1024 * 1024)
        {
            TVPTotalPhysMemory -= 512 * 1024 * 1024; // assume that system reserved 512M memory
        }
        else
        {
            TVPTotalPhysMemory /= 2; // use half memory in small memory devices
        }

        TVPAddImportantLog(
            TVPFormatMessage(TVPInfoTotalPhysicalMemory, tjs_int64(TVPTotalPhysMemory)));
        if (TVPTotalPhysMemory > 256 * 1024 * 1024)
        {
            std::string str = "unlimited";
            if (str == ("low"))
                TVPTotalPhysMemory = 0; // assumes zero
            else if (str == ("medium"))
                TVPTotalPhysMemory = 128 * 1024 * 1024;
            else if (str == ("high"))
                TVPTotalPhysMemory = 256 * 1024 * 1024;
        }
        else
        { // use minimum memory usage if less than 256M(512M physics)
            TVPTotalPhysMemory = 0;
        }

        if (TVPTotalPhysMemory < 128 * 1024 * 1024)
        {
            // very very low memory, forcing to assume zero memory
            TVPTotalPhysMemory = 0;
        }

        if (TVPTotalPhysMemory < 128 * 1024 * 1024)
        {
            // extra low memory
            if (TJSObjectHashBitsLimit > 0)
                TJSObjectHashBitsLimit = 0;
            TVPSegmentCacheLimit = 0;
            TVPFreeUnusedLayerCache = true; // in LayerIntf.cpp
        }
        else if (TVPTotalPhysMemory < 256 * 1024 * 1024)
        {
            // low memory
            if (TJSObjectHashBitsLimit > 4)
                TJSObjectHashBitsLimit = 4;
        }
    }
}
//---------------------------------------------------------------------------
static void TVPDumpOptions();
//---------------------------------------------------------------------------
extern bool TVPEnableGlobalHeapCompaction;
extern bool TVPAutoSaveBookMark;
static bool TVPHighTimerPeriod = false;
static uint32_t TVPTimeBeginPeriodRes = 0;
//---------------------------------------------------------------------------
void TVPAfterSystemInit()
{
    // check CPU type
    TVPDetectCPU();

    TVPAllocGraphicCacheOnHeap = false; // always false since beta 20

    // determine maximum graphic cache limit
    tTJSVariant opt;
    tjs_int64 limitmb = -1;
    if (TVPGetCommandLine(TJS_N("-gclim"), &opt))
    {
        ttstr str(opt);
        if (str == TJS_N("auto"))
            limitmb = -1;
        else
            limitmb = opt.AsInteger();
    }

    if (limitmb == -1)
    {
        if (TVPTotalPhysMemory <= 32 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 0;
        else if (TVPTotalPhysMemory <= 48 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 0;
        else if (TVPTotalPhysMemory <= 64 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 0;
        else if (TVPTotalPhysMemory <= 96 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 4;
        else if (TVPTotalPhysMemory <= 128 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 8;
        else if (TVPTotalPhysMemory <= 192 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 12;
        else if (TVPTotalPhysMemory <= 256 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 20;
        else if (TVPTotalPhysMemory <= 512 * 1024 * 1024)
            TVPGraphicCacheSystemLimit = 40;
        else
            TVPGraphicCacheSystemLimit =
                tjs_uint64(TVPTotalPhysMemory / (1024 * 1024 * 10)); // cachemem = physmem / 10
        TVPGraphicCacheSystemLimit *= 1024 * 1024;
    }
    else
    {
        TVPGraphicCacheSystemLimit = limitmb * 1024 * 1024;
    }
    // 32bit 側偺偱 512MB 傑偱偵惂尷
    if (TVPGraphicCacheSystemLimit >= 512 * 1024 * 1024)
        TVPGraphicCacheSystemLimit = 512 * 1024 * 1024;

    if (TVPTotalPhysMemory <= 64 * 1024 * 1024)
        TVPSetFontCacheForLowMem();

    //	TVPGraphicCacheSystemLimit = 1*1024*1024; // DEBUG

    if (TVPGetCommandLine(TJS_N("-autosave"), &opt))
    {
        ttstr str(opt);
        if (str == TJS_N("yes"))
        {
            TVPAutoSaveBookMark = true;
        }
    }
    // check TVPGraphicSplitOperation option
    std::string _val = GameSetting::renderer;
    if (_val != "software")
    {
        TVPGraphicSplitOperationType = gsotNone;
    }
    else
    {
        TVPDrawThreadNum = GameSetting::software_draw_thread;
        if (TVPGetCommandLine(TJS_N("-gsplit"), &opt))
        {
            ttstr str(opt);
            if (str == TJS_N("no"))
                TVPGraphicSplitOperationType = gsotNone;
            else if (str == TJS_N("int"))
                TVPGraphicSplitOperationType = gsotInterlace;
            else if (str == TJS_N("yes") || str == TJS_N("simple"))
                TVPGraphicSplitOperationType = gsotSimple;
            else if (str == TJS_N("bidi"))
                TVPGraphicSplitOperationType = gsotBiDirection;
        }
    }

    // check TVPDefaultHoldAlpha option
    if (TVPGetCommandLine(TJS_N("-holdalpha"), &opt))
    {
        ttstr str(opt);
        if (str == TJS_N("yes") || str == TJS_N("true"))
            TVPDefaultHoldAlpha = true;
        else
            TVPDefaultHoldAlpha = false;
    }

    // check TVPJPEGFastLoad option
    if (TVPGetCommandLine(TJS_N("-jpegdec"), &opt)) // this specifies precision for JPEG decoding
    {
        ttstr str(opt);
        if (str == TJS_N("normal"))
            TVPJPEGLoadPrecision = jlpMedium;
        else if (str == TJS_N("low"))
            TVPJPEGLoadPrecision = jlpLow;
        else if (str == TJS_N("high"))
            TVPJPEGLoadPrecision = jlpHigh;
    }

    // dump option
    TVPDumpOptions();

    // timer precision
    uint32_t prectick = 1;
    if (TVPGetCommandLine(TJS_N("-timerprec"), &opt))
    {
        ttstr str(opt);
        if (str == TJS_N("high"))
            prectick = 1;
        if (str == TJS_N("higher"))
            prectick = 5;
        if (str == TJS_N("normal"))
            prectick = 10;
    }

    // draw thread num
    tjs_int drawThreadNum = 0;
    if (TVPGetCommandLine(TJS_N("-drawthread"), &opt))
    {
        ttstr str(opt);
        if (str == TJS_N("auto"))
            drawThreadNum = 0;
        else
            drawThreadNum = (tjs_int)opt;
    }
    TVPDrawThreadNum = drawThreadNum;
}
//---------------------------------------------------------------------------
void TVPBeforeSystemUninit()
{
    // TVPDumpHWException(); // dump cached hw exceptoin
}
//---------------------------------------------------------------------------
void TVPAfterSystemUninit()
{
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
bool TVPTerminated = false;
bool TVPTerminateOnWindowClose = true;
bool TVPTerminateOnNoWindowStartup = true;
int TVPTerminateCode = 0;
//---------------------------------------------------------------------------
void TVPTerminateAsync(int code)
{
    // do "A"synchronous temination of application
    TVPTerminated = true;
    TVPTerminateCode = code;

    // posting dummy message will prevent "missing WM_QUIT bug" in Direct3D framework.
    if (TVPSystemControl)
        TVPSystemControl->CallDeliverAllEventsOnIdle();

    Application->Terminate();

    if (TVPSystemControl)
        TVPSystemControl->CallDeliverAllEventsOnIdle();
}
//---------------------------------------------------------------------------
void TVPTerminateSync(int code)
{
    // do synchronous temination of application (never return)
    TVPSystemUninit();
    TVPExitApplication(code);
}
//---------------------------------------------------------------------------
void TVPMainWindowClosed()
{
    // called from WindowIntf.cpp, caused by closing all window.
    if (TVPTerminateOnWindowClose)
        TVPTerminateAsync();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// GetCommandLine
//---------------------------------------------------------------------------
static std::vector<std::string>* TVPGetEmbeddedOptions()
{

    std::vector<std::string>* ret = NULL;
    return ret;
}
//---------------------------------------------------------------------------
static std::vector<std::string>* TVPGetConfigFileOptions(const ttstr& filename)
{
    std::vector<std::string>* ret = NULL; // new std::vector<std::string>();
    return ret;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------

static ttstr TVPParseCommandLineOne(const ttstr& i)
{
    // value is specified
    const tjs_char *p, *o;
    p = o = i.c_str();
    p = TJS_strchr(p, '=');

    if (p == NULL)
    {
        return i + TJS_N("=yes");
    }

    p++;

    ttstr optname(o, (int)(p - o));

    if (*p == TJS_N('\'') || *p == TJS_N('\"'))
    {
        // as an escaped string
        tTJSVariant v;
        TJSParseString(v, &p);

        return optname + ttstr(v);
    }
    else
    {
        // as a string
        return optname + p;
    }
}
//---------------------------------------------------------------------------
std::vector<ttstr> TVPProgramArguments;
static bool TVPProgramArgumentsInit = false;
static tjs_int TVPCommandLineArgumentGeneration = 0;
static bool TVPDataPathDirectoryEnsured = false;
//---------------------------------------------------------------------------
tjs_int TVPGetCommandLineArgumentGeneration()
{
    return TVPCommandLineArgumentGeneration;
}
//---------------------------------------------------------------------------
void TVPEnsureDataPathDirectory()
{
    if (!TVPDataPathDirectoryEnsured)
    {
        TVPDataPathDirectoryEnsured = true;
        // ensure data path existence
        if (!TVPCheckExistentLocalFolder(TVPNativeDataPath.c_str()))
        {
            if (TVPCreateFolders(TVPNativeDataPath.c_str()))
                TVPAddImportantLog(TVPFormatMessage(TVPInfoDataPathDoesNotExistTryingToMakeIt,
                                                    (const tjs_char*)TVPOk));
            else
                TVPAddImportantLog(TVPFormatMessage(TVPInfoDataPathDoesNotExistTryingToMakeIt,
                                                    (const tjs_char*)TVPFaild));
        }
    }
}
//---------------------------------------------------------------------------
static void PushConfigFileOptions(const std::vector<std::string>* options)
{
    if (!options)
        return;
    for (unsigned int j = 0; j < options->size(); j++)
    {
        if ((*options)[j].c_str()[0] != ';') // unless comment
            TVPProgramArguments.push_back(
                TVPParseCommandLineOne(TJS_N("-") + ttstr((*options)[j].c_str())));
    }
}
//---------------------------------------------------------------------------
static int tvp_argc = 0;
static char** tvp_argv = NULL;
void SetCommandlineArguments(int argc, char* argv[])
{
    tvp_argc = argc;
    tvp_argv = argv;
}

static void TVPInitProgramArgumentsAndDataPath(bool stop_after_datapath_got)
{
    if (!TVPProgramArgumentsInit)
    {
        TVPProgramArgumentsInit = true;

        // find options from self executable image
        try
        {
            // store arguments given by commandline to "TVPProgramArguments"
            bool acceptfilenameargument = GetSystemSecurityOption("acceptfilenameargument") != 0;
            bool argument_stopped = false;
            if (acceptfilenameargument)
                argument_stopped = true;
            int file_argument_count = 0;
            for (tjs_int i = 1; i < tvp_argc; i++)
            {
                if (argument_stopped)
                {
                    ttstr arg_name_and_value = TJS_N("-arg") + ttstr(file_argument_count) +
                                               TJS_N("=") + ttstr(tvp_argv[i]);
                    file_argument_count++;
                    TVPProgramArguments.push_back(arg_name_and_value);
                }
                else
                {
                    if (tvp_argv[i][0] == TJS_N('-'))
                    {
                        if (tvp_argv[i][1] == TJS_N('-') && tvp_argv[i][2] == 0)
                        {
                            // argument stopper
                            argument_stopped = true;
                        }
                        else
                        {
                            ttstr value(tvp_argv[i]);
                            if (!TJS_strchr(value.c_str(), TJS_N('=')))
                                value += TJS_N("=yes");
                            TVPProgramArguments.push_back(TVPParseCommandLineOne(value));
                        }
                    }
                }
            }

            // read datapath
            TVPNativeDataPath = GetDataPathDirectory(ExePath());

            if (stop_after_datapath_got)
                return;
        }
        catch (...)
        {
            throw;
        }

        // set data path
        TVPDataPath = TVPNormalizeStorageName(TVPNativeDataPath);
        TVPAddImportantLog(TVPFormatMessage(TVPInfoDataPath, TVPDataPath));

        // set log output directory
        TVPSetLogLocation(TVPNativeDataPath);

        // increment TVPCommandLineArgumentGeneration
        TVPCommandLineArgumentGeneration++;
    }
}
//---------------------------------------------------------------------------
static void TVPDumpOptions()
{
    std::vector<ttstr>::const_iterator i;
    ttstr options(TVPInfoSpecifiedOptionEarlierItemHasMorePriority);
    if (TVPProgramArguments.size())
    {
        for (i = TVPProgramArguments.begin(); i != TVPProgramArguments.end(); i++)
        {
            options += TJS_N(" ");
            options += *i;
        }
    }
    else
    {
        options += (const tjs_char*)TVPNone;
    }
    TVPAddImportantLog(options);
}
//---------------------------------------------------------------------------
bool TVPGetCommandLine(const tjs_char* name, tTJSVariant* value)
{
    TVPInitProgramArgumentsAndDataPath(false);

    tjs_int namelen = (tjs_int)TJS_strlen(name);
    std::vector<ttstr>::const_iterator i;
    for (i = TVPProgramArguments.begin(); i != TVPProgramArguments.end(); i++)
    {
        if (!TJS_strncmp(i->c_str(), name, namelen))
        {
            if (i->c_str()[namelen] == TJS_N('='))
            {
                // value is specified
                const tjs_char* p = i->c_str() + namelen + 1;
                if (value)
                    *value = p;
                return true;
            }
            else if (i->c_str()[namelen] == 0)
            {
                // value is not specified
                if (value)
                    *value = TJS_N("yes");
                return true;
            }
        }
    }
    return false;
}
//---------------------------------------------------------------------------
void TVPSetCommandLine(const tjs_char* name, const ttstr& value)
{
    //	TVPInitProgramArgumentsAndDataPath(false);

    tjs_int namelen = (tjs_int)TJS_strlen(name);
    std::vector<ttstr>::iterator i;
    for (i = TVPProgramArguments.begin(); i != TVPProgramArguments.end(); i++)
    {
        if (!TJS_strncmp(i->c_str(), name, namelen))
        {
            if (i->c_str()[namelen] == TJS_N('=') || i->c_str()[namelen] == 0)
            {
                // value found
                *i = ttstr(i->c_str(), namelen) + TJS_N("=") + value;
                TVPCommandLineArgumentGeneration++;
                if (TVPCommandLineArgumentGeneration == 0)
                    TVPCommandLineArgumentGeneration = 1;
                return;
            }
        }
    }

    // value not found; insert argument into front
    TVPProgramArguments.insert(TVPProgramArguments.begin(), ttstr(name) + TJS_N("=") + value);
    TVPCommandLineArgumentGeneration++;
    if (TVPCommandLineArgumentGeneration == 0)
        TVPCommandLineArgumentGeneration = 1;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPCheckPrintDataPath
//---------------------------------------------------------------------------
bool TVPCheckPrintDataPath()
{
    return false;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPCheckAbout
//---------------------------------------------------------------------------
bool TVPCheckAbout(void)
{
    return false;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPExecuteAsync
//---------------------------------------------------------------------------
static void TVPExecuteAsync(const std::wstring& progname)
{
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPWaitWritePermit
//---------------------------------------------------------------------------
static bool TVPWaitWritePermit(const std::wstring& fn)
{
    return false;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPExecuteUserConfig
//---------------------------------------------------------------------------
bool TVPExecuteUserConfig()
{
    return false;
    // check command line argument
}
//---------------------------------------------------------------------------
