#include "tjsCommHead.h"

#include "TVPScreen.h"
#include "TVPApplication.h"

#include "TVPConfig.h"

int tTVPScreen::GetWidth() {
	return 1920;
}
int tTVPScreen::GetHeight() {
	int w = GetWidth();
	int h = (w * GameSetting::currSize.height) / GameSetting::currSize.width;
	return h;
}

int tTVPScreen::GetDesktopLeft() {
	return 0;
}
int tTVPScreen::GetDesktopTop() {
	return 0;
}
int tTVPScreen::GetDesktopWidth() {
	return GetWidth();
}
int tTVPScreen::GetDesktopHeight() {
	return GetHeight();
}

