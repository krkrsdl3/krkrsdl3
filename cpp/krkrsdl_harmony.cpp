/*
 * krkrsdl_harmony.cpp - HarmonyOS entry point for krkr2 engine
 *
 * Replaces krkrsdl.cpp when building as a shared library (.so) for
 * the VintagePomelo HarmonyOS app. Exports the standard engine_loader
 * interface: runner_main(), requestShutdown(), cleanupSDL(), etc.
 *
 * The host app (VintagePomelo) creates the XComponent, which SDL_OHOS
 * maps to an EGL surface. We create the SDL window here and drive the
 * krkr2 TJS2 engine main loop.
 */

#include <SDL2/SDL.h>
#include <GLES3/gl3.h>

#include <map>
#include <vector>
#include <string>
#include <thread>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "tjsCommHead.h"
#include "TVPApplication.h"
#include "RenderManager.h"
#include "MainWindowLayer.h"
#include "TVPSystem.h"
#include "SystemControl.h"

#include "eventCallbackFun.h"

#ifdef __OHOS__
#include <hilog/log.h>
#define KRKR2_LOG(fmt, ...) \
    OH_LOG_Print(LOG_APP, LOG_INFO, 0xFF00, "krkr2", fmt, ##__VA_ARGS__)
#define KRKR2_ERR(fmt, ...) \
    OH_LOG_Print(LOG_APP, LOG_ERROR, 0xFF00, "krkr2", fmt, ##__VA_ARGS__)
#else
#define KRKR2_LOG(fmt, ...) SDL_Log("[krkr2] " fmt, ##__VA_ARGS__)
#define KRKR2_ERR(fmt, ...) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[krkr2] " fmt, ##__VA_ARGS__)
#endif

// ============================================================
// KR2ExitException - thrown by TVPExitApplication on HarmonyOS
// instead of calling exit(). Caught in runner_main.
// ============================================================
class KR2ExitException : public std::exception {
public:
    int code;
    KR2ExitException(int c) : code(c) {}
    const char* what() const noexcept override { return "KR2ExitApplication"; }
};

// ============================================================
// Globals shared with krkrsdl_gl.cpp, event handlers, etc.
// ============================================================
SDL_Window* tvp_window = nullptr;
static SDL_GLContext tvp_glContext = nullptr;

std::map<SDL_Sprite*, callbackOnKeyDownUpEvent> sdl_keyDownCallback;
std::map<SDL_Sprite*, callbackOnKeyDownUpEvent> sdl_keyUpCallback;
std::map<SDL_Sprite*, callbackOnMouseDownEvent> sdl_mouseDownCallback;
std::map<SDL_Sprite*, callbackOnMouseUpEvent> sdl_mouseUpCallback;
std::map<SDL_Sprite*, callbackOnMouseMoveEvent> sdl_mouseMoveCallback;
std::map<SDL_Sprite*, callbackOnMouseScroll> sdl_mouseScrollCallback;
std::mutex sdlCallbackMtx;
std::vector<SDL_Sprite*> renderTexture;
std::mutex sdlRenderMtx;

// Engine state used by KR2Entry / engine_loader interface
static std::string g_krkr2_game_path;
static std::string g_krkr2_data_dir;
static volatile int g_krkr2_shutdown_requested = 0;
static volatile int g_krkr2_running = 0;
static char g_krkr2_last_error[2048] = "";

// External engine globals
extern std::thread::id TVPMainThreadID;
extern bool TVPTerminated;
extern bool TVPSystemUninitCalled;
extern void TVPSystemUninit(void);
extern void TVPTerminateAsync(int code);

// ============================================================
// TVPGetDefaultFileDir - returns writable data dir
// ============================================================
std::string TVPGetDefaultFileDir()
{
    if (!g_krkr2_data_dir.empty())
        return g_krkr2_data_dir;
    return "./";
}

// Forward declarations for re-entry reset functions
#ifdef __OHOS__
extern void TVPResetSystemForReentry();
extern void TVPResetScriptForReentry();
extern void TVPResetStorageForReentry();
extern void TVPResetApplicationForReentry();
#endif

// ============================================================
// Two-finger touch → right-click state machine (SDL2 / HarmonyOS)
// Single finger  → left click + mouse move
// Two fingers    → right click
// ============================================================
namespace {

enum HarmonyTouchState {
    HTS_IDLE,
    HTS_SINGLE,  // 单指 → 左键
    HTS_MULTI,   // 多指 → 右键
};

struct HarmonyFinger {
    SDL_FingerID id;
    float x, y;
    float startX, startY;
    bool active;
    bool moved;
    HarmonyFinger() : id(0), x(0), y(0), startX(0), startY(0), active(false), moved(false) {}
};

static HarmonyTouchState g_hts = HTS_IDLE;
static std::map<SDL_FingerID, HarmonyFinger> g_hfingers;

static void hts_toPixel(float nx, float ny, int& px, int& py)
{
    int w = 1, h = 1;
    if (tvp_window) SDL_GetWindowSize(tvp_window, &w, &h);
    px = (int)(nx * w);
    py = (int)(ny * h);
}

static void hts_sendDown(tTVPMouseButton btn, int px, int py)
{
    std::lock_guard<std::mutex> lock(sdlCallbackMtx);
    bool hasModal = false;
    for (auto& cb : sdl_mouseDownCallback)
        if (cb.first->type == 1 && cb.first->isVisible) hasModal = true;
    for (auto it = sdl_mouseDownCallback.rbegin(); it != sdl_mouseDownCallback.rend(); ++it) {
        auto& cb = *it;
        if (hasModal) {
            if (cb.first->type == 1) {
                cb.second(btn, (px - cb.first->xPos) / cb.first->scale,
                               (py - cb.first->yPos) / cb.first->scale);
                break;
            }
        } else {
            if (cb.first->isVisible) {
                cb.second(btn, (px - cb.first->xPos) / cb.first->scale,
                               (py - cb.first->yPos) / cb.first->scale);
                break;
            }
        }
    }
}

static void hts_sendUp(tTVPMouseButton btn, int px, int py)
{
    std::lock_guard<std::mutex> lock(sdlCallbackMtx);
    bool hasModal = false;
    for (auto& cb : sdl_mouseUpCallback)
        if (cb.first->type == 1 && cb.first->isVisible) hasModal = true;
    for (auto it = sdl_mouseUpCallback.rbegin(); it != sdl_mouseUpCallback.rend(); ++it) {
        auto& cb = *it;
        if (hasModal) {
            if (cb.first->type == 1) {
                cb.second(btn, (px - cb.first->xPos) / cb.first->scale,
                               (py - cb.first->yPos) / cb.first->scale);
                break;
            }
        } else {
            if (cb.first->isVisible) {
                cb.second(btn, (px - cb.first->xPos) / cb.first->scale,
                               (py - cb.first->yPos) / cb.first->scale);
                break;
            }
        }
    }
}

static void hts_sendMove(int px, int py)
{
    std::lock_guard<std::mutex> lock(sdlCallbackMtx);
    bool hasModal = false;
    for (auto& cb : sdl_mouseMoveCallback)
        if (cb.first->type == 1 && cb.first->isVisible) hasModal = true;
    for (auto it = sdl_mouseMoveCallback.rbegin(); it != sdl_mouseMoveCallback.rend(); ++it) {
        auto& cb = *it;
        if (hasModal) {
            if (cb.first->type == 1) {
                cb.second((px - cb.first->xPos) / cb.first->scale,
                           (py - cb.first->yPos) / cb.first->scale);
                break;
            }
        } else {
            if (cb.first->isVisible) {
                cb.second((px - cb.first->xPos) / cb.first->scale,
                           (py - cb.first->yPos) / cb.first->scale);
            }
        }
    }
}

static void handleTouchFingerDown(const SDL_TouchFingerEvent& e)
{
    HarmonyFinger f;
    f.id      = e.fingerId;
    f.x = f.startX = e.x;
    f.y = f.startY = e.y;
    f.active  = true;
    f.moved   = false;
    g_hfingers[e.fingerId] = f;

    if (g_hfingers.size() == 1) {
        g_hts = HTS_SINGLE;
    } else if (g_hfingers.size() == 2) {
        g_hts = HTS_MULTI;
    } else {
        // 3+ 指 → 暂不处理，清空
        g_hfingers.clear();
        g_hts = HTS_IDLE;
    }
}

static void handleTouchFingerUp(const SDL_TouchFingerEvent& e)
{
    auto it = g_hfingers.find(e.fingerId);
    if (it == g_hfingers.end()) return;

    HarmonyFinger& f = it->second;
    f.active = false;

    // 只有最后一根手指抬起时才触发 click
    if (g_hfingers.size() == 1) {
        int px, py;
        hts_toPixel(f.x, f.y, px, py);
        if (g_hts == HTS_SINGLE) {
            if (!f.moved) hts_sendDown(mbLeft, px, py);
            hts_sendUp(mbLeft, px, py);
        } else if (g_hts == HTS_MULTI) {
            if (!f.moved) hts_sendDown(mbRight, px, py);
            hts_sendUp(mbRight, px, py);
        }
        g_hts = HTS_IDLE;
    }
    g_hfingers.erase(it);
}

static void handleTouchFingerMotion(const SDL_TouchFingerEvent& e)
{
    auto it = g_hfingers.find(e.fingerId);
    if (it == g_hfingers.end()) return;

    HarmonyFinger& f = it->second;
    float dx = e.x - f.startX, dy = e.y - f.startY;
    if (dx * dx + dy * dy > 0.0001f) {
        f.moved = true;
        f.x = e.x;
        f.y = e.y;
        if (g_hts == HTS_SINGLE) {
            int px, py;
            hts_toPixel(f.x, f.y, px, py);
            hts_sendMove(px, py);
        }
    }
}

} // namespace

// ============================================================
// SDL event processing (adapted from krkrsdl.cpp for touch)
// ============================================================
static void sdl_process_events(bool& running)
{
    // Poll portrait offset env var for live adjustment
    {
        static int s_cached_portrait_offset = -1;
        int cur = 0;
        const char *env = getenv("TAPIR_PORTRAIT_TOP_OFFSET");
        if (env) cur = atoi(env);
        if (cur != s_cached_portrait_offset) {
            s_cached_portrait_offset = cur;
            KRKR2_LOG("Portrait offset changed to %{public}d", cur);
        }
    }

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_QUIT:
            running = false;
            return;

        case SDL_WINDOWEVENT:
            break;

        case SDL_KEYDOWN:
        {
            // Enter/Space → simulate left mouse click (for KAG [waitclick])
            if (event.key.keysym.scancode == SDL_SCANCODE_RETURN ||
                event.key.keysym.scancode == SDL_SCANCODE_SPACE)
            {
                int mx, my;
                SDL_GetMouseState(&mx, &my);
                std::lock_guard<std::mutex> lock(sdlCallbackMtx);
                for (auto it = sdl_mouseDownCallback.rbegin(); it != sdl_mouseDownCallback.rend(); ++it)
                {
                    auto callback = *it;
                    if (callback.first->isVisible)
                    {
                        callback.second(
                            mbLeft,
                            (mx - callback.first->xPos) / callback.first->scale,
                            (my - callback.first->yPos) / callback.first->scale);
                        break;
                    }
                }
                break;
            }
            std::lock_guard<std::mutex> lock(sdlCallbackMtx);
            bool hasModal = false;
            for (auto callback : sdl_keyDownCallback)
            {
                if (callback.first->type == 1 && callback.first->isVisible)
                    hasModal = true;
            }
            for (auto it = sdl_keyDownCallback.rbegin(); it != sdl_keyDownCallback.rend(); ++it)
            {
                auto callback = *it;
                if (hasModal)
                {
                    if (callback.first->type == 1)
                    {
                        callback.second(event.key.keysym.scancode);
                        break;
                    }
                }
                else
                {
                    if (callback.first->isVisible)
                    {
                        callback.second(event.key.keysym.scancode);
                    }
                }
            }
            break;
        }

        case SDL_KEYUP:
        {
            std::lock_guard<std::mutex> lock(sdlCallbackMtx);
            bool hasModal = false;
            for (auto callback : sdl_keyUpCallback)
            {
                if (callback.first->type == 1 && callback.first->isVisible)
                    hasModal = true;
            }
            for (auto it = sdl_keyUpCallback.rbegin(); it != sdl_keyUpCallback.rend(); ++it)
            {
                auto callback = *it;
                if (hasModal)
                {
                    if (callback.first->type == 1)
                    {
                        callback.second(event.key.keysym.scancode);
                        break;
                    }
                }
                else
                {
                    if (callback.first->isVisible)
                    {
                        callback.second(event.key.keysym.scancode);
                    }
                }
            }
            break;
        }

        case SDL_MOUSEBUTTONDOWN:
        {
            tTVPMouseButton btn = mbLeft;
            if (event.button.button == SDL_BUTTON_RIGHT) btn = mbRight;
            else if (event.button.button == SDL_BUTTON_MIDDLE) btn = mbMiddle;
            std::lock_guard<std::mutex> lock(sdlCallbackMtx);
            bool hasModal = false;
            for (auto callback : sdl_mouseDownCallback)
            {
                if (callback.first->type == 1 && callback.first->isVisible)
                    hasModal = true;
            }
            for (auto it = sdl_mouseDownCallback.rbegin(); it != sdl_mouseDownCallback.rend(); ++it)
            {
                auto callback = *it;
                if (hasModal)
                {
                    if (callback.first->type == 1)
                    {
                        callback.second(btn,
                            (event.button.x - callback.first->xPos) / callback.first->scale,
                            (event.button.y - callback.first->yPos) / callback.first->scale);
                        break;
                    }
                }
                else
                {
                    if (callback.first->isVisible)
                    {
                        callback.second(btn,
                            (event.button.x - callback.first->xPos) / callback.first->scale,
                            (event.button.y - callback.first->yPos) / callback.first->scale);
                        break;
                    }
                }
            }
            break;
        }

        case SDL_MOUSEBUTTONUP:
        {
            tTVPMouseButton btn = mbLeft;
            if (event.button.button == SDL_BUTTON_RIGHT) btn = mbRight;
            else if (event.button.button == SDL_BUTTON_MIDDLE) btn = mbMiddle;
            std::lock_guard<std::mutex> lock(sdlCallbackMtx);
            bool hasModal = false;
            for (auto callback : sdl_mouseUpCallback)
            {
                if (callback.first->type == 1 && callback.first->isVisible)
                    hasModal = true;
            }
            for (auto it = sdl_mouseUpCallback.rbegin(); it != sdl_mouseUpCallback.rend(); ++it)
            {
                auto callback = *it;
                if (hasModal)
                {
                    if (callback.first->type == 1)
                    {
                        callback.second(btn,
                            (event.button.x - callback.first->xPos) / callback.first->scale,
                            (event.button.y - callback.first->yPos) / callback.first->scale);
                        break;
                    }
                }
                else
                {
                    if (callback.first->isVisible)
                    {
                        callback.second(btn,
                            (event.button.x - callback.first->xPos) / callback.first->scale,
                            (event.button.y - callback.first->yPos) / callback.first->scale);
                        break;
                    }
                }
            }
            break;
        }

        case SDL_MOUSEMOTION:
        {
            std::lock_guard<std::mutex> lock(sdlCallbackMtx);
            bool hasModal = false;
            for (auto callback : sdl_mouseMoveCallback)
            {
                if (callback.first->type == 1 && callback.first->isVisible)
                    hasModal = true;
            }
            for (auto it = sdl_mouseMoveCallback.rbegin(); it != sdl_mouseMoveCallback.rend(); ++it)
            {
                auto callback = *it;
                if (hasModal)
                {
                    if (callback.first->type == 1)
                    {
                        callback.second(
                            (event.motion.x - callback.first->xPos) / callback.first->scale,
                            (event.motion.y - callback.first->yPos) / callback.first->scale);
                        break;
                    }
                }
                else
                {
                    if (callback.first->isVisible)
                    {
                        callback.second(
                            (event.motion.x - callback.first->xPos) / callback.first->scale,
                            (event.motion.y - callback.first->yPos) / callback.first->scale);
                    }
                }
            }
            break;
        }

        case SDL_MOUSEWHEEL:
        {
            int mx, my;
            SDL_GetMouseState(&mx, &my);
            std::lock_guard<std::mutex> lock(sdlCallbackMtx);
            for (auto it = sdl_mouseScrollCallback.rbegin(); it != sdl_mouseScrollCallback.rend(); ++it)
            {
                auto callback = *it;
                if (callback.first->isVisible)
                {
                    callback.second(
                        (mx - callback.first->xPos) / callback.first->scale,
                        (my - callback.first->yPos) / callback.first->scale,
                        event.wheel.x, event.wheel.y);
                    break;
                }
            }
            break;
        }

        case SDL_FINGERDOWN:
            handleTouchFingerDown(event.tfinger);
            break;

        case SDL_FINGERUP:
            handleTouchFingerUp(event.tfinger);
            break;

        case SDL_FINGERMOTION:
            handleTouchFingerMotion(event.tfinger);
            break;

        default:
            break;
        }
    }
}

// ============================================================
// Rendering (same as krkrsdl.cpp)
// ============================================================
static const int sdl_drawOrder[] = {0, 2, 1};  // window -> overlay -> modal
void sdl_render_frame()
{
    ::Application->Run();
    iTVPTexture2D::RecycleProcess();

    int RW = 1280, RH = 720;
    SDL_GetWindowSize(tvp_window, &RW, &RH);
    krkrsdl3::SDL_GL_BaseSet(RW, RH);
    {
        std::lock_guard<std::mutex> lock(sdlRenderMtx);
        for (int type : sdl_drawOrder)
        {
            for (int i = renderTexture.size() - 1; i >= 0; --i)
            {
                auto texture = renderTexture[i];
                if (texture->isVisible && texture->type == type)
                {
                    krkrsdl3::SDL_GL_DrawTexture(texture, RW, RH);
                }
            }
        }
    }
    SDL_GL_SwapWindow(tvp_window);
}

// Called by MainWindowLayer.cpp
void refreshWindow()
{
    if (tvp_window)
        SDL_ShowWindow(tvp_window);
}

// ============================================================
// Exported C interface for engine_loader
// ============================================================

extern "C" {

/**
 * runner_main - Main entry point called by engine_loader via dlsym.
 *
 * @param argc  >= 2
 * @param argv  argv[0] = program name, argv[1] = game path,
 *              argv[2] = data dir (optional, writable sandbox)
 * @return 0 on success
 */
__attribute__((visibility("default")))
int runner_main(int argc, char** argv)
{
    KRKR2_LOG("runner_main called, argc=%{public}d", argc);

    if (argc < 2 || !argv[1]) {
        KRKR2_ERR("Usage: krkr2 <game_path> [data_dir]");
        snprintf(g_krkr2_last_error, sizeof(g_krkr2_last_error),
                 "No game path provided");
        return -1;
    }

    // ---- Reset engine state for re-entry ----
    // The engine was designed as one-run-per-process. On HarmonyOS the .so
    // stays loaded across game launches, so we must reset all one-shot
    // flags and stale global state before each run.
    KRKR2_LOG("Resetting engine state for re-entry");
    TVPResetSystemForReentry();
    TVPResetScriptForReentry();
    TVPResetStorageForReentry();
    TVPResetApplicationForReentry();
    TVPSystemControl = nullptr;
    TVPSystemControlAlive = false;

    g_krkr2_game_path = argv[1];
    if (argc >= 3 && argv[2]) {
        g_krkr2_data_dir = argv[2];
    } else {
        g_krkr2_data_dir = g_krkr2_game_path;
    }

    KRKR2_LOG("Game path: %{public}s", g_krkr2_game_path.c_str());
    KRKR2_LOG("Data dir:  %{public}s", g_krkr2_data_dir.c_str());

    g_krkr2_shutdown_requested = 0;
    g_krkr2_running = 1;
    g_krkr2_last_error[0] = '\0';

    // Set main thread
    TVPMainThreadID = std::this_thread::get_id();

    // ---- SDL window + GL context ----
    // On HarmonyOS, SDL_Init is invoked by host app (SDL_OHOS).
    // We still need to create window & GL context ourselves.

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    // 禁用 SDL 自动 touch→mouse 转换，由我们的手势状态机手动处理
    // （支持双指右键、单指左键+拖动）
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");

    tvp_window = SDL_CreateWindow(
        "krkr2",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        0, 0,
        SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!tvp_window) {
        KRKR2_ERR("SDL_CreateWindow failed: %{public}s", SDL_GetError());
        snprintf(g_krkr2_last_error, sizeof(g_krkr2_last_error),
                 "SDL_CreateWindow failed: %s", SDL_GetError());
        g_krkr2_running = 0;
        return -1;
    }
    {
        int w = 0, h = 0;
        SDL_GetWindowSize(tvp_window, &w, &h);
        KRKR2_LOG("SDL window created: %{public}dx%{public}d", w, h);
    }

    tvp_glContext = SDL_GL_CreateContext(tvp_window);
    if (!tvp_glContext) {
        KRKR2_ERR("SDL_GL_CreateContext failed: %{public}s", SDL_GetError());
        snprintf(g_krkr2_last_error, sizeof(g_krkr2_last_error),
                 "SDL_GL_CreateContext failed: %s", SDL_GetError());
        SDL_DestroyWindow(tvp_window);
        tvp_window = nullptr;
        g_krkr2_running = 0;
        return -1;
    }
    SDL_GL_MakeCurrent(tvp_window, tvp_glContext);
    SDL_GL_SetSwapInterval(1);

    KRKR2_LOG("GL Version: %{public}s", (const char*)glGetString(GL_VERSION));
    KRKR2_LOG("GL Renderer: %{public}s", (const char*)glGetString(GL_RENDERER));

    // Initialize GL info for shader setup
    krkrsdl3::fetchGLInfo();

    // Don't show keyboard on startup
    SDL_StopTextInput();

    // ---- Start the TJS2 engine ----
    int result = 0;
    bool started = false;

    try {
        KRKR2_LOG("Starting application with path: %{public}s", g_krkr2_game_path.c_str());
        started = ::Application->StartApplication(argc, argv);
        if (started) {
            KRKR2_LOG("StartApplication succeeded");
        } else {
            KRKR2_ERR("StartApplication returned false");
            snprintf(g_krkr2_last_error, sizeof(g_krkr2_last_error),
                     "Failed to start TJS2 application");
            result = -1;
        }
    } catch (const KR2ExitException& e) {
        KRKR2_LOG("Engine exited during startup (code=%{public}d)", e.code);
        result = e.code;
        started = false;
    } catch (const std::exception& e) {
        KRKR2_ERR("StartApplication exception: %{public}s", e.what());
        snprintf(g_krkr2_last_error, sizeof(g_krkr2_last_error),
                 "Engine exception: %s", e.what());
        result = -1;
        started = false;
    } catch (...) {
        KRKR2_ERR("StartApplication unknown exception");
        snprintf(g_krkr2_last_error, sizeof(g_krkr2_last_error),
                 "Unknown engine exception during startup");
        result = -1;
        started = false;
    }

    // ---- Main loop ----
    if (started) {
        KRKR2_LOG("Entering main loop");
        SDL_ShowWindow(tvp_window);

        // Initial render
        sdl_render_frame();
        refreshWindow();

        bool running = true;
        int frameCount = 0;
        while (running && !TVPTerminated && !g_krkr2_shutdown_requested)
        {
            try {
                sdl_process_events(running);
                sdl_render_frame();
            } catch (const KR2ExitException& e) {
                KRKR2_LOG("Engine exited during Run() (code=%{public}d)", e.code);
                result = e.code;
                break;
            }

            frameCount++;
            if (frameCount == 1 || frameCount % 300 == 0) {
                KRKR2_LOG("Frame %{public}d, TVPTerminated=%{public}d", frameCount, TVPTerminated ? 1 : 0);
            }

            SDL_Delay(1);  // Yield; VSync handles pacing
        }

        KRKR2_LOG("Main loop exited (TVPTerminated=%{public}d, shutdown=%{public}d)",
                   TVPTerminated ? 1 : 0, g_krkr2_shutdown_requested);
    }

    // ---- Cleanup ----
    if (!TVPSystemUninitCalled) {
        KRKR2_LOG("Calling TVPSystemUninit...");
        try {
            TVPSystemUninit();
        } catch (const KR2ExitException& e) {
            KRKR2_LOG("TVPSystemUninit threw KR2ExitException (code=%{public}d)", e.code);
        } catch (...) {
            KRKR2_ERR("TVPSystemUninit exception");
        }
    }

    // Safety net: ensure ALL at-exit handlers run (especially thread joins).
    // TVPSystemUninit calls TVPCauseAtExit internally, but if it threw before
    // reaching that call, background threads (WatchThread, TimerThread,
    // WaveSoundBufferThread) would still be alive.  When the host app later
    // pops the game page, the .so code gets unmapped and those threads
    // SIGSEGV.  This call is idempotent — guarded by TVPAtExitShutdown flag.
    KRKR2_LOG("Safety-net: TVPForceCauseAtExit...");
    try {
        TVPForceCauseAtExit();
    } catch (...) {
        KRKR2_ERR("TVPForceCauseAtExit exception");
    }

    if (::Application) {
        KRKR2_LOG("Deleting Application...");
        delete ::Application;
        ::Application = nullptr;
    }

    // Stop audio before closing GL
    KRKR2_LOG("Shutting down SDL audio...");
    SDL_QuitSubSystem(SDL_INIT_AUDIO);

    SDL_StopTextInput();
    if (tvp_glContext) {
        SDL_GL_DeleteContext(tvp_glContext);
        tvp_glContext = nullptr;
    }
    if (tvp_window) {
        SDL_DestroyWindow(tvp_window);
        tvp_window = nullptr;
    }

    // Clear all callback maps to prevent dangling refs on re-entry
    {
        std::lock_guard<std::mutex> lock(sdlCallbackMtx);
        sdl_keyDownCallback.clear();
        sdl_keyUpCallback.clear();
        sdl_mouseDownCallback.clear();
        sdl_mouseUpCallback.clear();
        sdl_mouseMoveCallback.clear();
        sdl_mouseScrollCallback.clear();
    }
    {
        std::lock_guard<std::mutex> lock(sdlRenderMtx);
        renderTexture.clear();
    }

    KRKR2_LOG("runner_main cleanup complete, result=%{public}d", result);
    g_krkr2_running = 0;
    return result;
}

/**
 * requestShutdown - Request the engine to stop gracefully
 */
__attribute__((visibility("default")))
void requestShutdown(void)
{
    KRKR2_LOG("Shutdown requested");
    g_krkr2_shutdown_requested = 1;
    TVPTerminateAsync(0);
}

/**
 * isShutdownRequested - Check if shutdown has been requested
 */
__attribute__((visibility("default")))
int isShutdownRequested(void)
{
    return g_krkr2_shutdown_requested;
}

/**
 * cleanupSDL - Clean up SDL resources and reset state for re-entry
 */
__attribute__((visibility("default")))
void cleanupSDL(void)
{
    KRKR2_LOG("cleanupSDL called");

    if (!TVPSystemUninitCalled) {
        try {
            TVPSystemUninit();
        } catch (...) {
            KRKR2_ERR("cleanupSDL: TVPSystemUninit threw exception");
        }
    }

    g_krkr2_game_path.clear();
    g_krkr2_data_dir.clear();
    g_krkr2_shutdown_requested = 0;
    g_krkr2_running = 0;
}

/**
 * isRunning - Check if the engine is currently running
 */
__attribute__((visibility("default")))
int isRunning(void)
{
    return g_krkr2_running;
}

/**
 * get_last_ruby_error - Get the last engine error message
 * (Named for compatibility with engine_loader interface)
 */
__attribute__((visibility("default")))
const char* get_last_ruby_error(void)
{
    return g_krkr2_last_error[0] != '\0' ? g_krkr2_last_error : NULL;
}

} // extern "C"
