#include "tjsCommHead.h"
#include "TVPSettings.h"

#include "WindowIntf.h"

#include "TVPSystem.h"
#include "TVPStorage.h"
#include "TVPDebug.h"
#include "TVPMsg.h"
#include "Platform.h"
#include "PlatformView.h"
#include "PlatformFile.h"

TVPGlobalSettings TVPSettings;
static std::vector<ttstr> TVPProgramArguments;
static void TVPDumpOptions()
{
    std::vector<ttstr>::const_iterator i;
    ttstr options("(info) Specified option :");
    if (TVPProgramArguments.size())
    {
        for (i = TVPProgramArguments.begin(); i != TVPProgramArguments.end(); i++)
        {
            options += TJS_N(" ");
            options += *i;
        }
    }
    else
    {
        options += (const tjs_char*)TVPNone;
    }
    TVPAddImportantLog(options);
}
static ttstr TVPParseCommandLineOne(const ttstr& i)
{
    // value is specified
    const tjs_char *p, *o;
    p = o = i.c_str();
    p = TJS_strchr(p, '=');

    if (p == NULL)
    {
        return i + TJS_N("=yes");
    }

    p++;

    ttstr optname(o, (int)(p - o));
    // as a string
    return optname + p;
}
static void TVPInitProgramArguments(int tvp_argc, char* tvp_argv[])
{
    // find options from self executable image
    bool argument_stopped = false;
    int file_argument_count = 0;
    for (tjs_int i = 1; i < tvp_argc; i++)
    {
        if (argument_stopped)
        {
            ttstr arg_name_and_value =
                TJS_N("-arg") + ttstr(file_argument_count) + TJS_N("=") + ttstr(tvp_argv[i]);
            file_argument_count++;
            TVPProgramArguments.push_back(arg_name_and_value);
        }
        else
        {
            if (tvp_argv[i][0] == TJS_N('-'))
            {
                if (tvp_argv[i][1] == TJS_N('-') && tvp_argv[i][2] == 0)
                {
                    // argument stopper
                    argument_stopped = true;
                }
                else
                {
                    ttstr value(tvp_argv[i]);
                    if (!TJS_strchr(value.c_str(), TJS_N('=')))
                        value += TJS_N("=yes");
                    TVPProgramArguments.push_back(TVPParseCommandLineOne(value));
                }
            }
        }
    }
}

bool TVPParseArguments(int argc, char* argv[])
{
    // exeName
    TVPNativeExeName = argv[0];
    TVPNativeExeDir = TVPExtractStoragePath(TVPNativeExeName);
    TVPAddImportantLog(TVPFormatMessage("(info) Exe path : %1", TVPNativeExeName));
    // datas
    TVPNativeProjectData = argv[1];
    if (TVPCheckExistentLocalFolder(TVPNativeProjectData))
    {
        TVPNativeProjectDir = TVPNativeProjectData;
    }
    else if (TVPCheckExistentLocalFile(TVPNativeProjectData))
    {
        TVPNativeProjectDir = TVPExtractStoragePath(TVPNativeProjectData);
    }
    else
    {
        TVPAddImportantLog(TVPFormatMessage("%1 not found.", TVPNativeProjectData));
        return false;
    }
    // TVPNormalizeStorageName->file://
    TVPProjectData = TVPNormalizeStorageName(TVPNativeProjectData);
    TVPProjectDir = TVPNormalizeStorageName(TVPNativeProjectDir);
    TVPAddImportantLog(TVPFormatMessage("(info) Game path : %1", TVPProjectData));

    // savedata path
    TVPDataPath = TVPProjectDir + TJS_N("savedata/");
    TVPNativeDataPath = TVPGetLocallyAccessibleName(TVPDataPath);
    TVPAddImportantLog(TVPFormatMessage("(info) Savedata path : %1", TVPDataPath));

    // set log output directory
    TVPSetLogLocation(TVPNativeDataPath);

    // args 
    TVPInitProgramArguments(argc, argv);
    TVPDumpOptions();

    // check CPU type
    TVPDetectCPU();

    // check render
    std::vector<std::string> backends = TVPListAllRenderBackend();
    tTJSVariant opt;
    TVPSettings.renderer = "software"; // 软渲染保底
    if (TVPGetCommandLine(TJS_N("-render"), &opt))
    {
        ttstr str(opt);
        if (str == TJS_N("opengl") || str == TJS_N("gl") || str == TJS_N("gpu"))
        {
            if (std::find(backends.begin(), backends.end(), "opengl") != backends.end())
                TVPSettings.renderer = "opengl";
            else if (std::find(backends.begin(), backends.end(), "opengles2") != backends.end())
                TVPSettings.renderer = "opengl";
        }
        else if (str == TJS_N("software") || str == TJS_N("sw"))
            TVPSettings.renderer = "software";
        else
            TVPAddImportantLog(ttstr(TJS_N("Unknown renderer '")) + str +
                               TJS_N("', using default '") + ttstr(TVPSettings.renderer) +
                               TJS_N("'"));
    }
    else // 看看能不能opengl渲染
    {
        if (std::find(backends.begin(), backends.end(), "opengl") != backends.end())
            TVPSettings.renderer = "opengl";
        else if (std::find(backends.begin(), backends.end(), "opengles2") != backends.end())
            TVPSettings.renderer = "opengl";
    }
    TVPAddImportantLog(ttstr("Selected Render: ") + TVPSettings.renderer);

    // 其他设置
    TVPSettings.ogl_accurate_render = false;
    TVPSettings.default_font = "";
    TVPSettings.force_default_font = false;
    TVPSettings.software_draw_thread = 0;

    return true;
}
bool TVPGetCommandLine(const tjs_char* name, tTJSVariant* value)
{
    tjs_int namelen = (tjs_int)TJS_strlen(name);
    std::vector<ttstr>::const_iterator i;
    for (i = TVPProgramArguments.begin(); i != TVPProgramArguments.end(); i++)
    {
        if (!TJS_strncmp(i->c_str(), name, namelen))
        {
            if (i->c_str()[namelen] == TJS_N('='))
            {
                // value is specified
                const tjs_char* p = i->c_str() + namelen + 1;
                if (value)
                    *value = p;
                return true;
            }
            else if (i->c_str()[namelen] == 0)
            {
                // value is not specified
                if (value)
                    *value = TJS_N("yes");
                return true;
            }
        }
    }
    return false;
}
void TVPSetCommandLine(const tjs_char* name, const ttstr& value)
{
    tjs_int namelen = (tjs_int)TJS_strlen(name);
    std::vector<ttstr>::iterator i;
    for (i = TVPProgramArguments.begin(); i != TVPProgramArguments.end(); i++)
    {
        if (!TJS_strncmp(i->c_str(), name, namelen))
        {
            if (i->c_str()[namelen] == TJS_N('=') || i->c_str()[namelen] == 0)
            {
                // value found
                *i = ttstr(i->c_str(), namelen) + TJS_N("=") + value;
                return;
            }
        }
    }

    // value not found; insert argument into front
    TVPProgramArguments.insert(TVPProgramArguments.begin(), ttstr(name) + TJS_N("=") + value);
}