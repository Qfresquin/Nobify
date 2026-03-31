#@@CASE find_pathlike_local_tree_surface
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/find_items/nested/marker.txt
x
#@@END_FILE_TEXT
#@@FILE_TEXT source/find_items/include/marker.hpp
x
#@@END_FILE_TEXT
#@@FILE_TEXT source/find_items/lib/libsample.a
x
#@@END_FILE_TEXT
#@@FILE_TEXT source/find_items/lib/sample.lib
x
#@@END_FILE_TEXT
#@@QUERY VAR MY_FILE_NORM
#@@QUERY VAR MY_PATH_NORM
#@@QUERY VAR MY_LIB_NORM
#@@QUERY VAR MY_TOOL_NORM
set(FIND_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/find_items")
find_file(MY_FILE NAMES marker.txt HINTS "${FIND_ROOT}" PATH_SUFFIXES nested NO_DEFAULT_PATH NO_CACHE)
find_path(MY_PATH NAMES marker.hpp PATHS "${FIND_ROOT}" PATH_SUFFIXES include NO_DEFAULT_PATH)
find_library(MY_LIB NAMES sample HINTS "${FIND_ROOT}" PATH_SUFFIXES lib NO_DEFAULT_PATH)
get_filename_component(CMAKE_TOOL_DIR "${CMAKE_COMMAND}" DIRECTORY)
get_filename_component(CMAKE_TOOL_NAME "${CMAKE_COMMAND}" NAME)
find_program(MY_TOOL NAMES "${CMAKE_TOOL_NAME}" PATHS "${CMAKE_TOOL_DIR}" NO_DEFAULT_PATH NO_CACHE)
string(REPLACE "\\" "/" MY_FILE_NORM "${MY_FILE}")
string(REPLACE "\\" "/" MY_PATH_NORM "${MY_PATH}")
string(REPLACE "\\" "/" MY_LIB_NORM "${MY_LIB}")
string(REPLACE "\\" "/" MY_TOOL_NORM "${MY_TOOL}")
#@@ENDCASE

#@@CASE find_pathlike_invalid_form_is_rejected
#@@OUTCOME ERROR
find_file(FOUND_MARKER)
#@@ENDCASE

#@@CASE find_pathlike_env_and_path_surface
#@@OUTCOME SUCCESS
#@@PROJECT_LAYOUT RAW_CMAKELISTS
#@@ENV_PATH FIND_HINT_ROOT source/env_root
#@@FILE_TEXT source/env_root/files/env-marker.txt
env
#@@END_FILE_TEXT
#@@FILE_TEXT source/path_root/bin/env-only-tool
#!/bin/sh
exit 0
#@@END_FILE_TEXT
#@@QUERY VAR ENV_FILE_NORM
#@@QUERY VAR PATH_TOOL_NORM
cmake_minimum_required(VERSION 3.28)
project(FindPathEnv LANGUAGES NONE)
file(CHMOD "${CMAKE_CURRENT_SOURCE_DIR}/path_root/bin/env-only-tool"
     PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
set(ENV{PATH} "${CMAKE_CURRENT_SOURCE_DIR}/path_root/bin")
find_file(ENV_FILE NAMES env-marker.txt HINTS ENV FIND_HINT_ROOT PATH_SUFFIXES files NO_DEFAULT_PATH)
find_program(PATH_TOOL NAMES env-only-tool)
string(REPLACE "\\" "/" ENV_FILE_NORM "${ENV_FILE}")
string(REPLACE "\\" "/" PATH_TOOL_NORM "${PATH_TOOL}")
#@@ENDCASE

#@@CASE find_pathlike_prefix_and_install_prefix_surface
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/prefix_root/include/prefix-marker.h
prefix
#@@END_FILE_TEXT
#@@FILE_TEXT source/prefix_root/lib/libprefixsample.a
prefix
#@@END_FILE_TEXT
#@@FILE_TEXT source/prefix_root/lib/prefixsample.lib
prefix
#@@END_FILE_TEXT
#@@FILE_TEXT source/install_prefix_root/include/install-marker.h
install
#@@END_FILE_TEXT
#@@QUERY VAR PREFIX_INCLUDE_NORM
#@@QUERY VAR PREFIX_LIB_NORM
#@@QUERY VAR INSTALL_INCLUDE_NORM
set(CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/prefix_root")
set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/install_prefix_root")
find_path(PREFIX_INCLUDE NAMES prefix-marker.h)
find_library(PREFIX_LIB NAMES prefixsample)
find_path(INSTALL_INCLUDE NAMES install-marker.h)
string(REPLACE "\\" "/" PREFIX_INCLUDE_NORM "${PREFIX_INCLUDE}")
string(REPLACE "\\" "/" PREFIX_LIB_NORM "${PREFIX_LIB}")
string(REPLACE "\\" "/" INSTALL_INCLUDE_NORM "${INSTALL_INCLUDE}")
#@@ENDCASE
