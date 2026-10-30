//---------------------------------------------------------------------------
// 离屏自动化测试宿主（桌面 SDL3 实现）
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include <SDL3/SDL_keyboard.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

#include "PlatformAutoTest.h"
#include "PlatformView.h"
#include "TVPCompositor.h"
#include "TVPDebug.h"
#include "TVPScript.h"
#include "TVPSettings.h"
#include "TVPStorage.h"
#include "TVPGraphicsLoader.h"
#include "LayerBitmap.h"
#include "WindowManager.h"
#include "tjsError.h"

namespace
{
bool g_offscreen = false;
bool g_silent = false;
// 本次运行是否用到了测试宿主：主循环与送显路径的唯一判断，普通进游戏恒假
bool g_active = false;
tjs_uint64 g_frameBudget = 0;
std::string g_dumpPath;
std::string g_harness;
std::string g_lastError;

// 最近一次送显的整帧（RGBA 字节序，按 pitch 行对齐）
std::vector<uint8_t> g_frame;
tjs_int g_frameWidth = 0;
tjs_int g_frameHeight = 0;
tjs_int g_framePitch = 0;
tjs_uint64 g_frameCount = 0;
bool g_captureArmed = false;

bool TVPGetCommandLineNumber(const tjs_char* name, tjs_int64& value)
{
    tTJSVariant opt;
    if (!TVPGetCommandLine(name, &opt))
        return false;
    ttstr str(opt);
    if (str == TJS_N("yes"))
        return false;
    value = str.AsInteger();
    return true;
}

bool TVPGetCommandLineString(const tjs_char* name, std::string& value)
{
    tTJSVariant opt;
    if (!TVPGetCommandLine(name, &opt))
        return false;
    ttstr str(opt);
    if (str == TJS_N("yes"))
        return false;
    value = str.c_str();
    return true;
}

bool TVPGetCommandLineFlag(const tjs_char* name)
{
    tTJSVariant opt;
    return TVPGetCommandLine(name, &opt);
}

std::string imageFormatOf(const std::string& path)
{
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos)
        return "png";
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == "bmp")
        return "bmp";
    return "png";
}

tTVPMouseButton mouseButtonOf(tjs_int button)
{
    switch (button)
    {
        case 1:
            return mbRight;
        case 2:
            return mbMiddle;
        default:
            return mbLeft;
    }
}

// 收下回读到的整帧：行序统一成紧凑行距，并清掉预约（后端约定自上而下行序）
void StoreCapturedFrame(const uint8_t* rgba, int width, int height, int pitch)
{
    g_captureArmed = false;
    if (!rgba || width <= 0 || height <= 0)
    {
        g_lastError = "frame readback returned no pixels";
        return;
    }

    g_frameWidth = width;
    g_frameHeight = height;
    g_framePitch = width * 4;
    g_frame.resize(static_cast<size_t>(g_framePitch) * height);
    for (int y = 0; y < height; ++y)
        std::memcpy(g_frame.data() + static_cast<size_t>(y) * g_framePitch,
                    rgba + static_cast<size_t>(y) * pitch, static_cast<size_t>(g_framePitch));
    // 合成缓冲的 alpha 通道可以全是 0（-holdalpha 关闭时），窗口内容本身是不透明的，
    // 截图按不透明写，否则导出图片整幅透明
    for (size_t i = 3; i < g_frame.size(); i += 4)
        g_frame[i] = 0xff;
    g_lastError.clear();
    // -dump 要的是"最后一帧"：画面不变时不会送显，所以持续预约，保证缓存始终最新
    if (!g_dumpPath.empty())
        g_captureArmed = true;
}
} // namespace

namespace krkrsdl3
{
void TVPAutoTestSetup()
{
    g_offscreen = TVPGetCommandLineFlag(TJS_N("-offscreen"));
    g_silent = TVPGetCommandLineFlag(TJS_N("-silent"));

    tjs_int64 frames = 0;
    if (TVPGetCommandLineNumber(TJS_N("-frames"), frames) && frames > 0)
        g_frameBudget = static_cast<tjs_uint64>(frames);
    TVPGetCommandLineString(TJS_N("-dump"), g_dumpPath);
    TVPGetCommandLineString(TJS_N("-at_harness"), g_harness);
    g_captureArmed = !g_dumpPath.empty();
    g_active = g_offscreen || g_silent || g_frameBudget != 0 || !g_dumpPath.empty() ||
               !g_harness.empty();
}

bool TVPAutoTestActive()
{
    return g_active;
}

bool TVPAutoTestOffscreen()
{
    return g_offscreen;
}

bool TVPAutoTestSilent()
{
    return g_silent;
}

tjs_uint64 TVPAutoTestFrameBudget()
{
    return g_frameBudget;
}

const char* TVPAutoTestDumpPath()
{
    return g_dumpPath.c_str();
}

const char* TVPAutoTestLastError()
{
    return g_lastError.c_str();
}

void TVPAutoTestArmCapture()
{
    // 预约即激活：脚本由游戏自身加载、没带任何命令行开关时也能取帧
    g_active = true;
    g_captureArmed = true;
}

void TVPAutoTestCaptureTexture(void* texture, int width, int height)
{
    // 只在按需抓取时回读整帧：每帧都回读会把离屏跑拖慢一个数量级
    if (!g_captureArmed || !texture)
        return;
    iTVPRenderBackend* backend = TVPGetRenderBackend();
    int pitch = 0;
    uint8_t* pixels = backend ? backend->LockTexture(texture, pitch) : nullptr;
    if (!pixels)
    {
        g_captureArmed = false;
        g_lastError = "renderer cannot read back the presented texture";
        return;
    }
    StoreCapturedFrame(pixels, width, height, pitch);
}

bool TVPAutoTestGetFrame(std::vector<uint8_t>& pixels, tjs_int& width, tjs_int& height,
                         tjs_int& pitch)
{
    if (g_frame.empty())
    {
        g_lastError = "no frame captured yet";
        return false;
    }
    pixels = g_frame;
    width = g_frameWidth;
    height = g_frameHeight;
    pitch = g_framePitch;
    return true;
}

tjs_uint64 TVPAutoTestChecksum()
{
    // 只对 RGB 做 FNV-1a：截图前后指纹不同就说明画面真的变了
    if (g_frame.empty())
        return 0;
    tjs_uint64 hash = 1469598103934665603ull;
    for (tjs_int y = 0; y < g_frameHeight; ++y)
    {
        const uint8_t* row = g_frame.data() + static_cast<size_t>(y) * g_framePitch;
        for (tjs_int x = 0; x < g_frameWidth; ++x)
        {
            for (tjs_int c = 0; c < 3; ++c)
            {
                hash ^= row[x * 4 + c];
                hash *= 1099511628211ull;
            }
        }
    }
    return hash;
}

tjs_int TVPAutoTestBrightness()
{
    if (g_frame.empty())
        return -1;
    tjs_uint64 sum = 0;
    for (tjs_int y = 0; y < g_frameHeight; ++y)
    {
        const uint8_t* row = g_frame.data() + static_cast<size_t>(y) * g_framePitch;
        for (tjs_int x = 0; x < g_frameWidth; ++x)
        {
            const uint8_t* p = row + x * 4;
            sum += (p[0] * 299 + p[1] * 587 + p[2] * 114) / 1000;
        }
    }
    return static_cast<tjs_int>(sum / (static_cast<tjs_uint64>(g_frameWidth) * g_frameHeight));
}

bool TVPAutoTestSaveFrame(const char* path)
{
    std::vector<uint8_t> pixels;
    tjs_int width = 0, height = 0, pitch = 0;
    if (!TVPAutoTestGetFrame(pixels, width, height, pitch))
        return false;

    try
    {
        tTVPBaseTexture image(static_cast<tjs_uint>(width), static_cast<tjs_uint>(height), 32);
        image.Update(pixels.data(), static_cast<unsigned int>(pitch), 0, 0, width, height);
        const ttstr name = TVPNormalizeStorageName(ttstr(path));
        TVPSaveImage(name, ttstr(imageFormatOf(path).c_str()), &image, nullptr);
    }
    catch (const std::exception& e)
    {
        g_lastError = e.what();
        return false;
    }
    catch (const tjs_char* e)
    {
        g_lastError = e ? e : "";
        return false;
    }
    catch (...)
    {
        g_lastError = "save frame failed";
        return false;
    }
    return true;
}

tjs_uint64 TVPAutoTestFrameCount()
{
    return g_frameCount;
}

void TVPAutoTestNoteFrame()
{
    ++g_frameCount;
}

tjs_int TVPAutoTestStepFrames(tjs_int count)
{
    // interval=0 让 TVPDrawSceneOnce 立即跑完整一帧（与模态窗口同一驱动路径）
    tjs_int stepped = 0;
    for (tjs_int i = 0; i < count; ++i)
    {
        if (TVPDrawSceneOnce(0) != 0)
            break;
        ++stepped;
        if (g_frameBudget && g_frameCount >= g_frameBudget)
            break;
    }
    return stepped;
}

void TVPAutoTestRunHarness()
{
    if (g_harness.empty())
        return;
    try
    {
        TVPExecuteStorage(TVPNormalizeStorageName(ttstr(g_harness.c_str())));
    }
    catch (const TJS::eTJSScriptError& e)
    {
        g_lastError = std::string("harness: ") + e.GetMessage().c_str();
        TVPAddImportantLog(ttstr(g_lastError.c_str()));
    }
    catch (const TJS::eTJS& e)
    {
        g_lastError = std::string("harness: ") + e.GetMessage().c_str();
        TVPAddImportantLog(ttstr(g_lastError.c_str()));
    }
    catch (const std::exception& e)
    {
        g_lastError = std::string("harness: ") + e.what();
        TVPAddImportantLog(ttstr(g_lastError.c_str()));
    }
}

void TVPAutoTestWarpMouse(tjs_int x, tjs_int y)
{
    krkrsdl3::KRKR_Trig_MouseMove(x, y);
}

void TVPAutoTestMouseButton(bool down, tjs_int button, tjs_int x, tjs_int y)
{
    const tTVPMouseButton btn = mouseButtonOf(button);
    if (down)
        krkrsdl3::KRKR_Trig_MouseDown(btn, x, y);
    else
        krkrsdl3::KRKR_Trig_MouseUp(btn, x, y);
}

void TVPAutoTestMouseWheel(tjs_int x, tjs_int y, float dx, float dy)
{
    krkrsdl3::KRKR_Trig_MouseScroll(dx, dy, x, y);
}

bool TVPAutoTestKeyDown(const char* key, bool down)
{
    const SDL_Scancode scancode = SDL_GetScancodeFromName(key);
    if (scancode == SDL_SCANCODE_UNKNOWN)
    {
        g_lastError = std::string("unknown scancode name: ") + (key ? key : "");
        return false;
    }
    if (down)
        krkrsdl3::KRKR_Trig_KeyDown(scancode);
    else
        krkrsdl3::KRKR_Trig_KeyUp(scancode);
    return true;
}

void TVPAutoTestTextInput(const char* utf8)
{
    if (utf8)
        krkrsdl3::KRKR_Trig_TextInput(std::string(utf8));
}
} // namespace krkrsdl3
