# wots4-mods の共通ビルド設定。
# ModLoader（このリポジトリ）と各 Mod の両方から include する。
# Mod を単体でビルドするときは -DWOTS4_SDK_DIR=<wots4-mods のパス> を渡す。
include_guard(GLOBAL)
include(FetchContent)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "侍道4 は 32bit のゲームです。-A Win32 を付けて configure してください。")
endif()

set(WOTS4_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." CACHE INTERNAL "")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(/utf-8 $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
add_compile_definitions(UNICODE _UNICODE)

# Mod SDK（Mod API のヘッダー）
if(NOT TARGET wots4_sdk)
    add_library(wots4_sdk INTERFACE)
    target_include_directories(wots4_sdk INTERFACE "${WOTS4_ROOT}/include")
    add_library(wots4::sdk ALIAS wots4_sdk)
endif()

# ModLoader（dinput8.dll）を作る。Mod を単体でビルドするときも、ModLoader 経由のテストに使える。
#   wots4_add_loader([INSTALL])  INSTALL を付けると Release の zip に入れる（ModLoader のリポジトリだけ）
function(wots4_add_loader)
    cmake_parse_arguments(L "INSTALL" "" "" ${ARGN})
    if(TARGET wots4_loader)
        return()
    endif()
    add_library(wots4_loader SHARED ${WOTS4_ROOT}/src/dinput8_proxy.cpp ${WOTS4_ROOT}/src/dinput8_proxy.def)
    target_link_libraries(wots4_loader PRIVATE wots4::sdk)
    set_target_properties(wots4_loader PROPERTIES
        OUTPUT_NAME dinput8
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/game
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/game
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/game
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/game)
    if(L_INSTALL)
        install(TARGETS wots4_loader RUNTIME DESTINATION . COMPONENT loader)
        install(FILES ${WOTS4_ROOT}/docs/INSTALL.md DESTINATION . RENAME README.md COMPONENT loader)
    endif()
endfunction()

# 依存はハッシュを固定して取得する。SOURCE_SUBDIR _no_cmake で、依存側の CMakeLists は使わない。
function(wots4_use_minhook)
    if(TARGET wots4_minhook)
        return()
    endif()
    FetchContent_Declare(minhook
        URL https://github.com/TsudaKageyu/minhook/archive/refs/tags/v1.3.4.zip
        URL_HASH SHA256=172708123daa0c98d20d3a980b16a50be14af243dc95dee6f79c24193ad010e4
        SOURCE_SUBDIR _no_cmake)
    FetchContent_MakeAvailable(minhook)
    enable_language(C)
    add_library(wots4_minhook STATIC
        ${minhook_SOURCE_DIR}/src/buffer.c
        ${minhook_SOURCE_DIR}/src/hook.c
        ${minhook_SOURCE_DIR}/src/trampoline.c
        ${minhook_SOURCE_DIR}/src/hde/hde32.c)
    target_include_directories(wots4_minhook PUBLIC ${minhook_SOURCE_DIR}/include)
    set(WOTS4_MINHOOK_LICENSE ${minhook_SOURCE_DIR}/LICENSE.txt CACHE INTERNAL "")
endfunction()

function(wots4_use_libsodium)
    if(TARGET wots4_libsodium)
        return()
    endif()
    FetchContent_Declare(libsodium
        URL https://github.com/jedisct1/libsodium/releases/download/1.0.22-RELEASE/libsodium-1.0.22-msvc.zip
        URL_HASH SHA256=3e03a726fac4bc09cb61d8f29d658ef7a5eca0811de59082130414f7ca2e4279
        SOURCE_SUBDIR _no_cmake)
    FetchContent_MakeAvailable(libsodium)
    add_library(wots4_libsodium STATIC IMPORTED GLOBAL)
    set_target_properties(wots4_libsodium PROPERTIES
        IMPORTED_LOCATION ${libsodium_SOURCE_DIR}/Win32/Release/v143/static/libsodium.lib
        INTERFACE_INCLUDE_DIRECTORIES ${libsodium_SOURCE_DIR}/include
        INTERFACE_COMPILE_DEFINITIONS SODIUM_STATIC)
endfunction()

function(wots4_use_nlohmann_json)
    if(TARGET wots4_nlohmann_json)
        return()
    endif()
    FetchContent_Declare(nlohmann_json_header
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp
        URL_HASH SHA256=aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63
        DOWNLOAD_NO_EXTRACT TRUE
        SOURCE_SUBDIR _no_cmake)
    FetchContent_MakeAvailable(nlohmann_json_header)
    set(include_dir ${CMAKE_BINARY_DIR}/_wots4/nlohmann_json/include)
    configure_file(${nlohmann_json_header_SOURCE_DIR}/json.hpp ${include_dir}/nlohmann/json.hpp COPYONLY)
    add_library(wots4_nlohmann_json INTERFACE)
    target_include_directories(wots4_nlohmann_json SYSTEM INTERFACE ${include_dir})
endfunction()

# Mod を1つ作る。出力は mods/<NAME>/mod.dll。
#   wots4_add_mod(<NAME> SOURCES ... [DEF file.def] [LIBS ...] [FILES ...] [LICENSES ...] [NO_RELEASE])
#   FILES    : mod.dll と同じフォルダへ配布するファイル（config.ini など）
#   LICENSES : docs/licenses/ へ配布するファイル
#   NO_RELEASE : まだ配布しない（Release の zip に入れない）
function(wots4_add_mod name)
    cmake_parse_arguments(MOD "NO_RELEASE" "DEF" "SOURCES;LIBS;FILES;LICENSES" ${ARGN})
    set(target mod_${name})
    add_library(${target} SHARED ${MOD_SOURCES} ${MOD_DEF})
    target_link_libraries(${target} PRIVATE wots4::sdk ${MOD_LIBS})
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME mod
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/game/mods/${name}
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/game/mods/${name}
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/game/mods/${name}
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/game/mods/${name})
    # build/game/mods/<NAME>/ を、ゲームのフォルダに置くときと同じ中身にする（make mod で使う）
    if(MOD_FILES)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MOD_FILES} $<TARGET_FILE_DIR:${target}>
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    endif()
    if(MOD_NO_RELEASE)
        return()
    endif()
    install(TARGETS ${target} RUNTIME DESTINATION mods/${name} COMPONENT ${name})
    if(MOD_FILES)
        install(FILES ${MOD_FILES} DESTINATION mods/${name} COMPONENT ${name})
    endif()
    if(MOD_LICENSES)
        install(FILES ${MOD_LICENSES} DESTINATION mods/${name}/docs/licenses COMPONENT ${name})
    endif()
endfunction()

# テストを1つ足す。ゲームの exe が要るテストには NEEDS_EXE を付ける（CI では除外）。
#   wots4_add_probe(<NAME> SOURCES ... [LIBS ...] [ARGS ...] [NEEDS_EXE] [LINK_OPTIONS ...])
function(wots4_add_probe name)
    cmake_parse_arguments(P "NEEDS_EXE" "" "SOURCES;LIBS;ARGS;LINK_OPTIONS;INCLUDES" ${ARGN})
    add_executable(${name} ${P_SOURCES})
    target_link_libraries(${name} PRIVATE wots4::sdk ${P_LIBS})
    if(P_INCLUDES)
        target_include_directories(${name} PRIVATE ${P_INCLUDES})
    endif()
    if(P_LINK_OPTIONS)
        target_link_options(${name} PRIVATE ${P_LINK_OPTIONS})
    endif()
    add_test(NAME ${name} COMMAND ${name} ${P_ARGS})
    if(P_NEEDS_EXE)
        set_tests_properties(${name} PROPERTIES LABELS needs_exe)
    endif()
endfunction()
