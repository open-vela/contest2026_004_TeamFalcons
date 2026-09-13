# Standalone headless build for the VelaGuard HMI checks
# (task 09-13-hmi-performance-baseline).
#
# No SDL, no board: builds the real gui/main/ui into off-screen binaries
# and runs them under CTest. The product simulator configuration
# (gui/lv_conf.h, gui/CMakeLists.txt) is read but never modified.
#
# Parameters:
#   VG_LVGL_DIR             LVGL checkout to build against. Default: the
#                           firmware's actual checkout (no download, no
#                           upgrade). The PC checkout gui/lvgl is also valid.
#   VG_HEADLESS_COLOR_DEPTH 16 (RGB565, board-like) or 32 (XRGB8888, PC).
#   VG_HEADLESS_PROFILE     pc (256 points / 300 history) or
#                           board (64 points / 128 history, via the
#                           test-only VG_MODEL_BOARD_CAPACITY switch).
#
# Typical use from the contest repo root:
#   cmake -S gui/headless -B .debug/hmi-headless \
#     -DVG_LVGL_DIR="$PWD/../apps/graphics/lvgl/lvgl" -DVG_HEADLESS_COLOR_DEPTH=16
#   cmake --build .debug/hmi-headless
#   ctest --test-dir .debug/hmi-headless --output-on-failure

cmake_minimum_required(VERSION 3.13)
project(vghmi_headless C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(VG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")      # contest repo root
set(VG_GUI_ROOT "${VG_ROOT}/gui")

set(VG_LVGL_DIR "" CACHE PATH "LVGL checkout (default: firmware checkout)")
if(NOT VG_LVGL_DIR)
  get_filename_component(OPENVELA_ROOT "${VG_ROOT}/.." ABSOLUTE)
  set(VG_LVGL_DIR "${OPENVELA_ROOT}/apps/graphics/lvgl/lvgl")
endif()
if(NOT EXISTS "${VG_LVGL_DIR}/lvgl.h")
  message(FATAL_ERROR "VG_LVGL_DIR does not contain lvgl.h: ${VG_LVGL_DIR}")
endif()

set(VG_HEADLESS_COLOR_DEPTH "16" CACHE STRING "Display color depth: 16 or 32")
if(NOT VG_HEADLESS_COLOR_DEPTH MATCHES "^(16|32)$")
  message(FATAL_ERROR "VG_HEADLESS_COLOR_DEPTH must be 16 or 32, got '${VG_HEADLESS_COLOR_DEPTH}'")
endif()

set(VG_HEADLESS_PROFILE "pc" CACHE STRING "Capacity profile: pc or board")
if(NOT VG_HEADLESS_PROFILE MATCHES "^(pc|board)$")
  message(FATAL_ERROR "VG_HEADLESS_PROFILE must be 'pc' or 'board', got '${VG_HEADLESS_PROFILE}'")
endif()

# ---------------------------------------------------------------------------
# lv_conf.h: derived from the product simulator conf with only the color
# depth rewritten (and SDL output disabled). The original file is untouched;
# a failed rewrite aborts instead of silently building the wrong format.
# ---------------------------------------------------------------------------
file(READ "${VG_GUI_ROOT}/lv_conf.h" VG_CONF)
string(REPLACE "#define LV_COLOR_DEPTH 32"
               "#define LV_COLOR_DEPTH ${VG_HEADLESS_COLOR_DEPTH}"
               VG_CONF "${VG_CONF}")
if(VG_HEADLESS_COLOR_DEPTH STREQUAL "16")
  string(FIND "${VG_CONF}" "#define LV_COLOR_DEPTH 16" VG_DEPTH_IDX)
  if(VG_DEPTH_IDX EQUAL -1)
    message(FATAL_ERROR "failed to rewrite LV_COLOR_DEPTH in ${VG_GUI_ROOT}/lv_conf.h")
  endif()
endif()
string(REPLACE "#define LV_USE_SDL              1"
               "#define LV_USE_SDL              0"
               VG_CONF "${VG_CONF}")
string(FIND "${VG_CONF}" "#define LV_USE_SDL              0" VG_SDL_IDX)
if(VG_SDL_IDX EQUAL -1)
  message(FATAL_ERROR "failed to disable LV_USE_SDL in the derived lv_conf.h")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/lv_conf.h" "${VG_CONF}")

# ---------------------------------------------------------------------------
# LVGL: the chosen checkout's own CMake (no downloads). LV_CONF_PATH points
# at the derived conf and propagates PUBLIC from the lvgl target.
# ---------------------------------------------------------------------------
set(LV_CONF_PATH "${CMAKE_CURRENT_BINARY_DIR}/lv_conf.h")
add_subdirectory("${VG_LVGL_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/lvgl_build" EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# UI sources: same set as the SDL simulator build (main/ui glob), minus
# vg_app (SDL keyboard polling); the PC backend stub model/vg_ui_backend.c
# stays in (the board backend swap is a firmware Makefile concern).
# The headless harness performs its own init in the same order.
# ---------------------------------------------------------------------------
set(VG_UI_DIR "${VG_ROOT}/gui/main/ui")
set(VG_UI_SUBDIRS app shell pages model theme widgets fonts)
set(VG_UI_SRCS "")
foreach(_d ${VG_UI_SUBDIRS})
  file(GLOB _c "${VG_UI_DIR}/${_d}/*.c")
  list(APPEND VG_UI_SRCS ${_c})
endforeach()
list(REMOVE_ITEM VG_UI_SRCS "${VG_UI_DIR}/app/vg_app.c")

add_library(vg_ui STATIC ${VG_UI_SRCS})
target_include_directories(vg_ui PUBLIC
  "${VG_LVGL_DIR}/.."          # sources include "lvgl/lvgl.h"
  "${VG_UI_DIR}"
  "${VG_ROOT}/gui/main/inc"
  "${VG_ROOT}/app/velaguard"   # vg_hmi_perf.h (no-op without the perf flag)
)
if(VG_HEADLESS_PROFILE STREQUAL "board")
  target_compile_definitions(vg_ui PUBLIC VG_MODEL_BOARD_CAPACITY=1)
endif()

# ---------------------------------------------------------------------------
# Checks
# ---------------------------------------------------------------------------
foreach(check alarm_check trend_check fixture_check lifecycle_render_check interaction_check)
  add_executable(${check} ${check}_main.c harness_common.c)
  target_link_libraries(${check} PRIVATE vg_ui lvgl lvgl::thorvg m)
  target_compile_options(${check} PRIVATE -Wall -Wextra)
  target_compile_definitions(${check} PRIVATE
    VG_HEADLESS_PROFILE_${VG_HEADLESS_PROFILE}=1
    VG_HEADLESS_DEPTH_${VG_HEADLESS_COLOR_DEPTH}=1
  )
endforeach()

enable_testing()
foreach(check alarm_check trend_check fixture_check lifecycle_render_check interaction_check)
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/frames/${check}")
  add_test(NAME ${check}
           COMMAND ${check} "${CMAKE_CURRENT_BINARY_DIR}/frames/${check}")
endforeach()

message(STATUS "headless: LVGL=${VG_LVGL_DIR} depth=${VG_HEADLESS_COLOR_DEPTH} profile=${VG_HEADLESS_PROFILE}")
