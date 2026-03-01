#include <string>

#include "Platform.h"
#include "tjsCommHead.h"
#include "WindowIntf.h"
#include "TVPWindow.h"
#include "TVPStorage.h"
#include "TVPMsg.h"
#include "Random.h"
#include "TVPApplication.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "tjsNativeMenuItem.h"

//---------------------------------------------------------------------------
// TVPLocalExtrectFilePath
//---------------------------------------------------------------------------
ttstr TVPLocalExtractFilePath(const ttstr& name)
{
    // this extracts given name's path under local filename rule
    const tjs_char* p = name.c_str();
    tjs_int i = name.GetLen() - 1;
    for (; i >= 0; i--)
    {
        if (p[i] == TJS_N(':') || p[i] == TJS_N('/') || p[i] == TJS_N('\\'))
            break;
    }
    return ttstr(p, i + 1);
}
bool TVPWriteDataToFile(const ttstr& filepath, const void* data, unsigned int len)
{
    FILE* handle = fopen(filepath.AsStdString().c_str(), "wb");
    if (handle)
    {
        bool ret = fwrite(data, 1, len, handle) == len;
        fclose(handle);
        return ret;
    }
    return false;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPLocalFileStream
//---------------------------------------------------------------------------
tTVPLocalFileStream::tTVPLocalFileStream(const ttstr& origname,
                                         const ttstr& localname,
                                         tjs_uint32 flag)
  : MemBuffer(nullptr),
    FileName(localname),
    Handle(nullptr)
{
    tjs_uint32 access = flag & TJS_BS_ACCESS_MASK;
    if (access == TJS_BS_WRITE)
    {
        if (TVPCheckExistentLocalFile(localname))
        {
        }
        else
        {
            ttstr dirpath = TVPLocalExtractFilePath(localname);
            const tjs_char* p = dirpath.c_str();
            tjs_int i = dirpath.GetLen();
            if (p[i - 1] == TJS_N('/') || p[i - 1] == TJS_N('\\'))
                i--;
            dirpath = dirpath.SubString(0, i);
            if (!TVPCheckExistentLocalFolder(dirpath) && !TVPCreateFolders(dirpath))
            {
                TVPThrowExceptionMessage(TVPCannotOpenStorage, origname);
            }
        }
        MemBuffer = new tTVPMemoryStream();
        return;
    }

    const char* mode = nullptr;
    switch (access)
    {
        case TJS_BS_READ:
            mode = "rb";
            break;
        case TJS_BS_WRITE:
            mode = "wb+";
            break;
        case TJS_BS_APPEND:
            mode = "ab+";
            break;
        case TJS_BS_UPDATE:
            mode = "rb+";
            break;
    }

    Handle = SDL_IOFromFile(localname.c_str(), mode);

    if (!Handle)
    {
        if (access == TJS_BS_APPEND || access == TJS_BS_UPDATE)
        {
            Handle = SDL_IOFromFile(localname.c_str(), "rb");
            if (Handle)
            {
                Sint64 size = SDL_GetIOSize((SDL_IOStream*)Handle);
                if (size > 0 && size < 4 * 1024 * 1024)
                {
                    MemBuffer = new tTVPMemoryStream();
                    MemBuffer->SetSize(static_cast<tjs_uint64>(size));
                    SDL_ReadIO((SDL_IOStream*)Handle, MemBuffer->GetInternalBuffer(), size);
                }
                SDL_CloseIO((SDL_IOStream*)Handle);
            }
            if (!MemBuffer)
                TVPThrowExceptionMessage(TVPCannotOpenStorage, origname);
        }
    }

    // push current tick as an environment noise
    uint32_t tick = TVPGetRoughTickCount32();
    TVPPushEnvironNoise(&tick, sizeof(tick));
}
//---------------------------------------------------------------------------
tTVPLocalFileStream::~tTVPLocalFileStream()
{
    if (MemBuffer)
    {
        if (!TVPWriteDataToFile(FileName, MemBuffer->GetInternalBuffer(), MemBuffer->GetSize()))
        {
            delete MemBuffer;
            ttstr filename(FileName);
            FileName.~tTJSString();
            free(this);
            TVPThrowExceptionMessage(TJS_N("File Writing Error: %1"), filename);
        }
        delete MemBuffer;
    }
    if (Handle)
    {
        SDL_CloseIO((SDL_IOStream*)Handle);
    }

    // push current tick as an environment noise
    // (timing information from file accesses may be good noises)
    uint32_t tick = TVPGetRoughTickCount32();
    TVPPushEnvironNoise(&tick, sizeof(tick));
}
//---------------------------------------------------------------------------
tjs_uint64 tTVPLocalFileStream::Seek(tjs_int64 offset, tjs_int whence)
{
    if (MemBuffer)
    {
        return MemBuffer->Seek(offset, whence);
    }
    SDL_IOWhence sdl_whence;
    switch (whence)
    {
        case SEEK_SET:
            sdl_whence = SDL_IO_SEEK_SET;
            break;
        case SEEK_CUR:
            sdl_whence = SDL_IO_SEEK_CUR;
            break;
        case SEEK_END:
            sdl_whence = SDL_IO_SEEK_END;
            break;
        default:
            sdl_whence = SDL_IO_SEEK_SET;
            break;
    }
    return static_cast<tjs_uint64>(SDL_SeekIO((SDL_IOStream*)Handle, offset, sdl_whence));
}
//---------------------------------------------------------------------------
tjs_uint tTVPLocalFileStream::Read(void* buffer, tjs_uint read_size)
{
    if (MemBuffer)
    {
        return MemBuffer->Read(buffer, read_size);
    }
    return static_cast<tjs_uint>(SDL_ReadIO((SDL_IOStream*)Handle, buffer, read_size));
}
//---------------------------------------------------------------------------
tjs_uint tTVPLocalFileStream::Write(const void* buffer, tjs_uint write_size)
{
    if (MemBuffer)
    {
        return MemBuffer->Write(buffer, write_size);
    }
    return static_cast<tjs_uint>(SDL_WriteIO((SDL_IOStream*)Handle, buffer, write_size));
}
//---------------------------------------------------------------------------
void tTVPLocalFileStream::SetEndOfStorage()
{
    if (MemBuffer)
    {
        return MemBuffer->SetEndOfStorage();
    }

    SDL_SeekIO((SDL_IOStream*)Handle, 0, SDL_IO_SEEK_END);
}
//---------------------------------------------------------------------------
tjs_uint64 tTVPLocalFileStream::GetSize()
{
    if (MemBuffer)
    {
        return MemBuffer->GetSize();
    }
    return static_cast<tjs_uint64>(SDL_GetIOSize((SDL_IOStream*)Handle));
}
//---------------------------------------------------------------------------

std::string TVPShowFileSelector(const std::string& title,
                                const std::string& filename,
                                std::string initdir,
                                bool issave)
{
    std::string result = "";
    const SDL_DialogFileFilter filters[] = {{"All files", "*"}};
    const char* dialog_title =
        title.empty() ? (issave ? "Save File" : "Select File") : title.c_str();
    const char* initial_path = initdir.empty() ? NULL : initdir.c_str();
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

    if (issave)
    {
        const char* default_name = filename.empty() ? "untitled" : filename.c_str();
        SDL_ShowSaveFileDialog(callback, &data, NULL, filters, 1, default_name);
    }
    else
    {
        const char* default_path = initial_path ? initial_path : "";
        SDL_ShowOpenFolderDialog(callback, &data, NULL, default_path, false);
    }

    while (!data.completed)
    {
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
        font = TTF_OpenFont("C:/Windows/Fonts/simhei.ttf", 16);
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
                SDL_FRect promptRect = {20, 20, (float)promptSurface->w, (float)promptSurface->h};
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
                    SDL_FRect textRect = {25, 70,
                                          std::min((float)textSurface->w, (float)WIDTH - 50),
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
                                          SDL_min((float)textSurface->w, (float)inputRect.w - 10),
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
                            SDL_FRect cursorRect = {(float)cursorX, inputRect.y + 5, 2,
                                                    inputRect.h - 10};
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
                                        okRect.y + okRect.h / 2 - okSurface->h / 2,
                                        (float)okSurface->w, (float)okSurface->h};
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
                                            (float)cancelSurface->w, (float)cancelSurface->h};
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
    std::string inputStr = text.AsStdString();
    bool res = TVPInputQuery(caption.AsStdString(), prompt.AsStdString(), inputStr);
    text = inputStr;
    if (res)
        return 0;
    else
        return 1;
}

int TVPShowSimpleMessageBox(const ttstr& text,
                            const ttstr& caption,
                            const std::vector<ttstr>& vecButtons)
{
    std::vector<std::string> sdlButtonTexts;
    std::vector<SDL_MessageBoxButtonData> sdlButtons;
    sdlButtons.resize(vecButtons.size());
    for (const auto& btn : vecButtons)
    {
        sdlButtonTexts.push_back(btn.AsStdString());
    }
    for (size_t i = 0; i < vecButtons.size(); ++i)
    {
        SDL_MessageBoxButtonData btn = {0};
        btn.buttonID = static_cast<int>(i);

        if (i == 0)
        {
            btn.flags |= SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
        }
        if (i == vecButtons.size() - 1)
        {
            btn.flags |= SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
        }

        btn.text = sdlButtonTexts.at(i).c_str();
        sdlButtons.at(i) = btn;
    }

    std::string titleStr = caption.AsStdString();
    std::string textStr = text.AsStdString();
    SDL_MessageBoxData msgboxData = {SDL_MESSAGEBOX_INFORMATION,
                                     nullptr,
                                     titleStr.c_str(),
                                     textStr.c_str(),
                                     static_cast<int>(vecButtons.size()),
                                     vecButtons.empty() ? nullptr : sdlButtons.data(),
                                     nullptr};

    if (vecButtons.empty())
    {
        SDL_MessageBoxButtonData defaultButton = {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT |
                                                      SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,
                                                  0, "确定"};
        msgboxData.buttons = &defaultButton;
        msgboxData.numbuttons = 1;
    }

    int buttonid = -1;
    if (!SDL_ShowMessageBox(&msgboxData, &buttonid))
    {
        SDL_Log("SDL_ShowMessageBox failed: %s", SDL_GetError());
        return -1;
    }
    return buttonid;
}

int TVPShowSimpleMessageBox(const char* pszText,
                            const char* pszTitle,
                            unsigned int nButton,
                            const char** btnText)
{
    std::vector<ttstr> vecButtons;
    for (unsigned int i = 0; i < nButton; ++i)
    {
        vecButtons.emplace_back(btnText[i]);
    }
    return TVPShowSimpleMessageBox(pszText, pszTitle, vecButtons);
}

ttstr TVPGetPlatformName()
{
    const char* platform = SDL_GetPlatform();
    return ttstr(platform);
}

#ifdef _KRKRSDL3_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#ifdef _KRKRSDL3_LINUX
#include <sys/utsname.h>
#include <fstream>
#endif
#ifdef _KRKRSDL3_ANDROID
#include <sys/system_properties.h>
#endif
ttstr TVPGetOSName()
{
#ifdef _KRKRSDL3_WINDOWS
    std::string result = "Windows";
    HMODULE hMod = GetModuleHandleW(L"ntdll.dll");
    if (hMod)
    {
        typedef LONG(NTAPI * RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
        RtlGetVersionPtr RtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hMod, "RtlGetVersion");
        if (RtlGetVersion)
        {
            RTL_OSVERSIONINFOW osvi = {sizeof(osvi)};
            if (RtlGetVersion(&osvi) == 0)
            {
                if (osvi.dwMajorVersion == 10 && osvi.dwBuildNumber >= 22000)
                {
                    return "Windows 11";
                }
                switch (osvi.dwMajorVersion)
                {
                    case 10:
                        return "Windows 10";
                    case 6:
                        switch (osvi.dwMinorVersion)
                        {
                            case 3:
                                return "Windows 8.1";
                            case 2:
                                return "Windows 8";
                            case 1:
                                return "Windows 7";
                            case 0:
                                return "Windows Vista";
                        }
                        break;
                    case 5:
                        switch (osvi.dwMinorVersion)
                        {
                            case 2:
                                return "Windows XP x64";
                            case 1:
                                return "Windows XP";
                            case 0:
                                return "Windows 2000";
                        }
                        break;
                }
                return result + " " + std::to_string(osvi.dwMajorVersion) + "." +
                       std::to_string(osvi.dwMinorVersion);
            }
        }
    }

    OSVERSIONINFOW osvi = {sizeof(osvi)};
#pragma warning(push)
#pragma warning(disable : 4996)
    if (GetVersionExW(&osvi))
    {
#pragma warning(pop)
        if (osvi.dwMajorVersion == 10 && osvi.dwBuildNumber >= 22000)
            return "Windows 11";
        return result + " " + std::to_string(osvi.dwMajorVersion) + "." +
               std::to_string(osvi.dwMinorVersion);
    }
    return "Windows";
#elif defined(_KRKRSDL3_ANDROID)
    // Android版本检测
    char sdk_ver[PROP_VALUE_MAX];
    char release[PROP_VALUE_MAX];

    __system_property_get("ro.build.version.sdk", sdk_ver);
    __system_property_get("ro.build.version.release", release);

    std::string version = release;

    // 添加Android版本名称
    int sdk = atoi(sdk_ver);
    switch (sdk)
    {
        case 34:
            version += " (14)";
            break;
        case 33:
            version += " (13)";
            break;
        case 32:
            version += " (12L)";
            break;
        case 31:
            version += " (12)";
            break;
        case 30:
            version += " (11)";
            break;
        case 29:
            version += " (10)";
            break;
        case 28:
            version += " (9 Pie)";
            break;
        case 27:
            version += " (8.1 Oreo)";
            break;
        case 26:
            version += " (8.0 Oreo)";
            break;
        case 25:
            version += " (7.1 Nougat)";
            break;
        case 24:
            version += " (7.0 Nougat)";
            break;
        case 23:
            version += " (6.0 Marshmallow)";
            break;
        case 22:
            version += " (5.1 Lollipop)";
            break;
        case 21:
            version += " (5.0 Lollipop)";
            break;
    }

    return "Android " + version;

#elif defined(_KRKRSDL3_LINUX)
    // Linux发行版检测
    std::string id;
    std::string version_id;
    std::string pretty_name;
    std::ifstream file("/etc/os-release");
    std::string line;
    while (std::getline(file, line))
    {
        if (line.find("ID=") == 0)
        {
            id = line.substr(3);
            if (!id.empty() && id.front() == '"')
            {
                id = id.substr(1, id.size() - 2);
            }
        }
        else if (line.find("VERSION_ID=") == 0)
        {
            version_id = line.substr(11);
            if (!version_id.empty() && version_id.front() == '"')
            {
                version_id = version_id.substr(1, version_id.size() - 2);
            }
        }
        else if (line.find("PRETTY_NAME=") == 0)
        {
            pretty_name = line.substr(12);
            if (!pretty_name.empty() && pretty_name.front() == '"')
            {
                pretty_name = pretty_name.substr(1, pretty_name.size() - 2);
            }
        }
    }
    if (!pretty_name.empty())
    {
        return pretty_name;
    }
    if (!id.empty())
    {
        if (id == "ubuntu")
            return "Ubuntu " + version_id;
        if (id == "debian")
            return "Debian " + version_id;
        if (id == "fedora")
            return "Fedora " + version_id;
        if (id == "centos")
            return "CentOS " + version_id;
        if (id == "rhel")
            return "Red Hat " + version_id;
        if (id == "arch")
            return "Arch Linux";
        return id + " " + version_id;
    }
    struct utsname buffer;
    if (uname(&buffer) == 0)
    {
        return std::string(buffer.sysname) + " " + buffer.release;
    }

    return "Linux";
#else
    return "Unknown"
#endif
}

std::string TVPGetCurrentLanguage()
{
    SDL_Locale **locales;
    int count;
    locales = SDL_GetPreferredLocales(&count);
    for (int i = 0; i < count; i++) {
        SDL_Log("Preferred locale %d: %s_%s\n", i + 1, 
            locales[i]->language, 
            locales[i]->country ? locales[i]->country : "ANY");
    }
    std::string ret = "";
    if( count > 0)
    {
        ret += std::string(locales[0]->language);
        if(locales[0]->country)
        {
             ret += std::string("_") + std::string(locales[0]->country);
        }
        else
        {
            ret += std::string("_ANY");
        }
    }
    SDL_free(locales);
    return ret;
}

void TVPShowPopMenu(tTJSNI_MenuItem* menu)
{
}

void TVPCheckAndSendDumps(const std::string& dumpdir,
                          const std::string& packageName,
                          const std::string& versionStr)
{
}

void TVPOpenPatchLibUrl()
{
    std::string url = "https://zeas2.github.io/Kirikiroid2_patch/patch";
    SDL_OpenURL(url.c_str());
}

std::string TVPGetDefaultFileDir()
{
    char* path = SDL_GetCurrentDirectory();
    if (!path)
    {
        return std::string();
    }
    std::string result(path);
    SDL_free(path);
    return result;
}

std::vector<std::string> TVPGetAppStoragePath()
{
    std::vector<std::string> ret;
    ret.emplace_back(TVPGetDefaultFileDir());
    return ret;
}
#include "TVPScreen.h"
int tTVPScreen::GetWidth()
{
    return 1920;
}
int tTVPScreen::GetHeight()
{
    int w = GetWidth();
    int h = (w * 720) / 1280;
    return h;
}

int tTVPScreen::GetDesktopLeft()
{
    return 0;
}
int tTVPScreen::GetDesktopTop()
{
    return 0;
}
int tTVPScreen::GetDesktopWidth()
{
    return GetWidth();
}
int tTVPScreen::GetDesktopHeight()
{
    return GetHeight();
}
