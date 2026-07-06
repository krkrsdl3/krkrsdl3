#include "tjsCommHead.h"

#include "Platform.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_dialog.h>

int TVPShowSimpleMessageBox(const ttstr& text, const ttstr& caption)
{
    std::vector<ttstr> normal;
    normal.emplace_back("OK");
    return TVPShowSimpleMessageBox(text, caption, normal);
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