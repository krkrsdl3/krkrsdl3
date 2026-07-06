#pragma once

class iWindowLayer;
class tTJSNI_Window;
bool TVPGetKeyMouseAsyncState(tjs_uint keycode, bool getcurrent);
bool TVPGetJoyPadAsyncState(tjs_uint keycode, bool getcurrent);
iWindowLayer* TVPCreateAndAddWindow(tTJSNI_Window* w);
tTJSNI_Window* TVPGetActiveWindow();
