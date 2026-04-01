#@@CASE legacy_meta_policy_gated_surface
#@@PROJECT_LAYOUT RAW_CMAKELISTS
#@@OUTCOME SUCCESS
#@@FILE_TEXT dir_a/CMakeLists.txt
get_property(_vis GLOBAL PROPERTY LEGACY_VISITS)
list(APPEND _vis dir_a)
set_property(GLOBAL PROPERTY LEGACY_VISITS "${_vis}")
#@@END_FILE_TEXT
#@@FILE_TEXT dir_b/CMakeLists.txt
get_property(_vis GLOBAL PROPERTY LEGACY_VISITS)
list(APPEND _vis dir_b)
set_property(GLOBAL PROPERTY LEGACY_VISITS "${_vis}")
#@@END_FILE_TEXT
#@@QUERY FILE_TEXT build/__oracle/legacy_meta_report.txt
cmake_minimum_required(VERSION 3.28)
cmake_policy(SET CMP0032 OLD)
cmake_policy(SET CMP0029 OLD)
project(LegacyMetaSurface LANGUAGES NONE)
include("${CMAKE_CURRENT_SOURCE_DIR}/__nob_diff_helpers/__nob_diff_oracle_common.cmake")
set(_report "${CMAKE_BINARY_DIR}/__oracle/legacy_meta_report.txt")
nob_diff_report_reset("${_report}")
file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/input.c" "int main(void){return 0;}\n")
output_required_files(input.c output.txt)
subdir_depends(dir_a dep1 dep2)
subdirs(dir_a dir_b)
get_property(LEGACY_VISITS GLOBAL PROPERTY LEGACY_VISITS)
nob_diff_sort_join(LEGACY_VISITS_SORTED ${LEGACY_VISITS})
nob_diff_report_append_kv("${_report}" "LEGACY_VISITS" "${LEGACY_VISITS_SORTED}")
nob_diff_report_append_kv("${_report}" "OUTPUT_REQUIRED_CALLED" "1")
nob_diff_report_append_kv("${_report}" "SUBDIR_DEPENDS_CALLED" "1")
#@@ENDCASE

#@@CASE legacy_meta_invalid_forms
#@@PROJECT_LAYOUT RAW_CMAKELISTS
#@@OUTCOME ERROR
cmake_minimum_required(VERSION 3.28)
project(LegacyMetaInvalid LANGUAGES NONE)
output_required_files(input.c)
subdir_depends(src)
subdirs()
#@@ENDCASE
