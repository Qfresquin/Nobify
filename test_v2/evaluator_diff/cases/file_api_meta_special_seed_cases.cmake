#@@CASE file_api_meta_custom_targets_source_groups_and_msproject_surface
#@@OUTCOME SUCCESS
#@@FILE_TEXT main.c
int main(void) { return 0; }
#@@END_FILE_TEXT
#@@FILE_TEXT src/a.c
int a(void) { return 1; }
#@@END_FILE_TEXT
#@@FILE_TEXT src/sub/b.c
int b(void) { return 2; }
#@@END_FILE_TEXT
#@@FILE_TEXT texts/info.txt
hello
#@@END_FILE_TEXT
#@@FILE_TEXT external.vcxproj
<Project DefaultTargets="Build" />
#@@END_FILE_TEXT
#@@QUERY FILE_TEXT build/__oracle/file_api_meta_report.txt
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/__oracle")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/__oracle/file_api_meta_prelude.txt"
  "MSPROJECT:ext_proj:external.vcxproj\n")
add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated.txt"
  COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_CURRENT_BINARY_DIR}/generated.txt"
  BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/byproduct.txt")
add_custom_target(gen ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/generated.txt")
add_executable(app main.c "${CMAKE_CURRENT_BINARY_DIR}/generated.txt" src/a.c src/sub/b.c texts/info.txt)
add_dependencies(app gen)
source_group("Root Files" FILES main.c)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX Generated FILES src/a.c src/sub/b.c)
source_group(Texts REGULAR_EXPRESSION ".*\\.txt$")
include_external_msproject(ext_proj external.vcxproj TYPE type-guid GUID proj-guid PLATFORM Win32 app)
cmake_file_api(QUERY API_VERSION 1 CODEMODEL 2 CACHE 2.0 CMAKEFILES 1 TOOLCHAINS 1)
#@@ENDCASE

#@@CASE file_api_meta_invalid_forms
#@@OUTCOME ERROR
include_external_msproject(ext_proj external.vcxproj TYPE)
cmake_file_api(QUERY API_VERSION 1 CODEMODEL)
source_group(TREE src FILES ../outside.c)
#@@ENDCASE
