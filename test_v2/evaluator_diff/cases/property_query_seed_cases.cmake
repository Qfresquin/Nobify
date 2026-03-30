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

#@@CASE property_query_synthetic_directory_and_cmake_providers
#@@OUTCOME SUCCESS
#@@FILE source/gp_provider_sub/local.c
#@@FILE_TEXT source/gp_provider_sub/CMakeLists.txt
set(CHILD_ONLY child_scope)
macro(child_macro)
endmacro()
add_library(child_real STATIC local.c)
add_library(child_imp STATIC IMPORTED)
add_test(NAME child_test COMMAND "${CMAKE_COMMAND}" -E true)
#@@END_FILE_TEXT
#@@QUERY VAR CP_ROLE
#@@QUERY VAR CP_TRY
#@@QUERY VAR CP_MULTI
#@@QUERY VAR CHILD_DEF
#@@QUERY VAR CHILD_BUILDSYSTEM
#@@QUERY VAR CHILD_IMPORTED
#@@QUERY VAR CHILD_TESTS
#@@QUERY VAR CHILD_SOURCE_HAS_SUBDIR
#@@QUERY VAR CHILD_BINARY_HAS_BUILD
#@@QUERY VAR CHILD_PARENT_HAS_ROOT
#@@QUERY VAR ROOT_SUBDIRS_HAS_CHILD
#@@QUERY VAR HAS_CHILD_VAR
#@@QUERY VAR HAS_CHILD_MACRO
#@@QUERY VAR HAS_CHILD_LISTFILE
enable_testing()
macro(root_macro)
endmacro()
add_subdirectory(gp_provider_sub gp_provider_build)
set(CHILD_ONLY root_shadow)
get_cmake_property(CP_ROLE CMAKE_ROLE)
get_cmake_property(CP_TRY IN_TRY_COMPILE)
get_cmake_property(CP_MULTI GENERATOR_IS_MULTI_CONFIG)
get_directory_property(CHILD_DEF DIRECTORY gp_provider_sub DEFINITION CHILD_ONLY)
get_directory_property(CHILD_SOURCE DIRECTORY gp_provider_sub SOURCE_DIR)
get_directory_property(CHILD_BINARY DIRECTORY gp_provider_sub BINARY_DIR)
get_directory_property(CHILD_PARENT DIRECTORY gp_provider_sub PARENT_DIRECTORY)
get_directory_property(ROOT_SUBDIRS SUBDIRECTORIES)
get_directory_property(CHILD_BUILDSYSTEM DIRECTORY gp_provider_sub BUILDSYSTEM_TARGETS)
get_directory_property(CHILD_IMPORTED DIRECTORY gp_provider_sub IMPORTED_TARGETS)
get_directory_property(CHILD_TESTS DIRECTORY gp_provider_sub TESTS)
get_directory_property(CHILD_VARIABLES DIRECTORY gp_provider_sub VARIABLES)
get_directory_property(CHILD_MACROS DIRECTORY gp_provider_sub MACROS)
get_directory_property(CHILD_LISTFILES DIRECTORY gp_provider_sub LISTFILE_STACK)
list(FIND CHILD_VARIABLES CHILD_ONLY IDX_CHILD_VAR)
list(FIND CHILD_MACROS child_macro IDX_CHILD_MACRO)
string(FIND "${CHILD_SOURCE}" "gp_provider_sub" CHILD_SOURCE_HAS_SUBDIR_POS)
string(FIND "${CHILD_BINARY}" "gp_provider_build" CHILD_BINARY_HAS_BUILD_POS)
string(FIND "${CHILD_PARENT}" "${CMAKE_CURRENT_SOURCE_DIR}" CHILD_PARENT_HAS_ROOT_POS)
string(FIND "${ROOT_SUBDIRS}" "gp_provider_sub" ROOT_SUBDIRS_HAS_CHILD_POS)
string(FIND "${CHILD_LISTFILES}" "gp_provider_sub/CMakeLists.txt" HAS_CHILD_LISTFILE_POS)
if(IDX_CHILD_VAR EQUAL -1)
  set(HAS_CHILD_VAR 0)
else()
  set(HAS_CHILD_VAR 1)
endif()
if(IDX_CHILD_MACRO EQUAL -1)
  set(HAS_CHILD_MACRO 0)
else()
  set(HAS_CHILD_MACRO 1)
endif()
if(CHILD_SOURCE_HAS_SUBDIR_POS EQUAL -1)
  set(CHILD_SOURCE_HAS_SUBDIR 0)
else()
  set(CHILD_SOURCE_HAS_SUBDIR 1)
endif()
if(CHILD_BINARY_HAS_BUILD_POS EQUAL -1)
  set(CHILD_BINARY_HAS_BUILD 0)
else()
  set(CHILD_BINARY_HAS_BUILD 1)
endif()
if(CHILD_PARENT_HAS_ROOT_POS EQUAL -1)
  set(CHILD_PARENT_HAS_ROOT 0)
else()
  set(CHILD_PARENT_HAS_ROOT 1)
endif()
if(ROOT_SUBDIRS_HAS_CHILD_POS EQUAL -1)
  set(ROOT_SUBDIRS_HAS_CHILD 0)
else()
  set(ROOT_SUBDIRS_HAS_CHILD 1)
endif()
if(HAS_CHILD_LISTFILE_POS EQUAL -1)
  set(HAS_CHILD_LISTFILE 0)
else()
  set(HAS_CHILD_LISTFILE 1)
endif()
#@@ENDCASE

#@@CASE property_query_binary_dir_scoped_source_and_test_wrappers
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/gp_bin_src/CMakeLists.txt
set_property(DIRECTORY PROPERTY BIN_DIR_PROP from_child)
set_source_files_properties(sub_file.c PROPERTIES BIN_SRC_PROP from_source)
add_test(NAME gp_bin_test COMMAND "${CMAKE_COMMAND}" -E true)
set_tests_properties(gp_bin_test PROPERTIES LABELS from_test)
#@@END_FILE_TEXT
#@@FILE source/gp_bin_src/sub_file.c
#@@QUERY VAR DIR_FROM_BIN
#@@QUERY VAR TEST_FROM_BIN
#@@QUERY VAR SRC_WRAP_FROM_BIN
#@@QUERY VAR TEST_WRAP_FROM_BIN
#@@QUERY VAR DIR_UPDATED
#@@QUERY VAR SRC_UPDATED
#@@QUERY VAR TEST_UPDATED
#@@QUERY VAR SRC_WRAP_UPDATED
#@@QUERY VAR TEST_WRAP_UPDATED
enable_testing()
add_subdirectory(gp_bin_src gp_bin_build)
set(CHILD_BIN_DIR "${CMAKE_CURRENT_BINARY_DIR}/gp_bin_build")
get_property(DIR_FROM_BIN DIRECTORY "${CHILD_BIN_DIR}" PROPERTY BIN_DIR_PROP)
get_property(TEST_FROM_BIN TEST gp_bin_test DIRECTORY "${CHILD_BIN_DIR}" PROPERTY LABELS)
get_source_file_property(SRC_WRAP_FROM_BIN sub_file.c DIRECTORY "${CHILD_BIN_DIR}" BIN_SRC_PROP)
get_test_property(gp_bin_test LABELS DIRECTORY "${CHILD_BIN_DIR}" TEST_WRAP_FROM_BIN)
set_property(DIRECTORY "${CHILD_BIN_DIR}" PROPERTY BIN_DIR_PROP updated_dir)
set_property(SOURCE sub_file.c DIRECTORY "${CHILD_BIN_DIR}" PROPERTY BIN_SRC_PROP updated_source)
set_property(TEST gp_bin_test DIRECTORY "${CHILD_BIN_DIR}" PROPERTY LABELS updated_test)
get_property(DIR_UPDATED DIRECTORY "${CHILD_BIN_DIR}" PROPERTY BIN_DIR_PROP)
get_property(SRC_UPDATED SOURCE sub_file.c DIRECTORY "${CHILD_BIN_DIR}" PROPERTY BIN_SRC_PROP)
get_property(TEST_UPDATED TEST gp_bin_test DIRECTORY "${CHILD_BIN_DIR}" PROPERTY LABELS)
get_source_file_property(SRC_WRAP_UPDATED sub_file.c DIRECTORY "${CHILD_BIN_DIR}" BIN_SRC_PROP)
get_test_property(gp_bin_test LABELS DIRECTORY "${CHILD_BIN_DIR}" TEST_WRAP_UPDATED)
#@@ENDCASE
