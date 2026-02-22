#@@CASE golden_core_flow
project(CoreDemo VERSION 1.0)
set(FOO ON)
if(FOO)
  add_executable(core_app main.c)
  target_compile_definitions(core_app PRIVATE CORE_OK=1)
endif()
#@@ENDCASE

#@@CASE golden_targets_props_events
project(TargetDemo)
add_executable(app main.c util.c)
add_library(lib STATIC lib.c)
target_include_directories(app PRIVATE include)
target_compile_definitions(app PRIVATE APPDEF=1)
target_compile_options(app PRIVATE -Wall)
target_link_options(app PRIVATE -Wl,--as-needed)
target_link_directories(app PRIVATE lib)
target_link_libraries(app PRIVATE lib)
set_target_properties(app PROPERTIES OUTPUT_NAME appx)
#@@ENDCASE

#@@CASE golden_find_package_and_include
project(FindDemo)
set(CMAKE_MODULE_PATH modules)
include_guard(GLOBAL)
find_package(ZLIB QUIET)
add_executable(fp main.c)
#@@ENDCASE

#@@CASE golden_file_ops_and_security
project(FileDemo)
file(WRITE temp_golden_eval.txt hello)
file(READ temp_golden_eval.txt OUT)
add_executable(file_app main.c)
target_compile_definitions(file_app PRIVATE OUT_${OUT})
#@@ENDCASE

#@@CASE golden_cpack_commands
project(CPackDemo VERSION 2.0)
cpack_add_install_type(Full DISPLAY_NAME "Full")
cpack_add_component_group(base DISPLAY_NAME "Base Group" DESCRIPTION "Core Group")
cpack_add_component(core DISPLAY_NAME "Core")
add_executable(pkg_app main.c)
#@@ENDCASE

#@@CASE golden_probes_and_try_compile
cmake_minimum_required(VERSION 3.16)
project(ProbeDemo)
try_compile(HAVE_X ${CMAKE_BINARY_DIR} probe.c)
add_executable(probe_app main.c)
#@@ENDCASE

#@@CASE golden_ctest_meta
project(CTestDemo)
enable_testing()
add_test(NAME smoke COMMAND app)
add_test(legacy app)
add_executable(app main.c)
install(TARGETS app DESTINATION bin)
install(FILES readme.txt DESTINATION share/doc)
install(PROGRAMS run.sh DESTINATION bin)
install(DIRECTORY assets DESTINATION share/assets)
#@@ENDCASE

#@@CASE golden_misc_path_and_property
project(MiscDemo)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
cmake_path(SET P NORMALIZE "a/./b/../c")
add_executable(misc_app main.c)
#@@ENDCASE

#@@CASE expr_variable_expansion_and_if_ops
set(FOO abc)
set(MYLIST "b;a;c")
if(a IN_LIST MYLIST)
  set(IN_LIST_OK 1)
endif()
if("a\\b" PATH_EQUAL "a/b")
  set(PATH_EQ_OK 1)
endif()
#@@ENDCASE

#@@CASE expr_parenthesized_if_condition
set(A ON)
set(B ON)
if((A AND B) OR C)
  set(CONDITION_OK 1)
endif()
#@@ENDCASE

#@@CASE dispatcher_command_handlers
add_definitions(-DLEGACY=1 -fPIC)
add_compile_options(-Wall)
add_link_options(-Wl,--gc-sections)
link_libraries(m pthread)
include_directories(SYSTEM include)
link_directories(BEFORE lib)
add_executable(app main.c)
target_include_directories(app PRIVATE include)
target_compile_definitions(app PRIVATE APPDEF=1)
target_compile_options(app PRIVATE -Wextra)
#@@ENDCASE

#@@CASE dispatcher_cmake_minimum_required
cmake_minimum_required(VERSION 3.16...3.29)
#@@ENDCASE

#@@CASE dispatcher_cmake_policy_set_get_roundtrip
cmake_policy(SET CMP0077 NEW)
cmake_policy(GET CMP0077 OUT_VAR)
#@@ENDCASE

#@@CASE dispatcher_find_package_handler_module_mode
file(MAKE_DIRECTORY temp_pkg/CMake)
file(WRITE temp_pkg/CMake/FindDemoPkg.cmake [=[set(DemoPkg_FOUND 1)
set(DemoPkg_VERSION 9.1)
]=])
set(CMAKE_MODULE_PATH temp_pkg/CMake)
find_package(DemoPkg MODULE REQUIRED)
#@@ENDCASE

#@@CASE dispatcher_find_package_preserves_script_defined_found_state
file(MAKE_DIRECTORY temp_pkg2/CMake)
file(WRITE temp_pkg2/CMake/FindDemoPkg2.cmake [=[set(DemoPkg2_FOUND 0)
]=])
set(CMAKE_MODULE_PATH temp_pkg2/CMake)
find_package(DemoPkg2 MODULE QUIET)
#@@ENDCASE

#@@CASE dispatcher_find_package_config_components_and_version
file(MAKE_DIRECTORY temp_pkg_cfg)
file(WRITE temp_pkg_cfg/DemoCfgConfig.cmake [=[if("${DemoCfg_FIND_COMPONENTS}" STREQUAL "Core;Net")
  set(DemoCfg_FOUND 1)
else()
  set(DemoCfg_FOUND 0)
endif()
set(DemoCfg_VERSION 1.2.0)
]=])
set(CMAKE_PREFIX_PATH temp_pkg_cfg)
find_package(DemoCfg 1.0 CONFIG COMPONENTS Core Net QUIET)
set(CMAKE_PREFIX_PATH temp_pkg_cfg)
find_package(DemoCfg 2.0 EXACT CONFIG QUIET)
#@@ENDCASE

#@@CASE dispatcher_find_package_config_version_file_can_reject
file(MAKE_DIRECTORY temp_pkg_cfgver)
file(WRITE temp_pkg_cfgver/DemoVerConfig.cmake [=[set(DemoVer_FOUND 1)
set(DemoVer_VERSION 9.9.9)
]=])
file(WRITE temp_pkg_cfgver/DemoVerConfigVersion.cmake [=[set(PACKAGE_VERSION 9.9.9)
set(PACKAGE_VERSION_COMPATIBLE FALSE)
set(PACKAGE_VERSION_EXACT FALSE)
]=])
set(CMAKE_PREFIX_PATH temp_pkg_cfgver)
find_package(DemoVer 1.0 CONFIG QUIET)
#@@ENDCASE

#@@CASE dispatcher_set_target_properties_preserves_genex_semicolon_unquoted
add_executable(t main.c)
set_target_properties(t PROPERTIES MY_PROP $<$<CONFIG:Debug>:A;B>)
#@@ENDCASE

#@@CASE dispatcher_set_property_target_ops_emit_expected_event_op
add_executable(t main.c)
set_property(TARGET t APPEND PROPERTY COMPILE_OPTIONS $<$<CONFIG:Debug>:-g>)
set_property(TARGET t APPEND_STRING PROPERTY SUFFIX $<$<CONFIG:Debug>:_d>)
#@@ENDCASE

#@@CASE dispatcher_set_property_non_target_scope_emits_warning
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
#@@ENDCASE

#@@CASE file_security_read_rejects_absolute_outside_project_scope
file(READ /tmp/nobify_forbidden OUT)
#@@ENDCASE

#@@CASE file_security_strings_rejects_absolute_outside_project_scope
file(STRINGS /tmp/nobify_forbidden OUT)
#@@ENDCASE

#@@CASE file_security_read_relative_inside_project_scope_still_works
file(WRITE temp_read_ok.txt "hello\n")
file(READ temp_read_ok.txt OUT)
#@@ENDCASE

#@@CASE file_security_copy_with_permissions_executes_without_legacy_no_effect_warning
file(WRITE temp_copy_perm_src.txt "x")
file(COPY temp_copy_perm_src.txt DESTINATION temp_copy_perm_dst PERMISSIONS OWNER_READ OWNER_WRITE)
#@@ENDCASE
