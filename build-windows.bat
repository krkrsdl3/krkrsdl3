cmake --preset="Windows Config" -DDISABLE_TEST=ON
cmake --build --preset="Windows Debug Build"
cmake --build --preset="Windows Release Build"