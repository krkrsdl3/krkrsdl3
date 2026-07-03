#define SDL_MAIN_USE_CALLBACKS
#include "SDL3/SDL.h"
#include "SDL3/SDL_main.h"
#include "SDL3/SDL_init.h"
#ifdef _KRKRSDL3_GL
#include "glad/glad.h"
#else
#include "glad/glad_egl.h"
#include <GLES3/gl3.h>
#endif

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

    // 参数解析
    krkrsdl3::KRKR_ParseArguments(argc, argv);

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

#ifdef _KRKRSDL3_GL
    // 使用opengl3.3
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#endif
    tvp_glContext = SDL_GL_CreateContext(tvp_window);
    if (tvp_glContext == NULL)
        return SDL_APP_FAILURE;
    // 使用SDL3上下文
#if _KRKRSDL3_GL
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
#else
    if (!gladLoadEGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
#endif
    {
        SDL_Log("Failed to initialize GLAD");
        return SDL_APP_FAILURE;
    }
    SDL_GL_MakeCurrent(tvp_window, tvp_glContext);
    SDL_GL_SetSwapInterval(1);
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
    if (!::Application->StartApplication())
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

    return SDL_APP_CONTINUE;
}

#if defined(_KRKRSDL3_WINDOWS) || defined(_KRKRSDL3_LINUX)

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
            if (event->key.scancode == SDL_SCANCODE_F1)
            {
                int x = 0, y = 0;
                SDL_GetWindowPosition(tvp_window, &x, &y);
                krkrsdl3::SDL_Invoke_Menu(x, y);
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
                SDL_Sprite* retSpr = krkrsdl3::KRKR_Get_Current_Sprite();
                if (retSpr)
                    krkrsdl3::KRKR_Trig_MouseDown(tmp,
                                                  (event->button.x - retSpr->xPos) / retSpr->scale,
                                                  (event->button.y - retSpr->yPos) / retSpr->scale);
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
                SDL_Sprite* retSpr = krkrsdl3::KRKR_Get_Current_Sprite();
                if (retSpr)
                    krkrsdl3::KRKR_Trig_MouseUp(tmp,
                                                (event->button.x - retSpr->xPos) / retSpr->scale,
                                                (event->button.y - retSpr->yPos) / retSpr->scale);
            }
            break;
        }
        case SDL_EVENT_MOUSE_MOTION:
        {
            SDL_Sprite* retSpr = krkrsdl3::KRKR_Get_Current_Sprite();
            if (retSpr)
                krkrsdl3::KRKR_Trig_MouseMove((event->motion.x - retSpr->xPos) / retSpr->scale,
                                              (event->motion.y - retSpr->yPos) / retSpr->scale);
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
        {
            SDL_Sprite* retSpr = krkrsdl3::KRKR_Get_Current_Sprite();
            if (retSpr)
                krkrsdl3::KRKR_Trig_MouseScroll(event->wheel.x, event->wheel.y, event->wheel.x,
                                                event->wheel.y);
            break;
        }
        default:
            break;
    }
    return SDL_APP_CONTINUE;
}

#elif defined(_KRKRSDL3_ANDROID)

// 安卓专属事件机制
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
    bool active;
    bool moved;

    Finger() : id(0), x(0), y(0), startX(0), startY(0), downTime(0), active(false), moved(false) {}
};
static TouchState _state;
static std::map<SDL_FingerID, Finger> fingers;
static float rightClickX, rightClickY;
static Uint64 rightClickStartTime;
static const Uint32 RIGHT_CLICK_CONFIRM_DELAY = 150;
void sendMouseEvent(int button, int eventType, float pX, float pY);
void sendMouseMotion(float pX, float pY);
void handleFingerDown(const SDL_TouchFingerEvent& e)
{
    Finger f;
    f.id = e.fingerID;
    f.x = f.startX = e.x;
    f.y = f.startY = e.y;
    f.downTime = SDL_GetTicks();
    f.active = true;
    f.moved = false;

    fingers[e.fingerID] = f;

    if (fingers.size() == 1)
    {
        // 单击->左键
        _state = STATE_SINGLE_FINGER;
    }
    else if (fingers.size() == 2)
    {
        // 双击->右键
        _state = STATE_MULTI_FINGER;
    }
    else
    {
        // 三击->菜单
        _state = STATE_MENU;
        int windowWidth, windowHeight;
        SDL_GetWindowSize(tvp_window, &windowWidth, &windowHeight);
        int pixelX = static_cast<int>(f.x * windowWidth);
        int pixelY = static_cast<int>(f.y * windowHeight);
        krkrsdl3::SDL_Invoke_Menu(pixelX, pixelY);
        fingers.clear();
        _state = STATE_IDLE;
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
            if (!f.moved)
                sendMouseEvent(SDL_BUTTON_LEFT, SDL_EVENT_MOUSE_BUTTON_DOWN, f.x, f.y);
            sendMouseEvent(SDL_BUTTON_LEFT, SDL_EVENT_MOUSE_BUTTON_UP, f.x, f.y);
        }
        else if (_state == STATE_MULTI_FINGER)
        {
            if (!f.moved)
                sendMouseEvent(SDL_BUTTON_RIGHT, SDL_EVENT_MOUSE_BUTTON_DOWN, f.x, f.y);
            sendMouseEvent(SDL_BUTTON_RIGHT, SDL_EVENT_MOUSE_BUTTON_UP, f.x, f.y);
        }
        _state = STATE_IDLE;
    }

    fingers.erase(it);
}
void handleFingerMotion(const SDL_TouchFingerEvent& e)
{
    auto it = fingers.find(e.fingerID);
    if (it == fingers.end())
        return;

    Finger& f = it->second;

    // 检查是否移动
    float dx = e.x - f.startX;
    float dy = e.y - f.startY;
    float moveDist = dx * dx + dy * dy;

    if (moveDist > 0.0001f)
    { // 移动阈值
        f.moved = true;
        f.x = e.x;
        f.y = e.y;

        if (_state == STATE_SINGLE_FINGER)
        {
            // 单指移动，发送鼠标移动
            sendMouseMotion(f.x, f.y);
        }
    }
}

void sendMouseEvent(int button, int eventType, float pX, float pY)
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
            SDL_Sprite* retSpr = krkrsdl3::KRKR_Get_Current_Sprite();
            if (retSpr)
                krkrsdl3::KRKR_Trig_MouseDown(tmp,
                                              (pixelX - retSpr->xPos) / retSpr->scale,
                                              (pixelY - retSpr->yPos) / retSpr->scale);
        }
        else if (eventType == SDL_EVENT_MOUSE_BUTTON_UP)
        {
            SDL_Sprite* retSpr = krkrsdl3::KRKR_Get_Current_Sprite();
            if (retSpr)
                krkrsdl3::KRKR_Trig_MouseUp(tmp,
                                            (pixelX - retSpr->xPos) / retSpr->scale,
                                            (pixelY - retSpr->yPos) / retSpr->scale);
        }
    }
}
void sendMouseMotion(float pX, float pY)
{
    int windowWidth, windowHeight;
    SDL_GetWindowSize(tvp_window, &windowWidth, &windowHeight);
    int pixelX = static_cast<int>(pX * windowWidth);
    int pixelY = static_cast<int>(pY * windowHeight);

    SDL_Sprite* retSpr = krkrsdl3::KRKR_Get_Current_Sprite();
    if (retSpr)
        krkrsdl3::KRKR_Trig_MouseMove((pixelX - retSpr->xPos) / retSpr->scale,
                                      (pixelY - retSpr->yPos) / retSpr->scale);
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    switch (event->type)
    {
        // 退出
        case SDL_EVENT_QUIT:
            return SDL_APP_SUCCESS;
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
            // 菜单点击
        case SDL_EVENT_MENU_CLICK:
            krkrsdl3::SDL_Trig_Menu(event->user.data1);
            break;
        default:
            break;
    }
    return SDL_APP_CONTINUE;
}

#endif

std::vector<SDL_Sprite*> renderTexture;
static SDL_FRect rectBuff;

SDL_AppResult SDL_AppIterate(void* appstate)
{
    ::Application->Run();
    iTVPTexture2D::RecycleProcess();

    // 写入缓冲区
    int RW = 1280, RH = 720;
    SDL_GetWindowSize(tvp_window, &RW, &RH);
    krkrsdl3::SDL_GL_BaseSet(RW, RH);
    {
        // 绘制currentSprite
        SDL_Sprite* retSpr = krkrsdl3::KRKR_Get_Current_Sprite();
        if (retSpr) krkrsdl3::SDL_GL_DrawTexture(retSpr, RW, RH);

        // 绘制overlay
        for (auto texture : renderTexture)
        {
            if (texture->isVisible && texture->type == 2)
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
