//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// "Plugins" class implementation / Service for plug-ins
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include <algorithm>
#include <functional>
#include "TVPScript.h"
#include "TVPPlugin.h"
#include "TVPStorage.h"
#include "TVPGraphicsLoader.h"

#include "TVPMsg.h"
#include "TVPSystem.h"

#include "tjsHashSearch.h"
#include "TVPEvent.h"
#include "TransIntf.h"
#include "tjsArray.h"
#include "tjsDictionary.h"
#include "TVPDebug.h"
#include "Platform.h"
#include "tjs.h"
#include "FuncStubs.h"

#pragma pack(push, 8)
	///  tvpsnd.h needs packing size of 8
	//#include "tvpsnd.h"
#pragma pack(pop)

#include "TVPApplication.h"
#include "SDL3/SDL.h"
#include <set>

//---------------------------------------------------------------------------
bool TVPLoadInternalPlugin(const ttstr &_name);
extern std::set<ttstr> TVPRegisteredPlugins;
static bool TVPPluginLoading = false;
void TVPLoadPlugin(const ttstr & name)
{
	bool success = TVPLoadInternalPlugin(name);
    return; // seal all plugins
}
//---------------------------------------------------------------------------
bool TVPUnloadPlugin(const ttstr & name)
{
	return true;
}
//---------------------------------------------------------------------------





//---------------------------------------------------------------------------
// plug-in autoload support
//---------------------------------------------------------------------------
struct tTVPFoundPlugin
{
	std::string Path;
	std::string Name;
	bool operator < (const tTVPFoundPlugin &rhs) const { return Name < rhs.Name; }
};
static tjs_int TVPAutoLoadPluginCount = 0;
static void TVPSearchPluginsAt(std::vector<tTVPFoundPlugin> &list, std::string folder)
{
	TVPListDir(folder, [&](const std::string &filename, int mask){
		if (mask & S_IFREG) {
			if (!SDL_strcasecmp(filename.c_str() + filename.length() - 4, ".tpm")) {
				tTVPFoundPlugin fp;
				fp.Path = folder;
				fp.Name = filename;
				list.emplace_back(fp);
			}
		}
	});
}

void TVPLoadInternalPlugins();
void TVPLoadPluigins(void)
{
	TVPLoadInternalPlugins();
	// This function searches plugins which have an extension of ".tpm"
	// in the default path: 
	//    1. a folder which holds kirikiri executable
	//    2. "plugin" folder of it
	// Plugin load order is to be decided using its name;
	// aaa.tpm is to be loaded before aab.tpm (sorted by ASCII order)

	// search plugins from path: (exepath), (exepath)\system, (exepath)\plugin
	std::vector<tTVPFoundPlugin> list;

	std::string exepath = TVPNativeProjectDir.AsStdString();

	TVPSearchPluginsAt(list, exepath);
	TVPSearchPluginsAt(list, exepath + "/system");
	TVPSearchPluginsAt(list, exepath + "/plugin");

	// sort by filename
	std::sort(list.begin(), list.end());

	// load each plugin
	TVPAutoLoadPluginCount = (tjs_int)list.size();
	for(std::vector<tTVPFoundPlugin>::iterator i = list.begin();
		i != list.end();
		i++)
	{
		TVPAddImportantLog(ttstr(TJS_W("(info) Loading ")) + ttstr(i->Name.c_str()));
		TVPLoadPlugin((i->Path + "/" + i->Name).c_str());
	}
}
//---------------------------------------------------------------------------
tjs_int TVPGetAutoLoadPluginCount() { return TVPAutoLoadPluginCount; }
//---------------------------------------------------------------------------





//---------------------------------------------------------------------------
// some service functions for plugin
//---------------------------------------------------------------------------
#include <zlib.h>
int ZLIB_uncompress(unsigned char *dest, unsigned long *destlen,
	const unsigned char *source, unsigned long sourcelen)
{
	return uncompress(dest, destlen, source, sourcelen);
}
//---------------------------------------------------------------------------
int ZLIB_compress(unsigned char *dest, unsigned long *destlen,
	const unsigned char *source, unsigned long sourcelen)
{
	return compress(dest, destlen, source, sourcelen);
}
//---------------------------------------------------------------------------
int ZLIB_compress2(unsigned char *dest, unsigned long *destlen,
	const unsigned char *source, unsigned long sourcelen, int level)
{
	return compress2(dest, destlen, source, sourcelen, level);
}
//---------------------------------------------------------------------------
#include "md5.h"
static char TVP_assert_md5_state_t_size[
	 (sizeof(TVP_md5_state_t) >= sizeof(md5_state_t))];
	// if this errors, sizeof(TVP_md5_state_t) is not equal to sizeof(md5_state_t).
	// sizeof(TVP_md5_state_t) must be equal to sizeof(md5_state_t).
//---------------------------------------------------------------------------
void TVP_md5_init(TVP_md5_state_t *pms)
{
	md5_init((md5_state_t*)pms);
}
//---------------------------------------------------------------------------
void TVP_md5_append(TVP_md5_state_t *pms, const tjs_uint8 *data, int nbytes)
{
	md5_append((md5_state_t*)pms, (const md5_byte_t*)data, nbytes);
}
//---------------------------------------------------------------------------
void TVP_md5_finish(TVP_md5_state_t *pms, tjs_uint8 *digest)
{
	md5_finish((md5_state_t*)pms, digest);
}

//---------------------------------------------------------------------------
bool TVPRegisterGlobalObject(const tjs_char *name, iTJSDispatch2 * dsp)
{
	// register given object to global object
	tTJSVariant val(dsp);
	iTJSDispatch2 *global = TVPGetScriptDispatch();
	tjs_error er;
	try
	{
		er = global->PropSet(TJS_MEMBERENSURE, name, NULL, &val, global);
	}
	catch(...)
	{
		global->Release();
		return false;
	}
	global->Release();
	return TJS_SUCCEEDED(er);
}
//---------------------------------------------------------------------------
bool TVPRemoveGlobalObject(const tjs_char *name)
{
	// remove registration of global object
	iTJSDispatch2 *global = TVPGetScriptDispatch();
	if(!global) return false;
	tjs_error er;
	try
	{
		er = global->DeleteMember(0, name, NULL, global);
	}
	catch(...)
	{
		global->Release();
		return false;
	}
	global->Release();
	return TJS_SUCCEEDED(er);
}
//---------------------------------------------------------------------------
void TVPDoTryBlock(
	tTVPTryBlockFunction tryblock,
	tTVPCatchBlockFunction catchblock,
	tTVPFinallyBlockFunction finallyblock,
	void *data)
{
	try
	{
		tryblock(data);
	}
	catch(const eTJS & e)
	{
		if(finallyblock) finallyblock(data);
		tTVPExceptionDesc desc;
		desc.type = TJS_W("eTJS");
		desc.message = e.GetMessage();
		if(catchblock(data, desc)) throw;
		return;
	}
	catch(...)
	{
		if(finallyblock) finallyblock(data);
		tTVPExceptionDesc desc;
		desc.type = TJS_W("unknown");
		if(catchblock(data, desc)) throw;
		return;
	}
	if(finallyblock) finallyblock(data);
}
//---------------------------------------------------------------------------


#include "ncbind/ncbind.hpp"

void TVPLoadInternalPlugins() {
	// 如果插件不冲突，实际上可以加载全部
	ncbAutoRegister::AllRegist();
	ncbAutoRegister::LoadModule(TJS_W("xp3filter.dll"));
    ncbAutoRegister::LoadModule(TJS_W("varfile.dll"));
    ncbAutoRegister::LoadModule(TJS_W("shrinkCopy.dll"));
}

[[maybe_unused]] void TVPUnloadInternalPlugins() {
	ncbAutoRegister::AllUnregist();
}

bool TVPLoadInternalPlugin(const ttstr& _name) {
	return ncbAutoRegister::LoadModule(TVPExtractStorageName(_name));
}