#@@CASE message_stdout_and_stderr_surface
#@@OUTCOME SUCCESS
#@@PROJECT_LAYOUT RAW_CMAKELISTS
#@@QUERY STDOUT
#@@QUERY STDERR
cmake_minimum_required(VERSION 3.28)
project(MessageDiff LANGUAGES NONE)
message(NOTICE notice-msg)
message(STATUS status-msg)
message(CHECK_START probe)
message(CHECK_PASS ok)
message(CHECK_START probe2)
message(CHECK_FAIL fail)
#@@ENDCASE

#@@CASE message_send_error_output_surface
#@@OUTCOME ERROR
#@@PROJECT_LAYOUT RAW_CMAKELISTS
#@@QUERY STDOUT
#@@QUERY STDERR
cmake_minimum_required(VERSION 3.28)
project(MessageErr LANGUAGES NONE)
message(SEND_ERROR boom)
#@@ENDCASE
