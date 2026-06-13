cmake_minimum_required(VERSION 3.15)
project(LoomPi VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Set path for find_package to look for SDL2 via brew if on macOS
if (APPLE)
  set(CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local" ${CMAKE_PREFIX_PATH})
endif()

find_package(SDL2 REQUIRED)

# We need lv_conf.h in the include path before LVGL builds
add_compile_definitions(LV_CONF_INCLUDE_SIMPLE)

# Disable ASM in LVGL CMake config to prevent macOS ARM64 compilation failures
set(LV_USE_DRAW_SW_ASM OFF CACHE BOOL "Disable SW ASM")
set(LV_USE_NATIVE_HELIUM_ASM OFF CACHE BOOL "Disable Helium")

include(FetchContent)
FetchContent_Declare(
  lvgl
  GIT_REPOSITORY https://github.com/lvgl/lvgl.git
  GIT_TAG        v9.1.0
)
FetchContent_MakeAvailable(lvgl)

# Filter out assembly (.S) files from target lvgl to prevent compilation failures on ARM (Raspberry Pi)
get_target_property(LVGL_SOURCES lvgl SOURCES)
set(LVGL_CLEAN_SOURCES "")
foreach(source ${LVGL_SOURCES})
  if(NOT source MATCHES "\\.S$")
    list(APPEND LVGL_CLEAN_SOURCES ${source})
  endif()
endforeach()
set_target_properties(lvgl PROPERTIES SOURCES "${LVGL_CLEAN_SOURCES}")

target_include_directories(lvgl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lvgl PUBLIC SDL2::SDL2)

file(GLOB_RECURSE SRC_FILES "src/*.cpp")

add_executable(LoomPi ${SRC_FILES})

target_include_directories(LoomPi PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(LoomPi PRIVATE src)
target_include_directories(LoomPi PRIVATE src/engines)
target_include_directories(LoomPi PRIVATE src/libs)

target_link_libraries(LoomPi PRIVATE SDL2::SDL2 lvgl)

if (APPLE)
  target_link_libraries(LoomPi PRIVATE "-framework CoreMIDI" "-framework CoreFoundation")
endif()

if (UNIX AND NOT APPLE)
  find_package(ALSA REQUIRED)
  target_link_libraries(LoomPi PRIVATE ALSA::ALSA)
endif()

