#@@CASE argument_parsing_separate_arguments_and_cmake_parse_arguments_surface
#@@OUTCOME SUCCESS
#@@QUERY VAR OUT_UNIX
#@@QUERY VAR OUT_EMPTY
#@@QUERY VAR OUT_PROGRAM
#@@QUERY VAR ARG_OPT
#@@QUERY VAR ARG_FAST
#@@QUERY VAR ARG_DEST
#@@QUERY VAR ARG_TARGETS
#@@QUERY VAR ARG_T0
#@@QUERY VAR ARG_T1
#@@QUERY VAR ARG_UNPARSED_ARGUMENTS
#@@QUERY VAR ARG_KEYWORDS_MISSING_VALUES
#@@QUERY VAR PARSE_FLAG
#@@QUERY VAR PARSE_ONE_DEFINED
#@@QUERY VAR PARSE_MULTI
#@@QUERY VAR PARSE_UNPARSED
#@@QUERY VAR PARSE_MISSING
macro(parse_direct)
  cmake_parse_arguments(ARG "OPT;FAST" "DEST" "TARGETS;CONFIGS" ${ARGN})
  list(GET ARG_TARGETS 0 ARG_T0)
  list(GET ARG_TARGETS 1 ARG_T1)
endmacro()
function(parse_argv)
  cmake_parse_arguments(PARSE_ARGV 1 FN "FLAG" "ONE" "MULTI")
  set(PARSE_FLAG "${FN_FLAG}" PARENT_SCOPE)
  if(DEFINED FN_ONE)
    set(PARSE_ONE_DEFINED 1 PARENT_SCOPE)
  else()
    set(PARSE_ONE_DEFINED 0 PARENT_SCOPE)
  endif()
  set(PARSE_MULTI "${FN_MULTI}" PARENT_SCOPE)
  set(PARSE_UNPARSED "${FN_UNPARSED_ARGUMENTS}" PARENT_SCOPE)
  set(PARSE_MISSING "${FN_KEYWORDS_MISSING_VALUES}" PARENT_SCOPE)
endfunction()
separate_arguments(OUT_UNIX UNIX_COMMAND [=[alpha "two words" three\ four]=])
separate_arguments(OUT_EMPTY UNIX_COMMAND)
set(CMDLINE "${CMAKE_COMMAND} -E true")
separate_arguments(OUT_PROGRAM NATIVE_COMMAND PROGRAM SEPARATE_ARGS "${CMDLINE}")
parse_direct(OPT EXTRA DEST bin TARGETS a b CONFIGS)
parse_argv(skip FLAG TAIL ONE "" MULTI alpha beta)
#@@ENDCASE

#@@CASE argument_parsing_invalid_shapes_are_rejected
#@@OUTCOME ERROR
separate_arguments(BAD_MISSING_MODE PROGRAM alpha)
cmake_parse_arguments()
#@@ENDCASE
