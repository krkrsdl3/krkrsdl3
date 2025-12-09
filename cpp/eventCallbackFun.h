#pragma once

#include "tvpinputdefs.h"

typedef void (*callbackOnKeyDownUpEvent)(int);
typedef void (*callbackOnMouseScroll)(int, int, int, int);
typedef void (*callbackOnMouseDownEvent)(tTVPMouseButton, int, int);
typedef void (*callbackOnMouseUpEvent)(tTVPMouseButton, int, int);
typedef void (*callbackOnMouseMoveEvent)(int, int);

struct SDL_Sprite
{
	SDL_Texture* texture = NULL;
	int xPos = 0, yPos = 0;
	bool isVisible = false;
    bool isModal = false;
};