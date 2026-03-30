#@@CASE property_setters_target_directory_source_test_and_cache_surface
#@@OUTCOME SUCCESS
#@@FILE main.c
#@@QUERY TARGET_PROP real OUTPUT_NAME
#@@QUERY DIR_PROP . DIR_LIST
#@@QUERY DIR_PROP . DIR_TEXT
#@@QUERY DIR_PROP . DIR_WRAP
#@@QUERY VAR SRC_FLAG_OUT
#@@QUERY VAR TEST_LABELS_OUT
#@@QUERY VAR CACHED_VALUE_OUT
enable_testing()
add_library(real STATIC main.c)
add_test(NAME smoke COMMAND "${CMAKE_COMMAND}" -E true)
set_target_properties(real PROPERTIES OUTPUT_NAME renamed)
set_property(TARGET real PROPERTY OUTPUT_NAME renamed2)
set_property(DIRECTORY PROPERTY DIR_LIST one)
set_property(DIRECTORY APPEND PROPERTY DIR_LIST two)
set_property(DIRECTORY PROPERTY DIR_TEXT alpha)
set_property(DIRECTORY APPEND_STRING PROPERTY DIR_TEXT -beta)
set_directory_properties(PROPERTIES DIR_WRAP wrapped)
set_source_files_properties(main.c PROPERTIES SRC_FLAG local)
set_property(SOURCE main.c APPEND_STRING PROPERTY SRC_FLAG _tail)
set_tests_properties(smoke PROPERTIES LABELS fast)
set_property(TEST smoke APPEND PROPERTY LABELS slow)
set(CACHED_X old CACHE STRING "doc")
set_property(CACHE CACHED_X PROPERTY VALUE new_ok)
get_source_file_property(SRC_FLAG_OUT main.c SRC_FLAG)
get_test_property(smoke LABELS TEST_LABELS_OUT)
get_property(CACHED_VALUE_OUT CACHE CACHED_X PROPERTY VALUE)
#@@ENDCASE

#@@CASE property_setters_alias_target_is_rejected
#@@OUTCOME ERROR
#@@FILE main.c
add_library(real STATIC main.c)
add_library(alias_real ALIAS real)
set_target_properties(alias_real PROPERTIES OUTPUT_NAME bad_alias)
#@@ENDCASE

#@@CASE property_setters_missing_cache_entry_is_rejected
#@@OUTCOME ERROR
set_property(CACHE MISSING_X PROPERTY VALUE bad)
#@@ENDCASE

#@@CASE property_setters_binary_dir_scoped_directory_source_and_test_surface
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/gp_bin_src/CMakeLists.txt
set_property(DIRECTORY PROPERTY BIN_DIR_PROP from_child)
set_source_files_properties(sub_file.c PROPERTIES BIN_SRC_PROP from_source)
add_test(NAME gp_bin_test COMMAND "${CMAKE_COMMAND}" -E true)
set_tests_properties(gp_bin_test PROPERTIES LABELS from_test)
#@@END_FILE_TEXT
#@@FILE source/gp_bin_src/sub_file.c
#@@QUERY VAR DIR_UPDATED
#@@QUERY VAR SRC_UPDATED
#@@QUERY VAR TEST_UPDATED
#@@QUERY VAR SRC_WRAP_UPDATED
#@@QUERY VAR TEST_WRAP_UPDATED
enable_testing()
add_subdirectory(gp_bin_src gp_bin_build)
set(CHILD_BIN_DIR "${CMAKE_CURRENT_BINARY_DIR}/gp_bin_build")
set_property(DIRECTORY "${CHILD_BIN_DIR}" PROPERTY BIN_DIR_PROP updated_dir)
set_property(SOURCE sub_file.c DIRECTORY "${CHILD_BIN_DIR}" PROPERTY BIN_SRC_PROP updated_source)
set_property(TEST gp_bin_test DIRECTORY "${CHILD_BIN_DIR}" PROPERTY LABELS updated_test)
get_property(DIR_UPDATED DIRECTORY "${CHILD_BIN_DIR}" PROPERTY BIN_DIR_PROP)
get_property(SRC_UPDATED SOURCE sub_file.c DIRECTORY "${CHILD_BIN_DIR}" PROPERTY BIN_SRC_PROP)
get_property(TEST_UPDATED TEST gp_bin_test DIRECTORY "${CHILD_BIN_DIR}" PROPERTY LABELS)
get_source_file_property(SRC_WRAP_UPDATED sub_file.c DIRECTORY "${CHILD_BIN_DIR}" BIN_SRC_PROP)
get_test_property(gp_bin_test LABELS DIRECTORY "${CHILD_BIN_DIR}" TEST_WRAP_UPDATED)
#@@ENDCASE
