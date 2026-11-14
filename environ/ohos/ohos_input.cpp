// OHOS input — stubs (input handled via NAPI in ohos_entry.cpp)
// Replaces sdl3 input.cpp

#include "tjsCommHead.h"

bool TVPInputQuery(const std::string& title, const std::string& prompt, std::string& value)
{
    // Input dialog requires ArkTS promptAction, not available in native C++
    // For OHOS, use the original value as-is
    return true;
}

int TVPShowSimpleInputBox(ttstr& text, const ttstr& caption, const ttstr& prompt,
                          const std::vector<ttstr>& btns)
{
    return 0;
}
