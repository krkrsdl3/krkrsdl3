// WIN32Dialog 兼容插件（全平台单一实现）
//
// 原插件基于 Win32 对话框模板与通用控件。这里保留相同的 TJS 类、方法与常量，
// 改用引擎自身的窗口/对话框接口（Platform.h）实现：
//   - 消息框 / 颜色选择：TVPShowSimpleMessageBox / TVPShowSimpleInputBox
//   - 文本框 / 下拉框 / 列表框：弹出输入框，输入文本或序号
//   - 复选框 / 单选框：是/否选择
//   - 按钮：作为对话框的返回值（IDOK / IDCANCEL / 自定义 ID）
// WASM 下消息框与输入框走浏览器原生 dialog。

#include "ncbind/ncbind.hpp"
#include "Platform.h"
#include "PlatformAutoTest.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#ifdef _KRKRSDL3_EMSCRIPTEN
#include <emscripten.h>

EM_JS(int, krkr_js_prompt, (const char* title, const char* prompt, const char* def, char* out, int outlen), {
    var r = window.prompt(UTF8ToString(prompt), UTF8ToString(def));
    if (r === null) return 0;
    stringToUTF8(r, out, outlen);
    return 1;
});

EM_JS(int, krkr_js_choose, (const char* caption, const char* text, const char* labels, int count), {
    var msg = UTF8ToString(caption) + "\n\n" + UTF8ToString(text);
    if (count <= 1) { window.alert(msg); return 0; }
    if (count === 2) return window.confirm(msg) ? 0 : 1;
    var r = window.prompt(msg + "\n" + UTF8ToString(labels), "1");
    if (r === null) return 0;
    var n = parseInt(r, 10);
    return (n >= 1 && n <= count) ? n - 1 : 0;
});
#endif

#define NCB_MODULE_NAME TJS_N("win32dialog.dll")

namespace w32
{
enum
{
    WM_INITDIALOG = 0x0110,
    MB_OK = 0x00000000,
    MB_OKCANCEL = 0x00000001,
    MB_ABORTRETRYIGNORE = 0x00000002,
    MB_YESNOCANCEL = 0x00000003,
    MB_YESNO = 0x00000004,
    MB_RETRYCANCEL = 0x00000005,
    MB_CANCELTRYCONTINUE = 0x00000006,
    MB_TYPEMASK = 0x0000000F,
    IDOK = 1,
    IDCANCEL = 2,
    IDABORT = 3,
    IDRETRY = 4,
    IDIGNORE = 5,
    IDYES = 6,
    IDNO = 7,
    IDTRYAGAIN = 10,
    IDCONTINUE = 11,
    BM_GETCHECK = 0x00F0,
    BM_SETCHECK = 0x00F1,
    BM_GETSTATE = 0x00F2,
    BM_SETSTATE = 0x00F3,
    BM_SETSTYLE = 0x00F4,
    BM_CLICK = 0x00F5,
    CB_ADDSTRING = 0x0143,
    CB_INSERTSTRING = 0x014A,
    CB_GETCOUNT = 0x0146,
    CB_GETCURSEL = 0x0147,
    CB_SETCURSEL = 0x014E,
    CB_RESETCONTENT = 0x014B,
    LB_ADDSTRING = 0x0180,
    LB_INSERTSTRING = 0x0181,
    LB_GETCOUNT = 0x018B,
    LB_GETCURSEL = 0x0188,
    LB_SETCURSEL = 0x0186,
    LB_GETSEL = 0x00C7,
    LB_RESETCONTENT = 0x0184,
    SBM_SETPOS = 0x00E0,
    SBM_GETPOS = 0x00E1,
    WND_BUTTON = 0x0080,
    WND_EDIT = 0x0081,
    WND_STATIC = 0x0082,
    WND_LISTBOX = 0x0083,
    WND_SCROLLBAR = 0x0084,
    WND_COMBOBOX = 0x0085,
    BS_TYPEMASK = 0x0000000F,
    BS_PUSHBUTTON = 0,
    BS_DEFPUSHBUTTON = 1,
    BS_CHECKBOX = 2,
    BS_AUTOCHECKBOX = 3,
    BS_RADIOBUTTON = 4,
    BS_GROUPBOX = 7,
    BS_AUTORADIOBUTTON = 9,
    ES_MULTILINE = 0x0004,
    ES_READONLY = 0x0800,
    CBS_DROPDOWNLIST = 0x0003,
    CBS_TYPEMASK = 0x0003,
    LBS_MULTIPLESEL = 0x0008,
};
}

static std::string ToStd(const ttstr& s) { return s.AsStdString(); }
static ttstr ToTT(const std::string& s) { return ttstr(s.c_str()); }

static tjs_int PropInt(ncbPropAccessor& p, const tjs_char* name, tjs_int def = 0)
{
    tjs_uint32 hint = 0;
    tTJSVariantType type = tvtVoid;
    if (!p.HasValue(name, &hint, &type))
        return def;
    if (type == tvtInteger || type == tvtReal)
        return p.getIntValue(name);
    return def;
}

static std::string PropStr(ncbPropAccessor& p, const tjs_char* name)
{
    tjs_uint32 hint = 0;
    tTJSVariantType type = tvtVoid;
    if (!p.HasValue(name, &hint, &type) || type != tvtString)
        return std::string();
    return ToStd(p.getStrValue(name));
}

static int ClassFromName(const std::string& name)
{
    std::string cls;
    for (char c : name)
        cls += (char)tolower((unsigned char)c);
    if (cls == "button") return w32::WND_BUTTON;
    if (cls == "edit") return w32::WND_EDIT;
    if (cls == "static") return w32::WND_STATIC;
    if (cls == "listbox") return w32::WND_LISTBOX;
    if (cls == "scrollbar") return w32::WND_SCROLLBAR;
    if (cls == "combobox") return w32::WND_COMBOBOX;
    return 0;
}

static const char* ClassName(int cls)
{
    switch (cls)
    {
    case w32::WND_BUTTON: return "button";
    case w32::WND_EDIT: return "edit";
    case w32::WND_STATIC: return "static";
    case w32::WND_LISTBOX: return "listbox";
    case w32::WND_SCROLLBAR: return "scrollbar";
    case w32::WND_COMBOBOX: return "combobox";
    default: return "";
    }
}

static bool ShowInput(const ttstr& caption, const ttstr& prompt, std::string& value)
{
    if (krkrsdl3::TVPAutoTestActive() && krkrsdl3::TVPAutoTestOffscreen())
        return true;
#ifdef _KRKRSDL3_EMSCRIPTEN
    char buf[4096];
    std::snprintf(buf, sizeof(buf), "%s", value.c_str());
    if (!krkr_js_prompt(caption.c_str(), prompt.c_str(), value.c_str(), buf, (int)sizeof(buf)))
        return false;
    value = buf;
    return true;
#else
    ttstr text(value.c_str());
    int r = TVPShowSimpleInputBox(text, caption, prompt, std::vector<ttstr>{ttstr("OK"), ttstr("Cancel")});
    if (r != 0)
        return false;
    value = ToStd(text);
    return true;
#endif
}

static int ShowChoice(const ttstr& caption, const ttstr& text, const std::vector<ttstr>& buttons)
{
    if (buttons.empty())
        return 0;
    if (krkrsdl3::TVPAutoTestActive() && krkrsdl3::TVPAutoTestOffscreen())
        return 0;
#ifdef _KRKRSDL3_EMSCRIPTEN
    std::string labels;
    for (size_t i = 0; i < buttons.size(); ++i)
    {
        labels += std::to_string(i + 1);
        labels += "=";
        labels += ToStd(buttons[i]);
        labels += "\n";
    }
    return krkr_js_choose(caption.c_str(), text.c_str(), labels.c_str(), (int)buttons.size());
#else
    int idx = TVPShowSimpleMessageBox(text, caption, buttons);
    if (idx < 0 || idx >= (int)buttons.size())
        idx = 0;
    return idx;
#endif
}

struct PItem
{
    int id = -1;
    int cls = 0;
    tjs_uint32 style = 0;
    int x = 0, y = 0, cx = 0, cy = 0;
    std::string title;
    std::string text;
    std::vector<std::string> strings;
    int sel = -1;
    int check = 0;
    bool enabled = true;
};

struct Header;
struct Items;
class WIN32Dialog;

struct Bitmap
{
    iTJSDispatch2* lay = nullptr;
    Bitmap(iTJSDispatch2* layer) : lay(layer) { if (lay) lay->AddRef(); }
    ~Bitmap() { if (lay) lay->Release(); }
};

struct SolidBrush
{
    tjs_uint32 color = 0;
    SolidBrush(tjs_uint32 rgb) : color(rgb) {}
};

struct DrawItem
{
    tjs_uint32 GetCtlType() const { return 0; }
    tjs_uint32 GetCtlID() const { return 0; }
    tjs_uint32 GetItemID() const { return 0; }
    tjs_uint32 GetItemAction() const { return 0; }
    tjs_uint32 GetItemState() const { return 0; }
    tjs_uint32 GetItemData() const { return 0; }
    tjs_int64 GetHWndItem() const { return 0; }
    tTJSVariant GetItemRect() const
    {
        ncbDictionaryAccessor dict;
        if (dict.IsValid())
        {
            dict.SetValue(TJS_N("x"), 0);
            dict.SetValue(TJS_N("y"), 0);
            dict.SetValue(TJS_N("w"), 0);
            dict.SetValue(TJS_N("h"), 0);
            return tTJSVariant(dict, dict);
        }
        return tTJSVariant();
    }
    void draw(Bitmap*, tjs_int, tjs_int) {}
};

struct NotifyAccessor
{
    tjs_int64 GetHWndFrom() const { return 0; }
    tjs_uint32 GetIdFrom() const { return 0; }
    tjs_uint32 GetCode() const { return 0; }
    tjs_uint8 GetByte(int) const { return 0; }
    tjs_uint16 GetWord(int) const { return 0; }
    tjs_uint32 GetDWord(int) const { return 0; }
};

struct Blob
{
    tjs_uint8* ptr = nullptr;
    tjs_uint32 size = 0;
    Blob(tjs_uint32 sz) : size(sz)
    {
        if (size)
        {
            ptr = new tjs_uint8[size];
            std::memset(ptr, 0, size);
        }
    }
    Blob(tjs_uint8* p) : ptr(p), size(0) {}
    ~Blob() { if (size) delete[] ptr; }
    tjs_int64 GetPointer() const { return (tjs_int64)(tjs_intptr_t)ptr; }
    tjs_uint8 GetByte(int ofs) const { return ptr[ofs]; }
    tjs_uint16 GetWord(int ofs) const { return (tjs_uint16)(GetByte(ofs) | (GetByte(ofs + 1) << 8)); }
    tjs_uint32 GetDWord(int ofs) const { return (tjs_uint32)(GetWord(ofs) | (GetWord(ofs + 2) << 16)); }
    tjs_int64 GetDWordLong(int ofs) const { return (tjs_int64)(GetDWord(ofs) | ((tjs_int64)GetDWord(ofs + 4) << 32)); }
    void SetByte(int ofs, tjs_uint8 v) { ptr[ofs] = v; }
    void SetWord(int ofs, tjs_uint16 v) { SetByte(ofs, (tjs_uint8)v); SetByte(ofs + 1, (tjs_uint8)(v >> 8)); }
    void SetDWord(int ofs, tjs_uint32 v) { SetWord(ofs, (tjs_uint16)v); SetWord(ofs + 2, (tjs_uint16)(v >> 16)); }
    void SetDWordLong(int ofs, tjs_int64 v) { SetDWord(ofs, (tjs_uint32)v); SetDWord(ofs + 4, (tjs_uint32)(v >> 32)); }
    ttstr GetText(int ofs) const { return (const tjs_char*)(tjs_intptr_t)GetDWordLong(ofs); }
    void SetText(int ofs, const tjs_char* text) { SetDWordLong(ofs, (tjs_int64)(tjs_intptr_t)text); }
    static Blob* ReferPointer(tjs_int64 p) { return new Blob((tjs_uint8*)(tjs_intptr_t)p); }
};

struct Header
{
    std::string title;
    int cx = 0, cy = 0, pointSize = 9;
    int dlgItems = 0;

    void store(tTJSVariant elmobj)
    {
        ncbPropAccessor elm(elmobj);
        title = PropStr(elm, TJS_N("title"));
        cx = PropInt(elm, TJS_N("cx"));
        cy = PropInt(elm, TJS_N("cy"));
        pointSize = PropInt(elm, TJS_N("pointSize"), 9);
    }
    int get_dlgItems() const { return dlgItems; }
    void set_dlgItems(int n) { dlgItems = n; }
};

struct Items
{
    PItem item;

    void store(tTJSVariant elmobj)
    {
        ncbPropAccessor elm(elmobj);
        item.id = PropInt(elm, TJS_N("id"), -1);
        item.style = (tjs_uint32)PropInt(elm, TJS_N("style"));
        item.x = PropInt(elm, TJS_N("x"));
        item.y = PropInt(elm, TJS_N("y"));
        item.cx = PropInt(elm, TJS_N("cx"));
        item.cy = PropInt(elm, TJS_N("cy"));
        item.title = PropStr(elm, TJS_N("title"));

        tjs_uint32 hint = 0;
        tTJSVariantType type = tvtVoid;
        if (elm.HasValue(TJS_N("windowClass"), &hint, &type))
        {
            if (type == tvtString)
                item.cls = ClassFromName(PropStr(elm, TJS_N("windowClass")));
            else
                item.cls = PropInt(elm, TJS_N("windowClass"));
        }
    }
};

class WIN32Dialog
{
public:
    typedef ncbPropAccessor PropT;
    typedef ncbDictionaryAccessor DictT;
    typedef tTJSVariant VarT;
    typedef iTJSDispatch2 DspT;
    typedef float ProgressValueT;

private:
    typedef ncbInstanceAdaptor<WIN32Dialog> SelfAdaptorT;
    typedef ncbInstanceAdaptor<Header> HeadAdaptorT;
    typedef ncbInstanceAdaptor<Items> ItemAdaptorT;
    typedef ncbInstanceAdaptor<Bitmap> BitmapAdaptorT;
    typedef ncbInstanceAdaptor<SolidBrush> SolidBrushAdaptorT;

    DspT* owner = nullptr;
    DspT* objthis = nullptr;
    bool modeless = false;
    bool opened = false;
    bool progressMode = false;
    bool progressCanceled = false;
    ProgressValueT progressValue = 0;
    int resultId = 0;
    int winX = 0, winY = 0, winW = 0, winH = 0;
    std::string title;
    int dlgCx = 0, dlgCy = 0;
    std::vector<PItem> items;
    std::map<int, size_t> itemMap;
    std::vector<std::string> tabs;
    int curTab = 0;
    VarT iconBitmap;

    PItem* findItem(int id)
    {
        auto it = itemMap.find(id);
        return (it == itemMap.end()) ? nullptr : &items[it->second];
    }

    DspT* _hasCallback(const tjs_char* event)
    {
        DspT* obj = owner ? owner : objthis;
        if (!obj)
            return nullptr;
        VarT rslt;
        tjs_uint32 hint = 0;
        if (TJS_FAILED(obj->PropGet(TJS_MEMBERMUSTEXIST, event, &hint, &rslt, obj)))
            return nullptr;
        return obj;
    }

    bool callback(const tjs_char* event, tjs_int numparams, VarT** params)
    {
        DspT* obj = _hasCallback(event);
        if (!obj)
            return false;
        VarT rslt;
        obj->FuncCall(0, event, 0, &rslt, numparams, params, obj);
        return rslt.AsInteger() != 0;
    }

    bool callback(const tjs_char* event, tjs_int msg, tjs_int wp, tjs_int64 lp)
    {
        VarT vmsg(msg), vwp(wp), vlp(lp);
        VarT* params[] = {&vmsg, &vwp, &vlp};
        return callback(event, 3, params);
    }

    static bool IsCheckable(const PItem& item)
    {
        if (item.cls != w32::WND_BUTTON)
            return false;
        tjs_uint32 type = item.style & w32::BS_TYPEMASK;
        return type == w32::BS_CHECKBOX || type == w32::BS_AUTOCHECKBOX ||
               type == w32::BS_RADIOBUTTON || type == w32::BS_AUTORADIOBUTTON;
    }

    static bool IsPushButton(const PItem& item)
    {
        if (item.cls != w32::WND_BUTTON)
            return false;
        tjs_uint32 type = item.style & w32::BS_TYPEMASK;
        return type == w32::BS_PUSHBUTTON || type == w32::BS_DEFPUSHBUTTON;
    }

    bool promptItem(const PItem& item, const std::string& label)
    {
        std::string prompt = label;
        if (prompt.empty())
            prompt = item.title;
        if (prompt.empty())
            prompt = "ID " + std::to_string(item.id);

        switch (item.cls)
        {
        case w32::WND_EDIT:
        {
            if (item.style & w32::ES_READONLY)
                return true;
            PItem* cur = findItem(item.id);
            if (!cur)
                return true;
            std::string value = cur->text;
            if (!ShowInput(ToTT(title), ToTT(prompt), value))
            {
                resultId = w32::IDCANCEL;
                return false;
            }
            cur->text = value;
            return true;
        }
        case w32::WND_COMBOBOX:
        case w32::WND_LISTBOX:
        {
            PItem* cur = findItem(item.id);
            if (!cur)
                return true;
            bool dropdownList = item.cls == w32::WND_COMBOBOX &&
                                (item.style & w32::CBS_TYPEMASK) == w32::CBS_DROPDOWNLIST;
            if (dropdownList || item.cls == w32::WND_LISTBOX)
            {
                if (cur->strings.empty())
                    return true;
                std::string text = prompt + "\n";
                for (size_t i = 0; i < cur->strings.size(); ++i)
                {
                    text += std::to_string(i);
                    text += ": ";
                    text += cur->strings[i];
                    text += "\n";
                }
                text += "输入序号";
                std::string value = std::to_string(cur->sel < 0 ? 0 : cur->sel);
                if (!ShowInput(ToTT(title), ToTT(text), value))
                {
                    resultId = w32::IDCANCEL;
                    return false;
                }
                int sel = std::atoi(value.c_str());
                if (sel >= 0 && sel < (int)cur->strings.size())
                    cur->sel = sel;
                return true;
            }
            // 可输入的组合框：编辑文本
            std::string value = cur->text;
            if (!ShowInput(ToTT(title), ToTT(prompt), value))
            {
                resultId = w32::IDCANCEL;
                return false;
            }
            cur->text = value;
            return true;
        }
        default:
            return true;
        }
    }

    void runForm()
    {
        std::string label;
        for (size_t i = 0; i < items.size(); ++i)
        {
            PItem& item = items[i];
            if (item.cls == w32::WND_STATIC || (item.cls == w32::WND_BUTTON &&
                (item.style & w32::BS_TYPEMASK) == w32::BS_GROUPBOX))
            {
                if (!item.title.empty())
                    label = item.title;
                continue;
            }
            if (IsCheckable(item))
            {
                PItem* cur = findItem(item.id);
                if (!cur)
                    continue;
                std::string text = label.empty() ? item.title : label;
                std::vector<ttstr> buttons{ttstr("是"), ttstr("否")};
                if (ShowChoice(ToTT(title), ToTT(text), buttons) == 0)
                    cur->check = 1;
                continue;
            }
            if (!promptItem(item, label))
                return;
        }

        std::vector<PItem*> buttons;
        for (auto& item : items)
            if (IsPushButton(item))
                buttons.push_back(&item);

        if (buttons.empty())
        {
            resultId = w32::IDOK;
            return;
        }
        if (buttons.size() == 1)
        {
            resultId = buttons[0]->id;
            return;
        }
        std::vector<ttstr> labels;
        for (auto* btn : buttons)
            labels.push_back(ToTT(btn->title.empty() ? ("ID " + std::to_string(btn->id)) : btn->title));
        resultId = buttons[(size_t)ShowChoice(ToTT(title), ToTT("请选择操作"), labels)]->id;
    }

    int _open(VarT win)
    {
        (void)win;
        if (opened)
            return resultId;
        // 让 TJS 层完成控件初始值设置（WIN32DialogEX.onInit -> initItems）
        callback(TJS_N("onInit"), (tjs_int)w32::WM_INITDIALOG, 0, 0);
        winW = dlgCx;
        winH = dlgCy;
        if (modeless)
        {
            opened = true;
            resultId = 0;
            return resultId;
        }
        runForm();

        // 对应 Win32 的按钮消息循环：onCommand -> defaultCommand -> close()
        // close() 里会执行 TJS 层的 makeResults()，否则调用方拿不到 items 结果。
        opened = true;
        callback(TJS_N("onCommand"), (tjs_int)0x0111 /* WM_COMMAND */, (tjs_int)resultId, 0);
        if (opened)
        {
            DspT* obj = owner ? owner : objthis;
            if (obj)
            {
                tTJSVariant arg((tjs_int)resultId);
                tTJSVariant* args[] = {&arg};
                tTJSVariant rslt;
                obj->FuncCall(0, TJS_N("close"), 0, &rslt, 1, args, obj);
            }
            opened = false;
        }
        return resultId;
    }

public:
    WIN32Dialog(DspT* _owner = nullptr) : owner(_owner) {}

    static tjs_error makeTemplate(VarT* result, tjs_int numparams, VarT** param, WIN32Dialog* self);
    static tjs_error open(VarT* result, tjs_int numparams, VarT** param, DspT* objthis);
    static tjs_error loadResource_noop(VarT*, tjs_int, VarT**, WIN32Dialog*) { return TJS_S_OK; }

    tjs_int close(int id)
    {
        if (!opened)
            return 0;
        opened = false;
        return id;
    }

    tjs_int64 getHWND() const { return opened ? 1 : 0; }
    bool getModeless() const { return modeless; }
    void setModeless(bool b) { modeless = b; }
    void show(int) {}
    bool IsValid() const { return opened; }

    bool onInit(long, long, long) { return false; }
    bool onCommand(long, long, long) { return false; }
    bool onNotify(long, NotifyAccessor*) { return false; }
    bool onHScroll(long, long, long) { return false; }
    bool onVScroll(long, long, long) { return false; }
    bool onSize(long, long, long) { return false; }

    VarT getDialogIcon() const { return iconBitmap; }
    void setDialogIcon(VarT bmp) { iconBitmap = bmp; }

    tjs_int64 GetItem(int id) const { return itemMap.count(id) ? id : 0; }
    tjs_int GetItemID(tjs_int64 id) const { return (tjs_int)id; }
    VarT GetItemClassName(int id) const
    {
        auto it = itemMap.find(id);
        return (it == itemMap.end()) ? VarT() : VarT(ClassName(items[it->second].cls));
    }

    tjs_int64 GetItemLong(int id, int index)
    {
        PItem* item = findItem(id);
        if (!item)
            return 0;
        switch (index)
        {
        case -16: return item->style;   // GWL_STYLE
        case -20: return item->style;   // GWL_EXSTYLE
        case -12: return item->id;      // GWL_ID
        default: return 0;
        }
    }
    tjs_int64 SetItemLong(int id, int index, tjs_int64 value)
    {
        PItem* item = findItem(id);
        if (!item)
            return 0;
        tjs_int64 old = GetItemLong(id, index);
        if (index == -16)
            item->style = (tjs_uint32)value;
        return old;
    }
    tjs_int GetItemInt(int id) const
    {
        auto it = itemMap.find(id);
        if (it == itemMap.end())
            return 0;
        return std::atoi(items[it->second].text.c_str());
    }
    void SetItemInt(int id, int value)
    {
        PItem* item = findItem(id);
        if (item)
            item->text = std::to_string(value);
    }
    ttstr GetItemText(int id) const
    {
        if (id == 0)
            return ToTT(title);
        auto it = itemMap.find(id);
        return (it == itemMap.end()) ? ttstr() : ToTT(items[it->second].text);
    }
    void SetItemText(int id, const tjs_char* text)
    {
        if (id == 0)
        {
            title = text ? text : "";
            return;
        }
        PItem* item = findItem(id);
        if (item)
            item->text = text ? text : "";
    }
    void SetItemEnabled(int id, bool en)
    {
        PItem* item = findItem(id);
        if (item)
            item->enabled = en;
    }
    bool GetItemEnabled(int id)
    {
        PItem* item = findItem(id);
        return item ? item->enabled : false;
    }
    void SetItemFocus(int) {}

    long GetItemLeft(int id) const { auto it = itemMap.find(id); return it == itemMap.end() ? 0 : items[it->second].x; }
    long GetItemTop(int id) const { auto it = itemMap.find(id); return it == itemMap.end() ? 0 : items[it->second].y; }
    long GetItemWidth(int id) const { auto it = itemMap.find(id); return it == itemMap.end() ? 0 : items[it->second].cx; }
    long GetItemHeight(int id) const { auto it = itemMap.find(id); return it == itemMap.end() ? 0 : items[it->second].cy; }
    void SetItemPos(int id, int x, int y)
    {
        PItem* item = findItem(id);
        if (item) { item->x = x; item->y = y; }
    }
    void SetItemSize(int id, int w, int h)
    {
        PItem* item = findItem(id);
        if (item) { item->cx = w; item->cy = h; }
    }

    static tjs_error SetItemBitmap(VarT* result, tjs_int numparams, VarT** param, WIN32Dialog* self)
    {
        (void)result; (void)numparams; (void)param; (void)self;
        return TJS_S_OK;
    }

    bool IsExistentItem(int id) const { return id == 0 || itemMap.count(id) != 0; }
    bool LockItemUpdate(int) const { return false; }
    bool UnlockItemUpdate() const { return true; }

    static tjs_error sendItemMessage(VarT* result, tjs_int numparams, VarT** param, WIN32Dialog* self);

    VarT GetBaseUnits() const
    {
        VarT var;
        DictT dict;
        if (dict.IsValid())
        {
            dict.SetValue(TJS_N("h"), 8);
            dict.SetValue(TJS_N("v"), 16);
            var = dict;
        }
        return var;
    }
    VarT MapRect(VarT in) const
    {
        VarT var;
        PropT from(in);
        DictT to;
        if (from.IsValid() && to.IsValid())
        {
            to.SetValue(TJS_N("left"), PropInt(from, TJS_N("left")) * 2);
            to.SetValue(TJS_N("top"), PropInt(from, TJS_N("top")) * 2);
            to.SetValue(TJS_N("right"), PropInt(from, TJS_N("right")) * 2);
            to.SetValue(TJS_N("bottom"), PropInt(from, TJS_N("bottom")) * 2);
            var = to;
        }
        return var;
    }
    bool InvalidateRect(VarT, bool) { return true; }
    bool InvalidateAll(bool) { return true; }

    void SetPos(int x, int y) { winX = x; winY = y; }
    void SetSize(int w, int h) { winW = w; winH = h; }
    void SetActive() {}
    long GetLeft() const { return winX; }
    long GetTop() const { return winY; }
    long GetWidth() const { return winW; }
    long GetHeight() const { return winH; }
    VarT GetWindowRect() const { return rectVar(winX, winY, winW, winH); }
    VarT GetClientRect() const { return rectVar(0, 0, winW, winH); }

    static VarT rectVar(long l, long t, long w, long h)
    {
        VarT var;
        DictT dict;
        if (dict.IsValid())
        {
            dict.SetValue(TJS_N("left"), l);
            dict.SetValue(TJS_N("top"), t);
            dict.SetValue(TJS_N("right"), l + w);
            dict.SetValue(TJS_N("bottom"), t + h);
            var = dict;
        }
        return var;
    }

    static void InitCommonControls() {}
    static bool InitCommonControlsEx(tjs_uint32) { return true; }

    static tjs_int64 GetOctetAddress(VarT oct)
    {
        return (oct.Type() == tvtOctet) ? (tjs_int64)(tjs_intptr_t)oct.AsOctetNoAddRef()->GetData() : 0;
    }
    static tjs_int64 GetStringAddress(VarT str)
    {
        return (str.Type() == tvtString) ? (tjs_int64)(tjs_intptr_t)(const tjs_char*)str.AsStringNoAddRef() : 0;
    }

    static tjs_error messageBox(VarT* result, tjs_int num, VarT** param, DspT* obj);
    static tjs_error chooseColor(VarT* result, tjs_int num, VarT** param, DspT* obj);

    bool isPropertySheet() const { return false; }
    tjs_int64 OpenPropertySheet(VarT, VarT) { return 0; }
    tjs_int64 propSheetMessage(VarT, VarT) { return 0; }
    tjs_int64 setMessageResult(VarT, VarT) { return 0; }

    tjs_int64 InsertTabItem(int, VarT pos, VarT titleVar)
    {
        std::string text = (titleVar.Type() == tvtString) ? titleVar.GetString() : "";
        int at = pos.Type() == tvtVoid ? 0 : (int)pos.AsInteger();
        if (at < 0 || at > (int)tabs.size()) at = (int)tabs.size();
        tabs.insert(tabs.begin() + at, text);
        return 0;
    }
    tjs_int64 DeleteTabItem(int, VarT pos) { int at=(int)pos.AsInteger(); if(at>=0&&at<(int)tabs.size()) tabs.erase(tabs.begin()+at); return 0; }
    tjs_int64 DeleteAllTabItem(int) { tabs.clear(); curTab=0; return 0; }
    tjs_int GetCurSelTab(int) { return curTab; }
    tjs_int SetCurSelTab(int, VarT pos) { int i=pos.Type()==tvtVoid?0:(int)pos.AsInteger(); curTab=i; return 0; }
    tjs_int64 SelectTab(int, VarT) { return 0; }

    static tjs_error openProgress(VarT* result, tjs_int num, VarT** param, DspT* objthis)
    {
        WIN32Dialog* self = SelfAdaptorT::GetNativeInstance(objthis);
        if (!self)
            return TJS_E_NATIVECLASSCRASH;
        if (num < 1)
            return TJS_E_BADPARAMCOUNT;
        self->objthis = objthis;
        self->progressMode = true;
        self->progressValue = 0;
        self->progressCanceled = false;
        self->callback(TJS_N("onInit"), (tjs_int)w32::WM_INITDIALOG, 0, 0);
        if (result)
            *result = true;
        return TJS_S_OK;
    }
    bool isProgress() const { return progressMode; }
    ProgressValueT getProgressValue() const { return progressValue; }
    void setProgressValue(ProgressValueT v) { progressValue = v; }
    bool getProgressCanceled() const { return progressCanceled; }
    void setProgressCanceled(bool b) { progressCanceled = b; }
    void closeProgress() { progressMode = false; }

    VarT getScrollInfo(int id)
    {
        VarT var;
        DictT dict;
        if (dict.IsValid())
        {
            PItem* item = findItem(id);
            int pos = item ? item->check : 0;
            dict.SetValue(TJS_N("pos"), pos);
            dict.SetValue(TJS_N("min"), 0);
            dict.SetValue(TJS_N("max"), 100);
            dict.SetValue(TJS_N("page"), 0);
            dict.SetValue(TJS_N("trackpos"), pos);
            var = dict;
        }
        return var;
    }
    static tjs_error setScrollInfo(VarT* result, tjs_int num, VarT** param, DspT* objthis)
    {
        if (num < 1)
            return TJS_E_BADPARAMCOUNT;
        WIN32Dialog* self = SelfAdaptorT::GetNativeInstance(objthis);
        if (!self)
            return TJS_E_NATIVECLASSCRASH;
        if (num > 1)
        {
            PItem* item = self->findItem((int)param[0]->AsInteger());
            if (item)
                item->check = (int)param[1]->AsInteger();
        }
        if (result)
            *result = 0;
        return TJS_S_OK;
    }

    void BringToFront() {}

    tjs_int sendMessage(int id, tjs_uint32 msg, tjs_int wp, tjs_int64 lp, const std::string& lps, bool isstr)
    {
        PItem* item = findItem(id);
        switch (msg)
        {
        case w32::BM_GETCHECK:
        case w32::BM_GETSTATE:
            return item ? item->check : 0;
        case w32::BM_SETCHECK:
        case w32::BM_SETSTATE:
            if (item) item->check = wp;
            return 0;
        case w32::BM_CLICK:
        case w32::BM_SETSTYLE:
            return 0;
        case w32::LB_ADDSTRING:
        case w32::CB_ADDSTRING:
            if (item) { item->strings.push_back(isstr ? lps : std::to_string(lp)); return (tjs_int)(item->strings.size() - 1); }
            return 0;
        case w32::LB_INSERTSTRING:
        case w32::CB_INSERTSTRING:
            if (item)
            {
                std::string s = isstr ? lps : std::to_string(lp);
                int at = wp;
                if (at < 0 || at > (int)item->strings.size()) at = (int)item->strings.size();
                item->strings.insert(item->strings.begin() + at, s);
                return at;
            }
            return 0;
        case w32::LB_GETCOUNT:
        case w32::CB_GETCOUNT:
            return item ? (tjs_int)item->strings.size() : 0;
        case w32::LB_GETCURSEL:
        case w32::CB_GETCURSEL:
            return item ? item->sel : -1;
        case w32::LB_SETCURSEL:
        case w32::CB_SETCURSEL:
            if (item) item->sel = wp;
            return 0;
        case w32::LB_GETSEL:
            return item && wp == item->sel ? 1 : 0;
        case w32::LB_RESETCONTENT:
        case w32::CB_RESETCONTENT:
            if (item) { item->strings.clear(); item->sel = -1; }
            return 0;
        case w32::SBM_SETPOS:
            if (item) item->check = wp;
            return 0;
        case w32::SBM_GETPOS:
            return item ? item->check : 0;
        default:
            return 0;
        }
    }
};

// -------------------------------------------------------------

tjs_error WIN32Dialog::makeTemplate(VarT* result, tjs_int numparams, VarT** param, WIN32Dialog* self)
{
    (void)result;
    if (numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    Header* head = HeadAdaptorT::GetNativeInstance(param[0]->AsObjectNoAddRef(), true);
    if (!head)
        return TJS_E_INVALIDPARAM;

    self->title = head->title;
    self->dlgCx = head->cx;
    self->dlgCy = head->cy;
    self->items.clear();
    self->itemMap.clear();

    for (tjs_int i = 1; i < numparams; ++i)
    {
        Items* item = ItemAdaptorT::GetNativeInstance(param[i]->AsObjectNoAddRef(), true);
        if (!item)
            continue;
        PItem copy = item->item;
        if (copy.text.empty() && !copy.title.empty())
            copy.text = copy.title;
        self->items.push_back(copy);
    }
    for (size_t i = 0; i < self->items.size(); ++i)
        if (self->items[i].id >= 0)
            self->itemMap[self->items[i].id] = i;
    return TJS_S_OK;
}

tjs_error WIN32Dialog::open(VarT* result, tjs_int numparams, VarT** param, DspT* objthis)
{
    WIN32Dialog* self = SelfAdaptorT::GetNativeInstance(objthis);
    if (!self)
        return TJS_E_NATIVECLASSCRASH;
    self->objthis = objthis;
    int ret = self->_open(numparams > 0 ? *param[0] : VarT());
    if (result)
        *result = (tjs_int)ret;
    return TJS_S_OK;
}

tjs_error WIN32Dialog::sendItemMessage(VarT* result, tjs_int numparams, VarT** param, WIN32Dialog* self)
{
    if (numparams < 2)
        return TJS_E_BADPARAMCOUNT;
    int id = (int)param[0]->AsInteger();
    tjs_uint32 msg = (tjs_uint32)param[1]->AsInteger();
    tjs_int wp = (numparams > 2) ? (tjs_int)param[2]->AsInteger() : 0;
    std::string lps;
    tjs_int64 lpn = 0;
    bool isstr = false;
    if (numparams > 3)
    {
        if (param[3]->Type() == tvtString)
        {
            isstr = true;
            lps = param[3]->GetString();
        }
        else
        {
            lpn = (tjs_int64)*param[3];
        }
    }
    if (result)
        *result = (tjs_int)self->sendMessage(id, msg, wp, lpn, lps, isstr);
    return TJS_S_OK;
}

tjs_error WIN32Dialog::messageBox(VarT* result, tjs_int num, VarT** param, DspT* obj)
{
    (void)obj;
    if (num < 3)
        return TJS_E_BADPARAMCOUNT;
    std::string text = param[1]->GetString();
    std::string caption = param[2]->GetString();
    tjs_uint32 type = (num >= 4 && param[3]->Type() != tvtVoid) ? (tjs_uint32)(tjs_int64)*param[3] : w32::MB_OK;

    std::vector<ttstr> buttons;
    std::vector<int> ids;
    switch (type & w32::MB_TYPEMASK)
    {
    case w32::MB_OKCANCEL:
        buttons = {ttstr("OK"), ttstr("Cancel")}; ids = {w32::IDOK, w32::IDCANCEL}; break;
    case w32::MB_ABORTRETRYIGNORE:
        buttons = {ttstr("Abort"), ttstr("Retry"), ttstr("Ignore")}; ids = {w32::IDABORT, w32::IDRETRY, w32::IDIGNORE}; break;
    case w32::MB_YESNOCANCEL:
        buttons = {ttstr("Yes"), ttstr("No"), ttstr("Cancel")}; ids = {w32::IDYES, w32::IDNO, w32::IDCANCEL}; break;
    case w32::MB_YESNO:
        buttons = {ttstr("Yes"), ttstr("No")}; ids = {w32::IDYES, w32::IDNO}; break;
    case w32::MB_RETRYCANCEL:
        buttons = {ttstr("Retry"), ttstr("Cancel")}; ids = {w32::IDRETRY, w32::IDCANCEL}; break;
    case w32::MB_CANCELTRYCONTINUE:
        buttons = {ttstr("Cancel"), ttstr("Try Again"), ttstr("Continue")}; ids = {w32::IDCANCEL, w32::IDTRYAGAIN, w32::IDCONTINUE}; break;
    case w32::MB_OK:
    default:
        buttons = {ttstr("OK")}; ids = {w32::IDOK}; break;
    }
    int idx = ShowChoice(ToTT(caption), ToTT(text), buttons);
    if (result)
        *result = (tjs_int)ids[(size_t)idx];
    return TJS_S_OK;
}

tjs_error WIN32Dialog::chooseColor(VarT* result, tjs_int num, VarT** param, DspT* obj)
{
    (void)obj;
    int color = 0;
    bool hasPalette = false;
    VarT paletteVar;
    if (num >= 2)
    {
        ncbPropAccessor elm(*param[1]);
        color = PropInt(elm, TJS_N("color"));
        tjs_uint32 hint = 0;
        tTJSVariantType type = tvtVoid;
        if (elm.HasValue(TJS_N("palette"), &hint, &type) && type == tvtObject)
        {
            hasPalette = true;
            paletteVar = elm.GetValue(TJS_N("palette"), ncbTypedefs::Tag<VarT>());
        }
    }

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%06X", color & 0xFFFFFF);
    std::string value = buf;
    if (!ShowInput(ttstr("选择颜色"), ttstr("输入颜色值（RRGGBB）"), value))
        return TJS_S_OK;

    char* end = nullptr;
    unsigned long rgb = std::strtoul(value.c_str(), &end, 16);
    if (end == value.c_str() || (*end != 0 && *end != ' ') || rgb > 0xFFFFFF)
        return TJS_S_OK;

    if (result)
        *result = (tjs_int64)(rgb & 0xFFFFFF);

    if (hasPalette)
    {
        PropT array(paletteVar);
        if (array.GetCount() > 0)
            array.SetValue(0, (tjs_int64)(rgb & 0xFFFFFF));
    }
    return TJS_S_OK;
}

// -------------------------------------------------------------

#define X(n, v) Variant(#n, (tTVInteger)(v), 0);
NCB_REGISTER_SUBCLASS(Header)
{
    Constructor();
    Method(TJS_N("store"), &Class::store);
    Property(TJS_N("dlgItems"), &Class::get_dlgItems, &Class::set_dlgItems);
}
NCB_REGISTER_SUBCLASS(Items)
{
    Constructor();
    Method(TJS_N("store"), &Class::store);
}
NCB_REGISTER_SUBCLASS(Bitmap)
{
    Constructor<iTJSDispatch2*>(0);
}
NCB_REGISTER_SUBCLASS(SolidBrush)
{
    Constructor<tjs_uint32>(0);
}
NCB_REGISTER_SUBCLASS(DrawItem)
{
    Constructor();
    Property(TJS_N("ctrlType"),   &Class::GetCtlType,    (int)0);
    Property(TJS_N("ctrlID"),     &Class::GetCtlID,      (int)0);
    Property(TJS_N("itemID"),     &Class::GetItemID,     (int)0);
    Property(TJS_N("itemAction"), &Class::GetItemAction, (int)0);
    Property(TJS_N("itemState"),  &Class::GetItemState,  (int)0);
    Property(TJS_N("itemData"),   &Class::GetItemData,   (int)0);
    Property(TJS_N("itemRect"),   &Class::GetItemRect,   (int)0);
    Property(TJS_N("hwndItem"),   &Class::GetHWndItem,   (int)0);
    Method(TJS_N("draw"),         &Class::draw);
}
NCB_REGISTER_SUBCLASS(NotifyAccessor)
{
    Constructor();
    Property(TJS_N("hwndFrom"), &Class::GetHWndFrom, (int)0);
    Property(TJS_N("idFrom"),   &Class::GetIdFrom,   (int)0);
    Property(TJS_N("code"),     &Class::GetCode,     (int)0);
    Method(TJS_N("getByte"),  &Class::GetByte);
    Method(TJS_N("getWord"),  &Class::GetWord);
    Method(TJS_N("getDWord"), &Class::GetDWord);
}
NCB_REGISTER_SUBCLASS(Blob)
{
    Constructor<tjs_uint32>(0);
    Property(TJS_N("pointer"), &Class::GetPointer, (int)0);
    Method(TJS_N("getByte"),  &Class::GetByte);
    Method(TJS_N("getWord"),  &Class::GetWord);
    Method(TJS_N("getDWord"), &Class::GetDWord);
    Method(TJS_N("getDWordLong"), &Class::GetDWordLong);
    Method(TJS_N("getText"),  &Class::GetText);
    Method(TJS_N("setByte"),  &Class::SetByte);
    Method(TJS_N("setWord"),  &Class::SetWord);
    Method(TJS_N("setDWord"), &Class::SetDWord);
    Method(TJS_N("setDWordLong"), &Class::SetDWordLong);
    Method(TJS_N("setText"),  &Class::SetText);
    Method(TJS_N("ReferPointer"), &Class::ReferPointer);
}
NCB_REGISTER_CLASS(WIN32Dialog)
{
    NCB_SUBCLASS(Header, Header);
    NCB_SUBCLASS(Items,  Items);
    NCB_SUBCLASS(Bitmap, Bitmap);
    NCB_SUBCLASS(SolidBrush, SolidBrush);
    NCB_SUBCLASS(DrawItem, DrawItem);
    NCB_SUBCLASS(Notify, NotifyAccessor);
    NCB_SUBCLASS(Blob, Blob);

    Constructor<iTJSDispatch2*>(0);

    RawCallback(TJS_N("loadResource"), &Class::loadResource_noop, 0);
    RawCallback(TJS_N("makeTemplate"), &Class::makeTemplate, 0);
    RawCallback(TJS_N("open"), &Class::open, 0);
    Method(TJS_N("close"), &Class::close);

    Method(TJS_N("getItem"),          &Class::GetItem);
    Method(TJS_N("getItemID"),        &Class::GetItemID);
    Method(TJS_N("getItemClassName"), &Class::GetItemClassName);
    Method(TJS_N("setItemLong"),      &Class::SetItemLong);
    Method(TJS_N("getItemLong"),      &Class::GetItemLong);
    Method(TJS_N("setItemInt"),       &Class::SetItemInt);
    Method(TJS_N("getItemInt"),       &Class::GetItemInt);
    Method(TJS_N("setItemText"),      &Class::SetItemText);
    Method(TJS_N("getItemText"),      &Class::GetItemText);
    Method(TJS_N("setItemEnabled"),   &Class::SetItemEnabled);
    Method(TJS_N("getItemEnabled"),   &Class::GetItemEnabled);
    Method(TJS_N("setItemFocus"),     &Class::SetItemFocus);
    Method(TJS_N("getItemLeft"),      &Class::GetItemLeft);
    Method(TJS_N("getItemTop"),       &Class::GetItemTop);
    Method(TJS_N("getItemWidth"),     &Class::GetItemWidth);
    Method(TJS_N("getItemHeight"),    &Class::GetItemHeight);
    Method(TJS_N("setItemPos"),       &Class::SetItemPos);
    Method(TJS_N("setItemSize"),      &Class::SetItemSize);
    RawCallback(TJS_N("setItemBitmap"), &Class::SetItemBitmap, 0);
    Method(TJS_N("isExistentItem"),   &Class::IsExistentItem);
    Method(TJS_N("lockItemUpdate"),   &Class::LockItemUpdate);
    Method(TJS_N("unlockItemUpdate"), &Class::UnlockItemUpdate);

    RawCallback(TJS_N("sendItemMessage"), &Class::sendItemMessage, 0);

    Method(TJS_N("getBaseUnits"),   &Class::GetBaseUnits);
    Method(TJS_N("mapRect"),        &Class::MapRect);
    Method(TJS_N("invalidateRect"), &Class::InvalidateRect);
    Method(TJS_N("invalidateAll"),  &Class::InvalidateAll);

    Method(TJS_N("setPos"),         &Class::SetPos);
    Method(TJS_N("setSize"),        &Class::SetSize);
    Method(TJS_N("setActive"),      &Class::SetActive);
    Property(TJS_N("left"),         &Class::GetLeft,   (int)0);
    Property(TJS_N("top"),          &Class::GetTop,    (int)0);
    Property(TJS_N("width"),        &Class::GetWidth,  (int)0);
    Property(TJS_N("height"),       &Class::GetHeight, (int)0);
    Method(TJS_N("getWindowRect"),  &Class::GetWindowRect);
    Method(TJS_N("getClientRect"),  &Class::GetClientRect);

    Method(TJS_N("onInit"),    &Class::onInit);
    Method(TJS_N("onCommand"), &Class::onCommand);
    Method(TJS_N("onNotify"),  &Class::onNotify);
    Method(TJS_N("onHScroll"), &Class::onHScroll);
    Method(TJS_N("onVScroll"), &Class::onVScroll);
    Method(TJS_N("onSize"),    &Class::onSize);

    Method(TJS_N("show"),           &Class::show);
    Property(TJS_N("modeless"),     &Class::getModeless, &Class::setModeless);
    Property(TJS_N("HWND"),         &Class::getHWND, (int)0);
    Property(TJS_N("icon"),         &Class::getDialogIcon, &Class::setDialogIcon);
    Property(TJS_N("isValid"),      &Class::IsValid, (int)0);

    // 原插件以 Variant 形式注册的控件类 / 类名常量
    X(BUTTON,     0x0080LL)
    X(EDIT,       0x0081LL)
    X(STATIC,     0x0082LL)
    X(LISTBOX,    0x0083LL)
    X(SCROLLBAR,  0x0084LL)
    X(COMBOBOX,   0x0085LL)
    Variant(TJS_N("HEADER"),        "SysHeader32", 0);
    Variant(TJS_N("LINK"),          "SysLink", 0);
    Variant(TJS_N("LISTVIEW"),      "SysListView32", 0);
    Variant(TJS_N("TREEVIEW"),      "SysTreeView32", 0);
    Variant(TJS_N("TABCONTROL"),    "SysTabControl32", 0);
    Variant(TJS_N("IPADDRESS"),     "SysIPAddress32", 0);
    Variant(TJS_N("PAGESCROLLER"),  "SysPager", 0);
    Variant(TJS_N("ANIMATE"),       "SysAnimate32", 0);
    Variant(TJS_N("MONTHCAL"),      "SysMonthCal32", 0);
    Variant(TJS_N("DATETIMEPICK"),  "SysDateTimePick32", 0);
    Variant(TJS_N("COMBOBOXEX"),    "ComboBoxEx32", 0);
    Variant(TJS_N("NATIVEFONTCTL"), "NativeFontCtl", 0);
    Variant(TJS_N("TOOLBAR"),       "ToolbarWindow32", 0);
    Variant(TJS_N("REBAR"),         "ReBarWindow32", 0);
    Variant(TJS_N("TOOLTIPS"),      "tooltips_class32", 0);
    Variant(TJS_N("STATUS"),        "msctls_statusbar32", 0);
    Variant(TJS_N("TRACKBAR"),      "msctls_trackbar32", 0);
    Variant(TJS_N("UPDOWN"),        "msctls_updown32", 0);
    Variant(TJS_N("PROGRESS"),      "msctls_progress32", 0);
    Variant(TJS_N("HOTKEY"),        "msctls_hotkey32", 0);
    X(TCM_GETITEM,    0x133CLL)
    X(TCM_SETITEM,    0x133DLL)
    X(TCM_INSERTITEM, 0x133FLL)
    X(BLACK_BRUSH, 4LL)
    X(BM_CLICK, 245LL)
    X(BM_GETCHECK, 240LL)
    X(BM_GETIMAGE, 246LL)
    X(BM_GETSTATE, 242LL)
    X(BM_SETCHECK, 241LL)
    X(BM_SETDONTCLICK, 248LL)
    X(BM_SETIMAGE, 247LL)
    X(BM_SETSTATE, 243LL)
    X(BM_SETSTYLE, 244LL)
    X(BN_CLICKED, 0LL)
    X(BN_DBLCLK, 5LL)
    X(BN_DISABLE, 4LL)
    X(BN_DOUBLECLICKED, 5LL)
    X(BN_HILITE, 2LL)
    X(BN_KILLFOCUS, 7LL)
    X(BN_PAINT, 1LL)
    X(BN_PUSHED, 2LL)
    X(BN_SETFOCUS, 6LL)
    X(BN_UNHILITE, 3LL)
    X(BN_UNPUSHED, 3LL)
    X(BST_CHECKED, 1LL)
    X(BST_FOCUS, 8LL)
    X(BST_INDETERMINATE, 2LL)
    X(BST_PUSHED, 4LL)
    X(BST_UNCHECKED, 0LL)
    X(BS_3STATE, 5LL)
    X(BS_AUTO3STATE, 6LL)
    X(BS_AUTOCHECKBOX, 3LL)
    X(BS_AUTORADIOBUTTON, 9LL)
    X(BS_BITMAP, 128LL)
    X(BS_BOTTOM, 2048LL)
    X(BS_CENTER, 768LL)
    X(BS_CHECKBOX, 2LL)
    X(BS_DEFPUSHBUTTON, 1LL)
    X(BS_FLAT, 32768LL)
    X(BS_GROUPBOX, 7LL)
    X(BS_ICON, 64LL)
    X(BS_LEFT, 256LL)
    X(BS_LEFTTEXT, 32LL)
    X(BS_MULTILINE, 8192LL)
    X(BS_NOTIFY, 16384LL)
    X(BS_OWNERDRAW, 11LL)
    X(BS_PUSHBOX, 10LL)
    X(BS_PUSHBUTTON, 0LL)
    X(BS_PUSHLIKE, 4096LL)
    X(BS_RADIOBUTTON, 4LL)
    X(BS_RIGHT, 512LL)
    X(BS_RIGHTBUTTON, 32LL)
    X(BS_TEXT, 0LL)
    X(BS_TOP, 1024LL)
    X(BS_TYPEMASK, 15LL)
    X(BS_USERBUTTON, 8LL)
    X(BS_VCENTER, 3072LL)
    X(CBN_CLOSEUP, 8LL)
    X(CBN_DBLCLK, 2LL)
    X(CBN_DROPDOWN, 7LL)
    X(CBN_EDITCHANGE, 5LL)
    X(CBN_EDITUPDATE, 6LL)
    X(CBN_ERRSPACE, -1LL)
    X(CBN_KILLFOCUS, 4LL)
    X(CBN_SELCHANGE, 1LL)
    X(CBN_SELENDCANCEL, 10LL)
    X(CBN_SELENDOK, 9LL)
    X(CBN_SETFOCUS, 3LL)
    X(CBS_AUTOHSCROLL, 64LL)
    X(CBS_DISABLENOSCROLL, 2048LL)
    X(CBS_DROPDOWN, 2LL)
    X(CBS_DROPDOWNLIST, 3LL)
    X(CBS_HASSTRINGS, 512LL)
    X(CBS_LOWERCASE, 16384LL)
    X(CBS_NOINTEGRALHEIGHT, 1024LL)
    X(CBS_OEMCONVERT, 128LL)
    X(CBS_OWNERDRAWFIXED, 16LL)
    X(CBS_OWNERDRAWVARIABLE, 32LL)
    X(CBS_SIMPLE, 1LL)
    X(CBS_SORT, 256LL)
    X(CBS_UPPERCASE, 8192LL)
    X(CB_ADDSTRING, 323LL)
    X(CB_DELETESTRING, 324LL)
    X(CB_DIR, 325LL)
    X(CB_ERR, -1LL)
    X(CB_ERRSPACE, -2LL)
    X(CB_FINDSTRING, 332LL)
    X(CB_FINDSTRINGEXACT, 344LL)
    X(CB_GETCOMBOBOXINFO, 356LL)
    X(CB_GETCOUNT, 326LL)
    X(CB_GETCURSEL, 327LL)
    X(CB_GETDROPPEDCONTROLRECT, 338LL)
    X(CB_GETDROPPEDSTATE, 343LL)
    X(CB_GETDROPPEDWIDTH, 351LL)
    X(CB_GETEDITSEL, 320LL)
    X(CB_GETEXTENDEDUI, 342LL)
    X(CB_GETHORIZONTALEXTENT, 349LL)
    X(CB_GETITEMDATA, 336LL)
    X(CB_GETITEMHEIGHT, 340LL)
    X(CB_GETLBTEXT, 328LL)
    X(CB_GETLBTEXTLEN, 329LL)
    X(CB_GETLOCALE, 346LL)
    X(CB_GETTOPINDEX, 347LL)
    X(CB_INITSTORAGE, 353LL)
    X(CB_INSERTSTRING, 330LL)
    X(CB_LIMITTEXT, 321LL)
    X(CB_MSGMAX, 357LL)
    X(CB_MULTIPLEADDSTRING, 0x0163LL)
    X(CB_OKAY, 0LL)
    X(CB_RESETCONTENT, 331LL)
    X(CB_SELECTSTRING, 333LL)
    X(CB_SETCURSEL, 334LL)
    X(CB_SETDROPPEDWIDTH, 352LL)
    X(CB_SETEDITSEL, 322LL)
    X(CB_SETEXTENDEDUI, 341LL)
    X(CB_SETHORIZONTALEXTENT, 350LL)
    X(CB_SETITEMDATA, 337LL)
    X(CB_SETITEMHEIGHT, 339LL)
    X(CB_SETLOCALE, 345LL)
    X(CB_SETTOPINDEX, 348LL)
    X(CB_SHOWDROPDOWN, 335LL)
    X(DC_HASDEFID, 21323LL)
    X(DLGC_BUTTON, 8192LL)
    X(DLGC_DEFPUSHBUTTON, 16LL)
    X(DLGC_HASSETSEL, 8LL)
    X(DLGC_RADIOBUTTON, 64LL)
    X(DLGC_STATIC, 256LL)
    X(DLGC_UNDEFPUSHBUTTON, 32LL)
    X(DLGC_WANTALLKEYS, 4LL)
    X(DLGC_WANTARROWS, 1LL)
    X(DLGC_WANTCHARS, 128LL)
    X(DLGC_WANTMESSAGE, 4LL)
    X(DLGC_WANTTAB, 2LL)
    X(DM_GETDEFID, 1024LL)
    X(DM_REPOSITION, 1026LL)
    X(DM_SETDEFID, 1025LL)
    X(DS_3DLOOK, 4LL)
    X(DS_ABSALIGN, 1LL)
    X(DS_CENTER, 2048LL)
    X(DS_CENTERMOUSE, 4096LL)
    X(DS_CONTEXTHELP, 8192LL)
    X(DS_CONTROL, 1024LL)
    X(DS_FIXEDSYS, 8LL)
    X(DS_LOCALEDIT, 32LL)
    X(DS_MODALFRAME, 128LL)
    X(DS_NOFAILCREATE, 16LL)
    X(DS_NOIDLEMSG, 256LL)
    X(DS_SETFONT, 64LL)
    X(DS_SETFOREGROUND, 512LL)
    X(DS_SHELLFONT, 72LL)
    X(DS_SYSMODAL, 2LL)
    X(DS_USEPIXELS, 0x8000LL)
    X(DWLP_DLGPROC, 8LL)
    X(DWLP_MSGRESULT, 0LL)
    X(DWLP_USER, 16LL)
    X(EC_LEFTMARGIN, 1LL)
    X(EC_RIGHTMARGIN, 2LL)
    X(EC_USEFONTINFO, 65535LL)
    X(EIMES_CANCELCOMPSTRINFOCUS, 2LL)
    X(EIMES_COMPLETECOMPSTRKILLFOCUS, 4LL)
    X(EIMES_GETCOMPSTRATONCE, 1LL)
    X(EMSIS_COMPOSITIONSTRING, 1LL)
    X(EM_CANUNDO, 198LL)
    X(EM_CHARFROMPOS, 215LL)
    X(EM_EMPTYUNDOBUFFER, 205LL)
    X(EM_FMTLINES, 200LL)
    X(EM_GETFIRSTVISIBLELINE, 206LL)
    X(EM_GETHANDLE, 189LL)
    X(EM_GETIMESTATUS, 217LL)
    X(EM_GETLIMITTEXT, 213LL)
    X(EM_GETLINE, 196LL)
    X(EM_GETLINECOUNT, 186LL)
    X(EM_GETMARGINS, 212LL)
    X(EM_GETMODIFY, 184LL)
    X(EM_GETPASSWORDCHAR, 210LL)
    X(EM_GETRECT, 178LL)
    X(EM_GETSEL, 176LL)
    X(EM_GETTHUMB, 190LL)
    X(EM_GETWORDBREAKPROC, 209LL)
    X(EM_LIMITTEXT, 197LL)
    X(EM_LINEFROMCHAR, 201LL)
    X(EM_LINEINDEX, 187LL)
    X(EM_LINELENGTH, 193LL)
    X(EM_LINESCROLL, 182LL)
    X(EM_POSFROMCHAR, 214LL)
    X(EM_REPLACESEL, 194LL)
    X(EM_SCROLL, 181LL)
    X(EM_SCROLLCARET, 183LL)
    X(EM_SETHANDLE, 188LL)
    X(EM_SETIMESTATUS, 216LL)
    X(EM_SETLIMITTEXT, 197LL)
    X(EM_SETMARGINS, 211LL)
    X(EM_SETMODIFY, 185LL)
    X(EM_SETPASSWORDCHAR, 204LL)
    X(EM_SETREADONLY, 207LL)
    X(EM_SETRECT, 179LL)
    X(EM_SETRECTNP, 180LL)
    X(EM_SETSEL, 177LL)
    X(EM_SETTABSTOPS, 203LL)
    X(EM_SETWORDBREAKPROC, 208LL)
    X(EM_UNDO, 199LL)
    X(EN_ALIGN_LTR_EC, 1792LL)
    X(EN_ALIGN_RTL_EC, 1793LL)
    X(EN_CHANGE, 768LL)
    X(EN_ERRSPACE, 1280LL)
    X(EN_HSCROLL, 1537LL)
    X(EN_KILLFOCUS, 512LL)
    X(EN_MAXTEXT, 1281LL)
    X(EN_SETFOCUS, 256LL)
    X(EN_UPDATE, 1024LL)
    X(EN_VSCROLL, 1538LL)
    X(ES_AUTOHSCROLL, 128LL)
    X(ES_AUTOVSCROLL, 64LL)
    X(ES_CENTER, 1LL)
    X(ES_LEFT, 0LL)
    X(ES_LOWERCASE, 16LL)
    X(ES_MULTILINE, 4LL)
    X(ES_NOHIDESEL, 256LL)
    X(ES_NUMBER, 8192LL)
    X(ES_OEMCONVERT, 1024LL)
    X(ES_PASSWORD, 32LL)
    X(ES_READONLY, 2048LL)
    X(ES_RIGHT, 2LL)
    X(ES_UPPERCASE, 8LL)
    X(ES_WANTRETURN, 4096LL)
    X(FW_BOLD, 700LL)
    X(FW_DONTCARE, 0LL)
    X(FW_EXTRABOLD, 800LL)
    X(FW_EXTRALIGHT, 200LL)
    X(FW_HEAVY, 900LL)
    X(FW_LIGHT, 300LL)
    X(FW_MEDIUM, 500LL)
    X(FW_NORMAL, 400LL)
    X(FW_SEMIBOLD, 600LL)
    X(FW_THIN, 100LL)
    X(GWLP_HINSTANCE, -6LL)
    X(GWLP_HWNDPARENT, -8LL)
    X(GWLP_USERDATA, -21LL)
    X(GWLP_WNDPROC, -4LL)
    X(GWL_EXSTYLE, -20LL)
    X(GWL_ID, -12LL)
    X(GWL_STYLE, -16LL)
    X(HOLLOW_BRUSH, 5LL)
    X(ICC_ANIMATE_CLASS, 128LL)
    X(ICC_BAR_CLASSES, 4LL)
    X(ICC_COOL_CLASSES, 1024LL)
    X(ICC_DATE_CLASSES, 256LL)
    X(ICC_HOTKEY_CLASS, 64LL)
    X(ICC_INTERNET_CLASSES, 2048LL)
    X(ICC_LINK_CLASS, 32768LL)
    X(ICC_LISTVIEW_CLASSES, 1LL)
    X(ICC_NATIVEFNTCTL_CLASS, 8192LL)
    X(ICC_PAGESCROLLER_CLASS, 4096LL)
    X(ICC_PROGRESS_CLASS, 32LL)
    X(ICC_STANDARD_CLASSES, 16384LL)
    X(ICC_TAB_CLASSES, 8LL)
    X(ICC_TREEVIEW_CLASSES, 2LL)
    X(ICC_UPDOWN_CLASS, 16LL)
    X(ICC_USEREX_CLASSES, 512LL)
    X(ICC_WIN95_CLASSES, 255LL)
    X(IDABORT, 3LL)
    X(IDCANCEL, 2LL)
    X(IDCLOSE, 8LL)
    X(IDCONTINUE, 11LL)
    X(IDHELP, 9LL)
    X(IDIGNORE, 5LL)
    X(IDNO, 7LL)
    X(IDOK, 1LL)
    X(IDRETRY, 4LL)
    X(IDTIMEOUT, 32000LL)
    X(IDTRYAGAIN, 10LL)
    X(IDYES, 6LL)
    X(ID_PSREBOOTSYSTEM, 3LL)
    X(ID_PSRESTARTWINDOWS, 2LL)
    X(LBN_DBLCLK, 2LL)
    X(LBN_ERRSPACE, -2LL)
    X(LBN_KILLFOCUS, 5LL)
    X(LBN_SELCANCEL, 3LL)
    X(LBN_SELCHANGE, 1LL)
    X(LBN_SETFOCUS, 4LL)
    X(LBS_COMBOBOX, 32768LL)
    X(LBS_DISABLENOSCROLL, 4096LL)
    X(LBS_EXTENDEDSEL, 2048LL)
    X(LBS_HASSTRINGS, 64LL)
    X(LBS_MULTICOLUMN, 512LL)
    X(LBS_MULTIPLESEL, 8LL)
    X(LBS_NODATA, 8192LL)
    X(LBS_NOINTEGRALHEIGHT, 256LL)
    X(LBS_NOREDRAW, 4LL)
    X(LBS_NOSEL, 16384LL)
    X(LBS_NOTIFY, 1LL)
    X(LBS_OWNERDRAWFIXED, 16LL)
    X(LBS_OWNERDRAWVARIABLE, 32LL)
    X(LBS_SORT, 2LL)
    X(LBS_STANDARD, 10485763LL)
    X(LBS_USETABSTOPS, 128LL)
    X(LBS_WANTKEYBOARDINPUT, 1024LL)
    X(LB_ADDFILE, 406LL)
    X(LB_ADDSTRING, 384LL)
    X(LB_CTLCODE, 0LL)
    X(LB_DELETESTRING, 386LL)
    X(LB_DIR, 397LL)
    X(LB_ERR, -1LL)
    X(LB_ERRSPACE, -2LL)
    X(LB_FINDSTRING, 399LL)
    X(LB_FINDSTRINGEXACT, 418LL)
    X(LB_GETANCHORINDEX, 413LL)
    X(LB_GETCARETINDEX, 415LL)
    X(LB_GETCOUNT, 395LL)
    X(LB_GETCURSEL, 392LL)
    X(LB_GETHORIZONTALEXTENT, 403LL)
    X(LB_GETITEMDATA, 409LL)
    X(LB_GETITEMHEIGHT, 417LL)
    X(LB_GETITEMRECT, 408LL)
    X(LB_GETLISTBOXINFO, 434LL)
    X(LB_GETLOCALE, 422LL)
    X(LB_GETSEL, 391LL)
    X(LB_GETSELCOUNT, 400LL)
    X(LB_GETSELITEMS, 401LL)
    X(LB_GETTEXT, 393LL)
    X(LB_GETTEXTLEN, 394LL)
    X(LB_GETTOPINDEX, 398LL)
    X(LB_INITSTORAGE, 424LL)
    X(LB_INSERTSTRING, 385LL)
    X(LB_ITEMFROMPOINT, 425LL)
    X(LB_MSGMAX, 435LL)
    X(LB_MULTIPLEADDSTRING, 0x01B1LL)
    X(LB_OKAY, 0LL)
    X(LB_RESETCONTENT, 388LL)
    X(LB_SELECTSTRING, 396LL)
    X(LB_SELITEMRANGE, 411LL)
    X(LB_SELITEMRANGEEX, 387LL)
    X(LB_SETANCHORINDEX, 412LL)
    X(LB_SETCARETINDEX, 414LL)
    X(LB_SETCOLUMNWIDTH, 405LL)
    X(LB_SETCOUNT, 423LL)
    X(LB_SETCURSEL, 390LL)
    X(LB_SETHORIZONTALEXTENT, 404LL)
    X(LB_SETITEMDATA, 410LL)
    X(LB_SETITEMHEIGHT, 416LL)
    X(LB_SETLOCALE, 421LL)
    X(LB_SETSEL, 389LL)
    X(LB_SETTABSTOPS, 402LL)
    X(LB_SETTOPINDEX, 407LL)
    X(LVM_APPROXIMATEVIEWRECT, 4160LL)
    X(LVM_ARRANGE, 4118LL)
    X(LVM_CANCELEDITLABEL, 4275LL)
    X(LVM_CREATEDRAGIMAGE, 4129LL)
    X(LVM_DELETEALLITEMS, 4105LL)
    X(LVM_DELETECOLUMN, 4124LL)
    X(LVM_DELETEITEM, 4104LL)
    X(LVM_EDITLABEL, 4119LL)
    X(LVM_EDITLABELA, 4119LL)
    X(LVM_EDITLABELW, 4214LL)
    X(LVM_ENABLEGROUPVIEW, 4253LL)
    X(LVM_ENSUREVISIBLE, 4115LL)
    X(LVM_FINDITEMA, 4109LL)
    X(LVM_FINDITEMW, 4179LL)
    X(LVM_FIRST, 4096LL)
    X(LVM_GETBKCOLOR, 4096LL)
    X(LVM_GETBKIMAGE, 4165LL)
    X(LVM_GETBKIMAGEA, 4165LL)
    X(LVM_GETBKIMAGEW, 4235LL)
    X(LVM_GETCALLBACKMASK, 4106LL)
    X(LVM_GETCOLUMNA, 4121LL)
    X(LVM_GETCOLUMNORDERARRAY, 4155LL)
    X(LVM_GETCOLUMNW, 4191LL)
    X(LVM_GETCOLUMNWIDTH, 4125LL)
    X(LVM_GETCOUNTPERPAGE, 4136LL)
    X(LVM_GETEDITCONTROL, 4120LL)
    X(LVM_GETEMPTYTEXT, 4300LL)
    X(LVM_GETEXTENDEDLISTVIEWSTYLE, 4151LL)
    X(LVM_GETFOCUSEDGROUP, 4189LL)
    X(LVM_GETFOOTERINFO, 4302LL)
    X(LVM_GETFOOTERITEM, 4304LL)
    X(LVM_GETFOOTERITEMRECT, 4303LL)
    X(LVM_GETFOOTERRECT, 4301LL)
    X(LVM_GETGROUPCOUNT, 4248LL)
    X(LVM_GETGROUPINFO, 4245LL)
    X(LVM_GETGROUPINFOBYINDEX, 4249LL)
    X(LVM_GETGROUPMETRICS, 4252LL)
    X(LVM_GETGROUPRECT, 4194LL)
    X(LVM_GETGROUPSTATE, 4188LL)
    X(LVM_GETHEADER, 4127LL)
    X(LVM_GETHOTCURSOR, 4159LL)
    X(LVM_GETHOTITEM, 4157LL)
    X(LVM_GETHOVERTIME, 4168LL)
    X(LVM_GETIMAGELIST, 4098LL)
    X(LVM_GETINSERTMARK, 4263LL)
    X(LVM_GETINSERTMARKCOLOR, 4267LL)
    X(LVM_GETINSERTMARKRECT, 4265LL)
    X(LVM_GETISEARCHSTRING, 4148LL)
    X(LVM_GETISEARCHSTRINGA, 4148LL)
    X(LVM_GETISEARCHSTRINGW, 4213LL)
    X(LVM_GETITEM, 4101LL)
    X(LVM_GETITEMA, 4101LL)
    X(LVM_GETITEMCOUNT, 4100LL)
    X(LVM_GETITEMINDEXRECT, 4305LL)
    X(LVM_GETITEMPOSITION, 4112LL)
    X(LVM_GETITEMRECT, 4110LL)
    X(LVM_GETITEMSPACING, 4147LL)
    X(LVM_GETITEMSTATE, 4140LL)
    X(LVM_GETITEMTEXTA, 4141LL)
    X(LVM_GETITEMTEXTW, 4211LL)
    X(LVM_GETITEMW, 4171LL)
    X(LVM_GETNEXTITEM, 4108LL)
    X(LVM_GETNEXTITEMINDEX, 4307LL)
    X(LVM_GETNUMBEROFWORKAREAS, 4169LL)
    X(LVM_GETORIGIN, 4137LL)
    X(LVM_GETOUTLINECOLOR, 4272LL)
    X(LVM_GETSELECTEDCOLUMN, 4270LL)
    X(LVM_GETSELECTEDCOUNT, 4146LL)
    X(LVM_GETSELECTIONMARK, 4162LL)
    X(LVM_GETSTRINGWIDTHA, 4113LL)
    X(LVM_GETSTRINGWIDTHW, 4183LL)
    X(LVM_GETSUBITEMRECT, 4152LL)
    X(LVM_GETTEXTBKCOLOR, 4133LL)
    X(LVM_GETTEXTCOLOR, 4131LL)
    X(LVM_GETTILEINFO, 4261LL)
    X(LVM_GETTILEVIEWINFO, 4259LL)
    X(LVM_GETTOOLTIPS, 4174LL)
    X(LVM_GETTOPINDEX, 4135LL)
    X(LVM_GETUNICODEFORMAT, 8198LL)
    X(LVM_GETVIEW, 4239LL)
    X(LVM_GETVIEWRECT, 4130LL)
    X(LVM_GETWORKAREAS, 4166LL)
    X(LVM_HASGROUP, 4257LL)
    X(LVM_HITTEST, 4114LL)
    X(LVM_INSERTCOLUMNA, 4123LL)
    X(LVM_INSERTCOLUMNW, 4193LL)
    X(LVM_INSERTGROUP, 4241LL)
    X(LVM_INSERTGROUPSORTED, 4255LL)
    X(LVM_INSERTITEM, 4103LL)
    X(LVM_INSERTITEMA, 4103LL)
    X(LVM_INSERTITEMW, 4173LL)
    X(LVM_INSERTMARKHITTEST, 4264LL)
    X(LVM_ISGROUPVIEWENABLED, 4271LL)
    X(LVM_ISITEMVISIBLE, 4278LL)
    X(LVM_MAPIDTOINDEX, 4277LL)
    X(LVM_MAPINDEXTOID, 4276LL)
    X(LVM_MOVEGROUP, 4247LL)
    X(LVM_MOVEITEMTOGROUP, 4250LL)
    X(LVM_REDRAWITEMS, 4117LL)
    X(LVM_REMOVEALLGROUPS, 4256LL)
    X(LVM_REMOVEGROUP, 4246LL)
    X(LVM_SCROLL, 4116LL)
    X(LVM_SETBKCOLOR, 4097LL)
    X(LVM_SETBKIMAGE, 4164LL)
    X(LVM_SETBKIMAGEA, 4164LL)
    X(LVM_SETBKIMAGEW, 4234LL)
    X(LVM_SETCALLBACKMASK, 4107LL)
    X(LVM_SETCOLUMNA, 4122LL)
    X(LVM_SETCOLUMNORDERARRAY, 4154LL)
    X(LVM_SETCOLUMNW, 4192LL)
    X(LVM_SETCOLUMNWIDTH, 4126LL)
    X(LVM_SETEXTENDEDLISTVIEWSTYLE, 4150LL)
    X(LVM_SETGROUPINFO, 4243LL)
    X(LVM_SETGROUPMETRICS, 4251LL)
    X(LVM_SETHOTCURSOR, 4158LL)
    X(LVM_SETHOTITEM, 4156LL)
    X(LVM_SETHOVERTIME, 4167LL)
    X(LVM_SETICONSPACING, 4149LL)
    X(LVM_SETIMAGELIST, 4099LL)
    X(LVM_SETINSERTMARK, 4262LL)
    X(LVM_SETINSERTMARKCOLOR, 4266LL)
    X(LVM_SETITEM, 4102LL)
    X(LVM_SETITEMA, 4102LL)
    X(LVM_SETITEMCOUNT, 4143LL)
    X(LVM_SETITEMINDEXSTATE, 4306LL)
    X(LVM_SETITEMPOSITION, 4111LL)
    X(LVM_SETITEMPOSITION32, 4145LL)
    X(LVM_SETITEMSTATE, 4139LL)
    X(LVM_SETITEMTEXTA, 4142LL)
    X(LVM_SETITEMTEXTW, 4212LL)
    X(LVM_SETITEMW, 4172LL)
    X(LVM_SETOUTLINECOLOR, 4273LL)
    X(LVM_SETSELECTEDCOLUMN, 4236LL)
    X(LVM_SETSELECTIONMARK, 4163LL)
    X(LVM_SETTEXTBKCOLOR, 4134LL)
    X(LVM_SETTEXTCOLOR, 4132LL)
    X(LVM_SETTILEINFO, 4260LL)
    X(LVM_SETTILEVIEWINFO, 4258LL)
    X(LVM_SETTOOLTIPS, 4170LL)
    X(LVM_SETUNICODEFORMAT, 8197LL)
    X(LVM_SETVIEW, 4238LL)
    X(LVM_SETWORKAREAS, 4161LL)
    X(LVM_SORTGROUPS, 4254LL)
    X(LVM_SORTITEMS, 4144LL)
    X(LVM_SORTITEMSEX, 4177LL)
    X(LVM_SUBITEMHITTEST, 4153LL)
    X(LVM_UPDATE, 4138LL)
    X(LVNI_ABOVE, 256LL)
    X(LVNI_ALL, 0LL)
    X(LVNI_BELOW, 512LL)
    X(LVNI_CUT, 4LL)
    X(LVNI_DIRECTIONMASK, 3840LL)
    X(LVNI_DROPHILITED, 8LL)
    X(LVNI_FOCUSED, 1LL)
    X(LVNI_PREVIOUS, 32LL)
    X(LVNI_SAMEGROUPONLY, 128LL)
    X(LVNI_SELECTED, 2LL)
    X(LVNI_STATEMASK, 15LL)
    X(LVNI_TOLEFT, 1024LL)
    X(LVNI_TORIGHT, 2048LL)
    X(LVNI_VISIBLEONLY, 64LL)
    X(LVNI_VISIBLEORDER, 16LL)
    X(LVN_BEGINDRAG, 4294967187LL)
    X(LVN_BEGINLABELEDIT, 4294967191LL)
    X(LVN_BEGINLABELEDITA, 4294967191LL)
    X(LVN_BEGINLABELEDITW, 4294967121LL)
    X(LVN_BEGINRDRAG, 4294967185LL)
    X(LVN_BEGINSCROLL, 4294967116LL)
    X(LVN_COLUMNCLICK, 4294967188LL)
    X(LVN_COLUMNDROPDOWN, 4294967132LL)
    X(LVN_COLUMNOVERFLOWCLICK, 4294967130LL)
    X(LVN_DELETEALLITEMS, 4294967192LL)
    X(LVN_DELETEITEM, 4294967193LL)
    X(LVN_ENDLABELEDIT, 4294967190LL)
    X(LVN_ENDLABELEDITA, 4294967190LL)
    X(LVN_ENDLABELEDITW, 4294967120LL)
    X(LVN_ENDSCROLL, 4294967115LL)
    X(LVN_GETDISPINFO, 4294967146LL)
    X(LVN_GETDISPINFOA, 4294967146LL)
    X(LVN_GETDISPINFOW, 4294967119LL)
    X(LVN_GETEMPTYMARKUP, 4294967109LL)
    X(LVN_GETINFOTIP, 4294967139LL)
    X(LVN_GETINFOTIPA, 4294967139LL)
    X(LVN_GETINFOTIPW, 4294967138LL)
    X(LVN_HOTTRACK, 4294967175LL)
    X(LVN_INCREMENTALSEARCH, 4294967134LL)
    X(LVN_INCREMENTALSEARCHA, 4294967134LL)
    X(LVN_INCREMENTALSEARCHW, 4294967133LL)
    X(LVN_INSERTITEM, 4294967194LL)
    X(LVN_ITEMACTIVATE, 4294967182LL)
    X(LVN_ITEMCHANGED, 4294967195LL)
    X(LVN_ITEMCHANGING, 4294967196LL)
    X(LVN_KEYDOWN, 4294967141LL)
    X(LVN_LINKCLICK, 4294967112LL)
    X(LVN_MARQUEEBEGIN, 4294967140LL)
    X(LVN_ODCACHEHINT, 4294967183LL)
    X(LVN_ODFINDITEM, 4294967144LL)
    X(LVN_ODFINDITEMA, 4294967144LL)
    X(LVN_ODFINDITEMW, 4294967117LL)
    X(LVN_ODSTATECHANGED, 4294967181LL)
    X(LVN_SETDISPINFO, 4294967145LL)
    X(LVN_SETDISPINFOA, 4294967145LL)
    X(LVN_SETDISPINFOW, 4294967118LL)
    X(LVS_ALIGNLEFT, 2048LL)
    X(LVS_ALIGNMASK, 3072LL)
    X(LVS_ALIGNTOP, 0LL)
    X(LVS_AUTOARRANGE, 256LL)
    X(LVS_EDITLABELS, 512LL)
    X(LVS_EX_AUTOAUTOARRANGE, 16777216LL)
    X(LVS_EX_AUTOCHECKSELECT, 134217728LL)
    X(LVS_EX_AUTOSIZECOLUMNS, 268435456LL)
    X(LVS_EX_BORDERSELECT, 32768LL)
    X(LVS_EX_CHECKBOXES, 4LL)
    X(LVS_EX_COLUMNOVERFLOW, 2147483648LL)
    X(LVS_EX_COLUMNSNAPPOINTS, 1073741824LL)
    X(LVS_EX_DOUBLEBUFFER, 65536LL)
    X(LVS_EX_FLATSB, 256LL)
    X(LVS_EX_FULLROWSELECT, 32LL)
    X(LVS_EX_GRIDLINES, 1LL)
    X(LVS_EX_HEADERDRAGDROP, 16LL)
    X(LVS_EX_HEADERINALLVIEWS, 33554432LL)
    X(LVS_EX_HIDELABELS, 131072LL)
    X(LVS_EX_INFOTIP, 1024LL)
    X(LVS_EX_JUSTIFYCOLUMNS, 2097152LL)
    X(LVS_EX_LABELTIP, 16384LL)
    X(LVS_EX_MULTIWORKAREAS, 8192LL)
    X(LVS_EX_ONECLICKACTIVATE, 64LL)
    X(LVS_EX_REGIONAL, 512LL)
    X(LVS_EX_SIMPLESELECT, 1048576LL)
    X(LVS_EX_SINGLEROW, 262144LL)
    X(LVS_EX_SNAPTOGRID, 524288LL)
    X(LVS_EX_SUBITEMIMAGES, 2LL)
    X(LVS_EX_TRACKSELECT, 8LL)
    X(LVS_EX_TRANSPARENTBKGND, 4194304LL)
    X(LVS_EX_TRANSPARENTSHADOWTEXT, 8388608LL)
    X(LVS_EX_TWOCLICKACTIVATE, 128LL)
    X(LVS_EX_UNDERLINECOLD, 4096LL)
    X(LVS_EX_UNDERLINEHOT, 2048LL)
    X(LVS_ICON, 0LL)
    X(LVS_LIST, 3LL)
    X(LVS_NOCOLUMNHEADER, 16384LL)
    X(LVS_NOLABELWRAP, 128LL)
    X(LVS_NOSCROLL, 8192LL)
    X(LVS_NOSORTHEADER, 32768LL)
    X(LVS_OWNERDATA, 4096LL)
    X(LVS_OWNERDRAWFIXED, 1024LL)
    X(LVS_REPORT, 1LL)
    X(LVS_SHAREIMAGELISTS, 64LL)
    X(LVS_SHOWSELALWAYS, 8LL)
    X(LVS_SINGLESEL, 4LL)
    X(LVS_SMALLICON, 2LL)
    X(LVS_SORTASCENDING, 16LL)
    X(LVS_SORTDESCENDING, 32LL)
    X(LVS_TYPEMASK, 3LL)
    X(LVS_TYPESTYLEMASK, 64512LL)
    X(MB_ABORTRETRYIGNORE, 2LL)
    X(MB_APPLMODAL, 0LL)
    X(MB_CANCELTRYCONTINUE, 6LL)
    X(MB_DEFAULT_DESKTOP_ONLY, 131072LL)
    X(MB_DEFBUTTON1, 0LL)
    X(MB_DEFBUTTON2, 256LL)
    X(MB_DEFBUTTON3, 512LL)
    X(MB_DEFBUTTON4, 768LL)
    X(MB_HELP, 16384LL)
    X(MB_ICONASTERISK, 64LL)
    X(MB_ICONERROR, 16LL)
    X(MB_ICONEXCLAMATION, 48LL)
    X(MB_ICONHAND, 16LL)
    X(MB_ICONINFORMATION, 64LL)
    X(MB_ICONQUESTION, 32LL)
    X(MB_ICONSTOP, 16LL)
    X(MB_ICONWARNING, 48LL)
    X(MB_OK, 0LL)
    X(MB_OKCANCEL, 1LL)
    X(MB_OWNER_CENTER, 1073741824LL)
    X(MB_RETRYCANCEL, 5LL)
    X(MB_RIGHT, 524288LL)
    X(MB_RTLREADING, 1048576LL)
    X(MB_SERVICE_NOTIFICATION, 2097152LL)
    X(MB_SERVICE_NOTIFICATION_NT3X, 262144LL)
    X(MB_SETFOREGROUND, 65536LL)
    X(MB_SYSTEMMODAL, 4096LL)
    X(MB_TASKMODAL, 8192LL)
    X(MB_TOPMOST, 262144LL)
    X(MB_YESNO, 4LL)
    X(MB_YESNOCANCEL, 3LL)
    X(NULL_BRUSH, 5LL)
    X(ODA_DRAWENTIRE, 1LL)
    X(ODA_FOCUS, 4LL)
    X(ODA_SELECT, 2LL)
    X(ODS_CHECKED, 8LL)
    X(ODS_COMBOBOXEDIT, 4096LL)
    X(ODS_DEFAULT, 32LL)
    X(ODS_DISABLED, 4LL)
    X(ODS_FOCUS, 16LL)
    X(ODS_GRAYED, 2LL)
    X(ODS_SELECTED, 1LL)
    X(ODT_BUTTON, 4LL)
    X(ODT_COMBOBOX, 3LL)
    X(ODT_LISTBOX, 2LL)
    X(ODT_LISTVIEW, 102LL)
    X(ODT_MENU, 1LL)
    X(ODT_STATIC, 5LL)
    X(ODT_TAB, 101LL)
    X(PBM_DELTAPOS, 1027LL)
    X(PBM_GETBARCOLOR, 1039LL)
    X(PBM_GETBKCOLOR, 1038LL)
    X(PBM_GETPOS, 1032LL)
    X(PBM_GETRANGE, 1031LL)
    X(PBM_GETSTATE, 1041LL)
    X(PBM_GETSTEP, 1037LL)
    X(PBM_SETBARCOLOR, 1033LL)
    X(PBM_SETBKCOLOR, 8193LL)
    X(PBM_SETMARQUEE, 1034LL)
    X(PBM_SETPOS, 1026LL)
    X(PBM_SETRANGE, 1025LL)
    X(PBM_SETRANGE32, 1030LL)
    X(PBM_SETSTATE, 1040LL)
    X(PBM_SETSTEP, 1028LL)
    X(PBM_STEPIT, 1029LL)
    X(PBST_ERROR, 2LL)
    X(PBST_NORMAL, 1LL)
    X(PBST_PAUSED, 3LL)
    X(PBS_MARQUEE, 8LL)
    X(PBS_SMOOTH, 1LL)
    X(PBS_SMOOTHREVERSE, 16LL)
    X(PBS_VERTICAL, 4LL)
    X(PROP_LG_CXDLG, 252LL)
    X(PROP_LG_CYDLG, 218LL)
    X(PROP_MED_CXDLG, 227LL)
    X(PROP_MED_CYDLG, 215LL)
    X(PROP_SM_CXDLG, 212LL)
    X(PROP_SM_CYDLG, 188LL)
    X(PSBTN_APPLYNOW, 4LL)
    X(PSBTN_BACK, 0LL)
    X(PSBTN_CANCEL, 5LL)
    X(PSBTN_FINISH, 2LL)
    X(PSBTN_HELP, 6LL)
    X(PSBTN_MAX, 6LL)
    X(PSBTN_NEXT, 1LL)
    X(PSBTN_OK, 3LL)
    X(PSM_ADDPAGE, 1127LL)
    X(PSM_APPLY, 1134LL)
    X(PSM_CANCELTOCLOSE, 1131LL)
    X(PSM_CHANGED, 1128LL)
    X(PSM_ENABLEWIZBUTTONS, 1163LL)
    X(PSM_GETCURRENTPAGEHWND, 1142LL)
    X(PSM_GETRESULT, 1159LL)
    X(PSM_GETTABCONTROL, 1140LL)
    X(PSM_HWNDTOINDEX, 1153LL)
    X(PSM_IDTOINDEX, 1157LL)
    X(PSM_INDEXTOHWND, 1154LL)
    X(PSM_INDEXTOID, 1158LL)
    X(PSM_INDEXTOPAGE, 1156LL)
    X(PSM_INSERTPAGE, 1143LL)
    X(PSM_ISDIALOGMESSAGE, 1141LL)
    X(PSM_PAGETOINDEX, 1155LL)
    X(PSM_PRESSBUTTON, 1137LL)
    X(PSM_QUERYSIBLINGS, 1132LL)
    X(PSM_REBOOTSYSTEM, 1130LL)
    X(PSM_RECALCPAGESIZES, 1160LL)
    X(PSM_REMOVEPAGE, 1126LL)
    X(PSM_RESTARTWINDOWS, 1129LL)
    X(PSM_SETBUTTONTEXT, 1164LL)
    X(PSM_SETCURSEL, 1125LL)
    X(PSM_SETCURSELID, 1138LL)
    X(PSM_SETFINISHTEXT, 1139LL)
    X(PSM_SETHEADERSUBTITLE, 1151LL)
    X(PSM_SETHEADERTITLE, 1149LL)
    X(PSM_SETNEXTTEXT, 1161LL)
    X(PSM_SETTITLE, 1135LL)
    X(PSM_SETWIZBUTTONS, 1136LL)
    X(PSM_SHOWWIZBUTTONS, 1162LL)
    X(PSM_UNCHANGED, 1133LL)
    X(PSNRET_INVALID, 1LL)
    X(PSNRET_INVALID_NOCHANGEPAGE, 2LL)
    X(PSNRET_MESSAGEHANDLED, 3LL)
    X(PSNRET_NOERROR, 0LL)
    X(PSN_APPLY, 4294967094LL)
    X(PSN_GETOBJECT, 4294967086LL)
    X(PSN_HELP, 4294967091LL)
    X(PSN_KILLACTIVE, 4294967095LL)
    X(PSN_QUERYCANCEL, 4294967087LL)
    X(PSN_QUERYINITIALFOCUS, 4294967083LL)
    X(PSN_RESET, 4294967093LL)
    X(PSN_SETACTIVE, 4294967096LL)
    X(PSN_TRANSLATEACCELERATOR, 4294967084LL)
    X(PSN_WIZBACK, 4294967090LL)
    X(PSN_WIZFINISH, 4294967088LL)
    X(PSN_WIZNEXT, 4294967089LL)
    X(PSWIZBF_ELEVATIONREQUIRED, 0x00000001LL)
    X(PSWIZB_BACK, 1LL)
    X(PSWIZB_CANCEL, 0x00000010LL)
    X(PSWIZB_DISABLEDFINISH, 8LL)
    X(PSWIZB_FINISH, 4LL)
    X(PSWIZB_NEXT, 2LL)
    X(PSWIZB_RESTORE, 1LL)
    X(PSWIZB_SHOW, 0LL)
    X(PSWIZF_SETCOLOR, 4294967295LL)
    X(SBM_ENABLE_ARROWS, 228LL)
    X(SBM_GETPOS, 225LL)
    X(SBM_GETRANGE, 227LL)
    X(SBM_GETSCROLLBARINFO, 235LL)
    X(SBM_GETSCROLLINFO, 234LL)
    X(SBM_SETPOS, 224LL)
    X(SBM_SETRANGE, 226LL)
    X(SBM_SETRANGEREDRAW, 230LL)
    X(SBM_SETSCROLLINFO, 233LL)
    X(SBS_BOTTOMALIGN, 4LL)
    X(SBS_HORZ, 0LL)
    X(SBS_LEFTALIGN, 2LL)
    X(SBS_RIGHTALIGN, 4LL)
    X(SBS_SIZEBOX, 8LL)
    X(SBS_SIZEBOXBOTTOMRIGHTALIGN, 4LL)
    X(SBS_SIZEBOXTOPLEFTALIGN, 2LL)
    X(SBS_SIZEGRIP, 16LL)
    X(SBS_TOPALIGN, 2LL)
    X(SBS_VERT, 1LL)
    X(SB_BOTTOM, 7LL)
    X(SB_ENDSCROLL, 8LL)
    X(SB_LEFT, 6LL)
    X(SB_LINEDOWN, 1LL)
    X(SB_LINELEFT, 0LL)
    X(SB_LINERIGHT, 1LL)
    X(SB_LINEUP, 0LL)
    X(SB_PAGEDOWN, 3LL)
    X(SB_PAGELEFT, 2LL)
    X(SB_PAGERIGHT, 3LL)
    X(SB_PAGEUP, 2LL)
    X(SB_RIGHT, 7LL)
    X(SB_THUMBPOSITION, 4LL)
    X(SB_THUMBTRACK, 5LL)
    X(SB_TOP, 6LL)
    X(SIF_ALL, 23LL)
    X(SIF_DISABLENOSCROLL, 8LL)
    X(SIF_PAGE, 2LL)
    X(SIF_POS, 4LL)
    X(SIF_RANGE, 1LL)
    X(SIF_TRACKPOS, 16LL)
    X(SS_BITMAP, 14LL)
    X(SS_BLACKFRAME, 7LL)
    X(SS_BLACKRECT, 4LL)
    X(SS_CENTER, 1LL)
    X(SS_CENTERIMAGE, 512LL)
    X(SS_EDITCONTROL, 8192LL)
    X(SS_ELLIPSISMASK, 49152LL)
    X(SS_ENDELLIPSIS, 16384LL)
    X(SS_ENHMETAFILE, 15LL)
    X(SS_ETCHEDFRAME, 18LL)
    X(SS_ETCHEDHORZ, 16LL)
    X(SS_ETCHEDVERT, 17LL)
    X(SS_GRAYFRAME, 8LL)
    X(SS_GRAYRECT, 5LL)
    X(SS_ICON, 3LL)
    X(SS_LEFT, 0LL)
    X(SS_LEFTNOWORDWRAP, 12LL)
    X(SS_NOPREFIX, 128LL)
    X(SS_NOTIFY, 256LL)
    X(SS_OWNERDRAW, 13LL)
    X(SS_PATHELLIPSIS, 32768LL)
    X(SS_REALSIZECONTROL, 64LL)
    X(SS_REALSIZEIMAGE, 2048LL)
    X(SS_RIGHT, 2LL)
    X(SS_RIGHTJUST, 1024LL)
    X(SS_SIMPLE, 11LL)
    X(SS_SUNKEN, 4096LL)
    X(SS_TYPEMASK, 31LL)
    X(SS_USERITEM, 10LL)
    X(SS_WHITEFRAME, 9LL)
    X(SS_WHITERECT, 6LL)
    X(SS_WORDELLIPSIS, 49152LL)
    X(STM_GETICON, 369LL)
    X(STM_GETIMAGE, 371LL)
    X(STM_MSGMAX, 372LL)
    X(STM_SETICON, 368LL)
    X(STM_SETIMAGE, 370LL)
    X(STN_CLICKED, 0LL)
    X(STN_DBLCLK, 1LL)
    X(STN_DISABLE, 3LL)
    X(STN_ENABLE, 2LL)
    X(SW_FORCEMINIMIZE, 11LL)
    X(SW_HIDE, 0LL)
    X(SW_MAXIMIZE, 3LL)
    X(SW_MINIMIZE, 6LL)
    X(SW_NORMAL, 1LL)
    X(SW_RESTORE, 9LL)
    X(SW_SHOW, 5LL)
    X(SW_SHOWDEFAULT, 10LL)
    X(SW_SHOWMAXIMIZED, 3LL)
    X(SW_SHOWMINIMIZED, 2LL)
    X(SW_SHOWMINNOACTIVE, 7LL)
    X(SW_SHOWNA, 8LL)
    X(SW_SHOWNOACTIVATE, 4LL)
    X(SW_SHOWNORMAL, 1LL)
    X(TBCD_CHANNEL, 3LL)
    X(TBCD_THUMB, 2LL)
    X(TBCD_TICS, 1LL)
    X(TBM_CLEARSEL, 1043LL)
    X(TBM_CLEARTICS, 1033LL)
    X(TBM_GETBUDDY, 1057LL)
    X(TBM_GETCHANNELRECT, 1050LL)
    X(TBM_GETLINESIZE, 1048LL)
    X(TBM_GETNUMTICS, 1040LL)
    X(TBM_GETPAGESIZE, 1046LL)
    X(TBM_GETPOS, 1024LL)
    X(TBM_GETPTICS, 1038LL)
    X(TBM_GETRANGEMAX, 1026LL)
    X(TBM_GETRANGEMIN, 1025LL)
    X(TBM_GETSELEND, 1042LL)
    X(TBM_GETSELSTART, 1041LL)
    X(TBM_GETTHUMBLENGTH, 1052LL)
    X(TBM_GETTHUMBRECT, 1049LL)
    X(TBM_GETTIC, 1027LL)
    X(TBM_GETTICPOS, 1039LL)
    X(TBM_GETTOOLTIPS, 1054LL)
    X(TBM_GETUNICODEFORMAT, 8198LL)
    X(TBM_SETBUDDY, 1056LL)
    X(TBM_SETLINESIZE, 1047LL)
    X(TBM_SETPAGESIZE, 1045LL)
    X(TBM_SETPOS, 1029LL)
    X(TBM_SETRANGE, 1030LL)
    X(TBM_SETRANGEMAX, 1032LL)
    X(TBM_SETRANGEMIN, 1031LL)
    X(TBM_SETSEL, 1034LL)
    X(TBM_SETSELEND, 1036LL)
    X(TBM_SETSELSTART, 1035LL)
    X(TBM_SETTHUMBLENGTH, 1051LL)
    X(TBM_SETTIC, 1028LL)
    X(TBM_SETTICFREQ, 1044LL)
    X(TBM_SETTIPSIDE, 1055LL)
    X(TBM_SETTOOLTIPS, 1053LL)
    X(TBM_SETUNICODEFORMAT, 8197LL)
    X(TBS_AUTOTICKS, 1LL)
    X(TBS_BOTH, 8LL)
    X(TBS_BOTTOM, 0LL)
    X(TBS_DOWNISLEFT, 1024LL)
    X(TBS_ENABLESELRANGE, 32LL)
    X(TBS_FIXEDLENGTH, 64LL)
    X(TBS_HORZ, 0LL)
    X(TBS_LEFT, 4LL)
    X(TBS_NOTHUMB, 128LL)
    X(TBS_NOTICKS, 16LL)
    X(TBS_NOTIFYBEFOREMOVE, 2048LL)
    X(TBS_REVERSED, 512LL)
    X(TBS_RIGHT, 0LL)
    X(TBS_TOOLTIPS, 256LL)
    X(TBS_TOP, 4LL)
    X(TBS_TRANSPARENTBKGND, 4096LL)
    X(TBS_VERT, 2LL)
    X(TBTS_BOTTOM, 2LL)
    X(TBTS_LEFT, 1LL)
    X(TBTS_RIGHT, 3LL)
    X(TBTS_TOP, 0LL)
    X(TB_BOTTOM, 7LL)
    X(TB_ENDTRACK, 8LL)
    X(TB_LINEDOWN, 1LL)
    X(TB_LINEUP, 0LL)
    X(TB_PAGEDOWN, 3LL)
    X(TB_PAGEUP, 2LL)
    X(TB_THUMBPOSITION, 4LL)
    X(TB_THUMBTRACK, 5LL)
    X(TB_TOP, 6LL)
    X(TCHT_NOWHERE, 1LL)
    X(TCHT_ONITEM, 6LL)
    X(TCHT_ONITEMICON, 2LL)
    X(TCHT_ONITEMLABEL, 4LL)
    X(TCIF_IMAGE, 2LL)
    X(TCIF_PARAM, 8LL)
    X(TCIF_RTLREADING, 4LL)
    X(TCIF_STATE, 16LL)
    X(TCIF_TEXT, 1LL)
    X(TCIS_BUTTONPRESSED, 1LL)
    X(TCIS_HIGHLIGHTED, 2LL)
    X(TCM_ADJUSTRECT, 4904LL)
    X(TCM_DELETEALLITEMS, 4873LL)
    X(TCM_DELETEITEM, 4872LL)
    X(TCM_DESELECTALL, 4914LL)
    X(TCM_GETCURFOCUS, 4911LL)
    X(TCM_GETCURSEL, 4875LL)
    X(TCM_GETEXTENDEDSTYLE, 4917LL)
    X(TCM_GETIMAGELIST, 4866LL)
    X(TCM_GETITEMCOUNT, 4868LL)
    X(TCM_GETITEMRECT, 4874LL)
    X(TCM_GETROWCOUNT, 4908LL)
    X(TCM_GETTOOLTIPS, 4909LL)
    X(TCM_GETUNICODEFORMAT, 8198LL)
    X(TCM_HIGHLIGHTITEM, 4915LL)
    X(TCM_HITTEST, 4877LL)
    X(TCM_REMOVEIMAGE, 4906LL)
    X(TCM_SETCURFOCUS, 4912LL)
    X(TCM_SETCURSEL, 4876LL)
    X(TCM_SETEXTENDEDSTYLE, 4916LL)
    X(TCM_SETIMAGELIST, 4867LL)
    X(TCM_SETITEMEXTRA, 4878LL)
    X(TCM_SETITEMSIZE, 4905LL)
    X(TCM_SETMINTABWIDTH, 4913LL)
    X(TCM_SETPADDING, 4907LL)
    X(TCM_SETTOOLTIPS, 4910LL)
    X(TCM_SETUNICODEFORMAT, 8197LL)
    X(TCN_FOCUSCHANGE, 4294966742LL)
    X(TCN_GETOBJECT, 4294966743LL)
    X(TCN_KEYDOWN, 4294966746LL)
    X(TCN_SELCHANGE, 4294966745LL)
    X(TCN_SELCHANGING, 4294966744LL)
    X(TCS_BOTTOM, 2LL)
    X(TCS_BUTTONS, 256LL)
    X(TCS_EX_FLATSEPARATORS, 1LL)
    X(TCS_EX_REGISTERDROP, 2LL)
    X(TCS_FIXEDWIDTH, 1024LL)
    X(TCS_FLATBUTTONS, 8LL)
    X(TCS_FOCUSNEVER, 32768LL)
    X(TCS_FOCUSONBUTTONDOWN, 4096LL)
    X(TCS_FORCEICONLEFT, 16LL)
    X(TCS_FORCELABELLEFT, 32LL)
    X(TCS_HOTTRACK, 64LL)
    X(TCS_MULTILINE, 512LL)
    X(TCS_MULTISELECT, 4LL)
    X(TCS_OWNERDRAWFIXED, 8192LL)
    X(TCS_RAGGEDRIGHT, 2048LL)
    X(TCS_RIGHT, 2LL)
    X(TCS_RIGHTJUSTIFY, 0LL)
    X(TCS_SCROLLOPPOSITE, 1LL)
    X(TCS_SINGLELINE, 0LL)
    X(TCS_TABS, 0LL)
    X(TCS_TOOLTIPS, 16384LL)
    X(TCS_VERTICAL, 128LL)
    X(TRBN_THUMBPOSCHANGING, 4294965794LL)
    X(WB_ISDELIMITER, 2LL)
    X(WB_LEFT, 0LL)
    X(WB_RIGHT, 1LL)
    X(WHITE_BRUSH, 0LL)
    X(WIZ_BODYCX, 184LL)
    X(WIZ_BODYX, 92LL)
    X(WIZ_CXBMP, 80LL)
    X(WIZ_CXDLG, 276LL)
    X(WIZ_CYDLG, 140LL)
    X(WS_BORDER, 8388608LL)
    X(WS_CAPTION, 12582912LL)
    X(WS_CHILD, 1073741824LL)
    X(WS_CHILDWINDOW, 1073741824LL)
    X(WS_CLIPCHILDREN, 33554432LL)
    X(WS_CLIPSIBLINGS, 67108864LL)
    X(WS_DISABLED, 134217728LL)
    X(WS_DLGFRAME, 4194304LL)
    X(WS_EX_ACCEPTFILES, 16LL)
    X(WS_EX_APPWINDOW, 262144LL)
    X(WS_EX_CLIENTEDGE, 512LL)
    X(WS_EX_COMPOSITED, 33554432LL)
    X(WS_EX_CONTEXTHELP, 1024LL)
    X(WS_EX_CONTROLPARENT, 65536LL)
    X(WS_EX_DLGMODALFRAME, 1LL)
    X(WS_EX_LAYERED, 524288LL)
    X(WS_EX_LAYOUTRTL, 4194304LL)
    X(WS_EX_LEFT, 0LL)
    X(WS_EX_LEFTSCROLLBAR, 16384LL)
    X(WS_EX_LTRREADING, 0LL)
    X(WS_EX_MDICHILD, 64LL)
    X(WS_EX_NOACTIVATE, 134217728LL)
    X(WS_EX_NOINHERITLAYOUT, 1048576LL)
    X(WS_EX_NOPARENTNOTIFY, 4LL)
    X(WS_EX_OVERLAPPEDWINDOW, 768LL)
    X(WS_EX_PALETTEWINDOW, 392LL)
    X(WS_EX_RIGHT, 4096LL)
    X(WS_EX_RIGHTSCROLLBAR, 0LL)
    X(WS_EX_RTLREADING, 8192LL)
    X(WS_EX_STATICEDGE, 131072LL)
    X(WS_EX_TOOLWINDOW, 128LL)
    X(WS_EX_TOPMOST, 8LL)
    X(WS_EX_TRANSPARENT, 32LL)
    X(WS_EX_WINDOWEDGE, 256LL)
    X(WS_GROUP, 131072LL)
    X(WS_HSCROLL, 1048576LL)
    X(WS_ICONIC, 536870912LL)
    X(WS_MAXIMIZE, 16777216LL)
    X(WS_MAXIMIZEBOX, 65536LL)
    X(WS_MINIMIZE, 536870912LL)
    X(WS_MINIMIZEBOX, 131072LL)
    X(WS_OVERLAPPED, 0LL)
    X(WS_OVERLAPPEDWINDOW, 13565952LL)
    X(WS_POPUP, 2147483648LL)
    X(WS_POPUPWINDOW, 2156396544LL)
    X(WS_SIZEBOX, 262144LL)
    X(WS_SYSMENU, 524288LL)
    X(WS_TABSTOP, 65536LL)
    X(WS_THICKFRAME, 262144LL)
    X(WS_TILED, 0LL)
    X(WS_TILEDWINDOW, 13565952LL)
    X(WS_VISIBLE, 268435456LL)
    X(WS_VSCROLL, 2097152LL)
    X(TCN_FOCUSCHANGE, 4294966742ULL)
    X(TCN_GETOBJECT, 4294966743ULL)
    X(TCN_KEYDOWN, 4294966746ULL)
    X(TCN_SELCHANGE, 4294966745ULL)
    X(TCN_SELCHANGING, 4294966744ULL)

    Method(TJS_N("initCommonControls"),   &Class::InitCommonControls);
    Method(TJS_N("initCommonControlsEx"), &Class::InitCommonControlsEx);
    Method(TJS_N("getOctetAddress"),      &Class::GetOctetAddress);
    Method(TJS_N("getStringAddress"),     &Class::GetStringAddress);

    RawCallback(TJS_N("chooseColor"), &Class::chooseColor, TJS_STATICMEMBER);
    RawCallback(TJS_N("messageBox"),  &Class::messageBox,  TJS_STATICMEMBER);

    Method(TJS_N("openPropertySheet"), &Class::OpenPropertySheet);
    Method(TJS_N("propSheetMessage"),  &Class::propSheetMessage);
    Method(TJS_N("setMessageResult"),  &Class::setMessageResult);
    Property(TJS_N("propsheet"),       &Class::isPropertySheet, 0);

    Method(TJS_N("insertTab"),    &Class::InsertTabItem);
    Method(TJS_N("deleteTab"),    &Class::DeleteTabItem);
    Method(TJS_N("deleteAllTab"), &Class::DeleteAllTabItem);
    Method(TJS_N("getCurSel"),    &Class::GetCurSelTab);
    Method(TJS_N("setCurSel"),    &Class::SetCurSelTab);
    Method(TJS_N("selectTab"),    &Class::SelectTab);

    RawCallback(TJS_N("openProgress"), &Class::openProgress, 0);
    Property(TJS_N("progress"),         &Class::isProgress, (int)0);
    Property(TJS_N("progressValue"),    &Class::getProgressValue,    &Class::setProgressValue);
    Property(TJS_N("progressCanceled"), &Class::getProgressCanceled, &Class::setProgressCanceled);
    Method(TJS_N("closeProgress"),      &Class::closeProgress);

    Method(TJS_N("getScrollInfo"), &Class::getScrollInfo);
    RawCallback(TJS_N("setScrollInfo"), &Class::setScrollInfo, 0);

    Method(TJS_N("bringToFront"), &Class::BringToFront);
}
#undef X
