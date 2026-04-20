#@@CASE ctest_local_dashboard_parity_surface
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@FILE_TEXT dash_src/CMakeLists.txt
cmake_minimum_required(VERSION 3.28)
project(NobDiffDash C)
enable_testing()
add_executable(dash main.c)
add_test(NAME dash_run COMMAND dash)
#@@END_FILE_TEXT
#@@FILE_TEXT dash_src/main.c
int main(void) { return 0; }
#@@END_FILE_TEXT
#@@QUERY TREE build/Testing
include("${CMAKE_CURRENT_LIST_DIR}/__nob_diff_helpers/__nob_diff_oracle_common.cmake")
set(CMAKE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(CMAKE_BINARY_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(CMAKE_CURRENT_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(CMAKE_CURRENT_BINARY_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(CTEST_SOURCE_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/dash_src")
set(CTEST_BINARY_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/dash_build")
set(CTEST_CMAKE_COMMAND "$ENV{NOB_DIFF_CMAKE_BIN}")
set(CTEST_COMMAND "$ENV{NOB_DIFF_CTEST_BIN}")
set(CTEST_BUILD_COMMAND "$ENV{NOB_DIFF_CMAKE_BIN} --build .")
ctest_empty_binary_directory("${CTEST_BINARY_DIRECTORY}")
ctest_start(Experimental "${CTEST_SOURCE_DIRECTORY}" "${CTEST_BINARY_DIRECTORY}" QUIET)
ctest_configure(QUIET)
ctest_build(QUIET)
ctest_test(QUIET)
ctest_sleep(0.01)
#@@ENDCASE

#@@CASE ctest_local_coverage_memcheck_parity_surface
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@FILE_TEXT project_src/CMakeLists.txt
cmake_minimum_required(VERSION 3.28)
project(NobDiffCtestExtended NONE)
enable_testing()
add_test(NAME pass
  COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_runner.sh" pass
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/memcheck_work")
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/src/main.c" PROPERTIES LABELS "core;ui")
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/src/net.c" PROPERTIES LABELS infra)
#@@END_FILE_TEXT
#@@FILE_TEXT project_src/src/main.c
int main(void) { return 0; }
#@@END_FILE_TEXT
#@@FILE_TEXT project_src/src/net.c
int net(void) { return 0; }
#@@END_FILE_TEXT
#@@FILE_TEXT project_src/tools/coverage.sh
#!/bin/sh
pwd > coverage.pwd
printf 'coverage ok\n'
exit 0
#@@END_FILE_TEXT
#@@FILE_TEXT project_src/tools/test_runner.sh
#!/bin/sh
mode="$1"
pwd > "test-${mode}.pwd"
printf '%s\n' "$mode"
exit 0
#@@END_FILE_TEXT
#@@FILE_TEXT project_src/tools/memcheck.sh
#!/bin/sh
printf '%s\n' "$*" >> memcheck-args.log
pwd >> memcheck-cwd.log
while [ "$#" -gt 0 ] && [ "$1" != "--" ]; do shift; done
if [ "$#" -gt 0 ]; then shift; fi
"$@"
exit $?
#@@END_FILE_TEXT
#@@QUERY FILE_TEXT build/__oracle/ctest_extended_report.txt
if("$ENV{NOB_DIFF_SOURCE_DIR}" STREQUAL "")
  set(_nob_diff_source_dir "${CMAKE_CURRENT_LIST_DIR}")
else()
  set(_nob_diff_source_dir "$ENV{NOB_DIFF_SOURCE_DIR}")
endif()
set(CMAKE_SOURCE_DIR "${_nob_diff_source_dir}")
if("$ENV{NOB_DIFF_BINARY_DIR}" STREQUAL "")
  set(_nob_diff_binary_dir "${CMAKE_CURRENT_BINARY_DIR}")
else()
  set(_nob_diff_binary_dir "$ENV{NOB_DIFF_BINARY_DIR}")
endif()
set(CMAKE_BINARY_DIR "${_nob_diff_binary_dir}")
set(CMAKE_CURRENT_SOURCE_DIR "${_nob_diff_source_dir}")
set(CMAKE_CURRENT_BINARY_DIR "${_nob_diff_binary_dir}")
cmake_minimum_required(VERSION 3.28)
function(nob_diff_report_reset path)
  get_filename_component(_nob_diff_dir "${path}" DIRECTORY)
  if(NOT "${_nob_diff_dir}" STREQUAL "")
    file(MAKE_DIRECTORY "${_nob_diff_dir}")
  endif()
  file(WRITE "${path}" "")
endfunction()
function(nob_diff_report_append_kv path key value)
  file(APPEND "${path}" "${key}=${value}\n")
endfunction()
set(CTEST_SOURCE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/project_src")
set(CTEST_BINARY_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set(CTEST_CMAKE_COMMAND "$ENV{NOB_DIFF_CMAKE_BIN}")
set(CTEST_COMMAND "$ENV{NOB_DIFF_CTEST_BIN}")
set(CMAKE_GENERATOR "Unix Makefiles")
set(CTEST_CMAKE_GENERATOR "${CMAKE_GENERATOR}")
file(RELATIVE_PATH _source_from_build "${CTEST_BINARY_DIRECTORY}" "${CTEST_SOURCE_DIRECTORY}")
file(MAKE_DIRECTORY "${CTEST_SOURCE_DIRECTORY}/memcheck_work")
set(COVERAGE_COMMAND "/bin/sh;${_source_from_build}/tools/coverage.sh")
set(CTEST_MEMORYCHECK_COMMAND "${CTEST_SOURCE_DIRECTORY}/tools/memcheck.sh")
set(CTEST_MEMORYCHECK_TYPE Valgrind)
set(_report "${CTEST_BINARY_DIRECTORY}/__oracle/ctest_extended_report.txt")
ctest_empty_binary_directory("${CTEST_BINARY_DIRECTORY}")
nob_diff_report_reset("${_report}")
ctest_start(Experimental "${CTEST_SOURCE_DIRECTORY}" "${CTEST_BINARY_DIRECTORY}" QUIET)
ctest_configure(QUIET)
ctest_build(QUIET)
ctest_test(QUIET)
ctest_coverage(LABELS core ui APPEND QUIET)
ctest_memcheck(APPEND QUIET)
set(TAG_EXISTS 0)
set(COVERAGE_XML_EXISTS 0)
set(MEMCHECK_XML_EXISTS 0)
if(EXISTS "${CTEST_BINARY_DIRECTORY}/Testing/TAG")
  set(TAG_EXISTS 1)
  file(READ "${CTEST_BINARY_DIRECTORY}/Testing/TAG" _tag_text)
  string(REGEX MATCH "^[^\r\n]+" _tag "${_tag_text}")
  if(_tag)
    set(_tag_dir "${CTEST_BINARY_DIRECTORY}/Testing/${_tag}")
    if(EXISTS "${_tag_dir}/Coverage.xml")
      set(COVERAGE_XML_EXISTS 1)
    endif()
    if(EXISTS "${_tag_dir}/MemCheck.xml")
      set(MEMCHECK_XML_EXISTS 1)
    endif()
  endif()
endif()
if(EXISTS "${CTEST_SOURCE_DIRECTORY}/memcheck_work/test-pass.pwd")
  set(TEST_WORKDIR_EXISTS 1)
else()
  set(TEST_WORKDIR_EXISTS 0)
endif()
if(EXISTS "${CTEST_SOURCE_DIRECTORY}/memcheck_work/memcheck-args.log")
  set(MEMCHECK_ARGS_EXISTS 1)
else()
  set(MEMCHECK_ARGS_EXISTS 0)
endif()
nob_diff_report_append_kv("${_report}" "TAG_EXISTS" "${TAG_EXISTS}")
nob_diff_report_append_kv("${_report}" "COVERAGE_XML_EXISTS" "${COVERAGE_XML_EXISTS}")
nob_diff_report_append_kv("${_report}" "MEMCHECK_XML_EXISTS" "${MEMCHECK_XML_EXISTS}")
nob_diff_report_append_kv("${_report}" "TEST_WORKDIR_EXISTS" "${TEST_WORKDIR_EXISTS}")
nob_diff_report_append_kv("${_report}" "MEMCHECK_ARGS_EXISTS" "${MEMCHECK_ARGS_EXISTS}")
#@@ENDCASE

#@@CASE ctest_local_dashboard_surface
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@FILE_TEXT dash_src/CMakeLists.txt
cmake_minimum_required(VERSION 3.28)
project(NobDiffDash C)
enable_testing()
add_executable(dash main.c)
add_test(NAME dash_run COMMAND dash)
#@@END_FILE_TEXT
#@@FILE_TEXT dash_src/main.c
int main(void) { return 0; }
#@@END_FILE_TEXT
#@@FILE_TEXT custom/CTestCustom.cmake
set(CTEST_CUSTOM_MARKER yes)
#@@END_FILE_TEXT
#@@FILE_TEXT child.cmake
file(WRITE "${CMAKE_CURRENT_LIST_DIR}/child_marker.txt" "child-loaded\n")
#@@END_FILE_TEXT
#@@QUERY FILE_TEXT build/__oracle/ctest_report.txt
#@@QUERY TREE build/Testing
include("${CMAKE_CURRENT_LIST_DIR}/__nob_diff_helpers/__nob_diff_oracle_common.cmake")
set(CMAKE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(CMAKE_BINARY_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(CMAKE_CURRENT_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(CMAKE_CURRENT_BINARY_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(CTEST_SOURCE_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/dash_src")
set(CTEST_BINARY_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/dash_build")
set(CTEST_CMAKE_COMMAND "$ENV{NOB_DIFF_CMAKE_BIN}")
set(CTEST_COMMAND "$ENV{NOB_DIFF_CTEST_BIN}")
set(CTEST_BUILD_COMMAND "$ENV{NOB_DIFF_CMAKE_BIN} --build .")
set(_report "${CMAKE_BINARY_DIR}/__oracle/ctest_report.txt")
nob_diff_report_reset("${_report}")
ctest_empty_binary_directory("${CTEST_BINARY_DIRECTORY}")
ctest_start(Experimental "${CTEST_SOURCE_DIRECTORY}" "${CTEST_BINARY_DIRECTORY}" QUIET)
ctest_configure(QUIET)
ctest_build(QUIET)
ctest_test(RETURN_VALUE TEST_RV QUIET)
ctest_read_custom_files("${CMAKE_CURRENT_LIST_DIR}/custom")
ctest_run_script("${CMAKE_CURRENT_LIST_DIR}/child.cmake")
ctest_sleep(0.01)
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/child_marker.txt")
  set(CHILD_MARKER 1)
else()
  set(CHILD_MARKER 0)
endif()
if(EXISTS "${CTEST_BINARY_DIRECTORY}/Testing/TAG")
  set(TAG_EXISTS 1)
else()
  set(TAG_EXISTS 0)
endif()
nob_diff_report_append_kv("${_report}" "TEST_RV" "${TEST_RV}")
nob_diff_report_append_kv("${_report}" "CHILD_MARKER" "${CHILD_MARKER}")
nob_diff_report_append_kv("${_report}" "TAG_EXISTS" "${TAG_EXISTS}")
nob_diff_report_append_kv("${_report}" "CUSTOM_MARKER" "${CTEST_CUSTOM_MARKER}")
#@@ENDCASE

#@@CASE ctest_submit_and_upload_surface
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@FILE_TEXT notes.txt
notes
#@@END_FILE_TEXT
#@@FILE_TEXT upload.bin
payload
#@@END_FILE_TEXT
#@@QUERY FILE_TEXT build/__oracle/ctest_report.txt
include("${CMAKE_CURRENT_LIST_DIR}/__nob_diff_helpers/__nob_diff_oracle_common.cmake")
set(CMAKE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(CMAKE_BINARY_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(CMAKE_CURRENT_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(CMAKE_CURRENT_BINARY_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(CTEST_SOURCE_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}")
set(CTEST_BINARY_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}")
set(CTEST_CMAKE_COMMAND "$ENV{NOB_DIFF_CMAKE_BIN}")
set(CTEST_COMMAND "$ENV{NOB_DIFF_CTEST_BIN}")
set(_report "${CMAKE_BINARY_DIR}/__oracle/ctest_report.txt")
nob_diff_report_reset("${_report}")
ctest_start(Experimental "${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_CURRENT_LIST_DIR}" QUIET)
ctest_submit(PARTS Notes FILES "${CMAKE_CURRENT_LIST_DIR}/notes.txt"
  RETURN_VALUE SUBMIT_RV
  QUIET
  SUBMIT_URL "$ENV{NOB_DIFF_CTEST_SERVER_URL}")
ctest_upload(FILES "${CMAKE_CURRENT_LIST_DIR}/upload.bin" QUIET)
nob_diff_report_append_kv("${_report}" "SUBMIT_RV" "${SUBMIT_RV}")
#@@ENDCASE

#@@CASE ctest_invalid_forms
#@@MODE SCRIPT
#@@OUTCOME ERROR
ctest_start()
ctest_submit(FILES)
ctest_upload()
#@@ENDCASE
