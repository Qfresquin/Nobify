#@@CASE cmake_policy_push_set_get_pop_surface
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@QUERY VAR POL_OUTER_BEFORE
#@@QUERY VAR POL_INNER
#@@QUERY VAR POL_AFTER
cmake_policy(VERSION 3.28)
cmake_policy(GET CMP0077 POL_OUTER_BEFORE)
cmake_policy(PUSH)
cmake_policy(SET CMP0077 OLD)
cmake_policy(GET CMP0077 POL_INNER)
cmake_policy(POP)
cmake_policy(GET CMP0077 POL_AFTER)
#@@ENDCASE

#@@CASE cmake_policy_version_and_predicate_surface
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@QUERY VAR POL_VER
#@@QUERY VAR IF_KNOWN
#@@QUERY VAR IF_UNKNOWN
cmake_policy(VERSION 3.10...3.28)
cmake_policy(GET CMP0077 POL_VER)
if(POLICY CMP0077)
  set(IF_KNOWN 1)
else()
  set(IF_KNOWN 0)
endif()
if(POLICY CMP9999)
  set(IF_UNKNOWN 1)
else()
  set(IF_UNKNOWN 0)
endif()
#@@ENDCASE

#@@CASE cmake_policy_invalid_forms
#@@MODE SCRIPT
#@@OUTCOME ERROR
cmake_policy(PUSH EXTRA)
cmake_policy(GET CMP9999 BAD_OUT)
cmake_policy(VERSION 3.29)
#@@ENDCASE
