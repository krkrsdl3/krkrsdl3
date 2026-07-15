#pragma once

// CPU
//---------------------------------------------------------------------------
struct TVPMemoryInfo
{ // all in kB
    unsigned long MemTotal;
    unsigned long MemFree;
    unsigned long SwapTotal;
    unsigned long SwapFree;
    unsigned long VirtualTotal;
    unsigned long VirtualUsed;
};
void TVPGetMemoryInfo(TVPMemoryInfo& m);
tjs_int TVPGetSystemFreeMemory(); // in MB
tjs_int TVPGetSelfUsedMemory();   // in MB
void TVPRelinquishCPU();
void TVPDetectCPU();
tjs_uint32 TVPGetCPUType();
tjs_int TVPGetProcessorNum();
//
class tTVPScreen
{
public:
    static int GetWidth();
    static int GetHeight();
    static int GetDesktopLeft();
    static int GetDesktopTop();
    static int GetDesktopWidth();
    static int GetDesktopHeight();
};

// 衍生UI
int TVPShowSimpleMessageBox(const ttstr& text, const ttstr& caption);
int TVPShowSimpleMessageBox(const char* text,
                                       const char* caption,
                                       unsigned int nButton,
                                       const char** btnText);
int TVPShowSimpleMessageBox(const ttstr& text,
                            const ttstr& caption,
                            const std::vector<ttstr>& vecButtons);
std::string TVPShowFileSelector(const std::string& title,
                                const std::string& filename,
                                std::string initdir,
                                bool issave);
std::string TVPShowDirectorySelector(const std::string& title,
                                     std::string initdir,
                                     std::string rootdir);
int TVPShowSimpleInputBox(ttstr& text,
                          const ttstr& caption,
                          const ttstr& prompt,
                          const std::vector<ttstr>& vecButtons);

// 日志管理
void TVPConsoleLog(const tjs_char* format, ...);

// 其它
std::string TVPGetPackageVersionString();
ttstr TVPGetOSName();
ttstr TVPGetPlatformName();
std::string TVPGetCurrentLanguage();
void TVPOpenPatchLibUrl();
tjs_uint64 TVPGetRoughTickCount();
#define TVPGetTickCount TVPGetRoughTickCount
//
void TVPShowIME(int x, int y, int w, int h);
void TVPHideIME();
//
void TVPExitApplication(int code);
//
void TVPInvokeMenu(int x, int y, void* _menu = NULL);