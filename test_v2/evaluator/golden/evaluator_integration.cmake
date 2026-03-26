#@@CASE file_security_read_rejects_symlink_escape_outside_project_scope
file(READ temp_symlink_escape_link/outside.txt OUT)
#@@ENDCASE

#@@CASE file_security_strings_rejects_symlink_escape_outside_project_scope
file(STRINGS temp_symlink_escape_link/outside.txt OUT)
#@@ENDCASE

#@@CASE file_copy_follow_symlink_chain_regular_file_no_warning
file(WRITE temp_copy_chain_src.txt "ok")
file(MAKE_DIRECTORY temp_copy_chain_dst)
file(COPY temp_copy_chain_src.txt DESTINATION temp_copy_chain_dst FOLLOW_SYMLINK_CHAIN)
file(READ temp_copy_chain_dst/temp_copy_chain_src.txt COPY_CHAIN_TXT)
add_executable(copy_chain_file main.c)
target_compile_definitions(copy_chain_file PRIVATE COPY_CHAIN_TXT=${COPY_CHAIN_TXT})
#@@ENDCASE

#@@CASE file_read_symlink_and_create_link
file(WRITE temp_file_link_src.txt "LINKDATA")
file(CREATE_LINK temp_file_link_src.txt temp_file_link_sym.txt SYMBOLIC RESULT FILE_LINK_SYM_RES)
file(READ_SYMLINK temp_file_link_sym.txt FILE_LINK_TARGET)
file(CREATE_LINK temp_file_link_src.txt temp_file_link_hard.txt RESULT FILE_LINK_HARD_RES)
file(READ temp_file_link_hard.txt FILE_LINK_HARD_TXT)
set(FILE_LINK_OK 0)
if(EXISTS temp_file_link_sym.txt AND EXISTS temp_file_link_hard.txt)
  set(FILE_LINK_OK 1)
endif()
add_executable(file_link_case main.c)
target_compile_definitions(file_link_case PRIVATE FILE_LINK_OK=${FILE_LINK_OK} FILE_LINK_TARGET=${FILE_LINK_TARGET} FILE_LINK_HARD_TXT=${FILE_LINK_HARD_TXT})
#@@ENDCASE

