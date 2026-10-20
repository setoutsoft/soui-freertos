#!/bin/bash
set -e
cd /d/work/freertos_env/demo
echo "MSYSTEM=$MSYSTEM"
which arm-none-eabi-gcc cmake ninja
rm -rf build
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-freertos.cmake > configure.log 2>&1
echo "CONFIGURE exit=$?"
cmake --build build > build.log 2>&1
echo "BUILD exit=$?"
ls -la build/demo.elf 2>/dev/null || echo "NO demo.elf"
