#!/bin/bash
# Build krkrsdl2 using MSYS2 MinGW64
# Run in MSYS2 MinGW64 shell: bash build_sdl2_msys2.sh
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build_sdl2"

# Install dependencies if needed
check_pkg() { pacman -Qi "$1" &>/dev/null; }
DEPS=(
    mingw-w64-x86_64-cmake
    mingw-w64-x86_64-gcc
    mingw-w64-x86_64-SDL2
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
)
MISSING=()
for pkg in "${DEPS[@]}"; do
    check_pkg "$pkg" || MISSING+=("$pkg")
done
if [ ${#MISSING[@]} -gt 0 ]; then
    echo "Installing missing packages: ${MISSING[*]}"
    pacman -S --noconfirm "${MISSING[@]}"
fi

# Configure
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake "$SCRIPT_DIR" \
    -G "MSYS Makefiles" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_COMPILER=gcc \
    -DCMAKE_CXX_COMPILER=g++

# Build
cmake --build . -j$(nproc) 2>&1 | tee build.log

echo "Build complete. Binary: ${BUILD_DIR}/krkrsdl2.exe"
