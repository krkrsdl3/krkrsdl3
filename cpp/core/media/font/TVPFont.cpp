#include "tjsCommHead.h"
#include "TVPFont.h"
#include <ft2build.h>
#include FT_TRUETYPE_IDS_H
#include FT_SFNT_NAMES_H
#include FT_FREETYPE_H
#include "TVPStorage.h"
#include "TVPDebug.h"
#include "TVPMsg.h"
#include "Platform.h"
#include "StringUtil.h"
#include "TVPConfig.h"
#include <vector>
#ifdef _KRKRSDL3_OHOS
#include <SDL2/SDL.h>
#include <cstdio>
#endif

tTJSHashTable<ttstr, TVPFontNamePathInfo, tTVPttstrHash> TVPFontNames;

//---------------------------------------------------------------------------

static ttstr TVPDefaultFontName;
static tTVPFont TVPDefaultFont;

static FT_Library TVPFontLibrary;
static FT_Library& TVPGetFontLibrary()
{
    if (!TVPFontLibrary)
    {
        FT_Error error = FT_Init_FreeType(&TVPFontLibrary);
        if (error)
            TVPThrowExceptionMessage((ttstr(TJS_N("Initialize FreeType failed, error = ")) +
                                      TJSIntegerToString((tjs_int)error))
                                         .c_str());
        TVPInitFontNames();
    }
    return TVPFontLibrary;
}
static void TVPReleaseFontLibrary()
{
    if (TVPFontLibrary)
    {
        FT_Done_FreeType(TVPFontLibrary);
    }
}

static int TVPInternalEnumFonts(
    FT_Byte* pBuf,
    int buflen,
    const ttstr& FontPath,
    const std::function<tTJSBinaryStream*(TVPFontNamePathInfo*)>& getter)
{
    unsigned int faceCount = 0;
    FT_Face fontface;
    FT_Error error = FT_New_Memory_Face(TVPGetFontLibrary(), pBuf, buflen, 0, &fontface);
    if (error)
    {
        TVPAddLog(ttstr(TJS_N("Load Font \"") + FontPath + "\" failed (" +
                        TJSIntegerToString((int)error) + ")"));
        return faceCount;
    }
    int nFaceNum = fontface->num_faces;
    for (int i = 0; i < nFaceNum; ++i)
    {
        if (i > 0)
        {
            if (FT_New_Memory_Face(TVPGetFontLibrary(), pBuf, buflen, i, &fontface))
            {
                continue;
            }
        }
        if (FT_IS_SCALABLE(fontface))
        {
            FT_UInt namecount = FT_Get_Sfnt_Name_Count(fontface);
            int addCount = 0;
            for (FT_UInt i = 0; i < namecount; ++i)
            {
                FT_SfntName name;
                if (FT_Get_Sfnt_Name(fontface, i, &name))
                {
                    continue;
                }
                if (name.name_id != TT_NAME_ID_FONT_FAMILY)
                {
                    continue;
                }
                if (name.platform_id != TT_PLATFORM_MICROSOFT)
                {
                    continue;
                }
                switch (name.language_id)
                { // for CJK names
                    case TT_MS_LANGID_JAPANESE_JAPAN:
                    case TT_MS_LANGID_CHINESE_GENERAL:
                    case TT_MS_LANGID_CHINESE_TAIWAN:
                    case TT_MS_LANGID_CHINESE_PRC:
                    case TT_MS_LANGID_CHINESE_HONG_KONG:
                    case TT_MS_LANGID_CHINESE_SINGAPORE:
                    case TT_MS_LANGID_KOREAN_EXTENDED_WANSUNG_KOREA:
                    case TT_MS_LANGID_KOREAN_JOHAB_KOREA:
                        break;
                    default:
                        continue;
                }
                ttstr fontname;
                if (name.encoding_id == TT_MS_ID_UNICODE_CS)
                {
                    std::vector<tjs_char> tmp;
                    int namelen = name.string_len / 2;
                    tmp.resize(namelen + 1);
                    for (int j = 0; j < namelen; ++j)
                    {
                        tmp[j] = (name.string[j * 2] << 8) | (name.string[j * 2 + 1]);
                    }
                    fontname = &tmp.front();
                }
                else
                {
                    continue;
                }
                TVPFontNamePathInfo info;
                info.Path = FontPath;
                info.Index = i;
                info.Getter = getter;
                TVPFontNames.Add(fontname, info);
                addCount = 1;
            }
            /*if (!addCount)*/ {
                ttstr fontname((tjs_char*)fontface->family_name);
                TVPFontNamePathInfo info;
                info.Path = FontPath;
                info.Index = i;
                info.Getter = getter;
                TVPFontNames.Add(fontname, info);
            }
            ++faceCount;
        }

        FT_Done_Face(fontface);
    }
    return faceCount;
}

//---------------------------------------------------------------------------
void TVPGetAllFontList(std::vector<ttstr>& list)
{
    auto itend = TVPFontNames.GetLast();
    for (auto it = TVPFontNames.GetFirst(); it != itend; ++it)
    {
        list.push_back(it.GetKey());
    }
}

void TVPInitFontNames()
{
    static bool TVPFontNamesInit = false;
    // enumlate all fonts
    if (TVPFontNamesInit)
        return;
    TVPFontNamesInit = true;
    do
    {
        ttstr userFont = GameSetting::default_font;
        if (!userFont.IsEmpty() && TVPEnumFontsProc(userFont))
            break;

        if (TVPEnumFontsProc(TVPGetAppPath() + "default.ttf"))
            break;
        if (TVPEnumFontsProc(TVPGetAppPath() + "default.ttc"))
            break;
        if (TVPEnumFontsProc(TVPGetAppPath() + "default.otf"))
            break;
        if (TVPEnumFontsProc(TVPGetAppPath() + "default.otc"))
            break;

        auto stream = GetResourceStream("DroidSansFallback.ttf");
        if (stream != nullptr)
        {
            TVPInternalEnumFonts((tjs_uint8*)stream->GetInternalBuffer(), stream->GetSize(),
                                 "DroidSansFallback.ttf",
                                 [](TVPFontNamePathInfo* info) -> tTJSBinaryStream*
                                 { return GetResourceStream(info->Path.AsStdString()); });
            delete stream;
            break;
        }
#ifdef _KRKRSDL3_OHOS
        // HarmonyOS: probe system fonts directly via fopen()
        // (mirrors Kirikiroid2's KR2_HARMONY_BUILD path in FontImpl.cpp)
        {
            static const char* ohosFontPaths[] = {
                "/system/fonts/HarmonyOS_Sans_SC.ttf",
                "/system/fonts/NotoSansCJK-Regular.ttc",
                "/system/fonts/DroidSansFallback.ttf",
                "/system/fonts/NotoSansHans-Regular.otf",
                "/system/fonts/HarmonyOS_Sans.ttf",
                "/system/fonts/HarmonyOS_Sans_SC_Regular.ttf",
                nullptr
            };
            for (int i = 0; ohosFontPaths[i]; ++i) {
                FILE* fp = fopen(ohosFontPaths[i], "rb");
                if (!fp) continue;
                fseek(fp, 0, SEEK_END);
                long sz = ftell(fp);
                fseek(fp, 0, SEEK_SET);
                if (sz <= 0) { fclose(fp); continue; }
                std::vector<tjs_uint8> buf(sz);
                fread(&buf[0], 1, sz, fp);
                fclose(fp);
                ttstr fontPath(ohosFontPaths[i]);
                int count = TVPInternalEnumFonts(&buf[0], (int)sz, fontPath,
                    [](TVPFontNamePathInfo* info) -> tTJSBinaryStream* {
                        FILE* f = fopen(info->Path.AsStdString().c_str(), "rb");
                        if (!f) return nullptr;
                        fseek(f, 0, SEEK_END);
                        long s = ftell(f);
                        fseek(f, 0, SEEK_SET);
                        if (s <= 0) { fclose(f); return nullptr; }
                        tTVPMemoryStream* ret = new tTVPMemoryStream(nullptr, (tjs_uint)s);
                        fread(ret->GetInternalBuffer(), 1, s, f);
                        fclose(f);
                        return ret;
                    });
                if (count > 0) {
                    SDL_Log("[krkr2-font] Registered system font: %s (%d face(s))", ohosFontPaths[i], count);
                    break;
                }
            }
        }
#endif
    } while (false);
    if (TVPFontNames.GetCount() > 0)
    {
        // set default fontface name
        TVPDefaultFontName = TVPFontNames.GetLast().GetKey();
    }

    // check exePath + "/fonts/*.ttf"
    {
        std::vector<ttstr> list;
        auto lister = [&](const ttstr& name, tTVPLocalFileInfo* s)
        {
            if (s->Mode & (S_IFREG | S_IFDIR))
            {
                list.emplace_back(name);
            }
        };
        TVPGetLocalFileListAt(TVPGetAppPath() + "/fonts", lister);
        auto itend = list.end();
        for (auto it = list.begin(); it != itend; ++it)
        {
            TVPEnumFontsProc(*it);
        }
    }

    if (TVPDefaultFontName.IsEmpty())
    {
        TVPShowSimpleMessageBox(("Could not found any font.\nPlease ensure "
                                 "that at least \"default.ttf\" exists"),
                                "Exception Occured");
    }
}

int TVPEnumFontsProc(const ttstr& FontPath)
{
    if (!TVPIsExistentStorageNoSearch(FontPath))
    {
        return 0;
    }

    tTJSBinaryStream* Stream = TVPCreateStream(FontPath, TJS_BS_READ);
    if (!Stream)
    {
        return 0;
    }
    int bufflen = Stream->GetSize();
    std::vector<FT_Byte> buf;
    buf.resize(bufflen);
    Stream->ReadBuffer(&buf.front(), bufflen);
    delete Stream;
    return TVPInternalEnumFonts(&buf.front(), bufflen, FontPath, nullptr);
}

const ttstr& TVPGetDefaultFontName()
{
    return TVPDefaultFontName;
}

tTJSBinaryStream* TVPCreateFontStream(const ttstr& fontname)
{
    TVPFontNamePathInfo* info = TVPFindFont(fontname);
    if (!info)
    {
        info = TVPFontNames.Find(TVPDefaultFontName);
        if (!info)
            return nullptr;
    }
    if (info->Getter)
    {
        return info->Getter(info);
    }
    return TVPCreateBinaryStreamForRead(info->Path, TJS_N(""));
}

//---------------------------------------------------------------------------
TVPFontNamePathInfo* TVPFindFont(const ttstr& fontname)
{
    // check existence of font
    TVPInitFontNames();

    TVPFontNamePathInfo* info = nullptr;
    if (!fontname.IsEmpty() && fontname[0] == TJS_N('@'))
    { // vertical version
        info = TVPFontNames.Find(fontname.c_str() + 1);
    }
    if (!info)
    {
        info = TVPFontNames.Find(fontname);
    }
    return info;
}

tjs_uint32 tTVPttstrHash::Make(const ttstr& val)
{
    const tjs_char* ptr = val.c_str();
    if (*ptr == 0)
        return 0;
    tjs_uint32 v = 0;
    while (*ptr)
    {
        v += *ptr;
        v += (v << 10);
        v ^= (v >> 6);
        ptr++;
    }
    v += (v << 3);
    v ^= (v >> 11);
    v += (v << 15);
    if (!v)
        v = (tjs_uint32)-1;
    return v;
}

bool TVPFontExists(const ttstr& name)
{
    TVPFontNamePathInfo* t = TVPFontNames.Find(name);
    return t != NULL;
}

ttstr TVPGetBeingFont(ttstr fonts)
{
    // retrieve being font in the system.
    // font candidates are given by "fonts", separated by comma.

    bool vfont;

    if (fonts.c_str()[0] == TJS_N('@'))
    { // for vertical writing
        fonts = fonts.c_str() + 1;
        vfont = true;
    }
    else
    {
        vfont = false;
    }

    static bool force_default_font = GameSetting::force_default_font;
    if (!force_default_font)
    {
        bool prev_empty_name = false;
        while (fonts != TJS_N(""))
        {
            ttstr fontname;
            int pos = fonts.IndexOf(TJS_N(","));
            if (pos != -1)
            {
                fontname = Trim(fonts.SubString(0, pos));
                fonts = fonts.SubString(pos + 1, -1);
            }
            else
            {
                fontname = Trim(fonts);
                fonts = TJS_N("");
            }

            // no existing check if previously specified font candidate is empty
            // eg. ",Fontname"

            if (fontname != TJS_N("") && (prev_empty_name || TVPFontExists(fontname)))
            {
                if (vfont && fontname.c_str()[0] != TJS_N('@'))
                {
                    return TJS_N("@") + fontname;
                }
                else
                {
                    return fontname;
                }
            }

            prev_empty_name = (fontname == TJS_N(""));
        }
    }

    if (vfont)
    {
        return ttstr(TJS_N("@")) + TVPGetDefaultFontName();
    }
    else
    {
        return TVPGetDefaultFontName();
    }
}

void TVPCreateDefaultFont()
{
    static bool TVPDefaultLOGFONTCreated = false;
    if (TVPDefaultLOGFONTCreated)
        return;
    TVPDefaultLOGFONTCreated = true;
    TVPDefaultFont.Height = -12;
    TVPDefaultFont.Flags = 0;
    TVPDefaultFont.Angle = 0;
    TVPDefaultFont.Face = ttstr(TVPGetDefaultFontName());
}

const tTVPFont& TVPGetDefaultFont()
{
    return TVPDefaultFont;
}
