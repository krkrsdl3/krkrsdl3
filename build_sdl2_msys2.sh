#!/bin/bash
# Build krkrsdl2 using MSYS2 MinGW64
# Run in MSYS2 MinGW64 shell: bash build_sdl2_msys2.sh
# Or from PowerShell: $env:MSYSTEM="MINGW64"; C:\msys64\usr\bin\bash.exe -l -c "bash /d/MyApplication/krkr2/build_sdl2_msys2.sh"
#
# Usage:
#   bash build_sdl2_msys2.sh              # Release build
#   BUILD_TYPE=Debug bash build_sdl2_msys2.sh
set -e

# Ensure MinGW64 tools are in PATH
export PATH="/mingw64/bin:${PATH}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/out/windows-mingw"
BUILD_TYPE="${BUILD_TYPE:-Release}"

echo "=== krkrsdl2 MSYS2 MinGW64 Build ==="
echo "Build type : ${BUILD_TYPE}"
echo "Output dir : ${BUILD_DIR}"
echo ""

# -------------------------------------------------------
# Install missing MSYS2 packages
# -------------------------------------------------------
check_pkg() { pacman -Qi "$1" &>/dev/null; }
DEPS=(
    mingw-w64-x86_64-cmake
    mingw-w64-x86_64-ninja
    mingw-w64-x86_64-gcc
    mingw-w64-x86_64-pkg-config
    mingw-w64-x86_64-SDL2
    mingw-w64-x86_64-SDL2_ttf
    mingw-w64-x86_64-glew
    mingw-w64-x86_64-freetype
    mingw-w64-x86_64-ffmpeg
    mingw-w64-x86_64-libpng
    mingw-w64-x86_64-glm
    mingw-w64-x86_64-libjpeg-turbo
    mingw-w64-x86_64-libvorbis
    mingw-w64-x86_64-libwebp
    mingw-w64-x86_64-oniguruma
    mingw-w64-x86_64-opus
    mingw-w64-x86_64-opusfile
    mingw-w64-x86_64-zlib
)
MISSING=()
for pkg in "${DEPS[@]}"; do
    check_pkg "$pkg" || MISSING+=("$pkg")
done
if [ ${#MISSING[@]} -gt 0 ]; then
    echo "Installing missing packages: ${MISSING[*]}"
    pacman -S --noconfirm "${MISSING[@]}"
fi

# -------------------------------------------------------
# Configure
# -------------------------------------------------------
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake "$SCRIPT_DIR" \
    -G "Ninja" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_C_COMPILER=gcc \
    -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# -------------------------------------------------------
# Build
# -------------------------------------------------------
cmake --build . -j$(nproc) 2>&1 | tee build.log

echo ""
echo "=== Build complete ==="
echo "Binary: ${BUILD_DIR}/krkrsdl2.exe"
