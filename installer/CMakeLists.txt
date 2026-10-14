# WayOfTheSamurai4ModInstaller.exe: installs or updates the ModLoader and the published Mods from GitHub Releases.
# Released as its own file next to the ModLoader zip.

# miniz extracts the release zips. Fetched with a pinned hash like the other dependencies.
FetchContent_Declare(miniz
    URL https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
    URL_HASH SHA256=ada38db0b703a56d3dd6d57bf84a9c5d664921d870d8fea4db153979fb5332c5
    SOURCE_SUBDIR _no_cmake)
FetchContent_MakeAvailable(miniz)
add_library(wots4_miniz STATIC ${miniz_SOURCE_DIR}/miniz.c)
target_include_directories(wots4_miniz PUBLIC ${miniz_SOURCE_DIR})

wots4_generate_version_header()
# The license text goes into the exe (installer.rc), which is the only file distributed
configure_file(${miniz_SOURCE_DIR}/LICENSE ${WOTS4_VERSION_INCLUDE_DIR}/miniz_license.txt COPYONLY)

# Everything but main.cpp, so that the tests can use it
add_library(wots4_installer_core STATIC
    src/config_merge.cpp
    src/console_ui.cpp
    src/file_io.cpp
    src/game_install.cpp
    src/game_location.cpp
    src/http_client.cpp
    src/messages.cpp
    src/release_sources.cpp
    src/text_utils.cpp
    src/zip_archive.cpp
    src/zip_entries.cpp)
target_include_directories(wots4_installer_core PUBLIC src ${WOTS4_VERSION_INCLUDE_DIR})
target_link_libraries(wots4_installer_core PUBLIC wots4_miniz winhttp version)

add_executable(wots4_installer src/main.cpp src/installer.rc)
target_link_libraries(wots4_installer PRIVATE wots4_installer_core comdlg32 ole32)
target_include_directories(wots4_installer PRIVATE ${WOTS4_VERSION_INCLUDE_DIR})
set_target_properties(wots4_installer PROPERTIES OUTPUT_NAME WayOfTheSamurai4ModInstaller)
# Writing under Program Files needs administrator rights; asking once at start avoids failing part way
target_link_options(wots4_installer PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
_wots4_set_runtime_output_directory(wots4_installer ${CMAKE_BINARY_DIR}/installer)

if(WOTS4_BUILD_TESTS)
    add_subdirectory(tests)
endif()
