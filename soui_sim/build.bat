@echo off
rem ===========================================================================
rem soui_sim/build.bat -- Windows 入口：找到 bash 后转调 build.sh
rem
rem 为什么要转调：真正的构建逻辑（找工具、找 SDL2、并行构建、--run）只写在
rem build.sh 一处，避免两份实现各自漂移。Windows 上 Git for Windows / MSYS2
rem 都自带 bash，而本工程的子模块与脚本本来就要求它们。
rem
rem 用法与 build.sh 完全一致：
rem   build.bat                配置 + 构建
rem   build.bat --run          构建完直接运行
rem   build.bat --clean -j8    全量构建，8 并行
rem   build.bat -- --DSDL2_DIR=D:/SDL2/cmake
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
rem 没有 bash：退化到纯 cmd 的"配置 + 构建"（不支持 --run / --clean 之外的选项）
rem ---------------------------------------------------------------------------
echo [build.bat] 没有找到 bash（Git for Windows 或 MSYS2 都自带），改用 cmd 直接构建。
echo [build.bat] 想要 build.sh 的完整功能请安装 Git for Windows：
echo [build.bat]   https://git-scm.com/download/win
echo.

where cmake >nul 2>&1
if errorlevel 1 (
    echo [build.bat] 缺少 cmake。请安装 CMake 并确保它在 PATH 上：
    echo [build.bat]   https://cmake.org/download/
    exit /b 1
)
where ninja >nul 2>&1
if errorlevel 1 (
    echo [build.bat] 缺少 ninja。MSYS2 下: pacman -S mingw-w64-x86_64-ninja
    exit /b 1
)

echo [build.bat] configure...
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo.
    echo [build.bat] configure 失败。最常见的原因是找不到 SDL2：
    echo [build.bat]   MSYS2: pacman -S mingw-w64-x86_64-SDL2
    echo [build.bat]   或指定：build.bat -- --DSDL2_DIR=^<SDL2 的 cmake 目录^>
    exit /b 1
)

echo [build.bat] build...
cmake --build build --parallel
if errorlevel 1 exit /b 1

echo.
echo [build.bat] 完成：%~dp0build\soui_sim.exe
endlocal
