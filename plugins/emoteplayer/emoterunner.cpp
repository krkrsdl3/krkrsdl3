#include "emoterunner.h"
#include "emoteframeselect.h"

#include <cstdint>
#include "tjsArray.h"

#include "Platform.h"

namespace emoteplayer
{
namespace
{
tTJSVariant stateArray()
{
    auto* array = TJSCreateArrayObject();
    tTJSVariant value(array, array); array->Release(); return value;
}
void stateAppend(tTJSVariant& array, const tTJSVariant& value)
{
    auto* object = array.AsObjectNoAddRef();
    tTJSVariant item = value;
    tTJSVariant* args[] = {&item};
    object->FuncCall(0, TJS_N("add"), nullptr, nullptr, 1, args, object);
}
tTJSVariant stateItem(const tTJSVariant& array, int index)
{
    tTJSVariant value;
    if (array.Type() == tvtObject && array.AsObjectNoAddRef())
        array.AsObjectNoAddRef()->PropGetByNum(0, index, &value, array.AsObjectNoAddRef());
    return value;
}
int stateCount(const tTJSVariant& array)
{
    tTJSVariant count;
    if (array.Type() != tvtObject || !array.AsObjectNoAddRef() ||
        TJS_FAILED(array.AsObjectNoAddRef()->PropGet(0, TJS_N("count"), nullptr, &count, array.AsObjectNoAddRef())) ||
        count.Type() == tvtVoid) return 0;
    return std::clamp(int((tjs_int)count), 0, 4096);
}
tTJSVariant saveTween(const EmoteTween& tween)
{
    auto value = stateArray();
    for (double field : {tween.from, tween.to, tween.start, tween.duration, tween.easing})
        stateAppend(value, tTJSVariant(field));
    return value;
}
void loadTween(const tTJSVariant& value, EmoteTween& tween)
{
    if (stateCount(value) != 5) return;
    double fields[5];
    for (int i = 0; i < 5; ++i)
    {
        fields[i] = (tjs_real)stateItem(value, i);
        if (!std::isfinite(fields[i])) return;
    }
    tween = {fields[0], fields[1], fields[2], std::max(0.0, fields[3]), fields[4]};
}
tTJSVariant saveVariables(const std::map<std::string, float>& variables)
{
    auto array = stateArray();
    for (const auto& entry : variables)
    {
        auto pair = stateArray();
        stateAppend(pair, tTJSVariant(ttstr(entry.first.c_str())));
        stateAppend(pair, tTJSVariant(entry.second));
        stateAppend(array, pair);
    }
    return array;
}
void loadVariables(const tTJSVariant& array, std::map<std::string, float>& variables)
{
    for (int i = 0; i < stateCount(array); ++i)
    {
        const auto pair = stateItem(array, i);
        if (stateCount(pair) != 2) continue;
        const auto key = ttstr(stateItem(pair, 0)).AsStdString();
        const double number = (tjs_real)stateItem(pair, 1);
        if (std::isfinite(number)) variables[key] = number;
    }
}
}

tTJSVariant emoteengine::serializeControls() const
{
    auto state = stateArray();
    stateAppend(state, tTJSVariant(controlTick));
    stateAppend(state, saveVariables(baseVariables));
    auto tweens = stateArray();
    for (const auto& entry : variableTweens)
    {
        auto pair = stateArray();
        stateAppend(pair, tTJSVariant(ttstr(entry.first.c_str())));
        stateAppend(pair, saveTween(entry.second)); stateAppend(tweens, pair);
    }
    stateAppend(state, tweens);
    auto timelines = stateArray();
    for (const auto& playing : currTimeline)
    {
        auto item = stateArray();
        stateAppend(item, tTJSVariant(ttstr(playing.timeline->label.c_str())));
        stateAppend(item, tTJSVariant(playing.flags));
        stateAppend(item, tTJSVariant(playing.startTick));
        stateAppend(item, saveTween(playing.blend));
        stateAppend(item, tTJSVariant(playing.stopWhenBlended));
        stateAppend(item, saveVariables(playing.initialVariables));
        stateAppend(timelines, item);
    }
    stateAppend(state, timelines);
    return state;
}

void emoteengine::unserializeControls(const tTJSVariant& state)
{
    if (stateCount(state) != 4) return;
    const double tick = (tjs_real)stateItem(state, 0);
    if (!std::isfinite(tick) || tick < 0) return;
    // Keep the physics rules supplied to initPhysics(), including script edits.
    // Restoring a pose must not replace them with the cached file's templates.
    currTimeline.clear(); variableTweens.clear(); _varCache.clear(); baseVariables.clear();
    if (_mainfile && _mainfile->_metadata)
        for (const auto& entry : _mainfile->_metadata->_varList)
            baseVariables[entry.first.substr(0, entry.first.find('\0'))] = entry.second;
    controlTick = tick;
    loadVariables(stateItem(state, 1), baseVariables);
    controlVariables = baseVariables;
    const auto tweens = stateItem(state, 2);
    for (int i = 0; i < stateCount(tweens); ++i)
    {
        const auto pair = stateItem(tweens, i);
        if (stateCount(pair) != 2) continue;
        const auto key = ttstr(stateItem(pair, 0)).AsStdString();
        loadTween(stateItem(pair, 1), variableTweens[key]);
        _varCache[key + std::string(1, '\0')] = variableTweens[key].to;
    }
    const auto timelines = stateItem(state, 3);
    for (int i = 0; i < stateCount(timelines); ++i)
    {
        const auto item = stateItem(timelines, i);
        if (stateCount(item) != 6) continue;
        const auto label = ttstr(stateItem(item, 0)).AsStdString();
        const size_t before = currTimeline.size();
        startTimeline((tjs_real)stateItem(item, 2), label, (tjs_int)stateItem(item, 1));
        if (currTimeline.size() == before) continue;
        auto& playing = currTimeline.back();
        loadTween(stateItem(item, 3), playing.blend);
        playing.stopWhenBlended = (tjs_int)stateItem(item, 4) != 0;
        loadVariables(stateItem(item, 5), playing.initialVariables);
    }
    updateTimelineControl(controlTick);
}

void emotenoderef::checkDrawStatus(float tick, std::vector<emoteRender>& renderList, emotelimit lim)
{
    // 不绘制进行节点传递
    if (renderList.size() > 0 && renderList.back().type == 0)
    {
        isNeedDraw = false;
        return;
    }

    // 确定时间轴
    if (currentNode->frameList.size() == 0)
    {
        isNeedDraw = false;
        return;
    }
    const auto selected = selectEmoteNodeFrames(currentNode->frameList, tick, currentNode->type == 1);
    frame = selected.first;
    nextframe = selected.second;
    if (frame == nullptr || !frame->hasContent)
    {
        isNeedDraw = false;
        return;
    }

    // 节点基础信息获取
    isNeedDraw = true;
    isIcon = false;
    isLayout = false;
    isShape = false;
    emoteicon* tmpic = currentNode-> _filePtr->findsourceByName(frame->src);
    if (tmpic == nullptr)
        currentMtn = refTop->findmotionByName(frame->src);
    else
        currentMtn = nullptr;
    if (tmpic != nullptr)
    {
        isIcon = true;

        if (tmpic != ic) // 直接比对ic
        {
            ic = tmpic;
            if (!refTop->samplingPhysics) ic->ensureLoad();
            width = ic->width;
            height = ic->height;
            originX = ic->originX;
            originY = ic->originY;
        }
        // 设置混色
        currbm = frame->bm;
    }
    else if (currentMtn != nullptr || strcmp(frame->src.c_str(), "layout") == 0 ||
             strcmp(frame->src.c_str(), "clip") == 0)
    {
        // 部分导出的 PSB 会剥离 shape 帧的 src 字段
        //（content 仅剩 coord/mask/zx/zy），解析后 src 落到默认值 "layout"，
        // 此前一律当 layout 处理，shape 节点（node type==1）的判定区域
        // 收集不到。type==1 即 shape 层，按 shape 处理并记录判定区域
        if (strcmp(frame->src.c_str(), "layout") == 0 && currentNode->type == 1)
        {
            isShape = true;
            isNeedDraw = false;
            // shape 像素尺寸 = zx/zy * 16 (PSB 规范: 单元正方形为 16x16)
            width = height = 16.0f;
            originX = width / 2;
            originY = height / 2;
        }
        else
        {
            isLayout = true;

            // 直接用父类提供的区域
            if (width != lim.width || height != lim.height)
            {
                width = lim.width;
                height = lim.height;
                originX = lim.originX;
                originY = lim.originY;
            }
        }
    }
    else
    {
        std::istringstream iss(frame->src);
        std::string token;
        std::getline(iss, token, '/');
        if (token == "src")
        {
            // emote 拼接前缀 "src/<type>/<name>"，跳过第一段。
            // 此前直接拿 "src" 判类型，落入 unsupported 分支被丢弃，
            // shape 判定层因此收集不到区域
            std::getline(iss, token, '/');
        }
        if (strcmp(token.c_str(), "blank") == 0)
        {
            std::getline(iss, token, ':');
            int32_t w = std::stoi(token);
            std::getline(iss, token, ':');
            int32_t h = std::stoi(token);
            std::getline(iss, token, ':');
            int32_t ox = std::stoi(token);
            std::getline(iss, token, ':');
            int32_t oy = std::stoi(token);

            if (width != w || height != h)
            {
                width = w;
                height = h;
            }
            originX = ox;
            originY = oy;
        }
        else if (strcmp(token.c_str(), "shape") == 0)
        {
            // shape节点: 不绘制但记录区域信息
            // shape的像素尺寸 = zx/zy * 16 (PSB规范: 单元正方形为16x16)
            // shape子类型从src的第二部分获取: rect(默认)、circle、point、quad
            isShape = true; // 标记 shape 判定层
            isNeedDraw = false;
            width = height = 16.0f;
            originX = width / 2;
            originY = height / 2;
        }
        else
        {
            TVPConsoleLog("source unsupported!!!--->%s", frame->src.c_str());
            isNeedDraw = false;
            return;
        }
    }
}
void emotenoderef::progress(float tick, std::vector<emoteRender>& renderList, emotelimit lim)
{
    // 参数化时可能改变
    currTick = tick;
    // 对于motion，增加终结机制, 即无法越过selfSyncTime
    if (currentNode->_filePtr->isMotion && currTick > currentNode->_rootmotion->selfSyncTime &&
        currentNode->frameList.size() > 1 &&
        currentNode->frameList.at(currentNode->frameList.size() - 2)->type == 2 &&
        currentNode->frameList.at(currentNode->frameList.size() - 1)->type == 0)
        currTick = currentNode->_rootmotion->selfSyncTime;
    // 再来一个非时间戳节点, 采用时间永驻机制
    if (currentNode->_filePtr->isMotion && currentNode->frameList.size() == 2 &&
        currentNode->frameList.at(0)->type == 2 && currentNode->frameList.at(1)->type == 0)
        currTick = currentNode->frameList.at(0)->time;
    // 再来一个3->0的拦截机制，感觉确实未能理解motion的运行，只能凑合着搞了
    if (currentNode->_filePtr->isMotion && currentNode->frameList.size() > 1 &&
        currentNode->frameList.at(currentNode->frameList.size() - 1)->type == 0 &&
        currentNode->frameList.at(currentNode->frameList.size() - 2)->type == 3)
    {
        currTick = std::min(
            currTick, (float)currentNode->frameList.at(currentNode->frameList.size() - 2)->time);
    }
    // 不会处理，先跳过
    if (currentNode->type == 12)
    {
        checkDrawStatus(currTick, renderList, lim);
        isNeedDraw = true;
        isLayout = true;
        originX = lim.originX;
        originY = lim.originY;
        width = lim.width;
        height = lim.height;
    }
    // 对于参数节点 进行参数反查来定位tick
    else if (currentNode->isParameterize)
    {
        if (refMtn != nullptr)
        {
            float new_tick = refMtn->getTickByIdx(currentNode->parameterIdx);
            if (new_tick >= 0.0)
            {
                checkDrawStatus(new_tick, renderList, lim);
                currTick = new_tick;
            }
            else
                isNeedDraw = false;
        }
        else
            isNeedDraw = false;
    }
    else
    {
        // 时间戳判断 绘制信息检测
        checkDrawStatus(currTick, renderList, lim);
    }

    // 注意：这里不能提前 return！官方实现中 isNeedDraw=false 的节点
    // 仍会继续构建 renderMethod 并递归所有子节点（父容器节点某些帧无内容
    // 但子孙 icon 可见是正常结构）。提前剪枝会导致整棵子树不被 progress，
    // 子孙部件消失；蒙版层（layerNode）不被绘制时 hasStencil 会被置 false，
    // 主体部件失去蒙版裁剪，纯色填充部件（bm21）以完整形态显示为色块。
    // 子孙的可见性由它们自己的 checkDrawStatus 与 draw() 中的 isNeedDraw 判定。
    // 修复（修复部分游戏emote立绘消失以及DRACU-RIOT!菜单问题。）

    // 构建渲染方法
    renderMethod.clear();
    renderMethod = renderList;
    if ((!isNeedDraw && (width == 0 || height == 0)) || currentNode->type == 7)
    {
        originX = lim.originX;
        originY = lim.originY;
        width = lim.width;
        height = lim.height;
    }
    else
    {
        // 基础参数
        if (nextframe != nullptr &&
            ((frame->type != 2) ||
             (frame->type == 2 && nextframe->type == 2))) // 存在下一帧则对关键帧进行插值
        {
            // 针对nan/inf情形动态完成刷新
            if (std::isnan(frame->coordX))
                frame->coordX = -lim.originX;
            if (std::isnan(frame->coordY))
                frame->coordY = -lim.originY;
            if (std::isnan(nextframe->coordX))
                nextframe->coordX = -lim.originX;
            if (std::isnan(nextframe->coordY))
                nextframe->coordY = -lim.originY;
            if (std::isinf(frame->coordX))
                frame->coordX = lim.width - lim.originX;
            if (std::isinf(frame->coordY))
                frame->coordY = lim.height - lim.originY;
            if (std::isinf(nextframe->coordX))
                nextframe->coordX = lim.width - lim.originX;
            if (std::isinf(nextframe->coordY))
                nextframe->coordY = lim.height - lim.originY;

            // 坐标
            currCoordx =
                (frame->coordX + (nextframe->coordX - frame->coordX) /
                                     (nextframe->time - frame->time) * (currTick - frame->time));
            currCoordy =
                (frame->coordY + (nextframe->coordY - frame->coordY) /
                                     (nextframe->time - frame->time) * (currTick - frame->time));
            currCoordz =
                (frame->coordZ + (nextframe->coordZ - frame->coordZ) /
                                     (nextframe->time - frame->time) * (currTick - frame->time));
            // 透明度
            currOpa = (frame->opa + (nextframe->opa - frame->opa) /
                                        (nextframe->time - frame->time) * (currTick - frame->time));
            // 变换参数(太sb了，感觉180才是分界点)
            if (nextframe->angle < 180 && frame->angle > 180) // 从 小360 到大0
            {
                currAngle = (frame->angle - 360 +
                             (nextframe->angle + 360 - frame->angle) /
                                 (nextframe->time - frame->time) * (currTick - frame->time));
            }
            else if (nextframe->angle > 180 && frame->angle < 180) // 从 大0 到 小360
            {
                currAngle =
                    (frame->angle + (nextframe->angle - 360 - frame->angle) /
                                        (nextframe->time - frame->time) * (currTick - frame->time));
            }
            else
            {
                currAngle =
                    (frame->angle + (nextframe->angle - frame->angle) /
                                        (nextframe->time - frame->time) * (currTick - frame->time));
            }
            currSx = (frame->sx + (nextframe->sx - frame->sx) / (nextframe->time - frame->time) *
                                      (currTick - frame->time));
            currSy = (frame->sy + (nextframe->sy - frame->sy) / (nextframe->time - frame->time) *
                                      (currTick - frame->time));
            currZx = (frame->zx + (nextframe->zx - frame->zx) / (nextframe->time - frame->time) *
                                      (currTick - frame->time));
            currZy = (frame->zy + (nextframe->zy - frame->zy) / (nextframe->time - frame->time) *
                                      (currTick - frame->time));
            currOx = (frame->ox + (nextframe->ox - frame->ox) / (nextframe->time - frame->time) *
                                      (currTick - frame->time));
            currOy = (frame->oy + (nextframe->oy - frame->oy) / (nextframe->time - frame->time) *
                                      (currTick - frame->time));
            // 偏移
            currTimeOffset = (frame->timeOffset + (nextframe->timeOffset - frame->timeOffset) /
                                                      (nextframe->time - frame->time) *
                                                      (currTick - frame->time));
            // 网格参数
            if (frame->hasbp || nextframe->hasbp)
            {
                isNeedBp = true;
                for (size_t i = 0; i < 32; i++)
                    currbp[i] = (frame->bp[i] + (nextframe->bp[i] - frame->bp[i]) /
                                                    (nextframe->time - frame->time) *
                                                    (currTick - frame->time));
            }
            else
                isNeedBp = false;
        }
        else
        {
            // 针对nan/inf情形动态完成刷新
            if (std::isnan(frame->coordX))
                frame->coordX = -lim.originX;
            if (std::isnan(frame->coordY))
                frame->coordY = -lim.originY;
            if (std::isinf(frame->coordX))
                frame->coordX = lim.width - lim.originX;
            if (std::isinf(frame->coordY))
                frame->coordY = lim.height - lim.originY;

            // 计算坐标
            currCoordx = frame->coordX;
            currCoordy = frame->coordY;
            currCoordz = frame->coordZ;
            // 透明度
            currOpa = frame->opa;
            // 变换参数
            currAngle = frame->angle;
            currSx = frame->sx, currSy = frame->sy;
            currZx = frame->zx, currZy = frame->zy;
            currOx = frame->ox, currOy = frame->oy;
            // 偏移
            currTimeOffset = frame->timeOffset;
            // 网格参数
            if (frame->hasbp)
            {
                isNeedBp = true;
                for (size_t i = 0; i < 32; i++)
                    currbp[i] = frame->bp[i];
            }
            else
                isNeedBp = false;
        }

        // 有深度信息时，穿透到最顶层
        if (renderMethod.size() > 0 && currCoordz != 0.0)
        {
            renderMethod.at(0).attachMat =
                glm::translate(renderMethod.at(0).attachMat, glm::vec3(0.0, 0.0, currCoordz));
        }

        // 有模板信息时，穿透到最顶层
        if (renderMethod.size() > 0 && currentNode->stencilCompositeMaskLayerList.size() > 0 && currentNode->type == 12)
        {
            renderMethod.at(0).hasStencil = true;
            for (auto nodeName : currentNode->stencilCompositeMaskLayerList)
            {
                // 让父类去找节点
                emotenoderef* tmpNode = refMtn->getNodeRef(currentNode->_rootmotion->getNodeByName(nodeName));
                if (tmpNode != nullptr)
                {
                    renderMethod.at(0).layerNode.push_back(tmpNode);
                }
            }
        }

        // Shape zoom stays in the shared transform, including frame interpolation.
        if (isShape && frame->src.find("shape/point") != std::string::npos)
            width = height = 2.0f; // one local unit around the node's center
        if (isShape)
        {
            originX = width * 0.5f;
            originY = height * 0.5f;
        }

        // 渲染信息记录
        emoteRender emt;
        if (isLayout)
            emt.type = 3;
        else
            emt.type = 2;
        if (isNeedBp)
        {
            for (size_t i = 0; i < 32; i++)
                emt.controlPts[i] = currbp[i];
            emt.type = 1;
        }
        emt.opa = currOpa;
        // 矩阵计算信息
        emt.currCoordx = currCoordx;
        emt.currCoordy = currCoordy;
        emt.currAngle = currAngle;
        emt.currSx = currSx;
        emt.currSy = currSy;
        emt.currZx = currZx;
        emt.currZy = currZy;
        emt.currOx = currOx;
        emt.currOy = currOy;
        emt.currInheritMask = currentNode->inheritMask;
        std::copy(std::begin(currentNode->transformOrder), std::end(currentNode->transformOrder),
                  std::begin(emt.transformOrder));
        // fbo信息
        emt.originX = originX;
        emt.originY = originY;
        emt.width = width;
        emt.height = height;
        emt.label = currentNode->label;
        renderMethod.push_back(emt);
    }

    if (refTop->samplingPhysics && frame && !currentNode->removed)
        refTop->recordPhysicsAnchor(currentNode->label, renderMethod);

    if (!refTop->samplingPhysics && isShape && frame && !currentNode->removed && refMtn && !renderMethod.empty())
    {
        emoterect area;
        area.label = currentNode->label;
        if (frame->src.find("shape/circle") != std::string::npos) area.shapeType = 1;
        else if (frame->src.find("shape/point") != std::string::npos) area.shapeType = 0;
        else if (frame->src.find("shape/quad") != std::string::npos) area.shapeType = 3;
        buildShapeMesh(renderMethod, area.shapeType, area.vertices, area.indices);
        const float vw = lim.viewW > 0 ? lim.viewW : renderMethod.front().width;
        const float vh = lim.viewH > 0 ? lim.viewH : renderMethod.front().height;
        float minX = 1, minY = 1, maxX = -1, maxY = -1;
        if (!area.vertices.empty())
        {
            minX = maxX = area.vertices.front().x;
            minY = maxY = area.vertices.front().y;
        }
        for (const auto& v : area.vertices)
        {
            minX = std::min(minX, v.x); maxX = std::max(maxX, v.x);
            minY = std::min(minY, v.y); maxY = std::max(maxY, v.y);
        }
        area.left = (minX + 1) * 0.5f * vw;
        area.top = (minY + 1) * 0.5f * vh;
        area.width = (maxX - minX) * 0.5f * vw;
        area.height = (maxY - minY) * 0.5f * vh;
        refMtn->shapeNodeAreas.push_back(std::move(area));
    }

    // 对于icon节点，在CPU上计算细分网格
    if (!refTop->samplingPhysics && isIcon && isNeedDraw && renderMethod.size() > 0)
    {
        // 确定细分等级
        int div = currentNode ? (int)currentNode->meshDivision : 0;
        if (div < 2) div = 8; // 默认8x8，匹配原曲面细分着色器的细分等级
        div = std::min(div, 254); // (div + 1)^2 must fit uint16_t indices
        // 检查是否包含mesh变形
        bool containsMesh = false;
        for (auto itm : renderMethod)
        {
            if (itm.type == 1)
            {
                containsMesh = true;
                break;
            }
        }
        // 变换
        if (containsMesh)
        {
            // 细分并变形
            _meshDivX = div;
            _meshDivY = div;
            buildSubdivMesh(renderMethod, _meshDivX, _meshDivY, _meshVertices, _meshIndices);
        }
        else
        {
            // 进行简单三角剖分
            buildRectMesh(renderMethod, _meshVertices, _meshIndices);
        }
    }
    else
    {
        _meshVertices.clear();
        _meshIndices.clear();
    }

    // 传递给子类: 通过_parentMotion查找对应ref，避免创建重复状态
    if (refMtn != nullptr)
    {
        for (auto ch : currentNode->children)
        {
            emotenoderef* childRef = refMtn->getNodeRef(ch);
            if (childRef)
            {
                // Preserve the target viewport through nested motions.
                childRef->progress(tick, renderMethod,
                                   {originX, originY, width, height, lim.zMax, lim.viewW,
                                    lim.viewH});
            }
        }

        // 处理子motion: 创建子emotemotionref递归处理
        if (currentMtn != nullptr)
        {
            // 在引擎中创建持久化的子motion ref
            currentMtnRef = new emotemotionref(currentMtn, refTop, this);
            refMtn->_subMotionRefs.push_back(currentMtnRef);
            currentMtnRef->progress(tick + currTimeOffset, renderMethod,
                            {originX, originY, width, height, lim.zMax, lim.viewW, lim.viewH});
        }
    }
}
bool emotenoderef::draw(krkrsdl3::iTVPRenderBackend* renderer, void* target, emotelimit lim, void* maskTarget)
{
    if (!isNeedDraw || !isIcon || renderMethod.size() < 1 || currentNode->removed)
        return false; // 跳过无需绘制的 和 非icon的 和 无method 的节点
    if (!renderer || !target || !ic || !ic->selftexture)
        return false;

    // 跳过空网格
    if (_meshVertices.empty() || _meshIndices.empty())
        return false;

    // 提前绘制好蒙版目标（不考虑复合蒙版的情况）
    if (renderMethod.at(0).hasStencil && maskTarget != 0)
    {
        renderer->SetTarget(maskTarget);
        renderer->ClearTarget(true);
        bool hasDraw = false;
        for (auto maskLayer : renderMethod.at(0).layerNode)
        {
            if (maskLayer != nullptr && maskLayer->currOpa > 0)
            {
                hasDraw = true;
                maskLayer->draw(renderer, maskTarget, lim, nullptr);
            }
        }
        // 排除异常蒙版
        if (!hasDraw)
            renderMethod.at(0).hasStencil = false;
    }

    // 主体绘制（SetTarget 内部完成视口/深度/混合基础状态设置）
    renderer->SetTarget(target);
    renderer->SetMask(renderMethod.at(0).hasStencil ? maskTarget : nullptr);

    // 透明度与混色
    float totalOpa = currOpa;
    for (size_t i = 0; i < renderMethod.size(); i++)
        totalOpa *= renderMethod.at(i).opa;
    int blendMode = currbm;
    float uniformColor[4] = {0, 0, 0, 0};
    float colorModulation[4] = {1, 1, 1, 1};
    if (blendMode == 21 && frame)
    {
        uniformColor[0] = ((frame->color) & 0xFF) / 255.0f;
        uniformColor[1] = ((frame->color >> 8) & 0xFF) / 255.0f;
        uniformColor[2] = ((frame->color >> 16) & 0xFF) / 255.0f;
        uniformColor[3] = ((frame->color >> 24) & 0xFF) / 255.0f;
    }
    else if (frame && frame->hasColor && static_cast<uint32_t>(frame->color) != 0xff808080u)
    {
        // bm!=21 时 color 是乘性染色。0xff808080 是 PSB 中性灰，必须保持原图；
        // 未写 color 时 hasColor=false，也不能用默认 0 去乘（会把虹膜乘成黑/灰）。
        const uint32_t tint = static_cast<uint32_t>(frame->color);
        colorModulation[0] = (tint & 0xFF) / 255.0f;
        colorModulation[1] = ((tint >> 8) & 0xFF) / 255.0f;
        colorModulation[2] = ((tint >> 16) & 0xFF) / 255.0f;
        colorModulation[3] = ((tint >> 24) & 0xFF) / 255.0f;
    }
    renderer->SetBlendMode(blendMode, blendMode == 21 ? uniformColor : nullptr);
    if (blendMode == 6 || totalOpa <= 0)
        return false; // 该模式不绘制（GL 后端原行为）

    // 绘制网格（MeshVertex 布局与接口的交错 xyuv 格式一致，直接传递）
    renderer->DrawMesh((const float*)_meshVertices.data(), (int)_meshVertices.size(),
                       _meshIndices.data(), (int)_meshIndices.size(), ic->selftexture, totalOpa,
                       colorModulation);
    return true;
}
float emotenoderef::getCurrentRenderZ()
{
    if (renderMethod.size() > 0)
    {
        return renderMethod.at(0).attachMat[3][2];
    }
    return 0;
}

emotemotionref::~emotemotionref()
{
    for (auto sub : _subMotionRefs)
        delete sub;
    _subMotionRefs.clear();
}
float emotemotionref::getTickByIdx(int32_t idx)
{
    if (idx >= currentMotion->parameter.size() || idx < 0)
        return -1.0f;
    float currVal = 0;
    // file系控制
    if (!refTop->getControlVariable(currentMotion->parameter.at(idx)->id, currVal) &&
        !currentMotion->_filePtr->getTickByName(currentMotion->parameter.at(idx)->id, currVal))
    {
        if (currentMotion->_filePtr->isMotion)
        {
            // mtn系控制
            auto itmMap = currentMotion->parameterCache.find(currentMotion->parameter.at(idx)->id);
            if (itmMap != currentMotion->parameterCache.end())
            {
                if (currentMotion->lastTime < 0)
                    return itmMap->second;
                else
                {
                    emoteVar* tmpV = currentMotion->parameter.at(idx);
                    return itmMap->second * currentMotion->lastTime / (tmpV->rangeEnd + 1 - tmpV->rangeBegin); 
                }
            }
            if (parent && parent->refMtn->parent)
            {
                // label逆查找的构造系列(两层)
                std::string fullvals = parent->refMtn->parent->currentNode->label + "/" +
                                       parent->currentNode->label + "/" +
                                       currentMotion->parameter.at(idx)->id;
                fullvals.erase(std::remove(fullvals.begin(), fullvals.end(), '\0'), fullvals.end());
                fullvals.append(1, '\0');
                tjs_real retV = 0.0;
                if (refTop->getTickByName(fullvals, retV))
                {
                    return retV;
                }

                // label逆查找的构造系列(一层)
                fullvals = parent->refMtn->parent->currentNode->label + "/" +
                                       currentMotion->parameter.at(idx)->id;
                fullvals.erase(std::remove(fullvals.begin(), fullvals.end(), '\0'), fullvals.end());
                fullvals.append(1, '\0');
                retV = 0.0;
                if (refTop->getTickByName(fullvals, retV))
                {
                    return retV;
                }
            }
        }
    }
    return currentMotion->parameter.at(idx)->transToTick(currVal);
}
emotenoderef* emotemotionref::getNodeRef(emotenode* node)
{
    // 在_nodeCache中查找指定node的ref
    for (auto& ref : _nodeCache)
    {
        if (ref.currentNode == node)
            return &ref;
    }
    return nullptr;
}
void emotemotionref::progress(float tick, std::vector<emoteRender>& renderList, emotelimit lim)
{
    // 起始
    shapeNodeAreas.clear();
    renderMethod.clear();
    renderMethod = renderList;

    // 按priority顺序构建_nodeCache
    _nodeCache.clear();
    if (currentMotion == nullptr) return;
    size_t count = currentMotion->nodeList.size();
    _nodeCache.reserve(count);
    for (size_t i = 0; i < count; i++)
    {
        _nodeCache.emplace_back(currentMotion->nodeList[i], refTop, this);
    }

    // 清理旧的子motion ref
    for (auto sub : _subMotionRefs)
        delete sub;
    _subMotionRefs.clear();

    //  对每个layer节点调用对应的ref->progress
    //  注意: ref的progress内部会通过_parentMotion递归处理children和sub-motion
    for (auto ch : currentMotion->layer)
    {
        float actualTick = tick;
        if (currentMotion->loopTime >= 0)
        {
            if (currentMotion->loopTime == 0)
                actualTick = std::fmod(tick, currentMotion->lastTime);
            else
                actualTick = std::fmod(tick, currentMotion->loopTime);
        }

        std::vector<emoteRender> localRender = renderList;
        if (localRender.size() > 0 && ch->type == 2)
        {
            localRender.at(0).hasStencil = false;
            localRender.at(0).layerNode.clear();
        }

        emotenoderef* ref = getNodeRef(ch);
        if (ref)
        {
            ref->progress(actualTick, localRender, lim);
        }
    }
}
void emotemotionref::draw(krkrsdl3::iTVPRenderBackend* renderer, void* target, emotelimit lim, void* maskTarget)
{
    if (_nodeCache.empty()) return;

    // 递归收集所有可绘制ref(展开嵌套子motion)
    std::vector<emotenoderef*> drawList;
    // 先展开所有的motion情形
    std::vector<emotenoderef*> stack;
    for (auto it = _nodeCache.begin();
        it != _nodeCache.end(); ++it)
    {
        stack.push_back(&(*it));
    }
    // 再递归获取全部
    while (!stack.empty())
    {
        emotenoderef* current = stack.back();
        stack.pop_back();

        if (current->currentMtn == nullptr)
        {
            drawList.push_back(current);
        }
        else if (current->currentMtnRef)
        {
            for (auto it = current->currentMtnRef->_nodeCache.begin();
                 it != current->currentMtnRef->_nodeCache.end();
                 ++it)
            {
                stack.push_back(&(*it));
            }
        }
    }

    // 按z排序(同dev分支)
    std::stable_sort(drawList.begin(), drawList.end(),
                     [](emotenoderef* a, emotenoderef* b)
                     { return a->getCurrentRenderZ() < b->getCurrentRenderZ(); });

    // 绘制
    for (auto r : drawList)
    {
        if (r != nullptr)
        {
            r->wasDrawn = r->draw(renderer, target, lim, maskTarget);
        }
    }
}
bool emotemotionref::contains(float x, float y, const char* label) const
{
    for (const auto& area : shapeNodeAreas)
        if ((!label || std::strcmp(area.label.c_str(), label) == 0) && area.contains(x, y))
            return true;
    for (const auto* sub : _subMotionRefs)
        if (sub->contains(x, y, label))
            return true;
    if (!label)
        for (const auto& node : _nodeCache)
            if (node.wasDrawn && containsMesh(node._meshVertices, node._meshIndices, x, y))
                return true;
    return false;
}

bool EmoteHitFrame::contains(const char* label, float x, float y, bool local) const
{
    if (!motion || width <= 0 || height <= 0)
        return false;
    if (local)
    {
        const auto clip = inputToClip * glm::vec4(x, y, 0, 1);
        x = clip.x;
        y = -clip.y; // same final Y flip as evaluateSurfaceChain
    }
    else
    {
        x = x * 2 / width - 1;
        y = y * 2 / height - 1;
    }
    // Geometry outside the rendered target is clipped by all rendering backends.
    return std::isfinite(x) && std::isfinite(y) && x >= -1 && x < 1 && y >= -1 && y < 1 &&
           motion->contains(x, y, label);
}

emoteengine::~emoteengine() = default;

void emoteengine::progress(float tick, std::vector<emoteRender>& renderList, emotelimit lim)
{
    _mainMotionRef.reset();
    if (!_mainmotion)
        return;
    _mainMotionRef = std::make_shared<emotemotionref>(_mainmotion, this);
    _mainMotionRef->progress(tick, renderList, lim);
}
void emoteengine::draw(krkrsdl3::iTVPRenderBackend* renderer, void* target, emotelimit lim, void* maskTarget)
{
    if (_mainMotionRef)
        _mainMotionRef->draw(renderer, target, lim, maskTarget);
}
bool emoteengine::getTickByName(const std::string& name, tjs_real& retVal)
{
    auto it = _varCache.find(name);
    if (it != _varCache.end())
    {
        retVal = it->second;
        return true;
    }
    return false;
}
void emoteengine::addEmoteFile(emotefile* itm)
{
    _attach.push_back(itm);
}
float emoteengine::getZMax()
{
    float _zMaxTmp = 0.0;
    if (_mainfile) _zMaxTmp = _mainfile->getZMax();
    // 附属
    for (auto itm : _attach)
    {
        if (itm->getZMax() > _zMaxTmp)
            _zMaxTmp = itm->getZMax();
    }
    return _zMaxTmp;
}
emotemotion* emoteengine::findmotionByName(const std::string& name)
{
    std::istringstream iss(name);
    std::string token, token1, token2;

    // 源头
    std::getline(iss, token, '/');
    if (strcmp(token.c_str(), "motion") == 0)
    {
        std::getline(iss, token1, '/');
        std::getline(iss, token2, '/');
        // 一级路径
        auto tmp = _mainfile->_objects.find(token1.c_str());
        if (tmp != _mainfile->_objects.end())
        {
            emoteobject* src = tmp->second;
            // 二级路径
            auto tmp1 = src->motion.find(token2.c_str());
            if (tmp1 != src->motion.end())
            {
                return tmp1->second;
            }
        }
        // 附属
        for (auto itm : _attach)
        {
            // 一级路径
            tmp = itm->_objects.find(token1.c_str());
            if (tmp != itm->_objects.end())
            {
                emoteobject* src = tmp->second;
                // 二级路径
                auto tmp1 = src->motion.find(token2.c_str());
                if (tmp1 != src->motion.end())
                {
                    return tmp1->second;
                }
            }
        }
        TVPConsoleLog("motion find failed!!!");
    }
    else
    {
        return nullptr;
    }
    return nullptr;
}
namespace
{
std::string controlKey(const std::string& name)
{
    return name.substr(0, name.find('\0'));
}

bool eyeCanBlink(double current, double closedValue)
{
    return current <= closedValue;
}

double eyeBlinkValue(double current, double closedValue, double ratio)
{
    if (!eyeCanBlink(current, closedValue)) return current;
    return current + (closedValue - current) * ratio;
}

bool timelineValue(const emoteTimeVar& variable, double tick, double initial, bool instant, double& value)
{
    const emoteTimeVarFrame* current = nullptr;
    const emoteTimeVarFrame* next = nullptr;
    double previous = initial;
    for (size_t i = 0; i < variable.frameList.size(); ++i)
    {
        const auto* frame = variable.frameList[i];
        if (frame->time > tick) { next = frame; break; }
        if (current && current->hasContent) previous = current->value;
        current = frame;
    }
    if (!current) return false;
    // Terminal empty keys delimit duration; interior empty keys hide the channel.
    if (!current->hasContent && current == variable.frameList.back())
    {
        for (auto it = variable.frameList.rbegin(); it != variable.frameList.rend(); ++it)
            if ((*it)->hasContent) { value = (*it)->value; return true; }
    }
    if (!current->hasContent) return false;
    value = current->value;
    // A timeline key requests a target to reach before the next key. It is
    // different from a geometry key, which describes the pose at its own time.
    if (!instant && next && next->time > current->time + 1)
        value = previous + (value - previous) * emoteEase((tick - current->time) /
                                      (next->time - current->time - 1), current->easing);
    return true;
}
}

void emoteengine::resetControls()
{
    controlTick = 0;
    currTimeline.clear(); baseVariables.clear(); controlVariables.clear(); variableTweens.clear(); _varCache.clear();
    eyes.clear();
    if (!_mainfile || !_mainfile->_metadata) return;
    const auto& metadata = *_mainfile->_metadata;
    for (const auto& entry : metadata._varList) baseVariables[controlKey(entry.first)] = entry.second;
    controlVariables = baseVariables;
    for (const auto* eye : metadata._eyeControl)
    {
        if (!eye->enabled || !eye->blinkEnabled) continue;
        eyes.push_back(*eye);
        auto& copy = eyes.back();
        copy.hasStart = copy.isBlinking = false;
        copy.currWaitInterval = -1;
    }
    physics.reset(metadata._bustControl, metadata._hairControl, metadata._partsControl);
}

bool emoteengine::getControlVariable(const std::string& name, float& value) const
{
    const auto entry = controlVariables.find(controlKey(name));
    if (entry == controlVariables.end()) return false;
    value = entry->second;
    return true;
}

void emoteengine::updateEyeControl(float tick, bool isMain)
{
    for (auto& eye : eyes)
    {
        const auto key = controlKey(eye.label);
        if (!eye.hasStart)
        {
            eye.hasStart = true;
            eye.lastTick = tick;
        }
        if (eye.currWaitInterval < 0) eye.currWaitInterval = eye.uid(random);
        const auto active = controlVariables.find(key);
        const double current = active == controlVariables.end() ? eye.beginFrame : active->second;
        if (!eye.isBlinking)
        {
            if (tick - eye.lastTick < eye.currWaitInterval) continue;
            if (!eyeCanBlink(current, eye.endFrame)) continue;
            eye.isBlinking = true;
            eye.lastTick = tick;
        }
        const double elapsed = tick - eye.lastTick;
        if (eye.blinkFrameCount <= 0 || elapsed >= eye.blinkFrameCount)
        {
            eye.isBlinking = false; eye.currWaitInterval = -1; eye.lastTick = tick;
            continue;
        }
        const double ratio = 1 - std::abs(2 * elapsed / eye.blinkFrameCount - 1);
        auto& value = controlVariables[key];
        value = eyeBlinkValue(value, eye.endFrame, ratio);
    }
}

void emoteengine::startTimeline(float tick, const std::string& name, int flags)
{
    if (!_mainfile || !_mainfile->_metadata) return;
    for (auto* timeline : _mainfile->_metadata->_timelineControl)
    {
        if (controlKey(timeline->label) != controlKey(name)) continue;
        stopTimeline(name);
        PlayingTimeline playing;
        playing.timeline = timeline; playing.startTick = tick;
        playing.flags = flags | 1 | (timeline->diff ? 2 : 0);
        if (!(playing.flags & 2)) playing.initialVariables = baseVariables;
        currTimeline.push_back(playing);
        return;
    }
}

void emoteengine::stopTimeline(const std::string& name, bool isMain)
{
    const auto key = controlKey(name);
    currTimeline.erase(std::remove_if(currTimeline.begin(), currTimeline.end(),
        [&](const PlayingTimeline& playing) { return key.empty() || controlKey(playing.timeline->label) == key; }),
        currTimeline.end());
}

bool emoteengine::checkTimline(const std::string& name, bool& result, bool isMain)
{
    result = false;
    const auto key = controlKey(name);
    for (const auto& playing : currTimeline)
        if (key.empty() || controlKey(playing.timeline->label) == key) { result = true; break; }
    return true;
}

void emoteengine::blendTimeline(const std::string& name, double ratio, double time, double easing, bool stopWhenDone)
{
    for (auto& playing : currTimeline)
        if (controlKey(playing.timeline->label) == controlKey(name))
        {
            playing.blend.set(playing.blend.value(controlTick), ratio, controlTick, time, easing);
            playing.stopWhenBlended = stopWhenDone;
        }
}

void emoteengine::updateTimelineControl(float tick, bool isMain)
{
    for (auto it = variableTweens.begin(); it != variableTweens.end();)
    {
        baseVariables[it->first] = it->second.value(tick);
        if (it->second.done(tick)) it = variableTweens.erase(it);
        else ++it;
    }
    controlVariables = baseVariables;
    // Main channels establish the pose; difference channels add to it. Rebuild
    // from the inputs every frame so a breathing loop never accumulates drift.
    for (int difference = 0; difference < 2; ++difference)
    {
        for (auto& playing : currTimeline)
        {
            if (bool(playing.flags & 2) != bool(difference)) continue;
            auto* timeline = playing.timeline;
            double local = std::max(0.0, tick - playing.startTick);
            if (timeline->loopBegin >= 0 && timeline->loopEnd > timeline->loopBegin && local >= timeline->loopEnd)
                local = timeline->loopBegin + std::fmod(local - timeline->loopBegin,
                                                      timeline->loopEnd - timeline->loopBegin);
            else if (timeline->lastTime >= 0) local = std::min(local, double(timeline->lastTime));
            const double ratio = playing.blend.value(tick);
            for (const auto* variable : timeline->variableList)
            {
                double value;
                const auto initial = playing.initialVariables.find(controlKey(variable->label));
                const auto& instant = _mainfile->_metadata->instantVariables;
                const bool immediate = std::find(instant.begin(), instant.end(), controlKey(variable->label)) != instant.end();
                if (!timelineValue(*variable, local, initial == playing.initialVariables.end() ? 0 : initial->second, immediate, value)) continue;
                auto& output = controlVariables[controlKey(variable->label)];
                if (difference) output += value * ratio;
                else output += (value - output) * ratio;
            }
        }
    }
    currTimeline.erase(std::remove_if(currTimeline.begin(), currTimeline.end(), [&](const PlayingTimeline& playing)
    {
        const auto* timeline = playing.timeline;
        const bool looping = timeline->loopBegin >= 0 && timeline->loopEnd > timeline->loopBegin;
        const bool finished = !looping && timeline->lastTime >= 0 && tick - playing.startTick >= timeline->lastTime;
        if (finished && !(playing.flags & 2))
            for (const auto* variable : timeline->variableList)
            {
                const auto key = controlKey(variable->label);
                const auto initial = playing.initialVariables.find(key);
                const auto& instant = _mainfile->_metadata->instantVariables;
                const bool immediate = std::find(instant.begin(), instant.end(), key) != instant.end();
                double value;
                if (timelineValue(*variable, timeline->lastTime,
                                  initial == playing.initialVariables.end() ? 0 : initial->second, immediate, value))
                    baseVariables[key] += (value - baseVariables[key]) * playing.blend.value(tick);
            }
        return finished || (playing.stopWhenBlended && playing.blend.done(tick));
    }), currTimeline.end());
}

void emoteengine::samplePhysics(float tick, const emoteRender& root, emotelimit lim)
{
    if (physics.empty() || !_mainmotion) return;
    physics.beginAnchors();
    samplingPhysics = true;
    struct Restore { bool& flag; ~Restore() { flag = false; } } restore{samplingPhysics};
    std::vector<emoteRender> chain{root};
    emotemotionref sample(_mainmotion, this);
    sample.progress(tick, chain, lim);
}

void emoteengine::recordPhysicsAnchor(const std::string& name, const std::vector<emoteRender>& chain)
{
    if (chain.empty() || !physics.needsAnchor(name)) return;
    float x, y;
    // Shape/point is centered at UV (.5, .5); use the same inherited placement
    // and Bezier evaluation as visible geometry, before camera/projection/affine.
    evaluateSurfaceChain(chain, 0.5f, 0.5f, x, y);
    physics.setAnchor(name, Vector3(x, -y));
}

void emoteengine::updatePhysics(float frames)
{
    physics.update(frames, [&](const std::string& name, double value)
    {
        if (!name.empty() && std::isfinite(value)) controlVariables[controlKey(name)] = value;
    });
}

bool emoteengine::controlsAnimating() const
{
    if (!variableTweens.empty()) return true;
    for (const auto& playing : currTimeline)
        if (!playing.blend.done(controlTick)) return true;
    return false;
}

void emoteengine::skipControls()
{
    for (auto& tween : variableTweens) tween.second.duration = 0;
    for (auto& playing : currTimeline) playing.blend.duration = 0;
    updateTimelineControl(controlTick);
}

emoteVar* emoteengine::findVarByName(const std::string& name)
{
    for (auto obj : _mainfile->_objects)
    {
        emoteVar* ret = nullptr;
        ret = obj.second->findVarByName(name);
        if (ret != nullptr)
            return ret;
    }
    return nullptr;
}
void emoteengine::setVariable(const std::string& name, tjs_real value, double time, double easing)
{
    if (!std::isfinite(value)) return;
    const auto key = controlKey(name);
    if (_mainfile && !_mainfile->isMotion)
    {
        for (const auto* selector : _mainfile->_metadata->_selectorControl)
        {
            if (controlKey(selector->lable) != key) continue;
            const int index = (int)value;
            if (index >= 0 && index < (int)selector->selectItem.size())
                for (size_t i = 0; i < selector->selectItem.size(); ++i)
                {
                    const auto& item = selector->selectItem[i];
                    setVariable(item.label, i == index ? item.onValue : item.offValue, time, easing);
                }
            return;
        }
        const float current = baseVariables.count(key) ? baseVariables[key] : 0;
        if (time > 0) variableTweens[key].set(current, value, controlTick, time, easing);
        else { variableTweens.erase(key); baseVariables[key] = value; controlVariables[key] = value; }
        _varCache[name] = value;
        return;
    }
    // 所有file
    std::vector<emotefile*> allFiles = _attach;
    allFiles.push_back(_mainfile);
    for (auto tmpFile : allFiles)
    {
        // 设置(metadata _varList / _selectorControl)
        tmpFile->setVariable(name, value);

        // motion_inter (name = "motionName/varName")
        size_t pos = name.find('/');
        if (pos != std::string::npos)
        {
            std::string motionName = name.substr(0, name.find('/'));
            emoteobject* obj = nullptr;
            for (auto objItm : _mainfile->_objects)
            {
                if (objItm.first == motionName)
                {
                    obj = objItm.second;
                    break;
                }
            }
            if (obj)
            {
                std::string varName = name.substr(name.find('/') + 1);
                for (auto mtnItm : obj->motion)
                {
                    for (auto varItm : mtnItm.second->parameter)
                    {
                        if (varItm->id == varName)
                        {
                            mtnItm.second->parameterCache[varItm->id] = value;
                            break;
                        }
                    }
                }
}
        }
        else
        {
            // 简单名(如"helptext"): 遍历所有motion的parameter, 匹配id后写入parameterCache
            if (tmpFile != nullptr)
            {
                for (auto& objPair : tmpFile->_objects)
                {
                    for (auto& mtnPair : objPair.second->motion)
                    {
                        for (auto varItm : mtnPair.second->parameter)
                        {
                            if (varItm->id == name)
                            {
                                mtnPair.second->parameterCache[varItm->id] = value;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    // 变量历史缓存
    _varCache[name] = value;
}
tjs_real emoteengine::getVariable(const std::string& name)
{
    const auto base = baseVariables.find(controlKey(name));
    if (base != baseVariables.end()) return base->second;
    float controlled;
    if (getControlVariable(name, controlled)) return controlled;
    size_t pos = name.find('/');
    if (_mainfile->isMotion) // motion类
    {
        size_t pos = name.find('/');
        if (pos != std::string::npos)
        {
            std::string motionName = name.substr(0, name.find('/'));
            emoteobject* obj = nullptr;
            for (auto objItm : _mainfile->_objects)
            {
                if (objItm.first == motionName)
                {
                    obj = objItm.second;
                    break;
                }
            }
            if (obj)
            {
                std::string varName = name.substr(name.find('/') + 1);
                for (auto mtnItm : obj->motion)
                {
                    for (auto varItm : mtnItm.second->parameter)
                    {
                        if (varItm->id == varName)
                        {
                            return mtnItm.second->parameterCache[varItm->id];
                        }
                    }
                }
            }
        }
        else
        {
            // 简单名(如"helptext"): 遍历所有motion的parameter, 匹配id后写入parameterCache
            for (auto& objPair : _mainfile->_objects)
            {
                for (auto& mtnPair : objPair.second->motion)
                {
                    for (auto varItm : mtnPair.second->parameter)
                    {
                        if (varItm->id == name)
                        {
                            return mtnPair.second->parameterCache[varItm->id];
                        }
                    }
                }
            }
        }
    }
    else // emote类
    {
        auto varPos = _mainfile->_metadata->_varList.find(name);
        if (varPos != _mainfile->_metadata->_varList.end())
        {
            return varPos->second;
        }
    }
    // 看看缓存
    tjs_real ret = 0.0;
    if (getTickByName(name, ret))
    {
        return ret;
    }
    return 0.0;
}

} // namespace emoteplayer
