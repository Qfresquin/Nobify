#@@CASE dependency_provider_find_package_surface
#@@PROJECT_LAYOUT RAW_CMAKELISTS
#@@OUTCOME SUCCESS
#@@FILE_TEXT provider_top_find_package.cmake
function(dep_provider method dep_name)
  get_property(_provider_log GLOBAL PROPERTY PROVIDER_LOG)
  list(APPEND _provider_log "${method}:${dep_name}")
  set_property(GLOBAL PROPERTY PROVIDER_LOG "${_provider_log}")
  if(method STREQUAL "FIND_PACKAGE")
    if(dep_name STREQUAL "ProvidedPkg")
      set(${dep_name}_FOUND TRUE PARENT_SCOPE)
      set(${dep_name}_CONFIG provider://ProvidedPkg PARENT_SCOPE)
      set(PROVIDED_BY_PROVIDER yes PARENT_SCOPE)
    else()
      find_package(${ARGV1} ${ARGN} BYPASS_PROVIDER QUIET)
    endif()
  endif()
endfunction()
cmake_language(SET_DEPENDENCY_PROVIDER dep_provider SUPPORTED_METHODS FIND_PACKAGE)
#@@END_FILE_TEXT
#@@FILE_TEXT provider_root/FallbackPkgConfig.cmake
set(FallbackPkg_FOUND 1)
set(FallbackPkg_CONFIG_HIT 1)
#@@END_FILE_TEXT
#@@QUERY VAR PROVIDED_BY_PROVIDER
#@@QUERY VAR ProvidedPkg_FOUND_BOOL
#@@QUERY VAR ProvidedPkg_CONFIG
#@@QUERY VAR FallbackPkg_FOUND_BOOL
#@@QUERY VAR FallbackPkg_CONFIG_HIT
#@@QUERY VAR PROVIDER_LOG
cmake_minimum_required(VERSION 3.28)
set(CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/provider_root")
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/provider_top_find_package.cmake")
project(ProviderFindPackage LANGUAGES NONE)
find_package(ProvidedPkg QUIET)
find_package(FallbackPkg CONFIG QUIET)
if(ProvidedPkg_FOUND)
  set(ProvidedPkg_FOUND_BOOL yes)
else()
  set(ProvidedPkg_FOUND_BOOL no)
endif()
if(FallbackPkg_FOUND)
  set(FallbackPkg_FOUND_BOOL yes)
else()
  set(FallbackPkg_FOUND_BOOL no)
endif()
get_property(PROVIDER_LOG GLOBAL PROPERTY PROVIDER_LOG)

#@@ENDCASE

#@@CASE dependency_provider_fetchcontent_surface
#@@PROJECT_LAYOUT RAW_CMAKELISTS
#@@OUTCOME SUCCESS
#@@FILE_TEXT provider_top_fetchcontent.cmake
function(dep_provider method dep_name)
  get_property(_provider_log GLOBAL PROPERTY PROVIDER_LOG)
  list(APPEND _provider_log "${method}:${dep_name}")
  set_property(GLOBAL PROPERTY PROVIDER_LOG "${_provider_log}")
  if(method STREQUAL "FETCHCONTENT_MAKEAVAILABLE_SERIAL")
    if(dep_name STREQUAL "ProvidedDep")
      FetchContent_SetPopulated(${dep_name}
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/provided_dep_src"
        BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/provided_dep_build")
    elseif(dep_name STREQUAL "LocalDep")
      FetchContent_MakeAvailable(${dep_name})
    endif()
  endif()
endfunction()
cmake_language(SET_DEPENDENCY_PROVIDER dep_provider SUPPORTED_METHODS FETCHCONTENT_MAKEAVAILABLE_SERIAL)
#@@END_FILE_TEXT
#@@FILE_TEXT provided_dep_src/CMakeLists.txt
add_library(provided_from_fetch INTERFACE)
#@@END_FILE_TEXT
#@@FILE_TEXT fetchcontent_local/CMakeLists.txt
add_library(local_from_fetch INTERFACE)
#@@END_FILE_TEXT
#@@QUERY VAR PROVIDER_LOG
#@@QUERY VAR PROVIDED_POPULATED
#@@QUERY VAR LOCAL_POPULATED
#@@QUERY VAR provideddep_POPULATED
#@@QUERY TARGET_EXISTS local_from_fetch
cmake_minimum_required(VERSION 3.28)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/provider_top_fetchcontent.cmake")
project(ProviderFetchContent LANGUAGES NONE)
include(FetchContent)
FetchContent_Declare(ProvidedDep)
FetchContent_Declare(LocalDep
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fetchcontent_local"
  BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/fetchcontent_local_build")
FetchContent_MakeAvailable(ProvidedDep LocalDep)
FetchContent_GetProperties(ProvidedDep POPULATED PROVIDED_POPULATED)
FetchContent_GetProperties(LocalDep POPULATED LOCAL_POPULATED)
get_property(PROVIDER_LOG GLOBAL PROPERTY PROVIDER_LOG)

#@@ENDCASE

#@@CASE dependency_provider_first_project_only_surface
#@@PROJECT_LAYOUT RAW_CMAKELISTS
#@@OUTCOME SUCCESS
#@@FILE_TEXT provider_top_once.cmake
if(DEFINED TOP_LEVEL_PROVIDER_INCLUDE_COUNT)
  math(EXPR TOP_LEVEL_PROVIDER_INCLUDE_COUNT "${TOP_LEVEL_PROVIDER_INCLUDE_COUNT} + 1")
else()
  set(TOP_LEVEL_PROVIDER_INCLUDE_COUNT 1)
endif()
function(dep_provider_once method dep_name)
  get_property(_provider_log GLOBAL PROPERTY PROVIDER_LOG)
  list(APPEND _provider_log "${method}:${dep_name}")
  set_property(GLOBAL PROPERTY PROVIDER_LOG "${_provider_log}")
  if(method STREQUAL "FIND_PACKAGE" AND dep_name STREQUAL "OnlyOncePkg")
    set(${dep_name}_FOUND TRUE PARENT_SCOPE)
    set(ONLY_ONCE_PROVIDER yes PARENT_SCOPE)
  endif()
endfunction()
cmake_language(SET_DEPENDENCY_PROVIDER dep_provider_once SUPPORTED_METHODS FIND_PACKAGE)
#@@END_FILE_TEXT
#@@QUERY VAR TOP_LEVEL_PROVIDER_INCLUDE_COUNT
#@@QUERY VAR ONLY_ONCE_PROVIDER
#@@QUERY VAR OnlyOncePkg_FOUND_BOOL
#@@QUERY VAR PROVIDER_LOG
cmake_minimum_required(VERSION 3.28)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/provider_top_once.cmake")
project(ProviderFirst LANGUAGES NONE)
project(ProviderSecond LANGUAGES NONE)
find_package(OnlyOncePkg QUIET)
if(OnlyOncePkg_FOUND)
  set(OnlyOncePkg_FOUND_BOOL yes)
else()
  set(OnlyOncePkg_FOUND_BOOL no)
endif()
get_property(PROVIDER_LOG GLOBAL PROPERTY PROVIDER_LOG)
#@@ENDCASE

#@@CASE dependency_provider_invalid_registration_forms
#@@PROJECT_LAYOUT RAW_CMAKELISTS
#@@OUTCOME ERROR
#@@FILE_TEXT provider_invalid_top.cmake
macro(dep_provider_bad method)
endmacro()
function(dep_provider_scope)
  cmake_language(SET_DEPENDENCY_PROVIDER dep_provider_bad SUPPORTED_METHODS FIND_PACKAGE)
endfunction()
dep_provider_scope()
cmake_language(SET_DEPENDENCY_PROVIDER missing_provider SUPPORTED_METHODS FIND_PACKAGE)
cmake_language(SET_DEPENDENCY_PROVIDER dep_provider_bad SUPPORTED_METHODS BAD_METHOD)
#@@END_FILE_TEXT
cmake_minimum_required(VERSION 3.28)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/provider_invalid_top.cmake")
project(ProviderInvalid LANGUAGES NONE)
cmake_language(SET_DEPENDENCY_PROVIDER dep_provider_bad SUPPORTED_METHODS FIND_PACKAGE)
#@@ENDCASE
