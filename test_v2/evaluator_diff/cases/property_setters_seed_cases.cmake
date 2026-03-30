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
