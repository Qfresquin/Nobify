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
