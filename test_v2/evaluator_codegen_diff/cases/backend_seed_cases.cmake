#@@CASE backend_build_controlled_artifacts
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/src/core.c
int core_value(void) { return 41; }
#@@END_FILE_TEXT
#@@FILE_TEXT source/src/main.c
int core_value(void);
int main(void) { return core_value() == 41 ? 0 : 1; }
#@@END_FILE_TEXT
add_library(core STATIC src/core.c)
set_target_properties(core PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/artifacts/lib")
add_executable(app src/main.c)
target_link_libraries(app PRIVATE core)
set_target_properties(app PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/artifacts/bin")
#@@ENDCASE

#@@CASE backend_install_supported_surface
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/src/core.c
int core_value(void) { return 51; }
#@@END_FILE_TEXT
#@@FILE_TEXT source/src/main.c
int core_value(void);
int main(void) { return core_value() == 51 ? 0 : 1; }
#@@END_FILE_TEXT
#@@FILE_TEXT source/include/core.h
#define CORE_VALUE 51
#@@END_FILE_TEXT
#@@FILE_TEXT source/scripts/helper.sh
#!/bin/sh
exit 0
#@@END_FILE_TEXT
#@@FILE_TEXT source/assets/asset.txt
asset
#@@END_FILE_TEXT
#@@FILE_TEXT source/README.txt
install readme
#@@END_FILE_TEXT
add_library(core STATIC src/core.c)
set_target_properties(core PROPERTIES PUBLIC_HEADER include/core.h)
add_executable(app src/main.c)
target_link_libraries(app PRIVATE core)
install(TARGETS app core EXPORT DemoTargets
  RUNTIME DESTINATION bin
  ARCHIVE DESTINATION lib
  PUBLIC_HEADER DESTINATION include/demo)
install(FILES README.txt DESTINATION share/demo)
install(PROGRAMS scripts/helper.sh DESTINATION bin)
install(DIRECTORY assets/ DESTINATION share/demo_assets)
install(EXPORT DemoTargets DESTINATION lib/cmake/Demo FILE DemoTargets.cmake NAMESPACE Demo::)
#@@ENDCASE

#@@CASE backend_package_supported_archives
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/src/core.c
int core_value(void) { return 61; }
#@@END_FILE_TEXT
#@@FILE_TEXT source/src/main.c
int core_value(void);
int main(void) { return core_value() == 61 ? 0 : 1; }
#@@END_FILE_TEXT
#@@FILE_TEXT source/include/core.h
#define CORE_VALUE 61
#@@END_FILE_TEXT
#@@FILE_TEXT source/README.txt
package readme
#@@END_FILE_TEXT
add_library(core STATIC src/core.c)
set_target_properties(core PROPERTIES PUBLIC_HEADER include/core.h)
add_executable(app src/main.c)
target_link_libraries(app PRIVATE core)
install(TARGETS app core EXPORT DemoTargets
  RUNTIME DESTINATION bin
  ARCHIVE DESTINATION lib
  PUBLIC_HEADER DESTINATION include/demo)
install(FILES README.txt DESTINATION share/demo)
install(EXPORT DemoTargets DESTINATION lib/cmake/Demo FILE DemoTargets.cmake NAMESPACE Demo::)
set(CPACK_GENERATOR "TGZ;TXZ;ZIP")
set(CPACK_PACKAGE_NAME "DemoPkg")
set(CPACK_PACKAGE_VERSION "1.2.3")
set(CPACK_PACKAGE_FILE_NAME "demo-pkg")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/packages")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
include(CPack)
#@@ENDCASE

#@@CASE backend_configure_generation_supported_surface
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/cfg_template.in
VALUE=@GEN_VALUE@
#@@END_FILE_TEXT
set(GEN_VALUE 42)
write_file("${CMAKE_CURRENT_BINARY_DIR}/generated/legacy.txt" legacy-body)
make_directory("${CMAKE_CURRENT_BINARY_DIR}/generated/legacy_dir/sub")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/write_append.txt" "alpha")
file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/generated/write_append.txt" "beta")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/file_dir/sub")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cfg_template.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/configured.txt" @ONLY)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/generated.txt" CONTENT "GEN")
#@@ENDCASE

#@@CASE backend_configure_host_effect_supported_surface
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/download_src.txt
download-body
#@@END_FILE_TEXT
#@@FILE_TEXT source/archive_input/root.txt
root
#@@END_FILE_TEXT
#@@FILE_TEXT source/archive_input/sub/child.txt
child
#@@END_FILE_TEXT
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/download_src.txt" DL_HASH)
file(DOWNLOAD
  "file://${CMAKE_CURRENT_SOURCE_DIR}/download_src.txt"
  "${CMAKE_CURRENT_BINARY_DIR}/replay/downloaded.txt"
  EXPECTED_HASH "SHA256=${DL_HASH}")
file(ARCHIVE_CREATE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/replay/sample.tar"
  PATHS "${CMAKE_CURRENT_SOURCE_DIR}/archive_input"
  FORMAT paxr
  MTIME 0)
file(ARCHIVE_EXTRACT
  INPUT "${CMAKE_CURRENT_BINARY_DIR}/replay/sample.tar"
  DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/replay/archive_out")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/replay/generated.txt" CONTENT "GEN")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/replay/lock_dir")
file(LOCK "${CMAKE_CURRENT_BINARY_DIR}/replay/lock_dir" DIRECTORY)
file(LOCK "${CMAKE_CURRENT_BINARY_DIR}/replay/lock_dir" DIRECTORY RELEASE)
#@@ENDCASE

#@@CASE backend_reject_target_precompile_headers
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/src/main.c
int main(void) { return 0; }
#@@END_FILE_TEXT
#@@FILE_TEXT source/include/pch.h
#define PCH_READY 1
#@@END_FILE_TEXT
add_executable(app src/main.c)
target_precompile_headers(app PRIVATE include/pch.h)
#@@ENDCASE

#@@CASE backend_reject_export_append
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/src/core.c
int core_value(void) { return 0; }
#@@END_FILE_TEXT
add_library(core STATIC src/core.c)
export(TARGETS core FILE CoreTargets.cmake APPEND)
#@@ENDCASE
