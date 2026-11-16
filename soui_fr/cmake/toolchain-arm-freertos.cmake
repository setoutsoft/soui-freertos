# toolchain-arm-freertos.cmake
#
# Bare-metal ARM Cortex-M3 cross-compilation toolchain for the FreeRTOS targets.
# Used by soui_fr (both soui_fr.elf and soui_fr_selftest.elf):
#
#   cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-freertos.cmake
#
# build.bat / build.sh in the same project pass this file automatically, and add
# the arm-none-eabi bin directory to PATH when the compiler is installed but not
# on PATH.

set( CMAKE_SYSTEM_NAME        Generic )
set( CMAKE_SYSTEM_PROCESSOR   arm )

set( TOOLCHAIN_PREFIX arm-none-eabi )
set( CMAKE_C_COMPILER     ${TOOLCHAIN_PREFIX}-gcc )
set( CMAKE_CXX_COMPILER   ${TOOLCHAIN_PREFIX}-g++ )
set( CMAKE_OBJCOPY        ${TOOLCHAIN_PREFIX}-objcopy )
set( CMAKE_SIZE           ${TOOLCHAIN_PREFIX}-size )

# Try-compiles must not require a full link (no startup/linker script yet).
set( CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY )

set( CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER )
set( CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY )
set( CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY )

# Bare-metal CPU flags.
set( CPU_FLAGS "-mcpu=cortex-m3 -mthumb -Wall -Wextra -ffunction-sections -fdata-sections" )
set( CMAKE_C_FLAGS_INIT   "${CPU_FLAGS}" )
set( CMAKE_CXX_FLAGS_INIT "${CPU_FLAGS} -fno-rtti" )

# This is the macro freeRTOS.cmake expects the real target toolchain to define:
# it selects the real FreeRTOS-backed implementation of the swinx STL shims.
add_definitions( -DSOUI_FREERTOS_REAL )
