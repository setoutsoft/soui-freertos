@echo off
rem ===========================================================================
rem soui_fr/build.bat -- Windows 入口：找到 bash 后转调 build.sh
rem
rem 真正的构建逻辑（找 arm-none-eabi-gcc / qemu、configure、并行构建、实跑）
rem 只写在 build.sh 一处，Windows 上 Git for Windows / MSYS2 都自带 bash。
rem
rem 用法与 build.sh 完全一致：
rem   build.bat                配置 + 构建（app + selftest）
rem   build.bat --run          构建完在 QEMU 里跑两个镜像
rem   build.bat --run selftest 只跑平台层自测
rem   build.bat --clean -j8    全量构建，8 并行
rem ===========================================================================
setlocal
chcp 65001 >nul 2>&1
cd /d "%~dp0"

set "BASH_EXE="
for %%B in (
    "%ProgramFiles%\Git\bin\bash.exe"
    "%ProgramFiles%\Git\usr\bin\bash.exe"
    "%ProgramFiles(x86)%\Git\bin\bash.exe"
    "%LOCALAPPDATA%\Programs\Git\bin\bash.exe"
    "C:\msys64\usr\bin\bash.exe"
    "C:\msys64\mingw64\bin\bash.exe"
) do (
    if not defined BASH_EXE if exist "%%~B" set "BASH_EXE=%%~B"
)
if not defined BASH_EXE (
    for /f "delims=" %%B in ('where bash 2^>nul') do (
        if not defined BASH_EXE set "BASH_EXE=%%B"
    )
)

if defined BASH_EXE (
    "%BASH_EXE%" "%~dp0build.sh" %*
    exit /b %ERRORLEVEL%
)

rem ---------------------------------------------------------------------------
rem 没有 bash：退化成纯 cmd 的"配置 + 构建"
rem ---------------------------------------------------------------------------
echo [build.bat] 没有找到 bash（Git for Windows 或 MSYS2 都自带），改用 cmd 直接构建。
echo [build.bat] 想要 --run / --clean 等完整功能请安装 Git for Windows：
echo [build.bat]   https://git-scm.com/download/win
echo.

where cmake >nul 2>&1
if errorlevel 1 (
    echo [build.bat] 缺少 cmake: https://cmake.org/download/
    exit /b 1
)
where ninja >nul 2>&1
if errorlevel 1 (
    echo [build.bat] 缺少 ninja。MSYS2: pacman -S mingw-w64-x86_64-ninja
    exit /b 1
)
where arm-none-eabi-gcc >nul 2>&1
if errorlevel 1 (
    echo [build.bat] 缺少 arm-none-eabi-gcc。
    echo [build.bat]   MSYS2:          pacman -S mingw-w64-x86_64-arm-none-eabi-gcc
    echo [build.bat]   Arm 官方工具链: https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
    echo [build.bat]   装好后把它的 bin 目录加进 PATH 再重试。
    exit /b 1
)

echo [build.bat] configure...
cmake -S . -B build -G Ninja ^
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-freertos.cmake ^
    -DCMAKE_BUILD_TYPE=MinSizeRel
if errorlevel 1 exit /b 1

echo [build.bat] build...
cmake --build build --parallel
if errorlevel 1 exit /b 1

echo.
echo [build.bat] 完成：
echo [build.bat]   %~dp0build\soui_fr.elf
echo [build.bat]   %~dp0build\soui_fr_selftest.elf
echo [build.bat] 实跑需要 qemu-system-arm:
echo [build.bat]   qemu-system-arm -M mps2-an385 -nographic -semihosting -kernel build\soui_fr.elf
endlocal
