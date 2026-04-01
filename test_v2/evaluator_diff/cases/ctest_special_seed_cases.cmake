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
