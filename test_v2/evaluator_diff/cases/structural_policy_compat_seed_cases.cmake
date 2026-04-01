#@@CASE return_cmp0140_policy_surface
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@QUERY VAR RET_OLD
#@@QUERY VAR RET_NEW
set(RET_OLD root_old)
set(RET_NEW root_new)
cmake_policy(SET CMP0140 OLD)
function(diff_ret_old)
  set(RET_OLD changed_old)
  return(PROPAGATE RET_OLD)
endfunction()
diff_ret_old()
cmake_policy(SET CMP0140 NEW)
function(diff_ret_new)
  set(RET_NEW changed_new)
  return(PROPAGATE RET_NEW)
endfunction()
diff_ret_new()
#@@ENDCASE

#@@CASE foreach_cmp0124_old_new_surface
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@QUERY VAR OLD_DEF
#@@QUERY VAR OLD_VAL
#@@QUERY VAR NEW_DEF
#@@QUERY VAR NEW_VAL
cmake_policy(SET CMP0124 OLD)
foreach(OLD_LOOP IN ITEMS x y)
endforeach()
if(DEFINED OLD_LOOP)
  set(OLD_DEF yes)
  set(OLD_VAL "${OLD_LOOP}")
else()
  set(OLD_DEF no)
endif()
cmake_policy(SET CMP0124 NEW)
foreach(NEW_LOOP IN ITEMS x y)
endforeach()
if(DEFINED NEW_LOOP)
  set(NEW_DEF yes)
  set(NEW_VAL "${NEW_LOOP}")
else()
  set(NEW_DEF no)
endif()
#@@ENDCASE

#@@CASE policy_include_scope_surface
#@@MODE PROJECT
#@@OUTCOME SUCCESS
#@@FILE_TEXT inc_policy_scope.cmake
cmake_policy(SET CMP0124 OLD)
#@@END_FILE_TEXT
#@@QUERY VAR AFTER_NO_SCOPE
#@@QUERY VAR AFTER_SCOPED
cmake_policy(VERSION 3.28)
include("${CMAKE_CURRENT_SOURCE_DIR}/inc_policy_scope.cmake" NO_POLICY_SCOPE)
cmake_policy(GET CMP0124 AFTER_NO_SCOPE)
cmake_policy(SET CMP0124 NEW)
include("${CMAKE_CURRENT_SOURCE_DIR}/inc_policy_scope.cmake")
cmake_policy(GET CMP0124 AFTER_SCOPED)
#@@ENDCASE

#@@CASE minimum_required_inside_function_policy_surface
#@@MODE PROJECT
#@@OUTCOME SUCCESS
#@@PROJECT_LAYOUT RAW_CMAKELISTS
#@@QUERY VAR OUT_POL
#@@QUERY VAR MIN_VER
cmake_minimum_required(VERSION 3.10)
project(StructuralPolicyCompat)
function(set_local_min)
  cmake_minimum_required(VERSION 3.28)
endfunction()
set_local_min()
cmake_policy(GET CMP0124 OUT_POL)
set(MIN_VER "${CMAKE_MINIMUM_REQUIRED_VERSION}")
#@@ENDCASE

#@@CASE if_policy_predicate_surface
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@QUERY VAR KNOWN_POLICY
#@@QUERY VAR UNKNOWN_POLICY
if(POLICY CMP0140)
  set(KNOWN_POLICY yes)
else()
  set(KNOWN_POLICY no)
endif()
if(POLICY CMP9999)
  set(UNKNOWN_POLICY yes)
else()
  set(UNKNOWN_POLICY no)
endif()
#@@ENDCASE

#@@CASE structural_policy_invalid_forms
#@@MODE SCRIPT
#@@OUTCOME ERROR
cmake_policy(SET CMP0140 INVALID)
#@@ENDCASE
