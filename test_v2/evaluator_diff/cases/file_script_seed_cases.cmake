#@@CASE file_rw_append_and_strings_surface
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@QUERY FILE_TEXT io.txt
#@@QUERY VAR READ_HEX
#@@QUERY VAR STR_LINES
file(WRITE io.txt "alpha\nbeta\n")
file(APPEND io.txt "gamma\n")
file(READ io.txt READ_HEX HEX)
file(STRINGS io.txt STR_LINES REGEX "^(alpha|gamma)$")
#@@ENDCASE

#@@CASE file_configure_empty_content_and_read_invalid_limit_surface
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@QUERY FILE_TEXT empty_configure.txt
#@@QUERY VAR EMPTY_CONFIGURE_OUT
#@@QUERY VAR READ_LIMIT_OUT
file(CONFIGURE OUTPUT empty_configure.txt CONTENT "")
file(READ empty_configure.txt EMPTY_CONFIGURE_OUT)
file(WRITE read_limit_src.txt "abc")
file(READ read_limit_src.txt READ_LIMIT_OUT LIMIT nope)
#@@ENDCASE

#@@CASE file_glob_and_real_path_surface
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@QUERY VAR GLOB_TOP
#@@QUERY VAR GLOB_ALL
#@@QUERY VAR REAL_A
file(MAKE_DIRECTORY glob_root/sub)
file(WRITE glob_root/a.c "int a = 0;\n")
file(WRITE glob_root/sub/b.c "int b = 0;\n")
file(GLOB GLOB_TOP
     RELATIVE "${CMAKE_CURRENT_LIST_DIR}/glob_root"
     "${CMAKE_CURRENT_LIST_DIR}/glob_root/*.c")
file(GLOB_RECURSE GLOB_ALL
     RELATIVE "${CMAKE_CURRENT_LIST_DIR}/glob_root"
     "${CMAKE_CURRENT_LIST_DIR}/glob_root/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/glob_root/*/*.c")
file(REAL_PATH glob_root/sub/../a.c REAL_A)
#@@ENDCASE

#@@CASE file_fsops_size_hash_and_timestamp_surface
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@QUERY FILE_TEXT renamed.txt
#@@QUERY FILE_TEXT copy_dir/copied.txt
#@@QUERY FILE_EXISTS seed.txt
#@@QUERY FILE_EXISTS rm_tree
#@@QUERY VAR SIZE_OUT
#@@QUERY VAR HASH_OUT
#@@QUERY VAR TS_DAY
file(WRITE seed.txt "abc")
file(COPY_FILE seed.txt copied.txt)
file(COPY copied.txt DESTINATION copy_dir)
file(RENAME copied.txt renamed.txt)
file(SIZE renamed.txt SIZE_OUT)
file(SHA256 renamed.txt HASH_OUT)
file(TIMESTAMP renamed.txt TS_DAY "%Y-%m-%d" UTC)
file(MAKE_DIRECTORY rm_tree/sub)
file(WRITE rm_tree/sub/gone.txt "x")
file(REMOVE seed.txt)
file(REMOVE_RECURSE rm_tree)
#@@ENDCASE

#@@CASE file_copy_file_result_failure_surface
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@QUERY VAR COPY_INPUT_RES
#@@QUERY VAR COPY_OUTPUT_RES
#@@QUERY VAR COPY_INPUT_LITERAL_ONE
#@@QUERY VAR COPY_OUTPUT_LITERAL_ONE
#@@QUERY VAR COPY_OUTPUT_EXISTS
file(REMOVE missing_copy_input.txt)
file(REMOVE_RECURSE missing_copy_parent)
file(COPY_FILE missing_copy_input.txt copy_from_missing.txt RESULT COPY_INPUT_RES)
file(WRITE copy_parent_src.txt "x")
file(COPY_FILE copy_parent_src.txt missing_copy_parent/out.txt RESULT COPY_OUTPUT_RES)
if(COPY_INPUT_RES STREQUAL "1")
  set(COPY_INPUT_LITERAL_ONE 1)
else()
  set(COPY_INPUT_LITERAL_ONE 0)
endif()
if(COPY_OUTPUT_RES STREQUAL "1")
  set(COPY_OUTPUT_LITERAL_ONE 1)
else()
  set(COPY_OUTPUT_LITERAL_ONE 0)
endif()
if(EXISTS missing_copy_parent/out.txt)
  set(COPY_OUTPUT_EXISTS 1)
else()
  set(COPY_OUTPUT_EXISTS 0)
endif()
#@@ENDCASE

#@@CASE file_strings_newline_consume_surface
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@QUERY VAR STR_NC_VISIBLE
#@@QUERY VAR STR_NC_LEN
#@@QUERY VAR STR_NC_BYTES
file(WRITE strings_nc.txt "alpha\nbeta\n")
file(STRINGS strings_nc.txt STR_NC NEWLINE_CONSUME)
string(REPLACE "\n" "|" STR_NC_VISIBLE "${STR_NC}")
list(LENGTH STR_NC STR_NC_LEN)
string(LENGTH "${STR_NC}" STR_NC_BYTES)
#@@ENDCASE

#@@CASE file_copy_install_and_strings_option_surface
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@QUERY TREE copy_filtered
#@@QUERY TREE copy_excluded
#@@QUERY TREE install_out
#@@QUERY VAR STR_LIMIT
#@@QUERY VAR STR_LIMIT_VISIBLE
#@@QUERY VAR STR_LIMIT_LEN
file(MAKE_DIRECTORY copy_src/sub)
file(WRITE copy_src/keep.txt "K")
file(WRITE copy_src/skip.tmp "S")
file(WRITE copy_src/sub/keep2.txt "K2")
file(WRITE copy_src/sub/skip2.tmp "S2")
file(COPY copy_src DESTINATION copy_filtered FILES_MATCHING PATTERN "*.txt")
file(COPY copy_src DESTINATION copy_excluded FILES_MATCHING PATTERN "*.txt" PATTERN "keep.txt" EXCLUDE)
file(WRITE install_src.txt "I")
file(INSTALL DESTINATION install_out TYPE FILE FILES install_src.txt OPTIONAL MESSAGE_NEVER)
file(WRITE strings_limits.txt "aa\nbbbb\nccc\nddddd\n")
file(STRINGS strings_limits.txt STR_LIMIT
     REGEX "^[a-z]+$"
     LENGTH_MINIMUM 3
     LENGTH_MAXIMUM 4
     LIMIT_COUNT 2
     LIMIT_OUTPUT 99
     ENCODING UTF-8)
string(REPLACE ";" "|" STR_LIMIT_VISIBLE "${STR_LIMIT}")
list(LENGTH STR_LIMIT STR_LIMIT_LEN)
#@@ENDCASE

#@@CASE file_copy_install_chmod_permission_surface
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@QUERY VAR COPY_USE_MODE
#@@QUERY VAR COPY_NO_MODE
#@@QUERY VAR COPY_FILE_MODE
#@@QUERY VAR COPY_DIR_MODE
#@@QUERY VAR COPY_DIR_FILE_MODE
#@@QUERY VAR INSTALL_FILE_MODE
#@@QUERY VAR CHMOD_FILE_MODE
#@@QUERY VAR CHMOD_RECURSE_DIR_MODE
#@@QUERY VAR CHMOD_RECURSE_FILE_MODE
file(WRITE perm_src.txt "x")
file(MAKE_DIRECTORY perm_dir)
file(WRITE perm_dir/nested.txt "x")
file(CHMOD perm_src.txt PERMISSIONS OWNER_READ OWNER_WRITE)
file(CHMOD perm_dir PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
file(CHMOD perm_dir/nested.txt PERMISSIONS OWNER_READ)
file(COPY perm_src.txt DESTINATION perm_copy_use USE_SOURCE_PERMISSIONS)
file(COPY perm_src.txt DESTINATION perm_copy_no NO_SOURCE_PERMISSIONS)
file(COPY perm_src.txt DESTINATION perm_copy_file FILE_PERMISSIONS OWNER_READ)
file(COPY perm_dir DESTINATION perm_copy_dir
     DIRECTORY_PERMISSIONS OWNER_READ OWNER_EXECUTE
     FILE_PERMISSIONS OWNER_READ)
file(INSTALL DESTINATION perm_install TYPE FILE FILES perm_src.txt
     PERMISSIONS OWNER_READ OPTIONAL MESSAGE_NEVER)
file(WRITE chmod_target.txt "x")
file(MAKE_DIRECTORY chmod_tree)
file(WRITE chmod_tree/nested.txt "x")
file(CHMOD chmod_target.txt PERMISSIONS OWNER_READ)
file(CHMOD_RECURSE chmod_tree
     PERMISSIONS OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)
execute_process(
  COMMAND /bin/sh -c "stat -c '%a' perm_copy_use/perm_src.txt"
  OUTPUT_VARIABLE COPY_USE_MODE
  OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(
  COMMAND /bin/sh -c "stat -c '%a' perm_copy_no/perm_src.txt"
  OUTPUT_VARIABLE COPY_NO_MODE
  OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(
  COMMAND /bin/sh -c "stat -c '%a' perm_copy_file/perm_src.txt"
  OUTPUT_VARIABLE COPY_FILE_MODE
  OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(
  COMMAND /bin/sh -c "stat -c '%a' perm_copy_dir/perm_dir"
  OUTPUT_VARIABLE COPY_DIR_MODE
  OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(
  COMMAND /bin/sh -c "stat -c '%a' perm_copy_dir/perm_dir/nested.txt"
  OUTPUT_VARIABLE COPY_DIR_FILE_MODE
  OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(
  COMMAND /bin/sh -c "stat -c '%a' perm_install/perm_src.txt"
  OUTPUT_VARIABLE INSTALL_FILE_MODE
  OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(
  COMMAND /bin/sh -c "stat -c '%a' chmod_target.txt"
  OUTPUT_VARIABLE CHMOD_FILE_MODE
  OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(
  COMMAND /bin/sh -c "stat -c '%a' chmod_tree"
  OUTPUT_VARIABLE CHMOD_RECURSE_DIR_MODE
  OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(
  COMMAND /bin/sh -c "stat -c '%a' chmod_tree/nested.txt"
  OUTPUT_VARIABLE CHMOD_RECURSE_FILE_MODE
  OUTPUT_STRIP_TRAILING_WHITESPACE)
#@@ENDCASE

#@@CASE file_strings_missing_option_values_and_conflict_surface
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@QUERY VAR OUT_LIMIT_MISSING
#@@QUERY VAR OUT_REGEX_MISSING
#@@QUERY VAR OUT_ENCODING_MISSING
#@@QUERY VAR OUT_CONFLICT
#@@QUERY VAR OUT_LIMIT_NEXT
file(WRITE strings_missing_options.txt "abc\ndef\n")
file(STRINGS strings_missing_options.txt OUT_LIMIT_MISSING LIMIT_COUNT)
file(STRINGS strings_missing_options.txt OUT_REGEX_MISSING REGEX)
file(STRINGS strings_missing_options.txt OUT_ENCODING_MISSING ENCODING)
file(STRINGS strings_missing_options.txt OUT_CONFLICT LENGTH_MINIMUM 5 LENGTH_MAXIMUM 2)
file(STRINGS strings_missing_options.txt OUT_LIMIT_NEXT LIMIT_COUNT REGEX "abc")
#@@ENDCASE

#@@CASE file_invalid_forms
#@@MODE SCRIPT
#@@OUTCOME ERROR
file()
file(WRITE)
file(READ)
#@@ENDCASE

#@@CASE file_strings_rejects_unknown_argument
#@@MODE SCRIPT
#@@OUTCOME ERROR
file(WRITE strings_unknown_arg.txt "abc\n")
file(STRINGS strings_unknown_arg.txt OUT UNKNOWN_OPTION)
#@@ENDCASE

#@@CASE file_strings_rejects_invalid_numeric_value
#@@MODE SCRIPT
#@@OUTCOME ERROR
file(WRITE strings_invalid_numeric.txt "abc\n")
file(STRINGS strings_invalid_numeric.txt OUT LENGTH_MINIMUM UNKNOWN_OPTION)
#@@ENDCASE

#@@CASE file_strings_rejects_invalid_encoding_value
#@@MODE SCRIPT
#@@OUTCOME ERROR
file(WRITE strings_invalid_encoding.txt "abc\n")
file(STRINGS strings_invalid_encoding.txt OUT ENCODING WAT)
#@@ENDCASE

#@@CASE file_rejects_unknown_rename_argument
#@@MODE SCRIPT
#@@OUTCOME ERROR
file(WRITE reject_rename_src.txt "x")
file(RENAME reject_rename_src.txt reject_rename_dst.txt JUNK)
#@@ENDCASE

#@@CASE file_rejects_unknown_create_link_argument
#@@MODE SCRIPT
#@@OUTCOME ERROR
file(WRITE reject_link_src.txt "x")
file(CREATE_LINK reject_link_src.txt reject_link_dst.txt JUNK)
#@@ENDCASE

#@@CASE file_rejects_unknown_real_path_argument
#@@MODE SCRIPT
#@@OUTCOME ERROR
file(REAL_PATH . REAL_OUT JUNK)
#@@ENDCASE

#@@CASE file_rejects_unknown_copy_argument
#@@MODE SCRIPT
#@@OUTCOME ERROR
file(WRITE reject_copy_src.txt "x")
file(COPY reject_copy_src.txt DESTINATION reject_copy_dst JUNK)
#@@ENDCASE

#@@CASE file_copy_rejects_permissions_keyword
#@@MODE SCRIPT
#@@OUTCOME ERROR
file(WRITE reject_copy_perm_src.txt "x")
file(COPY reject_copy_perm_src.txt DESTINATION reject_copy_perm_dst PERMISSIONS OWNER_READ)
#@@ENDCASE

#@@CASE file_copy_rejects_invalid_file_permission
#@@MODE SCRIPT
#@@OUTCOME ERROR
file(WRITE reject_file_perm_src.txt "x")
file(COPY reject_file_perm_src.txt DESTINATION reject_file_perm_dst FILE_PERMISSIONS OWNER_READ BOGUS)
#@@ENDCASE

#@@CASE file_install_rejects_invalid_permission
#@@MODE SCRIPT
#@@OUTCOME ERROR
file(WRITE reject_install_perm_src.txt "x")
file(INSTALL DESTINATION reject_install_perm_dst TYPE FILE FILES reject_install_perm_src.txt PERMISSIONS OWNER_READ BOGUS)
#@@ENDCASE

#@@CASE file_chmod_rejects_invalid_permission
#@@MODE SCRIPT
#@@OUTCOME ERROR
file(WRITE reject_chmod_perm_src.txt "x")
file(CHMOD reject_chmod_perm_src.txt PERMISSIONS OWNER_READ BOGUS)
#@@ENDCASE
