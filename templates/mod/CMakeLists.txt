cmake_minimum_required(VERSION 3.25)
project(wots4_mod_example LANGUAGES C CXX)

# When building standalone, pass -DWOTS4_SDK_DIR=<path to wots4-mods>
if(NOT COMMAND wots4_add_mod)
    set(WOTS4_SDK_DIR "" CACHE PATH "Path to wots4-mods (ModLoader)")
    if(NOT EXISTS "${WOTS4_SDK_DIR}/cmake/Wots4.cmake")
        message(FATAL_ERROR "Set WOTS4_SDK_DIR to the path of wots4-mods")
    endif()
    include(${WOTS4_SDK_DIR}/cmake/Wots4.cmake)
    enable_testing()
    set(WOTS4_BUILD_TESTS ON)
endif()

# Use the Mod's folder name in the game folder (mods/<name>/) as <name>
wots4_add_mod(example SOURCES src/mod.cpp DEF src/mod.def)
