# Shared build settings for wots4-mods.
# Included by both the ModLoader (this repository) and each Mod.
# When building a Mod standalone, pass -DWOTS4_SDK_DIR=<path to wots4-mods>.
include_guard(GLOBAL)
include(FetchContent)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Way of the Samurai 4 is a 32-bit game. Configure with -A Win32.")
endif()

set(WOTS4_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." CACHE INTERNAL "")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(/utf-8 $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
add_compile_definitions(UNICODE _UNICODE)

# Mod SDK (Mod API headers)
if(NOT TARGET wots4_sdk)
    add_library(wots4_sdk INTERFACE)
    target_include_directories(wots4_sdk INTERFACE "${WOTS4_ROOT}/include")
    add_library(wots4::sdk ALIAS wots4_sdk)
endif()

# Put the DLL in <directory> itself for every configuration (Release, Debug, etc.).
# Otherwise the Visual Studio generator adds a folder named after the configuration, such as <directory>/Release/.
function(_wots4_set_runtime_output_directory target directory)
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${directory})
    foreach(config IN LISTS CMAKE_CONFIGURATION_TYPES)
        string(TOUPPER ${config} config)
        set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} ${directory})
    endforeach()
endfunction()

# Release tag written into the VERSIONINFO of the ModLoader and the installer. The Release workflow passes the tag.
set(WOTS4_VERSION "dev" CACHE STRING "Release tag (vYYYY.MM.DD or vYYYY.MM.DD.N), or dev for local builds")

# Writes wots4_version.h (the version and the ModLoader's product name) and sets WOTS4_VERSION_INCLUDE_DIR.
# The installer reads ProductName to tell this ModLoader from other dinput8.dll files.
function(wots4_generate_version_header)
    set(directory ${CMAKE_BINARY_DIR}/wots4_generated)
    set(WOTS4_VERSION_INCLUDE_DIR ${directory} CACHE INTERNAL "")
    # FILEVERSION needs four numbers: v2026.10.14 -> 2026,10,14,1 and v2026.10.14.2 -> 2026,10,14,2
    set(WOTS4_VERSION_NUMBERS 0,0,0,0)
    if(WOTS4_VERSION MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)(\\.([0-9]+))?$")
        set(release_of_day 1)
        if(CMAKE_MATCH_5)
            set(release_of_day ${CMAKE_MATCH_5})
        endif()
        set(WOTS4_VERSION_NUMBERS ${CMAKE_MATCH_1},${CMAKE_MATCH_2},${CMAKE_MATCH_3},${release_of_day})
    elseif(NOT WOTS4_VERSION STREQUAL "dev")
        message(FATAL_ERROR "WOTS4_VERSION must be vYYYY.MM.DD, vYYYY.MM.DD.N or dev (got ${WOTS4_VERSION})")
    endif()
    configure_file(${WOTS4_ROOT}/cmake/wots4_version.h.in ${directory}/wots4_version.h @ONLY)
endfunction()

# Build the ModLoader (dinput8.dll). Also usable for tests through the ModLoader when building a Mod standalone.
#   wots4_add_loader([INSTALL])  INSTALL puts it in the Release zip (ModLoader repository only)
function(wots4_add_loader)
    cmake_parse_arguments(ARG "INSTALL" "" "" ${ARGN})
    if(TARGET wots4_loader)
        return()
    endif()
    wots4_use_minhook()
    wots4_generate_version_header()
    add_library(wots4_loader SHARED ${WOTS4_ROOT}/src/dinput8_proxy.cpp ${WOTS4_ROOT}/src/dinput8_proxy.def
        ${WOTS4_ROOT}/src/wots4_game_hooks.cpp ${WOTS4_ROOT}/src/wots4_game_conversation.cpp
        ${WOTS4_ROOT}/src/dinput8_proxy.rc)
    target_include_directories(wots4_loader PRIVATE ${WOTS4_VERSION_INCLUDE_DIR})
    target_link_libraries(wots4_loader PRIVATE wots4::sdk wots4_minhook)
    set_target_properties(wots4_loader PROPERTIES OUTPUT_NAME dinput8)
    _wots4_set_runtime_output_directory(wots4_loader ${CMAKE_BINARY_DIR}/game)
    if(ARG_INSTALL)
        install(TARGETS wots4_loader RUNTIME DESTINATION . COMPONENT loader)
        install(FILES ${WOTS4_ROOT}/docs/INSTALL.md DESTINATION . RENAME README.md COMPONENT loader)
        install(FILES ${WOTS4_MINHOOK_LICENSE} DESTINATION docs/licenses RENAME minhook.txt COMPONENT loader)
    endif()
endfunction()

# Dependencies are fetched with pinned hashes. SOURCE_SUBDIR _no_cmake keeps the dependencies' own CMakeLists from being used.
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

# Build one Mod. The output is mods/<NAME>/mod.dll.
#   wots4_add_mod(<NAME> SOURCES ... [DEF file.def] [LIBS ...] [FILES ...] [LICENSES ...] [NO_RELEASE])
#   FILES    : files distributed in the same folder as mod.dll (config.ini, etc.)
#   LICENSES : files distributed in docs/licenses/
#   NO_RELEASE : not distributed yet (not put in the Release zip)
function(wots4_add_mod name)
    cmake_parse_arguments(ARG "NO_RELEASE" "DEF" "SOURCES;LIBS;FILES;LICENSES" ${ARGN})
    set(target mod_${name})
    add_library(${target} SHARED ${ARG_SOURCES} ${ARG_DEF})
    target_link_libraries(${target} PRIVATE wots4::sdk ${ARG_LIBS})
    set_target_properties(${target} PROPERTIES OUTPUT_NAME mod)
    _wots4_set_runtime_output_directory(${target} ${CMAKE_BINARY_DIR}/game/mods/${name})
    # Make build/game/mods/<NAME>/ hold the same contents as in the game folder (used by make install)
    if(ARG_FILES)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ARG_FILES} $<TARGET_FILE_DIR:${target}>
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    endif()
    if(ARG_NO_RELEASE)
        return()
    endif()
    install(TARGETS ${target} RUNTIME DESTINATION mods/${name} COMPONENT ${name})
    if(ARG_FILES)
        install(FILES ${ARG_FILES} DESTINATION mods/${name} COMPONENT ${name})
    endif()
    if(ARG_LICENSES)
        install(FILES ${ARG_LICENSES} DESTINATION mods/${name}/docs/licenses COMPONENT ${name})
    endif()
endfunction()

# Add one test. Mark tests that need the game exe with NEEDS_EXE (excluded in CI).
#   wots4_add_probe(<NAME> SOURCES ... [LIBS ...] [INCLUDES ...] [LINK_OPTIONS ...] [ARGS ...] [NEEDS_EXE])
#   INCLUDES : include directories
#   ARGS     : arguments passed when running the test
function(wots4_add_probe name)
    cmake_parse_arguments(ARG "NEEDS_EXE" "" "SOURCES;LIBS;ARGS;LINK_OPTIONS;INCLUDES" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE wots4::sdk ${ARG_LIBS})
    if(ARG_INCLUDES)
        target_include_directories(${name} PRIVATE ${ARG_INCLUDES})
    endif()
    if(ARG_LINK_OPTIONS)
        target_link_options(${name} PRIVATE ${ARG_LINK_OPTIONS})
    endif()
    add_test(NAME ${name} COMMAND ${name} ${ARG_ARGS})
    if(ARG_NEEDS_EXE)
        set_tests_properties(${name} PROPERTIES LABELS needs_exe)
    endif()
endfunction()
