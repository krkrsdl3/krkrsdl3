# 介绍

基于[krkr2](https://github.com/2468785842/krkr2)/[krkrz](https://github.com/krkrz/krkrz/)/[kirikiroid2](https://github.com/zeas2/Kirikiroid2)等开发出的krkr视觉小说引擎(支持windows/linux/android)，使用SDL3统一跨平台管理、opengl进行gpu渲染、blend2d进行cpu绘图。

# 目录结构说明

```
📁android/ # 安卓工程文件夹
📁cpp/ # 主要代码文件夹
├── 📁 core/ # 核心代码
    ├── 📁 tjs2/   # tjs2语言内核代码
    ├── 📁 script/ # tjs2 native绑定代码
    ├── 📁 main/   # 引擎运行内核代码 窗体/事件循环/线程等
    ├── 📁 msg/    # 调试信息/提示信息
    ├── 📁 archive/# 数据包格式相关代码
    ├── 📁 media/  # 媒体文件格式相关代码
        ├── 📁 font/      # 字体系统
        ├── 📁 graphics/  # Layer渲染
        ├── 📁 image/     # 图片解码
        ├── 📁 movie/     # 视频解码
        ├── 📁 sound/     # 音频解码
    ├── 📁 utils/  # 工具包
├── 📁 environ/ # 不同系统/芯片架构之间的差异化代码
├── 📁 plugins/ # 扩展插件代码
├── 📄 krkrsdl.cpp           # 桌面端启动文件
├── 📄 krkrsdl_android.cpp   # 移动端启动文件
📁Res/   # 程序资源文件
📁vcpkg/ # 自定义vcpkg依赖
📄.clang-format # 格式化代码风格定义文件
📄build-*** # 各平台的程序构建脚本
📄CMakeLists.txt/CMakePresets.json # CMake配置文件
📄vcpkg.json/vcpkg-configuration.json # vcpkg配置文件
```

# 依赖库说明

- oniguruma:用于tjs2语言内核的正则表达式匹配
- zlib:基础压缩算法
- ffmpeg:音视频解码
- libpng/libwebp/libjpeg-turbo:图片解码
- libvorbis/opusfile:音频解码
- freetype:字体
- sdl3/glad/blend2d:跨平台核心

使用说明：对于api稳定的库默认采用最新版本，对于api有较大改动的库采用能兼容的最高版本。

# 外部环境依赖

- cmake/ninja:跨平台构建工具
- vcpkg:包管理工具
- LLVM-MinGW/Visual Studio 2022:windows构建工具链
- Android SDK/Android NDK:安卓构建工具链

# 补充说明

系统越复杂问题就越难以排除，所以非必要时请保持当前的构建系统
