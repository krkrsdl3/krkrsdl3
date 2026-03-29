//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// "Plugins" class implementation / Service for plug-ins
//---------------------------------------------------------------------------
#ifndef PluginImplH
#define PluginImplH
//---------------------------------------------------------------------------
#include <memory.h>
#include <set>

#include "TVPPlugin.h"

void TVPLoadPlugin(const ttstr& name);
bool TVPUnloadPlugin(const ttstr& name);
extern std::set<ttstr> TVPRegisteredPlugins;

inline extern void* TVP_malloc(size_t size)
{
    return malloc(size);
}
inline extern void* TVP_realloc(void* pp, size_t size)
{
    return realloc(pp, size);
}
inline extern void TVP_free(void* pp)
{
    return free(pp);
}
extern tjs_int TVPGetAutoLoadPluginCount();
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
extern int ZLIB_uncompress(unsigned char* dest,
                           unsigned long* destlen,
                           const unsigned char* source,
                           unsigned long sourcelen);
extern int ZLIB_compress(unsigned char* dest,
                         unsigned long* destlen,
                         const unsigned char* source,
                         unsigned long sourcelen);
extern int ZLIB_compress2(unsigned char* dest,
                          unsigned long* destlen,
                          const unsigned char* source,
                          unsigned long sourcelen,
                          int level);

/*[*/

//---------------------------------------------------------------------------
// this stub includes exported function from Independent implementation of
// MD5 (RFC 1321) by Aladdin Enterprises.
//---------------------------------------------------------------------------
// TVP_md5_init, TVP_md5_append, TVP_md5_finish are exported
typedef struct TVP_md5_state_s
{
    tjs_uint8 buffer[4 * 2 + 8 + 4 * 4 + 8 + 64];
} TVP_md5_state_t; // md5_state_t
//---------------------------------------------------------------------------

/*]*/

extern void TVP_md5_init(TVP_md5_state_t* pms);
extern void TVP_md5_append(TVP_md5_state_t* pms, const tjs_uint8* data, int nbytes);
extern void TVP_md5_finish(TVP_md5_state_t* pms, tjs_uint8* digest);

extern void TVPProcessApplicationMessages();
extern void TVPHandleApplicationMessage();

extern bool TVPRegisterGlobalObject(const tjs_char* name, iTJSDispatch2* dsp);
extern bool TVPRemoveGlobalObject(const tjs_char* name);

/*[*/
//---------------------------------------------------------------------------
// data types for TVPDoTryBlock
//---------------------------------------------------------------------------
// TVPDoTryBlock executes specified 'tryblock' in try block.
// If any exception occured,
// 'catchblock' is to be executed. 'data' is applicatoin defined data
// block passed to 'tryblock' and 'catchblock' and 'finallyblock'.
// if the 'catchblock' returns true, the exception is to be rethrown.
// if false then the exception is to be vanished.
// 'finallyblock' can be null, is to be executed whatever the exception
// is generated or not.

struct tTVPExceptionDesc
{
    ttstr type;    // the exception type, currently 'eTJS' or 'unknown'
    ttstr message; // the exception message (if exists. otherwise empty).
};

typedef void (*tTVPTryBlockFunction)(void* data);
typedef bool (*tTVPCatchBlockFunction)(void* data, const tTVPExceptionDesc& desc);
typedef void (*tTVPFinallyBlockFunction)(void* data);
//---------------------------------------------------------------------------

/*]*/

extern void TVPDoTryBlock(tTVPTryBlockFunction tryblock,
                          tTVPCatchBlockFunction catchblock,
                          tTVPFinallyBlockFunction finallyblock,
                          void* data);

//---------------------------------------------------------------------------
extern bool TVPPluginUnloadedAtSystemExit;
extern void TVPLoadPluigins(void);
#ifdef _KRKRSDL3_OHOS
extern void TVPResetPluginsForReentry();
#endif
//---------------------------------------------------------------------------

#endif
