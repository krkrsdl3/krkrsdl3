#include <string>

#include "tjsCommHead.h"
#include "WindowIntf.h"
#include "TVPWindow.h"
#include "TVPStorage.h"
#include "TVPApplication.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "tjsNativeMenuItem.h"

std::string TVPShowFileSelector(
    const std::string& title,
    const std::string& filename,
    std::string initdir,
    bool issave
)
{
    std::string result = "";
    const SDL_DialogFileFilter filters[] = {
        { "All files", "*" }
    };
    const char* dialog_title = title.empty() ? 
        (issave ? "Save File" : "Select File") : title.c_str();
    const char* initial_path = initdir.empty() ? NULL : initdir.c_str();
    struct DialogData {
        std::string* result_ptr;
        bool completed;
    };
    DialogData data = {&result, false};
    auto callback = [](void* userdata, const char* const* files, int filter) {
        DialogData* data = static_cast<DialogData*>(userdata);
        if (files && files[0]) {
            *(data->result_ptr) = files[0];
        }
        data->completed = true;
    };
    
    if (issave) {
        const char* default_name = filename.empty() ? "untitled" : filename.c_str();
        SDL_ShowSaveFileDialog(callback, &data, NULL, filters, 1, default_name);
    } else {
        const char* default_path = initial_path ? initial_path : "";
        SDL_ShowOpenFolderDialog(callback, &data, NULL, default_path, false);
    }
    
    while (!data.completed) {
        SDL_PumpEvents();
        SDL_Delay(10);
    }
    
    return result;
}

std::string TVPShowDirectorySelector(const std::string& title,
                                std::string initdir,
                                std::string rootdir)
{
    std::string result = "";
    const char* dialog_title = title.empty() ? "Select Folder" : title.c_str();
    const char* initial_path = initdir.empty() ? NULL : initdir.c_str();
    const char* root_path = rootdir.empty() ? NULL : rootdir.c_str();
    struct DialogData
    {
        std::string* result_ptr;
        bool completed;
    };
    DialogData data = {&result, false};
    auto callback = [](void* userdata, const char* const* files, int filter)
    {
        DialogData* data = static_cast<DialogData*>(userdata);
        if (files && files[0])
        {
            *(data->result_ptr) = files[0];
        }
        data->completed = true;
    };

    SDL_ShowOpenFolderDialog(callback, &data, NULL, initial_path ? initial_path : "", false);

    if (root_path && root_path[0])
    {
        while (!data.completed)
        {
            SDL_PumpEvents();
            SDL_Delay(10);
        }

        if (!result.empty() && result.find(root_path) != 0)
        {
            result.clear();
        }
    }
    else
    {
        while (!data.completed)
        {
            SDL_PumpEvents();
            SDL_Delay(10);
        }
    }

    return result;
}

bool TVPInputQuery(const std::string& title, const std::string& prompt, std::string& value)
{
    const int WIDTH = 400;
    const int HEIGHT = 200;

    // 初始化TTF（如果尚未初始化）
    static bool ttf_inited = false;
    if (!ttf_inited)
    {
        if (!TTF_Init())
        {
            SDL_Log("fontInit error: %s", SDL_GetError());
            return false;
        }
        ttf_inited = true;
    }

    // 加载字体
    TTF_Font* font = TTF_OpenFont("simhei.ttf", 16);
    if (!font)
    {
        // 尝试系统字体
#ifdef _WIN32
        font = TTF_OpenFont("C:/Windows/Fonts/arial.ttf", 16);
#elif __APPLE__
        font = TTF_OpenFont("/System/Library/Fonts/Helvetica.ttc", 16);
#else
        font = TTF_OpenFont("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", 16);
#endif

        if (!font)
        {
            return false;
        }
    }

    SDL_Window* window = SDL_CreateWindow(title.c_str(), WIDTH, HEIGHT, 0);
    if (!window)
    {
        TTF_CloseFont(font);
        return false;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer)
    {
        TTF_CloseFont(font);
        SDL_DestroyWindow(window);
        return false;
    }

    std::string inputText = value;
    bool running = true;
    bool result = false;
    size_t cursorPos = inputText.length();
    bool showCursor = true;
    Uint64 cursorBlinkTime = SDL_GetTicks();
    const Uint64 cursorBlinkInterval = 500;

    // 按钮矩形
    SDL_FRect okRect = {WIDTH / 2 - 100, HEIGHT - 60, 90, 40};
    SDL_FRect cancelRect = {WIDTH / 2 + 10, HEIGHT - 60, 90, 40};

    // 输入框矩形
    SDL_FRect inputRect = {50, 100, WIDTH - 100, 40};

    // 开始文本输入
    SDL_StartTextInput(window);

    while (running)
    {
        Uint64 currentTime = SDL_GetTicks();
        if (currentTime - cursorBlinkTime > cursorBlinkInterval)
        {
            showCursor = !showCursor;
            cursorBlinkTime = currentTime;
        }

        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_EVENT_QUIT)
            {
                running = false;
            }
            else if (e.type == SDL_EVENT_KEY_DOWN)
            {
                switch (e.key.key)
                {
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:
                        result = true;
                        running = false;
                        break;

                    case SDLK_ESCAPE:
                        result = false;
                        running = false;
                        break;

                    case SDLK_BACKSPACE:
                        if (cursorPos > 0 && !inputText.empty())
                        {
                            inputText.erase(cursorPos - 1, 1);
                            cursorPos--;
                            showCursor = true;
                            cursorBlinkTime = currentTime;
                        }
                        break;

                    case SDLK_DELETE:
                        if (cursorPos < inputText.length())
                        {
                            inputText.erase(cursorPos, 1);
                            showCursor = true;
                            cursorBlinkTime = currentTime;
                        }
                        break;

                    case SDLK_LEFT:
                        if (cursorPos > 0)
                        {
                            cursorPos--;
                            showCursor = true;
                            cursorBlinkTime = currentTime;
                        }
                        break;

                    case SDLK_RIGHT:
                        if (cursorPos < inputText.length())
                        {
                            cursorPos++;
                            showCursor = true;
                            cursorBlinkTime = currentTime;
                        }
                        break;

                    case SDLK_HOME:
                        cursorPos = 0;
                        showCursor = true;
                        cursorBlinkTime = currentTime;
                        break;

                    case SDLK_END:
                        cursorPos = inputText.length();
                        showCursor = true;
                        cursorBlinkTime = currentTime;
                        break;

                    case SDLK_TAB:
                        break;
                }
            }
            else if (e.type == SDL_EVENT_TEXT_INPUT)
            {
                inputText.insert(cursorPos, e.text.text);
                cursorPos += strlen(e.text.text);
                showCursor = true;
                cursorBlinkTime = currentTime;
            }
            else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
            {
                int x = e.button.x;
                int y = e.button.y;

                if (x >= okRect.x && x < okRect.x + okRect.w && y >= okRect.y &&
                    y < okRect.y + okRect.h)
                {
                    result = true;
                    running = false;
                }
                else if (x >= cancelRect.x && x < cancelRect.x + cancelRect.w &&
                         y >= cancelRect.y && y < cancelRect.y + cancelRect.h)
                {
                    result = false;
                    running = false;
                }

                else if (x >= inputRect.x && x < inputRect.x + inputRect.w && y >= inputRect.y &&
                         y < inputRect.y + inputRect.h)
                {
                    // 计算点击位置对应的字符位置
                    if (font)
                    {
                        int clickX = x - inputRect.x - 5; // 减去内边距
                        std::string textBeforeCursor;
                        int textWidth = 0;

                        // 找到点击位置对应的字符
                        for (size_t i = 0; i <= inputText.length(); i++)
                        {
                            textBeforeCursor = inputText.substr(0, i);
                            SDL_Surface* surface =
                                TTF_RenderText_Blended(font, textBeforeCursor.c_str(),
                                                       textBeforeCursor.length(), {0, 0, 0, 255});
                            if (surface)
                            {
                                int w = surface->w;
                                SDL_DestroySurface(surface);

                                if (clickX <= w)
                                {
                                    cursorPos = i;
                                    break;
                                }
                            }
                        }
                    }
                    showCursor = true;
                    cursorBlinkTime = currentTime;
                }
            }
            else if (e.type == SDL_EVENT_MOUSE_MOTION)
            {
                int x = e.motion.x;
                int y = e.motion.y;
            }
        }

        // 渲染
        SDL_SetRenderDrawColor(renderer, 240, 240, 240, 255);
        SDL_RenderClear(renderer);

        // 渲染提示文本
        SDL_Color black = {0, 0, 0, 255};
        SDL_Surface* promptSurface =
            TTF_RenderText_Blended(font, prompt.c_str(), prompt.size(), black);
        if (promptSurface)
        {
            SDL_Texture* promptTexture = SDL_CreateTextureFromSurface(renderer, promptSurface);
            if (promptTexture)
            {
                SDL_FRect promptRect = {20, 20, (float)promptSurface->w,  (float)promptSurface->h};
                SDL_RenderTexture(renderer, promptTexture, NULL, &promptRect);
                SDL_DestroyTexture(promptTexture);
            }
            SDL_DestroySurface(promptSurface);
        }

        // 绘制输入框
        SDL_FRect inputRect = {20, 60, WIDTH - 40, 40};
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderFillRect(renderer, &inputRect);
        SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
        SDL_RenderRect(renderer, &inputRect);

        // 渲染输入文本
        if (!inputText.empty())
        {
            SDL_Surface* textSurface =
                TTF_RenderText_Blended(font, inputText.c_str(), inputText.size(), black);
            if (textSurface)
            {
                SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
                if (textTexture)
                {
                    SDL_FRect textRect = {25, 70, std::min((float)textSurface->w,  (float)WIDTH - 50),
                                          (float)textSurface->h};
                    SDL_RenderTexture(renderer, textTexture, NULL, &textRect);
                    SDL_DestroyTexture(textTexture);
                }
                SDL_DestroySurface(textSurface);
            }
        }

        // 绘制输入框
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderFillRect(renderer, &inputRect);
        SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
        SDL_RenderRect(renderer, &inputRect);

        // 渲染输入文本
        if (!inputText.empty())
        {
            SDL_Surface* textSurface =
                TTF_RenderText_Blended(font, inputText.c_str(), inputText.size(), black);
            if (textSurface)
            {
                SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
                if (textTexture)
                {
                    SDL_FRect textRect = {inputRect.x + 5, inputRect.y + 10,
                                          SDL_min( (float)textSurface->w,  (float)inputRect.w - 10),
                                          (float)textSurface->h};
                    SDL_RenderTexture(renderer, textTexture, NULL, &textRect);

                    // 计算光标位置
                    if (cursorPos <= inputText.length())
                    {
                        std::string beforeCursor = inputText.substr(0, cursorPos);
                        SDL_Surface* cursorSurface = TTF_RenderText_Blended(
                            font, beforeCursor.c_str(), beforeCursor.length(), black);
                        if (cursorSurface && showCursor)
                        {
                            int cursorX = inputRect.x + 5 + cursorSurface->w;
                            SDL_FRect cursorRect = { (float)cursorX, inputRect.y + 5, 2, inputRect.h - 10};
                            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                            SDL_RenderFillRect(renderer, &cursorRect);
                            SDL_DestroySurface(cursorSurface);
                        }
                    }

                    SDL_DestroyTexture(textTexture);
                }
                SDL_DestroySurface(textSurface);
            }
        }
        else
        {
            // 空文本时也绘制光标
            if (showCursor)
            {
                SDL_FRect cursorRect = {inputRect.x + 5, inputRect.y + 5, 2, inputRect.h - 10};
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                SDL_RenderFillRect(renderer, &cursorRect);
            }
        }

        // 绘制按钮（带悬停效果）
        // 检查鼠标位置
        float mouseX, mouseY;
        SDL_GetMouseState(&mouseX, &mouseY);

        bool okHover = (mouseX >= okRect.x && mouseX < okRect.x + okRect.w && mouseY >= okRect.y &&
                        mouseY < okRect.y + okRect.h);
        bool cancelHover = (mouseX >= cancelRect.x && mouseX < cancelRect.x + cancelRect.w &&
                            mouseY >= cancelRect.y && mouseY < cancelRect.y + cancelRect.h);

        // OK按钮
        SDL_SetRenderDrawColor(renderer, okHover ? 120 : 100, okHover ? 220 : 200,
                               okHover ? 120 : 100, 255);
        SDL_RenderFillRect(renderer, &okRect);

        // Cancel按钮
        SDL_SetRenderDrawColor(renderer, cancelHover ? 220 : 200, cancelHover ? 120 : 100,
                               cancelHover ? 120 : 100, 255);
        SDL_RenderFillRect(renderer, &cancelRect);

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderRect(renderer, &okRect);
        SDL_RenderRect(renderer, &cancelRect);

        // 按钮文字
        SDL_Surface* okSurface = TTF_RenderText_Blended(font, "OK", 2, black);
        if (okSurface)
        {
            SDL_Texture* okTexture = SDL_CreateTextureFromSurface(renderer, okSurface);
            if (okTexture)
            {
                SDL_FRect okTextRect = {okRect.x + okRect.w / 2 - okSurface->w / 2,
                                        okRect.y + okRect.h / 2 - okSurface->h / 2,  (float)okSurface->w,
                                        (float)okSurface->h};
                SDL_RenderTexture(renderer, okTexture, NULL, &okTextRect);
                SDL_DestroyTexture(okTexture);
            }
            SDL_DestroySurface(okSurface);
        }

        SDL_Surface* cancelSurface = TTF_RenderText_Blended(font, "Cancel", 6, black);
        if (cancelSurface)
        {
            SDL_Texture* cancelTexture = SDL_CreateTextureFromSurface(renderer, cancelSurface);
            if (cancelTexture)
            {
                SDL_FRect cancelTextRect = {cancelRect.x + cancelRect.w / 2 - cancelSurface->w / 2,
                                            cancelRect.y + cancelRect.h / 2 - cancelSurface->h / 2,
                                            (float)cancelSurface->w,  (float)cancelSurface->h};
                SDL_RenderTexture(renderer, cancelTexture, NULL, &cancelTextRect);
                SDL_DestroyTexture(cancelTexture);
            }
            SDL_DestroySurface(cancelSurface);
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    SDL_StopTextInput(window);

    if (result)
    {
        value = inputText;
    }

    TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    return result;
}

int TVPShowSimpleInputBox(ttstr& text,
                          const ttstr& caption,
                          const ttstr& prompt,
                          const std::vector<ttstr>&)
{
    std::string inputStr;
    bool res = TVPInputQuery(caption.AsNarrowStdString(), prompt.AsNarrowStdString(), inputStr);
    text = inputStr;
    if (res)
        return 0;
    else
        return 1;
}

ttstr TVPGetPlatformName()
{
    return "Window10";
}

ttstr TVPGetOSName()
{
	return TVPGetPlatformName();
}

void TVPShowPopMenu(tTJSNI_MenuItem* menu) {
    
}

void TVPCheckAndSendDumps(const std::string& dumpdir, const std::string& packageName, const std::string& versionStr)
{

}

void TVPOpenPatchLibUrl() {
    std::string url = "https://zeas2.github.io/Kirikiroid2_patch/patch";
}