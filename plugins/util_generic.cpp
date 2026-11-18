//---------------------------------------------------------------------------
// util_generic : biscrat 系发行包里常见的小工具插件（Digest / 字节序常量）
//
// 部分游戏的 patch.tjs / utils.tjs 会用：
//   System.calculateSHA256 = function(s) { return Digest.sha256(s, tpdmLittleEndian); };
// 再把结果当作图片名映射/校验用的键。原来的 util_generic.dll 是外部 Win32 插件，
// 内核把插件全部静态编入，所以这里补一份等价实现，避免 Digest / tpdm* 找不到。
//---------------------------------------------------------------------------

#include "tjsCommHead.h"
#include "ncbind/ncbind.hpp"

#include "TVPScript.h"
#include "TVPMsg.h"

#include <cstdio>
#include <string>
#include <vector>

#include "tjsConfig.h"

#include "Kirikiroid2/7zip/Sha256.h"

#define NCB_MODULE_NAME TJS_N("util_generic.dll")

class Digest
{
public:
    /**
     * 计算字符串的 SHA-256，返回 64 位十六进制字符串
     *
     * 原版 util_generic.dll 是 UTF-16 的 TJS 环境，哈希的是 UTF-16LE 字节；
     * 本内核的 TJS 字符串是 UTF-8，所以要显式转回 UTF-16LE 才能算出同样的摘要，
     * 否则 getSHA256Name 映射出来的资源名对不上包里的文件名。
     *
     * @param str 输入字符串
     * @param endian 保留参数（0 = tpdmLittleEndian）
     */
    static ttstr sha256(ttstr str, tjs_int endian)
    {
        (void)endian;

        std::string in = str.AsStdString();

        // UTF-8 -> UTF-16（宽字符个数）
        tjs_int64 wlen = TVPUtf8ToWideCharString(in.c_str(), NULL);
        if (wlen < 0)
            wlen = 0;

        std::vector<tjs_wchar> wide((size_t)wlen + 1);
        if (wlen > 0)
            TVPUtf8ToWideCharString(in.c_str(), wide.data());

        Byte digest[SHA256_DIGEST_SIZE];
        CSha256 ctx;
        Sha256_Init(&ctx);
        Sha256_Update(&ctx, (const Byte*)wide.data(), (size_t)wlen * sizeof(tjs_wchar));
        Sha256_Final(&ctx, digest);

        char hex[SHA256_DIGEST_SIZE * 2 + 1];
        for (int i = 0; i < SHA256_DIGEST_SIZE; ++i)
            std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
        hex[SHA256_DIGEST_SIZE * 2] = 0;

        return ttstr(hex);
    }
};

NCB_REGISTER_CLASS(Digest)
{
    NCB_METHOD(sha256);
}

//---------------------------------------------------------------------------
// 全局常量：TJS 侧直接用 tpdmLittleEndian / tpdmBigEndian
//---------------------------------------------------------------------------
static void InitPlugin_UtilGeneric()
{
    iTJSDispatch2* global = TVPGetScriptDispatch();
    if (!global)
        return;

    tTJSVariant vLittle((tjs_int)0);
    global->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP, TJS_N("tpdmLittleEndian"), NULL, &vLittle,
                    global);

    tTJSVariant vBig((tjs_int)1);
    global->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP, TJS_N("tpdmBigEndian"), NULL, &vBig,
                    global);

    global->Release();
}

NCB_PRE_REGIST_CALLBACK(InitPlugin_UtilGeneric);
