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

#@@CASE cmake_language_defer_queue_cancel_and_flush_surface
#@@MODE PROJECT
#@@OUTCOME SUCCESS
#@@QUERY VAR IDS_OUT
#@@QUERY VAR CALL_INFO_OK
#@@QUERY VAR AUTO_ID_WAS_GENERATED
#@@QUERY FILE_TEXT source/defer_results.txt
file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/defer_results.txt" "")
set(DEFER_VALUE before)
cmake_language(DEFER ID later CALL file APPEND "${CMAKE_CURRENT_SOURCE_DIR}/defer_results.txt" "later=${DEFER_VALUE}\n")
cmake_language(DEFER ID cancel_me CALL file APPEND "${CMAKE_CURRENT_SOURCE_DIR}/defer_results.txt" "cancelled=yes\n")
cmake_language(DEFER ID_VAR AUTO_ID CALL file APPEND "${CMAKE_CURRENT_SOURCE_DIR}/defer_results.txt" "auto=yes\n")
cmake_language(DEFER CANCEL_CALL cancel_me ${AUTO_ID})
cmake_language(DEFER GET_CALL_IDS IDS_OUT)
cmake_language(DEFER GET_CALL later CALL_INFO)
if(AUTO_ID MATCHES "^_")
  set(AUTO_ID_WAS_GENERATED 1)
else()
  set(AUTO_ID_WAS_GENERATED 0)
endif()
if(DEFINED CALL_INFO)
  set(CALL_INFO_OK 1)
else()
  set(CALL_INFO_OK 0)
endif()
set(DEFER_VALUE after)
#@@ENDCASE

#@@CASE cmake_language_defer_subdirectory_context_surface
#@@MODE PROJECT
#@@OUTCOME SUCCESS
#@@FILE_TEXT subdir/CMakeLists.txt
cmake_language(DEFER CALL get_filename_component DEFER_SRC_NAME "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
cmake_language(DEFER CALL get_filename_component DEFER_BIN_NAME "${CMAKE_CURRENT_BINARY_DIR}" NAME)
cmake_language(DEFER CALL get_filename_component DEFER_LIST_DIR_NAME "${CMAKE_CURRENT_LIST_DIR}" NAME)
cmake_language(DEFER CALL get_filename_component DEFER_FILE_NAME "${CMAKE_CURRENT_LIST_FILE}" NAME)
cmake_language(DEFER CALL file WRITE "${CMAKE_CURRENT_SOURCE_DIR}/defer_context.txt" "")
cmake_language(DEFER CALL file APPEND "${CMAKE_CURRENT_SOURCE_DIR}/defer_context.txt" "src=${DEFER_SRC_NAME}\n")
cmake_language(DEFER CALL file APPEND "${CMAKE_CURRENT_SOURCE_DIR}/defer_context.txt" "bin=${DEFER_BIN_NAME}\n")
cmake_language(DEFER CALL file APPEND "${CMAKE_CURRENT_SOURCE_DIR}/defer_context.txt" "list_dir=${DEFER_LIST_DIR_NAME}\n")
cmake_language(DEFER CALL file APPEND "${CMAKE_CURRENT_SOURCE_DIR}/defer_context.txt" "file=${DEFER_FILE_NAME}\n")
#@@END_FILE_TEXT
#@@QUERY FILE_TEXT subdir/defer_context.txt
add_subdirectory(subdir)
#@@ENDCASE

#@@CASE cmake_language_defer_invalid_forms
#@@MODE PROJECT
#@@OUTCOME ERROR
cmake_language(DEFER)
cmake_language(DEFER DIRECTORY)
cmake_language(DEFER GET_CALL_IDS)
cmake_language(DEFER GET_CALL later)
cmake_language(DEFER CANCEL_CALL)
cmake_language(DEFER ID _manual CALL set X 1)
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
