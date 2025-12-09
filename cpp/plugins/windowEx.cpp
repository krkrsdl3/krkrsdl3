#include "ncbind/ncbind.hpp"

#define NCB_MODULE_NAME TJS_W("windowEx.dll")

static void InitPlugin_WINDOWEx()
{
	TVPExecuteScript(
		TJS_W("Window.registerExEvent = function(){};")
		TJS_W("Window.getNotificationNum = function() {return 0;};")
		TJS_W("Window.setMessageHook = function() { return 0; }; ")
		TJS_W("Window.getClientRect = function() { return %['x' => this.left,'y' => this.top,'w' => this.width,'h' => this.height]; };")
		TJS_W("System.getDisplayMonitors = function() { return [%['x'=>0,'y'=>'0','w'=>1280,'h'=>720]]; };")
		TJS_W("System.getMonitorInfo = function() {")
		TJS_W("  var r = %['x' => 0, 'y' => 0, 'w' => System.desktopWidth, 'h' => System.desktopHeight];")
		TJS_W("  return %[ 'work' => r,'monitor' => r];")
		TJS_W("};")
		TJS_W("with (Window) {")
		TJS_W("  .disableResize = function(){};")
		TJS_W("  .maximize = function(b){this.maximized = b;};")
		TJS_W("  .registerExEvent = function(){this.exSystemMenu = void;this.maximized = false;};")
		TJS_W("  .resetWindowIcon = function(){};")
		TJS_W("};")
	);
}

NCB_PRE_REGIST_CALLBACK(InitPlugin_WINDOWEx);