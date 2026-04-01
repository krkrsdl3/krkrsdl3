#include "ncbind/ncbind.hpp"

#include <optional>

#include "Log.h"
#include "FreeTypeFontRasterizer.h"

#define NCB_MODULE_NAME TJS_N("textrender.dll")

// use Kirikiri-Z rasterizer for layouting

using RgbColor = uint32_t;

#define setprop_t(d, p, ty) \
    { \
        tTJSVariant v(ty(p)); \
        d->PropSet(TJS_MEMBERENSURE, TJS_N(#p), nullptr, &v, d); \
    }

#define setprop_opt_t(d, p, ty) \
    if (0 != p) \
    { \
        tTJSVariant v(ty(p)); \
        d->PropSet(TJS_MEMBERENSURE, TJS_N(#p), nullptr, &v, d); \
    } \
    else \
    { \
        tTJSVariant v; \
        d->PropSet(TJS_MEMBERENSURE, TJS_N(#p), nullptr, &v, d); \
    }

#define setprop(d, p) setprop_t(d, p, )

#define setprop_opt(d, p) setprop_opt_t(d, p, )

#define getprop_opt(d, p) getprop_opt_t(d, p, )

#define getprop_t(d, p, ty) \
    { \
        tTJSVariant v; \
        if (TJS_SUCCEEDED(d->PropGet(0, TJS_N(#p), nullptr, &v, d)) && v.Type() != tvtVoid) \
        { \
            p = ty(v); \
        } \
    }

#define getprop_opt_t(d, p, ty) \
    { \
        tTJSVariant v; \
        if (TJS_SUCCEEDED(d->PropGet(0, TJS_N(#p), nullptr, &v, d)) && v.Type() != tvtVoid) \
        { \
            p = ty(v); \
        } \
        else \
        { \
            p = 0; \
        } \
    }

#define getprop_ensure_opt_deref(d, p, e) \
    { \
        tTJSVariant v; \
        if (TJS_SUCCEEDED(d->PropGet(0, TJS_N(#p), nullptr, &v, d)) && v.Type() != tvtVoid) \
        { \
            auto ens = v.e; \
            if (ens) \
                p = *ens; \
            else \
                p = std::nullopt; \
        } \
        else \
        { \
            p = std::nullopt; \
        } \
    }

#define getprop(d, p) getprop_t(d, p, )

#define getprop_ensure_deref(d, p, e) \
    { \
        tTJSVariant v; \
        if (TJS_SUCCEEDED(d->PropGet(0, TJS_N(#p), nullptr, &v, d)) && v.Type() != tvtVoid) \
        { \
            auto ens = v.e; \
            if (ens) \
                p = *ens; \
        } \
    }

struct TextRenderState
{
    bool bold = false;
    bool italic = false;
    ttstr face = TJS_N("user");
    int fontsize = 24;
    double fontscale = 1.0;
    RgbColor chColor = 0xffffff;
    int rubySize = 10;
    int rubyOffset = -2;
    bool shadow = true;
    RgbColor shadowColor = 0x000000;
    bool edge = false;
    RgbColor edgeColor = 0x0080ff;
    int lineSpacing = 6;
    int pitch = 0;
    int lineSize = 0;
    int align = -1;
    int valign = -1;

    bool renderOver = false;
    int renderDelay = 1000;
    ttstr renderText = TJS_N("");

    // -------------------------------------------------------------- //

    tTJSVariant serialize() const
    {
        auto dict = TJSCreateDictionaryObject();

        setprop(dict, bold);
        setprop(dict, italic);
        setprop(dict, fontsize);
        setprop(dict, fontscale);
        setprop(dict, face);
        setprop_t(dict, chColor, static_cast<tjs_int>);
        setprop(dict, rubySize);
        setprop(dict, rubyOffset);
        setprop(dict, shadow);
        setprop_t(dict, shadowColor, static_cast<tjs_int>);
        setprop(dict, edge);
        setprop_t(dict, edgeColor, static_cast<tjs_int>);
        setprop(dict, lineSpacing);
        setprop(dict, pitch);
        setprop(dict, lineSize);
        setprop(dict, align);
        setprop(dict, valign);

        auto res = tTJSVariant(dict, dict);
        dict->Release();

        return res;
    }

    void deserialize(tTJSVariant t)
    {
        auto dict = t.AsObjectNoAddRef();
        if (!dict)
        {
            return;
        }

        getprop_t(dict, bold, static_cast<tjs_int>);
        getprop_t(dict, italic, static_cast<tjs_int>);
        getprop(dict, fontsize);
        getprop(dict, fontscale);
        getprop_ensure_deref(dict, face, AsStringNoAddRef());
        getprop_t(dict, chColor, static_cast<tjs_int>);
        getprop(dict, rubySize);
        getprop(dict, rubyOffset);
        getprop_t(dict, shadow, static_cast<tjs_int>);
        getprop_t(dict, shadowColor, static_cast<tjs_int>);
        getprop_t(dict, edge, static_cast<tjs_int>);
        getprop_t(dict, edgeColor, static_cast<tjs_int>);
        getprop(dict, lineSpacing);
        getprop(dict, pitch);
        getprop(dict, lineSize);
        getprop(dict, align);
        getprop(dict, valign);
    }

    static TextRenderState from(tTJSVariant t)
    {
        TextRenderState state{};
        state.deserialize(t);
        return state;
    }
};

struct TextRenderOptions
{
    const tjs_wchar* following =
        TJS_W("%),:;]}????。，、．：；゛゜ヽヾゝゞ々’”）〕］｝〉》」』】°′″℃￠％‰　!.?=????????????"
              "?？！ーぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ");
    const tjs_wchar* leading = TJS_W("\\$([{?‘“（〔［｛〈《「『【￥＄￡");
    const tjs_wchar* begin = TJS_W("「『（‘“〔［｛〈《");
    const tjs_wchar* end = TJS_W("」』）’”〕］｝〉》");

    // -------------------------------------------------------------- //

    tTJSVariant serialize() const
    {
        auto dict = TJSCreateDictionaryObject();

        setprop(dict, following);
        setprop(dict, leading);
        setprop(dict, begin);
        setprop(dict, end);

        auto res = tTJSVariant(dict, dict);
        dict->Release();

        return res;
    }

    void deserialize(tTJSVariant t)
    {
        auto dict = t.AsObjectNoAddRef();
        if (!dict)
        {
            return;
        }
    }

    static TextRenderState from(tTJSVariant t)
    {
        TextRenderState state{};
        state.deserialize(t);
        return state;
    }
};

struct CharacterInfo
{
    bool bold = false;
    bool italic = false;
    bool graph = false;
    bool vertical = false;
    ttstr face = TJS_N("user");

    int x = 0;
    int y = 0;
    int cw = 0;
    int size = 0;

    RgbColor color = 0xffffff;
    RgbColor edge = 0x00;   //
    RgbColor shadow = 0x00; //

    ttstr text = TJS_N("");

    // -------------------------------------------------------------- //

    tTJSVariant serialize() const
    {
        auto dict = TJSCreateDictionaryObject();

        setprop(dict, bold);
        setprop(dict, italic);
        setprop(dict, graph);
        setprop(dict, vertical);
        setprop(dict, x);
        setprop(dict, y);
        setprop(dict, cw);
        setprop(dict, size);
        setprop(dict, face);

        setprop_t(dict, color, static_cast<tjs_int>);
        setprop_opt_t(dict, edge, static_cast<tjs_int>);
        setprop_opt_t(dict, shadow, static_cast<tjs_int>);

        setprop(dict, text);

        auto res = tTJSVariant(dict, dict);
        dict->Release();

        return res;
    }

    void deserialize(tTJSVariant t)
    {
        auto dict = t.AsObjectNoAddRef();
        if (!dict)
        {
            return;
        }

        getprop(dict, bold);
        getprop(dict, italic);
        getprop(dict, graph);
        getprop(dict, vertical);
        getprop(dict, x);
        getprop(dict, y);
        getprop(dict, cw);
        getprop(dict, size);
        getprop_ensure_deref(dict, face, AsStringNoAddRef());

        getprop_t(dict, color, static_cast<tjs_int>);
        getprop_opt_t(dict, edge, static_cast<tjs_int>);
        getprop_opt_t(dict, shadow, static_cast<tjs_int>);

        getprop_ensure_deref(dict, text, AsStringNoAddRef());
    }

    static TextRenderState from(tTJSVariant t)
    {
        TextRenderState state{};
        state.deserialize(t);
        return state;
    }
};

#define property_accessor(name, type, storage) \
    type get_##name() const \
    { \
        return storage; \
    } \
    void set_##name(type v) \
    { \
        storage = v; \
    }

#define property_accessor_cast(name, type, cast, storage) \
    cast get_##name() const \
    { \
        return cast(storage); \
    } \
    void set_##name(cast v) \
    { \
        storage = type(v); \
    }

#define property_accessor_string(name, storage) \
    tTJSVariant get_##name() const \
    { \
        return tTJSVariant(storage); \
    } \
    void set_##name(tTJSVariant v) \
    { \
        auto s = v.AsStringNoAddRef(); \
        storage = s->LongString ? s->LongString : s->ShortString; \
    }

#define property_delegate(name) NCB_PROPERTY(name, get_##name, set_##name);

class TextRenderBase
{
public:
    TextRenderBase();
    virtual ~TextRenderBase();
    bool render(tTJSString text, int autoIndent, int diff, int all, bool _reserved);
    void setRenderSize(int width, int height);
    void setDefault(tTJSVariant defaultSettings);
    void setOption(tTJSVariant options);
    tTJSVariant getCharacters(int start, int end);
    void clear();
    void done();

    void resetFont();
    void resetStyle();
    tTJSVariant getKeyWait();
    void setKeyWait(tTJSVariant) { throw "no support setKeyWait"; };
    int calcShowCount();

    // property accessor
    property_accessor(vertical, bool, m_vertical);

    property_accessor(bold, bool, m_state.bold);
    property_accessor(italic, bool, m_state.italic);
    property_accessor_string(face, m_state.face);
    property_accessor(fontSize, int, m_state.fontsize);
    property_accessor(fontScale, double, m_state.fontscale);
    property_accessor_cast(chColor, RgbColor, tjs_int, m_state.chColor);
    property_accessor(rubySize, int, m_state.rubySize);
    property_accessor(rubyOffset, int, m_state.rubyOffset);
    property_accessor(shadow, bool, m_state.shadow);
    property_accessor_cast(shadowColor, RgbColor, tjs_int, m_state.shadowColor);
    property_accessor(edge, bool, m_state.edge);
    property_accessor(lineSpacing, int, m_state.lineSpacing);
    property_accessor(pitch, int, m_state.pitch);
    property_accessor(lineSize, int, m_state.lineSize);

    property_accessor(renderOver, bool, m_state.renderOver);
    property_accessor(renderDelay, int, m_state.renderDelay);
    property_accessor(renderLeft, int, m_boxLeft);
    int get_renderRight() const { return m_boxLeft + m_boxWidth; }
    void set_renderRight(int v) { m_boxWidth = v - m_boxLeft; };
    property_accessor(renderTop, int, m_boxTop);
    int get_renderBottom() const { return m_boxTop + m_boxHeight; }
    void set_renderBottom(int v) { m_boxHeight = v - m_boxTop; };
    property_accessor(renderText, ttstr, m_state.renderText);
    int get_renderCount() const { return m_state.renderText.length(); }
    void set_renderCount(int v) { throw "avoid to set renderCount"; };

    property_accessor(defaultBold, bool, m_default.bold);
    property_accessor(defaultItalic, bool, m_default.italic);
    property_accessor_string(defaultFace, m_default.face);
    property_accessor(defaultFontSize, int, m_default.fontsize);
    property_accessor(defaultFontScale, float, m_default.fontscale);
    property_accessor_cast(defaultChColor, RgbColor, tjs_int, m_default.chColor);
    property_accessor(defaultRubySize, int, m_default.rubySize);
    property_accessor(defaultRubyOffset, int, m_default.rubyOffset);
    property_accessor(defaultShadow, bool, m_default.shadow);
    property_accessor_cast(defaultShadowColor, RgbColor, tjs_int, m_default.shadowColor);
    property_accessor(defaultEdge, bool, m_default.edge);
    property_accessor(defaultLineSpacing, int, m_default.lineSpacing);
    property_accessor(defaultPitch, int, m_default.pitch);
    property_accessor(defaultLineSize, int, m_default.lineSize);
    property_accessor(defaultValign, int, m_default.valign);

private:
    int m_boxLeft = 0;
    int m_boxTop = 0;
    int m_boxWidth = 0;
    int m_boxHeight = 0;

    int curr_x = 0;
    int curr_y = 0;

    int m_indent = 0;
    int m_autoIndent = 0;
    bool m_overflow = false;
    bool m_isBeginningOfLine = true;

    bool m_vertical = false;

    // 需要与主渲染隔离
    FontRasterizer* m_rasterizer = NULL;
    FontRasterizer* GetCurrentRasterizer();

    TextRenderOptions m_options{};
    TextRenderState m_default{};
    TextRenderState m_state{};

    std::vector<CharacterInfo> m_characters{};
    std::vector<CharacterInfo> m_buffer{};
    uint32_t m_mode = 0;

    void pushCharacter(const tjs_char* ch);
    void pushGraphicalCharacter(ttstr const& graph);
    void performLinebreak();
    void flush(bool force = false);
    void updateFont();
};

enum TextRenderAlignment
{
    kTextRenderAlignmentLeft = -1,
    kTextRenderAlignmentCenter = 0,
    kTextRenderAlignmentRight = 1,
};

// 这个没理解，后续再研究
enum TextRenderMode
{
    kTextRenderModeLeading = 0,
    kTextRenderModeNormal,
    kTextRenderModeFollowing,
};

// -------------------------------------------------------------------

TextRenderBase::TextRenderBase()
{
}

TextRenderBase::~TextRenderBase()
{
    if (!m_rasterizer)
        m_rasterizer->Release();
}

static int read_integer(tTJSString const& str, const tjs_char* p, int& value)
{
    bool is_negative = false;
    int retLen = 0;
    while (true)
    {
        int chLen = utf8_char_len(p + retLen);
        if (chLen != 1)
        {
            TVPThrowExceptionMessage(TJS_N("TextRenderBase::render() failed to "
                                           "parse: expected either integer or ';', found EOF"));
        }
        tjs_char ch = *(p + retLen);
        retLen++;
        if ('0' <= ch && ch <= '9')
        {
            value = value * 10 + (ch - '0');
            continue;
        }
        else if (ch == '-')
        {
            is_negative = !is_negative;
            continue;
        }
        else if (ch == ';')
        {
            if (is_negative)
            {
                value = -value;
            }
            break;
        }

        TVPThrowExceptionMessage(TJS_N("TextRenderBase::render() failed to "
                                       "parse: expected either integer or ';', found '%1'"),
                                 ch);
    }
    return retLen;
}

static const char* constChar = "\t ";
bool TextRenderBase::render(tTJSString text, int autoIndent, int diff, int all, bool same)
{
    const tjs_char* p = text.c_str();
    for (; *p; p++)
    {
        int chLen = utf8_char_len(p);
        if (chLen <= 0)
            continue;

        if (chLen == 1)
        {
            bool hasget = true;
            switch (*p)
            {
                case '%':
                    p += chLen;
                    chLen = utf8_char_len(p);
                    if (chLen != 1)
                    {
                        TVPThrowExceptionMessage(TJS_N("TextRenderBase::render() failed to "
                                                       "parse: expected character, found EOF"));
                    }
                    switch (*p)
                    {
                        case 't':
                        case 'f':
                        {
                            ttstr faceName{};
                            while (true)
                            {
                                p += chLen;
                                chLen = utf8_char_len(p);
                                if (chLen == 0)
                                {
                                    TVPThrowExceptionMessage(
                                        TJS_N("TextRenderBase::render() failed to "
                                              "parse: expected character, found EOF"));
                                }

                                if (chLen == 1 && *p == ';')
                                    break;

                                faceName += ttstr(p, chLen);
                            }

                            m_state.face = faceName;

                            break;
                        }
                        case 'b':
                        {
                            p += chLen;
                            chLen = utf8_char_len(p);
                            if (chLen != 1 || (*p != '0' && *p != '1'))
                            {
                                TVPThrowExceptionMessage(
                                    TJS_N("TextRenderBase::render() failed to "
                                          "parse %%b: expected either '0' or '1', found EOF"));
                            }
                            bool flag = *p == '1';
                            m_state.bold = flag;

                            break;
                        }
                        case 'i':
                        {
                            p += chLen;
                            chLen = utf8_char_len(p);
                            if (chLen != 1 || (*p != '0' && *p != '1'))
                            {
                                TVPThrowExceptionMessage(
                                    TJS_N("TextRenderBase::render() failed to "
                                          "parse %%i: expected either '0' or '1', found EOF"));
                            }
                            bool flag = *p == '1';
                            m_state.italic = flag;
                            break;
                        }
                        case 's':
                        {
                            p += chLen;
                            chLen = utf8_char_len(p);
                            if (chLen != 1 || (*p != '0' && *p != '1'))
                            {
                                TVPThrowExceptionMessage(
                                    TJS_N("TextRenderBase::render() failed to "
                                          "parse %%s: expected either '0' or '1', found EOF"));
                            }
                            bool flag = *p == '1';
                            m_state.shadow = flag;
                            break;
                        }
                        case 'e':
                        {
                            p += chLen;
                            chLen = utf8_char_len(p);
                            if (chLen != 1 || (*p != '0' && *p != '1'))
                            {
                                TVPThrowExceptionMessage(
                                    TJS_N("TextRenderBase::render() failed to "
                                          "parse %%e: expected either '0' or '1', found '%1'"),
                                    *p);
                            }
                            bool flag = *p == '1';
                            m_state.edge = flag;
                            break;
                        }
                        case 'B': // TODO: Big
                            TVPConsoleLog(TJS_N("TODO big font"));
                            break;
                        case 'S': // TODO: Small
                            TVPConsoleLog(TJS_N("TODO small font"));
                            break;
                        case 'r': // Reset
                            resetFont();
                            break;
                        case 'C': // TODO: Centre
                            TVPConsoleLog(TJS_N("TODO centre"));
                            break;
                        case 'R': // TODO: Right
                            TVPConsoleLog(TJS_N("TODO right"));
                            break;
                        case 'L': // TODO: Left
                            TVPConsoleLog(TJS_N("TODO left"));
                            break;
                        case 'l':
                        {
                            while (true)
                            {
                                p += chLen;
                                chLen = utf8_char_len(p);
                                if (chLen == 0)
                                {
                                    TVPThrowExceptionMessage(
                                        TJS_N("TextRenderBase::render() failed to "
                                              "parse: expected character, found EOF"));
                                }
                                if (chLen == 1 && *p == ';')
                                    break;
                            }
                            break;
                        }
                        case 'n':
                        {
                            int n = 0;
                            p++;
                            chLen = read_integer(text, p, n);
                            if (chLen > 0)
                                p += chLen - 1;
                            if (n == 0)
                                n = 1;
                            flush();
                            performLinebreak();
                            for (int i = 0; i < n; i++)
                                performLinebreak();
                            break;
                        }
                        case 'p':
                        {
                            int value = 0;
                            p++;
                            chLen = read_integer(text, p, value);
                            if (chLen > 0)
                                p += chLen - 1;
                            m_state.pitch = value;
                            break;
                        }
                        case 'd':
                        {
                            int value = 0;
                            p++;
                            chLen = read_integer(text, p, value);
                            if (chLen > 0)
                                p += chLen - 1;
                            // TODO: text speed
                            break;
                        }
                        case 'w':
                        {
                            int value = 0;
                            p++;
                            chLen = read_integer(text, p, value);
                            if (chLen > 0)
                                p += chLen - 1;
                            // TODO: wait
                            break;
                        }
                        case 'D': // %D[0-9]+; || %D$.+;
                        {
                            p += chLen;
                            chLen = utf8_char_len(p);
                            if (chLen == 1 && *p == '$')
                            {
                                ttstr labelName{};
                                while (true)
                                {
                                    p += chLen;
                                    chLen = utf8_char_len(p);
                                    if (chLen == 0)
                                    {
                                        TVPThrowExceptionMessage(
                                            TJS_N("TextRenderBase::render() failed to "
                                                  "parse: expected character, found EOF"));
                                    }
                                    if (chLen == 1 && *p == ';')
                                        break;
                                    labelName += ttstr(p, chLen);
                                }
                                // TODO: labelName
                            }
                            else
                            {
                                int value = 0;
                                p += chLen;
                                chLen = read_integer(text, p, value);
                                if (chLen > 0)
                                    p += chLen - 1;
                                // TODO: label?
                            }
                            //
                            break;
                        }
                        case '0':
                        case '1':
                        case '2':
                        case '3':
                        case '4':
                        case '5':
                        case '6':
                        case '7':
                        case '8':
                        case '9':
                        {
                            int value = static_cast<int>(*p - '0');
                            p += chLen;
                            chLen = read_integer(text, p, value);
                            if (chLen > 0)
                                p += chLen - 1;

                            // font size
                            m_state.fontsize = m_default.fontsize * value / 100;

                            updateFont();

                            break;
                        }
                        default:
                            TVPThrowExceptionMessage(
                                TJS_N("TextRenderBase::render() failed to "
                                      "parse: expected any of 'fbiseBSrCRLpdwD0123456789', found "
                                      "'%1'"),
                                *p);
                            break;
                    }
                    break;
                case '\\':
                    p += chLen;
                    chLen = utf8_char_len(p);
                    if (chLen != 1)
                    {
                        TVPThrowExceptionMessage(TJS_N("TextRenderBase::render() failed to "
                                                       "parse: expected character, found EOF"));
                    }
                    switch (*p)
                    {
                        case 'n':
                            flush();
                            performLinebreak();
                            break;
                        case 't':
                            pushCharacter(constChar);
                            break;
                        case 'i':
                            m_indent = curr_x;
                            break;
                        case 'r':
                            m_indent = 0;
                            break;
                        case 'w':
                            pushCharacter(constChar + 1);
                            break;
                        case 'k':
                            break;
                        case 'x':
                            break;
                        default:
                            hasget = false;
                            break;
                    }
                    break;
                case '[':
                {
                    // [ .* ]
                    // [ .*, [0-9]+ ]
                    ttstr ruby{};
                    while (true)
                    {
                        p += chLen;
                        chLen = utf8_char_len(p);
                        if (chLen == 0)
                        {
                            TVPThrowExceptionMessage(TJS_N("TextRenderBase::render() failed to "
                                                           "parse: expected character, found EOF"));
                        }
                        if (chLen == 1 && *p == ']')
                            break;
                        break;
                        ruby += ttstr(p, chLen);
                    }
                    // TODO: implement ruby
                    break;
                }
                case '#':
                {
                    // [ .* ]
                    // [ .*, [0-9]+ ]
                    RgbColor colour = 0x00000000;
                    bool isNull = true;
                    while (true)
                    {
                        p += chLen;
                        chLen = utf8_char_len(p);
                        if (chLen != 1)
                        {
                            TVPThrowExceptionMessage(TJS_N("TextRenderBase::render() failed to "
                                                           "parse: expected character, found EOF"));
                        }
                        if (*p == ';')
                            break;
                        isNull = false;
                        RgbColor c = 0;
                        if ('0' <= *p && *p <= '9')
                        {
                            c = static_cast<RgbColor>(*p - '0');
                        }
                        else if ('A' <= *p && *p <= 'F')
                        {
                            c = 0x0a + static_cast<RgbColor>(*p - 'A');
                        }
                        else if ('a' <= *p && *p <= 'f')
                        {
                            c = 0x0a + static_cast<RgbColor>(*p - 'a');
                        }
                        else
                        {
                            TVPThrowExceptionMessage(
                                TJS_N("TextRenderBase::render() failed to "
                                      "parse: expected hexadecimal number, found '%1'"),
                                *p);
                        }

                        colour = (colour << 4) | c;
                    }
                    if (isNull)
                        colour = 0xffffff;
                    m_state.chColor = colour;

                    break;
                }
                case '&':
                {
                    // '&' .+ ';'
                    ttstr graph{};
                    while (true)
                    {
                        p += chLen;
                        chLen = utf8_char_len(p);
                        if (chLen != 1)
                        {
                            TVPThrowExceptionMessage(TJS_N("TextRenderBase::render() failed to "
                                                           "parse: expected character, found EOF"));
                        }
                        if (chLen == 1 && *p == ';')
                            break;
                        graph += ttstr(p, chLen);
                    }
                    pushGraphicalCharacter(graph);
                    break;
                }
                case '$':
                {
                    // '&' .+ ';'
                    ttstr varName{};
                    while (true)
                    {
                        p += chLen;
                        chLen = utf8_char_len(p);
                        if (chLen != 1)
                        {
                            TVPThrowExceptionMessage(TJS_N("TextRenderBase::render() failed to "
                                                           "parse: expected character, found EOF"));
                        }
                        if (chLen == 1 && *p == ';')
                            break;
                        varName += ttstr(p, chLen);
                    }
                    // TODO: implement eval
                    break;
                }
                case '\n':
                {
                    flush();
                    performLinebreak();
                    break;
                }
                default:
                    hasget = false;
            }
            if (hasget)
                continue;
        }
        // TODO: character should include format options;
        //       as the font is lazy-evaluated/drawn
        //       (restrictions for line-breaking algorithm)
        pushCharacter(p);
        p += chLen - 1;
    }

    if (curr_y > m_boxHeight)
    {
        m_overflow = true;
        set_renderOver(true);
    }
    else
    {
        m_overflow = false;
        set_renderOver(false);
    }

    return !m_overflow;
}

void TextRenderBase::performLinebreak()
{
    auto rasterizer = GetCurrentRasterizer();
    if (m_state.align == kTextRenderAlignmentLeft)
        curr_x = m_boxLeft + m_indent;
    else if (m_state.align == kTextRenderAlignmentRight)
        curr_x = m_boxWidth - m_indent;
    else
        curr_x = m_boxLeft + m_indent;
    m_isBeginningOfLine = true;
    if (m_state.valign == kTextRenderAlignmentLeft)
        curr_y += rasterizer->GetAscentHeight() + m_state.lineSpacing;
    else if (m_state.valign == kTextRenderAlignmentRight)
        curr_y -= rasterizer->GetAscentHeight() + m_state.lineSpacing;
    else
        curr_y += rasterizer->GetAscentHeight() + m_state.lineSpacing;
}

void TextRenderBase::pushGraphicalCharacter(ttstr const& graph)
{
    // TODO: implement graphical characters
}

static bool findchInChars(const tjs_wchar* chz, const tjs_wchar ch)
{
    for (int i = 0; chz[i] != '\0'; i++)
    {
        if (chz[i] == ch)
        {
            return true;
        }
    }
    return false;
}

FontRasterizer* TextRenderBase::GetCurrentRasterizer()
{
    if (!m_rasterizer)
        m_rasterizer = new FreeTypeFontRasterizer();
    return m_rasterizer;
}

void TextRenderBase::pushCharacter(const tjs_char* ch)
{
    tjs_wchar chwd = 0;
    if (!TVP_utf8_to_utf16(ch, &chwd))
        return;
    auto isIndent = findchInChars(m_options.begin, chwd);
    auto isIndentDecr = findchInChars(m_options.end, chwd);

    auto rasterizer = GetCurrentRasterizer();
    auto text_height = rasterizer->GetAscentHeight();

    int advance_width = 0, advance_height = 0;

    rasterizer->GetTextExtent(chwd, advance_width, advance_height);

    CharacterInfo info{
        m_state.bold,
        m_state.italic,
        false,
        false,
        m_state.face,
        0,
        0,
        advance_width,
        m_state.fontsize,
        m_state.chColor,
        (m_state.edge ? m_state.edgeColor : 0),
        (m_state.shadow ? m_state.shadowColor : 0),
        (ttstr(ch, utf8_char_len(ch))),
    };

    m_buffer.push_back(std::move(info));

    if (m_autoIndent)
    {
        // pre-indent
        if (m_isBeginningOfLine && m_autoIndent < 0)
        {
            curr_x -= advance_width;
        }

        if (isIndent)
        {
            m_indent = curr_x + advance_width;
            // TODO: register pair
        }

        if (isIndentDecr && m_indent > 0)
        {
            flush(); // FIXME: not safe?
            m_indent = 0;
        }
    }

    m_isBeginningOfLine = false;
}
#include <SDL3/SDL.h>
void TextRenderBase::flush(bool force)
{
    if (m_buffer.empty())
    {
        return;
    }

    // try place all characters in the same line
    // 左对齐
    if (m_state.align == kTextRenderAlignmentLeft)
    {
        auto x = m_boxLeft;

        for (auto& ch : m_buffer)
        {
            auto advance_width = ch.cw;
            auto new_x = advance_width + x + m_state.pitch;

            if (m_boxWidth < new_x)
            {
                if (force)
                {
                    performLinebreak();
                    x = curr_x;
                    new_x = advance_width + x + m_state.pitch;
                }
                else
                {
                    flush(true);
                    return;
                }
            }

            ch.x = x;
            ch.y = curr_y;

            x = new_x;
        }

        curr_x = x;
    }
    // 右对齐
    else if (m_state.align == kTextRenderAlignmentRight)
    {
        auto x = m_boxLeft + m_boxWidth;

        for (auto& ch : m_buffer)
        {
            auto advance_width = ch.cw;
            auto new_x = x - advance_width;

            if (new_x < m_boxLeft)
            {
                if (force)
                {
                    performLinebreak();
                    new_x = curr_x - advance_width;
                }
                else
                {
                    flush(true);
                    return;
                }
            }

            ch.x = new_x;
            ch.y = curr_y;

            x = new_x - m_state.pitch;
        }

        curr_x = x;
    }
    // 居中对齐
    else
    {
        int totalWidth = 0;
        for (size_t i = 0; i < m_buffer.size(); i++)
        {
            totalWidth += m_buffer[i].cw;
            if (i < m_buffer.size() - 1)
            {
                totalWidth += m_state.pitch;
            }
        }
        if (totalWidth > m_boxWidth && !force)
        {
            performLinebreak();
            flush(true);
            return;
        }

        int startX = m_boxLeft + (m_boxWidth - totalWidth) / 2;
        int x = startX;
        for (size_t i = 0; i < m_buffer.size(); i++)
        {
            auto& ch = m_buffer[i];
            float advance_width = ch.cw;

            ch.x = x;
            ch.y = curr_y;

            x += advance_width;
            if (i < m_buffer.size() - 1)
            {
                x += m_state.pitch;
            }
        }

        curr_x = x;
    }
    
    m_characters.insert(m_characters.end(), m_buffer.begin(), m_buffer.end());
    for (size_t i = 0; i < m_buffer.size(); i++)
    {
        m_state.renderText += m_buffer.at(i).text;
    }
    m_buffer.clear();
}

void TextRenderBase::setRenderSize(int width, int height)
{
    m_boxWidth = width;
    m_boxHeight = height;
    clear();
}

void TextRenderBase::setDefault(tTJSVariant defaultSettings)
{
    m_default.deserialize(defaultSettings);
}

void TextRenderBase::setOption(tTJSVariant options)
{
    m_options.deserialize(options);
}

tTJSVariant TextRenderBase::getCharacters(int start, int end)
{
    // TODO: only (0, 0) is observed
    auto array = TJSCreateArrayObject();

    if ((end < start) || (start == 0 && end == 0))
    {
        for (size_t i = 0, cnt = m_characters.size(); i < cnt; ++i)
        {
            auto ch = m_characters[i].serialize();
            array->PropSetByNum(TJS_MEMBERENSURE, i, &ch, array);
        }
    }
    else
    {
        // TODO: unknown behaviour
    }

    return tTJSVariant(array, array);
}

void TextRenderBase::clear()
{
    m_characters.clear();

    m_state = m_default;
    m_overflow = false;

    curr_x = 0;
    // 提前检查y的对齐方式
    if (m_state.valign == kTextRenderAlignmentLeft)
    {
        // 正常搞
        curr_y = m_boxTop;
    }
    else if (m_state.valign == kTextRenderAlignmentRight)
    {
        // 从底部开始
        curr_y = m_boxTop + m_boxHeight - m_state.fontsize;
    }
    else
    {
        // 感觉为了高效实现对齐和实时渲染，以后可能得改整个textrender的流程
        // 不考虑多行了，现在的流程不好搞
        curr_y = m_boxTop + (m_boxHeight - GetCurrentRasterizer()->GetAscentHeight()) / 2;
    }
    m_indent = 0;

    m_isBeginningOfLine = true;

    updateFont();
}

void TextRenderBase::updateFont()
{
    auto rasterizer = GetCurrentRasterizer();
    auto font = tTVPFont{
        m_state.fontsize, // height of text
        static_cast<tjs_uint32>((m_state.bold ? TVP_TF_BOLD : 0) |
                                (m_state.italic ? TVP_TF_ITALIC : 0)),
        0,
        m_state.face, // TODO: this may fuck up the font settings by
                      // forcing the fallback font (in most cases)
    };
    rasterizer->ApplyFont(font);
}

void TextRenderBase::done()
{
    flush();
}

void TextRenderBase::resetFont()
{
    m_state = m_default;
}

void TextRenderBase::resetStyle()
{
    m_state = m_default;
}

tTJSVariant TextRenderBase::getKeyWait()
{
    iTJSDispatch2* dsp = TJSCreateArrayObject();
    tTJSVariant ret(dsp, dsp);
    dsp->Release();
    return ret;
}

int TextRenderBase::calcShowCount()
{
    return m_characters.size();
}

// register the class
NCB_REGISTER_CLASS(TextRenderBase)
{
    Constructor();

    NCB_METHOD(render);
    NCB_METHOD(setRenderSize);
    NCB_METHOD(setDefault);
    NCB_METHOD(setOption);
    NCB_METHOD(getCharacters);
    NCB_METHOD(clear);
    NCB_METHOD(done);

    NCB_METHOD(resetFont);
    NCB_METHOD(resetStyle);
    NCB_METHOD(getKeyWait);
    NCB_METHOD(calcShowCount);
    Property(TJS_N("keyWait"), &Class::getKeyWait, &Class::setKeyWait);
    property_delegate(renderCount);
    property_delegate(renderOver);
    property_delegate(renderDelay);
    property_delegate(renderText);
    property_delegate(renderLeft);
    property_delegate(renderRight);
    property_delegate(renderTop);
    property_delegate(renderBottom);

    property_delegate(vertical);
    property_delegate(bold);
    property_delegate(italic);
    property_delegate(face);
    property_delegate(fontSize);
    property_delegate(fontScale);
    property_delegate(chColor);
    property_delegate(rubySize);
    property_delegate(rubyOffset);
    property_delegate(shadow);
    property_delegate(edge);
    property_delegate(lineSpacing);
    property_delegate(pitch);
    property_delegate(lineSize);

    property_delegate(defaultBold);
    property_delegate(defaultItalic);
    property_delegate(defaultFace);
    property_delegate(defaultFontSize);
    property_delegate(defaultFontScale);
    property_delegate(defaultChColor);
    property_delegate(defaultRubySize);
    property_delegate(defaultRubyOffset);
    property_delegate(defaultShadow);
    property_delegate(defaultEdge);
    property_delegate(defaultLineSpacing);
    property_delegate(defaultPitch);
    property_delegate(defaultLineSize);
    property_delegate(defaultValign);
};
