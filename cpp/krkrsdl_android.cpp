#define SDL_MAIN_USE_CALLBACKS
#include "SDL3/SDL.h"
#include "SDL3/SDL_main.h"
#include "SDL3/SDL_init.h"
#include "glad/glad_egl.h"
#include <GLES3/gl32.h>

#include <map>

#include "TVPApplication.h"
#include "RenderManager.h"
#include "MainWindowLayer.h"

#include "eventCallbackFun.h"

SDL_Window* tvp_window;
SDL_Renderer* tvp_renderer;
SDL_GLContext tvp_glContext = NULL;

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) { // for format converter
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

    // 使用gles3.2
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    tvp_glContext = SDL_GL_CreateContext(tvp_window);
    if (tvp_glContext == NULL)
    {
        SDL_Log("gl get failed!%s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    // 使用SDL3上下文
    if (!gladLoadEGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
    {
        SDL_Log("Failed to initialize GLAD");
        return SDL_APP_FAILURE;
    }

    // 初始化时不显示
    SDL_HideWindow(tvp_window);
    SDL_DestroyProperties(props);
    // 渲染器
    tvp_renderer = SDL_CreateRenderer(tvp_window, NULL);

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
static float currScale = 1.0f;

// 安卓专属事件机制
enum TouchState {
    STATE_IDLE,
    STATE_SINGLE_FINGER,    // 单指状态（处理左键和移动）
    STATE_MULTI_FINGER,     // 多指状态（处理右键）
};
struct Finger {
    SDL_FingerID id;
    float x, y;          // 归一化坐标
    float startX, startY; // 按下时的位置
    Uint64 downTime;
    bool active;
    bool moved;

    Finger() : id(0), x(0), y(0), startX(0), startY(0),
               downTime(0), active(false), moved(false) {}
};
static TouchState _state;
static std::map<SDL_FingerID, Finger> fingers;
static float rightClickX, rightClickY;
static Uint64 rightClickStartTime;
static const Uint32 RIGHT_CLICK_CONFIRM_DELAY = 150;
void sendMouseEvent(int button, int eventType, float pX, float pY);
void sendMouseMotion(float pX, float pY);
void calculateCenter(float& centerX, float& centerY) {
    float sumX = 0, sumY = 0;
    int count = 0;

    for (const auto& pair : fingers) {
        if (pair.second.active) {
            sumX += pair.second.x;
            sumY += pair.second.y;
            count++;
        }
    }

    if (count > 0) {
        centerX = sumX / count;
        centerY = sumY / count;
    }
}
void handleFingerDown(const SDL_TouchFingerEvent& e) {
    Finger f;
    f.id = e.fingerID;
    f.x = f.startX = e.x;
    f.y = f.startY = e.y;
    f.downTime = SDL_GetTicks();
    f.active = true;
    f.moved = false;

    fingers[e.fingerID] = f;

    if (fingers.size() == 1) {
        // 单击->左键
        _state = STATE_SINGLE_FINGER;
    } else if (fingers.size() >= 2) {
        // 双击->右键
        _state = STATE_MULTI_FINGER;
    }
}
void handleFingerUp(const SDL_TouchFingerEvent& e) {
    auto it = fingers.find(e.fingerID);
    if (it == fingers.end()) return;

    Finger& f = it->second;
    f.active = false;

    if (fingers.size() == 1)
    {
        if(_state == STATE_SINGLE_FINGER)
        {
            if(!f.moved) sendMouseEvent(SDL_BUTTON_LEFT, SDL_EVENT_MOUSE_BUTTON_DOWN, f.x, f.y);
            sendMouseEvent(SDL_BUTTON_LEFT, SDL_EVENT_MOUSE_BUTTON_UP, f.x, f.y);
        }
        else if(_state == STATE_MULTI_FINGER)
        {
            if(!f.moved) sendMouseEvent(SDL_BUTTON_RIGHT, SDL_EVENT_MOUSE_BUTTON_DOWN, f.x, f.y);
            sendMouseEvent(SDL_BUTTON_RIGHT, SDL_EVENT_MOUSE_BUTTON_UP, f.x, f.y);
        }
        _state = STATE_IDLE;
    }

    fingers.erase(it);
}
void handleFingerMotion(const SDL_TouchFingerEvent& e) {
    auto it = fingers.find(e.fingerID);
    if (it == fingers.end()) return;

    Finger& f = it->second;

    // 检查是否移动
    float dx = e.x - f.startX;
    float dy = e.y - f.startY;
    float moveDist = dx*dx + dy*dy;

    if (moveDist > 0.0001f) { // 移动阈值
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
int getActiveFingerCount() {
    int count = 0;
    for (const auto& pair : fingers) {
        if (pair.second.active) count++;
    }
    return count;
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
        if(eventType == SDL_EVENT_MOUSE_BUTTON_DOWN)
        {
            std::lock_guard<std::mutex> lock(sdlCallbackMtx);
            // 检查modal对象
            bool hasModal = false;
            for(auto callback : sdl_mouseDownCallback) {
                if(callback.first->isModal && callback.first->isVisible)
                    hasModal = true;
            }
            // 写入缓冲区
            for (auto callback : sdl_mouseDownCallback)
            {
                if(hasModal)
                {
                    if(callback.first->isModal) {
                        callback.second(tmp, (pixelX - callback.first->xPos) / currScale,
                                        (pixelY- callback.first->yPos) / currScale);
                        break;
                    }
                } else {
                    if(callback.first->isVisible) {
                        callback.second(tmp,
                                        (pixelX - callback.first->xPos) / currScale,
                                        (pixelY - callback.first->yPos) / currScale);
                    }
                }

            }
        }
        else if(eventType == SDL_EVENT_MOUSE_BUTTON_UP)
        {
            std::lock_guard<std::mutex> lock(sdlCallbackMtx);
            // 检查modal对象
            bool hasModal = false;
            for(auto callback : sdl_mouseUpCallback) {
                if(callback.first->isModal && callback.first->isVisible)
                    hasModal = true;
            }
            // 写入缓冲区
            for(auto callback : sdl_mouseUpCallback) {
                if(hasModal) {
                    if(callback.first->isModal) {
                        callback.second(tmp,
                                        (pixelX - callback.first->xPos)/ currScale,
                                        (pixelY - callback.first->yPos)/ currScale);
                        break;
                    }
                } else {
                    if(callback.first->isVisible) {
                        callback.second(tmp,
                                        (pixelX - callback.first->xPos)/ currScale,
                                        (pixelY - callback.first->yPos)/ currScale);
                    }
                }
            }
        }
    }
}
void sendMouseMotion(float pX, float pY)
{
    int windowWidth, windowHeight;
    SDL_GetWindowSize(tvp_window, &windowWidth, &windowHeight);
    int pixelX = static_cast<int>(pX * windowWidth);
    int pixelY = static_cast<int>(pY * windowHeight);

    std::lock_guard<std::mutex> lock(sdlCallbackMtx);
    // 检查modal对象
    bool hasModal = false;
    for(auto callback : sdl_mouseMoveCallback) {
        if(callback.first->isModal && callback.first->isVisible)
            hasModal = true;
    }
    // 写入缓冲区
    for(auto callback : sdl_mouseMoveCallback) {
        if(hasModal) {
            if(callback.first->isModal) {
                callback.second((pixelX - callback.first->xPos)/ currScale,
                                (pixelY - callback.first->yPos)/ currScale);
                break;
            }
        } else {
            if(callback.first->isVisible) {
                callback.second((pixelX - callback.first->xPos)/ currScale,
                                (pixelY - callback.first->yPos)/ currScale);
            }
        }
    }
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    switch (event->type)
    {
        //退出
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
    SDL_SetRenderDrawColor(tvp_renderer, 0, 0, 0, 0);
    SDL_RenderClear(tvp_renderer);
    {
        std::lock_guard<std::mutex> lock(sdlRenderMtx);
        for(auto texture : renderTexture) {
            if(texture->isVisible) {
                rectBuff.w = texture->texture->w;
                rectBuff.h = texture->texture->h;
                rectBuff.x = texture->xPos;
                rectBuff.y = texture->yPos;
                int RW = 0, RH = 0; 
                if (SDL_GetWindowSize(tvp_window, &RW, &RH))
                {
                    currScale = SDL_min(((float)RW) / texture->texture->w,
                                        ((float)RH) / texture->texture->h);
                    rectBuff.w = currScale * texture->texture->w;
                    rectBuff.h = currScale * texture->texture->h;
                    if(RW - rectBuff.w > 0)
                        texture->xPos = (RW - rectBuff.w) / 2;
                    if(RH - rectBuff.h > 0)
                        texture->yPos = (RH - rectBuff.h) / 2;
                }
                // 素材
                SDL_RenderTexture(tvp_renderer, texture->texture, NULL,
                                    &rectBuff);
            }
        }
    }
    // 渲染
    SDL_RenderPresent(tvp_renderer);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_Fail() {
    SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "Error %s", SDL_GetError());
    return SDL_APP_FAILURE;
}


void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    SDL_DestroyRenderer(tvp_renderer);
    SDL_DestroyWindow(tvp_window);
    SDL_Log("Game quit successfully!");
    SDL_Quit();
}
