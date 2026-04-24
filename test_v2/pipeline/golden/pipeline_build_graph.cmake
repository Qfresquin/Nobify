#@@CASE build_graph_generated_source_and_hooks
project(Test C)
add_custom_command(
  OUTPUT generated.c
  COMMAND echo gen
  DEPENDS schema.idl
  BYPRODUCTS generated.log)
add_executable(helper helper.c)
add_custom_target(prepare
  DEPENDS helper ${CMAKE_CURRENT_BINARY_DIR}/generated.c extra.txt
  BYPRODUCTS prepared.txt)
add_executable(app main.c ${CMAKE_CURRENT_BINARY_DIR}/generated.c)
install(TARGETS helper EXPORT DemoTargets DESTINATION lib)
export(TARGETS helper FILE ${CMAKE_CURRENT_BINARY_DIR}/exports/HelperTargets.cmake NAMESPACE Demo::)
export(EXPORT DemoTargets FILE ${CMAKE_CURRENT_BINARY_DIR}/exports/DemoTargets.cmake NAMESPACE Demo::)
cmake_policy(SET CMP0090 NEW)
set(CMAKE_EXPORT_PACKAGE_REGISTRY ON)
export(PACKAGE DemoPkg)
add_custom_command(TARGET app PRE_BUILD COMMAND echo before BYPRODUCTS before.txt)
add_custom_command(TARGET app PRE_LINK COMMAND echo pre BYPRODUCTS pre.txt)
add_custom_command(TARGET app POST_BUILD COMMAND echo post BYPRODUCTS post.txt)
#@@ENDCASE

#@@CASE install_graph_supported_target_kinds
project(InstallGraph C)
add_executable(app main.c)
add_library(core_static STATIC core_static.c)
set_target_properties(core_static PROPERTIES PUBLIC_HEADER include/core_static.h)
add_library(core_shared SHARED core_shared.c)
add_library(plugin MODULE plugin.c)
add_library(iface INTERFACE)
target_include_directories(iface INTERFACE "$<INSTALL_INTERFACE:include/iface>")
install(TARGETS app RUNTIME DESTINATION bin COMPONENT AppRuntime)
install(TARGETS core_static EXPORT DemoTargets
  ARCHIVE DESTINATION lib/static COMPONENT StaticDevelopment
  PUBLIC_HEADER DESTINATION include/static COMPONENT StaticHeaders)
install(TARGETS core_shared EXPORT DemoTargets
  LIBRARY DESTINATION lib/shared COMPONENT SharedRuntime
  RUNTIME DESTINATION bin/shared COMPONENT SharedRuntime
  ARCHIVE DESTINATION lib/import COMPONENT SharedImport
  NAMELINK_COMPONENT SharedDev)
install(TARGETS plugin EXPORT DemoTargets LIBRARY DESTINATION lib/plugins COMPONENT PluginRuntime)
install(TARGETS iface EXPORT DemoTargets DESTINATION share/meta COMPONENT InterfaceMeta
  INCLUDES DESTINATION include/iface COMPONENT InterfaceHeaders)
install(FILES cmake/DemoConfig.cmake DESTINATION lib/cmake/demo RENAME DemoPkgConfig.cmake COMPONENT DemoConfig)
install(PROGRAMS scripts/tool.sh DESTINATION bin/tools RENAME demo-tool COMPONENT RuntimeTools)
install(DIRECTORY assets DESTINATION share/root COMPONENT RuntimeTree)
install(EXPORT DemoTargets NAMESPACE Demo:: DESTINATION lib/cmake/demo FILE DemoTargets.cmake COMPONENT DemoConfig)
#@@ENDCASE

#@@CASE export_round_trip_graph_snapshot
project(ExportGraph C)
add_library(core STATIC core.c)
set_target_properties(core PROPERTIES EXPORT_NAME api)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/nested")
file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/nested/CMakeLists.txt" "add_library(extra STATIC extra.c)\nset_target_properties(extra PROPERTIES EXPORT_NAME helper)\ninstall(TARGETS extra EXPORT RoundTripTargets ARCHIVE DESTINATION lib/nested COMPONENT Development)\n")
file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/nested/extra.c" "int extra_value(void) { return 7; }\n")
add_subdirectory(nested)
install(TARGETS core EXPORT RoundTripTargets ARCHIVE DESTINATION lib COMPONENT Development)
install(EXPORT RoundTripTargets NAMESPACE Round:: DESTINATION lib/cmake/round FILE RoundTripTargets.cmake COMPONENT Development)
export(TARGETS core extra FILE ${CMAKE_CURRENT_BINARY_DIR}/exports/StandaloneTargets.cmake NAMESPACE Round::)
export(EXPORT RoundTripTargets FILE ${CMAKE_CURRENT_BINARY_DIR}/exports/BuildSetTargets.cmake NAMESPACE Round::)
cmake_policy(SET CMP0090 NEW)
set(CMAKE_EXPORT_PACKAGE_REGISTRY ON)
export(PACKAGE RoundTripPkg)
#@@ENDCASE

#@@CASE cpack_package_plan_snapshot
project(PackDemo C)
add_library(core STATIC core.c)
install(TARGETS core DESTINATION lib)
set(CPACK_GENERATOR "TGZ;ZIP")
set(CPACK_PACKAGE_NAME "PackDemo")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_FILE_NAME "pack-demo")
set(CPACK_PACKAGE_DIRECTORY packages/out)
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)
set(CPACK_COMPONENTS_ALL Runtime Development)
include(CPack)
#@@ENDCASE
