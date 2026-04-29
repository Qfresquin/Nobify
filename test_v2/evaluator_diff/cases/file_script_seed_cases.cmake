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

#@@CASE file_invalid_forms
#@@MODE SCRIPT
#@@OUTCOME ERROR
file()
file(WRITE)
file(READ)
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
