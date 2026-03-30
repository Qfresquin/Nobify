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
