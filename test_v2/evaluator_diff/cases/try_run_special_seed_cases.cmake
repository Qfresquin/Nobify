#@@CASE try_run_new_and_legacy_surface
#@@OUTCOME SUCCESS
#@@FILE_TEXT legacy_try_run.c
#include <stdio.h>
int main(void) { putchar('L'); return 0; }
#@@END_FILE_TEXT
#@@QUERY FILE_TEXT build/__oracle/try_run_report.txt
include("${CMAKE_CURRENT_SOURCE_DIR}/__nob_diff_helpers/__nob_diff_oracle_common.cmake")
set(_report "${CMAKE_BINARY_DIR}/__oracle/try_run_report.txt")
nob_diff_report_reset("${_report}")
try_run(RUN_NEW COMPILE_NEW
  SOURCE_FROM_CONTENT probe_new.c "#include <stdio.h>\nint main(void){putchar('N');fputc('E', stderr);return 2;}\n"
  NO_CACHE
  COMPILE_OUTPUT_VARIABLE COMPILE_NEW_LOG
  RUN_OUTPUT_STDOUT_VARIABLE RUN_NEW_STDOUT
  RUN_OUTPUT_STDERR_VARIABLE RUN_NEW_STDERR)
try_run(RUN_LEGACY COMPILE_LEGACY
  "${CMAKE_BINARY_DIR}/legacy_try_build"
  "${CMAKE_CURRENT_SOURCE_DIR}/legacy_try_run.c"
  NO_CACHE
  OUTPUT_VARIABLE RUN_LEGACY_ALL)
nob_diff_bool(COMPILE_NEW_BOOL "${COMPILE_NEW}")
nob_diff_bool(COMPILE_LEGACY_BOOL "${COMPILE_LEGACY}")
if(DEFINED COMPILE_NEW_LOG)
  set(COMPILE_NEW_LOG_DEFINED 1)
else()
  set(COMPILE_NEW_LOG_DEFINED 0)
endif()
if(DEFINED RUN_LEGACY_ALL)
  set(RUN_LEGACY_ALL_DEFINED 1)
else()
  set(RUN_LEGACY_ALL_DEFINED 0)
endif()
nob_diff_report_append_kv("${_report}" "COMPILE_NEW" "${COMPILE_NEW_BOOL}")
nob_diff_report_append_kv("${_report}" "RUN_NEW" "${RUN_NEW}")
nob_diff_report_append_kv("${_report}" "RUN_NEW_STDOUT" "${RUN_NEW_STDOUT}")
nob_diff_report_append_kv("${_report}" "RUN_NEW_STDERR" "${RUN_NEW_STDERR}")
nob_diff_report_append_kv("${_report}" "COMPILE_NEW_LOG_DEFINED" "${COMPILE_NEW_LOG_DEFINED}")
nob_diff_report_append_kv("${_report}" "COMPILE_LEGACY" "${COMPILE_LEGACY_BOOL}")
nob_diff_report_append_kv("${_report}" "RUN_LEGACY" "${RUN_LEGACY}")
nob_diff_report_append_kv("${_report}" "RUN_LEGACY_ALL_DEFINED" "${RUN_LEGACY_ALL_DEFINED}")
#@@ENDCASE

#@@CASE try_run_crosscompile_prefilled_surface
#@@OUTCOME SUCCESS
#@@QUERY FILE_TEXT build/__oracle/try_run_report.txt
include("${CMAKE_CURRENT_SOURCE_DIR}/__nob_diff_helpers/__nob_diff_oracle_common.cmake")
set(_report "${CMAKE_BINARY_DIR}/__oracle/try_run_report.txt")
nob_diff_report_reset("${_report}")
set(RUN_PRESET 23 CACHE STRING "")
set(RUN_PRESET__TRYRUN_OUTPUT_STDOUT preset_out CACHE STRING "")
set(RUN_PRESET__TRYRUN_OUTPUT_STDERR preset_err CACHE STRING "")
set(CMAKE_CROSSCOMPILING ON)
unset(CMAKE_CROSSCOMPILING_EMULATOR)
try_run(RUN_PRESET COMPILE_PRESET
  SOURCE_FROM_CONTENT probe_preset.c "int main(void){return 0;}\n"
  RUN_OUTPUT_STDOUT_VARIABLE RUN_PRESET_STDOUT
  RUN_OUTPUT_STDERR_VARIABLE RUN_PRESET_STDERR)
set(CMAKE_CROSSCOMPILING OFF)
nob_diff_bool(COMPILE_PRESET_BOOL "${COMPILE_PRESET}")
if(EXISTS "${CMAKE_BINARY_DIR}/TryRunResults.cmake")
  set(TRYRUN_RESULTS_EXISTS 1)
else()
  set(TRYRUN_RESULTS_EXISTS 0)
endif()
nob_diff_report_append_kv("${_report}" "COMPILE_PRESET" "${COMPILE_PRESET_BOOL}")
nob_diff_report_append_kv("${_report}" "RUN_PRESET" "${RUN_PRESET}")
nob_diff_report_append_kv("${_report}" "RUN_PRESET_STDOUT" "${RUN_PRESET_STDOUT}")
nob_diff_report_append_kv("${_report}" "RUN_PRESET_STDERR" "${RUN_PRESET_STDERR}")
nob_diff_report_append_kv("${_report}" "TRYRUN_RESULTS_EXISTS" "${TRYRUN_RESULTS_EXISTS}")
#@@ENDCASE

#@@CASE try_run_crosscompile_placeholder_surface
#@@OUTCOME ERROR
#@@QUERY FILE_TEXT build/__oracle/try_run_report.txt
#@@QUERY FILE_EXISTS build/TryRunResults.cmake
include("${CMAKE_CURRENT_SOURCE_DIR}/__nob_diff_helpers/__nob_diff_oracle_common.cmake")
set(_report "${CMAKE_BINARY_DIR}/__oracle/try_run_report.txt")
nob_diff_report_reset("${_report}")
set(CMAKE_CROSSCOMPILING ON)
unset(CMAKE_CROSSCOMPILING_EMULATOR)
try_run(RUN_XC COMPILE_XC
  SOURCE_FROM_CONTENT probe_xc.c "int main(void){return 0;}\n"
  NO_CACHE
  RUN_OUTPUT_STDOUT_VARIABLE RUN_XC_STDOUT
  RUN_OUTPUT_STDERR_VARIABLE RUN_XC_STDERR)
set(CMAKE_CROSSCOMPILING OFF)
nob_diff_bool(COMPILE_XC_BOOL "${COMPILE_XC}")
if(EXISTS "${CMAKE_BINARY_DIR}/TryRunResults.cmake")
  set(TRYRUN_RESULTS_EXISTS 1)
else()
  set(TRYRUN_RESULTS_EXISTS 0)
endif()
nob_diff_report_append_kv("${_report}" "COMPILE_XC" "${COMPILE_XC_BOOL}")
nob_diff_report_append_kv("${_report}" "RUN_XC" "${RUN_XC}")
nob_diff_report_append_kv("${_report}" "TRYRUN_RESULTS_EXISTS" "${TRYRUN_RESULTS_EXISTS}")
#@@ENDCASE

#@@CASE try_run_invalid_forms
#@@OUTCOME ERROR
try_run()
try_run(RUN_BAD2 COMPILE_BAD2
  SOURCE_FROM_CONTENT probe_bad2.c "int main(void){return 0;}\n"
  RUN_OUTPUT_VARIABLE)
try_run(RUN_BAD3 COMPILE_BAD3
  SOURCE_FROM_CONTENT probe_bad3.c "int main(void){return 0;}\n"
  RUN_OUTPUT_VARIABLE BAD_ALL
  RUN_OUTPUT_STDOUT_VARIABLE BAD_STDOUT)
#@@ENDCASE
