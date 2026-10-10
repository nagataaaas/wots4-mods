cmake_minimum_required(VERSION 3.25)
project(wots4_mods VERSION 0.1.0 LANGUAGES C CXX)

include(cmake/Wots4.cmake)
option(WOTS4_BUILD_TESTS "テストをビルドする" ON)
# Release では ModLoader だけを作る（各 Mod は自分のリポジトリでリリースする）
option(WOTS4_BUILD_MODS "mods/ に置いた Mod も一緒にビルドする（mods/ は Git で追跡しない）" ON)
if(WOTS4_BUILD_TESTS)
    enable_testing()
endif()

# ModLoader: ゲームの exe と同じフォルダに置く dinput8.dll
wots4_add_loader(INSTALL)

if(WOTS4_BUILD_TESTS)
    add_subdirectory(tests)
    add_subdirectory(templates/mod)  # Mod の雛形がビルドできることも確かめる
endif()

# mods/<name>/CMakeLists.txt がある Mod をすべてビルドする
if(WOTS4_BUILD_MODS)
    file(GLOB mod_dirs LIST_DIRECTORIES true ${CMAKE_CURRENT_SOURCE_DIR}/mods/*)
    foreach(dir IN LISTS mod_dirs)
        if(EXISTS ${dir}/CMakeLists.txt)
            add_subdirectory(${dir})
        endif()
    endforeach()
endif()
