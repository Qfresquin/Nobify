#@@CASE pipeline_positive_links_and_dirs
project(PipeLinks)
add_link_options(-Wl,--as-needed)
link_libraries(m pthread)
include_directories(SYSTEM include)
link_directories(BEFORE lib)
add_library(lib STATIC lib.c)
add_executable(app main.c)
target_link_options(app PRIVATE -Wl,--gc-sections)
target_link_directories(app PRIVATE lib2)
target_link_libraries(app PRIVATE lib)
#@@ENDCASE

#@@CASE pipeline_positive_testing_install
project(PipeTestInstall)
enable_testing()
add_executable(app main.c)
add_test(NAME smoke COMMAND app WORKING_DIRECTORY . COMMAND_EXPAND_LISTS)
add_test(legacy app)
install(TARGETS app DESTINATION bin)
install(FILES readme.txt DESTINATION share/doc)
install(PROGRAMS run.sh DESTINATION bin)
install(DIRECTORY assets DESTINATION share/assets)
#@@ENDCASE

#@@CASE pipeline_positive_cpack
project(PipePack)
include(CPackComponent)
cpack_add_install_type(Full DISPLAY_NAME "Full Install")
cpack_add_component_group(base DISPLAY_NAME "Base" DESCRIPTION "Base Group")
cpack_add_component(core DISPLAY_NAME "Core" GROUP base INSTALL_TYPES Full REQUIRED)
#@@ENDCASE

#@@CASE pipeline_positive_usage_requirement_item_model
project(PipeUsage LANGUAGES C)
link_libraries(global_dep)
set_property(DIRECTORY APPEND PROPERTY LINK_LIBRARIES dir_dep)
add_library(iface INTERFACE)
set_target_properties(iface PROPERTIES
  INTERFACE_SYSTEM_INCLUDE_DIRECTORIES iface_sys
  INTERFACE_COMPILE_FEATURES c_std_11)
add_executable(app main.c)
target_link_libraries(app PRIVATE iface)
#@@ENDCASE

#@@CASE pipeline_positive_effective_transitive_query_closure
project(PipeTransitive LANGUAGES C)
add_library(imported_iface INTERFACE IMPORTED)
set_target_properties(imported_iface PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES imported/include
  INTERFACE_COMPILE_DEFINITIONS IMPORTED_DEF=1
  INTERFACE_COMPILE_OPTIONS -DIMPORTED_OPT
  INTERFACE_COMPILE_FEATURES c_std_11
  INTERFACE_LINK_OPTIONS -Wl,--export-dynamic
  INTERFACE_LINK_DIRECTORIES imported/libdirs
  INTERFACE_LINK_LIBRARIES dl)
add_library(real_iface INTERFACE)
add_library(real_alias ALIAS real_iface)
target_include_directories(real_iface INTERFACE real/include)
target_compile_definitions(real_iface INTERFACE REAL_DEF=1)
target_compile_options(real_iface INTERFACE -DREAL_OPT)
target_link_options(real_iface INTERFACE -Wl,--no-undefined)
target_link_directories(real_iface INTERFACE real/libdirs)
target_link_libraries(real_iface INTERFACE imported_iface)
add_library(global_iface INTERFACE)
target_include_directories(global_iface INTERFACE global/include)
target_compile_definitions(global_iface INTERFACE GLOBAL_DEF=1)
target_compile_options(global_iface INTERFACE -DGLOBAL_OPT)
target_compile_features(global_iface INTERFACE c_std_99)
target_link_options(global_iface INTERFACE -Wl,--as-needed)
target_link_directories(global_iface INTERFACE global/libdirs)
target_link_libraries(global_iface INTERFACE real_alias)
add_library(dir_iface INTERFACE)
target_include_directories(dir_iface INTERFACE dir/include)
target_compile_definitions(dir_iface INTERFACE DIR_DEF=1)
target_compile_options(dir_iface INTERFACE -DDIR_OPT)
target_compile_features(dir_iface INTERFACE c_std_17)
target_link_options(dir_iface INTERFACE -Wl,-z,defs)
target_link_directories(dir_iface INTERFACE dir/libdirs)
target_link_libraries(dir_iface INTERFACE real_iface)
add_library(linkonly_iface INTERFACE)
target_compile_definitions(linkonly_iface INTERFACE LINKONLY_DEF=1)
target_link_libraries(linkonly_iface INTERFACE m)
set_property(GLOBAL APPEND PROPERTY LINK_LIBRARIES global_iface)
set_property(DIRECTORY APPEND PROPERTY LINK_LIBRARIES dir_iface "$<LINK_ONLY:linkonly_iface>")
add_executable(app main.c)
#@@ENDCASE

#@@CASE pipeline_positive_package_discovery_snapshot
cmake_minimum_required(VERSION 3.28)
project(PipePackages LANGUAGES NONE)
file(MAKE_DIRECTORY cmake)
file(WRITE cmake/FindModulePkg.cmake "set(ModulePkg_FOUND 1)\n")
file(MAKE_DIRECTORY prefix/lib/cmake/ConfigPkg)
file(WRITE prefix/lib/cmake/ConfigPkg/ConfigPkgConfig.cmake "set(ConfigPkg_FOUND 1)\n")
set(CMAKE_MODULE_PATH cmake)
set(CMAKE_PREFIX_PATH prefix)
find_package(ModulePkg QUIET MODULE)
find_package(ConfigPkg REQUIRED CONFIG)
find_package(MissingPkg QUIET CONFIG)
#@@ENDCASE

#@@CASE pipeline_negative_cpack_unknown_group
project(BadPackGroup)
include(CPackComponent)
cpack_add_component(core GROUP missing)
#@@ENDCASE

#@@CASE pipeline_negative_cpack_unknown_dependency
project(BadPackDepends)
include(CPackComponent)
cpack_add_install_type(Full)
cpack_add_component(core DEPENDS missing INSTALL_TYPES Full)
#@@ENDCASE

#@@CASE pipeline_negative_cpack_unknown_install_type
project(BadPackInstallType)
include(CPackComponent)
cpack_add_component(core INSTALL_TYPES MissingType)
#@@ENDCASE

#@@CASE pipeline_negative_install_missing_destination
project(BadInstall)
install(FILES foo.txt)
#@@ENDCASE
