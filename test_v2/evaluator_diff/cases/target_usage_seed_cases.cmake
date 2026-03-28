#@@CASE target_compile_definitions_split_direct_and_interface
#@@OUTCOME SUCCESS
#@@FILE usage.c
#@@QUERY TARGET_PROP usage COMPILE_DEFINITIONS
#@@QUERY TARGET_PROP usage INTERFACE_COMPILE_DEFINITIONS
add_library(usage STATIC usage.c)
target_compile_definitions(usage PRIVATE LOCAL_DEF PUBLIC PUBLIC_DEF INTERFACE IFACE_DEF)
#@@ENDCASE

#@@CASE target_compile_definitions_requires_visibility
#@@OUTCOME ERROR
#@@FILE usage.c
add_library(usage STATIC usage.c)
target_compile_definitions(usage LOCAL_DEF)
#@@ENDCASE

#@@CASE target_compile_definitions_imported_private_forbidden
#@@OUTCOME ERROR
add_library(imported STATIC IMPORTED)
target_compile_definitions(imported PRIVATE LOCAL_DEF)
#@@ENDCASE

#@@CASE target_include_directories_system_projects_interface_system_property
#@@OUTCOME SUCCESS
#@@FILE usage.c
#@@QUERY TARGET_PROP usage INCLUDE_DIRECTORIES
#@@QUERY TARGET_PROP usage INTERFACE_INCLUDE_DIRECTORIES
#@@QUERY TARGET_PROP usage INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
add_library(usage STATIC usage.c)
target_include_directories(usage SYSTEM PUBLIC include/pub INTERFACE include/iface PRIVATE include/local)
#@@ENDCASE

#@@CASE target_sources_file_set_headers_projects_include_dirs
#@@OUTCOME SUCCESS
#@@FILE usage.c
#@@FILE include/public.hpp
#@@FILE include/detail.hpp
#@@QUERY TARGET_PROP usage HEADER_SETS
#@@QUERY TARGET_PROP usage HEADER_SET
#@@QUERY TARGET_PROP usage HEADER_DIRS
#@@QUERY TARGET_PROP usage INCLUDE_DIRECTORIES
#@@QUERY TARGET_PROP usage INTERFACE_INCLUDE_DIRECTORIES
add_library(usage STATIC usage.c)
target_sources(usage PUBLIC FILE_SET HEADERS BASE_DIRS include FILES include/public.hpp include/detail.hpp)
#@@ENDCASE

#@@CASE target_compile_options_before_prepends_properties
#@@OUTCOME SUCCESS
#@@FILE usage.c
#@@QUERY TARGET_PROP usage COMPILE_OPTIONS
#@@QUERY TARGET_PROP usage INTERFACE_COMPILE_OPTIONS
add_library(usage STATIC usage.c)
target_compile_options(usage PRIVATE A PUBLIC PUB_A)
target_compile_options(usage BEFORE PRIVATE B PUBLIC PUB_B INTERFACE IFACE_B)
#@@ENDCASE
