cmake_minimum_required( VERSION 3.20 )

project( freertos_swinx_demo C CXX )

set( CMAKE_C_STANDARD   11 )
set( CMAKE_CXX_STANDARD 11 )

# ---- external sources -----------------------------------------------------
set( FREERTOS_KERNEL ${CMAKE_CURRENT_SOURCE_DIR}/../FreeRTOS-Kernel )
# swinx source: submodule layout (soui4/soui-freertos/demo) resolves ../..;
# standalone D:/work layout is the fallback.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../../swinx)
    set( SWINX_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/../../swinx )
else()
    set( SWINX_DIR   /d/work/soui4/swinx )
endif()

# ---- application + FreeRTOS kernel sources --------------------------------
add_executable( demo.elf
    startup.c
    syscalls.c
    uart.c
    main.cpp
    ${FREERTOS_KERNEL}/tasks.c
    ${FREERTOS_KERNEL}/queue.c
    ${FREERTOS_KERNEL}/list.c
    ${FREERTOS_KERNEL}/event_groups.c
    ${FREERTOS_KERNEL}/stream_buffer.c
    ${FREERTOS_KERNEL}/portable/GCC/ARM_CM3/port.c
    ${FREERTOS_KERNEL}/portable/MemMang/heap_3.c
)

# Order matters: the swinx override headers for <mutex>/<thread>/<condition_variable>
# live in src/platform/freertos/stl and MUST be found before the compiler's own C++
# standard headers, so list them first (and with BEFORE).
target_include_directories( demo.elf BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}                    # FreeRTOSConfig.h, uart.h
    ${SWINX_DIR}/src/platform/freertos/stl                 # OVERRIDE <mutex>/<thread>/<condition_variable> + swinx_freertos_api.h
)

# NOTE: ${SWINX_DIR}/src is intentionally NOT on the include path. swinx/src
# contains a Wine-derived `list.h` (linked-list helper) that would shadow
# FreeRTOS-Kernel/include/list.h, breaking list.c / tasks.c / queue.c. The demo
# only needs the freertos/stl override headers, which are self-contained.

target_include_directories( demo.elf PRIVATE
    ${FREERTOS_KERNEL}/include
    ${FREERTOS_KERNEL}/portable/GCC/ARM_CM3
)

set_target_properties( demo.elf PROPERTIES
    LINK_FLAGS "-T${CMAKE_CURRENT_SOURCE_DIR}/lm3s6965.ld -nostartfiles -Wl,--entry=Reset_Handler -Wl,--gc-sections"
)

# Build an objdump/disassembly helper target (optional).
add_custom_command( TARGET demo.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:demo.elf> demo.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:demo.elf>
)
