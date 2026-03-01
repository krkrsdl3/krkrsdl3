#include <string>

#include "TVPConfig.h"

namespace GameSetting
{
bool ogl_accurate_render = false;
std::string default_font = "";
bool force_default_font = false;
const int software_draw_thread = 0;
const std::string& renderer = "software";

} // namespace GameSetting