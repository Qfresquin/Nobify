#@@CASE configure_file_script_configure_and_newline_surface
#@@MODE SCRIPT
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
#@@QUERY FILE_TEXT cfg_configured.txt
set(NAME Demo)
set(QUOTE "one\"two")
set(ENABLE_FEATURE ON)
set(DISABLE_FEATURE 0)
configure_file(cfg_template.in cfg_configured.txt @ONLY ESCAPE_QUOTES NEWLINE_STYLE DOS)
#@@ENDCASE

#@@CASE configure_file_script_copyonly_surface
#@@MODE SCRIPT
#@@OUTCOME SUCCESS
#@@FILE_TEXT cfg_copy.in
@NAME@
${NAME}
#@@END_FILE_TEXT
#@@DIR cfg_out_dir
#@@QUERY FILE_TEXT cfg_out_dir/cfg_copy.in
set(NAME Demo)
configure_file(cfg_copy.in cfg_out_dir COPYONLY)
#@@ENDCASE

#@@CASE configure_file_script_missing_output_is_error
#@@MODE SCRIPT
#@@OUTCOME ERROR
#@@FILE_TEXT cfg_template.in
name=@NAME@
#@@END_FILE_TEXT
configure_file(cfg_template.in)
#@@ENDCASE
