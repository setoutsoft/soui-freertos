#!/usr/bin/env bash
# Rebuild and flash the ESP32-S3 SOUI demo.
# Run from any directory: ./rebuild.sh   (Git Bash / MSYS2 bash)
set -e

cd "$(dirname "$0")"

echo "start build"
./tools/idf_env.sh ninja -C build -j8

echo "start burn bin"
(
    cd build
    ../tools/idf_env.sh python -m esptool --chip esp32s3 -p COM4 -b 921600 write_flash "@flash_args"
)

echo "done"
