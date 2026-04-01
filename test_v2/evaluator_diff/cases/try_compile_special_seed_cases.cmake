#@@CASE try_compile_source_and_project_surface
#@@OUTCOME SUCCESS
#@@FILE_TEXT tc_project/CMakeLists.txt
cmake_minimum_required(VERSION 3.28)
project(TryCompileProject C)
add_library(tc_project_lib STATIC main.c)
#@@END_FILE_TEXT
#@@FILE_TEXT tc_project/main.c
int tc_project_symbol(void) { return 0; }
#@@END_FILE_TEXT
#@@QUERY FILE_TEXT build/__oracle/try_compile_report.txt
#@@QUERY FILE_EXISTS build/copied_probe.bin
include("${CMAKE_CURRENT_SOURCE_DIR}/__nob_diff_helpers/__nob_diff_oracle_common.cmake")
set(_report "${CMAKE_BINARY_DIR}/__oracle/try_compile_report.txt")
nob_diff_report_reset("${_report}")
try_compile(TC_SRC
  SOURCE_FROM_CONTENT probe.c "int main(void){return 0;}\n"
  OUTPUT_VARIABLE TC_SRC_LOG
  COPY_FILE "${CMAKE_BINARY_DIR}/copied_probe.bin"
  COPY_FILE_ERROR TC_COPY_ERR
  CMAKE_FLAGS "-DCMAKE_C_STANDARD=99"
  NO_CACHE)
try_compile(TC_PROJECT
  PROJECT DemoProject
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tc_project"
  BINARY_DIR "${CMAKE_BINARY_DIR}/tc_project_build"
  TARGET tc_project_lib
  OUTPUT_VARIABLE TC_PROJECT_LOG
  NO_CACHE)
nob_diff_bool(TC_SRC_BOOL "${TC_SRC}")
nob_diff_bool(TC_PROJECT_BOOL "${TC_PROJECT}")
if(DEFINED TC_SRC_LOG)
  set(TC_SRC_LOG_DEFINED 1)
else()
  set(TC_SRC_LOG_DEFINED 0)
endif()
if(DEFINED TC_PROJECT_LOG)
  set(TC_PROJECT_LOG_DEFINED 1)
else()
  set(TC_PROJECT_LOG_DEFINED 0)
endif()
if(EXISTS "${CMAKE_BINARY_DIR}/copied_probe.bin")
  set(TC_COPY_EXISTS 1)
else()
  set(TC_COPY_EXISTS 0)
endif()
nob_diff_report_append_kv("${_report}" "TC_SRC" "${TC_SRC_BOOL}")
nob_diff_report_append_kv("${_report}" "TC_SRC_LOG_DEFINED" "${TC_SRC_LOG_DEFINED}")
nob_diff_report_append_kv("${_report}" "TC_COPY_ERR" "${TC_COPY_ERR}")
nob_diff_report_append_kv("${_report}" "TC_COPY_EXISTS" "${TC_COPY_EXISTS}")
nob_diff_report_append_kv("${_report}" "TC_PROJECT" "${TC_PROJECT_BOOL}")
nob_diff_report_append_kv("${_report}" "TC_PROJECT_LOG_DEFINED" "${TC_PROJECT_LOG_DEFINED}")
#@@ENDCASE

#@@CASE try_compile_invalid_forms
#@@OUTCOME ERROR
try_compile()
try_compile(BAD_PROJECT PROJECT DemoProject BINARY_DIR "${CMAKE_BINARY_DIR}/bad_project")
#@@ENDCASE
