/**
 * copyright (c) 2007 Go Watanabe
 */

#include "ncbind/ncbind.hpp"

#include "TVPStorage.h"

#define NCB_MODULE_NAME TJS_N("json.dll")

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <algorithm>

#define UNICODE_BOM (0xfeff)

// CP_ACP为本地编码 我们基于TextStream确定
extern size_t TextStream_mbstowcs(tjs_wchar* pwcs, const tjs_char* s, size_t n);

// ----------------------------------------------------------------------

class IFileStorage
{

    tTJSBinaryStream* in;
    char buf[8192];
    tjs_uint64 pos;
    tjs_uint64 len;
    bool eofFlag;
    bool utf8;

public:
    IFileStorage(tTJSVariantString* filename, bool utf8) : utf8(utf8)
    {
        in = TVPCreateBinaryStreamForRead(filename, "");
        if (!in)
        {
            TVPThrowExceptionMessage((ttstr(TJS_N("cannot open : ")) + *filename).c_str());
        }
        pos = 0;
        len = 0;
        eofFlag = false;
    }

    ~IFileStorage()
    {
        if (in)
        {
            delete in;
            in = NULL;
        }
    }

    int getc()
    {
        if (pos < len)
        {
            return buf[pos++];
        }
        else
        {
            if (!in || eofFlag)
            {
                return EOF;
            }
            else
            {
                pos = 0;
                if ((len = in->Read(buf, sizeof buf)) > 0)
                {
                    eofFlag = len < sizeof buf;
                }
                else
                {
                    eofFlag = true;
                    len = 0;
                }
                return getc();
            }
        }
    }

    void ungetc()
    {
        if (pos > 0)
        {
            pos--;
        }
    }

    bool eof() { return pos >= len && eofFlag; }

    /**
     * 改行チェック
     */
    bool endOfLine(int c)
    {
        bool eol = (c == '\r' || c == '\n');
        if (c == '\r')
        {
            c = getc();
            if (!eof() && c != '\n')
            {
                ungetc();
            }
        }
        return eol;
    }

    bool addNextLine(ttstr& str)
    {
        int c;
        std::string mbline;
        while ((c = getc()) != EOF && !endOfLine(c))
        {
            mbline += c;
        }
        int l = (int)mbline.length();
        if (l > 0 || c != EOF)
        {
            if (utf8)
            {
                str += tTJSString(mbline.c_str());
            }
            else
            {
                tjs_char* buf = nullptr;
                tjs_wchar* interBuff = nullptr;
                try
                {
                    // 再次重蹈覆辙(看来对于编码得专门抽象一个工具包) TODO
                    // to utf-16
                    size_t interSize = TextStream_mbstowcs(NULL, (tjs_char*)mbline.c_str(), 0);
                    if (interSize == (size_t)-1)
                        TVPThrowExceptionMessage(TJSNarrowToWideConversionError);
                    interBuff = new tjs_wchar[interSize + 1];
                    TextStream_mbstowcs(interBuff, (tjs_char*)mbline.c_str(), interSize);
                    interBuff[interSize] = 0;
                    // to utf-8
                    size_t BufferLen = TVPWideCharToUtf8String(interBuff, NULL);
                    if (BufferLen == (size_t)-1)
                        TVPThrowExceptionMessage(TJSWideToNarrowConversionError);
                    buf = new tjs_char[BufferLen + 1];
                    TVPWideCharToUtf8String(interBuff, buf);
                    buf[BufferLen] = 0;
                }
                catch (...)
                {
                    if (buf)
                        delete[] buf;
                    if (interBuff)
                        delete[] interBuff;
                    return false;
                }
                str += buf;
                if (buf)
                    delete[] buf;
                if (interBuff)
                    delete[] interBuff;
            }
            return true;
        }
        else
        {
            return false;
        }
    }
};

// -----------------------------------------------------------------
class JSONTextReadStream : public iTJSTextReadStream
{
public:
    IFileStorage* Storage;
    tjs_int codepage;
    ttstr buf;
    tjs_int pos;

    JSONTextReadStream(tTJSVariantString* filename, tjs_int codepage)
    {
        Storage = new IFileStorage(filename, codepage);
        pos = 0;
    }

    ~JSONTextReadStream(void) { delete Storage; }

    virtual tjs_uint Read(tTJSString& targ, tjs_uint size)
    {
        tjs_uint readSize = 0;
        while (readSize < size)
        {
            if (pos >= buf.length())
            {
                buf.Clear();
                pos = 0;
                if (!Storage->addNextLine(buf))
                    break;
            }
            tjs_uint n = std::min(tjs_uint(buf.length() - pos), size - readSize);
            readSize += n;
            while (n > 0)
            {
                targ += buf[pos];
                pos++;
                n--;
            }
        }
        return readSize;
    }

    virtual void Destruct() { delete this; }
};

// -----------------------------------------------------------------

class IReader
{
public:
    virtual int getc() = 0;
    virtual void ungetc() = 0;
    virtual void close() = 0;

    bool isError;

    /**
     * コンストラクタ
     */
    IReader() { isError = false; }

    virtual ~IReader(){};

    /**
     * エラー処理
     */
    void error(const tjs_char* msg)
    {
        isError = true;
        TVPAddLog(msg);
    }

    /**
     * 行末まで読み飛ばす
     */
    void toEOL()
    {
        int c;
        do
        {
            c = getc();
        } while (c != EOF && c != '\n' && c != '\r');
    }

    /**
     * 空白とコメントを除去して次の文字を返す
     */
    int next()
    {
        for (;;)
        {
            int c = getc();
            if (c == '#')
            {
                toEOL();
            }
            else if (c == '/')
            {
                switch (getc())
                {
                    case '/':
                        toEOL();
                        break;
                    case '*':
                        for (;;)
                        {
                            c = getc();
                            if (c == EOF)
                            {
                                error(TJS_N("コメントが閉じていません"));
                                return EOF;
                            }
                            if (c == '*')
                            {
                                if (getc() == '/')
                                {
                                    break;
                                }
                                ungetc();
                            }
                        }
                        break;
                    default:
                        ungetc();
                        return '/';
                }
            }
            else if (c != UNICODE_BOM && (c == EOF || c > ' '))
            {
                return c;
            }
        }
    }

    /**
     * 指定された文字数分の文字列を取得
     * @param str 文字列の格納先
     * @param n 文字数
     */
    void next(ttstr& str, int n)
    {
        str = "";
        while (n > 0)
        {
            int c = getc();
            if (c == EOF)
            {
                break;
            }
            str += c;
            n--;
        }
    }

    void parseObject(tTJSVariant& var)
    {

        // 辞書を生成
        iTJSDispatch2* dict = TJSCreateDictionaryObject();
        var = tTJSVariant(dict, dict);
        dict->Release();

        int c;

        for (;;)
        {

            tTJSVariant key;

            c = next();
            if (c == EOF)
            {
                error(TJS_N("オブジェクトは '}' で終了する必要があります"));
                return;
            }
            else if (c == '}')
            {
                return;
            }
            else if (c == ',' || c == ';')
            {
                ungetc();
            }
            else
            {

                ungetc();
                parse(key);

                c = next();
                if (c == '=')
                {
                    if (getc() != '>')
                    {
                        ungetc();
                    }
                }
                else if (c != ':')
                {
                    error(TJS_N("キーの後には ':' または '=' または '=>' が必要です"));
                    return;
                }

                // メンバ登録
                tTJSVariant value;
                parse(value);

                dict->PropSet(TJS_MEMBERENSURE, key.GetString(), NULL, &value, dict);
            }

            switch (next())
            {
                case ';':
                case ',':
                    break;
                case '}':
                    return;
                default:
                    error(TJS_N(" ',' または ';' または '}' が必要です"));
                    return;
            }
        }
    }

    void parseArray(tTJSVariant& var)
    {

        // 配列を生成
        iTJSDispatch2* array = TJSCreateArrayObject();
        var = tTJSVariant(array, array);
        array->Release();

        tjs_int cnt = 0;

        for (;;)
        {
            int ch = next();
            switch (ch)
            {
                case EOF:
                    error(TJS_N("配列は ']' で終了する必要があります"));
                    return;
                case ']':
                    return;
                case ',':
                case ';':
                {
                    ungetc();
                    // 空のカラムを登録
                    tTJSVariant value;
                    array->PropSetByNum(TJS_MEMBERENSURE, cnt++, &value, array);
                }
                break;
                default:
                    ungetc();
                    tTJSVariant value;
                    parse(value);
                    array->PropSetByNum(TJS_MEMBERENSURE, cnt++, &value, array);
            }

            switch (next())
            {
                case ';':
                case ',':
                    break;
                case ']':
                    return;
                default:
                    error(TJS_N(" ',' または ';' または ']' が必要です"));
                    return;
            }
        }
    }

    /**
     * クオート文字列のパース
     * @param quote クオート文字
     * @param var 格納先
     */
    void parseQuoteString(int quote, tTJSVariant& var)
    {
        int c;
        ttstr str;
        for (;;)
        {
            c = getc();
            switch (c)
            {
                case 0:
                case '\n':
                case '\r':
                    error(TJS_N("文字列が終端していません"));
                    return;
                case '\\':
                    c = getc();
                    switch (c)
                    {
                        case 'b':
                            str += '\b';
                            break;
                        case 'f':
                            str += '\f';
                            break;
                        case 't':
                            str += '\t';
                            break;
                        case 'r':
                            str += '\r';
                            break;
                        case 'n':
                            str += '\n';
                            break;
                        case 'u':
                        {
                            ttstr work;
                            next(work, 4);
                            std::string out = work.c_str();
                            str += (tjs_char)std::stol(out.c_str(), NULL, 16);
                        }
                        break;
                        case 'x':
                        {
                            ttstr work;
                            next(work, 2);
                            std::string out = work.c_str();
                            str += (tjs_char)std::stol(out.c_str(), NULL, 16);
                        };
                        break;
                        default:
                            str += c;
                    }
                    break;
                default:
                    if (c == quote)
                    {
                        var = str;
                        return;
                    }
                    str += c;
            }
        }
    }

    /**
     * 指定した文字が数値の１文字目の構成要素かどうか
     */
    bool isNumberFirst(int ch)
    {
        return (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '+';
    }

    /**
     * 指定した文字が数値の構成要素かどうか
     */
    bool isNumber(int ch)
    {
        return (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '+' || ch == 'e' ||
               ch == 'E';
    }

    /**
     * 指定した文字が文字列の構成要素かどうか
     */
    bool isString(int ch)
    {
        return ch > 0x80 || ch > ' ' && TJS_strchr(TJS_N(",:]}/\\\"[{;=#"), ch) == NULL;
    }

    /**
     * パースの実行
     * @param var 結果格納先
     */
    void parse(tTJSVariant& var)
    {

        int ch = next();

        switch (ch)
        {
            case '"':
            case '\'':
                // クオート文字列
                parseQuoteString(ch, var);
                break;
            case '{':
                // オブジェクト
                parseObject(var);
                break;
            case '[':
                parseArray(var);
                break;
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
            case '.':
            case '-':
            case '+':
            {
                // 数値
                bool doubleValue = false;

                ttstr s;
                while (isNumber(ch))
                {
                    if (ch == '.')
                    {
                        doubleValue = true; // ひどい処理だ（笑)
                    }
                    s += ch;
                    ch = getc();
                }
                ungetc();

                std::string out = s.c_str();

                // 数値
                if (doubleValue)
                {
                    double value = std::strtod(out.c_str(), NULL);
                    var = value;
                }
                else
                {
                    tjs_int64 value = std::strtoll(out.c_str(), NULL, 10);
                    var = value;
                }
            }
            break;
            default:
                if (ch >= 'a' && ch <= 'z')
                {

                    // 文字列を抽出
                    ttstr s;
                    while (ch >= 'a' && ch <= 'z')
                    {
                        s += ch;
                        ch = getc();
                    }
                    ungetc();

                    // 識別子
                    if (s == TJS_N("true"))
                    {
                        var = true;
                    }
                    else if (s == TJS_N("false"))
                    {
                        var = false;
                    }
                    else if (s == TJS_N("null"))
                    {
                        var.Clear();
                    }
                    else if (s == TJS_N("void"))
                    {
                        var.Clear();
                    }
                    else
                    {
                        ttstr msg = TJS_N("不明なキーワードです:");
                        msg += s;
                        error(msg.c_str());
                    }
                }
                else
                {
                    ttstr msg = TJS_N("不明な文字です:");
                    error(msg.c_str());
                }
        }
    }
};

class IFileReader : public IReader
{

    /// 入力バッファ
    ttstr buf;
    /// 入力ストリーム
    iTJSTextReadStream* stream;

    tjs_uint64 pos;
    bool eofFlag;

public:
    IFileReader(tTJSVariantString* filename, tjs_int codepage)
    {
        stream = new JSONTextReadStream(filename, codepage);
        pos = 0;
        eofFlag = false;
    }

    virtual void close()
    {
        if (stream)
        {
            stream->Destruct();
            stream = NULL;
        }
    }

    ~IFileReader() { close(); }

    int getc()
    {
        if (pos < buf.length())
        {
            return buf.c_str()[pos++];
        }
        else
        {
            if (!stream || eofFlag)
            {
                return EOF;
            }
            else
            {
                pos = 0;
                buf.Clear();
                eofFlag = stream->Read(buf, 1024) < 1024;
                return getc();
            }
        }
    }

    void ungetc()
    {
        if (pos > 0)
        {
            pos--;
        }
    }
};

class IStringReader : public IReader
{

    ttstr dat;
    const tjs_char* p;
    tjs_uint64 length;
    tjs_uint64 pos;

public:
    IStringReader(const tjs_char* str)
    {
        dat = str;
        p = dat.c_str();
        length = dat.length();
        pos = 0;
    }

    void close() {}

    int getc() { return pos < length ? p[pos++] : EOF; }

    void ungetc()
    {
        if (pos > 0)
        {
            pos--;
        }
    }
};

// -----------------------------------------------------------------

/**
 * 出力処理用インターフェース
 */
class IWriter
{
protected:
    const tjs_char* newlinestr;

public:
    int indent;
    bool hex;
    IWriter(int newlinetype = 0)
    {
        indent = 0;
        hex = false;
        switch (newlinetype)
        {
            case 1:
                newlinestr = TJS_N("\n");
                break;
            default:
                newlinestr = TJS_N("\r\n");
                break;
        }
    }
    virtual ~IWriter(){};
    virtual void write(const tjs_char* str) = 0;
    virtual void write(tjs_char ch) = 0;
    virtual void write(tTVReal) = 0;
    virtual void write(tTVInteger) = 0;

    inline void newline()
    {
        write(newlinestr);
        for (int i = 0; i < indent; i++)
        {
            write((tjs_char)' ');
        }
    }

    inline void addIndent()
    {
        indent++;
        newline();
    }

    inline void delIndent()
    {
        indent--;
        newline();
    }
};

/**
 * 文字列出力
 */
class IStringWriter : public IWriter
{

public:
    ttstr buf;
    /**
     * コンストラクタ
     */
    IStringWriter(int newlinetype = 0) : IWriter(newlinetype){};

    virtual void write(const tjs_char* str) { buf += str; }

    virtual void write(tjs_char ch) { buf += ch; }

    virtual void write(tTVReal num)
    {
        if (hex)
        {
            tTJSVariantString* str = TJSRealToHexString(num);
            buf += str;
            str->Release();
            buf += TJS_N(" /* ");
            str = TJSRealToString(num);
            buf += str;
            str->Release();
            buf += TJS_N(" */");
        }
        else
        {
            tTJSVariantString* str = TJSRealToString(num);
            buf += str;
            str->Release();
        }
    }

    virtual void write(tTVInteger num)
    {
        tTJSVariantString* str = TJSIntegerToString(num);
        buf += str;
        str->Release();
    }
};

/**
 * ファイル出力
 */
class IFileWriter : public IWriter
{

    /// 出力バッファ
    ttstr buf;
    /// 出力ストリーム
    tTJSBinaryStream* stream;
    bool utf;
    char* dat;
    int datlen;

public:
    /**
     * コンストラクタ
     */
    IFileWriter(const tjs_char* filename, bool utf = false, int newlinetype = 0)
      : IWriter(newlinetype)
    {
        stream = TVPCreateBinaryStreamForWrite(filename, "");
        this->utf = utf;
        dat = NULL;
        datlen = 0;
    }

    /**
     * デストラクタ
     */
    ~IFileWriter()
    {
        if (stream)
        {
            if (buf.length() > 0)
            {
                output();
            }
            delete stream;
            stream = 0;
        }
        if (dat)
        {
            free(dat);
        }
    }

    void output()
    {
        if (stream)
        {
            tjs_uint64 s;
            if (utf)
            {
                // UTF-8 で出力
                if (buf.length() > 0)
                {
                    s = stream->Write(buf.c_str(), buf.length());
                }
            }
            else
            {
                // 現在のコードページで出力
                tjs_int size = TVPUtf8ToWideCharString(buf.c_str(), NULL);
                if (size <= 0)
                    return;
                tjs_wchar* us = new tjs_wchar[size + 1];
                try
                {
                    TVPUtf8ToWideCharString(buf.c_str(), us);
                    us[size] = 0;
                    stream->Write(us, size * 2);
                }
                catch (...)
                {
                    delete[] us;
                    throw;
                }
                delete[] us;
            }
        }
        buf.Clear();
    }

    virtual void write(const tjs_char* str)
    {
        if (stream)
        {
            buf += str;
            if (buf.length() >= 1024)
            {
                output();
            }
        }
    }

    virtual void write(tjs_char ch) { buf += ch; }

    virtual void write(tTVReal num)
    {
        if (hex)
        {
            tTJSVariantString* str = TJSRealToHexString(num);
            buf += str;
            str->Release();
            buf += TJS_N(" /* ");
            str = TJSRealToString(num);
            buf += str;
            str->Release();
            buf += TJS_N(" */");
        }
        else
        {
            tTJSVariantString* str = TJSRealToString(num);
            buf += str;
            str->Release();
        }
    }

    virtual void write(tTVInteger num)
    {
        tTJSVariantString* str = TJSIntegerToString(num);
        buf += str;
        str->Release();
    }
};

//---------------------------------------------------------------------------

// Array クラスメンバ
static iTJSDispatch2* ArrayCountProp = NULL; // Array.count

// -----------------------------------------------------------------

static void addMethod(iTJSDispatch2* dispatch, const tjs_char* methodName, tTJSDispatch* method)
{
    tTJSVariant var = tTJSVariant(method);
    method->Release();
    dispatch->PropSet(TJS_MEMBERENSURE, // メンバがなかった場合には作成するようにするフラグ
                      methodName, // メンバ名 ( かならず TJS_N( ) で囲む )
                      NULL, // ヒント ( 本来はメンバ名のハッシュ値だが、NULL でもよい )
                      &var,    // 登録する値
                      dispatch // コンテキスト
    );
}

static void delMethod(iTJSDispatch2* dispatch, const tjs_char* methodName)
{
    dispatch->DeleteMember(0,          // フラグ ( 0 でよい )
                           methodName, // メンバ名
                           NULL,       // ヒント
                           dispatch    // コンテキスト
    );
}

static iTJSDispatch2* getMember(iTJSDispatch2* dispatch, const tjs_char* name)
{
    tTJSVariant val;
    if (TJS_FAILED(dispatch->PropGet(TJS_IGNOREPROP, name, NULL, &val, dispatch)))
    {
        ttstr msg = TJS_N("can't get member:");
        msg += name;
        TVPThrowExceptionMessage(msg.c_str());
    }
    return val.AsObject();
}

// -----------------------------------------------------------------

static tjs_error eval(IReader& file, tTJSVariant* result)
{
    tjs_error ret = TJS_S_OK;
    if (result)
    {
        file.parse(*result);
    }
    file.close();
    if (file.isError)
    {
        TVPThrowExceptionMessage(TJS_N("JSONファイル のパースに失敗しました"));
    }
    return ret;
}

//---------------------------------------------------------------------------

/**
 * JSON を文字列から読み取る
 * @param text JSON の文字列表現
 */
class tEvalJSON : public tTJSDispatch
{
protected:
public:
    tjs_error FuncCall(tjs_uint32 flag,
                       const tjs_char* membername,
                       tjs_uint32* hint,
                       tTJSVariant* result,
                       tjs_int numparams,
                       tTJSVariant** param,
                       iTJSDispatch2* objthis)
    {

        if (membername)
            return TJS_E_MEMBERNOTFOUND;
        if (numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        IStringReader reader(param[0]->GetString());
        eval(reader, result);
        return TJS_S_OK;
    }
};

/**
 * JSON をファイルから読み取る
 * @param filename ファイル名
 */
class tEvalJSONStorage : public tTJSDispatch
{
protected:
public:
    tjs_error FuncCall(tjs_uint32 flag,
                       const tjs_char* membername,
                       tjs_uint32* hint,
                       tTJSVariant* result,
                       tjs_int numparams,
                       tTJSVariant** param,
                       iTJSDispatch2* objthis)
    {

        if (membername)
            return TJS_E_MEMBERNOTFOUND;
        if (numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        tTJSVariantString* filename = param[0]->AsStringNoAddRef();
        bool utf8 = numparams >= 2 ? (int*)param[1] != 0 : false;

        IFileReader reader(filename, utf8);
        eval(reader, result);
        return TJS_S_OK;
    }
};

static void quoteString(const tjs_char* str, IWriter* writer)
{
    if (str)
    {
        writer->write((tjs_char)'"');
        const tjs_char* p = str;
        int ch;
        while ((ch = *p++))
        {
            if (ch == '"')
            {
                writer->write(TJS_N("\\\""));
            }
            else if (ch == '\\')
            {
                writer->write(TJS_N("\\\\"));
            }
            else if (ch == 0x08)
            {
                writer->write(TJS_N("\\b"));
            }
            else if (ch == 0x0c)
            {
                writer->write(TJS_N("\\f"));
            }
            else if (ch == 0x0a)
            {
                writer->write(TJS_N("\\n"));
            }
            else if (ch == 0x0d)
            {
                writer->write(TJS_N("\\r"));
            }
            else if (ch == 0x09)
            {
                writer->write(TJS_N("\\t"));
            }
            else if (ch < 0x20)
            {
                char buf[256];
                std::snprintf(buf, 255, "\\u%04x", ch);
                writer->write(buf);
            }
            else
            {
                writer->write((tjs_char)ch);
            }
        }
        writer->write((tjs_char)'"');
    }
    else
    {
        writer->write(TJS_N("\"\""));
    }
}

static void getVariantString(tTJSVariant& var, IWriter* writer);

/**
 * 辞書の内容表示用の呼び出しロジック
 */
class DictMemberDispCaller : public tTJSDispatch /** EnumMembers 用 */
{
protected:
    IWriter* writer;
    bool first;

public:
    DictMemberDispCaller(IWriter* writer) : writer(writer) { first = true; };
    virtual tjs_error FuncCall(     // function invocation
        tjs_uint32 flag,            // calling flag
        const tjs_char* membername, // member name ( NULL for a default member )
        tjs_uint32* hint,           // hint for the member name (in/out)
        tTJSVariant* result,        // result
        tjs_int numparams,          // number of parameters
        tTJSVariant** param,        // parameters
        iTJSDispatch2* objthis      // object as "this"
    )
    {
        if (numparams > 1)
        {
            tTVInteger flag = param[1]->AsInteger();
            if (!(flag & TJS_HIDDENMEMBER))
            {
                if (first)
                {
                    first = false;
                }
                else
                {
                    writer->write((tjs_char)',');
                    writer->newline();
                }
                quoteString(param[0]->GetString(), writer);
                writer->write((tjs_char)':');
                getVariantString(*param[2], writer);
            }
        }
        if (result)
        {
            *result = true;
        }
        return TJS_S_OK;
    }
};

static void getDictString(iTJSDispatch2* dict, IWriter* writer)
{
    writer->write((tjs_char)'{');
    writer->addIndent();
    DictMemberDispCaller* caller = new DictMemberDispCaller(writer);
    tTJSVariantClosure closure(caller);
    dict->EnumMembers(TJS_IGNOREPROP, &closure, dict);
    caller->Release();
    writer->delIndent();
    writer->write((tjs_char)'}');
}

static void getArrayString(iTJSDispatch2* array, IWriter* writer)
{
    writer->write((tjs_char)'[');
    writer->addIndent();
    tjs_int count = 0;
    {
        tTJSVariant result;
        if (TJS_SUCCEEDED(ArrayCountProp->PropGet(0, NULL, NULL, &result, array)))
        {
            count = result.AsInteger();
        }
    }
    for (tjs_int i = 0; i < count; i++)
    {
        if (i != 0)
        {
            writer->write((tjs_char)',');
            writer->newline();
        }
        tTJSVariant result;
        if (array->PropGetByNum(TJS_IGNOREPROP, i, &result, array) == TJS_S_OK)
        {
            getVariantString(result, writer);
        }
    }
    writer->delIndent();
    writer->write((tjs_char)']');
}

static void getVariantString(tTJSVariant& var, IWriter* writer)
{
    switch (var.Type())
    {

        case tvtVoid:
            writer->write(TJS_N("null"));
            break;

        case tvtObject:
        {
            iTJSDispatch2* obj = var.AsObjectNoAddRef();
            if (obj == NULL)
            {
                writer->write(TJS_N("null"));
            }
            else if (obj->IsInstanceOf(TJS_IGNOREPROP, NULL, NULL, TJS_N("Array"), obj) ==
                     TJS_S_TRUE)
            {
                getArrayString(obj, writer);
            }
            else
            {
                getDictString(obj, writer);
            }
        }
        break;

        case tvtString:
            quoteString(var.GetString(), writer);
            break;

        case tvtInteger:
            writer->write((tTVInteger)var);
            break;

        case tvtReal:
        {
            ttstr str = var;
            // delete top '+' of number.
            if (str[0] == '+')
            {
                ttstr src = str;
                str = src.c_str() + 1;
            }
            writer->write(str.c_str());
            break;
        }

        default:
            writer->write(TJS_N("null"));
            break;
    };
}

/**
 *
 */
class tSaveJSON : public tTJSDispatch
{
protected:
public:
    tjs_error FuncCall(tjs_uint32 flag,
                       const tjs_char* membername,
                       tjs_uint32* hint,
                       tTJSVariant* result,
                       tjs_int numparams,
                       tTJSVariant** param,
                       iTJSDispatch2* objthis)
    {
        if (numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        IFileWriter writer(param[0]->GetString(), numparams > 2 ? (int)*param[2] != 0 : false,
                           numparams > 3 ? (int)*param[3] : 0);
        getVariantString(*param[1], &writer);
        return TJS_S_OK;
    }
};

/**
 *
 */
class tToJSONString : public tTJSDispatch
{
protected:
public:
    tjs_error FuncCall(tjs_uint32 flag,
                       const tjs_char* membername,
                       tjs_uint32* hint,
                       tTJSVariant* result,
                       tjs_int numparams,
                       tTJSVariant** param,
                       iTJSDispatch2* objthis)
    {
        if (numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        if (result)
        {
            IStringWriter writer(numparams > 1 ? (tjs_int)*param[1] : 0);
            getVariantString(*param[0], &writer);
            *result = writer.buf;
        }
        return TJS_S_OK;
    }
};

//---------------------------------------------------------------------------
void json_init()
{
    // Arary クラスメンバー取得
    {
        tTJSVariant varScripts;
        TVPExecuteExpression(TJS_N("Array"), &varScripts);
        iTJSDispatch2* dispatch = varScripts.AsObjectNoAddRef();
        // メンバ取得
        ArrayCountProp = getMember(dispatch, TJS_N("count"));
    }

    {
        tTJSVariant varScripts;
        TVPExecuteExpression(TJS_N("Scripts"), &varScripts);
        iTJSDispatch2* dispatch = varScripts.AsObjectNoAddRef();
        if (dispatch)
        {
            addMethod(dispatch, TJS_N("evalJSON"), new tEvalJSON());
            addMethod(dispatch, TJS_N("evalJSONStorage"), new tEvalJSONStorage());
            addMethod(dispatch, TJS_N("saveJSON"), new tSaveJSON());
            addMethod(dispatch, TJS_N("toJSONString"), new tToJSONString());
        }
    }
}

void json_done()
{
    tTJSVariant varScripts;
    TVPExecuteExpression(TJS_N("Scripts"), &varScripts);
    iTJSDispatch2* dispatch = varScripts.AsObjectNoAddRef();
    if (dispatch)
    {
        delMethod(dispatch, TJS_N("evalJSON"));
        delMethod(dispatch, TJS_N("evalJSONStorage"));
    }
}

NCB_PRE_REGIST_CALLBACK(json_init);
NCB_POST_UNREGIST_CALLBACK(json_done);