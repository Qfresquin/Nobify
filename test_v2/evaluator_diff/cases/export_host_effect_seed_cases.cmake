#@@CASE export_host_effect_target_and_export_file_surface
#@@OUTCOME SUCCESS
#@@QUERY FILE_EXISTS build/meta-targets.cmake
#@@QUERY FILE_EXISTS build/meta-export.cmake
add_library(meta_lib INTERFACE)
install(TARGETS meta_lib EXPORT DemoExport DESTINATION lib)
export(TARGETS meta_lib FILE meta-targets.cmake NAMESPACE Demo::)
export(EXPORT DemoExport FILE meta-export.cmake NAMESPACE Demo::)
#@@ENDCASE

#@@CASE export_host_effect_package_registry_surface
#@@OUTCOME SUCCESS
#@@ENV_PATH HOME build/home
#@@ENV_PATH XDG_DATA_HOME build/xdg
#@@QUERY VAR PkgRegOn_FOUND
#@@QUERY VAR PkgRegOn_FROM
#@@QUERY VAR PkgRegOn_REG_COUNT
#@@QUERY VAR PkgRegOn_REG_POINTS_TO_BUILD
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/PkgRegOnConfig.cmake" [=[set(PkgRegOn_FOUND 1)
set(PkgRegOn_FROM registry-on)
]=])
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)
set(CMAKE_PREFIX_PATH "")
cmake_policy(SET CMP0090 NEW)
set(CMAKE_EXPORT_PACKAGE_REGISTRY TRUE)
export(PACKAGE PkgRegOn)
file(GLOB PkgRegOn_REG_ENTRIES "$ENV{HOME}/.cmake/packages/PkgRegOn/*")
list(LENGTH PkgRegOn_REG_ENTRIES PkgRegOn_REG_COUNT)
if(PkgRegOn_REG_COUNT GREATER 0)
  list(GET PkgRegOn_REG_ENTRIES 0 PkgRegOn_REG_FIRST)
  file(READ "${PkgRegOn_REG_FIRST}" PkgRegOn_REG_CONTENT)
  string(STRIP "${PkgRegOn_REG_CONTENT}" PkgRegOn_REG_CONTENT)
  if(PkgRegOn_REG_CONTENT STREQUAL "${CMAKE_CURRENT_BINARY_DIR}")
    set(PkgRegOn_REG_POINTS_TO_BUILD 1)
  else()
    set(PkgRegOn_REG_POINTS_TO_BUILD 0)
  endif()
else()
  set(PkgRegOn_REG_POINTS_TO_BUILD 0)
endif()
find_package(PkgRegOn CONFIG QUIET)
#@@ENDCASE

#@@CASE export_host_effect_invalid_forms
#@@OUTCOME ERROR
add_library(real INTERFACE)
add_library(alias_real ALIAS real)
export(TARGETS real FILE bad-export.txt)
#@@ENDCASE
