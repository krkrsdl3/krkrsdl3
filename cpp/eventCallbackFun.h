#pragma once

#include "tvpinputdefs.h"

struct SDL_Sprite
{
    uint64_t texture = 0;
    int type = 0; //0:窗口 1:modal 2:overlay
    int xPos = 0, yPos = 0;
    float scale = 1.0;
    int width = 0, height = 0;
    bool isVisible = false;
};

namespace krkrsdl3
{
// 
SDL_Sprite* KRKR_Get_Current_Sprite();
void KRKR_Trig_MouseDown(tTVPMouseButton mouseId, int x, int y);
void KRKR_Trig_MouseUp(tTVPMouseButton mouseId, int x, int y);
void KRKR_Trig_MouseMove(int x, int y);
void KRKR_Trig_MouseScroll(int dx, int dy, int x, int y);
void KRKR_Trig_KeyDown(int vk);
void KRKR_Trig_KeyUp(int vk);

//
void fetchGLInfo();

// 
void SDL_GL_BaseSet(int w, int h);
void SDL_GL_DrawTexture(SDL_Sprite* sp, int w, int h);
void SDL_GL_CreateTexture(SDL_Sprite& sp);
void SDL_GL_JoinTexture(SDL_Sprite* sp);
void SDL_GL_UpdateTexture(SDL_Sprite* sp, uint8_t* buff, int width, int height, int pitch);
void SDL_GL_DepartTexture(SDL_Sprite* sp);
void SDL_GL_DestroyTexture(SDL_Sprite* sp);

#define SDL_EVENT_MENU_CLICK (SDL_EVENT_USER + 1)
void SDL_Invoke_Menu(int x, int y, void* _menu = NULL);
void SDL_Trig_Menu(void* data);

} // namespace krkrsdl3