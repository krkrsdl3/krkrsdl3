#pragma once

#include <vector>
#include <string>

/*
* Windows/View Operation
*/
void TVPSetWindowTitle(const char* title);
std::string TVPGetWindowTitle();
void TVPSetWindowFullscreen(bool isFullscreen);
void TVPGetWindowSize(int* w, int* h);
void TVPSetWindowSize(int w, int h);
int TVPDrawSceneOnce(int interval);
int TVPConvertKeyCodeToVKCode(int keyCode);

/*
* Render Operation
*/

struct TVPSprite
{
    union {
        uint64_t gpuTexture = 0;
        void* swTexture;
    } texture;
    int type = 0; // 0:窗口 1:modal 2:overlay
    int xPos = 0, yPos = 0;
    float scale = 1.0;
    int width = 0, height = 0;
    bool isVisible = false;
};

// 获取所有渲染可用后端
std::vector<std::string> TVPListAllRenderBackend();
// 对于GPU渲染，需要加载GPU函数(后端自行处理，TVPSettings.renderer告知当前渲染后端)
// 对于软渲染，需要提供相应的处理函数
void TVPCreateTextureBackend(TVPSprite& sp);
void TVPUpdateTextureBackend(TVPSprite* sp, uint8_t* buff, int width, int height, int pitch);
void TVPDestroyTextureBackend(TVPSprite* sp);
void TVPRenderTextureBackend(TVPSprite* sp, int posX, int posY, int width, int height);