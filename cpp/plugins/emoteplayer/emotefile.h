#pragma once

#include <map>
#include <array>
#include <string>
#include <random>

#if _KRKRSDL3_GL
#include <GL/glew.h>
#else
#include <GLES3/gl32.h>
#endif
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "psbfile/PSBData.h"

namespace emoteplayer
{
struct emotelimit // 区域限制
{
    float originX = 0.0f;
    float originY = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float zMax = 0.0f;
};
struct emoterect
{
    float left = 0;
    float top = 0;
    float right = 0;
    float bottom = 0;
};

class emotenode;
struct emoteRender // 渲染方式
{
    int type = 0; // 0:不绘制 1:参与网格变形和矩阵变换 2:只参与矩阵变换
                  // 3:layout/motion变形穿透(无法使用网格和透明度)，需要将matTrans合并到下一级使用
    glm::mat4 matTrans = glm::mat4(1.0f);
    float controlPts[32] = {0.0};
    float opa = 1.0;

    bool hasStencil = false;
    std::vector<emotenode*> layerNode;

    float originX = 0; // 基础参数
    float originY = 0;
    float width = 0;
    float height = 0;

    // 辅助信息
    std::string label;
};

struct emoteVar // 变量
{
    int32_t rangeBegin = 0;
    int32_t rangeEnd = 1;
    int32_t division = 0;
    std::string id;
    float currVal = 0.0f;
    float transToTick() { return division * ((currVal - rangeBegin) / (rangeEnd - rangeBegin)); }
};

class emotefile;
class emoteframe
{
public:
    emoteframe(emotefile* filePtr, uint32_t startOffset);
    ~emoteframe();

    // base
    double time = 0.0;
    uint8_t type;

    // content
    bool hasContent = false;
    double coordX = 0.0, coordY = 0.0, coordZ = 0.0, angle = 0.0, sx = 0.0, sy = 0.0, zx = 1.0,
           zy = 1.0, ox = 0.0, oy = 0.0, opa = 1.0;
    double timeOffset = 0.0;
    int64_t mask = 0;
    int64_t bm = 0;
    int64_t color = 0;
    std::string src;
    bool haszcc = false;
    std::array<double, 2> zcc_c = {0}; // 缩放曲线
    std::array<double, 4> zcc_x = {0};
    std::array<double, 4> zcc_y = {0};
    bool hasccc = false;
    std::array<double, 2> ccc_c = {0}; // 坐标曲线
    std::array<double, 4> ccc_x = {0};
    std::array<double, 4> ccc_y = {0};

    // mesh
    bool hasbp = false;
    double bp[32] = {0.000000f, 0.000000f, 0.333333f, 0.000000f, 0.666667f, 0.000000f, 1.000000f,
                     0.000000f, 0.000000f, 0.333333f, 0.333333f, 0.333333f, 0.666667f, 0.333333f,
                     1.000000f, 0.333333f, 0.000000f, 0.666667f, 0.333333f, 0.666667f, 0.666667f,
                     0.666667f, 1.000000f, 0.666667f, 0.000000f, 1.000000f, 0.333333f, 1.000000f,
                     0.666667f, 1.000000f, 1.000000f, 1.000000f}; // 网格数据
    bool hascc = false;
    std::array<double, 2> cc_c = {0}; // 网格曲线
    std::array<double, 4> cc_x = {0};
    std::array<double, 4> cc_y = {0};

private:
    emotefile* _filePtr = nullptr;
};

class emoteicon;
class emotemotion;
class emotenode
{
public:
    emotenode(emotemotion* rootmotion,
              emotenode* parent,
              std::vector<emotenode*>& nodeList,
              emotefile* filePtr,
              uint32_t startOffset);
    ~emotenode();

    void checkDrawStatus(float tick, std::vector<emoteRender>& renderList, emotelimit lim);
    void progress(float tick, std::vector<emoteRender>& renderList, emotelimit lim);
    void draw(GLuint targetFbo, emotelimit lim, GLuint exFbo, GLuint exTex);

    // once
    emotenode* _parent = nullptr;
    emotemotion* _rootmotion = nullptr;
    bool removed = false;
    float getCurrentRenderZ();
    // refmotion
    emotemotion* emot = nullptr;

    uint8_t meshCombine = 0;
    uint8_t meshDivision = 0;
    uint8_t meshTransform = 0;
    uint32_t meshSyncChildMask = 0;
    uint32_t inheritMask = 0x20007FC;
    uint8_t type = 0;
    bool isParameterize = false;
    int32_t parameterIdx = -1;
    std::string label;
    std::vector<std::string> stencilCompositeMaskLayerList;
    std::vector<emoteframe*> frameList;
    std::vector<emotenode*> children;

private:
    emotefile* _filePtr = nullptr;

    // render method
    std::vector<emoteRender> renderMethod;

    // check
    emoteicon* ic = nullptr;
    void resizeMainData();
    uint8_t* data = nullptr; // 自身icon
    float originX = 0;       // 给子类用的坐标原点 同时也作为变换的锚点
    float originY = 0;
    float width = 0;
    float height = 0;
    bool isNeedDraw = false; // 是否需要绘制
    bool isIcon = false; // 是否需要实际绘制(只有icon需要，诸如 blank 只起到布局和变形作用)
    bool isLayout =
        false; // layout/motion与blank的区别，前者无法提供网格变形，无缓存画布，无透明度，变化矩阵需要合并到下一级
    emoteframe* frame = nullptr;
    emoteframe* nextframe = nullptr;

    // runtime
    float currTick = 0;
    int8_t currbm = 0; // 混色模式 0:标准混色 1: 2: 3: 4:
    float currCoordx = 0;
    float currCoordy = 0;
    float currCoordz = 0;
    float currOpa = 1.0;
    float currAngle = 0.0; // 变换参数 旋转angle   错切sx/sy   缩放zx/zy
    float currSx = 0.0, currSy = 0.0;
    float currZx = 0.0, currZy = 0.0;
    float currOx = 0.0, currOy = 0.0;
    float currTimeOffset = 0.0; // motion引用偏移值
    bool isNeedBp = false;
    float currbp[32] = {0.0}; // 网格参数

    // opengl
    void resizeOpenGL();
    GLuint selftexture = 0; // 自身图片纹理
};

class emotemotion
{
public:
    emotemotion(emotefile* filePtr, uint32_t startOffset);
    ~emotemotion();

    float getTickByIdx(int32_t parameterIdx);
    emotenode* getNodeByName(const std::string& name);
    void progress(float tick, std::vector<emoteRender>& renderList, emotelimit lim);
    void draw(GLuint targetFbo, emotelimit lim, GLuint exFbo, GLuint exTex);
    bool contains(tjs_real x, tjs_real y);

    double lastTime;
    std::vector<emotenode*> layer;
    double loopTime;
    float selfSyncTime = 0.0f;
    float syncTime = 0.0f;
    std::vector<emotenode*> nodeList;

    std::vector<int32_t> priority;
    std::vector<emoteVar*> parameter;
    bool isParameterize = false;
    int32_t parameterIdx = -1;

    // shape rect
    std::vector<emoterect> shapeList;

private:
    emotefile* _filePtr = nullptr;

    // render method
    std::vector<emoteRender> renderMethod;
};

class emoteobject
{
public:
    emoteobject(emotefile* filePtr, uint32_t startOffset);
    ~emoteobject();
    emoteVar* findVarByName(const std::string& name);
    bool contains(tjs_real x, tjs_real y);

    uint8_t type;
    std::map<std::string, emotemotion*> motion;

private:
    emotefile* _filePtr = nullptr;
};

struct eyeControl
{
    float beginFrame = 0.0f;
    float endFrame = 0.0f;
    int32_t blinkFrameCount = 0;
    int32_t blinkIntervalMax = 0;
    int32_t blinkIntervalMin = 0;
    std::string label;

    std::uniform_int_distribution<int32_t> uid;

    // runtime
    bool hasStart = false;
    bool isBlinking = false;
    int32_t currWaitInterval = -1;
    float lastTick = 0.0f;
    float baseVal = 0.0f;
};
struct eyebrowControl
{
    float beginFrame = 0.0f;
    float endFrame = 0.0f;
    std::string label;
};
class emoteTimeVarFrame
{
public:
    emoteTimeVarFrame(emotefile* filePtr, uint32_t startOffset);
    ~emoteTimeVarFrame();

    // base
    double time = 0.0;
    uint8_t type = 0;
    // content
    bool hasContent = false;
    double easing = 0.0, value = 0.0;
};
class emoteTimeVar
{
public:
    emoteTimeVar(emotefile* filePtr, uint32_t startOffset);
    ~emoteTimeVar();

    std::vector<emoteTimeVarFrame*> frameList;
    std::string label;
};
class emotetimeline
{
public:
    emotetimeline(emotefile* filePtr, uint32_t startOffset);
    ~emotetimeline();

    int8_t diff = 0;
    std::vector<emoteTimeVar*> variableList;
    std::string label;
    int32_t lastTime = -1;
    int32_t loopBegin = -1;
    int32_t loopEnd = -1;
};

struct Vector3
{
    double x, y, z;

    Vector3() : x(0), y(0), z(0) {}
    Vector3(double x, double y, double z) : x(x), y(y), z(z) {}

    Vector3 operator+(const Vector3& other) const
    {
        return Vector3(x + other.x, y + other.y, z + other.z);
    }
    Vector3 operator-(const Vector3& other) const
    {
        return Vector3(x - other.x, y - other.y, z - other.z);
    }
    Vector3 operator*(double scalar) const { return Vector3(x * scalar, y * scalar, z * scalar); }
    Vector3 operator/(double scalar) const { return Vector3(x / scalar, y / scalar, z / scalar); }

    double dot(const Vector3& other) const { return x * other.x + y * other.y + z * other.z; }
    double magnitude() const { return std::sqrt(x * x + y * y + z * z); }
    Vector3 normalized() const
    {
        double m = magnitude();
        return m > 0 ? *this / m : *this;
    }

    static Vector3 zero() { return Vector3(0, 0, 0); }
};
class bustControl
{
public:
    bustControl(emotefile* filePtr, uint32_t startOffset);
    ~bustControl();
    struct BustState
    {
        Vector3 op; // 静止位置 (op)
        Vector3 p;  // 当前位置 (p)
        Vector3 pv; // 当前速度 (pv)
        double ofs; // 偏移量 (ofs)
    };

    BustState param;
    double spring;      // 弹簧系数
    double friction;    // 摩擦力
    double gravity;     // 重力
    double scale_x;     // X轴缩放
    double scale_y;     // Y轴缩放
    std::string var_lr; // 左右变量名
    std::string var_ud; // 上下变量名
};
struct uniSegment
{
public:
    Vector3 bp;
    Vector3 p;
    Vector3 pv;
    double scale_x;
    double scale_y;
    double length;
};
class uniControl
{
public:
    uniControl(emotefile* filePtr, uint32_t startOffset);
    ~uniControl();

    std::vector<uniSegment> segments;

    double b_rate;
    double gravity;
    double friction_x;
    double friction_y;
    double bend_spd;
    double bend_vol;

    double bendR;
    double bendS;
    double ofs;
    Vector3 op;

    double ud_eft;
    double v_bound;

    std::string var_lr;
    std::string var_lrm;
    std::string var_ud;
};

// 物理玩不明白，这都是ai写的 分别用于parts/hair/bust
class SmoothPeriodicRandom
{
private:
    float noise_seed;

public:
    SmoothPeriodicRandom();

    float generate(float tick, float a, float b, float period = 60.0f);
};
class HairSwaySimulator
{
private:
    std::vector<float> phases;
    std::vector<float> frequencies;
    std::vector<float> amplitudes;
    float wind_seed;

    // 风力参数
    struct WindParams
    {
        glm::vec2 start; // 风起始位置
        glm::vec2 goal;  // 风目标位置
        float speed;     // 风速
        float powMin;    // 最小强度
        float powMax;    // 最大强度
        float progress;  // 风进程 0.0-1.0
        bool active;     // 风是否激活
    };

    WindParams wind;

public:
    HairSwaySimulator();

    // 风开始
    void startWind(glm::vec2 start, glm::vec2 goal, float speed, float powMin, float powMax);

    // 停止风
    void stopWind();

    // 获取当前风力强度（基于位置和进度）
    float getCurrentWindStrength(glm::vec2 hairPosition);

    // 获取风的方向
    glm::vec2 getWindDirection();

    float generate(float tick, glm::vec2 hairPosition, float a, float b);

    // 检查风是否活跃
    bool isWindActive();

    // 获取风进度
    float getWindProgress();
};
class BreastJiggleSimulator
{
public:
    class SpringDamper
    {
    public:
        float position;  // 当前位置
        float velocity;  // 当前速度
        float target;    // 目标位置
        float stiffness; // 弹性系数 (0.1-0.3)
        float damping;   // 阻尼系数 (0.05-0.15)
        float mass;      // 质量 (0.8-1.2)

        SpringDamper(float stiff = 0.2f, float damp = 0.1f, float m = 1.0f);

        void update(float delta_time);
    };

    SpringDamper vertical_spring;   // 垂直运动
    SpringDamper horizontal_spring; // 水平运动
    SpringDamper bounce_spring;     // 弹跳运动
    float base_movement;            // 基础运动（如呼吸）
    float last_body_movement;       // 上一帧身体运动

    BreastJiggleSimulator();

    float generate(
        float tick, float body_movement, float a, float b, float delta_time = 1.0f / 60.0f);

    // 外部冲击（如跳跃落地）
    void addImpulse(float force);
};

struct emoteattrcompRemoveItem
{
    std::string chara;
    std::string motion;
    std::string layer;
    double value = 0;
};
class emoteattrcomp
{
public:
    emoteattrcomp(emotefile* filePtr, uint32_t startOffset);
    ~emoteattrcomp();

    std::string lable;
    std::vector<emoteattrcompRemoveItem> data_remove;
};
struct emoteselectItem
{
    std::string label;
    double offValue = 1;
    double onValue = 0;
};
class emoteselect
{
public:
    emoteselect(emotefile* filePtr, uint32_t startOffset);
    ~emoteselect();

    bool selectValue(int32_t opt);

    std::string lable;
    std::vector<emoteselectItem> selectItem;

private:
    emotefile* _filePtr = nullptr;
};

class emotemetadata
{
public:
    emotemetadata(emotefile* filePtr, uint32_t startOffset);
    ~emotemetadata();

    std::string chara;
    std::string motion;

    double scale = 1.0;

    std::map<std::string, float> _varList;
    std::vector<emoteattrcomp*> _attrcomp;
    // std::vector<std::string> _mirrorControl;
    std::vector<emoteselect*> _selectorControl;
    std::vector<emotetimeline*> _timelineControl;
    std::vector<eyeControl*> _eyeControl;
    std::vector<eyebrowControl*> _eyebrowControl;
    std::vector<bustControl*> _bustControl;
    std::vector<uniControl*> _hairControl;
    std::vector<uniControl*> _partsControl;

private:
    emotefile* _filePtr = nullptr;
};

class emoteicon
{
public:
    emoteicon(emotefile* filePtr, uint32_t startOffset);
    ~emoteicon();

    struct Clip
    {
        double bottom = 0.0;
        double left = 0.0;
        double right = 0.0;
        double top = 0.0;
    } _clip;
    std::string compress;
    double left = 0;
    double top = 0;
    double originX = 0;
    double originY = 0;
    double width = 0;
    double height = 0;
    std::string type;
    int32_t pixel = -1;
    int32_t pal = -1;
    double texWidth = 0;
    double texHeight = 0;

private:
    emotefile* _filePtr = nullptr;
};

class emotesource
{
public:
    emotesource(emotefile* filePtr, uint32_t startOffset);
    ~emotesource();

    uint8_t type;
    std::map<std::string, emoteicon*> icon;

private:
    emotefile* _filePtr = nullptr;
};

class emotefile
{
public:
    emotefile();
    ~emotefile();

    void setSeed(tjs_int seed);
    void setFun(tTJSVariantClosure decryptClo);
    bool load(const ttstr& filePath);
    tTJSVariant root();
    tTJSVariant readAllObjs(const ttstr& key, tjs_uint32 _objOffset);
    uint32_t readListInfo(std::vector<uint32_t>* target);
    void refreshListInfo(std::vector<uint32_t>* target1, std::vector<uint32_t>* target2);

    bool parseObject(std::map<std::string, uint32_t>& output, uint32_t _objOffset);
    bool parseNumber(int64_t& output, uint32_t _objOffset);
    bool parseReal(double& output, uint32_t _objOffset);
    bool parseString(std::string& output, uint32_t _objOffset);
    bool parseList(std::vector<uint32_t>& output, uint32_t _objOffset);
    bool parseResource(int32_t& id, uint32_t _objOffset);

    uint64_t GetSize();
    void ReadAllData(uint8_t* output, uint32_t outputlen);

public:
    bool isKrkr = true;    // 根据"spec"进行区别， true:"krkr" false:"win"
    bool isMotion = false; // false:emote true:motion
    uint8_t colorType = 0; // 0:BGRA(一般情形,krkr/win) 1:RGBA(common)
    bool isMirror = false;
    bool GenerateAniTree();
    bool ClearAniTree();
    void updateZMax(float zMax);
    float getZMax(bool isMain = false);
    void updateSyncTime(float _syn);
    float getSyncTime();
    emotemetadata* _metadata = nullptr;
    std::map<std::string, emoteobject*> _objects;
    struct ScreenSize
    {
        int32_t originX = 0;
        int32_t originY = 0;
        int32_t width = 0;
        int32_t height = 0;
    } _screenSize;
    std::map<std::string, emotesource*> _source;
    struct StereovisionProfile
    {
        double dist_e2d = 0.0;
        double dist_eye = 0.0;
        double eye_angle_ltd = 0.0;
        double f_level = 0.0;
        double fov = 0.0;
        double len_disp = 0.0;
    } _stereovisionProfile;
    emoteicon* findsourceByName(const std::string& name);
    bool readIconTobuffer(uint8_t* buff, uint32_t buffSize, uint32_t pitch, emoteicon* ic);
    emotemotion* findmotionByName(const std::string& name);
    bool getTickByName(const std::string& name, float& ret);
    void updateEyeControl(float tick, bool isMain = false);
    std::vector<emotetimeline*> currTimeline;
    float currStartTick = -1.0f;
    void startTimeline(float tick, const std::string& name, bool isMain = false);
    void stopTimeline(const std::string& name, bool isMain = false);
    bool checkTimline(const std::string& name, bool& result, bool isMain = false);
    void updateTimelineControl(float tick, bool isMain = false);
    emoteVar* findVarByName(const std::string& name);
    void setVariable(const std::string& name, tjs_real value, bool isMain = false);
    SmoothPeriodicRandom oscillator;
    HairSwaySimulator hair_sim;
    BreastJiggleSimulator breast_sim;
    void updatePhysics(float tick);

public:
    // emotefile附属机制
    std::vector<emotefile*> _attach;
    void addEmoteFile(emotefile* itm);

private:
    tTJSBinaryStream* filePtr = nullptr;
    tjs_int _seed = 0;
    tTJSVariantClosure _decryptClo = NULL;
    float _zMax = 0.0f;
    float _syncTime = 0.0f;

    PSB::PSBHeader _header;
    std::vector<uint32_t> stringsOffset;
    std::vector<uint32_t> namesData;
    std::vector<uint32_t> charset;
    std::vector<uint32_t> nameIndexes;
    std::vector<std::string> namesCache;

    std::vector<uint32_t> chunkOffsets;
    std::vector<uint32_t> chunkLengths;

    std::vector<uint32_t> extraChunkOffsets;
    std::vector<uint32_t> extraChunkLengths;
};
}; // namespace emoteplayer
