#@@CASE get_filename_component_path_modes_surface
#@@OUTCOME SUCCESS
#@@DIR gfc_real
#@@DIR gfc_real/sub
#@@FILE gfc_real/sub/file.txt
#@@QUERY VAR GFC_DIR
#@@QUERY VAR GFC_PATH
#@@QUERY VAR GFC_NAME
#@@QUERY VAR GFC_EXT
#@@QUERY VAR GFC_LAST_EXT
#@@QUERY VAR GFC_NAME_WE
#@@QUERY VAR GFC_NAME_WLE
#@@QUERY CACHE_DEFINED GFC_NAME_WLE
#@@QUERY VAR GFC_ABS_OK
#@@QUERY VAR GFC_REAL_OK
get_filename_component(GFC_DIR "a/b/c.tar.gz" DIRECTORY)
get_filename_component(GFC_PATH "a/b/" PATH)
get_filename_component(GFC_NAME "a/b/c.tar.gz" NAME)
get_filename_component(GFC_EXT "a/b/c.tar.gz" EXT)
get_filename_component(GFC_LAST_EXT "a/b/c.tar.gz" LAST_EXT)
get_filename_component(GFC_NAME_WE "a/b/c.tar.gz" NAME_WE)
get_filename_component(GFC_NAME_WLE "a/b/c.tar.gz" NAME_WLE CACHE)
get_filename_component(GFC_ABS "sub/file.txt" ABSOLUTE BASE_DIR "gfc_real")
get_filename_component(GFC_REAL "${CMAKE_CURRENT_SOURCE_DIR}/gfc_real/./sub/../sub/file.txt" REALPATH)
if(GFC_ABS STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/gfc_real/sub/file.txt")
  set(GFC_ABS_OK 1)
else()
  set(GFC_ABS_OK 0)
endif()
if(GFC_REAL STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/gfc_real/sub/file.txt")
  set(GFC_REAL_OK 1)
else()
  set(GFC_REAL_OK 0)
endif()
#@@ENDCASE

#@@CASE get_filename_component_program_and_cache_surface
#@@OUTCOME SUCCESS
#@@QUERY VAR GFC_PROG_OK
#@@QUERY VAR GFC_PROG_ARGS
#@@QUERY VAR GFC_SPACE_PROG_OK
#@@QUERY VAR GFC_SPACE_ARGS
#@@QUERY VAR GFC_MISSING_PROG
#@@QUERY VAR GFC_MISSING_ARGS
#@@QUERY VAR GFC_CACHE_HIT
#@@QUERY VAR GFC_CACHE_HIT_ARGS
#@@QUERY CACHE_DEFINED GFC_CACHE_HIT
#@@QUERY CACHE_DEFINED GFC_CACHE_HIT_ARGS
#@@QUERY VAR GFC_CACHE_PROG_OK
#@@QUERY VAR GFC_CACHE_PROG_ARGS
#@@QUERY CACHE_DEFINED GFC_CACHE_PROG
#@@QUERY CACHE_DEFINED GFC_CACHE_PROG_ARGS
file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/gfc spaced")
if(WIN32)
  set(_GFC_HELPER "${CMAKE_CURRENT_SOURCE_DIR}/gfc spaced/tool spaced.bat")
  file(WRITE "${_GFC_HELPER}" "@echo off\r\n")
else()
  set(_GFC_HELPER "${CMAKE_CURRENT_SOURCE_DIR}/gfc spaced/tool spaced.sh")
  file(WRITE "${_GFC_HELPER}" "#!/bin/sh\n")
  file(CHMOD "${_GFC_HELPER}" PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE)
endif()
set(GFC_SPACE_ARGS sentinel-space)
set(GFC_MISSING_ARGS sentinel-missing)
set(GFC_CACHE_HIT keep-me)
set(GFC_CACHE_HIT_ARGS keep-args)
get_filename_component(GFC_PROG "${CMAKE_COMMAND} -E true" PROGRAM PROGRAM_ARGS GFC_PROG_ARGS)
get_filename_component(GFC_SPACE_PROG "${_GFC_HELPER}" PROGRAM PROGRAM_ARGS GFC_SPACE_ARGS)
get_filename_component(GFC_CACHE_HIT "${CMAKE_COMMAND} -E true" PROGRAM PROGRAM_ARGS GFC_CACHE_HIT_ARGS CACHE)
get_filename_component(GFC_CACHE_PROG "${CMAKE_COMMAND} -E true" PROGRAM PROGRAM_ARGS GFC_CACHE_PROG_ARGS CACHE)
get_filename_component(GFC_MISSING_PROG "${CMAKE_CURRENT_SOURCE_DIR}/gfc_missing_program --flag" PROGRAM PROGRAM_ARGS GFC_MISSING_ARGS)
if(GFC_PROG STREQUAL "${CMAKE_COMMAND}")
  set(GFC_PROG_OK 1)
else()
  set(GFC_PROG_OK 0)
endif()
if(GFC_SPACE_PROG STREQUAL "${_GFC_HELPER}")
  set(GFC_SPACE_PROG_OK 1)
else()
  set(GFC_SPACE_PROG_OK 0)
endif()
if(GFC_CACHE_PROG STREQUAL "${CMAKE_COMMAND}")
  set(GFC_CACHE_PROG_OK 1)
else()
  set(GFC_CACHE_PROG_OK 0)
endif()
#@@ENDCASE

#@@CASE get_filename_component_invalid_option_shapes
#@@OUTCOME ERROR
get_filename_component(ONLY_TWO file)
get_filename_component(BAD_DIR a/b DIRECTORY EXTRA)
get_filename_component(BAD_ABS foo ABSOLUTE BASE_DIR)
get_filename_component(BAD_PROG foo PROGRAM PROGRAM_ARGS)
get_filename_component(BAD_MODE foo UNKNOWN)
#@@ENDCASE
