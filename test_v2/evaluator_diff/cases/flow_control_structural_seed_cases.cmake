#@@CASE if_condition_surface
#@@MODE PROJECT
#@@OUTCOME SUCCESS
#@@FILE_TEXT cond_exists.txt
ok
#@@END_FILE_TEXT
#@@QUERY VAR IF_ALL_TRUE
#@@QUERY VAR IF_ANY_FALSE
#@@QUERY VAR BRANCH_PICK
add_library(flow_cond_lib INTERFACE)
set(FLOW_DEF 1)
if(COMMAND add_library AND TARGET flow_cond_lib AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cond_exists.txt" AND DEFINED FLOW_DEF AND POLICY CMP0140 AND NOT DEFINED FLOW_MISSING)
  set(IF_ALL_TRUE yes)
endif()
if(TARGET missing_target OR EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/missing.txt")
  set(IF_ANY_FALSE bad)
else()
  set(IF_ANY_FALSE no)
endif()
if(FALSE)
  set(BRANCH_PICK bad_then)
elseif(IF_ALL_TRUE)
  set(BRANCH_PICK elseif_taken)
else()
  set(BRANCH_PICK bad_else)
endif()
#@@ENDCASE

#@@CASE foreach_while_break_continue_surface
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@QUERY VAR FLOW_ACC
#@@QUERY VAR WHILE_DONE
set(FLOW_ACC "")
foreach(I RANGE 0 4)
  if(I EQUAL 1)
    continue()
  endif()
  if(I EQUAL 3)
    break()
  endif()
  string(APPEND FLOW_ACC "F${I};")
endforeach()
set(J 0)
while(J LESS 5)
  math(EXPR J "${J} + 1")
  if(J EQUAL 2)
    continue()
  endif()
  if(J EQUAL 4)
    break()
  endif()
  string(APPEND FLOW_ACC "W${J};")
endwhile()
set(WHILE_DONE "${J}")
#@@ENDCASE

#@@CASE block_scope_and_unwind_surface
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@QUERY VAR OUTER_AFTER
#@@QUERY VAR INNER_VISIBLE_AFTER
#@@QUERY VAR TRACE_DONE
#@@QUERY FILE_TEXT block_trace.txt
set(OUTER outer)
file(WRITE "${CMAKE_CURRENT_LIST_DIR}/block_trace.txt" "")
block()
  set(OUTER inner)
  set(INNER_ONLY yes)
  file(APPEND "${CMAKE_CURRENT_LIST_DIR}/block_trace.txt" "inside_block\n")
endblock()
if(DEFINED INNER_ONLY)
  set(INNER_VISIBLE_AFTER yes)
else()
  set(INNER_VISIBLE_AFTER no)
endif()
set(OUTER_AFTER "${OUTER}")
set(TRACE_DONE done)
#@@ENDCASE

#@@CASE foreach_invalid_forms
#@@MODE SCRIPT
#@@OUTCOME ERROR
foreach()
endforeach()
#@@ENDCASE

#@@CASE break_continue_invalid_context
#@@MODE SCRIPT
#@@OUTCOME ERROR
cmake_policy(SET CMP0055 NEW)
break()
#@@ENDCASE

#@@CASE block_invalid_forms
#@@MODE SCRIPT
#@@OUTCOME ERROR
block(SCOPE_FOR)
#@@ENDCASE
