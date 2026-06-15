#@@DIVERGENCE_KEY math.overflow_wrap_and_literal_08
#@@CASE math_overflow_wrap_diverges
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@QUERY VAR M_WRAP
math(EXPR M_WRAP "9223372036854775807 + 1")
#@@ENDCASE

#@@DIVERGENCE_KEY math.overflow_wrap_and_literal_08
#@@CASE math_literal_08_diverges
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@QUERY VAR M_08
math(EXPR M_08 "08")
#@@ENDCASE

#@@DIVERGENCE_KEY block.propagate_unset
#@@CASE block_propagate_unset_diverges
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@QUERY VAR BLOCK_VAR
set(BLOCK_VAR parent)
block(PROPAGATE BLOCK_VAR)
  unset(BLOCK_VAR)
endblock()
#@@ENDCASE

#@@DIVERGENCE_KEY return.propagate_unset
#@@CASE return_propagate_unset_diverges
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@QUERY VAR RET_VAR
cmake_policy(SET CMP0140 NEW)
function(nob_ret_unset)
  unset(RET_VAR)
  return(PROPAGATE RET_VAR)
endfunction()
set(RET_VAR parent)
nob_ret_unset()
#@@ENDCASE

#@@DIVERGENCE_KEY exec_program.args_multi_token
#@@CASE exec_program_args_multi_token_diverges
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@QUERY VAR EP_OUT
#@@QUERY VAR EP_RES
cmake_policy(SET CMP0153 OLD)
exec_program("${CMAKE_COMMAND}" "${CMAKE_CURRENT_BINARY_DIR}" ARGS -E echo one two OUTPUT_VARIABLE EP_OUT RETURN_VALUE EP_RES)
#@@ENDCASE

#@@DIVERGENCE_KEY string.random_seed
#@@CASE string_random_seed_diverges
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@QUERY VAR RANDOM_OUT
string(RANDOM LENGTH 6 ALPHABET abc RANDOM_SEED 7 RANDOM_OUT)
#@@ENDCASE

#@@DIVERGENCE_KEY build_command.order_quoting
#@@CASE build_command_format_diverges
#@@OUTCOME SUCCESS
#@@QUERY VAR BUILD_CMD_OUT
build_command(BUILD_CMD_OUT CONFIGURATION Debug TARGET demo PARALLEL_LEVEL 3)
#@@ENDCASE

#@@DIVERGENCE_KEY set_property.cache_invalid_property
#@@CASE set_property_cache_invalid_property_diverges
#@@OUTCOME ERROR
set(CACHED_X value CACHE STRING "doc")
set_property(CACHE CACHED_X PROPERTY FOO bar)
#@@ENDCASE

#@@DIVERGENCE_KEY file.create_link_result_surface
#@@CASE file_create_link_result_surface_diverges
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@QUERY VAR LINK_RESULT
file(CREATE_LINK "${CMAKE_CURRENT_BINARY_DIR}/missing-input.txt" "${CMAKE_CURRENT_BINARY_DIR}/missing-link.txt" RESULT LINK_RESULT)
#@@ENDCASE

#@@DIVERGENCE_KEY add_custom_command.commandless_output_depends
#@@CASE add_custom_command_output_depends_without_command_diverges
#@@OUTCOME SUCCESS
#@@FILE input.txt
add_custom_command(OUTPUT generated.txt DEPENDS input.txt)
#@@ENDCASE

#@@DIVERGENCE_KEY add_subdirectory.extra_args_error
#@@CASE add_subdirectory_extra_arg_rejection_diverges
#@@OUTCOME ERROR
#@@FILE_TEXT child_extra_arg/CMakeLists.txt
# child stays intentionally empty
#@@END_FILE_TEXT
add_subdirectory(child_extra_arg child_extra_arg_build EXCLUDE_FROM_ALL unexpected)
#@@ENDCASE

#@@DIVERGENCE_KEY add_test.duplicate_name
#@@CASE add_test_duplicate_name_diverges
#@@OUTCOME ERROR
enable_testing()
add_test(NAME duplicate_case COMMAND "${CMAKE_COMMAND}" -E true)
add_test(NAME duplicate_case COMMAND "${CMAKE_COMMAND}" -E true)
#@@ENDCASE

#@@DIVERGENCE_KEY source_group.tree_regex_surface
#@@CASE source_group_tree_missing_base_diverges
#@@OUTCOME ERROR
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/missing_root" FILES "${CMAKE_CURRENT_SOURCE_DIR}/missing_root/a.c")
#@@ENDCASE

#@@DIVERGENCE_KEY target_link_libraries.link_interface_surface
#@@CASE target_link_libraries_interface_keyword_surface_diverges
#@@OUTCOME SUCCESS
#@@FILE usage.c
#@@QUERY TARGET_PROP usage LINK_LIBRARIES
#@@QUERY TARGET_PROP usage INTERFACE_LINK_LIBRARIES
add_library(usage STATIC usage.c)
target_link_libraries(usage PRIVATE local PUBLIC pub INTERFACE iface)
#@@ENDCASE

#@@DIVERGENCE_KEY try_compile.generated_probe_execution
#@@CASE try_compile_missing_source_probe_diverges
#@@OUTCOME ERROR
try_compile(TC_OK "${CMAKE_CURRENT_BINARY_DIR}/tc-build" "${CMAKE_CURRENT_SOURCE_DIR}/missing_probe.c")
#@@ENDCASE

#@@DIVERGENCE_KEY unset.parent_scope_warning
#@@CASE unset_parent_scope_top_level_warning_diverges
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@QUERY VAR TOP_UNSET
set(TOP_UNSET value)
unset(TOP_UNSET PARENT_SCOPE)
#@@ENDCASE

#@@DIVERGENCE_KEY aux_source_directory.relative_output_spelling
#@@CASE aux_source_directory_relative_spelling_diverges
#@@OUTCOME SUCCESS
#@@FILE_TEXT asd_src/a.c
int a = 0;
#@@END_FILE_TEXT
#@@QUERY VAR ASD_REL_OUT
aux_source_directory(asd_src ASD_REL_OUT)
#@@ENDCASE

#@@DIVERGENCE_KEY create_test_sourcelist.generated_driver_contract
#@@CASE create_test_sourcelist_generated_driver_content_diverges
#@@OUTCOME SUCCESS
#@@FILE_TEXT alpha_test.c
int alpha_test(int argc, char **argv) { return argc > 0 && argv ? 0 : 1; }
#@@END_FILE_TEXT
#@@QUERY FILE_TEXT generated_driver.c
create_test_sourcelist(TEST_SRCS generated_driver.c alpha_test.c FUNCTION setup_hook)
#@@ENDCASE

#@@DIVERGENCE_KEY define_property.initialize_from_variable_contract
#@@CASE define_property_initialize_from_variable_diverges
#@@OUTCOME SUCCESS
#@@QUERY TARGET_PROP prop_target CUSTOM_PROP
set(CUSTOM_PROP_INIT seeded)
define_property(TARGET PROPERTY CUSTOM_PROP INITIALIZE_FROM_VARIABLE CUSTOM_PROP_INIT)
add_custom_target(prop_target)
get_target_property(_CUSTOM_PROP_VALUE prop_target CUSTOM_PROP)
#@@ENDCASE

#@@DIVERGENCE_KEY enable_language.optional_keyword
#@@CASE enable_language_optional_keyword_diverges
#@@OUTCOME SUCCESS
enable_language(C OPTIONAL)
#@@ENDCASE

#@@DIVERGENCE_KEY export_library_dependencies.typed_semantics_missing
#@@CASE export_library_dependencies_legacy_command_diverges
#@@OUTCOME ERROR
export_library_dependencies("${CMAKE_CURRENT_BINARY_DIR}/deps.cmake")
#@@ENDCASE

#@@DIVERGENCE_KEY install_files.regexp_extension
#@@CASE install_files_regexp_extension_diverges
#@@OUTCOME SUCCESS
#@@FILE a.h
#@@FILE b.txt
install_files(/include "\\.h$")
#@@ENDCASE

#@@DIVERGENCE_KEY install_targets.runtime_directory
#@@CASE install_targets_runtime_directory_diverges
#@@OUTCOME ERROR
#@@FILE app.c
add_executable(app app.c)
install_targets(/bin RUNTIME_DIRECTORY /runtime app)
#@@ENDCASE

#@@DIVERGENCE_KEY load_command.external_command_registration_missing
#@@CASE load_command_external_registration_diverges
#@@OUTCOME ERROR
load_command(nob_external_command "${CMAKE_CURRENT_BINARY_DIR}")
#@@ENDCASE

#@@DIVERGENCE_KEY output_required_files.typed_semantics_missing
#@@CASE output_required_files_legacy_command_diverges
#@@OUTCOME ERROR
#@@FILE main.c
output_required_files(main.c "${CMAKE_CURRENT_BINARY_DIR}/required.txt")
#@@ENDCASE

#@@DIVERGENCE_KEY set_source_files_properties.directory_scope_validation
#@@CASE set_source_files_properties_directory_scope_diverges
#@@OUTCOME ERROR
set_source_files_properties(missing.c DIRECTORY missing_dir PROPERTIES GENERATED TRUE)
#@@ENDCASE

#@@DIVERGENCE_KEY subdir_depends.typed_semantics_missing
#@@CASE subdir_depends_legacy_command_diverges
#@@OUTCOME ERROR
subdir_depends(src dep)
#@@ENDCASE

#@@DIVERGENCE_KEY target_precompile_headers.reuse_from_generate_failures
#@@CASE target_precompile_headers_reuse_from_diverges
#@@OUTCOME ERROR
#@@FILE usage.c
#@@FILE other.c
#@@FILE include/pch.h
add_library(base STATIC usage.c)
add_library(other STATIC other.c)
target_precompile_headers(other REUSE_FROM base)
#@@ENDCASE

#@@DIVERGENCE_KEY target_sources.empty_file_set
#@@CASE target_sources_empty_file_set_diverges
#@@OUTCOME ERROR
#@@FILE usage.c
add_library(usage STATIC usage.c)
target_sources(usage PUBLIC FILE_SET HEADERS)
#@@ENDCASE

#@@DIVERGENCE_KEY use_mangled_mesa.typed_semantics_missing
#@@CASE use_mangled_mesa_legacy_command_diverges
#@@OUTCOME ERROR
use_mangled_mesa("${CMAKE_CURRENT_BINARY_DIR}/mesa")
#@@ENDCASE

#@@DIVERGENCE_KEY utility_source.typed_semantics_missing
#@@CASE utility_source_legacy_command_diverges
#@@OUTCOME ERROR
utility_source(cache_entry executable_name path/to/source.c)
#@@ENDCASE

#@@DIVERGENCE_KEY variable_watch.callback_invocation_missing
#@@CASE variable_watch_callback_invocation_diverges
#@@OUTCOME SUCCESS
#@@QUERY VAR WATCH_HIT
function(watch_cb var access value current_list_file stack)
  set(WATCH_HIT "${access}:${value}" PARENT_SCOPE)
endfunction()
variable_watch(WATCHED_VAR watch_cb)
set(WATCHED_VAR value)
#@@ENDCASE
