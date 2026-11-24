cmake_minimum_required(VERSION 3.20)
project(ddrmini_smoke_test C)

set(DDRMINI_SDL_INCLUDE_DIR "" CACHE PATH "Directory containing SDL2/SDL.h")
if(NOT DDRMINI_SDL_INCLUDE_DIR)
  message(FATAL_ERROR
    "Set DDRMINI_SDL_INCLUDE_DIR to compatible SDL2 headers (for example /usr/include/SDL2)")
endif()

find_library(DDRMINI_SDL_LIBRARY
  NAMES SDL2-2.0 SDL2
  PATHS "${DDRMINI_SDL_ROOT}/usr/lib/aarch64-linux-gnu"
  NO_DEFAULT_PATH
  REQUIRED)

add_executable(ddrmini-smoke main.c)
target_include_directories(ddrmini-smoke PRIVATE
  "${DDRMINI_SDL_INCLUDE_DIR}"
  "${DDRMINI_SDL_INCLUDE_DIR}/../aarch64-linux-gnu")
target_compile_definitions(ddrmini-smoke PRIVATE SDL_MAIN_HANDLED)
target_compile_options(ddrmini-smoke PRIVATE -Wall -Wextra)

# The compact Docker overlay intentionally omits SDL's optional transitive
# libraries; the DDR Mini runtime supplies the libraries its SDL build needs.
target_link_options(ddrmini-smoke PRIVATE -Wl,--allow-shlib-undefined)
target_link_libraries(ddrmini-smoke PRIVATE "${DDRMINI_SDL_LIBRARY}" dl pthread m)

configure_file(DDRmenu DDRmenu COPYONLY)
configure_file(monitor_audio.sh monitor_audio.sh COPYONLY)

install(TARGETS ddrmini-smoke RUNTIME DESTINATION .)
install(PROGRAMS DDRmenu monitor_audio.sh DESTINATION .)

