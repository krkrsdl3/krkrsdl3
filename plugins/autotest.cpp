//---------------------------------------------------------------------------
// AutoTest : 离屏自动化测试脚本接口（tests/ 下的 .tjs 宿主使用）
//
// 能力全部转调 environ 侧的测试宿主（PlatformAutoTest.h），本文件只做 TJS 绑定：
//   AutoTest.capture(path) / write(path, text)
//   AutoTest.pump(n) / frame() / size() / title() / offscreen()
//   AutoTest.checksum() / brightness()   最近一次抓帧的指纹与平均亮度，用来判断画面是否变了
//   AutoTest.click(x, y) 等              合成鼠标（坐标是窗口像素坐标）
//   AutoTest.tapKey(name) / type(text)   合成键盘与文字输入（键名为扫描码名）
//   AutoTest.log(n) / lastError() / quit()
// 宿主脚本先 Plugins.link("autotest.dll") 取用；正常进游戏不会加载这个模块。
// ENABLE_AUTOTEST=OFF 时 CMake 不编译本文件，模块也不会注册。
//---------------------------------------------------------------------------
#if defined(_KRKRSDL3_USE_SDL3) && !defined(_KRKRSDL3_OHOS) && defined(_KRKRSDL3_AUTOTEST)

#include "ncbind/ncbind.hpp"

#include <cstdio>
#include <string>

#include "PlatformAutoTest.h"
#include "PlatformView.h"
#include "TextStream.h"
#include "TVPApplication.h"
#include "TVPDebug.h"
#include "TVPStorage.h"

#define NCB_MODULE_NAME TJS_N("autotest.dll")

using namespace krkrsdl3;

class AutoTest
{
public:
    // 抓一帧存成图片（png / bmp 按扩展名）：先跑一帧，刚注入的输入会算进去
    static bool capture(ttstr path)
    {
        TVPAutoTestArmCapture();
        TVPAutoTestStepFrames(1);
        return TVPAutoTestSaveFrame(path.c_str());
    }

    // 把文本写到本地文件（宿主自己的报告 / 断言结果）
    static bool write(ttstr path, ttstr text)
    {
        iTJSTextWriteStream* stream =
            TVPCreateTextStreamForWrite(TVPNormalizeStorageName(path), TJS_N(""));
        try
        {
            stream->Write(text);
        }
        catch (...)
        {
            stream->Destruct();
            throw;
        }
        stream->Destruct();
        return true;
    }

    static tjs_int pump(tjs_int frames)
    {
        return TVPAutoTestStepFrames(frames);
    }

    static tjs_int frame()
    {
        return static_cast<tjs_int>(TVPAutoTestFrameCount());
    }

    static ttstr size()
    {
        int w = 0, h = 0;
        TVPGetWindowSize(&w, &h);
        return ttstr(w) + TJS_N("x") + ttstr(h);
    }

    static ttstr title()
    {
        const std::string text = TVPGetWindowTitle();
        return ttstr(text.c_str());
    }

    // 最近一次 capture 那一帧的 RGB 指纹（16 位十六进制）：两次不同就说明画面真的变了
    static ttstr checksum()
    {
        char text[17];
        std::snprintf(text, sizeof(text), "%016llx",
                      static_cast<unsigned long long>(TVPAutoTestChecksum()));
        return ttstr(text);
    }

    // 最近一次 capture 那一帧的平均亮度 0-255，-1 表示还没抓过帧
    static tjs_int brightness()
    {
        return TVPAutoTestBrightness();
    }

    static void moveTo(tjs_int x, tjs_int y)
    {
        TVPAutoTestWarpMouse(x, y);
    }

    static void mouseDown(tjs_int x, tjs_int y)
    {
        TVPAutoTestMouseButton(true, 0, x, y);
    }

    static void mouseUp(tjs_int x, tjs_int y)
    {
        TVPAutoTestMouseButton(false, 0, x, y);
    }

    // 按下后空跑两帧再松开，和游戏里一次真实点击的节奏一致
    static void click(tjs_int x, tjs_int y)
    {
        tapAt(x, y, 0);
    }

    static void rightClick(tjs_int x, tjs_int y)
    {
        tapAt(x, y, 1);
    }

    static void doubleClick(tjs_int x, tjs_int y)
    {
        tapAt(x, y, 0);
        TVPAutoTestStepFrames(1);
        tapAt(x, y, 0);
    }

    static void wheel(tjs_int x, tjs_int y, tjs_int dx, tjs_int dy)
    {
        TVPAutoTestMouseWheel(x, y, static_cast<float>(dx), static_cast<float>(dy));
    }

    static bool keyDown(ttstr key)
    {
        return TVPAutoTestKeyDown(key.c_str(), true);
    }

    static bool keyUp(ttstr key)
    {
        return TVPAutoTestKeyDown(key.c_str(), false);
    }

    static bool tapKey(ttstr key, tjs_int hold)
    {
        if (!TVPAutoTestKeyDown(key.c_str(), true))
            return false;
        if (hold < 1)
            hold = 1;
        TVPAutoTestStepFrames(hold);
        TVPAutoTestKeyDown(key.c_str(), false);
        return true;
    }

    static void type(ttstr text)
    {
        TVPAutoTestTextInput(text.c_str());
    }

    static ttstr log(tjs_int lines)
    {
        if (lines < 0)
            lines = 0;
        return TVPGetLastLog(static_cast<tjs_uint>(lines));
    }

    static ttstr lastError()
    {
        return ttstr(TVPAutoTestLastError());
    }

    static bool offscreen()
    {
        return TVPAutoTestOffscreen();
    }

    static void quit()
    {
        if (Application)
            Application->Terminate();
    }

private:
    static void tapAt(tjs_int x, tjs_int y, tjs_int button)
    {
        TVPAutoTestWarpMouse(x, y);
        TVPAutoTestMouseButton(true, button, x, y);
        TVPAutoTestStepFrames(2);
        TVPAutoTestMouseButton(false, button, x, y);
        TVPAutoTestStepFrames(1);
    }
};

NCB_REGISTER_CLASS(AutoTest)
{
    NCB_METHOD(capture);
    NCB_METHOD(write);
    NCB_METHOD(pump);
    NCB_METHOD(frame);
    NCB_METHOD(size);
    NCB_METHOD(title);
    NCB_METHOD(checksum);
    NCB_METHOD(brightness);
    NCB_METHOD(moveTo);
    NCB_METHOD(mouseDown);
    NCB_METHOD(mouseUp);
    NCB_METHOD(click);
    NCB_METHOD(rightClick);
    NCB_METHOD(doubleClick);
    NCB_METHOD(wheel);
    NCB_METHOD(keyDown);
    NCB_METHOD(keyUp);
    NCB_METHOD(tapKey);
    NCB_METHOD(type);
    NCB_METHOD(log);
    NCB_METHOD(lastError);
    NCB_METHOD(offscreen);
    NCB_METHOD(quit);
}

#endif // _KRKRSDL3_USE_SDL3 && !_KRKRSDL3_OHOS && _KRKRSDL3_AUTOTEST
