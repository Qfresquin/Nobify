#@@CASE cmake_language_call_eval_and_log_level
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@QUERY VAR CALL_OUT
#@@QUERY VAR EVAL_OUT
#@@QUERY VAR LOG_OUT
set(CMAKE_MESSAGE_LOG_LEVEL NOTICE)
cmake_language(CALL set CALL_OUT alpha)
cmake_language(EVAL CODE [[set(EVAL_OUT beta)]])
cmake_language(GET_MESSAGE_LOG_LEVEL LOG_OUT)
#@@ENDCASE

#@@CASE cmake_language_incomplete_and_unknown_forms
#@@MODE SCRIPT
#@@OUTCOME ERROR
cmake_language()
cmake_language(CALL)
cmake_language(EVAL)
cmake_language(EVAL CODE)
cmake_language(GET_MESSAGE_LOG_LEVEL)
cmake_language(UNKNOWN_SUBCOMMAND value)
#@@ENDCASE
