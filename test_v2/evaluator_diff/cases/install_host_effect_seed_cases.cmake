#@@CASE install_host_effect_rules_surface
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/install_payload.txt
payload
#@@END_FILE_TEXT
#@@FILE_TEXT source/install_helper.sh
#!/bin/sh
exit 0
#@@END_FILE_TEXT
#@@FILE_TEXT source/install_hook.cmake
set(HOOK 1)
#@@END_FILE_TEXT
#@@FILE_TEXT source/runtime/inst_imported.so
rt
#@@END_FILE_TEXT
#@@FILE_TEXT source/install_assets/asset.txt
asset
#@@END_FILE_TEXT
#@@QUERY FILE_EXISTS build/cmake_install.cmake
#@@QUERY VAR INSTALL_COMPONENTS
set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME Toolkit)
add_library(inst_meta INTERFACE)
add_library(inst_imported SHARED IMPORTED)
set_target_properties(inst_imported PROPERTIES IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/runtime/inst_imported.so")
install(TARGETS inst_meta EXPORT InstExport DESTINATION lib)
install(FILES install_payload.txt TYPE DOC COMPONENT Docs)
install(PROGRAMS install_helper.sh TYPE BIN)
install(DIRECTORY install_assets DESTINATION share/assets)
install(SCRIPT install_hook.cmake COMPONENT Runtime)
install(CODE "set(INSTALL_CODE_MARKER 1)")
install(EXPORT InstExport DESTINATION share/cmake/Inst)
install(EXPORT_ANDROID_MK InstExport DESTINATION share/cmake/android)
install(IMPORTED_RUNTIME_ARTIFACTS inst_imported DESTINATION bin)
install(RUNTIME_DEPENDENCY_SET inst_deps DESTINATION lib/deps)
get_cmake_property(INSTALL_COMPONENTS COMPONENTS)
set(INSTALL_COMPONENTS "${COMPONENTS}")
#@@ENDCASE

#@@CASE install_host_effect_file_set_modules_surface
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/meta_impl.cpp
int meta_impl = 0;
#@@END_FILE_TEXT
#@@FILE_TEXT source/modules/core.hpp
#pragma once
#@@END_FILE_TEXT
#@@FILE_TEXT source/include/meta.hpp
#pragma once
#@@END_FILE_TEXT
#@@QUERY FILE_EXISTS build/cmake_install.cmake
add_library(meta_lib STATIC meta_impl.cpp)
target_sources(meta_lib PUBLIC FILE_SET mods TYPE HEADERS BASE_DIRS modules FILES modules/core.hpp)
install(TARGETS meta_lib EXPORT DemoExport FILE_SET mods DESTINATION include/modules)
#@@ENDCASE

#@@CASE install_host_effect_invalid_forms
#@@OUTCOME ERROR
install(EXPORT)
#@@ENDCASE
