// OHOS dialog — native dialog stubs
// Replaces sdl3 dialog.cpp
// OHOS uses promptAction / AlertDialog via ArkTS, not native C++

#include "tjsCommHead.h"
#include "Platform.h"

#include <hilog/log.h>

#undef LOG_DOMAIN
#undef LOG_TAG
#define LOG_DOMAIN 0x3200
#define LOG_TAG "krkrsdl3"

int TVPShowSimpleMessageBox(const ttstr& text, const ttstr& caption)
{
    OH_LOG_INFO(LOG_APP, "MessageBox: %{public}s — %{public}s",
                caption.AsStdString().c_str(), text.AsStdString().c_str());
    return 0;
}

int TVPShowSimpleMessageBox(const char* pszText, const char* pszTitle, unsigned int, const char**)
{
    OH_LOG_INFO(LOG_APP, "MessageBox: %{public}s — %{public}s", pszTitle, pszText);
    return 0;
}

int TVPShowSimpleMessageBox(const ttstr& text, const ttstr& caption,
                            const std::vector<ttstr>& vecButtons)
{
    OH_LOG_INFO(LOG_APP, "MessageBox(%{public}zu btns): %{public}s — %{public}s",
                vecButtons.size(), caption.AsStdString().c_str(), text.AsStdString().c_str());
    return 0;
}

std::string TVPShowFileSelector(const std::string& title, const std::string& filename,
                                std::string initdir, bool issave)
{
    OH_LOG_INFO(LOG_APP, "FileSelector: %{public}s", title.c_str());
    return "";
}

std::string TVPShowDirectorySelector(const std::string& title, std::string initdir,
                                     std::string rootdir)
{
    OH_LOG_INFO(LOG_APP, "DirSelector: %{public}s", title.c_str());
    return "";
}
