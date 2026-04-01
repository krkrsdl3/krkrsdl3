#pragma once

#include "tjsCommHead.h"
#include "tjsNative.h"

#include <map>
#include "emotefile.h"

#include "tjsNativeLayer.h"

namespace emoteplayer
{
enum MaskMode
{
    MaskModeNone,
    MaskModeAlpha
};

enum PlayFlag
{
    PlayFlagNone,
    PlayFlagForce
};

class Motion
{
public:
    static bool getD3DAvailable() { return true; }
    static bool getEnableD3D() { return _enableD3D; }
    static void setEnableD3D(bool enableD3D) { _enableD3D = enableD3D; }

private:
    inline static bool _enableD3D;
};

class EmotePlayer;
class ResourceManager
{
public:
    ResourceManager(iTJSDispatch2* kagWindow, tjs_int cacheSize);
    ~ResourceManager();

    tTJSVariant load(tTJSString path);
    void unload(tTJSString path);
    void unloadAll();
    emotefile* GetPlayerByName(const tTJSString& name);

    static void setEmotePSBDecryptSeed(tjs_int decryptkey);
    static void setEmotePSBDecryptFunc(tTJSVariant funclosure);

    static iTJSDispatch2* _kagWindow; // 本身就是唯一的，所以直接static
    std::map<ttstr, emotefile*> cacheData;

private:
    inline static tjs_int _decryptkey = 0;
    inline static tTJSVariantClosure _decryptClo = NULL;
};

#define property_marco(name, type, storage) \
    type get_##name() \
    { \
        return storage; \
    } \
    void set_##name(type v) \
    { \
        storage = v; \
    }

class SeparateLayerAdaptor : public tTJSCustomObject
{
public:
    SeparateLayerAdaptor(iTJSDispatch2* targetLayer);
    ~SeparateLayerAdaptor();

    void assign(iTJSDispatch2* anotherAdaptor);
    void clear();

    tjs_int get_absolute();
    void set_absolute(tjs_int v);
    bool get_isPrimary();
    void set_isPrimary(bool v);
    tTJSVariant get_parent();
    void set_parent(tTJSVariant v);
    tTJSNI_Layer* GetLayer() { return _this; };

    // canvas
    void checkDrawArea(tjs_int width, tjs_int height);
    GLuint fbo = 0;
    GLuint fbotexture = 0;
    GLuint fbodepthtexture = 0;
    GLuint superfbo = 0;
    GLuint superfbotexture = 0;
    GLuint superfbodepthtexture = 0;
    tjs_int _width = 0, _height = 0;

private:
    tTJSNI_Layer* _this = nullptr;
    tjs_int _type = 0;
};

// EmotePlayer 和Player是一个玩意
class EmotePlayer
{
public:
    enum EmotePlayerFlag
    {
        TimelinePlayFlagParallel = 0,
        TimelinePlayFlagDifference
    };

    EmotePlayer(ResourceManager* resourceManager) : _resourceManager(resourceManager){};
    ~EmotePlayer();

    property_marco(playing, bool, _playing);
    property_marco(allplaying, bool, _allplaying);
    property_marco(animating, bool, _animating);
    property_marco(useD3D, bool, _useD3D);
    int32_t get_loopTime();
    void set_loopTime(int32_t v) { throw "reject to set loopTime"; };
    tTJSString get_motionKey() { return _motionKey; }
    void set_motionKey(tTJSString motionKey)
    {
        _motionKey = motionKey;
        _currentfile = _resourceManager->GetPlayerByName(motionKey);
    }
    property_marco(motion, tTJSString, _motion);
    property_marco(chara, tTJSString, _chara);
    void set_variableKeys() { throw "reject to set variableKeys"; };
    tTJSVariant get_variableKeys();
    property_marco(tickCount, tjs_real, clockPassed);
    property_marco(speed, tjs_real, speedRatio);
    tTJSVariant get_outline() { return tTJSVariant(); }
    void set_outline(tTJSVariant v){/* TODO */};
    property_marco(tags, tTJSVariant, _tags);

    tTJSVariant serialize();
    void unserialize(tTJSVariant data);
    void play(tTJSString name, int flag);
    void initPhysics(tTJSVariant metadata);
    void clear(iTJSDispatch2* layer, tjs_uint32 neutralColor);
    void progress(tjs_real mstime);
    void draw(iTJSDispatch2* layer);
    void assign(iTJSDispatch2* anotherAdaptor);

    void setCoord(tjs_real x, tjs_real y);
    void setScale(tjs_real scale);
    void setRotate(tjs_real rotate);
    void setColor(tjs_uint32 color);
    void setVariable(tTJSString name, tjs_real value);
    tjs_real getVariable(tTJSString name);
    void setOuterForce(tTJSString name, tjs_real ofx, tjs_real ofy);
    void setDrawAffineTranslateMatrix(
        tjs_real a, tjs_real b, tjs_real c, tjs_real d, tjs_int tx, tjs_int ty);
    void setCameraOffset(tjs_int w, tjs_int h);

    void startWind(tjs_real start, tjs_real goal, tjs_real speed, tjs_real powMin, tjs_real powMax);
    void stopWind();
    bool contains(tTJSString valName, tjs_real x, tjs_real y);

    void skip();
    void skipToSync();
    void pass();
    void stop();

    void playTimeline(tTJSString name, tjs_int flags = 0);
    void stopTimeline(tTJSString name = "");
    bool getTimelinePlaying(tTJSString name = "");
    bool getLoopTimeline(tTJSString name);
    tjs_real getTimelineTotalFrameCount(tTJSString name);
    tTJSVariant getMainTimelineLabelList();
    tTJSVariant getDiffTimelineLabelList();
    void setTimelineBlendRatio(tTJSString name,
                               tjs_real ratio,
                               tjs_real time = 0,
                               tjs_real easing = 0);
    void fadeInTimeline(tTJSString name, tjs_real time = 0, tjs_real easing = 0);
    void fadeOutTimeline(tTJSString name, tjs_real time = 0, tjs_real easing = 0);
    tTJSVariant getPlayingTimelineInfoList();
    tTJSVariant getVariableFrameList(tTJSString name);

private:
    // runtime
    ResourceManager* _resourceManager;
    emotefile* _currentfile = nullptr;
    emotemotion* _currmotion = nullptr;
    tjs_real clockPassed = -1.0;
    tjs_real speedRatio = 20.0;
    bool isSelfClear = false; // true:draw使用完全copy(以实现自主clear) false:通过clear函数间接完成
                              // 其分别对应了两种emote的运行模式
    // data
    void* m_BmpBits = nullptr;
    tjs_uint8* m_bmpData = nullptr;
    // canvas
    bool withoutAdaptor = false;
    void setOpenGLDrawArea(tjs_int width, tjs_int height);
    GLuint fbo = 0;
    GLuint fbotexture = 0;
    GLuint fbodepthtexture = 0;
    GLuint superfbo = 0;
    GLuint superfbotexture = 0;
    GLuint superfbodepthtexture = 0;
    // transform
    void updateTransMat();
    void ResetDrawArea(tjs_int width, tjs_int height);
    tjs_int _width = 0, _height = 0;
    float currCoordx = 0, currCoordy = 0, currCoordz = 0; // 坐标
    float currCamX = 0, currCamY = 0;
    float currAngle = 0.0; // 变换参数 旋转angle 缩放zx/zy
    float currZx = 1.0, currZy = 1.0;
    emoteRender _renderMethod;
    emotelimit _limitArea;
    glm::mat4 _affineTrans = glm::mat4(1.0f);

    ttstr _motionKey;
    ttstr _motion;
    ttstr _chara;
    MaskMode _maskMode = MaskModeNone;
    bool _isStop = false;
    bool _playing = false;
    bool _allplaying = false;
    bool _animating = false;
    bool _useD3D = false;
    tTJSVariant _tags;
};

class Player : public EmotePlayer
{
public:
    Player(ResourceManager* resourceManager) : EmotePlayer(resourceManager) {}

    using EmotePlayer::assign;
    using EmotePlayer::draw;
    using EmotePlayer::fadeInTimeline;
    using EmotePlayer::fadeOutTimeline;
    using EmotePlayer::getDiffTimelineLabelList;
    using EmotePlayer::getLoopTimeline;
    using EmotePlayer::getMainTimelineLabelList;
    using EmotePlayer::getPlayingTimelineInfoList;
    using EmotePlayer::getTimelinePlaying;
    using EmotePlayer::getTimelineTotalFrameCount;
    using EmotePlayer::getVariable;
    using EmotePlayer::initPhysics;
    using EmotePlayer::pass;
    using EmotePlayer::play;
    using EmotePlayer::playTimeline;
    using EmotePlayer::progress;
    using EmotePlayer::serialize;
    using EmotePlayer::setCameraOffset;
    using EmotePlayer::setColor;
    using EmotePlayer::setCoord;
    using EmotePlayer::setDrawAffineTranslateMatrix;
    using EmotePlayer::setOuterForce;
    using EmotePlayer::setRotate;
    using EmotePlayer::setScale;
    using EmotePlayer::setTimelineBlendRatio;
    using EmotePlayer::setVariable;
    using EmotePlayer::skip;
    using EmotePlayer::startWind;
    using EmotePlayer::stopTimeline;
    using EmotePlayer::stopWind;
    using EmotePlayer::unserialize;
};
}; // namespace emoteplayer
