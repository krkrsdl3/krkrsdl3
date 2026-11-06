#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "tjsCommHead.h"
#include "tjs.h"
#include "tjsDebug.h"
#include "tjsArray.h"
#include "tjsRandomGenerator.h"
#include "tjsScriptBlock.h"
#include "tjsDateParser.h"
#include "tjsByteCodeLoader.h"

#include "console_init.cpp"

tTJS* TVPScriptEngine;
ttstr TVPStartupScriptName(TJS_N("startup.tjs"));

namespace
{
//---------------------------------------------------------------------------
// In-memory binary stream used to collect the compiled byte code.
//---------------------------------------------------------------------------
class tTVPMemoryStream : public TJS::tTJSBinaryStream
{
public:
    tjs_uint64 Seek(tjs_int64 offset, tjs_int whence) override
    {
        tjs_int64 base = 0;
        if (whence == TJS_BS_SEEK_CUR)
            base = (tjs_int64)Position;
        else if (whence == TJS_BS_SEEK_END)
            base = (tjs_int64)Buffer.size();
        tjs_int64 target = base + offset;
        if (target < 0)
            target = 0;
        Position = (size_t)target;
        return Position;
    }

    tjs_uint Read(void* buffer, tjs_uint read_size) override
    {
        size_t avail = Position < Buffer.size() ? Buffer.size() - Position : 0;
        size_t n = read_size < avail ? read_size : avail;
        if (n)
            memcpy(buffer, Buffer.data() + Position, n);
        Position += n;
        return (tjs_uint)n;
    }

    tjs_uint Write(const void* buffer, tjs_uint write_size) override
    {
        if (Position + write_size > Buffer.size())
            Buffer.resize(Position + write_size);
        memcpy(Buffer.data() + Position, buffer, write_size);
        Position += write_size;
        return write_size;
    }

    bool Flush() override { return true; }

    tjs_uint64 GetSize() override { return Buffer.size(); }

    const std::vector<tjs_uint8>& GetBuffer() const { return Buffer; }

private:
    std::vector<tjs_uint8> Buffer;
    size_t Position = 0;
};
//---------------------------------------------------------------------------
bool ReadFileBytes(const char* path, std::vector<char>& bytes)
{
    FILE* fp = fopen(path, "rb");
    if (!fp)
        return false;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size < 0)
    {
        fclose(fp);
        return false;
    }
    bytes.resize((size_t)size + 1);
    if (size && fread(bytes.data(), 1, (size_t)size, fp) != (size_t)size)
    {
        fclose(fp);
        return false;
    }
    bytes[(size_t)size] = 0;
    fclose(fp);
    return true;
}
//---------------------------------------------------------------------------
bool DecodeScript(const std::vector<char>& bytes, ttstr& out)
{
    const unsigned char* p = (const unsigned char*)bytes.data();
    size_t n = bytes.empty() ? 0 : bytes.size() - 1; // ReadFileBytes appends NUL

    if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF)
    {
        p += 3;
        n -= 3;
    }
    else if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE)
    {
        // UTF-16LE -> UTF-8
        size_t count = (n - 2) / 2;
        std::vector<tjs_wchar> wide(count + 1);
        for (size_t i = 0; i < count; i++)
            wide[i] = (tjs_wchar)((unsigned char)p[2 + i * 2] |
                                  ((unsigned char)p[3 + i * 2] << 8));
        wide[count] = 0;
        std::vector<char> utf8(count * 4 + 1);
        tjs_int64 len = TVPWideCharToUtf8String(wide.data(), utf8.data());
        utf8[(size_t)len] = 0;
        out = ttstr(utf8.data(), (int)len);
        return true;
    }
    else if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF)
    {
        // UTF-16BE -> UTF-8
        size_t count = (n - 2) / 2;
        std::vector<tjs_wchar> wide(count + 1);
        for (size_t i = 0; i < count; i++)
            wide[i] = (tjs_wchar)(((unsigned char)p[2 + i * 2] << 8) |
                                  (unsigned char)p[3 + i * 2]);
        wide[count] = 0;
        std::vector<char> utf8(count * 4 + 1);
        tjs_int64 len = TVPWideCharToUtf8String(wide.data(), utf8.data());
        utf8[(size_t)len] = 0;
        out = ttstr(utf8.data(), (int)len);
        return true;
    }

    // Without a BOM the game's text stream tries UTF-8, then Shift-JIS, then
    // GBK.  Mirror that order so the corpus is read exactly like the game reads
    // it (same characters, same byte offsets as far as the parser cares).
    std::vector<tjs_wchar> wide(n + 1);
    if (TVPUtf8ToWideCharString((const char*)p, (tjs_uint)n, wide.data()) >= 0)
    {
        out = ttstr((const tjs_char*)p, (int)n);
        return true;
    }

#ifdef _WIN32
    static const UINT codepages[] = { 932, 936 };
    for (UINT cp : codepages)
    {
        int wlen = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, (LPCCH)p, (int)n, NULL, 0);
        if (wlen <= 0)
            continue;
        std::vector<wchar_t> w((size_t)wlen);
        MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, (LPCCH)p, (int)n, w.data(), wlen);
        int ulen = WideCharToMultiByte(CP_UTF8, 0, w.data(), wlen, NULL, 0, NULL, NULL);
        std::vector<char> u((size_t)ulen + 1);
        WideCharToMultiByte(CP_UTF8, 0, w.data(), wlen, u.data(), ulen, NULL, NULL);
        u[(size_t)ulen] = 0;
        out = ttstr(u.data(), ulen);
        return true;
    }
    // Lenient fallback for a few broken bytes: GBK replaces what it cannot map.
    {
        int wlen = MultiByteToWideChar(936, 0, (LPCCH)p, (int)n, NULL, 0);
        if (wlen > 0)
        {
            std::vector<wchar_t> w((size_t)wlen);
            MultiByteToWideChar(936, 0, (LPCCH)p, (int)n, w.data(), wlen);
            int ulen = WideCharToMultiByte(CP_UTF8, 0, w.data(), wlen, NULL, 0, NULL, NULL);
            std::vector<char> u((size_t)ulen + 1);
            WideCharToMultiByte(CP_UTF8, 0, w.data(), wlen, u.data(), ulen, NULL, NULL);
            u[(size_t)ulen] = 0;
            out = ttstr(u.data(), ulen);
            return true;
        }
    }
#endif

    out = ttstr((const tjs_char*)p, (int)n);
    return true;
}
//---------------------------------------------------------------------------
void InitCompileEngine()
{
    TVPScriptEngine = new tTJS();
    TVPScriptEngine->SetConsoleOutput(&TVPTJS2ConsoleOutputGateway);
    TJSCreateTextStreamForRead = TVPCreateTextStreamForRead;
    TJSCreateTextStreamForWrite = TVPCreateTextStreamForWrite;
    TJSCreateBinaryStreamForRead = TVPCreateBinaryStreamForRead;
    TJSCreateBinaryStreamForWrite = TVPCreateBinaryStreamForWrite;
}
//---------------------------------------------------------------------------
bool CompileOneFile(const char* input, const char* output, bool expression)
{
    std::vector<char> bytes;
    if (!ReadFileBytes(input, bytes))
    {
        printf("ERROR: cannot read %s\n", input);
        return false;
    }

    ttstr script;
    DecodeScript(bytes, script);

    tTVPMemoryStream stream;
    TVPScriptEngine->CompileScript(script.c_str(), &stream, false, false, expression, input, 0);

    FILE* fp = fopen(output, "wb");
    if (!fp)
    {
        printf("ERROR: cannot write %s\n", output);
        return false;
    }
    const std::vector<tjs_uint8>& data = stream.GetBuffer();
    if (!data.empty())
        fwrite(data.data(), 1, data.size(), fp);
    fclose(fp);
    return true;
}
//---------------------------------------------------------------------------
int RunLoadCheck(const char* input)
{
    InitCompileEngine();
    std::vector<char> bytes;
    if (!ReadFileBytes(input, bytes))
        return 1;
    try
    {
        TJS::tTJSByteCodeLoader loader;
        TJS::tTJSScriptBlock* block = loader.ReadByteCode(
            TVPScriptEngine, TJS_N("check"), (const tjs_uint8*)bytes.data(), bytes.size() - 1);
        if (!block)
        {
            printf("LOAD_FAIL %s\n", input);
            return 1;
        }
        block->Release();
        printf("LOAD_OK %s\n", input);
    }
    catch (const TJS::eTJS& e)
    {
        printf("LOAD_ERR %s %s\n", input, (const char*)e.GetMessage().c_str());
        return 2;
    }
    return 0;
}
//---------------------------------------------------------------------------
int RunCompileBatch(const char* list_path, const char* out_dir)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    InitCompileEngine();

    FILE* list = fopen(list_path, "rb");
    if (!list)
    {
        printf("ERROR: cannot read %s\n", list_path);
        return 1;
    }

    int index = 0;
    int ok = 0;
    int failed = 0;
    char line[4096];
    while (fgets(line, sizeof(line), list))
    {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = 0;
        if (!len)
            continue;

        char output[4096];
        snprintf(output, sizeof(output), "%s/%06d.tjs", out_dir, index);
        try
        {
            if (CompileOneFile(line, output, false))
            {
                printf("OK %d %s\n", index, line);
                ok++;
            }
            else
            {
                printf("ERR %d %s read/write\n", index, line);
                failed++;
            }
        }
        catch (const TJS::eTJS& e)
        {
            printf("ERR %d %s %s\n", index, line, (const char*)e.GetMessage().c_str());
            failed++;
        }
        catch (const std::exception& e)
        {
            printf("ERR %d %s %s\n", index, line, e.what());
            failed++;
        }
        catch (...)
        {
            printf("ERR %d %s unknown\n", index, line);
            failed++;
        }
        index++;
    }
    fclose(list);

    printf("SUMMARY total=%d ok=%d failed=%d\n", index, ok, failed);
    return failed ? 1 : 0;
}
//---------------------------------------------------------------------------
int RunCompileMode(const char* input, const char* output, bool expression)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    InitCompileEngine();

    int rc = 0;
    try
    {
        if (!CompileOneFile(input, output, expression))
            rc = 1;
    }
    catch (const TJS::eTJSScriptError& e)
    {
        printf("compile error: %s\n", (const char*)e.GetMessage().c_str());
        rc = 2;
    }
    catch (const TJS::eTJS& e)
    {
        printf("error: %s\n", (const char*)e.GetMessage().c_str());
        rc = 2;
    }
    catch (const std::exception& e)
    {
        printf("error: %s\n", e.what());
        rc = 2;
    }
    catch (...)
    {
        printf("unknown error\n");
        rc = 2;
    }

    printf("compile: done rc=%d\n", rc);
    return rc;
}
//---------------------------------------------------------------------------
int RunDateBatch(const char* list_path, const char* out_path)
{
    FILE* list = fopen(list_path, "rb");
    if (!list)
        return 1;
    FILE* out = fopen(out_path, "wb");
    if (!out)
    {
        fclose(list);
        return 1;
    }

    char line[4096];
    while (fgets(line, sizeof(line), list))
    {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = 0;
        try
        {
            TJS::tTJSDateParser parser(line);
            fprintf(out, "%lld\n", (long long)parser.GetTime());
        }
        catch (...)
        {
            fprintf(out, "ERR\n");
        }
    }
    fclose(out);
    fclose(list);
    return 0;
}
//---------------------------------------------------------------------------
} // namespace

int main(int argc, char* argv[])
{
    if (argc >= 3 && strcmp(argv[1], "--load-check") == 0)
        return RunLoadCheck(argv[2]);
    if (argc >= 4 && strcmp(argv[1], "--parse-date") == 0)
        return RunDateBatch(argv[2], argv[3]);
    if (argc >= 4 && strcmp(argv[1], "--compile-batch") == 0)
        return RunCompileBatch(argv[2], argv[3]);
    if (argc >= 4 && strcmp(argv[1], "--compile") == 0)
        return RunCompileMode(argv[2], argv[3], argc >= 5 && strcmp(argv[4], "--expr") == 0);

    /*
    * 初始化
    */
    TVPScriptEngine = new tTJS();

    TJSGetRandomBits128 = TVPGetRandomBits128;

    // script system initialization
    TVPScriptEngine->ExecScript(ttstr(TVPInitTJSScript));

    // set console output gateway handler
    TVPScriptEngine->SetConsoleOutput(&TVPTJS2ConsoleOutputGateway);

    // set text stream functions
    TJSCreateTextStreamForRead = TVPCreateTextStreamForRead;
    TJSCreateTextStreamForWrite = TVPCreateTextStreamForWrite;

    // set binary stream functions
    TJSCreateBinaryStreamForRead = TVPCreateBinaryStreamForRead;
    TJSCreateBinaryStreamForWrite = TVPCreateBinaryStreamForWrite;

    /*
     * 注册函数
     */
    // register some TVP classes/objects/functions/propeties
    iTJSDispatch2* dsp;
    iTJSDispatch2* global = TVPScriptEngine->GetGlobalNoAddRef();
    tTJSVariant val;

#define REGISTER_OBJECT(classname, instance) \
    dsp = (instance); \
    val = tTJSVariant(dsp /*, dsp*/); \
    dsp->Release(); \
    global->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP, TJS_N(#classname), NULL, &val, global);

    REGISTER_OBJECT(Debug, TVPCreateNativeClass_Debug());

    /*
     * 读取脚本
     */
    FILE* file = fopen(TVPStartupScriptName.c_str(), "rb");
    if (!file)
    {
        printf("ERROR: Cannot open %s\n", TVPStartupScriptName.c_str());
        return 1;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char* buffer = (char*)malloc(size + 1);
    fread(buffer, 1, size, file);
    buffer[size] = '\0';
    fclose(file);
    ttstr script_text(buffer);
    free(buffer);

    /*
     * 执行脚本
     */
    ttstr scriptError;
    try
    {
        tTJSVariant result;
        TVPScriptEngine->ExecScript(script_text, &result);
        delete TVPScriptEngine;
    }
    catch (const TJS::eTJSScriptError& e)
    {
        ttstr& msg = scriptError;
        msg += e.GetMessage();
        const tjs_char* pszBlockName = e.GetBlockName();
        if (pszBlockName && *pszBlockName)
        {
            msg += TJS_N("\n@line(");
            tjs_char tmp[34];
            msg += TJS_int_to_str(e.GetSourceLine(), tmp);
            msg += TJS_N(") ");
            msg += pszBlockName;
        }
        msg += TJS_N("\n");
        msg += e.GetTrace();
        printf(msg.c_str());
        return 1;
    }
    catch (const TJS::eTJS& e)
    {
        scriptError = e.GetMessage();
        printf(scriptError.c_str());
        return 1;
    }
    catch (const std::exception& e)
    {
        scriptError = e.what();
        printf(scriptError.c_str());
        return 1;
    }
    catch (const tjs_char* e)
    {
        scriptError = e;
        printf(scriptError.c_str());
        return 1;
    }
    return 0;
}