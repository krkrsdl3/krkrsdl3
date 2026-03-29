#include "ncbind/ncbind.hpp"

#define NCB_MODULE_NAME TJS_N("windowEx.dll")

static void InitPlugin_WINDOWEx()
{
    TVPExecuteScript(
        TJS_N("Window.registerExEvent = function(){this.exSystemMenu = void;};")
            TJS_N("Window.getNotificationNum = function() {return 0;};")
                TJS_N("Window.setMessageHook = function() { return 0; }; ")
                    TJS_N("Window.startMouseHook = function() {}; ")
                        TJS_N("Window.stopMouseHook = function() {}; ")
                    TJS_N("Window.getClientRect = function() { return %['x' => this.left,'y' => "
                          "this.top,'w' => this.width,'h' => this.height]; };")
                        TJS_N("System.getDisplayMonitors = function() { return "
                              "[%['x'=>0,'y'=>'0','w'=>1280,'h'=>720]]; };")
                            TJS_N("System.getMonitorInfo = function() {") TJS_N(
                                "  var r = %['x' => 0, 'y' => 0, 'w' => 1280, 'h' => 720];")
                                TJS_N("  return %[ 'work' => r,'monitor' => r];") TJS_N("};") TJS_N(
                                    "with (Window) {") TJS_N("  .disableResize = function(){};")
                                    TJS_N("  .maximize = function(b){this.maximized = b;};")
                                        TJS_N("  .resetWindowIcon = function(){};") TJS_N("};"));
}

NCB_PRE_REGIST_CALLBACK(InitPlugin_WINDOWEx);