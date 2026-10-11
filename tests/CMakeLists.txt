# ModLoader tests. They do not use the game; they check against a folder of test Mods.
add_library(test_fixture_mod SHARED wots4_mod_fixture.cpp wots4_mod_fixture.def)
target_link_libraries(test_fixture_mod PRIVATE wots4::sdk)

add_library(test_fixture_refused SHARED wots4_mod_fixture.cpp wots4_mod_fixture.def)
target_link_libraries(test_fixture_refused PRIVATE wots4::sdk)
target_compile_definitions(test_fixture_refused PRIVATE FIXTURE_ACCEPT=0)

add_library(test_fixture_helper SHARED wots4_dependency_fixture.cpp)

add_executable(wots4_mod_loader_probe wots4_mod_loader_probe.cpp)
target_include_directories(wots4_mod_loader_probe PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(wots4_mod_loader_probe PRIVATE wots4::sdk)

# Check that a normal import loads the dinput8.dll (ModLoader) next to the exe
add_executable(wots4_multi_autoload_probe wots4_multi_autoload_probe.cpp)
target_link_libraries(wots4_multi_autoload_probe PRIVATE dinput8.lib dxguid.lib)

set(tree ${CMAKE_CURRENT_BINARY_DIR}/loader_tree)
add_test(NAME loader_tree_setup COMMAND ${CMAKE_COMMAND}
    -DDEST=${tree}
    -DLOADER=$<TARGET_FILE:wots4_loader>
    -DMOD=$<TARGET_FILE:test_fixture_mod>
    -DREFUSED=$<TARGET_FILE:test_fixture_refused>
    -DHELPER=$<TARGET_FILE:test_fixture_helper>
    -DAUTOLOAD=$<TARGET_FILE:wots4_multi_autoload_probe>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/make_loader_tree.cmake)
set_tests_properties(loader_tree_setup PROPERTIES FIXTURES_SETUP loader_tree)

# The probe compares paths with Windows separators (\), so pass a native path
file(TO_NATIVE_PATH ${tree} tree_native)
add_test(NAME wots4_mod_loader_probe COMMAND wots4_mod_loader_probe ${tree_native})
add_test(NAME wots4_multi_autoload_probe COMMAND ${tree}/wots4_multi_autoload_probe.exe)
set_tests_properties(wots4_mod_loader_probe wots4_multi_autoload_probe PROPERTIES FIXTURES_REQUIRED loader_tree)
