#@@CASE bracket_arg_equals_delimiter
set(X [=[a;b]=])
set(Y [==[z]==])
#@@ENDCASE

#@@CASE parser_reports_command_without_parentheses_and_recovers
set VAR value
message(ok)
#@@ENDCASE

#@@CASE parser_reports_stray_paren_and_recovers
)
set(X 1)
#@@ENDCASE

#@@CASE parser_keeps_nested_parens_inside_regular_args
func(a(b))
#@@ENDCASE

#@@CASE parser_marks_quoted_string_args
set(X "a b c")
#@@ENDCASE

#@@CASE parser_reports_block_depth_limit_exceeded
if(A)
  if(B)
    set(X 1)
  endif()
endif()
#@@ENDCASE

#@@CASE parser_drops_command_node_on_unclosed_args
set(X 1
#@@ENDCASE

#@@CASE parser_drops_if_node_on_missing_endif
if(A)
  set(X 1)
#@@ENDCASE

#@@CASE parser_fail_fast_on_append_oom_budget
set(X 1)
#@@ENDCASE

#@@CASE parser_invalid_command_name_emits_error_and_recovers
1abc(x)
set(A 1)
#@@ENDCASE

#@@CASE parser_unexpected_statement_token_emits_error_and_recovers
;
set(A 1)
#@@ENDCASE

#@@CASE parser_golden_simple_commands
set(X "a b c")
set(Y [=[alpha;beta]=])
message(STATUS ok)
#@@ENDCASE

#@@CASE parser_golden_control_flow
if((A AND B) OR C)
  message("then")
elseif(D)
  message("elseif")
else()
  foreach(item a b)
    message(${item})
  endforeach()
endif()

function(foo x y)
  set(Z ${x})
endfunction()

macro(bar)
  message(bar)
endmacro()
#@@ENDCASE

#@@CASE parser_golden_recovery_invalid
set VAR value
)
set(A 1)
1abc(x)
message(ok)
#@@ENDCASE
