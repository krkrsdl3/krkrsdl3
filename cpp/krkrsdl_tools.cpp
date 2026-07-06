#include "tjsCommHead.h"
#include "eventCallbackFun.h"

#include "WindowIntf.h"

#include "TVPSystem.h"
#include "TVPStorage.h"
#include "TVPDebug.h"
#include "TVPMsg.h"

namespace krkrsdl3
{
    static std::vector<ttstr> TVPProgramArguments;
    KRKR_Settings settings;
    static void TVPDumpOptions()
    {
        std::vector<ttstr>::const_iterator i;
        ttstr options(TVPInfoSpecifiedOptionEarlierItemHasMorePriority);
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
    static void TVPInitProgramArgumentsAndDataPath(int tvp_argc, char* tvp_argv[])
    {
        // find options from self executable image
        try
        {
            bool argument_stopped = false;
            int file_argument_count = 0;
            for (tjs_int i = 1; i < tvp_argc; i++)
            {
                if (argument_stopped)
                {
                    ttstr arg_name_and_value = TJS_N("-arg") + ttstr(file_argument_count) +
                                                TJS_N("=") + ttstr(tvp_argv[i]);
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

            // read datapath
            TVPNativeDataPath = GetDataPathDirectory(TVPNativeProjectDir);
        }
        catch (...)
        {
            throw;
        }

        // set data path
        TVPDataPath = TVPNormalizeStorageName(TVPNativeDataPath);
        TVPAddImportantLog(TVPFormatMessage(TVPInfoDataPath, TVPDataPath));

        // set log output directory
        TVPSetLogLocation(TVPNativeDataPath);
    }

    void KRKR_ParseArguments(int argc, char* argv[])
    {
#ifdef _KRKRSDL3_LIB
        TVPNativeProjectDir = std::string(argv[1]);
#else
        size_t lastSlash = std::string(argv[0]).find_last_of("/\\");
        if (lastSlash != std::string::npos)
        {
            TVPNativeProjectDir = std::string(argv[0]).substr(0, lastSlash + 1) + "Res";
        }
#endif

        TVPProjectDir = TVPNormalizeStorageName(argv[1]);

        TVPInitProgramArgumentsAndDataPath(argc, argv);

        TVPDumpOptions();

        // GameSettings
        settings.ogl_accurate_render = false;
        settings.default_font = "";
        settings.force_default_font = false;
        settings.software_draw_thread = 0;
        settings.renderer = "software";
    }
    bool KRKR_GetCommandLine(const char* name, std::string* value)
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
                        *value = (char*)p;
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
    void KRKR_SetCommandLine(const char* name, const char* value)
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
}