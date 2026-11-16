#!/usr/bin/env bash
# 重新生成 vulkan_shaders.h 与 vulkan_shaders2d.h
#（由 GLSL 编译为 SPIR-V 并嵌入 C 数组）
#
# 依赖：glslc 或 glslang（Vulkan SDK / glslang release）+ python3
# 注意：两个头文件必须同时重生成——只更新窗口着色器会让 2D 网格/Layer 的
#       SPIR-V 与 .frag/.vert 源码脱节，表现为透明度、混合或坐标变换异常。
set -euo pipefail
cd "$(dirname "$0")"

# 优先 glslc；没有则退回 glslang（两者参数不同）
if command -v glslc >/dev/null 2>&1; then
    COMPILE() { glslc --target-env=vulkan1.0 -fshader-stage="$1" "$2" -o "$3"; }
    VALIDATE() { command -v spirv-val >/dev/null 2>&1 && spirv-val --target-env vulkan1.0 "$1" || true; }
else
    COMPILE() { glslang -V --target-env vulkan1.0 -S "$1" "$2" -o "$3"; }
    VALIDATE() { :; }
fi

COMPILE vert vk_quad.vert     vk_quad.vert.spv
COMPILE frag vk_quad.frag     vk_quad.frag.spv
COMPILE vert vk2d_quad.vert   vk2d_quad.vert.spv
COMPILE frag vk2d_quad.frag   vk2d_quad.frag.spv
COMPILE frag vk2d_layer.frag  vk2d_layer.frag.spv

for f in vk_quad.vert.spv vk_quad.frag.spv vk2d_quad.vert.spv vk2d_quad.frag.spv vk2d_layer.frag.spv; do
    VALIDATE "$f"
done

python3 - <<'PYEOF'
import struct

def to_c_array(path, name):
    data = open(path, 'rb').read()
    words = struct.unpack('<%dI' % (len(data)//4), data)
    lines = []
    for i in range(0, len(words), 8):
        lines.append('    ' + ', '.join('0x%08x' % w for w in words[i:i+8]) + ',')
    return f"static const uint32_t {name}[] = {{\n" + '\n'.join(lines) + "\n};"

# 窗口合成（vk_quad.*）
open('vulkan_shaders.h', 'w').write(f"""// 自动生成：由 GLSL 经 glslang/glslc --target-env=vulkan1.0 编译嵌入
// 重新生成：shader/regenerate.sh
// 源文件: vk_quad.vert / vk_quad.frag（同目录）
// 顶点着色器：单位四边形 + push constant 变换（与 GL 后端同一套像素坐标语义）
{to_c_array('vk_quad.vert.spv', 'kVulkanVertSpv')}

// 片元着色器：纹理采样
{to_c_array('vk_quad.frag.spv', 'kVulkanFragSpv')}
""")

# 2D 网格与 Layer 合成（vk2d_*）
open('vulkan_shaders2d.h', 'w').write(f"""// 自动生成：由 GLSL 经 glslang/glslc --target-env=vulkan1.0 编译嵌入
// 重新生成：shader/regenerate.sh
// 源文件: vk2d_quad.vert / vk2d_quad.frag
{to_c_array('vk2d_quad.vert.spv', 'kVulkan2DVertSpv')}

{to_c_array('vk2d_quad.frag.spv', 'kVulkan2DFragSpv')}

// Layer 合成片元着色器（软件 RenderManager bm* 语义）
// 源文件: shader/vk2d_layer.frag
{to_c_array('vk2d_layer.frag.spv', 'kVulkan2DLayerFragSpv')}
""")
print("vulkan_shaders.h / vulkan_shaders2d.h regenerated")
PYEOF
