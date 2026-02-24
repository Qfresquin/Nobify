#@@CASE golden_core_flow
project(CoreDemo VERSION 1.0)
set(FOO ON)
if(FOO)
  add_executable(core_app main.c)
  target_compile_definitions(core_app PRIVATE CORE_OK=1)
endif()
#@@ENDCASE

#@@CASE golden_targets_props_events
project(TargetDemo)
add_executable(app main.c util.c)
add_library(lib STATIC lib.c)
target_include_directories(app PRIVATE include)
target_compile_definitions(app PRIVATE APPDEF=1)
target_compile_options(app PRIVATE -Wall)
target_link_options(app PRIVATE -Wl,--as-needed)
target_link_directories(app PRIVATE lib)
target_link_libraries(app PRIVATE lib)
set_target_properties(app PROPERTIES OUTPUT_NAME appx)
#@@ENDCASE

#@@CASE golden_find_package_and_include
project(FindDemo)
set(CMAKE_MODULE_PATH modules)
include_guard(GLOBAL)
find_package(ZLIB QUIET)
add_executable(fp main.c)
#@@ENDCASE

#@@CASE golden_file_ops_and_security
project(FileDemo)
file(WRITE temp_golden_eval.txt hello)
file(READ temp_golden_eval.txt OUT)
add_executable(file_app main.c)
target_compile_definitions(file_app PRIVATE OUT_${OUT})
#@@ENDCASE

#@@CASE golden_cpack_commands
project(CPackDemo VERSION 2.0)
cpack_add_install_type(Full DISPLAY_NAME "Full")
cpack_add_component_group(base DISPLAY_NAME "Base Group" DESCRIPTION "Core Group")
cpack_add_component(core DISPLAY_NAME "Core")
add_executable(pkg_app main.c)
#@@ENDCASE

#@@CASE golden_probes_and_try_compile
cmake_minimum_required(VERSION 3.16)
project(ProbeDemo)
try_compile(HAVE_X ${CMAKE_BINARY_DIR} probe.c)
add_executable(probe_app main.c)
#@@ENDCASE

#@@CASE try_compile_source_from_content_copy_file_and_cache
try_compile(TC_CONTENT_OK tc_build
  SOURCE_FROM_CONTENT tc_generated.c "ok_content"
  OUTPUT_VARIABLE TC_CONTENT_MSG
  COPY_FILE tc_copy_out.txt
  CMAKE_FLAGS -DTC_FLAG:STRING=ready)
file(READ tc_copy_out.txt TC_COPY_TEXT)
add_executable(tc_content main.c)
target_compile_definitions(tc_content PRIVATE TC_CONTENT_OK=${TC_CONTENT_OK} TC_FLAG=${TC_FLAG} TC_CONTENT_MSG=${TC_CONTENT_MSG} TC_COPY_TEXT=${TC_COPY_TEXT})
#@@ENDCASE

#@@CASE try_compile_sources_missing_with_no_cache
try_compile(TC_MISSING tc_build SOURCES missing_a.c missing_b.c OUTPUT_VARIABLE TC_MISSING_MSG NO_CACHE)
add_executable(tc_missing main.c)
target_compile_definitions(tc_missing PRIVATE TC_MISSING=${TC_MISSING} "TC_MISSING_MSG=${TC_MISSING_MSG}")
#@@ENDCASE

#@@CASE try_compile_source_from_var_and_file_with_log_description
file(WRITE tc_real_src.c "from_file_src")
set(TC_VAR_SRC "from_var_src")
try_compile(TC_MIX_OK tc_build
  SOURCE_FROM_VAR from_var.c TC_VAR_SRC
  SOURCE_FROM_FILE from_file.c tc_real_src.c
  OUTPUT_VARIABLE TC_MIX_MSG
  LOG_DESCRIPTION mix_log_ok
  COPY_FILE tc_mix_copy.txt
  COPY_FILE_ERROR TC_COPY_ERR
  NO_CACHE)
file(READ tc_mix_copy.txt TC_MIX_COPY_TXT)
add_executable(tc_mix main.c)
target_compile_definitions(tc_mix PRIVATE TC_MIX_OK=${TC_MIX_OK} TC_MIX_MSG=${TC_MIX_MSG} TC_COPY_ERR=${TC_COPY_ERR} TC_MIX_COPY_TXT=${TC_MIX_COPY_TXT})
#@@ENDCASE

#@@CASE try_compile_project_signature_success_and_failure
file(MAKE_DIRECTORY tc_proj_ok)
file(WRITE tc_proj_ok/CMakeLists.txt "project(TryProj)")
try_compile(TC_PROJ_OK PROJECT TryProj SOURCE_DIR tc_proj_ok BINARY_DIR tc_proj_bin OUTPUT_VARIABLE TC_PROJ_MSG CMAKE_FLAGS -DTC_PROJ_FLAG:BOOL=ON)
file(MAKE_DIRECTORY tc_proj_fail)
try_compile(TC_PROJ_FAIL PROJECT TryProjFail SOURCE_DIR tc_proj_fail OUTPUT_VARIABLE TC_PROJ_FAIL_MSG NO_CACHE)
add_executable(tc_proj main.c)
target_compile_definitions(tc_proj PRIVATE TC_PROJ_OK=${TC_PROJ_OK} TC_PROJ_MSG=${TC_PROJ_MSG} TC_PROJ_FLAG=${TC_PROJ_FLAG} TC_PROJ_FAIL=${TC_PROJ_FAIL} "TC_PROJ_FAIL_MSG=${TC_PROJ_FAIL_MSG}")
#@@ENDCASE

#@@CASE golden_ctest_meta
project(CTestDemo)
enable_testing()
add_test(NAME smoke COMMAND app)
add_test(legacy app)
add_executable(app main.c)
install(TARGETS app DESTINATION bin)
install(FILES readme.txt DESTINATION share/doc)
install(PROGRAMS run.sh DESTINATION bin)
install(DIRECTORY assets DESTINATION share/assets)
#@@ENDCASE

#@@CASE golden_misc_path_and_property
project(MiscDemo)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
cmake_path(SET P NORMALIZE "a/./b/../c")
add_executable(misc_app main.c)
#@@ENDCASE

#@@CASE expr_variable_expansion_and_if_ops
set(FOO abc)
set(MYLIST "b;a;c")
if(a IN_LIST MYLIST)
  set(IN_LIST_OK 1)
endif()
if("a\\b" PATH_EQUAL "a/b")
  set(PATH_EQ_OK 1)
endif()
#@@ENDCASE

#@@CASE expr_parenthesized_if_condition
set(A ON)
set(B ON)
if((A AND B) OR C)
  set(CONDITION_OK 1)
endif()
#@@ENDCASE

#@@CASE flow_while_simple_counter
set(C 0)
while(${C} LESS 3)
  math(EXPR C "${C}+1")
endwhile()
add_executable(loop_simple main.c)
target_compile_definitions(loop_simple PRIVATE SIMPLE_C=${C})
#@@ENDCASE

#@@CASE flow_while_break_and_continue
set(I 0)
set(OUT "")
while(${I} LESS 5)
  if(${I} EQUAL 1)
    math(EXPR I "${I}+1")
    continue()
  endif()
  if(${I} EQUAL 3)
    break()
  endif()
  if("${OUT}" STREQUAL "")
    set(OUT "${I}")
  else()
    set(OUT "${OUT}_${I}")
  endif()
  math(EXPR I "${I}+1")
endwhile()
add_executable(loop_flow main.c)
target_compile_definitions(loop_flow PRIVATE LOOP_OUT=${OUT})
#@@ENDCASE

#@@CASE flow_while_nested_break_continue
set(O 0)
set(ACC 0)
while(${O} LESS 2)
  set(I 0)
  while(${I} LESS 4)
    math(EXPR I "${I}+1")
    if(${I} EQUAL 2)
      continue()
    endif()
    if(${I} EQUAL 4)
      break()
    endif()
    math(EXPR ACC "${ACC}+1")
  endwhile()
  math(EXPR O "${O}+1")
endwhile()
add_executable(loop_nested main.c)
target_compile_definitions(loop_nested PRIVATE NESTED_ACC=${ACC})
#@@ENDCASE

#@@CASE usercmd_function_scope_and_parent_scope
set(FN_GLOBAL 10)
function(fn_apply value)
  set(FN_GLOBAL "${value}")
  set(FN_LOCAL "${value}")
  set(FN_PARENT "${value}" PARENT_SCOPE)
endfunction()
fn_apply(42)
add_executable(fn_scope main.c)
target_compile_definitions(fn_scope PRIVATE FN_GLOBAL=${FN_GLOBAL} FN_PARENT=${FN_PARENT})
#@@ENDCASE

#@@CASE usercmd_macro_args_and_caller_scope
macro(mc_apply value)
  set(MC_OUT "${value}")
  set(MC_ARGC "${ARGC}")
  set(MC_ARGV0 "${ARGV0}")
  set(MC_ARGV1 "${ARGV1}")
endmacro()
mc_apply(alpha beta)
add_executable(macro_scope main.c)
target_compile_definitions(macro_scope PRIVATE MC_OUT=${MC_OUT} MC_ARGC=${MC_ARGC} MC_ARGV0=${MC_ARGV0} MC_ARGV1=${MC_ARGV1})
#@@ENDCASE

#@@CASE usercmd_nested_macro_calls_function
function(fn_inner out value)
  set(${out} "F_${value}" PARENT_SCOPE)
endfunction()
macro(mc_wrap out value)
  fn_inner(${out} "${value}")
endmacro()
mc_wrap(NESTED_VAL zed)
add_executable(nested_usercmd main.c)
target_compile_definitions(nested_usercmd PRIVATE NESTED_VAL=${NESTED_VAL})
#@@ENDCASE

#@@CASE stdlib_list_get_find_extended
set(LST "aa;bb;cc;dd")
list(GET LST 1 -1 PICKS)
list(FIND LST cc IDX_CC)
list(FIND LST zz IDX_ZZ)
add_executable(list_ext main.c)
target_compile_definitions(list_ext PRIVATE "PICKS=${PICKS}" IDX_CC=${IDX_CC} IDX_ZZ=${IDX_ZZ})
#@@ENDCASE

#@@CASE stdlib_string_tolower_substring_extended
string(TOLOWER "AbC-XyZ" STR_LOW)
string(SUBSTRING "${STR_LOW}" 2 3 STR_SUB)
string(SUBSTRING "${STR_LOW}" 4 -1 STR_TAIL)
add_executable(string_ext main.c)
target_compile_definitions(string_ext PRIVATE STR_LOW=${STR_LOW} STR_SUB=${STR_SUB} STR_TAIL=${STR_TAIL})
#@@ENDCASE

#@@CASE stdlib_string_regex_replace_extended
string(REGEX REPLACE "[0-9]+" "N" STR_RX "v1-v22-v333")
string(REGEX REPLACE "v([0-9]+)" "x\\1" STR_RX_BREF "v7")
add_executable(string_regex_ext main.c)
target_compile_definitions(string_regex_ext PRIVATE STR_RX=${STR_RX} STR_RX_BREF=${STR_RX_BREF})
#@@ENDCASE

#@@CASE stdlib_math_bitwise_and_precedence
math(EXPR M_SHL "1 << 2")
math(EXPR M_SHR "8 >> 1")
math(EXPR M_AND "3 & 1")
math(EXPR M_XOR "6 ^ 3")
math(EXPR M_OR "1 | 2")
math(EXPR M_PAREN "(1+2)*3")
math(EXPR M_PREC "1 + 2 * 3 << 1 & 14 | 1")
math(EXPR M_UNARY "~1")
math(EXPR M_DIV "8/3")
math(EXPR M_MOD "7%4")
add_executable(math_ext main.c)
target_compile_definitions(math_ext PRIVATE M_SHL=${M_SHL} M_SHR=${M_SHR} M_AND=${M_AND} M_XOR=${M_XOR} M_OR=${M_OR} M_PAREN=${M_PAREN} M_PREC=${M_PREC} M_UNARY=${M_UNARY} M_DIV=${M_DIV} M_MOD=${M_MOD})
#@@ENDCASE

#@@CASE dispatcher_custom_command_multiple_commands
add_custom_command(OUTPUT multi.c
  COMMAND echo first
  COMMAND echo second
  DEPENDS in.txt
  BYPRODUCTS multi.log)
add_executable(custom_multi main.c)
#@@ENDCASE

#@@CASE dispatcher_command_handlers
add_definitions(-DLEGACY=1 -fPIC)
add_compile_options(-Wall)
add_link_options(-Wl,--gc-sections)
link_libraries(m pthread)
include_directories(SYSTEM include)
link_directories(BEFORE lib)
add_executable(app main.c)
target_include_directories(app PRIVATE include)
target_compile_definitions(app PRIVATE APPDEF=1)
target_compile_options(app PRIVATE -Wextra)
#@@ENDCASE

#@@CASE dispatcher_cmake_minimum_required
cmake_minimum_required(VERSION 3.16...3.29)
#@@ENDCASE

#@@CASE dispatcher_cmake_policy_set_get_roundtrip
cmake_policy(SET CMP0077 NEW)
cmake_policy(GET CMP0077 OUT_VAR)
#@@ENDCASE

#@@CASE dispatcher_find_package_handler_module_mode
file(MAKE_DIRECTORY temp_pkg/CMake)
file(WRITE temp_pkg/CMake/FindDemoPkg.cmake [=[set(DemoPkg_FOUND 1)
set(DemoPkg_VERSION 9.1)
]=])
set(CMAKE_MODULE_PATH temp_pkg/CMake)
find_package(DemoPkg MODULE REQUIRED)
#@@ENDCASE

#@@CASE dispatcher_find_package_preserves_script_defined_found_state
file(MAKE_DIRECTORY temp_pkg2/CMake)
file(WRITE temp_pkg2/CMake/FindDemoPkg2.cmake [=[set(DemoPkg2_FOUND 0)
]=])
set(CMAKE_MODULE_PATH temp_pkg2/CMake)
find_package(DemoPkg2 MODULE QUIET)
#@@ENDCASE

#@@CASE dispatcher_find_package_config_components_and_version
file(MAKE_DIRECTORY temp_pkg_cfg)
file(WRITE temp_pkg_cfg/DemoCfgConfig.cmake [=[if("${DemoCfg_FIND_COMPONENTS}" STREQUAL "Core;Net")
  set(DemoCfg_FOUND 1)
else()
  set(DemoCfg_FOUND 0)
endif()
set(DemoCfg_VERSION 1.2.0)
]=])
set(CMAKE_PREFIX_PATH temp_pkg_cfg)
find_package(DemoCfg 1.0 CONFIG COMPONENTS Core Net QUIET)
set(CMAKE_PREFIX_PATH temp_pkg_cfg)
find_package(DemoCfg 2.0 EXACT CONFIG QUIET)
#@@ENDCASE

#@@CASE dispatcher_find_package_config_version_file_can_reject
file(MAKE_DIRECTORY temp_pkg_cfgver)
file(WRITE temp_pkg_cfgver/DemoVerConfig.cmake [=[set(DemoVer_FOUND 1)
set(DemoVer_VERSION 9.9.9)
]=])
file(WRITE temp_pkg_cfgver/DemoVerConfigVersion.cmake [=[set(PACKAGE_VERSION 9.9.9)
set(PACKAGE_VERSION_COMPATIBLE FALSE)
set(PACKAGE_VERSION_EXACT FALSE)
]=])
set(CMAKE_PREFIX_PATH temp_pkg_cfgver)
find_package(DemoVer 1.0 CONFIG QUIET)
#@@ENDCASE

#@@CASE dispatcher_set_target_properties_preserves_genex_semicolon_unquoted
add_executable(t main.c)
set_target_properties(t PROPERTIES MY_PROP $<$<CONFIG:Debug>:A;B>)
#@@ENDCASE

#@@CASE dispatcher_set_property_target_ops_emit_expected_event_op
add_executable(t main.c)
set_property(TARGET t APPEND PROPERTY COMPILE_OPTIONS $<$<CONFIG:Debug>:-g>)
set_property(TARGET t APPEND_STRING PROPERTY SUFFIX $<$<CONFIG:Debug>:_d>)
#@@ENDCASE

#@@CASE dispatcher_set_property_non_target_scopes_apply
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS -Wall)
set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS -Wextra)
set_property(DIRECTORY PROPERTY COMPILE_DEFINITIONS DIR_DEF=1)
set_property(CACHE CACHED_X PROPERTY VALUE cache_ok)
add_executable(non_target_prop main.c)
target_compile_definitions(non_target_prop PRIVATE CACHED_X=${CACHED_X})
#@@ENDCASE

#@@CASE file_security_read_rejects_absolute_outside_project_scope
file(READ /tmp/nobify_forbidden OUT)
#@@ENDCASE

#@@CASE file_security_strings_rejects_absolute_outside_project_scope
file(STRINGS /tmp/nobify_forbidden OUT)
#@@ENDCASE

#@@CASE file_security_read_rejects_symlink_escape_outside_project_scope
file(READ temp_symlink_escape_link/outside.txt OUT)
#@@ENDCASE

#@@CASE file_security_strings_rejects_symlink_escape_outside_project_scope
file(STRINGS temp_symlink_escape_link/outside.txt OUT)
#@@ENDCASE

#@@CASE file_security_read_relative_inside_project_scope_still_works
file(WRITE temp_read_ok.txt "hello\n")
file(MAKE_DIRECTORY temp_read_win_nested\\a\\b\\c)
file(READ temp_read_ok.txt OUT)
#@@ENDCASE

#@@CASE file_security_copy_with_permissions_executes_without_legacy_no_effect_warning
file(WRITE temp_copy_perm_src.txt "x")
file(COPY temp_copy_perm_src.txt DESTINATION temp_copy_perm_dst PERMISSIONS OWNER_READ OWNER_WRITE)
#@@ENDCASE

#@@CASE dispatcher_custom_command_and_target
add_custom_target(gen ALL
  COMMAND echo gen
  DEPENDS seed.txt
  BYPRODUCTS out.txt
  WORKING_DIRECTORY tools
  COMMENT "gen")
add_custom_command(TARGET gen POST_BUILD
  COMMAND echo post
  BYPRODUCTS post.txt
  DEPENDS dep1)
add_custom_command(OUTPUT generated.c generated.h
  COMMAND python gen.py
  DEPENDS schema.idl
  BYPRODUCTS gen.log
  MAIN_DEPENDENCY schema.idl
  DEPFILE gen.d)
#@@ENDCASE
