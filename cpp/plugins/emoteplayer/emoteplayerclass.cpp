#include "ncbind/ncbind.hpp"
#include "emoteplayerclass.h"
#include "tjsArray.h"
#include "TVPStorage.h"
#include "SDL3/SDL.h"
#include "TickCount.h"

#include "tjsCommHead.h"
#include "tjsNativeLayer.h"

namespace emoteplayer
{
extern GLuint createEmptyTexture(int width, int height);
extern GLuint createEmptyDepthTexture(int width, int height);
extern GLuint createFBO(GLuint texture, GLuint depthtexture);
extern void glBaseSet();
extern void glBaseSetWithoutClear();

iTJSDispatch2* ResourceManager::_kagWindow = nullptr;
static SeparateLayerAdaptor* _motionWorkLayer = nullptr;

ResourceManager::ResourceManager(iTJSDispatch2* kagWindow, tjs_int cacheSize)
{
    // window info
    tjs_int sWidth = 1280, sHeight = 720;
    if (kagWindow != nullptr)
    {
        tTJSVariant val;
        kagWindow->PropGet(0, TJS_N("width"), NULL, &val, kagWindow);
        sWidth = (tjs_int)val;
        kagWindow->PropGet(0, TJS_N("height"), NULL, &val, kagWindow);
        sHeight = (tjs_int)val;
        _kagWindow = kagWindow;
    }
    // 放这里来吧，省得init时啥也没有
    if (_motionWorkLayer == nullptr)
    {
        // kag.poolLayer作为父类
        tTJSVariant baseLayer;
        iTJSDispatch2* kag = kagWindow;
        if (TJS_FAILED(kag->PropGet(0, TJS_N("poolLayer"), NULL, &baseLayer, kag)) ||
            baseLayer.Type() != tvtObject)
        {
            SDL_Log("create motionWorkLayer failed");
            return;
        }
        // 创建motionWorkLayer实例
        _motionWorkLayer = new SeparateLayerAdaptor(baseLayer.AsObjectThisNoAddRef());
        // 置入全局变量
        tTJSVariant val = tTJSVariant(_motionWorkLayer);
        _motionWorkLayer->Release();
        iTJSDispatch2* global = TVPGetScriptDispatch();
        if (global)
        {
            global->PropSet(TJS_MEMBERENSURE, TJS_N("motionWorkLayer"), NULL, &val, global);
            global->Release();
        }
    }
}
ResourceManager::~ResourceManager()
{
    unloadAll();
}
tTJSVariant ResourceManager::load(tTJSString path)
{
    ttstr trimPath;
    if (path.StartsWith(TJS_N("lzfs://./")))
        trimPath = path.SubString(9, path.length() - 9);
    else
        trimPath = path;
    auto rst = cacheData.find(TVPGetPlacedPath(trimPath));
    if (rst != cacheData.end())
    {
        return rst->second->root();
    }
    emotefile* file = new emotefile();
    file->setSeed(_decryptkey);
    file->setFun(_decryptClo);
    file->load(trimPath);

    // motionKey是唯一可区分的表示符，我们用其作为标志
    cacheData.insert(std::pair<ttstr, emotefile*>(TVPGetPlacedPath(trimPath), file));
    return file->root();
}
void ResourceManager::unload(tTJSString path)
{
    ttstr trimPath;
    if (path.StartsWith(TJS_N("lzfs://./")))
        trimPath = path.SubString(9, path.length() - 9);
    else
        trimPath = path;
    auto it = cacheData.find(TVPGetPlacedPath(trimPath));
    if (it != cacheData.end())
    {
        if (it->second != nullptr)
            delete it->second;
        cacheData.erase(it);
    }
}
void ResourceManager::unloadAll()
{
    for (auto item : cacheData)
    {
        if (item.second != nullptr)
            delete item.second;
    }
    cacheData.clear();
}
void ResourceManager::clearCache()
{
    
}
emotefile* ResourceManager::GetPlayerByName(const tTJSString& name)
{
    auto it = cacheData.find(name);
    if (it != cacheData.end())
    {
        return it->second;
    }
    return nullptr;
}
void ResourceManager::setEmotePSBDecryptSeed(tjs_int decryptkey)
{
    _decryptkey = decryptkey;
}
void ResourceManager::setEmotePSBDecryptFunc(tTJSVariant funclosure)
{
    _decryptClo = funclosure.AsObjectClosure();
}

SeparateLayerAdaptor::SeparateLayerAdaptor(iTJSDispatch2* targetLayer)
{
    // 创建实例
    _this = new tTJSNI_Layer();
    tTJSVariant kag(ResourceManager::_kagWindow);
    tTJSVariant layer(targetLayer);
    tTJSVariant* params[] = {&kag, &layer};
    if (TJS_FAILED(_this->Construct(2, params, this)))
        TVPThrowExceptionMessage(TVPSpecifyLayer);
    // 获取父类实例
    tTJSNI_BaseLayer* ths = NULL;
    if (targetLayer->NativeInstanceSupport(TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                                           (iTJSNativeInstance**)&ths) < 0 ||
        ths == NULL)
        TVPThrowExceptionMessage(TVPSpecifyLayer);
    // 设置参数
    _this->SetSize(ths->GetWidth(), ths->GetHeight());
    _this->SetImageSize(ths->GetWidth(), ths->GetHeight());
    _this->SetVisible(true);
    _this->SetType(ltAlpha);
    _this->SetHitType(htProvince);
}
SeparateLayerAdaptor::~SeparateLayerAdaptor()
{
    clear();
}
void SeparateLayerAdaptor::assign(iTJSDispatch2* anotherAdaptor)
{
    // TODO
}
void SeparateLayerAdaptor::clear()
{
    if (fbotexture != 0 && glIsTexture(fbotexture) == GL_TRUE)
        glDeleteTextures(1, &fbotexture);
    if (fbodepthtexture != 0 && glIsTexture(fbodepthtexture) == GL_TRUE)
        glDeleteTextures(1, &fbodepthtexture);
    if (superfbotexture != 0 && glIsTexture(superfbotexture) == GL_TRUE)
        glDeleteTextures(1, &superfbotexture);
    if (superfbodepthtexture != 0 && glIsTexture(superfbodepthtexture) == GL_TRUE)
        glDeleteTextures(1, &superfbodepthtexture);
    if (fbo != 0 || glIsFramebuffer(fbo) == GL_TRUE)
        glDeleteFramebuffers(1, &fbo);
    if (superfbo != 0 || glIsFramebuffer(superfbo) == GL_TRUE)
        glDeleteFramebuffers(1, &superfbo);
    if (_this != nullptr)
    {
        _this->Invalidate();
        delete _this;
        _this = nullptr;
    }
}
tjs_int SeparateLayerAdaptor::get_absolute()
{
    if (_this != nullptr)
    {
        return _this->GetAbsoluteOrderIndex();
    }
    return 0;
}
void SeparateLayerAdaptor::set_absolute(tjs_int v)
{
    if (_this != nullptr)
    {
        _this->SetAbsoluteOrderIndex(v);
    }
}
bool SeparateLayerAdaptor::get_isPrimary()
{
    if (_this != nullptr)
    {
        return _this->IsPrimary();
    }
    return false;
}
void SeparateLayerAdaptor::set_isPrimary(bool v)
{
    //
}
tTJSVariant SeparateLayerAdaptor::get_parent()
{
    if (_this != nullptr)
    {
        return _this->GetParent();
    }
    return tTJSVariant();
}
void SeparateLayerAdaptor::set_parent(tTJSVariant v)
{
    //
}
void SeparateLayerAdaptor::checkDrawArea(tjs_int width, tjs_int height)
{
    // fbo
    if (fbo == 0 || glIsFramebuffer(fbo) != GL_TRUE)
        glGenFramebuffers(1, &fbo);
    if (superfbo == 0 || glIsFramebuffer(superfbo) != GL_TRUE)
        glGenFramebuffers(1, &superfbo);

    // 基础fbo
    if (_width != width || _height != height)
    {
        _width = width;
        _height = height;
        // 材质
        GLuint newfbotexture = createEmptyTexture(width, height);
        GLuint newfbodepthtexture = createEmptyDepthTexture(width, height);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, newfbotexture,
                               0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                               newfbodepthtexture, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            SDL_Log("Framebuffer不完整!");
        }
        // 模板fbo
        GLuint newsuperfbotexture = createEmptyTexture(width, height);
        GLuint newsuperfbodepthtexture = createEmptyDepthTexture(width, height);
        glBindFramebuffer(GL_FRAMEBUFFER, superfbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               newsuperfbotexture, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                               newsuperfbodepthtexture, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            SDL_Log("Framebuffer不完整!");
        }
        // 清理旧物
        if (fbotexture != 0 && glIsTexture(fbotexture) == GL_TRUE)
        {
            glDeleteTextures(1, &fbotexture);
        }
        if (fbodepthtexture != 0 && glIsTexture(fbodepthtexture) == GL_TRUE)
        {
            glDeleteTextures(1, &fbodepthtexture);
        }
        if (superfbotexture != 0 && glIsTexture(superfbotexture) == GL_TRUE)
        {
            glDeleteTextures(1, &superfbotexture);
        }
        if (superfbodepthtexture != 0 && glIsTexture(superfbodepthtexture) == GL_TRUE)
        {
            glDeleteTextures(1, &superfbodepthtexture);
        }
        // 赋予魔法
        fbotexture = newfbotexture;
        fbodepthtexture = newfbodepthtexture;
        superfbotexture = newsuperfbotexture;
        superfbodepthtexture = newsuperfbodepthtexture;
    }
}

// 专门用来保存contain信息
tTJSNativeClass* TVPCreateNativeClass_TmpMotionObj(EmotePlayer* ptr, emotemotion* obj);
class TmpMotionObj : public tTJSNativeClass
{
    typedef tTJSNativeClass inherited;

public:
    TmpMotionObj(EmotePlayer* ptr, emotemotion* obj)
      : tTJSNativeClass(TJS_N("MotionObj"))
    {
        _ptr = ptr;
        _obj = obj;
    }
    tjs_error FuncCall(tjs_uint32 flag,
        const tjs_char* membername,
        tjs_uint32* hint,
        tTJSVariant* result,
        tjs_int numparams,
        tTJSVariant** param,
        iTJSDispatch2* objthis)
    {
        // 古法找函数
        if (std::strcmp(membername, "contains") == 0)
        {
            if (numparams < 2)
                return TJS_E_BADPARAMCOUNT;
            tjs_real x = *param[0];
            tjs_real y = *param[1];
            if (result)
                *result = _obj ? _obj->contains(x, y) : false;
            return TJS_S_OK;
        }
        else if (std::strcmp(membername, "getLayerGetter") == 0)
        {
            if (numparams < 1)
                return TJS_E_BADPARAMCOUNT;

            ttstr name = *param[0];
            iTJSDispatch2* dsp = TJSCreateDictionaryObject();

            // 寻找mtn
            emotemotion* emtObj = nullptr;
            if (_obj != nullptr)
            {
                for (auto objItm : _obj->nodeList)
                {
                    if (objItm->emot && std::strcmp(objItm->label.c_str(), name.c_str()) == 0)
                    {
                        emtObj = objItm->emot;
                        break;
                    }
                }
            }

            if (emtObj)
            {
                // shape
                iTJSDispatch2* mtn = TVPCreateNativeClass_TmpMotionObj(_ptr, emtObj);
                if (mtn)
                {
                    tTJSVariant mtnVar(mtn);
                    dsp->PropSet(TJS_MEMBERENSURE, "shape", nullptr, &mtnVar, dsp);
                    mtn->Release();
                }
            }

            tTJSVariant var(dsp);
            dsp->Release();
            if (result)
                *result = var;
            return TJS_S_OK;
        }
        else
            return TJS_E_INVALIDPARAM;
    }

    ~TmpMotionObj()
    {
        _ptr = NULL;
        _obj = NULL;
    }
    static tjs_uint32 ClassID;

protected:
    tTJSNativeInstance* CreateNativeInstance() { return NULL; }

private:
    EmotePlayer* _ptr = NULL;
    emotemotion* _obj = NULL;
};
tTJSNativeClass* TVPCreateNativeClass_TmpMotionObj(EmotePlayer* ptr, emotemotion* obj)
{
    return new TmpMotionObj(ptr, obj);
}
tjs_uint32 TmpMotionObj::ClassID = (tjs_uint32)-1;

#define setprop_t(d, p, ty) \
    { \
        tTJSVariant v(ty(p)); \
        d->PropSet(TJS_MEMBERENSURE, TJS_N(#p), nullptr, &v, d); \
    }
#define setprop(d, p) setprop_t(d, p, )
#define getprop_t(d, p, ty) \
    { \
        tTJSVariant v; \
        if (TJS_SUCCEEDED(d->PropGet(0, TJS_N(#p), nullptr, &v, d)) && v.Type() != tvtVoid) \
        { \
            p = ty(v); \
        } \
    }
#define getprop(d, p) getprop_t(d, p, )
EmotePlayer::~EmotePlayer()
{
    if (fbotexture != 0 && glIsTexture(fbotexture) == GL_TRUE)
        glDeleteTextures(1, &fbotexture);
    if (fbodepthtexture != 0 && glIsTexture(fbodepthtexture) == GL_TRUE)
        glDeleteTextures(1, &fbodepthtexture);
    if (superfbotexture != 0 && glIsTexture(superfbotexture) == GL_TRUE)
        glDeleteTextures(1, &superfbotexture);
    if (superfbodepthtexture != 0 && glIsTexture(superfbodepthtexture) == GL_TRUE)
        glDeleteTextures(1, &superfbodepthtexture);
    if (fbo != 0 || glIsFramebuffer(fbo) == GL_TRUE)
        glDeleteFramebuffers(1, &fbo);
    if (superfbo != 0 || glIsFramebuffer(superfbo) == GL_TRUE)
        glDeleteFramebuffers(1, &superfbo);
    if (m_BmpBits != nullptr)
        delete (tTVPBaseTexture*)m_BmpBits;
    if (m_bmpData != nullptr)
        delete[] m_bmpData;
}
int32_t EmotePlayer::get_loopTime()
{
    if (_currentfile != nullptr && _currentfile->_objects.size() > 0 &&
        _currentfile->_objects.begin()->second->motion.size() > 0)
    {
        return _currentfile->_objects.begin()->second->motion.begin()->second->loopTime;
    }
    return 0;
}
tTJSVariant EmotePlayer::get_variableKeys()
{
    iTJSDispatch2* array = TJSCreateArrayObject();
    if (_currentfile != nullptr)
    {
        std::set<std::string> varList;
        for (auto varItm : _currentfile->_metadata->_varList)
        {
            tTJSVariant tmp(varItm.first);
            tTJSVariant* args[] = {&tmp};
            static tjs_uint addHint = 0;
            array->FuncCall(0, TJS_N("add"), &addHint, nullptr, 1, args, array);
        }
    }
    tTJSVariant result(array, array);
    array->Release();
    return result;
}
tTJSVariant EmotePlayer::serialize()
{
    auto dict = TJSCreateDictionaryObject();

    setprop(dict, currCoordx);
    setprop(dict, currCoordy);
    setprop(dict, currCoordz);
    setprop(dict, currAngle);
    setprop(dict, currZx);
    setprop(dict, currZy);

    auto res = tTJSVariant(dict, dict);
    dict->Release();
    return res;
}
void EmotePlayer::unserialize(tTJSVariant data)
{
    auto dict = data.AsObjectNoAddRef();
    if (!dict)
    {
        return;
    }

    getprop_t(dict, currCoordx, static_cast<tjs_real>);
    getprop_t(dict, currCoordy, static_cast<tjs_real>);
    getprop_t(dict, currCoordz, static_cast<tjs_real>);
    getprop_t(dict, currAngle, static_cast<tjs_real>);
    getprop_t(dict, currZx, static_cast<tjs_real>);
    getprop_t(dict, currZy, static_cast<tjs_real>);
}
void EmotePlayer::play(tTJSString name, int flag)
{
    if (_playing)
        return;
    // SDL_Log("play-->%s", name.AsStdString().c_str());
    if (_currentfile != nullptr && !isMotion) // motionKey的启动模式
    {
        // motion
        auto it = _currentfile->_objects.find(_currentfile->_metadata->chara.c_str());
        if (it != _currentfile->_objects.end())
        {
            auto it1 = it->second->motion.find(_currentfile->_metadata->motion.c_str());
            if (it1 != it->second->motion.end())
            {
                _currmotion = it1->second;
            }
        }
        // start
        clockPassed = 0.0;
        _motion = name;
        _playing = true;
        isSelfClear = true;
    }
    else if (_resourceManager->cacheData.size() > 0 && isMotion) // chara+motion启动方案
    {
        _motion = name;
        _currmotion = nullptr;
        // motion
        for (auto tmpFile : _resourceManager->cacheData)
        {
            auto it = tmpFile.second->_objects.find(_chara.AsStdString().c_str());
            if (it != tmpFile.second->_objects.end())
            {
                auto it1 = it->second->motion.find(_motion.AsStdString().c_str());
                if (it1 != it->second->motion.end())
                {
                    _currmotion = it1->second;
                }
            }
            if (_currmotion)
            {
                _currentfile = tmpFile.second;
                break;
            }
                
        }
        // start
        clockPassed = 0.0;
        _playing = true;
        isSelfClear = true;
    }
    else // 群体启动模式，即对manager的所有file进行拼好件(其会存在互相索引的情况，结构可能得改改了)
    {
        _currentfile = nullptr;
        _currmotion = nullptr;
        for (auto itm : _resourceManager->cacheData)
        {
            // 查询
            auto it = itm.second->_objects.find(itm.second->_metadata->chara.c_str());
            if (it != itm.second->_objects.end())
            {
                auto it1 = it->second->motion.find(itm.second->_metadata->motion.c_str());
                if (it1 != it->second->motion.end())
                {
                    _currentfile = itm.second;
                    _currmotion = it1->second;
                    break;
                }
            }
        }
        // 对剩下文件进行相互并连
        for (auto itm1 : _resourceManager->cacheData)
        {
            for (auto itm2 : _resourceManager->cacheData)
            {
                if (itm1.second != itm2.second)
                    itm1.second->addEmoteFile(itm2.second);
            }
        }
        // start
        clockPassed = 0.0;
        _motion = name;
        _playing = true;
        isSelfClear = false;
    }
}
void EmotePlayer::initPhysics(tTJSVariant metadata)
{
    SDL_Log("EmotePlayer::initPhysics TODO");
}
void EmotePlayer::clear(iTJSDispatch2* layer, tjs_uint32 neutralColor)
{
    auto* self = ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(layer);
    tTJSNI_BaseLayer* ths = NULL;
    if (self != nullptr)
    {
        ths = self->GetLayer();
    }
    else
    {
        if (layer->NativeInstanceSupport(TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                                         (iTJSNativeInstance**)&ths) < 0)
            return;
        withoutAdaptor = true;
    }
    if (ths == NULL)
        return;

    if (withoutAdaptor)
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    else if (self && self->fbo)
        glBindFramebuffer(GL_FRAMEBUFFER, self->fbo);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}
void EmotePlayer::progress(tjs_real mstime)
{
    // SDL_Log("progress-->%f %f", mstime, clockPassed);
    if (_isStop)
        return;
    if (_currentfile != nullptr && _currmotion != nullptr && clockPassed > -1.0 &&
        _limitArea.width != _limitArea.originX && _limitArea.height != _limitArea.originY)
    {
        if (_playing)
            clockPassed += mstime / speedRatio;
        std::vector<emoteRender> empty;
        empty.push_back(_renderMethod);
        // mirror
        if (_currentfile->isMirror)
        {
            empty.at(0).matTrans = glm::scale(empty.at(0).matTrans, glm::vec3(-1.0f, 1.0f, 1.0f));
        }
        // condition
        if (_currentfile->_metadata->_varList.size() > 0)
        {
            // 更新控制参数
            _currentfile->updateEyeControl(clockPassed, true);
            _currentfile->updateTimelineControl(clockPassed, true);
            // 物理玩不明白，就不开起来污染眼睛了
            //_currentfile->updatePhysics(clockPassed);
            //  将子对象画到自己身上
            _currmotion->progress(0, empty, _limitArea);
        }
        else
        {
            // 是否结束
            if (!isMotion && clockPassed > _currmotion->lastTime)
            {
                _playing = false;
            }
            // 对于motion限制最后时间并结束
            if (isMotion && _currmotion->loopTime < 0 &&
                clockPassed > _currmotion->syncTime)
            {
                clockPassed = _currmotion->syncTime;
                _playing = false;
            }
            // 将子对象画到自己身上
            _currmotion->progress(clockPassed, empty, _limitArea);
        }
    }
}
void EmotePlayer::draw(iTJSDispatch2* objthis)
{
    auto* self = ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(objthis);
    tTJSNI_BaseLayer* ths = NULL;
    if (self != nullptr)
    {
        ths = self->GetLayer();
        if (ths == NULL)
            return;
    }
    else
    {
        if (objthis->NativeInstanceSupport(TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                                           (iTJSNativeInstance**)&ths) < 0)
            return;
        withoutAdaptor = true;
    }

    if (_currentfile != nullptr && _currmotion != nullptr)
    {
        ResetDrawArea(ths->GetWidth(), ths->GetHeight());
        if (withoutAdaptor)
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        else
        {
            self->checkDrawArea(ths->GetWidth(), ths->GetHeight());
            glBindFramebuffer(GL_FRAMEBUFFER, self->fbo);
        }
        glBaseSetWithoutClear();
        if (isSelfClear)
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        else
            glClear(GL_DEPTH_BUFFER_BIT);
        if (withoutAdaptor)
            _currmotion->draw(fbo, _limitArea, superfbo, superfbotexture);
        else
            _currmotion->draw(self->fbo, _limitArea, self->superfbo, self->superfbotexture);
        if (m_bmpData != nullptr && m_BmpBits != nullptr)
        {
            // read
            glReadPixels(0, 0, _width, _height, GL_RGBA, GL_UNSIGNED_BYTE, m_bmpData);
            tjs_uint8* buff = (tjs_uint8*)ths->GetMainImagePixelBufferForWrite();
            // for (size_t i = 0; i < _width * _height; i++)
            //{
            //     if (m_bmpData[4 * i] == 0x00 && m_bmpData[4 * i + 1] == 0x00 &&
            //         m_bmpData[4 * i + 2] == 0x00 && m_bmpData[4 * i + 3] != 0xFF)
            //     {
            //         memset(buff + 4 * i, 0, 4);
            //     }
            //     else if (m_bmpData[4 * i + 3] > 0x00)
            //     {
            //         memcpy(buff + 4 * i, m_bmpData + 4 * i, 3);
            //         buff[4 * i + 3] = 0xFF;
            //     }
            //     else
            //     {
            //         memcpy(buff + 4 * i, m_bmpData + 4 * i, 4);
            //     }
            //
            // }
            memcpy(buff, m_bmpData, _width * _height * 4);
            ths->Update();
        }
    }
}
void EmotePlayer::assign(iTJSDispatch2* anotherAdaptor)
{
    SDL_Log("EmotePlayer::assign TODO");
}
void EmotePlayer::setCoord(tjs_real x, tjs_real y)
{
    currCoordx = x;
    currCoordy = y;
    updateTransMat();
}
void EmotePlayer::setScale(tjs_real scale)
{
    currZx = scale;
    currZy = scale;
    updateTransMat();
}
void EmotePlayer::setRotate(tjs_real rotate)
{
    currAngle = rotate;
    updateTransMat();
}
void EmotePlayer::setColor(tjs_uint32 color)
{
    SDL_Log("EmotePlayer::setColor TODO");
}
void EmotePlayer::setVariable(tTJSString name, tjs_real value)
{
    if (_currentfile != nullptr)
    {
        std::string tmpName = name.AsStdString();
        tmpName.append(1, '\0'); // 终有一天，我会把这sb字符串给优化掉
        _currentfile->setVariable(tmpName, value, true); // 管你有没有，设了再说
    }
}
tjs_real EmotePlayer::getVariable(tTJSString name)
{
    if (_currentfile != nullptr)
    {
        std::string tmpName = name.AsStdString();
        tmpName.append(1, '\0'); // 终有一天，我会把这sb字符串给优化掉
        auto varPos = _currentfile->_metadata->_varList.find(tmpName);
        if (varPos != _currentfile->_metadata->_varList.end())
        {
            return varPos->second;
        }
    }
    return 0.0;
}
void EmotePlayer::setOuterForce(tTJSString name, tjs_real ofx, tjs_real ofy)
{
    SDL_Log("EmotePlayer::setOuterForce TODO");
}
void EmotePlayer::setDrawAffineTranslateMatrix(
    tjs_real a, tjs_real b, tjs_real c, tjs_real d, tjs_int tx, tjs_int ty)
{
    if (_currentfile != nullptr)
    {
        _affineTrans = glm::mat4(a, -c, 0.0f, 0.0f, -b, d, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, tx,
                                 ty, 0.0f, 1.0f);
        updateTransMat();
    }
}
void EmotePlayer::setCameraOffset(tjs_int w, tjs_int h)
{
    currCamX = w;
    currCamY = h;
}
void EmotePlayer::startWind(
    tjs_real start, tjs_real goal, tjs_real speed, tjs_real powMin, tjs_real powMax)
{
    SDL_Log("EmotePlayer::startWind TODO");
}
void EmotePlayer::stopWind()
{
    SDL_Log("EmotePlayer::stopWind TODO");
}
bool EmotePlayer::contains(tjs_real x, tjs_real y)
{
    //SDL_Log("EmotePlayer::contains TODO");
    //恒定包含，通过getLayerGetter得到局域contains
    return true;
}
void EmotePlayer::skip()
{
    SDL_Log("EmotePlayer::skip TODO");
}
void EmotePlayer::skipToSync()
{
    if (_currmotion != nullptr)
    {
        clockPassed = _currentfile->getSyncTime();
    }
    SDL_Log("EmotePlayer::skipToSync TODO");
}
void EmotePlayer::pass()
{
    SDL_Log("EmotePlayer::pass TODO");
}
void EmotePlayer::stop()
{
    _isStop = true;
    _playing = false;
    SDL_Log("EmotePlayer::stop TODO");
}
void EmotePlayer::playTimeline(tTJSString name, tjs_int flags)
{
    if (_currentfile != nullptr)
    {
        _currentfile->startTimeline(-10000.0f, name.AsStdString(),
                                    true); // 管你有没有，设了再说
    }
}
void EmotePlayer::stopTimeline(tTJSString name)
{
    if (_currentfile != nullptr)
    {
        _currentfile->stopTimeline(name.AsStdString(), true); // 管你有没有，设了再说
    }
}
bool EmotePlayer::getTimelinePlaying(tTJSString name)
{
    if (_currentfile != nullptr)
    {
        bool ret = false;
        if (_currentfile->checkTimline(name.AsStdString(), ret, true))
            return ret;
    }
    return false;
}
bool EmotePlayer::getLoopTimeline(tTJSString name)
{
    if (_currentfile != nullptr)
    {
        for (auto itm : _currentfile->_metadata->_timelineControl)
        {
            if (strcmp(itm->label.c_str(), name.AsStdString().c_str()) == 0)
            {
                return itm->lastTime < 0;
            }
        }
    }
    return false;
}
tjs_real EmotePlayer::getTimelineTotalFrameCount(tTJSString name)
{
    if (_currentfile != nullptr)
    {
        for (auto itm : _currentfile->_metadata->_timelineControl)
        {
            if (strcmp(itm->label.c_str(), name.AsStdString().c_str()) == 0)
            {
                return itm->loopEnd - itm->loopBegin + 1;
            }
        }
    }
    return 0;
}
tTJSVariant EmotePlayer::getMainTimelineLabelList()
{
    iTJSDispatch2* array = TJSCreateArrayObject();
    if (_currentfile != nullptr)
    {
        for (auto itm : _currentfile->_metadata->_timelineControl)
        {
            if (itm->diff == 0)
            {
                tTJSVariant tmp(ttstr(itm->label));
                tTJSVariant* args[] = {&tmp};
                static tjs_uint addHint = 0;
                array->FuncCall(0, TJS_N("add"), &addHint, nullptr, 1, args, array);
            }
        }
    }
    tTJSVariant result(array, array);
    array->Release();
    return result;
}
tTJSVariant EmotePlayer::getDiffTimelineLabelList()
{
    iTJSDispatch2* array = TJSCreateArrayObject();
    if (_currentfile != nullptr)
    {
        for (auto itm : _currentfile->_metadata->_timelineControl)
        {
            if (itm->diff == 1)
            {
                tTJSVariant tmp(ttstr(itm->label));
                tTJSVariant* args[] = {&tmp};
                static tjs_uint addHint = 0;
                array->FuncCall(0, TJS_N("add"), &addHint, nullptr, 1, args, array);
            }
        }
    }
    tTJSVariant result(array, array);
    array->Release();
    return result;
}
void EmotePlayer::setTimelineBlendRatio(tTJSString name,
                                        tjs_real ratio,
                                        tjs_real time,
                                        tjs_real easing)
{
    SDL_Log("EmotePlayer::setTimelineBlendRatio TODO");
}
void EmotePlayer::fadeInTimeline(tTJSString name, tjs_real time, tjs_real easing)
{
    playTimeline(name);
}
void EmotePlayer::fadeOutTimeline(tTJSString name, tjs_real time, tjs_real easing)
{
    stopTimeline(name);
}
tTJSVariant EmotePlayer::getPlayingTimelineInfoList()
{
    iTJSDispatch2* array = TJSCreateArrayObject();
    tTJSVariant result(array, array);
    if (_currentfile != nullptr)
    {
        for (auto playingTimeline : _currentfile->currTimeline)
        {
            iTJSDispatch2* obj = TJSCreateDictionaryObject();
            tTJSVariant val = tTJSVariant(playingTimeline->label);
            obj->PropSet(TJS_MEMBERENSURE, TJS_N("label"), NULL, &val, obj);
            tTJSVariant objItm(obj, obj);
            obj->Release();
            tTJSVariant tmp(objItm);
            tTJSVariant* args[] = {&tmp};
            static tjs_uint addHint = 0;
            array->FuncCall(0, TJS_N("add"), &addHint, nullptr, 1, args, array);
        }
    }
    array->Release();
    return result;
}
tTJSVariant EmotePlayer::getVariableFrameList(tTJSString name)
{
    if (_currentfile != nullptr)
    {
        iTJSDispatch2* root = _currentfile->root().AsObject();
        tTJSVariant itm;
        if (TJS_FAILED(root->PropGet(0, TJS_N("metadata"), NULL, &itm, root)))
        {
            SDL_Log("emotefile donot contain metadata");
            return tTJSVariant();
        }
        root->Release();
        root = itm.AsObject();
        if (TJS_FAILED(root->PropGet(0, TJS_N("variableList"), NULL, &itm, root)))
        {
            SDL_Log("emotefile donot contain variableList");
            return tTJSVariant();
        }
        root->Release();
        root = itm.AsObjectThis();

        tTJSVariant retNeed;
        for (tjs_uint32 i = 0; i < _currentfile->_metadata->_varList.size(); i++)
        {
            tTJSVariant varItem;
            if (TJS_FAILED(root->PropGetByNum(TJS_MEMBERMUSTEXIST, i, &varItem, root)))
                break;
            iTJSDispatch2* rev = varItem.AsObjectThisNoAddRef();
            tTJSVariant labelname;
            if (TJS_FAILED(rev->PropGet(0, TJS_N("label"), NULL, &labelname, rev)))
                continue;
            if (labelname.Type() != tvtString || ttstr(labelname) != name)
                continue;
            if (TJS_FAILED(rev->PropGet(0, TJS_N("frameList"), NULL, &retNeed, rev)))
                continue;
            break;
        }

        root->Release();
        return retNeed;
    }
    else
    {
        iTJSDispatch2* array = TJSCreateArrayObject();
        SDL_Log("EmotePlayer::getVariableFrameList TODO");
        tTJSVariant result(array, array);
        array->Release();
        return result;
    }
}
tTJSVariant EmotePlayer::getCommandList()
{
    iTJSDispatch2* dsp = TJSCreateArrayObject();

    // 让它能触发更新就行了
    tTJSVariant tmp(clockPassed);
    tTJSVariant* args[] = {&tmp};
    static tjs_uint addHint = 0;
    dsp->FuncCall(0, TJS_N("add"), &addHint, nullptr, 1, args, dsp);

    tTJSVariant var(dsp);
    dsp->Release();
    return var;
}
tTJSVariant EmotePlayer::getLayerGetter(tTJSString name)
{
    iTJSDispatch2* dsp = TJSCreateDictionaryObject();

    // 寻找mtn
    emotemotion* emtObj = nullptr;
    if (_currmotion != nullptr)
    {
        for (auto objItm : _currmotion->nodeList)
        {
            if (objItm->emot && std::strcmp(objItm->label.c_str(), name.c_str()) == 0)
            {
                emtObj = objItm->emot;
                break;
            }
        }
    }

    if (emtObj)
    {
        // motion
        iTJSDispatch2* mtn = TVPCreateNativeClass_TmpMotionObj(this, emtObj);
        if (mtn)
        {
            tTJSVariant mtnVar(mtn);
            dsp->PropSet(TJS_MEMBERENSURE, "motion", nullptr, &mtnVar, dsp);
            mtn->Release();
        }
    }
    
    tTJSVariant var(dsp);
    dsp->Release();
    return var;
}
void EmotePlayer::setOpenGLDrawArea(tjs_int width, tjs_int height)
{
    // fbo
    if (fbo == 0 || glIsFramebuffer(fbo) != GL_TRUE)
        glGenFramebuffers(1, &fbo);
    if (superfbo == 0 || glIsFramebuffer(superfbo) != GL_TRUE)
        glGenFramebuffers(1, &superfbo);

    // 材质
    GLuint newfbotexture = createEmptyTexture(width, height);
    GLuint newfbodepthtexture = createEmptyDepthTexture(width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, newfbotexture, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, newfbodepthtexture,
                           0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        SDL_Log("Framebuffer不完整!");
    }
    // 模板fbo
    GLuint newsuperfbotexture = createEmptyTexture(width, height);
    GLuint newsuperfbodepthtexture = createEmptyDepthTexture(width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, superfbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, newsuperfbotexture,
                           0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           newsuperfbodepthtexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        SDL_Log("Framebuffer不完整!");
    }
    // 清理旧物
    if (fbotexture != 0 && glIsTexture(fbotexture) == GL_TRUE)
    {
        glDeleteTextures(1, &fbotexture);
    }
    if (fbodepthtexture != 0 && glIsTexture(fbodepthtexture) == GL_TRUE)
    {
        glDeleteTextures(1, &fbodepthtexture);
    }
    if (superfbotexture != 0 && glIsTexture(superfbotexture) == GL_TRUE)
    {
        glDeleteTextures(1, &superfbotexture);
    }
    if (superfbodepthtexture != 0 && glIsTexture(superfbodepthtexture) == GL_TRUE)
    {
        glDeleteTextures(1, &superfbodepthtexture);
    }
    // 赋予魔法
    fbotexture = newfbotexture;
    fbodepthtexture = newfbodepthtexture;
    superfbotexture = newsuperfbotexture;
    superfbodepthtexture = newsuperfbodepthtexture;
}
void EmotePlayer::updateTransMat()
{
    _renderMethod.type = 3;
    _renderMethod.opa = 1.0f;
    // 构建变换矩阵
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(currCoordx + currCamX, currCoordy + currCamY, 0));
    model = glm::rotate(model, glm::radians(currAngle), glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::scale(model, glm::vec3(currZx, currZy, 1.0f));
    _renderMethod.matTrans = _affineTrans * model;
}
void EmotePlayer::ResetDrawArea(tjs_int width, tjs_int height)
{
    if (_width != width || _height != height)
    {
        // limit
        _limitArea.originX = 0;
        _limitArea.originY = 0;
        _width = width;
        _height = height;
        _limitArea.width = width;
        _limitArea.height = height;
        if (_currentfile != nullptr)
        {
            _limitArea.zMax = _currentfile->getZMax(true) * 2;
        }
        if (_limitArea.zMax < 30.0f)
            _limitArea.zMax = 30.0f;
        // bitmap
        if (m_BmpBits != nullptr)
            delete (tTVPBaseTexture*)m_BmpBits;
        m_BmpBits = new tTVPBaseTexture(_width, _height, 32);
        if (m_bmpData != nullptr)
            delete[] m_bmpData;
        m_bmpData = new tjs_uint8[_width * _height * 4];
        // transForm
        updateTransMat();
        // setopengl
        if (withoutAdaptor)
            setOpenGLDrawArea(_width, _height);
    }
}

} // namespace emoteplayer
