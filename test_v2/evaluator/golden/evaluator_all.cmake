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

#@@CASE vars_unset_local_and_cache_modes
set(U_LOCAL "present")
set(U_CACHE "cached" CACHE STRING "doc")
unset(U_LOCAL)
unset(U_CACHE CACHE)
add_executable(unset_local_cache main.c)
target_compile_definitions(unset_local_cache PRIVATE U_LOCAL=${U_LOCAL} U_CACHE=${U_CACHE})
#@@ENDCASE

#@@CASE vars_unset_parent_scope_mode
set(UP_OUTER "outer")
function(unset_parent_fn)
  set(UP_OUTER "inner" PARENT_SCOPE)
  unset(UP_OUTER PARENT_SCOPE)
endfunction()
unset_parent_fn()
add_executable(unset_parent_scope main.c)
target_compile_definitions(unset_parent_scope PRIVATE UP_OUTER=${UP_OUTER})
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

#@@CASE flow_block_scope_and_propagate
set(BLK_OUTER outer)
cmake_policy(GET CMP0077 POL_BEFORE)
block(SCOPE_FOR VARIABLES POLICIES PROPAGATE BLK_PROP)
  set(BLK_OUTER inner)
  set(BLK_LOCAL local)
  set(BLK_PROP propagated)
  cmake_policy(SET CMP0077 NEW)
  cmake_policy(GET CMP0077 POL_INNER)
  add_executable(block_inner main.c)
  target_compile_definitions(block_inner PRIVATE POL_INNER=${POL_INNER})
endblock()
cmake_policy(GET CMP0077 POL_AFTER)
add_executable(block_outer main.c)
target_compile_definitions(block_outer PRIVATE BLK_OUTER=${BLK_OUTER} BLK_PROP=${BLK_PROP} BLK_LOCAL=${BLK_LOCAL} POL_BEFORE=${POL_BEFORE} POL_AFTER=${POL_AFTER})
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

#@@CASE stdlib_list_mutation_extended
set(L "b;c;c;d")
list(PREPEND L a)
list(INSERT L 2 x y)
list(REMOVE_AT L -1 2)
list(REMOVE_DUPLICATES L)
list(POP_FRONT L PF0 PF1)
list(POP_BACK L PB0)
add_executable(list_mut main.c)
target_compile_definitions(list_mut PRIVATE "L=${L}" PF0=${PF0} PF1=${PF1} PB0=${PB0})
#@@ENDCASE

#@@CASE stdlib_list_join_sublist_extended
set(LJS "aa;bb;cc;dd")
list(JOIN LJS , CSV)
list(SUBLIST LJS 1 2 MID)
list(SUBLIST LJS 2 -1 TAIL)
add_executable(list_join_sub main.c)
target_compile_definitions(list_join_sub PRIVATE "CSV=${CSV}" "MID=${MID}" "TAIL=${TAIL}")
#@@ENDCASE

#@@CASE stdlib_list_filter_regex_extended
set(LF "a1;B2;c3;dd")
list(FILTER LF INCLUDE REGEX "^[a-z][0-9]$")
set(LF2 "a1;B2;c3;dd")
list(FILTER LF2 EXCLUDE REGEX "^[A-Z]")
add_executable(list_filter main.c)
target_compile_definitions(list_filter PRIVATE "LF=${LF}" "LF2=${LF2}")
#@@ENDCASE

#@@CASE stdlib_list_transform_actions_selectors_extended
set(LT "  aa ;bb;cc3;dd4")
list(TRANSFORM LT STRIP)
list(TRANSFORM LT TOUPPER AT 0 1)
list(TRANSFORM LT REPLACE "[0-9]" "_" REGEX "[0-9]")
list(TRANSFORM LT APPEND "_Z" FOR 1 3 2)
list(TRANSFORM LT PREPEND "P_" REGEX "^[A-Z]")
list(TRANSFORM LT TOLOWER)
add_executable(list_transform main.c)
target_compile_definitions(list_transform PRIVATE "LT=${LT}")
#@@ENDCASE

#@@CASE stdlib_list_sort_advanced_extended
set(LS "src/z10.c;src/Z2.c;src/a1.c;src/A02.c")
list(SORT LS COMPARE FILE_BASENAME CASE INSENSITIVE ORDER ASCENDING)
set(NS "v2;v10;v01;v1")
list(SORT NS COMPARE NATURAL CASE SENSITIVE ORDER DESCENDING)
add_executable(list_sort main.c)
target_compile_definitions(list_sort PRIVATE "LS=${LS}" "NS=${NS}")
#@@ENDCASE

#@@CASE stdlib_list_remove_at_invalid_index_error
set(LE "a;b")
list(REMOVE_AT LE 5)
#@@ENDCASE

#@@CASE stdlib_list_filter_invalid_regex_error
set(LE "a;b")
list(FILTER LE INCLUDE REGEX "[")
#@@ENDCASE

#@@CASE stdlib_list_transform_invalid_action_error
set(LE "a;b")
list(TRANSFORM LE BAD_ACTION)
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

#@@CASE stdlib_string_append_prepend_concat_join_extended
set(SV "mid")
string(APPEND SV "_A" "_B")
string(PREPEND SV "P_")
string(CONCAT SCON "x" "-" "y")
string(JOIN ":" SJOIN "aa" "bb" "cc")
add_executable(string_append_prepend main.c)
target_compile_definitions(string_append_prepend PRIVATE SV=${SV} SCON=${SCON} SJOIN=${SJOIN})
#@@ENDCASE

#@@CASE stdlib_string_length_strip_find_compare_extended
string(LENGTH "a b " SLEN)
string(STRIP "  a b  " SSTRIP)
string(FIND "a-b-a" "a" SFIND_FWD)
string(FIND "a-b-a" "a" SFIND_REV REVERSE)
string(FIND "a-b-a" "x" SFIND_NONE)
string(COMPARE LESS "abc" "abd" SCMP_LESS)
string(COMPARE EQUAL "aa" "aa" SCMP_EQ)
add_executable(string_core_ops main.c)
target_compile_definitions(string_core_ops PRIVATE SLEN=${SLEN} SSTRIP=${SSTRIP} SFIND_FWD=${SFIND_FWD} SFIND_REV=${SFIND_REV} SFIND_NONE=${SFIND_NONE} SCMP_LESS=${SCMP_LESS} SCMP_EQ=${SCMP_EQ})
#@@ENDCASE

#@@CASE stdlib_string_regex_matchall_extended
string(REGEX MATCHALL "[0-9]+" SM_ALL "v1-v22-v333")
add_executable(string_matchall main.c)
target_compile_definitions(string_matchall PRIVATE SM_ALL=${SM_ALL})
#@@ENDCASE

#@@CASE stdlib_string_hash_md5_sha1_sha256_extended
string(MD5 H_MD5 "abc")
string(SHA1 H_SHA1 "abc")
string(SHA256 H_SHA256 "abc")
add_executable(string_hashes main.c)
target_compile_definitions(string_hashes PRIVATE H_MD5=${H_MD5} H_SHA1=${H_SHA1} H_SHA256=${H_SHA256})
#@@ENDCASE

#@@CASE stdlib_string_ascii_hex_extended
string(ASCII 65 66 67 S_ASCII)
string(HEX "Ab" S_HEX)
add_executable(string_ascii_hex main.c)
target_compile_definitions(string_ascii_hex PRIVATE S_ASCII=${S_ASCII} S_HEX=${S_HEX})
#@@ENDCASE

#@@CASE stdlib_string_configure_atonly_escape_quotes_extended
set(VAR "x\"y")
string(CONFIGURE "a=@VAR@ b=\\${VAR}" S_CFG1)
string(CONFIGURE "a=@VAR@ b=\\${VAR}" S_CFG2 @ONLY)
string(CONFIGURE "a=@VAR@ b=\\${VAR}" S_CFG3 ESCAPE_QUOTES)
add_executable(string_configure main.c)
target_compile_definitions(string_configure PRIVATE S_CFG1=${S_CFG1} S_CFG2=${S_CFG2} S_CFG3=${S_CFG3})
#@@ENDCASE

#@@CASE stdlib_string_make_c_identifier_extended
string(MAKE_C_IDENTIFIER "9a-b.c" S_ID)
add_executable(string_identifier main.c)
target_compile_definitions(string_identifier PRIVATE S_ID=${S_ID})
#@@ENDCASE

#@@CASE stdlib_string_genex_strip_extended
string(GENEX_STRIP "A$<$<CONFIG:Debug>:_d>B$<1:X>" S_GX)
add_executable(string_genex_strip main.c)
target_compile_definitions(string_genex_strip PRIVATE S_GX=${S_GX})
#@@ENDCASE

#@@CASE stdlib_string_random_seeded_extended
string(RANDOM LENGTH 8 RANDOM_SEED 42 SR1)
string(RANDOM LENGTH 8 RANDOM_SEED 42 SR2)
add_executable(string_random main.c)
target_compile_definitions(string_random PRIVATE SR1=${SR1} SR2=${SR2})
#@@ENDCASE

#@@CASE stdlib_string_timestamp_source_date_epoch_extended
string(TIMESTAMP STS "%Y-%m-%d %H:%M:%S" UTC)
add_executable(string_timestamp main.c)
target_compile_definitions(string_timestamp PRIVATE STS=${STS})
#@@ENDCASE

#@@CASE stdlib_string_uuid_name_based_extended
string(UUID SUUID NAMESPACE "6ba7b810-9dad-11d1-80b4-00c04fd430c8" NAME "abc" TYPE SHA1)
string(UUID SUUID_UP NAMESPACE "6ba7b810-9dad-11d1-80b4-00c04fd430c8" NAME "abc" TYPE SHA1 UPPER)
add_executable(string_uuid main.c)
target_compile_definitions(string_uuid PRIVATE SUUID=${SUUID} SUUID_UP=${SUUID_UP})
#@@ENDCASE

#@@CASE stdlib_string_json_get_type_length_extended
set(SJSON [=[{"k":[1,2,3],"s":"x","n":null}]=])
string(JSON SJ_TYPE TYPE "${SJSON}" k)
string(JSON SJ_LEN LENGTH "${SJSON}" k)
string(JSON SJ_GET_NUM GET "${SJSON}" k 1)
string(JSON SJ_GET_STR GET "${SJSON}" s)
string(JSON SJ_GET_NULL GET "${SJSON}" n)
add_executable(string_json main.c)
target_compile_definitions(string_json PRIVATE SJ_TYPE=${SJ_TYPE} SJ_LEN=${SJ_LEN} SJ_GET_NUM=${SJ_GET_NUM} SJ_GET_STR=${SJ_GET_STR} SJ_GET_NULL=${SJ_GET_NULL})
#@@ENDCASE

#@@CASE stdlib_string_ascii_invalid_code_error
string(ASCII 300 OUT_BAD)
#@@ENDCASE

#@@CASE stdlib_string_json_length_non_container_error
set(SJSON_BAD [=[{"s":"abc"}]=])
string(JSON BADLEN LENGTH "${SJSON_BAD}" s)
#@@ENDCASE

#@@CASE stdlib_string_uuid_malformed_namespace_error
string(UUID BADUUID NAMESPACE DNS NAME abc TYPE SHA1)
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

#@@CASE stdlib_math_overflow_add_reports_error
math(EXPR OV_ADD "9223372036854775807 + 1")
#@@ENDCASE

#@@CASE stdlib_math_overflow_mul_reports_error
math(EXPR OV_MUL "3037000500 * 3037000500")
#@@ENDCASE

#@@CASE stdlib_math_overflow_shift_reports_error
math(EXPR OV_SHL "1 << 64")
#@@ENDCASE

#@@CASE stdlib_math_output_format_extended
math(EXPR M_DEC "15" OUTPUT_FORMAT DECIMAL)
math(EXPR M_HEX "15" OUTPUT_FORMAT HEXADECIMAL)
math(EXPR M_NEG_HEX "-1" OUTPUT_FORMAT HEXADECIMAL)
add_executable(math_format main.c)
target_compile_definitions(math_format PRIVATE M_DEC=${M_DEC} M_HEX=${M_HEX} M_NEG_HEX=${M_NEG_HEX})
#@@ENDCASE

#@@CASE stdlib_math_legacy_output_format_extended
math(EXPR M_LEG_HEX "26" HEXADECIMAL)
math(EXPR M_LEG_DEC "26" DECIMAL)
add_executable(math_format_legacy main.c)
target_compile_definitions(math_format_legacy PRIVATE M_LEG_HEX=${M_LEG_HEX} M_LEG_DEC=${M_LEG_DEC})
#@@ENDCASE

#@@CASE stdlib_math_invalid_output_format_error
math(EXPR BAD_FMT "1+2" OUTPUT_FORMAT BINARY)
#@@ENDCASE

#@@CASE stdlib_math_invalid_subcommand_error
math(FOO OUT "1+2")
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

#@@CASE dispatcher_find_package_no_cmake_path_ignores_cmake_prefix_path
file(MAKE_DIRECTORY temp_pkg_no_cmake_path)
file(WRITE temp_pkg_no_cmake_path/NoCmakeConfig.cmake [=[set(NoCmake_FOUND 1)
]=])
set(CMAKE_PREFIX_PATH temp_pkg_no_cmake_path)
find_package(NoCmake CONFIG QUIET NO_CMAKE_PATH)
find_package(NoCmake CONFIG QUIET)
#@@ENDCASE

#@@CASE dispatcher_find_package_path_controls_still_allow_explicit_paths
file(MAKE_DIRECTORY temp_pkg_path_controls)
file(WRITE temp_pkg_path_controls/PathCtlConfig.cmake [=[set(PathCtl_FOUND 1)
]=])
set(CMAKE_PREFIX_PATH temp_pkg_not_used)
find_package(PathCtl CONFIG QUIET
  NO_CMAKE_PATH
  NO_CMAKE_ENVIRONMENT_PATH
  NO_SYSTEM_ENVIRONMENT_PATH
  NO_CMAKE_SYSTEM_PATH
  NO_CMAKE_INSTALL_PREFIX
  NO_PACKAGE_ROOT_PATH
  NO_CMAKE_PACKAGE_REGISTRY
  NO_CMAKE_SYSTEM_PACKAGE_REGISTRY
  PATHS temp_pkg_path_controls)
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

#@@CASE file_strings_newline_consume_and_encoding_utf8_supported
file(WRITE temp_strings_enc_utf8.txt "line1\nline2\n")
file(STRINGS temp_strings_enc_utf8.txt OUT NEWLINE_CONSUME ENCODING UTF-8)
#@@ENDCASE

#@@CASE file_security_copy_with_permissions_executes_without_legacy_no_effect_warning
file(WRITE temp_copy_perm_src.txt "x")
file(COPY temp_copy_perm_src.txt DESTINATION temp_copy_perm_dst PERMISSIONS OWNER_READ OWNER_WRITE)
#@@ENDCASE

#@@CASE file_copy_follow_symlink_chain_regular_file_no_warning
file(WRITE temp_copy_chain_src.txt "ok")
file(MAKE_DIRECTORY temp_copy_chain_dst)
file(COPY temp_copy_chain_src.txt DESTINATION temp_copy_chain_dst FOLLOW_SYMLINK_CHAIN)
file(READ temp_copy_chain_dst/temp_copy_chain_src.txt COPY_CHAIN_TXT)
add_executable(copy_chain_file main.c)
target_compile_definitions(copy_chain_file PRIVATE COPY_CHAIN_TXT=${COPY_CHAIN_TXT})
#@@ENDCASE

#@@CASE file_append_preserves_existing_content
file(WRITE temp_file_append.txt "A")
file(APPEND temp_file_append.txt "B")
file(READ temp_file_append.txt APP_TXT)
add_executable(file_append_case main.c)
target_compile_definitions(file_append_case PRIVATE APP_TXT=${APP_TXT})
#@@ENDCASE

#@@CASE file_rename_no_replace_and_result
file(WRITE temp_file_rename_src.txt "SRC")
file(WRITE temp_file_rename_dst.txt "DST")
file(RENAME temp_file_rename_src.txt temp_file_rename_dst.txt NO_REPLACE RESULT RN_NO_REPLACE)
file(READ temp_file_rename_src.txt RN_SRC_TXT)
file(READ temp_file_rename_dst.txt RN_DST_TXT)
file(RENAME temp_file_rename_src.txt temp_file_rename_dst2.txt RESULT RN_OK)
file(READ temp_file_rename_dst2.txt RN_DST2_TXT)
add_executable(file_rename_case main.c)
target_compile_definitions(file_rename_case PRIVATE RN_SRC_TXT=${RN_SRC_TXT} RN_DST_TXT=${RN_DST_TXT} RN_DST2_TXT=${RN_DST2_TXT})
#@@ENDCASE

#@@CASE file_remove_and_remove_recurse_non_existing_ok
file(WRITE temp_file_remove_one.txt "X")
file(REMOVE temp_file_remove_one.txt temp_file_remove_missing.txt)
set(FILE_REMOVE_OK 0)
if(NOT EXISTS temp_file_remove_one.txt)
  set(FILE_REMOVE_OK 1)
endif()
file(MAKE_DIRECTORY temp_file_remove_tree/sub)
file(WRITE temp_file_remove_tree/sub/leaf.txt "Y")
file(REMOVE_RECURSE temp_file_remove_tree temp_file_remove_missing_dir)
set(FILE_REMOVE_RECURSE_OK 0)
if(NOT EXISTS temp_file_remove_tree)
  set(FILE_REMOVE_RECURSE_OK 1)
endif()
add_executable(file_remove_case main.c)
target_compile_definitions(file_remove_case PRIVATE FILE_REMOVE_OK=${FILE_REMOVE_OK} FILE_REMOVE_RECURSE_OK=${FILE_REMOVE_RECURSE_OK})
#@@ENDCASE

#@@CASE file_size_and_timestamp_available
file(WRITE temp_file_size.txt "ABCD")
file(SIZE temp_file_size.txt FILE_SIZE_VAL)
file(TIMESTAMP temp_file_size.txt FILE_TS_VAL "%Y" UTC)
set(FILE_TS_OK 0)
if(FILE_TS_VAL)
  set(FILE_TS_OK 1)
endif()
add_executable(file_size_ts_case main.c)
target_compile_definitions(file_size_ts_case PRIVATE FILE_SIZE_VAL=${FILE_SIZE_VAL} FILE_TS_OK=${FILE_TS_OK})
#@@ENDCASE

#@@CASE file_read_symlink_and_create_link
file(WRITE temp_file_link_src.txt "LINKDATA")
file(CREATE_LINK temp_file_link_src.txt temp_file_link_sym.txt SYMBOLIC RESULT FILE_LINK_SYM_RES)
file(READ_SYMLINK temp_file_link_sym.txt FILE_LINK_TARGET)
file(CREATE_LINK temp_file_link_src.txt temp_file_link_hard.txt RESULT FILE_LINK_HARD_RES)
file(READ temp_file_link_hard.txt FILE_LINK_HARD_TXT)
set(FILE_LINK_OK 0)
if(EXISTS temp_file_link_sym.txt AND EXISTS temp_file_link_hard.txt)
  set(FILE_LINK_OK 1)
endif()
add_executable(file_link_case main.c)
target_compile_definitions(file_link_case PRIVATE FILE_LINK_OK=${FILE_LINK_OK} FILE_LINK_TARGET=${FILE_LINK_TARGET} FILE_LINK_HARD_TXT=${FILE_LINK_HARD_TXT})
#@@ENDCASE

#@@CASE file_chmod_and_chmod_recurse_runs
file(WRITE temp_file_chmod.txt "z")
file(CHMOD temp_file_chmod.txt PERMISSIONS OWNER_READ OWNER_WRITE)
file(MAKE_DIRECTORY temp_file_chmod_dir/sub)
file(WRITE temp_file_chmod_dir/sub/a.txt "a")
file(CHMOD_RECURSE temp_file_chmod_dir PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
set(FILE_CHMOD_OK 1)
add_executable(file_chmod_case main.c)
target_compile_definitions(file_chmod_case PRIVATE FILE_CHMOD_OK=${FILE_CHMOD_OK})
#@@ENDCASE

#@@CASE file_real_relative_and_path_conversion
file(MAKE_DIRECTORY temp_file_path/a)
file(WRITE temp_file_path/a/f.txt "p")
file(REAL_PATH temp_file_path/a/f.txt FILE_REAL_OUT)
file(RELATIVE_PATH FILE_REL_OUT temp_file_path temp_file_path/a/f.txt)
file(TO_CMAKE_PATH "a\\b\\c" FILE_CMAKE_PATH)
file(TO_NATIVE_PATH "x/y/z" FILE_NATIVE_PATH)
add_executable(file_path_case main.c)
target_compile_definitions(file_path_case PRIVATE FILE_REL_OUT=${FILE_REL_OUT} FILE_CMAKE_PATH=${FILE_CMAKE_PATH} FILE_NATIVE_PATH=${FILE_NATIVE_PATH})
#@@ENDCASE

#@@CASE file_download_upload_local_backend
file(WRITE temp_file_download_src.txt "0123456789")
file(DOWNLOAD temp_file_download_src.txt temp_file_download_dst.txt RANGE_START 2 RANGE_END 5 STATUS FILE_DL_STATUS LOG FILE_DL_LOG)
file(READ temp_file_download_dst.txt FILE_DL_TXT)
file(UPLOAD temp_file_download_dst.txt temp_file_upload_dst.txt STATUS FILE_UL_STATUS LOG FILE_UL_LOG)
file(READ temp_file_upload_dst.txt FILE_UL_TXT)
add_executable(file_transfer_case main.c)
target_compile_definitions(file_transfer_case PRIVATE FILE_DL_TXT=${FILE_DL_TXT} FILE_UL_TXT=${FILE_UL_TXT})
#@@ENDCASE

#@@CASE file_download_remote_url_reports_error
file(DOWNLOAD "https://example.com/demo.txt" temp_file_remote_out.txt STATUS FILE_REMOTE_STATUS LOG FILE_REMOTE_LOG)
#@@ENDCASE

#@@CASE file_generate_content_input_and_condition
file(WRITE temp_file_generate_input.in "IN_LINE")
file(GENERATE OUTPUT temp_file_generate_content.txt CONTENT "OUT_LINE")
file(GENERATE OUTPUT temp_file_generate_from_input.txt INPUT temp_file_generate_input.in)
file(GENERATE OUTPUT temp_file_generate_skip.txt CONTENT "SKIP" CONDITION 0)
file(READ temp_file_generate_content.txt FILE_GEN_CONTENT)
file(READ temp_file_generate_from_input.txt FILE_GEN_INPUT)
set(FILE_GEN_SKIP_OK 1)
if(EXISTS temp_file_generate_skip.txt)
  set(FILE_GEN_SKIP_OK 0)
endif()
add_executable(file_generate_case main.c)
target_compile_definitions(file_generate_case PRIVATE FILE_GEN_CONTENT=${FILE_GEN_CONTENT} FILE_GEN_INPUT=${FILE_GEN_INPUT} FILE_GEN_SKIP_OK=${FILE_GEN_SKIP_OK})
#@@ENDCASE

#@@CASE file_lock_acquire_release_reacquire
file(LOCK temp_file_lock_guard.lck RESULT_VARIABLE FILE_LOCK_A TIMEOUT 1)
file(LOCK temp_file_lock_guard.lck RELEASE RESULT_VARIABLE FILE_LOCK_B)
file(LOCK temp_file_lock_guard.lck RESULT_VARIABLE FILE_LOCK_C)
add_executable(file_lock_case main.c)
target_compile_definitions(file_lock_case PRIVATE FILE_LOCK_A=${FILE_LOCK_A} FILE_LOCK_B=${FILE_LOCK_B} FILE_LOCK_C=${FILE_LOCK_C})
#@@ENDCASE

#@@CASE file_archive_create_extract_tar_subset
file(MAKE_DIRECTORY temp_file_archive_src)
file(WRITE temp_file_archive_src/item.txt "ARCHIVE_OK")
file(ARCHIVE_CREATE OUTPUT temp_file_archive.tar PATHS temp_file_archive_src FORMAT TAR COMPRESSION NONE)
file(MAKE_DIRECTORY temp_file_archive_out)
file(ARCHIVE_EXTRACT INPUT temp_file_archive.tar DESTINATION temp_file_archive_out)
file(READ temp_file_archive_out/temp_file_archive_src/item.txt FILE_ARCHIVE_TXT)
add_executable(file_archive_case main.c)
target_compile_definitions(file_archive_case PRIVATE FILE_ARCHIVE_TXT=${FILE_ARCHIVE_TXT})
#@@ENDCASE

#@@CASE file_archive_create_unsupported_compression_error
file(MAKE_DIRECTORY temp_file_archive_bad_src)
file(WRITE temp_file_archive_bad_src/item.txt "X")
file(ARCHIVE_CREATE OUTPUT temp_file_archive_bad.tar PATHS temp_file_archive_bad_src FORMAT TAR COMPRESSION GZIP)
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
