cmake_minimum_required(VERSION 3.22)

#
# LVGL v9.x Middleware Library
# Integrated into STM32CubeMX project for STM32H7xx
#
# lv_conf.h should be placed next to lvgl.h (in this directory)
# and controls which modules/features are compiled via #define guards.
#

# Recursively collect all LVGL source files from src/
file(GLOB_RECURSE LVGL_Src
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

# LVGL include directories
# - Root directory exposes lv_conf.h and lvgl.h
# - src/ exposes internal headers
set(LVGL_Include_Dirs
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# LVGL compile definitions
# LV_CONF_INCLUDE_SIMPLE: tells LVGL to search lv_conf.h via include path
# rather than a relative path. Requires the directory containing lv_conf.h
# to be listed in LVGL_Include_Dirs.
set(LVGL_Defines
    LV_CONF_INCLUDE_SIMPLE
)

# Create LVGL OBJECT library (consistent with STM32CubeMX middleware style)
add_library(LVGL OBJECT)
target_sources(LVGL PRIVATE
    ${LVGL_Src}
)
target_include_directories(LVGL
    PUBLIC ${LVGL_Include_Dirs}
)
target_compile_definitions(LVGL
    PUBLIC ${LVGL_Defines}
)

# Inherit STM32CubeMX common includes and compile definitions
# (HAL headers, CMSIS, FreeRTOS, chip-specific macros, etc.)
target_link_libraries(LVGL PUBLIC stm32cubemx)
