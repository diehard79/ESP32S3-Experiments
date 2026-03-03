cmake_minimum_required(VERSION 3.16)

# Tell CMake where our local components live
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/components/lcd"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/lvgl_setup"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(exp1_display_text)
