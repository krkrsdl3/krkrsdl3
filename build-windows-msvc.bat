@echo off

echo [1/3] Configuring CMake...
cmake --preset="Windows Config MSVC"
echo[

echo [2/3] Building Debug version...
cmake --build --preset="Windows Debug Build MSVC"
echo[

echo [3/3] Building Release version...
cmake --build --preset="Windows Release Build MSVC"
echo[
