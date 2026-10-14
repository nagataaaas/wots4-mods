cmake_minimum_required(VERSION 3.25)
project(wots4_mods VERSION 0.1.0 LANGUAGES C CXX)

include(cmake/Wots4.cmake)
option(WOTS4_BUILD_TESTS "Build the tests" ON)
# Releases build only the ModLoader (each Mod is released from its own repository)
option(WOTS4_BUILD_MODS "Also build the Mods placed in mods/ (mods/ is not tracked by Git)" ON)
if(WOTS4_BUILD_TESTS)
    enable_testing()
endif()

# ModLoader: dinput8.dll, placed in the same folder as the game exe
wots4_add_loader(INSTALL)

# WayOfTheSamurai4ModInstaller.exe, released next to the ModLoader zip
add_subdirectory(installer)

if(WOTS4_BUILD_TESTS)
    add_subdirectory(tests)
    add_subdirectory(templates/mod)  # Also check that the Mod template builds
endif()

# Build every Mod that has mods/<name>/CMakeLists.txt
if(WOTS4_BUILD_MODS)
    file(GLOB mod_dirs LIST_DIRECTORIES true ${CMAKE_CURRENT_SOURCE_DIR}/mods/*)
    foreach(dir IN LISTS mod_dirs)
        if(EXISTS ${dir}/CMakeLists.txt)
            add_subdirectory(${dir})
        endif()
    endforeach()
endif()
