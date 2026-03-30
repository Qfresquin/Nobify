#@@CASE testing_meta_enable_testing_and_add_test_signatures_surface
#@@OUTCOME SUCCESS
#@@QUERY VAR TEST_HAS_SMOKE
#@@QUERY VAR TEST_HAS_LEGACY
#@@QUERY VAR SMOKE_WD
enable_testing()
add_test(NAME smoke COMMAND app --flag value CONFIGURATIONS Debug RelWithDebInfo WORKING_DIRECTORY tests COMMAND_EXPAND_LISTS)
add_test(legacy app WORKING_DIRECTORY tools)
get_property(TEST_LIST DIRECTORY PROPERTY TESTS)
list(FIND TEST_LIST smoke IDX_SMOKE)
list(FIND TEST_LIST legacy IDX_LEGACY)
if(IDX_SMOKE EQUAL -1)
  set(TEST_HAS_SMOKE 0)
else()
  set(TEST_HAS_SMOKE 1)
endif()
if(IDX_LEGACY EQUAL -1)
  set(TEST_HAS_LEGACY 0)
else()
  set(TEST_HAS_LEGACY 1)
endif()
get_test_property(smoke WORKING_DIRECTORY SMOKE_WD)
#@@ENDCASE

#@@CASE testing_meta_add_test_name_signature_rejects_unexpected_argument
#@@OUTCOME ERROR
enable_testing()
add_test(NAME bad COMMAND app WORKING_DIRECTORY bad_dir EXTRA_TOKEN value)
#@@ENDCASE
