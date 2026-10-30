//---------------------------------------------------------------------------
// 离屏自动化测试宿主接口
//
// 桌面 SDL3 环境实现（environ/sdl3/sdl3_autotest.cpp）：整帧呈现缓存、按帧步进、
// 合成输入注入与整帧导出，供 tests/ 下的脚本宿主与命令行开关使用。
// 由 ENABLE_AUTOTEST 控制是否编入（根 CMakeLists.txt）；关闭时下面的声明换成
// 头文件内的空实现，调用点无需 #ifdef，主循环与送显路径不留任何运行时代价。
// 引擎其它部分不依赖本接口的行为。
//---------------------------------------------------------------------------
#ifndef PlatformAutoTestH
#define PlatformAutoTestH

#include <cstdint>
#include <vector>
#include "tjsTypes.h"

#if defined(_KRKRSDL3_USE_SDL3) && !defined(_KRKRSDL3_OHOS) && defined(_KRKRSDL3_AUTOTEST)

namespace krkrsdl3
{
// 解析 -offscreen / -silent / -frames / -dump / -at_harness，
// 需在窗口与音频初始化之前调用
void TVPAutoTestSetup();
// 本次运行是否真的用到了测试宿主（任一开关给出、或脚本预约过抓帧）。
// 主循环与送显路径只保留这一个判断：普通进游戏恒假，
// ENABLE_AUTOTEST=OFF 时它是 inline 常量 false，判定整块消失。
bool TVPAutoTestActive();
bool TVPAutoTestOffscreen();
bool TVPAutoTestSilent();
// 呈现帧数上限，0 表示不限（-frames=N）
tjs_uint64 TVPAutoTestFrameBudget();
// 结束时导出最后一帧的路径（-dump=PATH），空串表示不导出
const char* TVPAutoTestDumpPath();

// 预约抓取下一帧；只有预约后送显的那一帧才会被回读缓存
void TVPAutoTestArmCapture();
// 送显入口：已预约时经渲染后端的 LockTexture 取走这一帧（各后端统一走这条通路）
void TVPAutoTestCaptureTexture(void* texture, int width, int height);
// 最近一次缓存帧的像素（RGBA 字节序，与引擎内部位图一致）
bool TVPAutoTestGetFrame(std::vector<uint8_t>& pixels, tjs_int& width, tjs_int& height,
                         tjs_int& pitch);
// 把最近一次缓存帧写成图片（扩展名 png / bmp）
bool TVPAutoTestSaveFrame(const char* path);
// 缓存帧的 RGB 指纹（FNV-1a，无帧为 0）与平均亮度 0-255（无帧为 -1）
tjs_uint64 TVPAutoTestChecksum();
tjs_int TVPAutoTestBrightness();
// 已呈现帧计数（每次送显加一）
tjs_uint64 TVPAutoTestFrameCount();
void TVPAutoTestNoteFrame();
// 立即跑完 count 个完整帧（与模态窗口同一驱动路径），返回实际跑过的帧数
tjs_int TVPAutoTestStepFrames(tjs_int count);
// 游戏正常启动之后执行 -at_harness=PATH 指定的脚本宿主（TJS）
void TVPAutoTestRunHarness();

// 合成输入注入：坐标是窗口像素坐标，与真实鼠标事件的解释方式一致
void TVPAutoTestWarpMouse(tjs_int x, tjs_int y);
// button：0 左键 / 1 右键 / 2 中键
void TVPAutoTestMouseButton(bool down, tjs_int button, tjs_int x, tjs_int y);
void TVPAutoTestMouseWheel(tjs_int x, tjs_int y, float dx, float dy);
// 键名用 SDL 扫描码名（A / F1 / RETURN / SPACE / LEFT ...）
bool TVPAutoTestKeyDown(const char* key, bool down);
void TVPAutoTestTextInput(const char* utf8);
// 最近一次注入/导出的失败原因，无失败为空串
const char* TVPAutoTestLastError();
} // namespace krkrsdl3

#else

// 未编入测试宿主：保持同名入口，全部内联成空操作
namespace krkrsdl3
{
inline void TVPAutoTestSetup() {}
inline bool TVPAutoTestActive() { return false; }
inline bool TVPAutoTestOffscreen() { return false; }
inline bool TVPAutoTestSilent() { return false; }
inline tjs_uint64 TVPAutoTestFrameBudget() { return 0; }
inline const char* TVPAutoTestDumpPath() { return ""; }
inline void TVPAutoTestArmCapture() {}
inline void TVPAutoTestCaptureTexture(void*, int, int) {}
inline bool TVPAutoTestGetFrame(std::vector<uint8_t>&, tjs_int&, tjs_int&, tjs_int&)
{
    return false;
}
inline bool TVPAutoTestSaveFrame(const char*) { return false; }
inline tjs_uint64 TVPAutoTestChecksum() { return 0; }
inline tjs_int TVPAutoTestBrightness() { return -1; }
inline tjs_uint64 TVPAutoTestFrameCount() { return 0; }
inline void TVPAutoTestNoteFrame() {}
inline tjs_int TVPAutoTestStepFrames(tjs_int) { return 0; }
inline void TVPAutoTestRunHarness() {}
inline void TVPAutoTestWarpMouse(tjs_int, tjs_int) {}
inline void TVPAutoTestMouseButton(bool, tjs_int, tjs_int, tjs_int) {}
inline void TVPAutoTestMouseWheel(tjs_int, tjs_int, float, float) {}
inline bool TVPAutoTestKeyDown(const char*, bool) { return false; }
inline void TVPAutoTestTextInput(const char*) {}
inline const char* TVPAutoTestLastError() { return "AutoTest host not built (ENABLE_AUTOTEST=OFF)"; }
} // namespace krkrsdl3

#endif

#endif
