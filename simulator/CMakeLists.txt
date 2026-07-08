cmake_minimum_required(VERSION 3.16)
project(vesc_ui_sim C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

include(FetchContent)

FetchContent_Declare(lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG        v8.3.9)
FetchContent_Declare(lv_drivers
    GIT_REPOSITORY https://github.com/lvgl/lv_drivers.git
    GIT_TAG        release/v8.3)

# Make LVGL & lv_drivers find lv_conf.h (../configs), lv_drv_conf.h (./),
# and SDL2 headers (Homebrew installs SDL.h under /opt/homebrew/include/SDL2/).
add_compile_definitions(LV_CONF_INCLUDE_SIMPLE LV_LVGL_H_INCLUDE_SIMPLE)
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../configs
    ${CMAKE_CURRENT_SOURCE_DIR}/../main
    ${SDL2_INCLUDE_DIRS}
)
link_directories(${SDL2_LIBRARY_DIRS})

FetchContent_MakeAvailable(lvgl lv_drivers)

# lv_drivers' headers (sdl/sdl.h) live at the source root.
include_directories(${lv_drivers_SOURCE_DIR})

add_executable(vesc_ui_sim
    main.c
    arduino_shim.cpp
    ../main/ui.cpp
)

target_link_libraries(vesc_ui_sim PRIVATE lvgl lv_drivers ${SDL2_LIBRARIES})
