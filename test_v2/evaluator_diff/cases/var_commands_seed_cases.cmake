#@@CASE var_commands_env_set_and_unset_are_observable
#@@OUTCOME SUCCESS
#@@QUERY VAR ENV_A_EMPTY
#@@QUERY VAR ENV_B_SET
#@@QUERY VAR ENV_B_UNSET
set(ENV{NOBIFY_DIFF_ENV_A} valueA)
set(ENV{NOBIFY_DIFF_ENV_A})
set(ENV_A_EMPTY "$ENV{NOBIFY_DIFF_ENV_A}")
set(ENV{NOBIFY_DIFF_ENV_B} valueB ignored)
set(ENV_B_SET "$ENV{NOBIFY_DIFF_ENV_B}")
unset(ENV{NOBIFY_DIFF_ENV_B})
set(ENV_B_UNSET "$ENV{NOBIFY_DIFF_ENV_B}")
#@@ENDCASE

#@@CASE var_commands_cache_cmp0126_old_and_new_bindings
#@@OUTCOME SUCCESS
#@@QUERY VAR OLD_BINDING
#@@QUERY VAR NEW_BINDING
#@@QUERY CACHE_DEFINED CACHE_OLD
#@@QUERY CACHE_DEFINED CACHE_NEW
set(CACHE_OLD local_old)
cmake_policy(SET CMP0126 OLD)
set(CACHE_OLD cache_old CACHE STRING "doc")
set(OLD_BINDING "${CACHE_OLD}")
cmake_policy(SET CMP0126 NEW)
set(CACHE_NEW local_new)
set(CACHE_NEW cache_new CACHE STRING "doc" FORCE)
set(NEW_BINDING "${CACHE_NEW}")
#@@ENDCASE

#@@CASE var_commands_option_and_mark_as_advanced_follow_policies
#@@OUTCOME SUCCESS
#@@QUERY VAR OPT_OLD_VAL
#@@QUERY VAR OPT_NEW_VAL
#@@QUERY VAR OPT_ADV
#@@QUERY CACHE_DEFINED OPT_OLD
#@@QUERY CACHE_DEFINED OLD_MISSING
set(OPT_OLD normal_old)
cmake_policy(SET CMP0077 OLD)
option(OPT_OLD "old doc" ON)
set(OPT_NEW normal_new)
cmake_policy(SET CMP0077 NEW)
option(OPT_NEW "new doc" OFF)
cmake_policy(SET CMP0102 OLD)
mark_as_advanced(FORCE OLD_MISSING)
cmake_policy(SET CMP0102 NEW)
mark_as_advanced(FORCE NEW_MISSING)
mark_as_advanced(FORCE OPT_OLD)
mark_as_advanced(CLEAR OPT_OLD)
include_regular_expression(^keep$ ^warn$)
get_property(OPT_ADV CACHE OPT_OLD PROPERTY ADVANCED)
set(OPT_OLD_VAL "${OPT_OLD}")
set(OPT_NEW_VAL "${OPT_NEW}")
#@@ENDCASE

#@@CASE var_commands_invalid_option_shape_errors
#@@OUTCOME ERROR
option()
#@@ENDCASE
