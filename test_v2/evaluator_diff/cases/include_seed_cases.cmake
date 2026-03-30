#@@CASE include_result_optional_and_path_surface
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@FILE_TEXT direct_inc.cmake
set(DIRECT_HIT 1)
#@@END_FILE_TEXT
#@@QUERY VAR DIRECT_HIT
#@@QUERY VAR DIRECT_RES_NAME
#@@QUERY VAR OPTIONAL_RES
include("${CMAKE_CURRENT_LIST_DIR}/direct_inc.cmake" RESULT_VARIABLE DIRECT_RES)
get_filename_component(DIRECT_RES_NAME "${DIRECT_RES}" NAME)
include(missing_optional OPTIONAL RESULT_VARIABLE OPTIONAL_RES)
#@@ENDCASE

#@@CASE include_module_search_surface
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@FILE_TEXT cmkmods/MyInc.cmake
set(MOD_HIT 1)
#@@END_FILE_TEXT
#@@QUERY VAR MOD_HIT
#@@QUERY VAR MOD_RES_NAME
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmkmods")
include(MyInc RESULT_VARIABLE MOD_RES)
get_filename_component(MOD_RES_NAME "${MOD_RES}" NAME)
#@@ENDCASE

#@@CASE include_guard_scope_surface
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@FILE_TEXT guard_default.cmake
include_guard()
set(DEFAULT_HITS "${DEFAULT_HITS}x")
#@@END_FILE_TEXT
#@@FILE_TEXT guard_directory.cmake
include_guard(DIRECTORY)
set(DIR_HITS "${DIR_HITS}x")
#@@END_FILE_TEXT
#@@FILE_TEXT guard_global.cmake
include_guard(GLOBAL)
set(GLOBAL_HITS "${GLOBAL_HITS}x" PARENT_SCOPE)
#@@END_FILE_TEXT
#@@QUERY VAR DEFAULT_HITS
#@@QUERY VAR DIR_HITS
#@@QUERY VAR GLOBAL_HITS
include(guard_default.cmake)
include(guard_default.cmake)
include(guard_directory.cmake)
include(guard_directory.cmake)
function(run_guard_once)
  include(guard_global.cmake)
endfunction()
run_guard_once()
run_guard_once()
#@@ENDCASE

#@@CASE include_cmp0017_search_surface
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@FILE_TEXT user_mods/Foo.cmake
set(PICK user)
#@@END_FILE_TEXT
#@@FILE_TEXT fake_root/Modules/Foo.cmake
set(PICK root)
#@@END_FILE_TEXT
#@@FILE_TEXT fake_root/Modules/Caller.cmake
include(Foo)
#@@END_FILE_TEXT
#@@QUERY VAR PICK_OLD
#@@QUERY VAR PICK_NEW
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/user_mods")
set(CMAKE_ROOT "${CMAKE_CURRENT_LIST_DIR}/fake_root")
cmake_policy(SET CMP0017 OLD)
include("${CMAKE_CURRENT_LIST_DIR}/fake_root/Modules/Caller.cmake")
set(PICK_OLD "${PICK}")
unset(PICK)
cmake_policy(SET CMP0017 NEW)
include("${CMAKE_CURRENT_LIST_DIR}/fake_root/Modules/Caller.cmake")
set(PICK_NEW "${PICK}")
#@@ENDCASE

#@@CASE include_invalid_option_forms
#@@MODE SCRIPT
#@@OUTCOME ERROR
#@@FILE_TEXT inc_ok.cmake
set(X 1)
#@@END_FILE_TEXT
include(inc_ok.cmake BAD_OPT)
include(inc_ok.cmake RESULT_VARIABLE)
include_guard(BAD)
#@@ENDCASE
