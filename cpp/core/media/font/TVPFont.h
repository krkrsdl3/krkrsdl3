#pragma once
#include "tjs.h"
#include "tjsHashSearch.h"
#include "TVPFontstruc.h"

void TVPGetAllFontList(std::vector<ttstr> &list);
void TVPInitFontNames();
int TVPEnumFontsProc(const ttstr &FontPath);
const ttstr &TVPGetDefaultFontName();
tTJSBinaryStream* TVPCreateFontStream(const ttstr &fontname);

struct TVPFontNamePathInfo {
    ttstr Path;
	std::function<tTJSBinaryStream*(TVPFontNamePathInfo*)> Getter;
    int Index;
};
TVPFontNamePathInfo* TVPFindFont(const ttstr &name);

//---------------------------------------------------------------------------
// font enumeration and existence check
//---------------------------------------------------------------------------
class tTVPttstrHash
{
public:
    static tjs_uint32 Make(const ttstr &val);
};
extern tTJSHashTable<ttstr, TVPFontNamePathInfo, tTVPttstrHash>
    TVPFontNames;

bool TVPFontExists(const ttstr &name);
ttstr TVPGetBeingFont(ttstr fonts);
void TVPCreateDefaultFont();
const tTVPFont &TVPGetDefaultFont();
