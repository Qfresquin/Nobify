#@@CASE property_wrappers_global_and_directory_surface
#@@OUTCOME SUCCESS
#@@QUERY CMAKE_PROP CMAKE_ROLE
#@@QUERY GLOBAL_PROP USE_FOLDERS
#@@QUERY DIR_PROP . ROOT_DIR_PROP
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(DIRECTORY PROPERTY ROOT_DIR_PROP from_root)
#@@ENDCASE

#@@CASE property_wrappers_missing_dir_sentinel_surface
#@@OUTCOME SUCCESS
#@@QUERY DIR_PROP source/missing_dir SOME_PROP
set(DUMMY ok)
#@@ENDCASE

#@@CASE property_wrappers_invalid_wrapper_forms
#@@OUTCOME ERROR
get_cmake_property()
#@@ENDCASE
