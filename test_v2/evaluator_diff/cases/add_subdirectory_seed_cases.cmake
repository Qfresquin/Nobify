#@@CASE add_subdirectory_directory_query_surface
#@@OUTCOME SUCCESS
#@@QUERY VAR CHILD_DEF
#@@QUERY VAR CHILD_SOURCE_OK
#@@QUERY VAR CHILD_BINARY_OK
#@@QUERY VAR CHILD_PARENT_OK
#@@QUERY VAR ROOT_SUBDIRS_OK
#@@QUERY VAR CHILD_BUILDSYSTEM
#@@QUERY VAR CHILD_IMPORTED
#@@QUERY VAR CHILD_TESTS
#@@QUERY VAR CHILD_VAR_FOUND
#@@QUERY VAR CHILD_MACRO_FOUND
#@@QUERY VAR CHILD_LISTFILE_FOUND
enable_testing()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/gp_provider_sub")
file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/gp_provider_sub/local.c" "int gp_provider_local;\n")
file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/gp_provider_sub/CMakeLists.txt" [=[
set(CHILD_ONLY child_scope)
macro(child_macro)
endmacro()
add_library(child_real STATIC local.c)
add_library(child_imp STATIC IMPORTED)
add_test(NAME child_test COMMAND "${CMAKE_COMMAND}" -E true)
]=])
add_subdirectory(gp_provider_sub gp_provider_build)
set(CHILD_ONLY root_shadow)
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
if(CHILD_SOURCE STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/gp_provider_sub")
  set(CHILD_SOURCE_OK 1)
else()
  set(CHILD_SOURCE_OK 0)
endif()
if(CHILD_BINARY STREQUAL "${CMAKE_CURRENT_BINARY_DIR}/gp_provider_build")
  set(CHILD_BINARY_OK 1)
else()
  set(CHILD_BINARY_OK 0)
endif()
if(CHILD_PARENT STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
  set(CHILD_PARENT_OK 1)
else()
  set(CHILD_PARENT_OK 0)
endif()
list(FIND ROOT_SUBDIRS "${CMAKE_CURRENT_SOURCE_DIR}/gp_provider_sub" ROOT_SUBDIR_IDX)
if(ROOT_SUBDIR_IDX GREATER -1)
  set(ROOT_SUBDIRS_OK 1)
else()
  set(ROOT_SUBDIRS_OK 0)
endif()
list(FIND CHILD_VARIABLES CHILD_ONLY IDX_CHILD_VAR)
if(IDX_CHILD_VAR GREATER -1)
  set(CHILD_VAR_FOUND 1)
else()
  set(CHILD_VAR_FOUND 0)
endif()
list(FIND CHILD_MACROS child_macro IDX_CHILD_MACRO)
if(IDX_CHILD_MACRO GREATER -1)
  set(CHILD_MACRO_FOUND 1)
else()
  set(CHILD_MACRO_FOUND 0)
endif()
list(FIND CHILD_LISTFILES "${CMAKE_CURRENT_SOURCE_DIR}/gp_provider_sub/CMakeLists.txt" IDX_CHILD_LISTFILE)
if(IDX_CHILD_LISTFILE GREATER -1)
  set(CHILD_LISTFILE_FOUND 1)
else()
  set(CHILD_LISTFILE_FOUND 0)
endif()
#@@ENDCASE

#@@CASE add_subdirectory_cross_directory_target_metadata_surface
#@@OUTCOME SUCCESS
#@@FILE root.c
#@@QUERY TARGET_EXISTS root_real
#@@QUERY TARGET_EXISTS root_imported
#@@QUERY TARGET_EXISTS root_alias
#@@QUERY TARGET_EXISTS child_local
#@@QUERY TARGET_EXISTS child_imported_local
#@@QUERY TARGET_EXISTS child_local_alias
#@@QUERY TARGET_EXISTS child_imported_alias
#@@QUERY TARGET_PROP root_real TYPE
#@@QUERY TARGET_PROP root_real IMPORTED
#@@QUERY TARGET_PROP root_imported IMPORTED
#@@QUERY TARGET_PROP root_imported IMPORTED_GLOBAL
#@@QUERY TARGET_PROP root_alias ALIASED_TARGET
#@@QUERY TARGET_PROP root_alias ALIAS_GLOBAL
#@@QUERY TARGET_PROP child_local TYPE
#@@QUERY TARGET_PROP child_imported_local IMPORTED
#@@QUERY TARGET_PROP child_imported_local IMPORTED_GLOBAL
#@@QUERY TARGET_PROP child_local_alias ALIASED_TARGET
#@@QUERY TARGET_PROP child_imported_alias ALIAS_GLOBAL
#@@QUERY VAR ROOT_ALIAS_SRC_OK
#@@QUERY VAR ROOT_ALIAS_BIN_OK
#@@QUERY VAR CHILD_SRC_OK
#@@QUERY VAR CHILD_BIN_OK
file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/gtp_sub")
file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/gtp_sub/local.c" "int gtp_local;\n")
file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/gtp_sub/CMakeLists.txt" [=[
add_library(child_local STATIC local.c)
add_library(child_imported_local STATIC IMPORTED)
add_library(child_local_alias ALIAS child_local)
add_library(child_imported_alias ALIAS child_imported_local)
]=])
add_library(root_real STATIC root.c)
add_library(root_imported SHARED IMPORTED GLOBAL)
add_library(root_alias ALIAS root_real)
add_subdirectory(gtp_sub gtp_sub_build)
get_target_property(ROOT_ALIAS_SRC root_alias SOURCE_DIR)
get_target_property(ROOT_ALIAS_BIN root_alias BINARY_DIR)
get_target_property(CHILD_SRC child_local SOURCE_DIR)
get_target_property(CHILD_BIN child_local BINARY_DIR)
if(ROOT_ALIAS_SRC STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
  set(ROOT_ALIAS_SRC_OK 1)
else()
  set(ROOT_ALIAS_SRC_OK 0)
endif()
if(ROOT_ALIAS_BIN STREQUAL "${CMAKE_CURRENT_BINARY_DIR}")
  set(ROOT_ALIAS_BIN_OK 1)
else()
  set(ROOT_ALIAS_BIN_OK 0)
endif()
if(CHILD_SRC STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/gtp_sub")
  set(CHILD_SRC_OK 1)
else()
  set(CHILD_SRC_OK 0)
endif()
if(CHILD_BIN STREQUAL "${CMAKE_CURRENT_BINARY_DIR}/gtp_sub_build")
  set(CHILD_BIN_OK 1)
else()
  set(CHILD_BIN_OK 0)
endif()
#@@ENDCASE

#@@CASE add_subdirectory_directory_qualified_mutation_surface
#@@OUTCOME SUCCESS
#@@QUERY VAR ROOT_PROP
#@@QUERY VAR SUB_PROP
#@@QUERY VAR SUB_DIR_PROP
#@@QUERY VAR SUB_SRC
#@@QUERY VAR SUB_TEST
#@@QUERY VAR SUB_SRC_UPDATED
#@@QUERY VAR SUB_TEST_UPDATED
enable_testing()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/gp_known_dir")
file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/gp_known_dir/sub_file.c" "int sub_file;\n")
file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/gp_known_dir/CMakeLists.txt" [=[
set_property(DIRECTORY PROPERTY SUB_BATCH_PROP from_subdir)
set_property(SOURCE sub_file.c PROPERTY SUB_SRC_PROP from_source)
add_test(NAME sub_batch_test COMMAND "${CMAKE_COMMAND}" -E true)
set_property(TEST sub_batch_test PROPERTY LABELS from_test)
]=])
set_property(DIRECTORY PROPERTY ROOT_BATCH_PROP from_root)
add_subdirectory(gp_known_dir)
get_directory_property(ROOT_PROP ROOT_BATCH_PROP)
get_directory_property(SUB_PROP DIRECTORY gp_known_dir SUB_BATCH_PROP)
get_property(SUB_DIR_PROP DIRECTORY gp_known_dir PROPERTY SUB_BATCH_PROP)
get_property(SUB_SRC SOURCE sub_file.c DIRECTORY gp_known_dir PROPERTY SUB_SRC_PROP)
get_property(SUB_TEST TEST sub_batch_test DIRECTORY gp_known_dir PROPERTY LABELS)
set_property(SOURCE sub_file.c DIRECTORY gp_known_dir PROPERTY SUB_SRC_PROP updated_source)
get_property(SUB_SRC_UPDATED SOURCE sub_file.c DIRECTORY gp_known_dir PROPERTY SUB_SRC_PROP)
set_property(TEST sub_batch_test DIRECTORY gp_known_dir PROPERTY LABELS updated_test)
get_property(SUB_TEST_UPDATED TEST sub_batch_test DIRECTORY gp_known_dir PROPERTY LABELS)
#@@ENDCASE

#@@CASE add_subdirectory_unknown_directory_qualified_query_errors
#@@OUTCOME ERROR
enable_testing()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/gp_known_dir")
file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/gp_known_dir/CMakeLists.txt" "# empty\n")
add_subdirectory(gp_known_dir)
get_directory_property(BAD_DIR DIRECTORY gp_missing_dir ROOT_BATCH_PROP)
#@@ENDCASE
