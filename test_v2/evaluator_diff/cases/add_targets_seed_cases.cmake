#@@CASE add_executable_imported_global_and_alias_surface
#@@OUTCOME SUCCESS
#@@QUERY TARGET_EXISTS tool
#@@QUERY TARGET_EXISTS tool_alias
#@@QUERY TARGET_PROP tool TYPE
#@@QUERY TARGET_PROP tool IMPORTED
#@@QUERY TARGET_PROP tool IMPORTED_GLOBAL
#@@QUERY TARGET_PROP tool_alias ALIASED_TARGET
#@@QUERY TARGET_PROP tool_alias ALIAS_GLOBAL
#@@QUERY TARGET_PROP tool_alias TYPE
add_executable(tool IMPORTED GLOBAL)
add_executable(tool_alias ALIAS tool)
#@@ENDCASE

#@@CASE add_library_default_type_imported_and_interface_surface
#@@OUTCOME SUCCESS
#@@FILE auto.c
#@@FILE base.c
#@@QUERY TARGET_EXISTS auto_lib
#@@QUERY TARGET_EXISTS imp_lib
#@@QUERY TARGET_EXISTS base_alias
#@@QUERY TARGET_EXISTS iface
#@@QUERY TARGET_PROP auto_lib TYPE
#@@QUERY TARGET_PROP imp_lib TYPE
#@@QUERY TARGET_PROP imp_lib IMPORTED
#@@QUERY TARGET_PROP imp_lib IMPORTED_GLOBAL
#@@QUERY TARGET_PROP base_alias ALIASED_TARGET
#@@QUERY TARGET_PROP base_alias TYPE
#@@QUERY TARGET_PROP iface TYPE
set(BUILD_SHARED_LIBS ON)
add_library(auto_lib auto.c)
add_library(imp_lib UNKNOWN IMPORTED GLOBAL)
add_library(base_lib STATIC base.c)
add_library(base_alias ALIAS base_lib)
add_library(iface INTERFACE EXCLUDE_FROM_ALL)
#@@ENDCASE

#@@CASE add_executable_alias_of_alias_errors
#@@OUTCOME ERROR
add_executable(tool IMPORTED GLOBAL)
add_executable(tool_alias ALIAS tool)
add_executable(tool_alias2 ALIAS tool_alias)
#@@ENDCASE

#@@CASE add_library_imported_requires_explicit_type
#@@OUTCOME ERROR
add_library(bad_import IMPORTED)
#@@ENDCASE
