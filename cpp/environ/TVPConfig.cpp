#include <string>

#include "TVPConfig.h"

namespace GameSetting
{
	bool ogl_accurate_render = false;
	std::string default_font = "";
	bool force_default_font = false;

	const std::string& startup_patch_fail = "startup_patch_fail";
	const std::string& msgbox_ok = "OK";
	const std::string& msgbox_yes = "是";
	const std::string& msgbox_no = "否";
	const std::string& retry = "重试";
	const std::string& cancel = "取消";
	const std::string& err_no_memory = "内存不足";
	const std::string& err_occured = "发生异常";
	const std::string& browse_patch_lib = "浏览补丁";
	const std::string& unkown = "未知";

	const std::string& memusage = "unlimited";
	const std::string& renderer = "software";
	const int software_draw_thread = 0;
	std::string software_compress_tex = "none";

	ScreenSize currSize = {1920 , 1080};
}