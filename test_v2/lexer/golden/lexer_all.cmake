#@@CASE empty_input
#@@ENDCASE

#@@CASE simple_parens
()
#@@ENDCASE

#@@CASE identifier
add_executable
#@@ENDCASE

#@@CASE string_token
"hello world"
#@@ENDCASE

#@@CASE string_with_escapes
"hello\nworld"
#@@ENDCASE

#@@CASE variable_simple
${VAR}
#@@ENDCASE

#@@CASE variable_env
$ENV{PATH}
#@@ENDCASE

#@@CASE generator_expression
$<TARGET_FILE:app>
#@@ENDCASE

#@@CASE raw_string
[[raw content]]
#@@ENDCASE

#@@CASE raw_string_with_equals
[=[raw content]=]
#@@ENDCASE

#@@CASE line_comment
# this is a comment
add_executable
#@@ENDCASE

#@@CASE block_comment
#[[ this is a 
block comment ]]add_executable
#@@ENDCASE

#@@CASE semicolon
;
#@@ENDCASE

#@@CASE multiple_tokens
set(VAR "value")
#@@ENDCASE

#@@CASE whitespace_handling
  set  (  VAR  )  
#@@ENDCASE

#@@CASE concatenated_args
lib${VAR}.a
#@@ENDCASE

#@@CASE line_continuation
set\
(VAR value)
#@@ENDCASE

#@@CASE nested_variables
${${NESTED}}
#@@ENDCASE

#@@CASE unclosed_string_is_invalid
"unterminated
#@@ENDCASE

#@@CASE unclosed_variable_is_invalid
${VAR
#@@ENDCASE

#@@CASE unclosed_genexp_is_invalid
$<TARGET_FILE:app
#@@ENDCASE

#@@CASE unclosed_raw_string_is_invalid
[[raw content
#@@ENDCASE

#@@CASE single_dollar_is_identifier
$
#@@ENDCASE

#@@CASE line_and_col_tracking_lf
one
  two
three
#@@ENDCASE

#@@CASE crlf_newlines_are_handled
a
b
#@@ENDCASE

#@@CASE block_comment_with_equals
#[=[ comment ]=]add_executable
#@@ENDCASE

#@@CASE raw_string_with_internal_brackets
[=[abc]def]=]
#@@ENDCASE

#@@CASE raw_string_mismatched_equals_does_not_close_early
[==[a]=]b]==]
#@@ENDCASE

#@@CASE genexp_nested_angle_brackets
$<A<B>>
#@@ENDCASE

#@@CASE var_with_escaped_braces_1
${A\}B}
#@@ENDCASE

#@@CASE var_with_escaped_braces_2
${A\{B\}}
#@@ENDCASE

#@@CASE identifier_with_escaped_delimiters_1
abc\ def
#@@ENDCASE

#@@CASE identifier_with_escaped_delimiters_2
abc\;def;
#@@ENDCASE

#@@CASE identifier_stops_before_comment
a#b
c
#@@ENDCASE

#@@CASE concatenated_multiple_vars_has_space_left_false
a${B}${C}d
#@@ENDCASE

#@@CASE golden_basic
set(X 1)
message(STATUS ok)
#@@ENDCASE

#@@CASE golden_strings_raw_var_genexp
set(X "a b" [=[raw;txt]=] ${VAR} $ENV{HOME} $<CONFIG:Debug>)
#@@ENDCASE

#@@CASE golden_invalid_tokens
"unterminated
#@@ENDCASE

#@@CASE golden_comments_and_whitespace
   # leading line comment
set (X 1) # trailing
#[=[block
comment]=]
message(OK)
#@@ENDCASE

#@@CASE golden_concatenation_and_vars
lib${VAR}.a
a${B}${C}d
abc\ def
abc\;def;
#@@ENDCASE

#@@CASE golden_linecol_and_newlines
one
  two
three
#@@ENDCASE

#@@CASE golden_nested_and_balancing
${${NESTED}}
$<A<B>>
[=[abc]def]=]
[==[a]=]b]==]
${A\{B\}}
#@@ENDCASE
