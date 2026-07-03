#include "tjsCommHead.h"
#include "eventCallbackFun.h"

#include "WindowIntf.h"
#include "tjsNativeMenuItem.h"

#include <SDL3/SDL.h>

#include "TVPSystem.h"
#include "TVPStorage.h"
#include "TVPDebug.h"
#include "TVPMsg.h"

extern SDL_Window* tvp_window;
extern iTJSDispatch2* TVPGetMenuDispatch(tTVInteger hWnd);
extern tTJSNI_Window *TVPGetActiveWindow();

#ifdef _KRKRSDL3_ANDROID
#include <jni.h>
static tTJSNI_MenuItem *sdl_current_menu = NULL;
extern "C" JNIEXPORT void JNICALL
Java_org_tvp_krkrsdl3_KRKRCall_nativeOnMenuItemClick(JNIEnv* env, jclass clazz, jint itemId, jstring itemCaption)
{
    // 获取菜单项目
    if(!sdl_current_menu || itemId >= sdl_current_menu->GetChildren().size()) return;
    tTJSNI_MenuItem* subitm = static_cast<tTJSNI_MenuItem*>(sdl_current_menu->GetChildren().at(itemId));
    // 条件判断
    if(!subitm->GetChildren().empty())
        krkrsdl3::SDL_Invoke_Menu(0,0, subitm);
    else
    {
        SDL_Event event;
        SDL_zero(event);
        event.type = SDL_EVENT_MENU_CLICK;
        event.user.data1 = subitm;
        SDL_PushEvent(&event);
    }
}
#endif
#ifdef _KRKRSDL3_WINDOWS
#include <windows.h>
static tTJSNI_MenuItem* sdl_current_menu = NULL;
static HWND sdl_hwnd = NULL;
struct MenuMapping
{
    int cmdId;
    tTJSNI_MenuItem* item;
};
static std::vector<MenuMapping> g_menuMap;
void BuildMenu(HMENU hMenu, tTJSNI_MenuItem* menuItem, int& idCounter)
{
    int count = menuItem->GetChildren().size();
    for (int i = 0; i < count; i++)
    {
        tTJSNI_MenuItem* subitem = static_cast<tTJSNI_MenuItem*>(menuItem->GetChildren().at(i));
        ttstr caption;
        subitem->GetCaption(caption);

        if (caption.IsEmpty() || caption == TJS_N("+"))
            continue;

        if (caption == TJS_N("-"))
        {
            AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
            continue;
        }
        if (!subitem->GetChildren().empty())
        {
            HMENU hSubMenu = CreatePopupMenu();
            BuildMenu(hSubMenu, subitem, idCounter);
            AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hSubMenu, caption.c_str());
        }
        else if (subitem->GetGroup() > 0 || subitem->GetRadio() || subitem->GetChecked())
        {
            UINT flags = MF_STRING;
            if (subitem->GetChecked())
                flags |= MF_CHECKED;
            int cmdId = ++idCounter;
            AppendMenu(hMenu, flags, cmdId, caption.c_str());
            g_menuMap.push_back({cmdId, subitem});
        }
        else
        {
            int cmdId = ++idCounter;
            AppendMenu(hMenu, MF_STRING, cmdId, caption.c_str());
            g_menuMap.push_back({cmdId, subitem});
        }
    }
}
static void ProcessMenuCommand(int cmdId)
{
    if (!sdl_current_menu)
        return;

    for (auto& entry : g_menuMap)
    {
        if (entry.cmdId == cmdId)
        {
            entry.item->OnClick();
            break;
        }
    }
}
#endif

namespace krkrsdl3
{

    void SDL_Invoke_Menu(int x, int y, void* _menu)
    {
#ifdef _KRKRSDL3_ANDROID
        // 获取菜单们
        if(_menu) sdl_current_menu = static_cast<tTJSNI_MenuItem*>(_menu);
        else
        {
            iTJSDispatch2 *menuobj = TVPGetMenuDispatch((tjs_intptr_t)TVPGetActiveWindow());
            if (!menuobj) return;
            menuobj->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
                                           tTJSNC_MenuItem::ClassID, (iTJSNativeInstance**)&sdl_current_menu);
            if (sdl_current_menu->GetChildren().empty()) return;
        }
        JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();

        // 构建java数据
        int count = sdl_current_menu->GetChildren().size();
        jclass dataClass = env->FindClass("org/tvp/krkrsdl3/KRKRCall$MenuItemData");
        jmethodID constructor = env->GetMethodID(dataClass, "<init>", "(ILjava/lang/String;)V");
        jobjectArray itemArray = env->NewObjectArray(count, dataClass, NULL);
        ttstr seperator = TJS_N("-");
        for (int i = 0; i < count; i++) {
            tTJSNI_MenuItem *subitem = static_cast<tTJSNI_MenuItem*>(sdl_current_menu->GetChildren().at(i));
            ttstr captions; subitem->GetCaption(captions);
            if (captions.IsEmpty() || captions == TJS_N("+")) continue;
            jstring caption = env->NewStringUTF(captions.c_str());
            jobject jitem = env->NewObject(dataClass, constructor, i, caption);
            jfieldID typeField = env->GetFieldID(dataClass, "type", "Lorg/tvp/krkrsdl3/KRKRCall$MenuItemType;");
            jclass typeClass = env->FindClass("org/tvp/krkrsdl3/KRKRCall$MenuItemType");
            jfieldID typeValue = NULL;
            if(!subitem->GetChildren().empty())
                typeValue = env->GetStaticFieldID(typeClass, "SUBMENU", "Lorg/tvp/krkrsdl3/KRKRCall$MenuItemType;");
            else if(subitem->GetGroup() > 0 || subitem->GetRadio())
                typeValue = env->GetStaticFieldID(typeClass, "NORMAL", "Lorg/tvp/krkrsdl3/KRKRCall$MenuItemType;");
            else if(subitem->GetChecked())
                typeValue = env->GetStaticFieldID(typeClass, "CHECKBOX", "Lorg/tvp/krkrsdl3/KRKRCall$MenuItemType;");
            else if(captions == "-")
                typeValue = env->GetStaticFieldID(typeClass, "SEPARATOR", "Lorg/tvp/krkrsdl3/KRKRCall$MenuItemType;");
            else
                typeValue = env->GetStaticFieldID(typeClass, "NORMAL", "Lorg/tvp/krkrsdl3/KRKRCall$MenuItemType;");
            jobject typeObj = env->GetStaticObjectField(typeClass, typeValue);
            env->SetObjectField(jitem, typeField, typeObj);
            jfieldID checkedField = env->GetFieldID(dataClass, "checked", "Z");
            env->SetBooleanField(jitem, checkedField, subitem->GetChecked());
            env->SetObjectArrayElement(itemArray, i, jitem);
            env->DeleteLocalRef(caption);
            env->DeleteLocalRef(jitem);

        }

        // 调用系统显示
        jclass cls = env->FindClass("org/tvp/krkrsdl3/KRKRCall");
        jmethodID method = env->GetStaticMethodID(cls, "showDynamicMenu", "(II[Lorg/tvp/krkrsdl3/KRKRCall$MenuItemData;)V");
        env->CallStaticVoidMethod(cls, method, x, y, itemArray);
        env->DeleteLocalRef(cls);
        env->DeleteLocalRef(dataClass);
        env->DeleteLocalRef(itemArray);
#endif
#ifdef _KRKRSDL3_WINDOWS
        if (_menu)
        {
            sdl_current_menu = static_cast<tTJSNI_MenuItem*>(_menu);
        }
        else
        {
            iTJSDispatch2* menuobj = TVPGetMenuDispatch((tjs_intptr_t)TVPGetActiveWindow());
            if (!menuobj)
                return;
            menuobj->NativeInstanceSupport(TJS_NIS_GETINSTANCE, tTJSNC_MenuItem::ClassID,
                                           (iTJSNativeInstance**)&sdl_current_menu);
            if (sdl_current_menu->GetChildren().empty())
                return;
        }
        if (!sdl_hwnd)
        {
            sdl_hwnd = GetActiveWindow();
            if (!sdl_hwnd)
                return;
        }
        HMENU hMenu = CreatePopupMenu();
        int idCounter = 0;
        g_menuMap.clear();
        BuildMenu(hMenu, sdl_current_menu, idCounter);
        int cmdId = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, x, y, 0,
                                   sdl_hwnd,
                       NULL);
        DestroyMenu(hMenu);
        if (cmdId > 0)
        {
            ProcessMenuCommand(cmdId);
        }
#endif
    }

    void SDL_Trig_Menu(void* data)
    {
        if(data)
        {
            tTJSNI_MenuItem* subitm = static_cast<tTJSNI_MenuItem*>(data);
            subitm->OnClick();
        }
    }

    static std::vector<ttstr> TVPProgramArguments;
    KRKR_Settings settings;
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
        // as a string
        return optname + p;
    }
    static void TVPInitProgramArgumentsAndDataPath(int tvp_argc, char* tvp_argv[])
    {
        // find options from self executable image
        try
        {
            bool argument_stopped = false;
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
            TVPNativeDataPath = GetDataPathDirectory(TVPNativeProjectDir);
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
    }

    void KRKR_ParseArguments(int argc, char* argv[])
    {
#ifdef _KRKRSDL3_LIB
        TVPNativeProjectDir = std::string(argv[1]);
#else
        size_t lastSlash = std::string(argv[0]).find_last_of("/\\");
        if (lastSlash != std::string::npos)
        {
            TVPNativeProjectDir = std::string(argv[0]).substr(0, lastSlash + 1) + "Res";
        }
#endif

        TVPProjectDir = TVPNormalizeStorageName(argv[1]);

        TVPInitProgramArgumentsAndDataPath(argc, argv);

        TVPDumpOptions();

        // GameSettings
        settings.ogl_accurate_render = false;
        settings.default_font = "";
        settings.force_default_font = false;
        settings.software_draw_thread = 0;
        settings.renderer = "software";
    }
    bool KRKR_GetCommandLine(const char* name, std::string* value)
    {
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
                        *value = (char*)p;
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
    void KRKR_SetCommandLine(const char* name, const char* value)
    {
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
                    return;
                }
            }
        }

        // value not found; insert argument into front
        TVPProgramArguments.insert(TVPProgramArguments.begin(), ttstr(name) + TJS_N("=") + value);
    }
}