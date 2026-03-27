#include "tjsCommHead.h"
#include "eventCallbackFun.h"

#include "WindowIntf.h"
#include "tjsNativeMenuItem.h"

#include <SDL3/SDL.h>

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
}