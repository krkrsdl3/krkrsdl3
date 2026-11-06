#define SDL_MAIN_USE_CALLBACKS
#define SDL_MAIN_NOIMPL
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>

#include <map>
#include <vector>
#ifdef _KRKRSDL3_EMSCRIPTEN
#include <emscripten.h>
#endif

#include "TVPApplication.h"
#include "TVPWindow.h"
#include "Platform.h"
#include "PlatformAutoTest.h"
#include "WindowManager.h"
#include "TVPSettings.h"
#include "TVPCompositor.h"
#include "TVPDebug.h"

#include "backend/SWRenderBackend.h"
#ifdef _KRKRSDL3_USE_OPENGL
#ifdef _KRKRSDL3_GL
#include "glad/glad.h"
#elif defined(_KRKRSDL3_GLES)
#include "glad/glad_egl.h"
#endif
#include "backend/GLRenderBackend.h"
#endif
#ifdef _KRKRSDL3_USE_VULKAN
#include <SDL3/SDL_vulkan.h>
#include "backend/VulkanRenderBackend.h"
#endif

#ifndef _DEBUG
#ifdef _KRKRSDL3_WINDOWS
#include <windows.h>
#endif
#endif

static SDL_Window* tvp_window;
static SDL_Renderer* tvp_renderer = NULL;
static SDL_GLContext tvp_glContext = NULL;

// 按后端名创建窗口（尺寸来自 TVPSettings，-window=WxH 可配置）
static bool TVPCreateWindowForBackend(const std::string& renderer)
{
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "TVP Engine");
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, TVPSettings.window_width);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, TVPSettings.window_height);
    Uint64 flags = SDL_WINDOW_RESIZABLE;
    // -offscreen：窗口自始至终不显示，只保留渲染目标（测试宿主离屏跑）
    if (krkrsdl3::TVPAutoTestOffscreen())
        flags |= SDL_WINDOW_HIDDEN;
#if defined(_KRKRSDL3_IOS)
    flags |= SDL_WINDOW_FULLSCREEN;
    flags &= ~SDL_WINDOW_RESIZABLE;
#endif
    if (renderer == "opengl")
        flags |= SDL_WINDOW_OPENGL;
    else if (renderer == "vulkan")
    {
        flags |= SDL_WINDOW_VULKAN;
#ifdef _KRKRSDL3_USE_VULKAN
        // 加载 Vulkan loader（无 vulkan 驱动时失败）
        if (!SDL_Vulkan_LoadLibrary(nullptr))
        {
            TVPConsoleLog("Vulkan loader unavailable: %s", SDL_GetError());
            return false;
        }
#endif
    }
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, flags);
    tvp_window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (!tvp_window)
    {
        SDL_Log("Failed to create window for renderer %s: %s", renderer.c_str(), SDL_GetError());
        return false;
    }
    return true;
}

// 初始化所选渲染后端（GL 上下文 / Vulkan 设备 / SDL 软渲染）
static bool TVPInitRenderBackend()
{
    if (false) { }
#ifdef _KRKRSDL3_USE_OPENGL
    if (TVPSettings.renderer == "opengl")
    {
#ifdef _KRKRSDL3_GL
        // 使用opengl3.3
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#elif defined(_KRKRSDL3_GLES)
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#endif
        tvp_glContext = SDL_GL_CreateContext(tvp_window);
        if (tvp_glContext == NULL)
        {
            SDL_Log("Failed to create GL context: %s", SDL_GetError());
            return false;
        }
        // 使用SDL3上下文
#if !defined(_KRKRSDL3_EMSCRIPTEN)
#if defined(_KRKRSDL3_GL)
        if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
#elif defined(_KRKRSDL3_GLES)
        if (!gladLoadEGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
#else
        if (false)
#endif
        {
            SDL_Log("Failed to initialize GLAD");
            return false;
        }
#endif
        SDL_GL_MakeCurrent(tvp_window, tvp_glContext);
        SDL_GL_SetSwapInterval(TVPSettings.vsync);
        // GL相关信息初始化
        krkrsdl3::TVPSetRenderBackend(new krkrsdl3::GLRenderBackend());
        krkrsdl3::fetchGLInfo();
    }
#endif
#ifdef _KRKRSDL3_USE_VULKAN
    else if (TVPSettings.renderer == "vulkan")
    {
        //vulkan需要SDL3提供inst和surface
        uint32_t extensionCount = 0;
        const char* const* extensionNames = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
        if (!extensionNames)
        {
            TVPConsoleLog("Vulkan instance extensions unavailable: %s", SDL_GetError());
            return false;
        }
        std::vector<const char*> extensions(extensionNames, extensionNames + extensionCount);
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "krkrsdl3";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "krkrsdl3";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = (uint32_t)extensions.size();
        createInfo.ppEnabledExtensionNames = extensions.data();
        //inst
        VkInstance instance_ = VK_NULL_HANDLE;
        VkResult result = vkCreateInstance(&createInfo, nullptr, &instance_);
        if (result != VK_SUCCESS)
        {
            TVPConsoleLog("Vulkan instance creation failed: %d (driver may be unavailable)",
                          (int)result);
            return false;
        }
        // surface
        VkSurfaceKHR surface_ = VK_NULL_HANDLE;
        if (!SDL_Vulkan_CreateSurface(tvp_window, instance_, nullptr, &surface_))
        {
            TVPConsoleLog("Vulkan surface creation failed: %s", SDL_GetError());
            return false;
        }
        // 初始化
        krkrsdl3::iTVPRenderBackend* backend =
            krkrsdl3::CreateVulkanRenderBackend(instance_, surface_);
        if (!backend)
        {
            SDL_Log("Failed to initialize Vulkan backend");
            return false;
        }
        // 设置
        krkrsdl3::TVPSetRenderBackend(backend);
    }
#endif
    else
    {
        // SDL模拟软渲染器，虽然可能选择GPU，但对于App来说是软渲染
        tvp_renderer = SDL_CreateRenderer(tvp_window, NULL);
        if (tvp_renderer == NULL)
        {
            SDL_Log("Failed to create SDL renderer: %s", SDL_GetError());
            return false;
        }
        SDL_SetRenderVSync(tvp_renderer, TVPSettings.vsync);
        SDL_Log("SWRender Backend: %s", SDL_GetRendererName(tvp_renderer));
        krkrsdl3::TVPSetRenderBackend(new krkrsdl3::SWRenderBackend());
    }
    return true;
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
    // exeName gameNamey args
    if (argc < 2)
    {
        SDL_Log("At least two parameters are required.");
        return SDL_APP_FAILURE;
    }

    // 参数解析
    if (!TVPParseArguments(argc, argv))
        return SDL_APP_FAILURE;

    // 测试宿主开关（-offscreen / -silent / -frames / -dump / -at_harness）
    krkrsdl3::TVPAutoTestSetup();
    if (krkrsdl3::TVPAutoTestSilent())
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
    { // for format converter
        SDL_Log("Fail to initialize SDL.");
        return SDL_APP_FAILURE;
    }

    // 窗口（按所选后端决定标志位；创建失败时回退软渲染）
    if (!TVPCreateWindowForBackend(TVPSettings.renderer))
    {
        SDL_Log("Window creation failed for renderer %s, falling back to software.",
                TVPSettings.renderer.c_str());
        TVPSettings.renderer = "software";
        if (!TVPCreateWindowForBackend(TVPSettings.renderer))
            return SDL_APP_FAILURE;
    }
    // 渲染后端（失败时回退软渲染）
    if (!TVPInitRenderBackend())
    {
        SDL_Log("Render backend '%s' init failed, falling back to software.", TVPSettings.renderer.c_str());
        krkrsdl3::TVPShutdownRenderBackend();
        SDL_DestroyWindow(tvp_window);
        tvp_window = NULL;
        TVPSettings.renderer = "software";
        if (!TVPCreateWindowForBackend(TVPSettings.renderer) || !TVPInitRenderBackend())
            return SDL_APP_FAILURE;
    }

    // 初始化时不显示
    SDL_HideWindow(tvp_window);

    // 启动游戏
    Application = new tTVPApplication;
    if (!::Application->StartApplication())
    {
        SDL_Log("Game Start Failed.");
        return SDL_APP_FAILURE;
    }

    // -at_harness=PATH：游戏正常启动后接管，跑脚本宿主写的旅程
    krkrsdl3::TVPAutoTestRunHarness();

    // 隐藏命令行（离屏跑要留着 stdout 给测试宿主收日志）
#ifndef _DEBUG
#ifdef _KRKRSDL3_WINDOWS
    if (!krkrsdl3::TVPAutoTestOffscreen())
        ShowWindow(GetConsoleWindow(), SW_HIDE);
#endif
#endif
    if (!krkrsdl3::TVPAutoTestOffscreen())
        SDL_ShowWindow(tvp_window);

    // 初始帧数
    SDL_AppIterate(NULL);

    return SDL_APP_CONTINUE;
}

#if defined(_KRKRSDL3_ANDROID) || defined(_KRKRSDL3_EMSCRIPTEN) || defined(_KRKRSDL3_IOS)
// 触屏事件机制（Android / WASM / iOS 移动端）
//
// 移动端没有鼠标指针，只有一个"手指"，所以这里做一层 触屏 -> 鼠标 的映射：
//   1. 手指按下         -> 先把指针移到按下点（等价于桌面端把鼠标挪过来，hover 立即生效）
//   2. 按住不放并移动   -> 持续发送 MouseMove，触发 onMouseMove / Layer.onMouseMove 等 hover 逻辑
//   3. 按下后原地抬起   -> MouseDown + MouseUp，作为一次点击
//   4. 拖动后抬起       -> 不发送 MouseUp（否则引擎会额外合成一次 Click，拖动看 hover 时误翻页）
//   5. 双指按住并上下移 -> 模拟滚轮（每 SCROLL_STEP_PIXELS 像素一格），不再触发右键
//   6. 按住不动再移动   -> 先按下左键再拖动，模拟桌面端的"拖拽"（滑块、拖放等）
enum TouchState
{
    STATE_IDLE,
    STATE_SINGLE_FINGER, // 单指状态（处理左键和移动）
    STATE_MULTI_FINGER,  // 多指状态（处理右键）
    STATE_MENU
};
struct Finger
{
    SDL_FingerID id;
    float x, y;           // 归一化坐标
    float startX, startY; // 按下时的位置
    Uint64 downTime;
    Uint64 lastMoveTime; // 上一次发送 MouseMove 的时间（用于节流）
    bool active;
    bool moved;
    bool dragMoved;     // 拖拽过程中是否真的拉出过 TAP_SLOP 的距离（决定松手要不要算点击）
    bool dragCancelled; // 长按判定期内就动了：本次手势判定为 hover，不再可能变成拖拽

    Finger()
        : id(0), x(0), y(0), startX(0), startY(0), downTime(0), lastMoveTime(0), active(false),
          moved(false), dragMoved(false), dragCancelled(false)
    {
    }
};
static TouchState _state;
static std::map<SDL_FingerID, Finger> fingers;
static float rightClickX, rightClickY;
static Uint64 rightClickStartTime;
static const Uint32 RIGHT_CLICK_CONFIRM_DELAY = 150;
// 判定"点击"允许的抖动（像素）：超出即认为用户在拖动看 hover
static const float TAP_SLOP_PIXELS = 12.0f;
// 长按之后，手指移动多少像素才开始真正"按住拖拽"
static const float DRAG_START_PIXELS = 6.0f;
// 长按判定期内（前 LONG_PRESS_MS）手指漂移超过这个距离，就认定本次是 hover 而不是拖拽
static const float DRAG_CANCEL_PIXELS = 4.0f;
// MouseMove 最小发送间隔（毫秒），避免手指高速滑动时把输入队列刷爆
static const Uint64 MOVE_SEND_INTERVAL_MS = 8;
// 双指拖动多少像素算一格滚轮
static const float SCROLL_STEP_PIXELS = 40.0f;
// 按住多少毫秒之后再移动，才算"按住左键拖拽"（否则算 hover 移动）
static const Uint64 LONG_PRESS_MS = 350;
// 左键是否处于按下状态（长按拖拽期间为 true）
static bool mouseButtonHeld = false;
// 双指滚轮状态：锚点（两指平均 Y）、累计位移、本次手势是否已经滚动过
static float twoFingerAnchorY = 0.0f;
static float scrollAccumPx = 0.0f;
static bool twoFingerScrolled = false;
void sendMouseEvent(int button, int eventType, float pX, float pY, bool generateClick = true);
void sendMouseMotion(float pX, float pY);
void sendMouseScroll(int dx, int dy, float pX, float pY);
// 结束"按住左键"状态（generateClick=false 用于拖拽收尾，避免拖完又触发一次点击）
void releaseHeldButton(bool generateClick)
{
    if (!mouseButtonHeld)
        return;
    mouseButtonHeld = false;
    if (fingers.empty())
        return;
    Finger& f = fingers.begin()->second;
    sendMouseEvent(SDL_BUTTON_LEFT, SDL_EVENT_MOUSE_BUTTON_UP, f.x, f.y, generateClick);
}
void handleFingerDown(const SDL_TouchFingerEvent& e)
{
    // 上一根手指还按着左键没松开就又按下新手指：先松开左键，避免按键卡住
    releaseHeldButton(false);

    Finger f;
    f.id = e.fingerID;
    f.x = f.startX = e.x;
    f.y = f.startY = e.y;
    f.downTime = SDL_GetTicks();
    f.lastMoveTime = f.downTime;
    f.active = true;
    f.moved = false;
    f.dragMoved = false;
    f.dragCancelled = false;

    fingers[e.fingerID] = f;

    if (fingers.size() == 1)
    {
        // 单击->左键
        _state = STATE_SINGLE_FINGER;
        // 按下即把指针移到这里：安卓没有真正的 hover，
        // 这一步让"按住不放"等价于桌面端"把鼠标停在某处"，hover 立刻可见
        sendMouseMotion(f.x, f.y);
    }
    else if (fingers.size() == 2)
    {
        // 双指轻点->右键；双指按住上下拖动->滚轮
        _state = STATE_MULTI_FINGER;
        // 以两指平均位置作为滚轮锚点，同时把指针挪到两指中间，
        // 这样滚轮和右键都落在手指所在的位置，而不是上一次单指点击的残留位置
        float sumX = 0.0f, sumY = 0.0f;
        for (auto& kv : fingers)
        {
            sumX += kv.second.x;
            sumY += kv.second.y;
        }
        sumX /= (float)fingers.size();
        sumY /= (float)fingers.size();
        twoFingerAnchorY = sumY;
        scrollAccumPx = 0.0f;
        twoFingerScrolled = false;
        sendMouseMotion(sumX, sumY);
    }
    else
    {
        // 三击->菜单
        _state = STATE_MENU;
        int windowWidth, windowHeight;
        SDL_GetWindowSize(tvp_window, &windowWidth, &windowHeight);
        int pixelX = static_cast<int>(f.x * windowWidth);
        int pixelY = static_cast<int>(f.y * windowHeight);
        TVPInvokeMenu(pixelX, pixelY);
        fingers.clear();
        _state = STATE_IDLE;
        scrollAccumPx = 0.0f;
        twoFingerScrolled = false;
    }
}
void handleFingerUp(const SDL_TouchFingerEvent& e)
{
    auto it = fingers.find(e.fingerID);
    if (it == fingers.end())
        return;

    Finger& f = it->second;
    f.active = false;

    if (fingers.size() == 1)
    {
        if (_state == STATE_SINGLE_FINGER)
        {
            if (mouseButtonHeld)
            {
                // 长按拖拽收尾：补一次 Move 再发 MouseUp。
                // 拖过 TAP_SLOP 就不再合成 Click，否则"拖完滑块"会顺手变成一次点击/翻页
                f.x = e.x;
                f.y = e.y;
                sendMouseMotion(f.x, f.y);

                int windowWidth, windowHeight;
                SDL_GetWindowSize(tvp_window, &windowWidth, &windowHeight);
                float dx = (e.x - f.startX) * windowWidth;
                float dy = (e.y - f.startY) * windowHeight;
                bool isClick = !f.dragMoved &&
                               dx * dx + dy * dy <= TAP_SLOP_PIXELS * TAP_SLOP_PIXELS;

                mouseButtonHeld = false;
                sendMouseEvent(SDL_BUTTON_LEFT, SDL_EVENT_MOUSE_BUTTON_UP, f.x, f.y, isClick);
            }
            else if (!f.moved)
            {
                // 轻点：先把指针落到抬起点，再补一次按下 / 抬起，等价于桌面端的一次点击
                f.x = e.x;
                f.y = e.y;
                sendMouseMotion(f.x, f.y);
                sendMouseEvent(SDL_BUTTON_LEFT, SDL_EVENT_MOUSE_BUTTON_DOWN, f.x, f.y);
                sendMouseEvent(SDL_BUTTON_LEFT, SDL_EVENT_MOUSE_BUTTON_UP, f.x, f.y);
            }
            else
            {
                // 拖动结束：只把指针停在最后的位置。
                // 这里绝不能发 MouseUp —— 引擎的 MouseUp 会顺带合成一次 Click，
                // 拖动只是为了看 hover，误触发就会变成"翻页"。
                f.x = e.x;
                f.y = e.y;
                sendMouseMotion(f.x, f.y);
            }
        }
        else if (_state == STATE_MULTI_FINGER)
        {
            // 滚动过就不再当成右键，否则"双指滑一下"会顺手弹出右键菜单
            if (!f.moved && !twoFingerScrolled)
                sendMouseEvent(SDL_BUTTON_RIGHT, SDL_EVENT_MOUSE_BUTTON_DOWN, f.x, f.y);
            if (!twoFingerScrolled)
                sendMouseEvent(SDL_BUTTON_RIGHT, SDL_EVENT_MOUSE_BUTTON_UP, f.x, f.y);
        }
        _state = STATE_IDLE;
        scrollAccumPx = 0.0f;
        twoFingerScrolled = false;
    }

    fingers.erase(it);
}
// 手势被系统/窗口打断（来电、切后台、系统返回手势等）
// 必须把手指从表里摘掉，否则残留的 finger 会让下一次触摸被当成"双指"
void handleFingerCancel(const SDL_TouchFingerEvent& e)
{
    auto it = fingers.find(e.fingerID);
    if (it == fingers.end())
        return;

    // 拖拽过程中被系统打断：先把左键松开
    releaseHeldButton(false);

    fingers.erase(it);

    if (fingers.empty())
    {
        _state = STATE_IDLE;
        scrollAccumPx = 0.0f;
        twoFingerScrolled = false;
    }
    else if (fingers.size() == 1)
    {
        // 还剩一根手指：继续按单指处理，但标记为已移动，抬起时不要误判成点击
        _state = STATE_SINGLE_FINGER;
        fingers.begin()->second.moved = true;
    }
    else
    {
        _state = STATE_MULTI_FINGER;
    }
}
void handleFingerMotion(const SDL_TouchFingerEvent& e)
{
    auto it = fingers.find(e.fingerID);
    if (it == fingers.end())
        return;

    Finger& f = it->second;

    if (_state == STATE_MULTI_FINGER && fingers.size() == 2)
    {
        // 双指按住并移动 -> 滚轮
        f.x = e.x;
        f.y = e.y;

        int windowWidth, windowHeight;
        SDL_GetWindowSize(tvp_window, &windowWidth, &windowHeight);

        float avgX = 0.0f, avgY = 0.0f;
        for (auto& kv : fingers)
        {
            avgX += kv.second.x;
            avgY += kv.second.y;
        }
        avgX /= (float)fingers.size();
        avgY /= (float)fingers.size();

        float deltaPx = (avgY - twoFingerAnchorY) * windowHeight;
        twoFingerAnchorY = avgY;
        scrollAccumPx += deltaPx;

        // 一次 FINGER_MOTION 最多发一格，剩下的留在累加器里，避免快速滑动一次喷出十几格
        if (scrollAccumPx >= SCROLL_STEP_PIXELS)
        {
            // 手指向下 = 内容跟着下来 = 相当于滚轮向上
            sendMouseScroll(0, 1, avgX, avgY);
            scrollAccumPx -= SCROLL_STEP_PIXELS;
            twoFingerScrolled = true;
        }
        else if (scrollAccumPx <= -SCROLL_STEP_PIXELS)
        {
            sendMouseScroll(0, -1, avgX, avgY);
            scrollAccumPx += SCROLL_STEP_PIXELS;
            twoFingerScrolled = true;
        }

        if (scrollAccumPx > SCROLL_STEP_PIXELS)
            scrollAccumPx = SCROLL_STEP_PIXELS;
        else if (scrollAccumPx < -SCROLL_STEP_PIXELS)
            scrollAccumPx = -SCROLL_STEP_PIXELS;
        return;
    }

    if (_state != STATE_SINGLE_FINGER)
    {
        // 多指 / 菜单状态不产生 Move，只记录位置
        f.x = e.x;
        f.y = e.y;
        return;
    }

    int windowWidth, windowHeight;
    SDL_GetWindowSize(tvp_window, &windowWidth, &windowHeight);
    float moveDx = (e.x - f.startX) * windowWidth;
    float moveDy = (e.y - f.startY) * windowHeight;
    float moveDist2 = moveDx * moveDx + moveDy * moveDy;
    Uint64 now = SDL_GetTicks();

    // 长按判定期内就移动了 -> 本次手势是 hover，后面再怎么按住也不会变成拖拽
    if (!mouseButtonHeld && !f.moved && !f.dragCancelled && now - f.downTime < LONG_PRESS_MS &&
        moveDist2 > DRAG_CANCEL_PIXELS * DRAG_CANCEL_PIXELS)
        f.dragCancelled = true;

    // 拖拽期间记录是否真的拉开过距离（松手时用它决定要不要合成 Click）
    if (mouseButtonHeld && moveDist2 > TAP_SLOP_PIXELS * TAP_SLOP_PIXELS)
        f.dragMoved = true;

    // 按住不放超过 LONG_PRESS_MS 再移动 -> 该次手势按"按住左键拖拽"处理
    // （滑块、拖放之类需要 onMouseDown 之后才有 onMouseMove 的逻辑）
    if (!mouseButtonHeld && !f.moved && !f.dragCancelled && now - f.downTime >= LONG_PRESS_MS)
    {
        if (moveDist2 < DRAG_START_PIXELS * DRAG_START_PIXELS)
            return; // 还没真的动，继续等
        mouseButtonHeld = true;
        f.dragMoved = moveDist2 > TAP_SLOP_PIXELS * TAP_SLOP_PIXELS;
        // 在按下的位置按下左键，再把指针移到当前位置
        sendMouseEvent(SDL_BUTTON_LEFT, SDL_EVENT_MOUSE_BUTTON_DOWN, f.startX, f.startY);
        f.moved = true;
        f.x = e.x;
        f.y = e.y;
        f.lastMoveTime = now;
        sendMouseMotion(f.x, f.y);
        return;
    }

    if (!f.moved)
    {
        if (moveDist2 < TAP_SLOP_PIXELS * TAP_SLOP_PIXELS)
            return; // 还在点击容差内：不产生 Move，轻轻一点不会让指针乱跳
        f.moved = true;
    }

    f.x = e.x;
    f.y = e.y;

    // 节流：手指快速划动时一帧会产生多个 FINGER_MOTION，全发会挤满输入队列
    if (now - f.lastMoveTime < MOVE_SEND_INTERVAL_MS)
        return;
    f.lastMoveTime = now;

    // 按住不放时的移动 = 鼠标 Move（hover / 拖拽过程中的移动）
    sendMouseMotion(f.x, f.y);
}

void sendMouseEvent(int button, int eventType, float pX, float pY, bool generateClick)
{
    int windowWidth, windowHeight;
    SDL_GetWindowSize(tvp_window, &windowWidth, &windowHeight);
    int pixelX = static_cast<int>(pX * windowWidth);
    int pixelY = static_cast<int>(pY * windowHeight);

    tTVPMouseButton tmp = mbX1;
    switch (button)
    {
        case SDL_BUTTON_RIGHT:
            tmp = mbRight;
            break;
        case SDL_BUTTON_MIDDLE:
            tmp = mbMiddle;
            break;
        case SDL_BUTTON_LEFT:
            tmp = mbLeft;
            break;
        default:
            break;
    }

    if (tmp != mbX1)
    {
        if (eventType == SDL_EVENT_MOUSE_BUTTON_DOWN)
        {
            krkrsdl3::KRKR_Trig_MouseDown(tmp, pixelX, pixelY);
        }
        else if (eventType == SDL_EVENT_MOUSE_BUTTON_UP)
        {
            krkrsdl3::KRKR_Trig_MouseUp(tmp, pixelX, pixelY, generateClick);
        }
    }
}
void sendMouseMotion(float pX, float pY)
{
    int windowWidth, windowHeight;
    SDL_GetWindowSize(tvp_window, &windowWidth, &windowHeight);
    int pixelX = static_cast<int>(pX * windowWidth);
    int pixelY = static_cast<int>(pY * windowHeight);
    krkrsdl3::KRKR_Trig_MouseMove(pixelX,pixelY);
}
// dx / dy 为滚轮格数（与 SDL 的 wheel.x / wheel.y 同义），pX / pY 为光标位置（归一化）
void sendMouseScroll(int dx, int dy, float pX, float pY)
{
    int windowWidth, windowHeight;
    SDL_GetWindowSize(tvp_window, &windowWidth, &windowHeight);
    int pixelX = static_cast<int>(pX * windowWidth);
    int pixelY = static_cast<int>(pY * windowHeight);
    krkrsdl3::KRKR_Trig_MouseScroll(dx, dy, pixelX, pixelY);
}
#endif

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    switch (event->type)
    {
        // 退出
        case SDL_EVENT_QUIT:
        {
            TVPWindow* tmpwind = TVPGetActiveWindow();
            if (tmpwind)
                tmpwind->Close();
            break;
        }
        // 切后台 / 失焦：清空触摸状态，避免残留手指把下一次触摸当成双指
        case SDL_EVENT_WINDOW_FOCUS_LOST:
        {
#if defined(_KRKRSDL3_ANDROID) || defined(_KRKRSDL3_EMSCRIPTEN) || defined(_KRKRSDL3_IOS)
            releaseHeldButton(false);
            fingers.clear();
            _state = STATE_IDLE;
            scrollAccumPx = 0.0f;
            twoFingerScrolled = false;
#endif
            break;
        }
        // 键盘事件
        case SDL_EVENT_KEY_DOWN:
        {
            if (event->key.scancode == SDL_SCANCODE_F1)
            {
                int x = 0, y = 0;
                SDL_GetWindowPosition(tvp_window, &x, &y);
                TVPInvokeMenu(x, y);
                break;
            }

            krkrsdl3::KRKR_Trig_KeyDown(event->key.scancode);
            break;
        }
        case SDL_EVENT_KEY_UP:
        {
            krkrsdl3::KRKR_Trig_KeyUp(event->key.scancode);
            break;
        }
        // 文字输入
        case SDL_EVENT_TEXT_INPUT:
        {
            std::string data(event->text.text);
            krkrsdl3::KRKR_Trig_TextInput(data);
            break;
        }
#if defined(_KRKRSDL3_WINDOWS) || defined(_KRKRSDL3_LINUX) || defined(_KRKRSDL3_EMSCRIPTEN)  || defined(_KRKRSDL3_MACOS)
        // 鼠标事件
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        {
            tTVPMouseButton tmp = mbX1;
            switch (event->button.button)
            {
                case SDL_BUTTON_RIGHT:
                    tmp = mbRight;
                    break;
                case SDL_BUTTON_MIDDLE:
                    tmp = mbMiddle;
                    break;
                case SDL_BUTTON_LEFT:
                    tmp = mbLeft;
                    break;
                default:
                    break;
            }

            if (tmp != mbX1)
            {
                krkrsdl3::KRKR_Trig_MouseDown(tmp, event->button.x, event->button.y);
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            tTVPMouseButton tmp = mbX1;
            switch (event->button.button)
            {
                case SDL_BUTTON_RIGHT:
                    tmp = mbRight;
                    break;
                case SDL_BUTTON_MIDDLE:
                    tmp = mbMiddle;
                    break;
                case SDL_BUTTON_LEFT:
                    tmp = mbLeft;
                    break;
                default:
                    break;
            }

            if (tmp != mbX1)
            {
                krkrsdl3::KRKR_Trig_MouseUp(tmp, event->button.x, event->button.y);
            }
            break;
        }
        case SDL_EVENT_MOUSE_MOTION:
        {
            krkrsdl3::KRKR_Trig_MouseMove(event->motion.x, event->motion.y);
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
        {
            krkrsdl3::KRKR_Trig_MouseScroll(event->wheel.x, event->wheel.y, event->wheel.x,
                                                event->wheel.y);
            break;
        }
#endif
#if defined(_KRKRSDL3_ANDROID) || defined(_KRKRSDL3_EMSCRIPTEN) || defined(_KRKRSDL3_IOS)
        // 触屏事件
        case SDL_EVENT_FINGER_DOWN:
            handleFingerDown(event->tfinger);
            break;
        case SDL_EVENT_FINGER_UP:
            handleFingerUp(event->tfinger);
            break;
        case SDL_EVENT_FINGER_MOTION:
            handleFingerMotion(event->tfinger);
            break;
        case SDL_EVENT_FINGER_CANCELED:
            handleFingerCancel(event->tfinger);
            break;
#endif
        default:
            break;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    if (!::Application->Run())
        return SDL_APP_SUCCESS;
    // 写入缓冲区
    int RW = 1280, RH = 720;
    SDL_GetWindowSize(tvp_window, &RW, &RH);
    // 合成器完成渲染（清屏/绘制/呈现全部由当前渲染后端负责）
    krkrsdl3::TVPRenderOnce(RW, RH);
    // 自动测试分支
    if (krkrsdl3::TVPAutoTestActive())
    {
        krkrsdl3::TVPAutoTestNoteFrame();
        const tjs_uint64 budget = krkrsdl3::TVPAutoTestFrameBudget();
        if (budget && krkrsdl3::TVPAutoTestFrameCount() >= budget)
            return SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_Fail()
{
    SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "Error %s", SDL_GetError());
    return SDL_APP_FAILURE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    // -dump=PATH：退出前把最后一帧落盘
    const char* dumpPath = krkrsdl3::TVPAutoTestDumpPath();
    if (dumpPath && *dumpPath && !krkrsdl3::TVPAutoTestSaveFrame(dumpPath))
        SDL_Log("Dump frame '%s' failed: %s", dumpPath, krkrsdl3::TVPAutoTestLastError());

    // 启动早期失败时 Application 可能尚未创建
    if (Application)
    {
        ::Application->OnExit();
        delete Application;
        Application = NULL;
    }
    // 后端先于上下文销毁（GL 后端析构需要上下文仍有效）
    krkrsdl3::TVPShutdownRenderBackend();
    if (tvp_glContext)
        SDL_GL_DestroyContext(tvp_glContext);
    if (tvp_renderer)
    {
        SDL_DestroyRenderer(tvp_renderer);
        tvp_renderer = NULL;
    }
    SDL_DestroyWindow(tvp_window);
#ifdef _KRKRSDL3_USE_VULKAN
    if (TVPSettings.renderer == "vulkan")
        SDL_Vulkan_UnloadLibrary();
#endif
    SDL_Log("KRKRSDL3 quit successfully!");
    SDL_Quit();
    TVPClearAllArguments();
}

void TVPSetWindowTitle(const char* title)
{
    if (!tvp_window)
        return;
    const char* override_title = SDL_getenv("KRKR_WINDOW_TITLE");
    SDL_SetWindowTitle(tvp_window, (override_title && *override_title) ? override_title : title);
}

std::string TVPGetWindowTitle()
{
    return SDL_GetWindowTitle(tvp_window);
}

void TVPSetWindowFullscreen(bool isFullscreen)
{
    SDL_SetWindowFullscreen(tvp_window, isFullscreen);
}

void TVPGetWindowSize(int* w, int* h)
{
    SDL_GetWindowSize(tvp_window, w, h);
}

void TVPGetWindowSizeInPixels(int* w, int* h)
{
    SDL_GetWindowSizeInPixels(tvp_window, w, h);
}

void TVPSetWindowSize(int w, int h)
{
    SDL_SetWindowSize(tvp_window, w, h);
}

int TVPDrawSceneOnce(int interval)
{
    static tjs_uint64 lastTick = TVPGetRoughTickCount();
    tjs_uint64 curTick = TVPGetRoughTickCount();
    int remain = interval - (curTick - lastTick);
    if (remain <= 0)
    {
        SDL_AppIterate(NULL);
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            SDL_AppEvent(NULL, &event);
        }
        lastTick = curTick;
        return 0;
    }
    else
    {
        return remain;
    }
}

void TVPShowIME(int x, int y, int w, int h)
{
    SDL_StartTextInput(tvp_window);
}

void TVPHideIME()
{
    SDL_StopTextInput(tvp_window);
}

int TVPConvertKeyCodeToVKCode(int keyCode)
{
#define CASE(x) \
    case SDL_SCANCODE_##x: \
        return VK_##x

    SDL_Scancode tmp = (SDL_Scancode)keyCode;
    switch (tmp)
    {
        CASE(0);
        CASE(1);
        CASE(2);
        CASE(3);
        CASE(4);
        CASE(5);
        CASE(6);
        CASE(7);
        CASE(8);
        CASE(9);
        CASE(A);
        CASE(B);
        CASE(C);
        CASE(D);
        CASE(E);
        CASE(F);
        CASE(G);
        CASE(H);
        CASE(I);
        CASE(J);
        CASE(K);
        CASE(L);
        CASE(M);
        CASE(N);
        CASE(O);
        CASE(P);
        CASE(Q);
        CASE(R);
        CASE(S);
        CASE(T);
        CASE(U);
        CASE(V);
        CASE(W);
        CASE(X);
        CASE(Y);
        CASE(Z);
        CASE(F1);
        CASE(F2);
        CASE(F3);
        CASE(F4);
        CASE(F5);
        CASE(F6);
        CASE(F7);
        CASE(F8);
        CASE(F9);
        CASE(F10);
        CASE(F11);
        CASE(F12);
        CASE(PAUSE);
        CASE(ESCAPE);
        CASE(CANCEL);
        CASE(INSERT);
        CASE(HOME);
        CASE(DELETE);
        CASE(END);
        CASE(SPACE);
        case SDL_SCANCODE_PRINTSCREEN:
            return VK_PRINT;
            CASE(TAB);
            CASE(RETURN);
        case SDL_SCANCODE_SCROLLLOCK:
            return VK_SCROLL;
        case SDL_SCANCODE_SYSREQ:
            return VK_SNAPSHOT;
        case SDL_SCANCODE_BACKSPACE:
            return VK_BACK;
        case SDL_SCANCODE_CAPSLOCK:
            return VK_CAPITAL;
        case SDL_SCANCODE_LSHIFT:
            return VK_SHIFT; // LR the same
        case SDL_SCANCODE_RSHIFT:
            return VK_SHIFT;
        case SDL_SCANCODE_LCTRL:
            return VK_CONTROL; // LR the same
        case SDL_SCANCODE_RCTRL:
            return VK_CONTROL;
        case SDL_SCANCODE_LALT:
            return VK_MENU;
        case SDL_SCANCODE_RALT:
            return VK_MENU;
        case SDL_SCANCODE_MENU:
            return VK_APPS;
        case SDL_SCANCODE_PAGEUP:
            return VK_PRIOR;
        case SDL_SCANCODE_PAGEDOWN:
            return VK_NEXT;
        case SDL_SCANCODE_LEFT:
            return VK_LEFT;
        case SDL_SCANCODE_RIGHT:
            return VK_RIGHT;
        case SDL_SCANCODE_UP:
            return VK_UP;
        case SDL_SCANCODE_DOWN:
            return VK_DOWN;
        case SDL_SCANCODE_NUMLOCKCLEAR:
            return VK_NUMLOCK;
        case SDL_SCANCODE_KP_PLUS:
            return VK_ADD;
        case SDL_SCANCODE_KP_MINUS:
            return VK_SUBTRACT;
        case SDL_SCANCODE_KP_MULTIPLY:
            return VK_MULTIPLY;
        case SDL_SCANCODE_KP_DIVIDE:
            return VK_DIVIDE;
        case SDL_SCANCODE_KP_ENTER:
            return VK_RETURN;
        case SDL_SCANCODE_COMMA:
            return VK_OEM_COMMA;
        case SDL_SCANCODE_MINUS:
            return VK_OEM_MINUS;
        case SDL_SCANCODE_PERIOD:
            return VK_OEM_PERIOD;
        case SDL_SCANCODE_EQUALS:
            return VK_OEM_PLUS;
        case SDL_SCANCODE_SLASH:
            return VK_OEM_2;
        case SDL_SCANCODE_SEMICOLON:
            return VK_OEM_1;
        case SDL_SCANCODE_BACKSLASH:
            return VK_OEM_5;
        case SDL_SCANCODE_LEFTBRACKET:
            return VK_OEM_4;
        case SDL_SCANCODE_RIGHTBRACKET:
            return VK_OEM_6;
        case SDL_SCANCODE_MEDIA_PLAY:
            return VK_PLAY;
        default:
            return 0;
    }
}

std::vector<std::string> TVPListAllRenderBackend()
{
    ttstr log(TJS_N("Available Render:"));
    std::vector<std::string> backends;
    int count = SDL_GetNumRenderDrivers();
    for (int i = 0; i < count; i++)
    {
        const char* name = SDL_GetRenderDriver(i);
        if (name)
        {
            backends.push_back(name);
            log += " " + ttstr((const char*)name);
        }
    }
    TVPAddImportantLog(log);

    return backends;
}

void TVPCreateTextureBackend(TVPSprite& sp)
{
    sp.texture = SDL_CreateTexture(tvp_renderer, SDL_PIXELFORMAT_ABGR8888,
                                             SDL_TEXTUREACCESS_STREAMING, sp.width, sp.height);
    SDL_BlendMode customBlendMode =
        SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE,                 // 源因子
                                   SDL_BLENDFACTOR_ZERO,                // 目标因子
                                   SDL_BLENDOPERATION_ADD,              // 混合操作
                                   SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, // 源因子（Alpha）
                                   SDL_BLENDFACTOR_SRC_ALPHA,           // 目标因子（Alpha）
                                   SDL_BLENDOPERATION_ADD               // 混合操作（Alpha）
        );
    SDL_SetTextureBlendMode((SDL_Texture*)sp.texture, customBlendMode);
}

void TVPUpdateTextureBackend(TVPSprite* sp, uint8_t* buff, int width, int height, int pitch)
{
    SDL_UpdateTexture((SDL_Texture*)sp->texture, nullptr, buff, pitch);
}

void TVPDestroyTextureBackend(TVPSprite* sp)
{
    SDL_DestroyTexture((SDL_Texture*)sp->texture);
}

void TVPRenderTextureBackend(TVPSprite* sp, int posX, int posY, int width, int height)
{
    SDL_FRect rectBuff;
    rectBuff.x = posX;
    rectBuff.y = posY;
    rectBuff.w = width;
    rectBuff.h = height;
    SDL_RenderTexture(tvp_renderer, (SDL_Texture*)sp->texture, NULL, &rectBuff);
}

//---------------------------------------------------------------------------
// 渲染后端平台钩子（由 core/render/backend 的 SW/GL 后端调用）
//---------------------------------------------------------------------------
bool TVPSoftwareRenderBackendAvailable()
{
    return true;
}

void TVPRenderClearBackend()
{
    SDL_SetRenderDrawColor(tvp_renderer, 0, 0, 0, 0);
    SDL_RenderClear(tvp_renderer);
}

void TVPRenderPresentBackend()
{
    SDL_RenderPresent(tvp_renderer);
}

void TVPSwapBuffersBackend()
{
    SDL_GL_SwapWindow(tvp_window);
}
