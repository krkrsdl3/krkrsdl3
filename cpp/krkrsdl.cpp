// SDL2 desktop entry point — traditional main() loop
#include <SDL2/SDL.h>
#include <GL/glew.h>

#include <map>
#include <vector>

#include "TVPApplication.h"
#include "RenderManager.h"
#include "MainWindowLayer.h"

#include "eventCallbackFun.h"

#ifndef _DEBUG
#ifdef _KRKRSDL3_WINDOWS
#include <windows.h>
#endif
#endif

SDL_Window* tvp_window;
static SDL_GLContext tvp_glContext = NULL;

// forward declarations
static void sdl_process_events(bool& running);
void sdl_render_frame();

int main(int argc, char* argv[])
{
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
    {
        SDL_Log("Fail to initialize SDL: %s", SDL_GetError());
        return 1;
    }

    // 窗口
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    tvp_window = SDL_CreateWindow("TVP Engine",
                                  SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  1280, 720,
                                  SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    if (!tvp_window)
    {
        SDL_Log("Failed to create window: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    tvp_glContext = SDL_GL_CreateContext(tvp_window);
    if (tvp_glContext == NULL)
    {
        SDL_Log("Failed to create GL context: %s", SDL_GetError());
        SDL_DestroyWindow(tvp_window);
        SDL_Quit();
        return 1;
    }

    // GLEW 初始化
    glewExperimental = GL_TRUE;
    GLenum glewErr = glewInit();
    if (glewErr != GLEW_OK)
    {
        SDL_Log("Failed to initialize GLEW: %s", glewGetErrorString(glewErr));
        SDL_DestroyWindow(tvp_window);
        SDL_Quit();
        return 1;
    }

    SDL_GL_MakeCurrent(tvp_window, tvp_glContext);
    SDL_GL_SetSwapInterval(1);
    // GL相关信息初始化
    krkrsdl3::fetchGLInfo();

    // 启动游戏
    if (argc < 2)
    {
        SDL_Log("At least two parameters are required.");
        SDL_DestroyWindow(tvp_window);
        SDL_Quit();
        return 1;
    }
    if (!::Application->StartApplication(argc, argv))
    {
        SDL_Log("Game Start Failed.");
        SDL_DestroyWindow(tvp_window);
        SDL_Quit();
        return 1;
    }

    // 隐藏命令行
#ifndef _DEBUG
#ifdef _KRKRSDL3_WINDOWS
    ShowWindow(GetConsoleWindow(), SW_HIDE);
#endif
#endif
    SDL_ShowWindow(tvp_window);

    // 初始帧
    sdl_render_frame();
    refreshWindow();

    // 主循环
    bool running = true;
    while (running)
    {
        sdl_process_events(running);
        sdl_render_frame();
    }

    SDL_DestroyWindow(tvp_window);
    SDL_Log("Game quit successfully!");
    SDL_Quit();
    return 0;
}

std::map<SDL_Sprite*, callbackOnKeyDownUpEvent> sdl_keyDownCallback;
std::map<SDL_Sprite*, callbackOnKeyDownUpEvent> sdl_keyUpCallback;
std::map<SDL_Sprite*, callbackOnMouseDownEvent> sdl_mouseDownCallback;
std::map<SDL_Sprite*, callbackOnMouseUpEvent> sdl_mouseUpCallback;
std::map<SDL_Sprite*, callbackOnMouseMoveEvent> sdl_mouseMoveCallback;
std::map<SDL_Sprite*, callbackOnMouseScroll> sdl_mouseScrollCallback;
std::mutex sdlCallbackMtx;
std::vector<SDL_Sprite*> renderTexture;
std::mutex sdlRenderMtx;
static SDL_FRect rectBuff;

static void sdl_process_events(bool& running)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
    switch (event.type)
    {
            // 退出
        case SDL_QUIT:
            running = false;
            return;
            // 窗口事件 (SDL2: SDL_WINDOWEVENT + sub-events)
        case SDL_WINDOWEVENT:
            break;
            // 键盘事件
        case SDL_KEYDOWN:
        {
            if (event.key.keysym.scancode == SDL_SCANCODE_F1)
            {
                int x = 0, y = 0;
                SDL_GetWindowPosition(tvp_window, &x, &y);
                krkrsdl3::SDL_Invoke_Menu(x, y);
                break;
            }
            // 确认键→鼠标模拟 (for KAG [waitclick])
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
            // 检查modal对象
            bool hasModal = false;
            for (auto callback : sdl_keyDownCallback)
            {
                if (callback.first->type == 1 && callback.first->isVisible)
                    hasModal = true;
            }
            // 写入缓冲区
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
            // 确认键→鼠标模拟 release
            if (event.key.keysym.scancode == SDL_SCANCODE_RETURN ||
                event.key.keysym.scancode == SDL_SCANCODE_SPACE)
            {
                int mx, my;
                SDL_GetMouseState(&mx, &my);
                std::lock_guard<std::mutex> lock(sdlCallbackMtx);
                for (auto it = sdl_mouseUpCallback.rbegin(); it != sdl_mouseUpCallback.rend(); ++it)
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
            // 检查modal对象
            bool hasModal = false;
            for (auto callback : sdl_keyUpCallback)
            {
                if (callback.first->type == 1 && callback.first->isVisible)
                    hasModal = true;
            }
            // 写入缓冲区
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
            // 鼠标事件
        case SDL_MOUSEBUTTONDOWN:
        {
            tTVPMouseButton tmp = mbX1;
            switch (event.button.button)
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
                std::lock_guard<std::mutex> lock(sdlCallbackMtx);
                // 检查modal对象
                bool hasModal = false;
                for (auto callback : sdl_mouseDownCallback)
                {
                    if (callback.first->type == 1 && callback.first->isVisible)
                        hasModal = true;
                }
                // 写入缓冲区
                for (auto it = sdl_mouseDownCallback.rbegin(); it != sdl_mouseDownCallback.rend(); ++it)
                {
                    auto callback = *it;
                    if (hasModal)
                    {
                        if (callback.first->type == 1)
                        {
                            callback.second(
                                tmp,
                                (event.button.x - callback.first->xPos) / callback.first->scale,
                                (event.button.y - callback.first->yPos) / callback.first->scale);
                            break;
                        }
                    }
                    else
                    {
                        if (callback.first->isVisible)
                        {
                            callback.second(
                                tmp,
                                (event.button.x - callback.first->xPos) / callback.first->scale,
                                (event.button.y - callback.first->yPos) / callback.first->scale);
                        }
                    }
                }
            }
            break;
        }
        case SDL_MOUSEBUTTONUP:
        {
            tTVPMouseButton tmp = mbX1;
            switch (event.button.button)
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
                std::lock_guard<std::mutex> lock(sdlCallbackMtx);
                // 检查modal对象
                bool hasModal = false;
                for (auto callback : sdl_mouseUpCallback)
                {
                    if (callback.first->type == 1 && callback.first->isVisible)
                        hasModal = true;
                }
                // 写入缓冲区
                for (auto it = sdl_mouseUpCallback.rbegin(); it != sdl_mouseUpCallback.rend(); ++it)
                {
                    auto callback = *it;
                    if (hasModal)
                    {
                        if (callback.first->type == 1)
                        {
                            callback.second(
                                tmp,
                                (event.button.x - callback.first->xPos) / callback.first->scale,
                                (event.button.y - callback.first->yPos) / callback.first->scale);
                            break;
                        }
                    }
                    else
                    {
                        if (callback.first->isVisible)
                        {
                            callback.second(
                                tmp,
                                (event.button.x - callback.first->xPos) / callback.first->scale,
                                (event.button.y - callback.first->yPos) / callback.first->scale);
                        }
                    }
                }
            }
            break;
        }
        case SDL_MOUSEMOTION:
        {
            std::lock_guard<std::mutex> lock(sdlCallbackMtx);
            // 检查modal对象
            bool hasModal = false;
            for (auto callback : sdl_mouseMoveCallback)
            {
                if (callback.first->type == 1 && callback.first->isVisible)
                    hasModal = true;
            }
            // 写入缓冲区
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
            std::lock_guard<std::mutex> lock(sdlCallbackMtx);
            // 检查modal对象
            bool hasModal = false;
            for (auto callback : sdl_mouseScrollCallback)
            {
                if (callback.first->type == 1 && callback.first->isVisible)
                    hasModal = true;
            }
            // 写入缓冲区
            for (auto it = sdl_mouseScrollCallback.rbegin(); it != sdl_mouseScrollCallback.rend(); ++it)
            {
                auto callback = *it;
                if (hasModal)
                {
                    if (callback.first->type == 1)
                    {
                        callback.second(event.wheel.x, event.wheel.y, event.wheel.x,
                                        event.wheel.y);
                        break;
                    }
                }
                else
                {
                    if (callback.first->isVisible)
                    {
                        callback.second(event.wheel.x, event.wheel.y, event.wheel.x,
                                        event.wheel.y);
                    }
                }
            }
            break;
        }
        default:
            // 自定义事件 (menu click 等)
            if (event.type >= SDL_USEREVENT)
            {
                krkrsdl3::SDL_Trig_Menu(event.user.data1);
            }
            break;
    }
    } // while PollEvent
}

static const int sdl_drawOrder[] = {0, 2, 1};  // 窗口 -> overlay -> modal
void sdl_render_frame()
{
    ::Application->Run();
    iTVPTexture2D::RecycleProcess();

    // 写入缓冲区
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
    // 渲染
    SDL_GL_SwapWindow(tvp_window);
}

// (SDL_Fail / SDL_AppQuit removed — cleanup is in main())
