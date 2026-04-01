#@@CASE function_scope_and_propagation_surface
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@QUERY VAR FN_PARENT
#@@QUERY VAR FN_ARGC
#@@QUERY VAR FN_ARGV
#@@QUERY VAR FN_ARGN
#@@QUERY VAR FN_ARGV0
#@@QUERY VAR FN_ARGV2
#@@QUERY VAR FN_LOCAL_VISIBLE
set(FN_PARENT start)
function(diff_fn A B)
  set(FN_PARENT changed PARENT_SCOPE)
  set(FN_ARGC "${ARGC}" PARENT_SCOPE)
  set(FN_ARGV "${ARGV}" PARENT_SCOPE)
  set(FN_ARGN "${ARGN}" PARENT_SCOPE)
  set(FN_ARGV0 "${ARGV0}" PARENT_SCOPE)
  set(FN_ARGV2 "${ARGV2}" PARENT_SCOPE)
  set(FN_LOCAL inside)
endfunction()
diff_fn(alpha beta gamma)
if(DEFINED FN_LOCAL)
  set(FN_LOCAL_VISIBLE yes)
else()
  set(FN_LOCAL_VISIBLE no)
endif()
#@@ENDCASE

#@@CASE macro_scope_and_visibility_surface
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@QUERY VAR MAC_PARENT
#@@QUERY VAR MAC_ARGC
#@@QUERY VAR MAC_ARGV
#@@QUERY VAR MAC_ARGN
#@@QUERY VAR MAC_COMMAND_SEEN
set(MAC_PARENT start)
macro(diff_mc A B)
  set(MAC_PARENT changed)
  set(MAC_ARGC "${ARGC}")
  set(MAC_ARGV "${ARGV}")
  set(MAC_ARGN "${ARGN}")
endmacro()
function(user_visible_fn)
endfunction()
macro(user_visible_mc)
endmacro()
if(COMMAND user_visible_fn AND COMMAND user_visible_mc)
  set(MAC_COMMAND_SEEN yes)
endif()
diff_mc(alpha beta gamma)
#@@ENDCASE

#@@CASE return_function_and_nested_block_unwind_surface
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@QUERY FILE_TEXT callable_trace.txt
#@@QUERY VAR AFTER_FUNCTION
file(WRITE "${CMAKE_CURRENT_LIST_DIR}/callable_trace.txt" "")
function(write_trace)
  file(APPEND "${CMAKE_CURRENT_LIST_DIR}/callable_trace.txt" "enter_function\n")
  block()
    file(APPEND "${CMAKE_CURRENT_LIST_DIR}/callable_trace.txt" "enter_block\n")
    return()
    file(APPEND "${CMAKE_CURRENT_LIST_DIR}/callable_trace.txt" "after_return\n")
  endblock()
  file(APPEND "${CMAKE_CURRENT_LIST_DIR}/callable_trace.txt" "after_block\n")
endfunction()
write_trace()
set(AFTER_FUNCTION reached)
file(APPEND "${CMAKE_CURRENT_LIST_DIR}/callable_trace.txt" "after_call\n")
#@@ENDCASE

#@@CASE return_macro_callsite_surface
#@@MODE PROJECT
#@@OUTCOME SUCCESS
#@@FILE_TEXT macro_return_include.cmake
macro(diff_return_macro)
  set(MACRO_RET before)
  return()
  set(MACRO_RET after)
endmacro()
diff_return_macro()
set(AFTER_MACRO_IN_INCLUDE inside)
#@@END_FILE_TEXT
#@@QUERY VAR MACRO_RET
#@@QUERY VAR AFTER_MACRO_IN_INCLUDE
#@@QUERY VAR AFTER_INCLUDE
set(MACRO_RET start)
include("${CMAKE_CURRENT_SOURCE_DIR}/macro_return_include.cmake")
set(AFTER_INCLUDE top)
#@@ENDCASE

#@@CASE return_invalid_forms
#@@MODE SCRIPT
#@@OUTCOME ERROR
cmake_policy(SET CMP0140 NEW)
return(BOGUS)
#@@ENDCASE
