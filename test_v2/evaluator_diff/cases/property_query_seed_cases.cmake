#@@CASE property_query_core_define_property_and_directory_wrappers
#@@OUTCOME SUCCESS
#@@QUERY VAR GP_SET
#@@QUERY VAR GP_DEF
#@@QUERY VAR GP_BRIEF
#@@QUERY VAR GP_FULL
#@@QUERY VAR GP_MISSING_DOC
#@@QUERY VAR GP_INH
#@@QUERY VAR GP_DIR
#@@QUERY VAR GP_DEFVAR
define_property(GLOBAL PROPERTY BATCH_DOC BRIEF_DOCS short_doc FULL_DOCS long_doc)
define_property(DIRECTORY PROPERTY INHERITED_DIR INHERITED BRIEF_DOCS dir_short FULL_DOCS dir_long)
set_property(GLOBAL PROPERTY INHERITED_DIR inherited_global)
set_directory_properties(PROPERTIES BATCH_DIR_PROP dir_value)
set(SCOPE_VAR scope_value)
get_property(GP_SET GLOBAL PROPERTY BATCH_DOC SET)
get_property(GP_DEF GLOBAL PROPERTY BATCH_DOC DEFINED)
get_property(GP_BRIEF GLOBAL PROPERTY BATCH_DOC BRIEF_DOCS)
get_property(GP_FULL GLOBAL PROPERTY BATCH_DOC FULL_DOCS)
get_property(GP_MISSING_DOC GLOBAL PROPERTY UNKNOWN_BATCH_PROP BRIEF_DOCS)
get_property(GP_INH DIRECTORY PROPERTY INHERITED_DIR)
get_directory_property(GP_DIR BATCH_DIR_PROP)
get_directory_property(GP_DEFVAR DEFINITION SCOPE_VAR)
#@@ENDCASE

#@@CASE property_query_target_source_and_test_wrappers
#@@OUTCOME SUCCESS
#@@FILE main.c
#@@QUERY VAR TGT_OK
#@@QUERY VAR TGT_MISS
#@@QUERY VAR SRC_OK
#@@QUERY VAR SRC_DIR
#@@QUERY VAR SRC_TGT_DIR
#@@QUERY VAR SRC_MISS
#@@QUERY VAR TEST_OK
#@@QUERY VAR TEST_DIR_OK
#@@QUERY VAR TEST_MISS
#@@QUERY VAR TEST_UNDECLARED
enable_testing()
add_executable(batch_target main.c)
set_target_properties(batch_target PROPERTIES CUSTOM_TGT hello)
set_source_files_properties(main.c PROPERTIES SRC_FLAG yes)
add_test(NAME batch_test COMMAND "${CMAKE_COMMAND}" -E true)
set_tests_properties(batch_test DIRECTORY . PROPERTIES LABELS fast)
get_target_property(TGT_OK batch_target CUSTOM_TGT)
get_target_property(TGT_MISS batch_target UNKNOWN_TGT)
get_source_file_property(SRC_OK main.c SRC_FLAG)
get_source_file_property(SRC_DIR main.c DIRECTORY . SRC_FLAG)
get_source_file_property(SRC_TGT_DIR main.c TARGET_DIRECTORY batch_target SRC_FLAG)
get_source_file_property(SRC_MISS main.c UNKNOWN_SRC)
get_property(TEST_OK TEST batch_test DIRECTORY . PROPERTY LABELS)
get_test_property(batch_test LABELS DIRECTORY . TEST_DIR_OK)
get_test_property(batch_test UNKNOWN_TEST TEST_MISS)
get_test_property(missing_test LABELS TEST_UNDECLARED)
#@@ENDCASE

#@@CASE property_query_set_property_cache_updates_existing_entry
#@@OUTCOME SUCCESS
#@@QUERY VAR CACHED_X_VALUE
#@@QUERY VAR CACHED_X_ADVANCED
#@@QUERY CACHE_DEFINED CACHED_X
set(CACHED_X old CACHE STRING "doc")
mark_as_advanced(FORCE CACHED_X)
set_property(CACHE CACHED_X PROPERTY VALUE new_ok)
get_property(CACHED_X_VALUE CACHE CACHED_X PROPERTY VALUE)
get_property(CACHED_X_ADVANCED CACHE CACHED_X PROPERTY ADVANCED)
#@@ENDCASE

#@@CASE property_query_set_property_cache_requires_existing_entry
#@@OUTCOME ERROR
set_property(CACHE MISSING_X PROPERTY VALUE bad)
#@@ENDCASE

#@@CASE property_query_source_directory_clause_and_get_cmake_property_lists
#@@OUTCOME SUCCESS
#@@FILE main.c
#@@QUERY VAR SRC_SCOPED
#@@QUERY VAR HAS_VAR
#@@QUERY VAR HAS_CACHE
#@@QUERY VAR HAS_COMMAND_ADD_LIBRARY
#@@QUERY VAR HAS_COMMAND_FUNCTION
#@@QUERY VAR HAS_COMMAND_MACRO
#@@QUERY VAR HAS_MACRO
#@@QUERY VAR MISSING_PROP
function(BatchFunction)
endfunction()
macro(batch_macro)
endmacro()
set(NORMAL_A one)
set(CACHED_A two CACHE STRING "doc")
set_source_files_properties(main.c DIRECTORY . PROPERTIES SCOPED_SRC local)
get_property(SRC_SCOPED SOURCE main.c DIRECTORY . PROPERTY SCOPED_SRC)
get_cmake_property(ALL_VARS VARIABLES)
get_cmake_property(CACHE_VARS CACHE_VARIABLES)
get_cmake_property(ALL_COMMANDS COMMANDS)
get_cmake_property(ALL_MACROS MACROS)
get_cmake_property(MISSING_PROP DOES_NOT_EXIST)
list(FIND ALL_VARS NORMAL_A IDX_VAR)
list(FIND CACHE_VARS CACHED_A IDX_CACHE)
list(FIND ALL_COMMANDS add_library IDX_COMMAND_ADD_LIBRARY)
list(FIND ALL_COMMANDS batchfunction IDX_COMMAND_FUNCTION)
list(FIND ALL_COMMANDS batch_macro IDX_COMMAND_MACRO)
list(FIND ALL_MACROS batch_macro IDX_MACRO)
if(IDX_VAR EQUAL -1)
  set(HAS_VAR 0)
else()
  set(HAS_VAR 1)
endif()
if(IDX_CACHE EQUAL -1)
  set(HAS_CACHE 0)
else()
  set(HAS_CACHE 1)
endif()
if(IDX_COMMAND_ADD_LIBRARY EQUAL -1)
  set(HAS_COMMAND_ADD_LIBRARY 0)
else()
  set(HAS_COMMAND_ADD_LIBRARY 1)
endif()
if(IDX_COMMAND_FUNCTION EQUAL -1)
  set(HAS_COMMAND_FUNCTION 0)
else()
  set(HAS_COMMAND_FUNCTION 1)
endif()
if(IDX_COMMAND_MACRO EQUAL -1)
  set(HAS_COMMAND_MACRO 0)
else()
  set(HAS_COMMAND_MACRO 1)
endif()
if(IDX_MACRO EQUAL -1)
  set(HAS_MACRO 0)
else()
  set(HAS_MACRO 1)
endif()
#@@ENDCASE
