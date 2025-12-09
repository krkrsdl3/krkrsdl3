#pragma once

namespace GameSetting
{
	extern bool ogl_accurate_render;
	extern std::string default_font;
	extern bool force_default_font;

	//Str
	extern const std::string& startup_patch_fail;
	extern const std::string& msgbox_ok;
	extern const std::string& msgbox_yes;
	extern const std::string& msgbox_no;
	extern const std::string& retry;
	extern const std::string& cancel;
	extern const std::string& err_no_memory;
	extern const std::string& err_occured;
	extern const std::string& browse_patch_lib;
	extern const std::string& unkown;

	extern const std::string& memusage;
	extern const std::string& renderer;
	extern const int software_draw_thread;
	extern std::string software_compress_tex;

	//Window Size
	struct ScreenSize {
        int width;
		int height;
	};
	extern ScreenSize currSize;
}