#define SDL_MAIN_USE_CALLBACKS
#include "SDL3/SDL.h"
#include "SDL3/SDL_main.h"
#include "SDL3/SDL_init.h"
#include "glad/glad.h"

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

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
    { // for format converter
        SDL_Log("Fail to initialize SDL.");
        return SDL_APP_FAILURE;
    }

    // 窗口
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "TVP Engine");
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, 1280);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, 720);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER,
                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    tvp_window = SDL_CreateWindowWithProperties(props);

    // 使用opengl4.3
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    tvp_glContext = SDL_GL_CreateContext(tvp_window);
    if (tvp_glContext == NULL)
        return SDL_APP_FAILURE;
    // 使用SDL3上下文
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
    {
        SDL_Log("Failed to initialize GLAD");
        return SDL_APP_FAILURE;
    }
    SDL_GL_MakeCurrent(tvp_window, tvp_glContext);
    // GL相关信息初始化
    krkrsdl3::fetchGLInfo();

    // 初始化时不显示
    SDL_HideWindow(tvp_window);
    SDL_DestroyProperties(props);

    // 启动游戏
    if (argc < 2)
    {
        // exeName gameNamey
        SDL_Log("At least two parameters are required.");
        return SDL_APP_FAILURE;
    }
    if (!::Application->StartApplication(argc, argv))
    {
        SDL_Log("Game Start Failed.");
        return SDL_APP_FAILURE;
    }

    // 隐藏命令行
#ifndef _DEBUG
#ifdef _KRKRSDL3_WINDOWS
    ShowWindow(GetConsoleWindow(), SW_HIDE);
#endif
#endif
    SDL_ShowWindow(tvp_window);

    // 初始帧数
    SDL_AppIterate(NULL);
    // 显示window
    refreshWindow();

    return SDL_APP_CONTINUE;
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

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    switch (event->type)
    {
            // 退出
        case SDL_EVENT_QUIT:
            return SDL_APP_SUCCESS;
            // 键盘事件
        case SDL_EVENT_KEY_DOWN:
        {
            std::lock_guard<std::mutex> lock(sdlCallbackMtx);
            // 检查modal对象
            bool hasModal = false;
            for (auto callback : sdl_keyDownCallback)
            {
                if (callback.first->isModal && callback.first->isVisible)
                    hasModal = true;
            }
            // 写入缓冲区
            for (auto callback : sdl_keyDownCallback)
            {
                if (hasModal)
                {
                    if (callback.first->isModal)
                    {
                        callback.second(event->key.scancode);
                        break;
                    }
                }
                else
                {
                    if (callback.first->isVisible)
                    {
                        callback.second(event->key.scancode);
                    }
                }
            }
            break;
        }
        case SDL_EVENT_KEY_UP:
        {
            std::lock_guard<std::mutex> lock(sdlCallbackMtx);
            // 检查modal对象
            bool hasModal = false;
            for (auto callback : sdl_keyUpCallback)
            {
                if (callback.first->isModal && callback.first->isVisible)
                    hasModal = true;
            }
            // 写入缓冲区
            for (auto callback : sdl_keyUpCallback)
            {
                if (hasModal)
                {
                    if (callback.first->isModal)
                    {
                        callback.second(event->key.scancode);
                        break;
                    }
                }
                else
                {
                    if (callback.first->isVisible)
                    {
                        callback.second(event->key.scancode);
                    }
                }
            }
            break;
        }
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
                std::lock_guard<std::mutex> lock(sdlCallbackMtx);
                // 检查modal对象
                bool hasModal = false;
                for (auto callback : sdl_mouseDownCallback)
                {
                    if (callback.first->isModal && callback.first->isVisible)
                        hasModal = true;
                }
                // 写入缓冲区
                for (auto callback : sdl_mouseDownCallback)
                {
                    if (hasModal)
                    {
                        if (callback.first->isModal)
                        {
                            callback.second(
                                tmp,
                                (event->button.x - callback.first->xPos) / callback.first->scale,
                                (event->button.y - callback.first->yPos) / callback.first->scale);
                            break;
                        }
                    }
                    else
                    {
                        if (callback.first->isVisible)
                        {
                            callback.second(
                                tmp,
                                (event->button.x - callback.first->xPos) / callback.first->scale,
                                (event->button.y - callback.first->yPos) / callback.first->scale);
                        }
                    }
                }
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
                std::lock_guard<std::mutex> lock(sdlCallbackMtx);
                // 检查modal对象
                bool hasModal = false;
                for (auto callback : sdl_mouseUpCallback)
                {
                    if (callback.first->isModal && callback.first->isVisible)
                        hasModal = true;
                }
                // 写入缓冲区
                for (auto callback : sdl_mouseUpCallback)
                {
                    if (hasModal)
                    {
                        if (callback.first->isModal)
                        {
                            callback.second(
                                tmp,
                                (event->button.x - callback.first->xPos) / callback.first->scale,
                                (event->button.y - callback.first->yPos) / callback.first->scale);
                            break;
                        }
                    }
                    else
                    {
                        if (callback.first->isVisible)
                        {
                            callback.second(
                                tmp,
                                (event->button.x - callback.first->xPos) / callback.first->scale,
                                (event->button.y - callback.first->yPos) / callback.first->scale);
                        }
                    }
                }
            }
            break;
        }
        case SDL_EVENT_MOUSE_MOTION:
        {
            std::lock_guard<std::mutex> lock(sdlCallbackMtx);
            // 检查modal对象
            bool hasModal = false;
            for (auto callback : sdl_mouseMoveCallback)
            {
                if (callback.first->isModal && callback.first->isVisible)
                    hasModal = true;
            }
            // 写入缓冲区
            for (auto callback : sdl_mouseMoveCallback)
            {
                if (hasModal)
                {
                    if (callback.first->isModal)
                    {
                        callback.second(
                            (event->motion.x - callback.first->xPos) / callback.first->scale,
                            (event->motion.y - callback.first->yPos) / callback.first->scale);
                        break;
                    }
                }
                else
                {
                    if (callback.first->isVisible)
                    {
                        callback.second(
                            (event->motion.x - callback.first->xPos) / callback.first->scale,
                            (event->motion.y - callback.first->yPos) / callback.first->scale);
                    }
                }
            }
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
        {
            std::lock_guard<std::mutex> lock(sdlCallbackMtx);
            // 检查modal对象
            bool hasModal = false;
            for (auto callback : sdl_mouseScrollCallback)
            {
                if (callback.first->isModal && callback.first->isVisible)
                    hasModal = true;
            }
            // 写入缓冲区
            for (auto callback : sdl_mouseScrollCallback)
            {
                if (hasModal)
                {
                    if (callback.first->isModal)
                    {
                        callback.second(event->wheel.x, event->wheel.y, event->wheel.x,
                                        event->wheel.y);
                        break;
                    }
                }
                else
                {
                    if (callback.first->isVisible)
                    {
                        callback.second(event->wheel.x, event->wheel.y, event->wheel.x,
                                        event->wheel.y);
                    }
                }
            }
            break;
        }
        default:
            break;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    ::Application->Run();
    iTVPTexture2D::RecycleProcess();

    // 写入缓冲区
    int RW = 1280, RH = 720;
    SDL_GetWindowSize(tvp_window, &RW, &RH);
    krkrsdl3::SDL_GL_BaseSet(RW, RH);
    {
        std::lock_guard<std::mutex> lock(sdlRenderMtx);
        for (auto texture : renderTexture)
        {
            if (texture->isVisible)
            {
                krkrsdl3::SDL_GL_DrawTexture(texture, RW, RH);
            }
        }
    }
    // 渲染
    SDL_GL_SwapWindow(tvp_window);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_Fail()
{
    SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "Error %s", SDL_GetError());
    return SDL_APP_FAILURE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    SDL_DestroyWindow(tvp_window);
    SDL_Log("Game quit successfully!");
    SDL_Quit();
}
