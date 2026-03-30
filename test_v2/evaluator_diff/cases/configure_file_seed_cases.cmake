#@@CASE configure_file_configure_and_newline_surface
#@@OUTCOME SUCCESS
#@@FILE_TEXT cfg_template.in
NAME=@NAME@
LITERAL=${NAME}
QUOTE=@QUOTE@
#cmakedefine ENABLE_FEATURE
#cmakedefine DISABLE_FEATURE
#cmakedefine01 ENABLE_FEATURE
#cmakedefine01 DISABLE_FEATURE
#@@END_FILE_TEXT
#@@QUERY FILE_TEXT build/cfg_configured.txt
#@@QUERY VAR CFG_HEX
set(NAME Demo)
set(QUOTE "one\"two")
set(ENABLE_FEATURE ON)
set(DISABLE_FEATURE 0)
configure_file(cfg_template.in cfg_configured.txt @ONLY ESCAPE_QUOTES NEWLINE_STYLE DOS)
file(READ "${CMAKE_CURRENT_BINARY_DIR}/cfg_configured.txt" CFG_HEX HEX)
#@@ENDCASE

#@@CASE configure_file_copyonly_surface
#@@OUTCOME SUCCESS
#@@FILE_TEXT cfg_copy.in
@NAME@
${NAME}
#@@END_FILE_TEXT
#@@DIR build/cfg_out_dir
#@@QUERY FILE_TEXT build/cfg_out_dir/cfg_copy.in
set(NAME Demo)
configure_file(cfg_copy.in cfg_out_dir COPYONLY)
#@@ENDCASE

#@@CASE configure_file_missing_output_is_error
#@@OUTCOME ERROR
#@@FILE_TEXT cfg_template.in
name=@NAME@
#@@END_FILE_TEXT
configure_file(cfg_template.in)
#@@ENDCASE
