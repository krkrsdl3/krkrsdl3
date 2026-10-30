#include "ncbind/ncbind.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "FreeTypeFontRasterizer.h"
#include "Platform.h"

#define NCB_MODULE_NAME TJS_N("textrender.dll")

using RgbColor = uint32_t;

extern bool Layer_FetchImageSize(ttstr imageName, int& w, int& h);

namespace
{
// ---------------------------------------------------------------------------
// small TJS helpers
// ---------------------------------------------------------------------------

tTJSVariant TakeObject(iTJSDispatch2* obj)
{
    tTJSVariant v(obj, obj);
    obj->Release();
    return v;
}

bool GetProp(iTJSDispatch2* obj, const char* name, tTJSVariant& value)
{
    return obj && TJS_SUCCEEDED(obj->PropGet(0, name, nullptr, &value, obj)) &&
           value.Type() != tvtVoid;
}

void SetProp(iTJSDispatch2* obj, const char* name, const tTJSVariant& value)
{
    obj->PropSet(TJS_MEMBERENSURE, name, nullptr, &value, obj);
}

void SetInt(iTJSDispatch2* obj, const char* name, tjs_int value)
{
    tTJSVariant v(value);
    SetProp(obj, name, v);
}

void SetReal(iTJSDispatch2* obj, const char* name, tjs_real value)
{
    tTJSVariant v(value);
    SetProp(obj, name, v);
}

void SetBool(iTJSDispatch2* obj, const char* name, bool value)
{
    SetInt(obj, name, value ? 1 : 0);
}

void SetString(iTJSDispatch2* obj, const char* name, const ttstr& value)
{
    tTJSVariant v(value);
    SetProp(obj, name, v);
}

void SetVoid(iTJSDispatch2* obj, const char* name)
{
    tTJSVariant v;
    SetProp(obj, name, v);
}

bool ReadInt(iTJSDispatch2* obj, const char* name, int& out)
{
    tTJSVariant v;
    if (!GetProp(obj, name, v))
        return false;
    out = static_cast<int>(v.AsInteger());
    return true;
}

bool ReadReal(iTJSDispatch2* obj, const char* name, tjs_real& out)
{
    tTJSVariant v;
    if (!GetProp(obj, name, v))
        return false;
    out = v.AsReal();
    return true;
}

bool ReadBool(iTJSDispatch2* obj, const char* name, bool& out)
{
    tTJSVariant v;
    if (!GetProp(obj, name, v))
        return false;
    out = v.AsInteger() != 0;
    return true;
}

bool ReadString(iTJSDispatch2* obj, const char* name, ttstr& out)
{
    tTJSVariant v;
    if (!GetProp(obj, name, v))
        return false;
    auto s = v.AsStringNoAddRef();
    if (s)
        out = ttstr(s);
    return true;
}

bool ReadColor(iTJSDispatch2* obj, const char* name, RgbColor& out)
{
    int value = 0;
    if (!ReadInt(obj, name, value))
        return false;
    out = static_cast<RgbColor>(static_cast<uint32_t>(value));
    return true;
}

bool ReadAlign(iTJSDispatch2* obj, const char* name, int& out)
{
    tTJSVariant v;
    if (!GetProp(obj, name, v))
        return false;
    if (v.Type() == tvtString)
    {
        std::string s = ttstr(v.AsStringNoAddRef()).AsStdString();
        if (s == "left" || s == "top")
            out = -1;
        else if (s == "center" || s == "middle")
            out = 0;
        else if (s == "right" || s == "bottom")
            out = 1;
        else
            out = std::atoi(s.c_str());
        return true;
    }
    out = static_cast<int>(v.AsInteger());
    return true;
}

// ---------------------------------------------------------------------------
// UTF-8 helpers
// ---------------------------------------------------------------------------

int Utf8CharLen(const char* p)
{
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c < 0x80)
        return 1;
    if ((c & 0xe0) == 0xc0)
        return 2;
    if ((c & 0xf0) == 0xe0)
        return 3;
    if ((c & 0xf8) == 0xf0)
        return 4;
    return 1;
}

std::string Utf8CharAt(const std::string& text, size_t pos)
{
    if (pos >= text.size())
        return std::string();
    int len = Utf8CharLen(text.c_str() + pos);
    if (pos + len > text.size())
        len = static_cast<int>(text.size() - pos);
    return text.substr(pos, len);
}

size_t Utf8Next(const std::string& text, size_t pos)
{
    return pos + Utf8CharLen(text.c_str() + pos);
}

bool Utf8ToWChar(const std::string& ch, tjs_wchar& out)
{
    return TVP_utf8_to_utf16(ch.c_str(), &out);
}

bool IsAsciiWordChar(const std::string& ch)
{
    return ch.size() == 1 && ((ch[0] >= '0' && ch[0] <= '9') ||
                              (ch[0] >= 'A' && ch[0] <= 'Z') ||
                              (ch[0] >= 'a' && ch[0] <= 'z') || ch[0] == '_' ||
                              ch[0] == '-');
}

bool IsSpaceChar(const std::string& ch)
{
    return ch == " " || ch == "\t" || ch == "\u3000";
}

// ---------------------------------------------------------------------------
// style / options / character
// ---------------------------------------------------------------------------

struct TextStyle
{
    ttstr face = TJS_N("user");
    bool bold = false;
    bool italic = false;
    int fontsize = 24;
    int bigfontsize = 48;
    int smallfontsize = 12;
    int rubysize = 10;
    int rubyoffset = -2;
    RgbColor color = 0x00ffffff;
    bool shadow = true;
    RgbColor shadowcolor = 0xff000000;
    int shadowdiff = 0;
    bool edge = false;
    RgbColor edgecolor = 0xff0080ff;
    int linespacing = 6;
    int pitch = 0;
    int linesize = 0;
    int align = -1;
    int valign = -1;
};

struct RenderOptions
{
    ttstr following = TJS_N(
        "%),:;]}｡｣ﾞﾟ。，、．：；゛゜ヽヾゝゞ々’”）〕］｝〉》」』】°′″℃￠％‰　!.?､･ｧｨｩｪｫｬｭｮｯｰ・？！ーぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ");
    ttstr leading = TJS_N("\\$([{｢‘“（〔［｛〈《「『【￥＄￡");
    ttstr begin = TJS_N("「『（‘“〔［｛〈《");
    ttstr end = TJS_N("」』）’”〕］｝〉》");

    bool vertical = false;
    int kinsoku_max = 0;
    int word_break = 0;
    tjs_real width_time_scale = 0;

    bool ignore_color = false;
    bool ignore_size = false;
    bool ignore_delay = false;
    bool ignore_over = false;
    bool ignore_overy = false;
    bool ignore_overx = false;
    bool ignore_ruby = false;
    bool ignore_type = false;
    bool ignore_face = false;
    bool ignore_style = false;
    bool ignore_xr = false;
};

struct RubyPart
{
    ttstr text;
    tjs_real x = 0;
    tjs_real y = 0;
    tjs_real size = 0;
};

struct Character
{
    bool graph = false;
    bool vertical = false;
    bool bold = false;
    bool italic = false;
    ttstr face = TJS_N("user");
    ttstr text;

    tjs_real x = 0;
    tjs_real y = 0;
    tjs_real cw = 0;   // advance along the writing direction
    tjs_real size = 0; // font height (width in vertical mode)

    RgbColor color = 0x00ffffff;
    bool shadow = false;
    RgbColor shadowcolor = 0x000000;
    int shadowdiff = 0;
    bool edge = false;
    RgbColor edgecolor = 0x000000;

    tjs_real delay = 0;
    tjs_real time = 0;

    int link = -1;
    ttstr linkName;

    std::vector<RubyPart> ruby;

    tjs_real pitch_after = 0;
    int line_spacing = 0;
};

struct KeyWait
{
    int pos = 0;
    tjs_real time = 0;
    ttstr label;
};

struct TextLine
{
    size_t start = 0;
    size_t end = 0;
    tjs_real pos = 0; // y (horizontal) / right edge x (vertical), relative to box
    tjs_real advance = 0;
    bool open = true;
};

struct LinkRect
{
    tjs_real left = 0;
    tjs_real top = 0;
    tjs_real width = 0;
    tjs_real height = 0;
};

// ---------------------------------------------------------------------------
// serialization helpers
// ---------------------------------------------------------------------------

tTJSVariant SerializeCharacter(const Character& ch, tjs_real dx = 0, tjs_real dy = 0)
{
    iTJSDispatch2* dict = TJSCreateDictionaryObject();
    if (!dict)
        return tTJSVariant();

    SetBool(dict, "graph", ch.graph);
    SetBool(dict, "vertical", ch.vertical);
    SetReal(dict, "x", ch.x + dx);
    SetReal(dict, "y", ch.y + dy);
    SetReal(dict, "cw", ch.cw);
    SetReal(dict, "size", ch.size);
    SetString(dict, "face", ch.face);
    SetInt(dict, "color", static_cast<tjs_int>(ch.color));
    SetBool(dict, "bold", ch.bold);
    SetBool(dict, "italic", ch.italic);
    SetBool(dict, "shadow", ch.shadow);
    SetInt(dict, "shadowColor", static_cast<tjs_int>(ch.shadowcolor));
    SetInt(dict, "shadowDiff", ch.shadowdiff);
    SetBool(dict, "edge", ch.edge);
    SetInt(dict, "edgeColor", static_cast<tjs_int>(ch.edgecolor));
    SetString(dict, "text", ch.text);
    SetReal(dict, "delay", ch.delay);
    SetInt(dict, "link", ch.link);
    if (ch.link >= 0)
        SetString(dict, "linkName", ch.linkName);
    else
        SetVoid(dict, "linkName");

    if (!ch.ruby.empty())
    {
        iTJSDispatch2* rubyArray = TJSCreateArrayObject();
        if (rubyArray)
        {
            for (size_t i = 0; i < ch.ruby.size(); ++i)
            {
                const RubyPart& r = ch.ruby[i];
                iTJSDispatch2* rd = TJSCreateDictionaryObject();
                if (!rd)
                    continue;
                SetReal(rd, "x", r.x);
                SetReal(rd, "y", r.y);
                SetReal(rd, "size", r.size);
                SetString(rd, "text", r.text);
                SetString(rd, "face", ch.face);
                SetBool(rd, "vertical", ch.vertical);
                SetBool(rd, "bold", ch.bold);
                SetBool(rd, "italic", ch.italic);
                SetInt(rd, "color", static_cast<tjs_int>(ch.color));
                SetBool(rd, "shadow", ch.shadow);
                SetInt(rd, "shadowColor", static_cast<tjs_int>(ch.shadowcolor));
                SetBool(rd, "edge", ch.edge);
                SetInt(rd, "edgeColor", static_cast<tjs_int>(ch.edgecolor));
                tTJSVariant rv = TakeObject(rd);
                rubyArray->PropSetByNum(TJS_MEMBERENSURE, i, &rv, rubyArray);
            }
            tTJSVariant rv = TakeObject(rubyArray);
            SetProp(dict, "ruby", rv);
        }
    }

    return TakeObject(dict);
}

} // namespace

// ---------------------------------------------------------------------------
// property helpers
// ---------------------------------------------------------------------------

#define property_accessor(name, type, storage) \
    type get_##name() const { return storage; } \
    void set_##name(type v) { storage = v; }
#define property_accessor_cast(name, type, cast, storage) \
    cast get_##name() const { return cast(storage); } \
    void set_##name(cast v) { storage = type(v); }
#define property_accessor_string(name, storage) \
    tTJSVariant get_##name() const { return tTJSVariant(storage); } \
    void set_##name(tTJSVariant v) { \
        auto s = v.AsStringNoAddRef(); \
        if (s) storage = ttstr(s); \
    }

// ---------------------------------------------------------------------------
// TextRenderBase
// ---------------------------------------------------------------------------

class TextRenderBase
{
public:
    TextRenderBase();
    virtual ~TextRenderBase();

    // ---------------------------------------------------------------- methods
    void setOption(tTJSVariant options);
    void setDefault(tTJSVariant defaults);
    void setFont(tTJSVariant font);
    void setStyle(tTJSVariant style);
    void setRenderSize(tjs_real width, tjs_real height);
    void clear();
    void resetFont();
    void resetStyle();
    void newline();
    void done();
    bool contains(tjs_real x, tjs_real y) const;
    tjs_real calcLineOffset(int line) const;
    int calcShowCount(tjs_real elapsed) const;
    tTJSVariant getKeyWait() const;
    tTJSVariant getCharacters(int start, int end) const;
    tTJSVariant getLinkNames() const;
    tTJSVariant getLinkRects(int index) const;
    tTJSVariant getLinkCharacters(int index) const;
    bool isLinkContains(int index, tjs_real x, tjs_real y) const;
    int getLinkOfPosition(tjs_real x, tjs_real y) const;

    tTJSVariant get_renderText() const { return tTJSVariant(m_renderText); }
    bool get_renderOver() const { return m_renderOver; }
    int get_renderLines() const { return static_cast<int>(m_lines.size()); }
    int get_renderCount() const { return static_cast<int>(m_chars.size()); }
    tjs_real get_renderDelay() const { return m_renderDelay; }
    tjs_real get_renderLeft() const { return m_renderLeft; }
    tjs_real get_renderTop() const { return m_renderTop; }
    tjs_real get_renderRight() const { return m_renderRight; }
    tjs_real get_renderBottom() const { return m_renderBottom; }
    int get_maxScrollOffset() const { return m_maxScrollOffset; }
    int get_maxScrollLine() const { return m_maxScrollLine; }

    // ------------------------------------------------------------- properties
    bool get_vertical() const { return m_vertical; }
    void set_vertical(bool v)
    {
        m_vertical = v;
        m_options.vertical = v;
    }
    property_accessor(timeScale, tjs_real, m_timeScale);
    property_accessor(fontScale, tjs_real, m_fontScale);

    property_accessor_string(defaultFace, m_default.face);
    property_accessor(defaultFontSize, int, m_default.fontsize);
    property_accessor(defaultBigFontSize, int, m_default.bigfontsize);
    property_accessor(defaultSmallFontSize, int, m_default.smallfontsize);
    property_accessor(defaultLineSize, int, m_default.linesize);
    property_accessor(defaultLineSpacing, int, m_default.linespacing);
    property_accessor(defaultPitch, int, m_default.pitch);
    property_accessor(defaultAlign, int, m_default.align);
    property_accessor(defaultValign, int, m_default.valign);
    property_accessor(defaultRubySize, int, m_default.rubysize);
    property_accessor(defaultRubyOffset, int, m_default.rubyoffset);
    property_accessor_cast(defaultChColor, RgbColor, tjs_int, m_default.color);
    property_accessor(defaultShadow, bool, m_default.shadow);
    property_accessor_cast(defaultShadowColor, RgbColor, tjs_int, m_default.shadowcolor);
    property_accessor(defaultShadowDiff, int, m_default.shadowdiff);
    property_accessor(defaultEdge, bool, m_default.edge);
    property_accessor_cast(defaultEdgeColor, RgbColor, tjs_int, m_default.edgecolor);
    property_accessor(defaultBold, bool, m_default.bold);
    property_accessor(defaultItalic, bool, m_default.italic);

    // --------------------------------------------------------------- callback
    static tjs_error cb_render(tTJSVariant* result,
                               tjs_int numparams,
                               tTJSVariant** param,
                               iTJSDispatch2* objthis);

private:
    int m_boxWidth = 0;
    int m_boxHeight = 0;
    tjs_real m_boxLeft = 0;
    tjs_real m_boxTop = 0;
    bool m_vertical = false;

    TextStyle m_default{};
    TextStyle m_state{};
    RenderOptions m_options{};

    std::vector<Character> m_chars;
    std::vector<TextLine> m_lines;
    size_t m_openLine = static_cast<size_t>(-1);
    tjs_real m_cursor = 0; // line cursor along the secondary axis

    int m_indent = 0;
    int m_autoIndent = 0;
    bool m_isBeginningOfLine = true;

    std::vector<KeyWait> m_keyWaits;
    std::vector<ttstr> m_linkNames;
    int m_currentLink = -1;

    ttstr m_renderText;

    tjs_real m_elapsed = 0;
    tjs_real m_perCharDelay = 0;
    tjs_real m_renderDelay = 0;
    tjs_real m_timeScale = 1.0;
    tjs_real m_fontScale = 1.0;

    tjs_real m_renderLeft = 0;
    tjs_real m_renderTop = 0;
    tjs_real m_renderRight = 0;
    tjs_real m_renderBottom = 0;
    int m_maxScrollOffset = 0;
    int m_maxScrollLine = 0;

    bool m_overflow = false;
    bool m_renderOver = false;
    tjs_real m_offsetX = 0;
    tjs_real m_offsetY = 0;

    std::optional<ttstr> m_pendingRuby;
    int m_pendingRubyCount = 0;
    bool m_pendingRubyAttached = false;

    iTJSDispatch2* m_owner = nullptr;
    bool m_inFontNotify = false;
    bool m_fontNotifyDirty = true;
    std::string m_lastFontNotify;

    mutable FontRasterizer* m_rasterizer = nullptr;
    mutable std::optional<tTVPFont> m_rasterizerFont;

    // parsing
    bool RenderText(const std::string& text, int autoIndent, int diff, bool all, bool same);
    void ParsePercent(const std::string& text, size_t& i);
    void ParseRuby(const std::string& text, size_t& i);
    void ParseColor(const std::string& text, size_t& i, RgbColor& color, bool& specified);
    void ParseEval(const std::string& text, size_t& i);
    void ParseGraph(const std::string& text, size_t& i);
    int ReadInteger(const std::string& text, size_t& i, int fallback = 0);

    // style
    void ApplyDefaultDict(iTJSDispatch2* dict);
    void ApplyFontDict(iTJSDispatch2* dict);
    void ApplyStyleDict(iTJSDispatch2* dict);
    void ApplyOptionDict(iTJSDispatch2* dict);
    void ResetFontState();
    void ResetStyleState();
    tjs_real ScaledFontSize(int size) const;
    tjs_real CurrentFontSize() const { return ScaledFontSize(m_state.fontsize); }
    void NotifyFontChange();
    void UpdateRasterizerFont();
    tjs_int AscentForSize(tjs_real size) const;

    // metrics / callbacks
    bool CallCallback(const char* name,
                      const std::vector<tTJSVariant>& args,
                      tTJSVariant& result) const;
    tjs_real MeasureWidth(const std::string& text, tjs_real size);
    tjs_real MeasureHeight(const std::string& text, tjs_real size);
    bool MeasureGraph(const std::string& name, tjs_real& width, tjs_real& height);
    ttstr EvalExpression(const std::string& exp) const;
    tjs_real LabelDelay(const std::string& label) const;

    // timing
    tjs_real DelayForCharacter(const Character& ch) const;
    void AddKeyWait(const ttstr& label, tjs_real wait);

    // characters
    void PushCharacter(const std::string& ch);
    void PushGraphCharacter(const std::string& name);
    void PushRawString(const std::string& text);
    void PushRubyToLast(const ttstr& ruby, int count);

    // line layout
    void EnsureOpenLine();
    void LayoutLine(size_t lineIndex);
    void LayoutOpenLine();
    void FinishLine();
    void SplitLine(size_t breakIndex);
    bool WrapIfNeeded();
    tjs_real LineWidth(size_t lineIndex) const;
    tjs_real LineAdvance(size_t lineIndex) const;
    tjs_real DefaultLineAdvance() const;
    size_t FindBreakIndex(size_t lineIndex) const;
    std::string CharAt(size_t index) const;
    bool IsCharIn(const ttstr& set, const std::string& ch) const;

    // finalization
    void FinalizeLayout();
    void UpdateBounds();
    void UpdateLinks();
    void ApplyVerticalAlignment();
    void RebuildLinkRects();

    std::vector<std::vector<LinkRect>> m_linkRects;
};

// ---------------------------------------------------------------------------

TextRenderBase::TextRenderBase() {}

TextRenderBase::~TextRenderBase()
{
    if (m_rasterizer)
        m_rasterizer->Release();
}

// ---------------------------------------------------------------------------
// TJS class registration helpers live near the end of the file.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// dict -> state
// ---------------------------------------------------------------------------

void TextRenderBase::ApplyDefaultDict(iTJSDispatch2* dict)
{
    if (!dict)
        return;

    ReadString(dict, "face", m_default.face);
    ReadBool(dict, "bold", m_default.bold);
    ReadBool(dict, "italic", m_default.italic);
    ReadInt(dict, "fontsize", m_default.fontsize);
    ReadInt(dict, "bigfontsize", m_default.bigfontsize);
    ReadInt(dict, "smallfontsize", m_default.smallfontsize);
    ReadInt(dict, "rubysize", m_default.rubysize);
    ReadInt(dict, "rubyoffset", m_default.rubyoffset);
    ReadColor(dict, "color", m_default.color);
    ReadBool(dict, "shadow", m_default.shadow);
    ReadColor(dict, "shadowcolor", m_default.shadowcolor);
    ReadInt(dict, "shadowdiff", m_default.shadowdiff);
    ReadBool(dict, "edge", m_default.edge);
    ReadColor(dict, "edgecolor", m_default.edgecolor);
    ReadInt(dict, "linespacing", m_default.linespacing);
    ReadInt(dict, "pitch", m_default.pitch);
    ReadInt(dict, "linesize", m_default.linesize);
    ReadAlign(dict, "align", m_default.align);
    ReadAlign(dict, "valign", m_default.valign);
}

static bool ResolveBool(iTJSDispatch2* dict, const char* name, bool current, bool& out)
{
    tTJSVariant v;
    if (!GetProp(dict, name, v))
        return false;
    if (v.Type() == tvtString)
    {
        auto s = ttstr(v.AsStringNoAddRef()).AsStdString();
        if (s == "default")
        {
            out = current;
            return true;
        }
    }
    out = v.AsInteger() != 0;
    return true;
}

void TextRenderBase::ApplyFontDict(iTJSDispatch2* dict)
{
    if (!dict)
        return;

    ttstr face;
    if (ReadString(dict, "face", face))
    {
        if (!m_options.ignore_face)
        {
            if (face.IsEmpty() || face == TJS_N("default"))
                m_state.face = m_default.face;
            else
                m_state.face = face;
        }
    }

    ResolveBool(dict, "bold", m_default.bold, m_state.bold);
    ResolveBool(dict, "italic", m_default.italic, m_state.italic);
    ResolveBool(dict, "shadow", m_default.shadow, m_state.shadow);
    ResolveBool(dict, "edge", m_default.edge, m_state.edge);

    if (!m_options.ignore_size)
    {
        tTJSVariant v;
        if (GetProp(dict, "size", v))
        {
            int size = m_state.fontsize;
            if (v.Type() == tvtString)
            {
                auto s = ttstr(v.AsStringNoAddRef()).AsStdString();
                if (s.empty() || s == "default")
                    size = m_default.fontsize;
                else if (s == "big")
                    size = m_default.bigfontsize;
                else if (s == "small")
                    size = m_default.smallfontsize;
                else if (s[0] == 'x')
                    size = static_cast<int>(m_default.fontsize * std::atof(s.c_str() + 1));
                else
                    size = static_cast<int>(std::atof(s.c_str()));
            }
            else
            {
                size = static_cast<int>(v.AsInteger());
            }
            if (size > 0)
                m_state.fontsize = size;
        }
        else
        {
            int size = 0;
            if (ReadInt(dict, "fontsize", size) && size > 0)
                m_state.fontsize = size;
        }
    }

    if (!m_options.ignore_color)
    {
        RgbColor color;
        if (ReadColor(dict, "color", color))
            m_state.color = color;
    }
    if (!m_options.ignore_ruby)
    {
        ReadInt(dict, "rubysize", m_state.rubysize);
        ReadInt(dict, "rubyoffset", m_state.rubyoffset);
    }
    {
        RgbColor color;
        if (ReadColor(dict, "shadowcolor", color))
            m_state.shadowcolor = color;
        if (ReadColor(dict, "edgecolor", color))
            m_state.edgecolor = color;
    }
    ReadInt(dict, "shadowdiff", m_state.shadowdiff);

    UpdateRasterizerFont();
    NotifyFontChange();
}

void TextRenderBase::ApplyStyleDict(iTJSDispatch2* dict)
{
    if (!dict)
        return;
    ReadInt(dict, "linespacing", m_state.linespacing);
    ReadInt(dict, "pitch", m_state.pitch);
    ReadInt(dict, "linesize", m_state.linesize);
    ReadAlign(dict, "align", m_state.align);
    ReadAlign(dict, "valign", m_state.valign);
}

void TextRenderBase::ApplyOptionDict(iTJSDispatch2* dict)
{
    if (!dict)
        return;

    ReadString(dict, "following", m_options.following);
    ReadString(dict, "leading", m_options.leading);
    ReadString(dict, "begin", m_options.begin);
    ReadString(dict, "end", m_options.end);

    bool vertical = m_options.vertical;
    if (ReadBool(dict, "vertical", vertical))
    {
        m_options.vertical = vertical;
        m_vertical = vertical;
    }

    ReadInt(dict, "kinsoku_max", m_options.kinsoku_max);
    ReadInt(dict, "word_break", m_options.word_break);
    ReadReal(dict, "width_time_scale", m_options.width_time_scale);

    ReadBool(dict, "ignore_color", m_options.ignore_color);
    ReadBool(dict, "ignore_size", m_options.ignore_size);
    ReadBool(dict, "ignore_delay", m_options.ignore_delay);
    ReadBool(dict, "ignore_over", m_options.ignore_over);
    ReadBool(dict, "ignore_overy", m_options.ignore_overy);
    ReadBool(dict, "ignore_overx", m_options.ignore_overx);
    ReadBool(dict, "ignore_ruby", m_options.ignore_ruby);
    ReadBool(dict, "ignore_type", m_options.ignore_type);
    ReadBool(dict, "ignore_face", m_options.ignore_face);
    ReadBool(dict, "ignore_style", m_options.ignore_style);
    ReadBool(dict, "ignore_xr", m_options.ignore_xr);
}

void TextRenderBase::setOption(tTJSVariant options)
{
    auto dict = options.AsObjectNoAddRef();
    if (dict)
        ApplyOptionDict(dict);
}

void TextRenderBase::setDefault(tTJSVariant defaults)
{
    auto dict = defaults.AsObjectNoAddRef();
    if (dict)
        ApplyDefaultDict(dict);
}

void TextRenderBase::setFont(tTJSVariant font)
{
    auto dict = font.AsObjectNoAddRef();
    if (dict)
        ApplyFontDict(dict);
}

void TextRenderBase::setStyle(tTJSVariant style)
{
    auto dict = style.AsObjectNoAddRef();
    if (dict)
        ApplyStyleDict(dict);
}

// ---------------------------------------------------------------------------
// style helpers
// ---------------------------------------------------------------------------

tjs_real TextRenderBase::ScaledFontSize(int size) const
{
    tjs_real scale = m_fontScale;
    if (scale <= 0)
        scale = 1.0;
    return size * scale;
}

void TextRenderBase::ResetFontState()
{
    m_state.face = m_default.face;
    m_state.bold = m_default.bold;
    m_state.italic = m_default.italic;
    m_state.fontsize = m_default.fontsize;
    m_state.bigfontsize = m_default.bigfontsize;
    m_state.smallfontsize = m_default.smallfontsize;
    m_state.rubysize = m_default.rubysize;
    m_state.rubyoffset = m_default.rubyoffset;
    m_state.color = m_default.color;
    m_state.shadow = m_default.shadow;
    m_state.shadowcolor = m_default.shadowcolor;
    m_state.shadowdiff = m_default.shadowdiff;
    m_state.edge = m_default.edge;
    m_state.edgecolor = m_default.edgecolor;
    UpdateRasterizerFont();
}

void TextRenderBase::ResetStyleState()
{
    m_state.linespacing = m_default.linespacing;
    m_state.pitch = m_default.pitch;
    m_state.linesize = m_default.linesize;
    m_state.align = m_default.align;
    m_state.valign = m_default.valign;
}

void TextRenderBase::resetFont()
{
    ResetFontState();
    NotifyFontChange();
}

void TextRenderBase::resetStyle()
{
    ResetStyleState();
}

void TextRenderBase::NotifyFontChange()
{
    if (!m_owner || m_inFontNotify)
        return;

    // Only notify when the effective font actually changed.  Games use this
    // callback to update Layer.font; firing it for every character/message
    // forces them to redraw the whole text block.
    std::string sig = m_state.face.AsStdString();
    sig += '|';
    sig += std::to_string(static_cast<long long>(CurrentFontSize()));
    sig += m_state.bold ? "b1" : "b0";
    sig += m_state.italic ? "i1" : "i0";
    sig += m_vertical ? "v1" : "v0";
    sig += '|';
    sig += std::to_string(static_cast<long long>(m_state.color));
    sig += m_state.shadow ? "s1" : "s0";
    sig += std::to_string(static_cast<long long>(m_state.shadowcolor));
    sig += ':';
    sig += std::to_string(m_state.shadowdiff);
    sig += m_state.edge ? "e1" : "e0";
    sig += std::to_string(static_cast<long long>(m_state.edgecolor));
    if (!m_fontNotifyDirty && sig == m_lastFontNotify)
        return;

    iTJSDispatch2* dict = TJSCreateDictionaryObject();
    if (!dict)
        return;

    SetString(dict, "face", m_state.face);
    SetReal(dict, "size", CurrentFontSize());
    SetBool(dict, "bold", m_state.bold);
    SetBool(dict, "italic", m_state.italic);
    SetBool(dict, "vertical", m_vertical);
    SetInt(dict, "color", static_cast<tjs_int>(m_state.color));
    SetBool(dict, "shadow", m_state.shadow);
    SetInt(dict, "shadowColor", static_cast<tjs_int>(m_state.shadowcolor));
    SetInt(dict, "shadowDiff", m_state.shadowdiff);
    SetBool(dict, "edge", m_state.edge);
    SetInt(dict, "edgeColor", static_cast<tjs_int>(m_state.edgecolor));

    tTJSVariant arg = TakeObject(dict);
    std::vector<tTJSVariant> args{arg};
    tTJSVariant ignored;
    m_inFontNotify = true;
    bool called = CallCallback("onFontChange", args, ignored);
    m_inFontNotify = false;
    if (called)
    {
        m_lastFontNotify = sig;
        m_fontNotifyDirty = false;
    }
}

// ---------------------------------------------------------------------------
// callbacks / metrics
// ---------------------------------------------------------------------------

bool TextRenderBase::CallCallback(const char* name,
                                  const std::vector<tTJSVariant>& args,
                                  tTJSVariant& result) const
{
    if (!m_owner)
        return false;

    tTJSVariant cb;
    if (!GetProp(m_owner, name, cb) || cb.Type() != tvtObject)
        return false;

    tTJSVariantClosure fn = cb.AsObjectClosureNoAddRef();
    if (!fn.Object)
        return false;

    std::vector<tTJSVariant*> params;
    params.reserve(args.size());
    for (const tTJSVariant& v : args)
        params.push_back(const_cast<tTJSVariant*>(&v));

    result.Clear();
    return TJS_SUCCEEDED(fn.FuncCall(0, nullptr, nullptr, &result,
                                     static_cast<tjs_int>(params.size()), params.data(),
                                     m_owner));
}

ttstr TextRenderBase::EvalExpression(const std::string& exp) const
{
    tTJSVariant arg(ttstr(exp.c_str()));
    std::vector<tTJSVariant> args{arg};
    tTJSVariant result;
    if (CallCallback("onEval", args, result))
    {
        auto s = result.AsStringNoAddRef();
        if (s)
        {
            ttstr text(s);
            return text.IsEmpty() ? ttstr(TJS_N(" ")) : text;
        }
    }
    return ttstr();
}

tjs_real TextRenderBase::LabelDelay(const std::string& label) const
{
    tTJSVariant arg(ttstr(label.c_str()));
    std::vector<tTJSVariant> args{arg};
    tTJSVariant result;
    if (CallCallback("onLabel", args, result))
        return result.AsReal();
    return 0;
}

void TextRenderBase::UpdateRasterizerFont()
{
    if (!m_rasterizer)
        return;
    AscentForSize(CurrentFontSize());
}

tjs_int TextRenderBase::AscentForSize(tjs_real size) const
{
    if (!m_rasterizer)
        m_rasterizer = new FreeTypeFontRasterizer();

    tTVPFont font;
    font.Height = static_cast<tjs_int>(std::max<tjs_real>(1, size));
    font.Flags = static_cast<tjs_uint32>((m_state.bold ? TVP_TF_BOLD : 0) |
                                         (m_state.italic ? TVP_TF_ITALIC : 0));
    font.Angle = 0;
    font.Face = m_state.face;
    if (!m_rasterizerFont || *m_rasterizerFont != font)
    {
        m_rasterizer->ApplyFont(font);
        m_rasterizerFont = font;
    }
    return m_rasterizer->GetAscentHeight();
}

tjs_real TextRenderBase::MeasureWidth(const std::string& text, tjs_real size)
{
    tTJSVariant argText(ttstr(text.c_str()));
    tTJSVariant argSize(size);
    std::vector<tTJSVariant> args{argText, argSize};
    tTJSVariant result;
    if (CallCallback("onGetTextWidth", args, result))
    {
        tjs_real width = result.AsReal();
        if (width > 0)
            return width;
    }

    if (!m_rasterizer)
        m_rasterizer = new FreeTypeFontRasterizer();
    UpdateRasterizerFont();

    tjs_real width = 0;
    size_t pos = 0;
    while (pos < text.size())
    {
        std::string ch = Utf8CharAt(text, pos);
        pos += ch.size();
        tjs_wchar wch;
        if (!Utf8ToWChar(ch, wch))
            continue;
        tjs_int w = 0, h = 0;
        m_rasterizer->GetTextExtent(wch, w, h);
        width += w;
    }
    return width > 0 ? width : size;
}

tjs_real TextRenderBase::MeasureHeight(const std::string& text, tjs_real size)
{
    tTJSVariant argText(ttstr(text.c_str()));
    tTJSVariant argSize(size);
    std::vector<tTJSVariant> args{argText, argSize};
    tTJSVariant result;
    if (CallCallback("onGetTextHeight", args, result))
    {
        tjs_real height = result.AsReal();
        if (height > 0)
            return height;
    }
    return size;
}

bool TextRenderBase::MeasureGraph(const std::string& name, tjs_real& width, tjs_real& height)
{
    tTJSVariant arg(ttstr(name.c_str()));
    std::vector<tTJSVariant> args{arg};
    tTJSVariant result;
    if (CallCallback("onGetGraphSize", args, result) && result.Type() == tvtObject)
    {
        auto dict = result.AsObjectNoAddRef();
        tjs_real w = 0, h = 0;
        ReadReal(dict, "width", w);
        ReadReal(dict, "height", h);
        width = w;
        height = h;
        return true;
    }

    int w = 0, h = 0;
    if (Layer_FetchImageSize(ttstr(name.c_str()), w, h))
    {
        width = w;
        height = h;
        return true;
    }
    width = height = 0;
    return false;
}

// ---------------------------------------------------------------------------
// parser helpers
// ---------------------------------------------------------------------------

int TextRenderBase::ReadInteger(const std::string& text, size_t& i, int fallback)
{
    int value = 0;
    bool negative = false;
    bool any = false;
    while (i < text.size())
    {
        char c = text[i];
        if (c >= '0' && c <= '9')
        {
            value = value * 10 + (c - '0');
            any = true;
            ++i;
        }
        else if (c == '-')
        {
            negative = !negative;
            ++i;
        }
        else if (c == ';')
        {
            ++i;
            if (!any)
                return fallback;
            return negative ? -value : value;
        }
        else
            break;
    }
    if (!any)
        return fallback;
    return negative ? -value : value;
}

bool TextRenderBase::IsCharIn(const ttstr& set, const std::string& ch) const
{
    if (ch.empty())
        return false;
    std::string s = set.AsStdString();
    return s.find(ch) != std::string::npos;
}

std::string TextRenderBase::CharAt(size_t index) const
{
    if (index >= m_chars.size())
        return std::string();
    return m_chars[index].text.AsStdString();
}

// ---------------------------------------------------------------------------
// character creation
// ---------------------------------------------------------------------------

tjs_real TextRenderBase::DelayForCharacter(const Character& ch) const
{
    if (m_options.ignore_delay || m_options.ignore_type)
        return 0;
    tjs_real delay = m_perCharDelay * m_timeScale;
    if (m_options.width_time_scale > 0 && CurrentFontSize() > 0)
    {
        tjs_real factor = ch.cw / CurrentFontSize();
        if (factor > 1.0)
            delay *= 1.0 + m_options.width_time_scale * (factor - 1.0);
    }
    return std::max<tjs_real>(0, delay);
}

void TextRenderBase::AddKeyWait(const ttstr& label, tjs_real wait)
{
    KeyWait kw;
    kw.pos = static_cast<int>(m_chars.size());
    kw.time = m_elapsed;
    kw.label = label;
    m_keyWaits.push_back(kw);
    if (wait > 0)
        m_elapsed += wait * m_timeScale;
}

void TextRenderBase::PushRubyToLast(const ttstr& rubyText, int count)
{
    if (m_options.ignore_ruby || rubyText.IsEmpty() || count <= 0 || m_chars.empty())
        return;

    // Attach the ruby to the first character of the span.  The TJS side
    // positions it relative to that character.
    Character& first = m_chars[m_chars.size() - std::min<size_t>(count, m_chars.size())];
    RubyPart part;
    part.text = rubyText;
    part.size = ScaledFontSize(m_state.rubysize);
    part.x = 0;
    part.y = 0;

    if (m_vertical)
    {
        part.x = first.size + m_state.rubyoffset;
        part.y = 0;
    }
    else
    {
        part.x = 0;
        part.y = -(part.size) - m_state.rubyoffset;
    }
    first.ruby.push_back(part);
}

void TextRenderBase::PushCharacter(const std::string& ch)
{
    if (ch.empty())
        return;

    tjs_real size = CurrentFontSize();
    tjs_real advance = 0;
    tjs_real height = 0;

    if (m_vertical)
    {
        advance = MeasureHeight(ch, size);
        height = MeasureWidth(ch, size);
    }
    else
    {
        advance = MeasureWidth(ch, size);
        height = MeasureHeight(ch, size);
    }
    if (advance <= 0)
        advance = size;
    if (height <= 0)
        height = size;

    Character c;
    c.vertical = m_vertical;
    c.bold = m_state.bold;
    c.italic = m_state.italic;
    c.face = m_state.face;
    c.text = ttstr(ch.c_str());
    c.cw = advance;
    c.size = height;
    c.color = m_state.color;
    c.shadow = m_state.shadow;
    c.shadowcolor = m_state.shadowcolor;
    c.shadowdiff = m_state.shadowdiff;
    c.edge = m_state.edge;
    c.edgecolor = m_state.edgecolor;
    c.delay = m_elapsed;
    c.time = m_elapsed;
    c.link = m_currentLink;
    if (m_currentLink >= 0 && m_currentLink < static_cast<int>(m_linkNames.size()))
        c.linkName = m_linkNames[m_currentLink];
    c.pitch_after = m_state.pitch;
    c.line_spacing = m_state.linespacing;

    m_renderText += ttstr(ch.c_str());

    EnsureOpenLine();
    m_chars.push_back(c);
    m_elapsed += DelayForCharacter(c);

    m_lines[m_openLine].end = m_chars.size();
    LayoutOpenLine();

    if (m_autoIndent > 0 && !m_options.ignore_style)
    {
        if (IsCharIn(m_options.begin, ch))
            m_indent = static_cast<int>(LineWidth(m_openLine));
        if (IsCharIn(m_options.end, ch) && m_indent > 0)
            m_indent = 0;
    }

    if (m_pendingRuby && m_pendingRubyCount > 0)
    {
        if (!m_pendingRubyAttached)
        {
            PushRubyToLast(*m_pendingRuby, 1);
            m_pendingRubyAttached = true;
        }
        --m_pendingRubyCount;
        if (m_pendingRubyCount == 0)
            m_pendingRuby.reset();
    }

    WrapIfNeeded();
    m_isBeginningOfLine = false;
}

void TextRenderBase::PushRawString(const std::string& text)
{
    size_t pos = 0;
    while (pos < text.size())
    {
        std::string ch = Utf8CharAt(text, pos);
        pos += ch.size();
        PushCharacter(ch);
    }
}

void TextRenderBase::PushGraphCharacter(const std::string& name)
{
    tjs_real width = 0, height = 0;
    MeasureGraph(name, width, height);
    if (width <= 0)
        width = 1;
    if (height <= 0)
        height = 1;

    Character c;
    c.graph = true;
    c.vertical = m_vertical;
    c.bold = m_state.bold;
    c.italic = m_state.italic;
    c.face = m_state.face;
    c.text = ttstr(name.c_str());
    c.cw = m_vertical ? height : width;
    c.size = m_vertical ? width : height;
    c.color = m_state.color;
    c.shadow = m_state.shadow;
    c.shadowcolor = m_state.shadowcolor;
    c.shadowdiff = m_state.shadowdiff;
    c.edge = m_state.edge;
    c.edgecolor = m_state.edgecolor;
    c.delay = m_elapsed;
    c.time = m_elapsed;
    c.link = m_currentLink;
    if (m_currentLink >= 0 && m_currentLink < static_cast<int>(m_linkNames.size()))
        c.linkName = m_linkNames[m_currentLink];
    c.pitch_after = m_state.pitch;
    c.line_spacing = m_state.linespacing;

    EnsureOpenLine();
    m_chars.push_back(c);
    m_elapsed += DelayForCharacter(c);

    m_lines[m_openLine].end = m_chars.size();
    LayoutOpenLine();
    WrapIfNeeded();
    m_isBeginningOfLine = false;
}

// ---------------------------------------------------------------------------
// line layout
// ---------------------------------------------------------------------------

void TextRenderBase::EnsureOpenLine()
{
    if (m_openLine != static_cast<size_t>(-1))
        return;
    TextLine line;
    line.start = m_chars.size();
    line.end = line.start;
    line.pos = m_cursor;
    line.open = true;
    m_lines.push_back(line);
    m_openLine = m_lines.size() - 1;
}

tjs_real TextRenderBase::LineWidth(size_t lineIndex) const
{
    if (lineIndex >= m_lines.size())
        return 0;
    const TextLine& line = m_lines[lineIndex];
    tjs_real width = 0;
    for (size_t i = line.start; i < line.end && i < m_chars.size(); ++i)
    {
        width += m_chars[i].cw;
        if (i + 1 < line.end)
            width += m_chars[i].pitch_after;
    }
    return width;
}

tjs_real TextRenderBase::LineAdvance(size_t lineIndex) const
{
    if (lineIndex >= m_lines.size())
        return 0;
    const TextLine& line = m_lines[lineIndex];
    tjs_real glyph = 0;
    int spacing = 0;
    for (size_t i = line.start; i < line.end && i < m_chars.size(); ++i)
    {
        glyph = std::max(glyph, m_chars[i].size);
        spacing = std::max(spacing, m_chars[i].line_spacing);
    }
    tjs_real base = 0;
    if (m_state.linesize > 0)
        base = static_cast<tjs_real>(m_state.linesize);
    else
    {
        // The line box follows the font ascent, not the nominal point size.
        // KAG ballons centre text against renderBottom, so using the em size
        // here leaves the text visibly low inside the balloon.
        tjs_real maxSize = std::max(glyph, CurrentFontSize());
        base = std::max(maxSize, static_cast<tjs_real>(AscentForSize(maxSize)));
    }
    if (base <= 0)
        base = CurrentFontSize();
    return base + spacing;
}

tjs_real TextRenderBase::DefaultLineAdvance() const
{
    tjs_real size = CurrentFontSize();
    tjs_real base = m_state.linesize > 0
        ? static_cast<tjs_real>(m_state.linesize)
        : std::max(size, static_cast<tjs_real>(AscentForSize(size)));
    if (base <= 0)
        base = size;
    return base + m_state.linespacing;
}

void TextRenderBase::LayoutLine(size_t lineIndex)
{
    if (lineIndex >= m_lines.size())
        return;
    TextLine& line = m_lines[lineIndex];
    if (line.start > m_chars.size())
        return;
    line.end = std::min(line.end, m_chars.size());

    tjs_real total = 0;
    for (size_t i = line.start; i < line.end; ++i)
    {
        total += m_chars[i].cw;
        if (i + 1 < line.end)
            total += m_chars[i].pitch_after;
    }

    if (!m_vertical)
    {
        tjs_real x = static_cast<tjs_real>(m_boxLeft) + m_indent;
        if (m_state.align == 0)
            x = static_cast<tjs_real>(m_boxLeft) + (m_boxWidth - total) / 2.0;
        else if (m_state.align == 1)
            x = static_cast<tjs_real>(m_boxLeft) + m_boxWidth - total;
        else
            x = static_cast<tjs_real>(m_boxLeft) + m_indent;

        tjs_real y = static_cast<tjs_real>(m_boxTop) + line.pos;
        for (size_t i = line.start; i < line.end; ++i)
        {
            Character& ch = m_chars[i];
            ch.x = x;
            ch.y = y;
            x += ch.cw + ch.pitch_after;
        }
    }
    else
    {
        tjs_real y = static_cast<tjs_real>(m_boxTop) + m_indent;
        if (m_state.valign == 0)
            y = static_cast<tjs_real>(m_boxTop) + (m_boxHeight - total) / 2.0;
        else if (m_state.valign == 1)
            y = static_cast<tjs_real>(m_boxTop) + m_boxHeight - total;

        tjs_real right = static_cast<tjs_real>(m_boxLeft) + m_boxWidth - line.pos;
        for (size_t i = line.start; i < line.end; ++i)
        {
            Character& ch = m_chars[i];
            ch.y = y;
            ch.x = right - ch.size;
            y += ch.cw + ch.pitch_after;
        }
    }

    line.advance = LineAdvance(lineIndex);
}

void TextRenderBase::LayoutOpenLine()
{
    if (m_openLine != static_cast<size_t>(-1))
        LayoutLine(m_openLine);
}

void TextRenderBase::FinishLine()
{
    tjs_real advance = 0;
    bool hadLine = false;
    if (m_openLine != static_cast<size_t>(-1))
    {
        LayoutLine(m_openLine);
        m_lines[m_openLine].open = false;
        advance = m_lines[m_openLine].advance;
        hadLine = true;
        m_openLine = static_cast<size_t>(-1);
    }

    if (!hadLine)
    {
        TextLine blank;
        blank.start = blank.end = m_chars.size();
        blank.pos = m_cursor;
        blank.advance = DefaultLineAdvance();
        blank.open = false;
        m_lines.push_back(blank);
        advance = blank.advance;
    }

    if (advance <= 0)
        advance = DefaultLineAdvance();
    m_cursor += advance;
    m_isBeginningOfLine = true;
}

void TextRenderBase::SplitLine(size_t breakIndex)
{
    if (m_openLine == static_cast<size_t>(-1))
        return;
    TextLine& line = m_lines[m_openLine];
    if (breakIndex <= line.start || breakIndex >= line.end)
        return;

    size_t oldEnd = line.end;
    line.end = breakIndex;
    LayoutLine(m_openLine);
    line.open = false;
    tjs_real advance = line.advance;
    if (advance <= 0)
        advance = DefaultLineAdvance();
    m_cursor += advance;
    m_openLine = static_cast<size_t>(-1);

    TextLine next;
    next.start = breakIndex;
    next.end = oldEnd;
    next.pos = m_cursor;
    next.open = true;
    m_lines.push_back(next);
    m_openLine = m_lines.size() - 1;
    LayoutLine(m_openLine);
}

size_t TextRenderBase::FindBreakIndex(size_t lineIndex) const
{
    if (lineIndex >= m_lines.size())
        return 0;
    const TextLine& line = m_lines[lineIndex];
    if (line.end <= line.start + 1)
        return line.start;

    tjs_real box = m_vertical ? m_boxHeight : m_boxWidth;
    tjs_real width = 0;
    size_t breakAt = line.start + 1;

    // Find the last position which still fits.
    for (size_t i = line.start; i < line.end; ++i)
    {
        width += m_chars[i].cw;
        if (i + 1 < line.end)
            width += m_chars[i].pitch_after;
        if (i + 1 < line.end && width <= box)
            breakAt = i + 1;
    }
    if (breakAt >= line.end)
        return line.start; // no meaningful break -> let it overflow

    // 行頭禁則: characters in "following" cannot start a line, keep them
    // on the previous line.
    int guard = 0;
    while (breakAt < line.end && IsCharIn(m_options.following, CharAt(breakAt)))
    {
        ++breakAt;
        if (m_options.kinsoku_max > 0 && ++guard >= m_options.kinsoku_max)
            break;
    }

    // 行末禁則: characters in "leading" cannot end a line, move them to
    // the next line.
    guard = 0;
    while (breakAt > line.start + 1 && IsCharIn(m_options.leading, CharAt(breakAt - 1)))
    {
        --breakAt;
        if (m_options.kinsoku_max > 0 && ++guard >= m_options.kinsoku_max)
            break;
    }

    // For Latin-like text, prefer to break after a space when possible.
    if (m_options.word_break && breakAt > line.start)
    {
        size_t space = static_cast<size_t>(-1);
        for (size_t i = breakAt; i > line.start; --i)
        {
            if (IsSpaceChar(CharAt(i - 1)))
            {
                space = i;
                break;
            }
        }
        if (space != static_cast<size_t>(-1) && space > line.start)
            breakAt = space;
    }

    return breakAt;
}

bool TextRenderBase::WrapIfNeeded()
{
    if (m_openLine == static_cast<size_t>(-1))
        return false;
    TextLine& line = m_lines[m_openLine];
    tjs_real box = m_vertical ? m_boxHeight : m_boxWidth;
    if (box <= 0)
        return false;

    tjs_real width = LineWidth(m_openLine);
    if (width <= box)
        return false;

    size_t breakAt = FindBreakIndex(m_openLine);
    if (breakAt <= line.start || breakAt >= line.end)
    {
        m_overflow = true;
        return false;
    }

    SplitLine(breakAt);
    // The new line may itself be too wide (e.g. a very long word).
    size_t guard = 0;
    while (LineWidth(m_openLine) > box && ++guard < 10000)
    {
        const TextLine& cur = m_lines[m_openLine];
        if (cur.end <= cur.start + 1)
            break;
        size_t nextBreak = FindBreakIndex(m_openLine);
        if (nextBreak <= cur.start || nextBreak >= cur.end)
            break;
        SplitLine(nextBreak);
    }
    m_overflow = true;
    return true;
}

// ---------------------------------------------------------------------------
// parsing
// ---------------------------------------------------------------------------

void TextRenderBase::ParseRuby(const std::string& text, size_t& i)
{
    // i points at '['; parse "[ruby]" or "[ruby,count]"
    ++i;
    std::string ruby;
    int count = 1;
    while (i < text.size() && text[i] != ']')
    {
        if (text[i] == ',')
        {
            ++i;
            size_t start = i;
            while (i < text.size() && text[i] != ']')
                ++i;
            count = std::atoi(text.substr(start, i - start).c_str());
            if (count <= 0)
                count = 1;
            break;
        }
        std::string ch = Utf8CharAt(text, i);
        i += ch.size();
        ruby += ch;
    }
    if (i < text.size() && text[i] == ']')
        ++i;

    if (!m_options.ignore_ruby && !ruby.empty() && count > 0)
    {
        m_pendingRuby = ttstr(ruby.c_str());
        m_pendingRubyCount = count;
        m_pendingRubyAttached = false;
    }
}

void TextRenderBase::ParseColor(const std::string& text, size_t& i, RgbColor& color, bool& specified)
{
    // i points at '#'; parse hex digits until ';'
    ++i;
    RgbColor value = 0;
    specified = false;
    while (i < text.size() && text[i] != ';')
    {
        char c = text[i];
        RgbColor digit;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'A' && c <= 'F')
            digit = 0x0a + c - 'A';
        else if (c >= 'a' && c <= 'f')
            digit = 0x0a + c - 'a';
        else
            break;
        value = (value << 4) | digit;
        specified = true;
        ++i;
    }
    if (i < text.size() && text[i] == ';')
        ++i;
    color = value;
}

void TextRenderBase::ParsePercent(const std::string& text, size_t& i)
{
    // i points at the character after '%'
    if (i >= text.size())
        return;
    char c = text[i];

    auto readValue = [&](int fallback) -> int {
        int value = ReadInteger(text, i, fallback);
        return value;
    };

    switch (c)
    {
        case 'f':
            ++i;
            if (!m_options.ignore_face)
            {
                std::string face;
                while (i < text.size() && text[i] != ';')
                {
                    std::string ch = Utf8CharAt(text, i);
                    i += ch.size();
                    face += ch;
                }
                if (i < text.size() && text[i] == ';')
                    ++i;
                m_state.face = face.empty() ? m_default.face : ttstr(face.c_str());
                UpdateRasterizerFont();
                NotifyFontChange();
            }
            else
            {
                while (i < text.size() && text[i] != ';')
                    ++i;
                if (i < text.size())
                    ++i;
            }
            break;
        case 'b':
        case 'i':
        case 's':
        case 'e':
        {
            ++i;
            if (i < text.size() && text[i] == ';')
            {
                ++i;
                if (c == 'b')
                    m_state.bold = m_default.bold;
                else if (c == 'i')
                    m_state.italic = m_default.italic;
                else if (c == 's')
                    m_state.shadow = m_default.shadow;
                else
                    m_state.edge = m_default.edge;
                UpdateRasterizerFont();
                NotifyFontChange();
                break;
            }
            if (i < text.size() && text[i] == '#')
            {
                RgbColor color = 0;
                bool specified = false;
                ParseColor(text, i, color, specified);
                if (c == 's')
                    m_state.shadowcolor = specified ? color : m_default.shadowcolor;
                else if (c == 'e')
                    m_state.edgecolor = specified ? color : m_default.edgecolor;
                break;
            }
            if (i < text.size() && (text[i] == 'd' || text[i] == 'D'))
            {
                ++i;
                if (c == 'b')
                    m_state.bold = m_default.bold;
                else if (c == 'i')
                    m_state.italic = m_default.italic;
                else if (c == 's')
                    m_state.shadow = m_default.shadow;
                else
                    m_state.edge = m_default.edge;
                UpdateRasterizerFont();
                NotifyFontChange();
                break;
            }
            bool on = i < text.size() && text[i] == '1';
            if (i < text.size() && (text[i] == '0' || text[i] == '1'))
                ++i;
            if (c == 'b')
                m_state.bold = on;
            else if (c == 'i')
                m_state.italic = on;
            else if (c == 's')
                m_state.shadow = on;
            else
                m_state.edge = on;
            UpdateRasterizerFont();
            NotifyFontChange();
            break;
        }
        case 'r':
            ++i;
            ResetFontState();
            NotifyFontChange();
            break;
        case 'B':
            ++i;
            if (!m_options.ignore_size)
                m_state.fontsize = m_default.bigfontsize;
            UpdateRasterizerFont();
            NotifyFontChange();
            break;
        case 'S':
            ++i;
            if (!m_options.ignore_size)
                m_state.fontsize = m_default.smallfontsize;
            UpdateRasterizerFont();
            NotifyFontChange();
            break;
        case 'C':
            ++i;
            if (!m_options.ignore_style)
                m_state.align = 0;
            break;
        case 'R':
            ++i;
            if (!m_options.ignore_style)
                m_state.align = 1;
            break;
        case 'L':
            ++i;
            if (!m_options.ignore_style)
                m_state.align = -1;
            break;
        case 'p':
            ++i;
            if (m_options.ignore_style)
            {
                while (i < text.size() && text[i] != ';')
                    ++i;
                if (i < text.size())
                    ++i;
            }
            else if (i < text.size() && text[i] == ';')
            {
                ++i;
                m_state.pitch = m_default.pitch;
            }
            else
                m_state.pitch = readValue(m_default.pitch);
            break;
        case 'n':
        {
            ++i;
            int n = readValue(1);
            if (n <= 0)
                n = 1;
            // %n terminates the current line and then adds n more breaks, so
            // "%n;" leaves one blank line (same as the reference plugin).
            FinishLine();
            for (int k = 0; k < n; ++k)
                FinishLine();
            for (int k = 0; k < n; ++k)
                m_renderText += TJS_N("\n");
            m_isBeginningOfLine = true;
            break;
        }
        case 'd':
        {
            ++i;
            if (i < text.size() && text[i] == ';')
            {
                ++i;
                m_perCharDelay = m_renderDelay;
            }
            else
            {
                int value = readValue(100);
                m_perCharDelay = m_renderDelay * value / 100.0;
            }
            break;
        }
        case 'a':
        {
            ++i;
            int value = readValue(0);
            m_perCharDelay = value;
            break;
        }
        case 'w':
        {
            ++i;
            if (i < text.size() && text[i] == '$')
            {
                ++i;
                std::string label;
                while (i < text.size() && text[i] != ';')
                {
                    std::string ch = Utf8CharAt(text, i);
                    i += ch.size();
                    label += ch;
                }
                if (i < text.size())
                    ++i;
                AddKeyWait(ttstr(label.c_str()), LabelDelay(label));
            }
            else
            {
                int value = readValue(100);
                m_elapsed += m_perCharDelay * m_timeScale * value / 100.0;
            }
            break;
        }
        case 't':
            ++i;
            if (i < text.size() && text[i] == '$')
            {
                ++i;
                std::string label;
                while (i < text.size() && text[i] != ';')
                {
                    std::string ch = Utf8CharAt(text, i);
                    i += ch.size();
                    label += ch;
                }
                if (i < text.size())
                    ++i;
                AddKeyWait(ttstr(label.c_str()), LabelDelay(label));
            }
            else
            {
                int value = readValue(0);
                AddKeyWait(ttstr(), value);
            }
            break;
        case 'D':
            ++i;
            if (i < text.size() && text[i] == '$')
            {
                ++i;
                std::string label;
                while (i < text.size() && text[i] != ';')
                {
                    std::string ch = Utf8CharAt(text, i);
                    i += ch.size();
                    label += ch;
                }
                if (i < text.size())
                    ++i;
                AddKeyWait(ttstr(label.c_str()), LabelDelay(label));
            }
            else
            {
                int value = readValue(0);
                AddKeyWait(ttstr(), value);
            }
            break;
        case 'l':
        {
            ++i;
            std::string name;
            while (i < text.size() && text[i] != ';')
            {
                std::string ch = Utf8CharAt(text, i);
                i += ch.size();
                name += ch;
            }
            if (i < text.size())
                ++i;
            if (name.empty())
            {
                m_currentLink = -1;
            }
            else
            {
                // Every %lname; starts a new link.  KAG allocates a fresh
                // index per beginHyperLink(), so deduplicating by name would
                // desynchronise our link indices from MessageLayer.links[].
                m_linkNames.push_back(ttstr(name.c_str()));
                m_currentLink = static_cast<int>(m_linkNames.size() - 1);
            }
            break;
        }
        case ';':
            ++i;
            if (!m_options.ignore_size)
                m_state.fontsize = m_default.fontsize;
            UpdateRasterizerFont();
            NotifyFontChange();
            break;
        default:
            if (c >= '0' && c <= '9')
            {
                int value = ReadInteger(text, i, 100);
                if (!m_options.ignore_size && value > 0)
                    m_state.fontsize = static_cast<int>(m_default.fontsize * value / 100.0);
                UpdateRasterizerFont();
                NotifyFontChange();
            }
            else
            {
                // Unknown directive: skip to ';' to stay compatible with
                // future/unknown tags.
                while (i < text.size() && text[i] != ';' && text[i] != '\n')
                    ++i;
                if (i < text.size() && text[i] == ';')
                    ++i;
            }
            break;
    }
}

void TextRenderBase::ParseEval(const std::string& text, size_t& i)
{
    // i points at '$'; parse until ';'
    ++i;
    std::string exp;
    while (i < text.size() && text[i] != ';')
    {
        std::string ch = Utf8CharAt(text, i);
        i += ch.size();
        exp += ch;
    }
    if (i < text.size() && text[i] == ';')
        ++i;

    ttstr value = EvalExpression(exp);
    if (!value.IsEmpty())
        PushRawString(value.AsStdString());
}

void TextRenderBase::ParseGraph(const std::string& text, size_t& i)
{
    // i points at '&'
    ++i;
    std::string name;
    while (i < text.size() && text[i] != ';')
    {
        std::string ch = Utf8CharAt(text, i);
        i += ch.size();
        name += ch;
    }
    if (i < text.size() && text[i] == ';')
        ++i;
    PushGraphCharacter(name);
}

bool TextRenderBase::RenderText(const std::string& text,
                                int autoIndent,
                                int diff,
                                bool all,
                                bool same)
{
    (void)all;

    // A non-"same" render starts a new timeline segment (a new message/line).
    // Characters from previous segments are already on screen; freeze them at
    // delay 0 so calcShowCount(0) does not replay the whole history.
    if (!same)
    {
        for (Character& c : m_chars)
            c.delay = 0;
        m_keyWaits.clear();
        m_elapsed = 0;
    }

    m_renderDelay = m_options.ignore_delay ? 0 : std::max(0, diff);
    m_perCharDelay = m_renderDelay;
    m_autoIndent = autoIndent;

    NotifyFontChange();

    size_t i = 0;
    while (i < text.size())
    {
        char c = text[i];

        if (c == '\\')
        {
            ++i;
            if (i >= text.size())
                break;
            char e = text[i++];
            switch (e)
            {
                case 'n':
                    FinishLine();
                    m_renderText += TJS_N("\n");
                    break;
                case 't':
                    PushCharacter("\t");
                    break;
                case 'w':
                    PushCharacter(" ");
                    break;
                case 'i':
                    m_indent = 0;
                    if (m_autoIndent >= 0 && m_openLine != static_cast<size_t>(-1))
                        m_indent = static_cast<int>(LineWidth(m_openLine));
                    break;
                case 'r':
                    m_indent = 0;
                    break;
                case 'k':
                    AddKeyWait(ttstr(), 0);
                    break;
                case 'x':
                    break;
                default:
                    PushCharacter(std::string(1, e));
                    break;
            }
            continue;
        }

        if (c == '%')
        {
            ++i;
            ParsePercent(text, i);
            continue;
        }
        if (c == '[')
        {
            ParseRuby(text, i);
            continue;
        }
        if (c == '#')
        {
            RgbColor color = 0;
            bool specified = false;
            ParseColor(text, i, color, specified);
            if (!m_options.ignore_color)
                m_state.color = specified ? color : m_default.color;
            continue;
        }
        if (c == '&')
        {
            ParseGraph(text, i);
            continue;
        }
        if (c == '$')
        {
            ParseEval(text, i);
            continue;
        }
        if (c == '\n')
        {
            ++i;
            FinishLine();
            m_renderText += TJS_N("\n");
            continue;
        }
        if (c == '\r')
        {
            ++i;
            continue;
        }

        std::string ch = Utf8CharAt(text, i);
        i += ch.size();
        if (ch.empty())
            continue;

        // Pending ruby is attached when the base character is emitted.
        PushCharacter(ch);
    }

    FinalizeLayout();
    return !m_overflow;
}

// ---------------------------------------------------------------------------
// layout finalization
// ---------------------------------------------------------------------------

void TextRenderBase::ApplyVerticalAlignment()
{
    m_offsetX = 0;
    m_offsetY = 0;

    if (m_lines.empty())
        return;

    tjs_real content = 0;
    for (const TextLine& line : m_lines)
        content += line.advance;

    if (!m_vertical)
    {
        if (m_state.valign == 0)
            m_offsetY = (m_boxHeight - content) / 2.0;
        else if (m_state.valign == 1)
            m_offsetY = m_boxHeight - content;
    }
    else
    {
        if (m_state.align == 0)
            m_offsetX = -(m_boxWidth - content) / 2.0;
        else if (m_state.align == -1)
            m_offsetX = -(m_boxWidth - content);
    }
}

void TextRenderBase::UpdateBounds()
{
    m_renderLeft = m_renderTop = 0;
    m_renderRight = m_renderBottom = 0;
    if (m_chars.empty())
    {
        m_renderLeft = 0;
        m_renderTop = 0;
        m_renderRight = 0;
        m_renderBottom = 0;
        return;
    }

    m_renderLeft = m_renderRight = m_chars[0].x + m_offsetX;
    m_renderTop = m_renderBottom = m_chars[0].y + m_offsetY;

    for (const Character& ch : m_chars)
    {
        tjs_real l = ch.x + m_offsetX;
        tjs_real r = l + (ch.vertical ? ch.size : ch.cw);
        tjs_real t = ch.y + m_offsetY;
        tjs_real b = t + (ch.vertical ? ch.cw : ch.size);

        for (const RubyPart& ruby : ch.ruby)
        {
            tjs_real rx = ch.x + ruby.x + m_offsetX;
            tjs_real ry = ch.y + ruby.y + m_offsetY;
            l = std::min(l, rx);
            r = std::max(r, rx + ruby.size);
            t = std::min(t, ry);
            b = std::max(b, ry + ruby.size);
        }

        m_renderLeft = std::min(m_renderLeft, l);
        m_renderRight = std::max(m_renderRight, r);
        m_renderTop = std::min(m_renderTop, t);
        m_renderBottom = std::max(m_renderBottom, b);
    }

    if (!m_vertical)
    {
        // renderBottom is the bottom of the last line box (including its
        // leading and spacing), which is what KAG uses to lay out balloons.
        tjs_real lineBottom = m_boxTop;
        for (const TextLine& line : m_lines)
            lineBottom = std::max(lineBottom, static_cast<tjs_real>(m_boxTop) + line.pos + line.advance);
        m_renderBottom = std::max(m_renderBottom, lineBottom + m_offsetY);
    }
}

void TextRenderBase::RebuildLinkRects()
{
    m_linkRects.assign(m_linkNames.size(), std::vector<LinkRect>());

    auto addRect = [&](size_t first, size_t last) {
        tjs_real l = 0, t = 0, r = 0, b = 0;
        for (size_t i = first; i <= last && i < m_chars.size(); ++i)
        {
            const Character& ch = m_chars[i];
            tjs_real cl = ch.x + m_offsetX;
            tjs_real ct = ch.y + m_offsetY;
            tjs_real cw = ch.vertical ? ch.size : ch.cw;
            tjs_real chh = ch.vertical ? ch.cw : ch.size;
            if (i == first)
            {
                l = cl;
                t = ct;
                r = cl + cw;
                b = ct + chh;
            }
            else
            {
                l = std::min(l, cl);
                t = std::min(t, ct);
                r = std::max(r, cl + cw);
                b = std::max(b, ct + chh);
            }
        }
        m_linkRects[m_chars[first].link].push_back(LinkRect{l, t, r - l, b - t});
    };

    // Group contiguous linked characters per laid-out line.  Merging by
    // geometry alone breaks when a link wraps (the next line shares the same
    // left edge) and produces rects spanning multiple lines.
    if (m_lines.empty())
    {
        size_t i = 0;
        while (i < m_chars.size())
        {
            int link = m_chars[i].link;
            if (link < 0 || link >= static_cast<int>(m_linkRects.size()))
            {
                ++i;
                continue;
            }
            size_t j = i;
            while (j + 1 < m_chars.size() && m_chars[j + 1].link == link)
                ++j;
            addRect(i, j);
            i = j + 1;
        }
        return;
    }

    for (const TextLine& line : m_lines)
    {
        size_t i = line.start;
        size_t end = std::min(line.end, m_chars.size());
        while (i < end)
        {
            int link = m_chars[i].link;
            if (link < 0 || link >= static_cast<int>(m_linkRects.size()))
            {
                ++i;
                continue;
            }
            size_t j = i;
            while (j + 1 < end && m_chars[j + 1].link == link)
                ++j;
            addRect(i, j);
            i = j + 1;
        }
    }
}

void TextRenderBase::UpdateLinks()
{
    RebuildLinkRects();
}

void TextRenderBase::FinalizeLayout()
{
    LayoutOpenLine();

    ApplyVerticalAlignment();
    UpdateBounds();
    UpdateLinks();

    m_renderOver = false;
    if (!m_options.ignore_over)
    {
        // Only overflow along the writing direction counts.  KAG grows the
        // render box while renderOver is set; cross-axis overflow (a line
        // slightly wider than the box, e.g. kinsoku punctuation) can never be
        // fixed that way and would otherwise loop forever.
        if (!m_vertical)
        {
            if (!m_options.ignore_overy && m_renderBottom > m_boxTop + m_boxHeight)
                m_renderOver = true;
        }
        else
        {
            if (!m_options.ignore_overx && m_renderLeft < m_boxLeft)
                m_renderOver = true;
        }
    }

    tjs_real content = 0;
    for (const TextLine& line : m_lines)
        content += line.advance;

    int totalLines = static_cast<int>(m_lines.size());
    int visible = 0;
    if (!m_vertical && m_state.linesize + m_state.linespacing > 0)
        visible = static_cast<int>(std::floor(m_boxHeight / (m_state.linesize + m_state.linespacing)));
    else if (m_vertical && m_state.linesize + m_state.linespacing > 0)
        visible = static_cast<int>(std::floor(m_boxWidth / (m_state.linesize + m_state.linespacing)));

    m_maxScrollOffset = static_cast<int>(std::max<tjs_real>(
        0, content - (m_vertical ? m_boxWidth : m_boxHeight)));
    m_maxScrollLine = std::max(0, totalLines - visible);
    m_overflow = m_renderOver;
}

void TextRenderBase::done()
{
    FinalizeLayout();
}

// ---------------------------------------------------------------------------
// public control
// ---------------------------------------------------------------------------

void TextRenderBase::setRenderSize(tjs_real width, tjs_real height)
{
    m_boxWidth = std::max(0, static_cast<int>(width));
    m_boxHeight = std::max(0, static_cast<int>(height));
    clear();
}

void TextRenderBase::clear()
{
    m_chars.clear();
    m_lines.clear();
    m_openLine = static_cast<size_t>(-1);
    m_cursor = 0;
    m_indent = 0;
    m_autoIndent = 0;
    m_isBeginningOfLine = true;

    m_keyWaits.clear();
    m_linkNames.clear();
    m_linkRects.clear();
    m_currentLink = -1;

    m_renderText = ttstr();
    m_elapsed = 0;
    m_perCharDelay = 0;
    m_renderDelay = 0;

    m_renderLeft = m_renderTop = m_renderRight = m_renderBottom = 0;
    m_maxScrollOffset = m_maxScrollLine = 0;
    m_offsetX = m_offsetY = 0;
    m_overflow = false;
    m_renderOver = false;

    m_pendingRuby.reset();
    m_pendingRubyCount = 0;
    m_pendingRubyAttached = false;

    m_fontNotifyDirty = true;
    m_state = m_default;
    m_vertical = m_options.vertical;

    UpdateRasterizerFont();
}

void TextRenderBase::newline()
{
    FinishLine();
    m_renderText += TJS_N("\n");
}

// ---------------------------------------------------------------------------
// queries
// ---------------------------------------------------------------------------

tTJSVariant TextRenderBase::getCharacters(int start, int end) const
{
    iTJSDispatch2* array = TJSCreateArrayObject();
    if (!array)
        return tTJSVariant();

    int count = static_cast<int>(m_chars.size());
    if (start < 0)
        start = 0;
    if (start >= count)
        return TakeObject(array);

    int stop;
    if (start == 0 && end == 0)
        stop = count;
    else
    {
        // The second argument is a character count, not an end index.
        int length = end;
        if (length <= 0)
            length = count - start;
        stop = std::min(count, start + length);
    }

    tjs_uint index = 0;
    for (int i = start; i < stop; ++i)
    {
        tTJSVariant ch = SerializeCharacter(m_chars[i], m_offsetX, m_offsetY);
        array->PropSetByNum(TJS_MEMBERENSURE, index++, &ch, array);
    }

    return TakeObject(array);
}

tTJSVariant TextRenderBase::getKeyWait() const
{
    iTJSDispatch2* array = TJSCreateArrayObject();
    if (!array)
        return tTJSVariant();

    for (size_t i = 0; i < m_keyWaits.size(); ++i)
    {
        iTJSDispatch2* dict = TJSCreateDictionaryObject();
        if (!dict)
            continue;
        SetInt(dict, "pos", m_keyWaits[i].pos);
        SetReal(dict, "time", m_keyWaits[i].time);
        tTJSVariant v = TakeObject(dict);
        array->PropSetByNum(TJS_MEMBERENSURE, static_cast<tjs_uint>(i), &v, array);
    }

    return TakeObject(array);
}

int TextRenderBase::calcShowCount(tjs_real elapsed) const
{
    int count = 0;
    for (const Character& ch : m_chars)
    {
        if (ch.delay <= elapsed)
            ++count;
        else
            break; // delays are monotonically increasing
    }
    return count;
}

tjs_real TextRenderBase::calcLineOffset(int line) const
{
    if (line < 0)
        return 0;
    if (line >= static_cast<int>(m_lines.size()))
        return m_renderBottom;
    return m_lines[line].pos + (m_vertical ? -m_offsetX : m_offsetY);
}

bool TextRenderBase::contains(tjs_real x, tjs_real y) const
{
    if (m_chars.empty())
        return false;
    return x >= m_renderLeft && x <= m_renderRight && y >= m_renderTop &&
           y <= m_renderBottom;
}

tTJSVariant TextRenderBase::getLinkNames() const
{
    iTJSDispatch2* array = TJSCreateArrayObject();
    if (!array)
        return tTJSVariant();

    for (size_t i = 0; i < m_linkNames.size(); ++i)
    {
        tTJSVariant v(m_linkNames[i]);
        array->PropSetByNum(TJS_MEMBERENSURE, static_cast<tjs_uint>(i), &v, array);
    }
    return TakeObject(array);
}

tTJSVariant TextRenderBase::getLinkRects(int index) const
{
    iTJSDispatch2* array = TJSCreateArrayObject();
    if (!array)
        return tTJSVariant();

    if (index >= 0 && index < static_cast<int>(m_linkRects.size()))
    {
        const std::vector<LinkRect>& rects = m_linkRects[index];
        for (size_t i = 0; i < rects.size(); ++i)
        {
            iTJSDispatch2* dict = TJSCreateDictionaryObject();
            if (!dict)
                continue;
            SetReal(dict, "left", rects[i].left);
            SetReal(dict, "top", rects[i].top);
            SetReal(dict, "width", rects[i].width);
            SetReal(dict, "height", rects[i].height);
            tTJSVariant v = TakeObject(dict);
            array->PropSetByNum(TJS_MEMBERENSURE, static_cast<tjs_uint>(i), &v, array);
        }
    }

    return TakeObject(array);
}

tTJSVariant TextRenderBase::getLinkCharacters(int index) const
{
    iTJSDispatch2* array = TJSCreateArrayObject();
    if (!array)
        return tTJSVariant();

    tjs_uint out = 0;
    for (const Character& ch : m_chars)
    {
        if (ch.link != index)
            continue;
        tTJSVariant v = SerializeCharacter(ch, m_offsetX, m_offsetY);
        array->PropSetByNum(TJS_MEMBERENSURE, out++, &v, array);
    }
    return TakeObject(array);
}

bool TextRenderBase::isLinkContains(int index, tjs_real x, tjs_real y) const
{
    if (index < 0 || index >= static_cast<int>(m_linkRects.size()))
        return false;
    for (const LinkRect& r : m_linkRects[index])
    {
        if (x >= r.left && x <= r.left + r.width && y >= r.top &&
            y <= r.top + r.height)
            return true;
    }
    return false;
}

int TextRenderBase::getLinkOfPosition(tjs_real x, tjs_real y) const
{
    for (size_t i = 0; i < m_linkRects.size(); ++i)
    {
        for (const LinkRect& r : m_linkRects[i])
        {
            if (x >= r.left && x <= r.left + r.width && y >= r.top &&
                y <= r.top + r.height)
                return static_cast<int>(i);
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// raw render entry (needed for TJS callbacks)
// ---------------------------------------------------------------------------

tjs_error TextRenderBase::cb_render(tTJSVariant* result,
                                    tjs_int numparams,
                                    tTJSVariant** param,
                                    iTJSDispatch2* objthis)
{
    auto self = ncbInstanceAdaptor<TextRenderBase>::GetNativeInstance(objthis);
    if (!self)
        return TJS_E_NATIVECLASSCRASH;
    if (numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    auto s = param[0]->AsStringNoAddRef();
    tTJSString text(s ? s : nullptr);

    int autoIndent = numparams > 1 ? static_cast<int>(param[1]->AsInteger()) : 0;
    int diff = numparams > 2 ? static_cast<int>(param[2]->AsInteger()) : 0;
    bool all = numparams > 3 ? (param[3]->AsInteger() != 0) : false;
    bool same = numparams > 4 ? (param[4]->AsInteger() != 0) : false;

    self->m_owner = objthis;
    bool ret = self->RenderText(text.AsStdString(), autoIndent, diff, all, same);
    self->m_owner = nullptr;

    if (result)
        *result = static_cast<tjs_int>(ret ? 1 : 0);
    return TJS_S_OK;
}

// ---------------------------------------------------------------------------
// registration
// ---------------------------------------------------------------------------

#define property_delegate(name) NCB_PROPERTY(name, get_##name, set_##name)
#define property_delegate_ro(name) NCB_PROPERTY_RO(name, get_##name)

NCB_REGISTER_CLASS(TextRenderBase)
{
    Constructor();

    NCB_METHOD_RAW_CALLBACK(render, &TextRenderBase::cb_render, 0);

    NCB_METHOD(setOption);
    NCB_METHOD(setDefault);
    NCB_METHOD(setRenderSize);
    NCB_METHOD(clear);
    NCB_METHOD(resetFont);
    NCB_METHOD(resetStyle);
    NCB_METHOD(setFont);
    NCB_METHOD(setStyle);
    NCB_METHOD(newline);
    NCB_METHOD(done);
    NCB_METHOD(contains);
    NCB_METHOD(getKeyWait);
    NCB_METHOD(calcLineOffset);
    NCB_METHOD(calcShowCount);
    NCB_METHOD(getCharacters);
    NCB_METHOD(getLinkNames);
    NCB_METHOD(getLinkRects);
    NCB_METHOD(getLinkCharacters);
    NCB_METHOD(isLinkContains);
    NCB_METHOD(getLinkOfPosition);

    property_delegate(vertical);
    property_delegate(timeScale);
    property_delegate(fontScale);
    property_delegate_ro(renderOver);
    property_delegate_ro(renderLines);
    property_delegate_ro(renderCount);
    property_delegate_ro(renderDelay);
    property_delegate_ro(renderLeft);
    property_delegate_ro(renderTop);
    property_delegate_ro(renderRight);
    property_delegate_ro(renderBottom);
    property_delegate_ro(renderText);
    property_delegate_ro(maxScrollOffset);
    property_delegate_ro(maxScrollLine);

    property_delegate(defaultFace);
    property_delegate(defaultFontSize);
    property_delegate(defaultBigFontSize);
    property_delegate(defaultSmallFontSize);
    property_delegate(defaultLineSize);
    property_delegate(defaultLineSpacing);
    property_delegate(defaultPitch);
    property_delegate(defaultAlign);
    property_delegate(defaultValign);
    property_delegate(defaultRubySize);
    property_delegate(defaultRubyOffset);
    property_delegate(defaultChColor);
    property_delegate(defaultShadow);
    property_delegate(defaultShadowColor);
    property_delegate(defaultShadowDiff);
    property_delegate(defaultEdge);
    property_delegate(defaultEdgeColor);
    property_delegate(defaultBold);
    property_delegate(defaultItalic);
};
