#@@CASE find_package_module_config_and_components_surface
#@@PROJECT_LAYOUT RAW_CMAKELISTS
#@@OUTCOME SUCCESS
#@@FILE_TEXT module/FindModulePkg.cmake
set(ModulePkg_FOUND 1)
set(ModulePkg_MODE_HINT MODULE)
set(ModulePkg_alpha_FOUND 1)
set(ModulePkg_beta_FOUND 0)
#@@END_FILE_TEXT
#@@FILE_TEXT prefix/lib/cmake/ConfigPkg/ConfigPkgConfig.cmake
set(ConfigPkg_FOUND 1)
set(ConfigPkg_MODE_HINT CONFIG)
set(ConfigPkg_DIR "${CMAKE_CURRENT_LIST_DIR}")
foreach(_comp IN LISTS ConfigPkg_FIND_COMPONENTS)
  if(_comp STREQUAL "core")
    set(ConfigPkg_core_FOUND 1)
  elseif(_comp STREQUAL "gui")
    set(ConfigPkg_gui_FOUND 0)
  endif()
endforeach()
#@@END_FILE_TEXT
#@@FILE_TEXT prefix/lib/cmake/ConfigPkg/ConfigPkgConfigVersion.cmake
set(PACKAGE_VERSION "1.2.0")
set(PACKAGE_VERSION_COMPATIBLE TRUE)
set(PACKAGE_VERSION_EXACT TRUE)
#@@END_FILE_TEXT
#@@FILE_TEXT prefix/lib/cmake/RejectPkg/RejectPkgConfig.cmake
set(RejectPkg_FOUND 1)
set(RejectPkg_MODE_HINT CONFIG)
set(RejectPkg_DIR "${CMAKE_CURRENT_LIST_DIR}")
#@@END_FILE_TEXT
#@@FILE_TEXT prefix/lib/cmake/RejectPkg/RejectPkgConfigVersion.cmake
set(PACKAGE_VERSION "1.0.0")
set(PACKAGE_VERSION_COMPATIBLE FALSE)
#@@END_FILE_TEXT
#@@QUERY FILE_TEXT build/__oracle/find_package_report.txt
cmake_minimum_required(VERSION 3.28)
project(FindPackageSpecialOne LANGUAGES NONE)
include("${CMAKE_CURRENT_SOURCE_DIR}/__nob_diff_helpers/__nob_diff_find_package_oracle.cmake")
set(_report "${CMAKE_BINARY_DIR}/__oracle/find_package_report.txt")
nob_diff_pkg_reset_report("${_report}")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/module")
set(CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/prefix")
find_package(ModulePkg QUIET COMPONENTS alpha beta)
find_package(ConfigPkg 1.2 EXACT QUIET CONFIG COMPONENTS core gui)
find_package(RejectPkg 2.0 EXACT QUIET CONFIG)
set(ConfigPkg_MODE_HINT CONFIG)
set(ConfigPkg_DIR "${CMAKE_CURRENT_SOURCE_DIR}/prefix/lib/cmake/ConfigPkg")
nob_diff_pkg_capture("${_report}" ModulePkg alpha beta)
nob_diff_pkg_capture("${_report}" ConfigPkg core gui)
nob_diff_pkg_capture("${_report}" RejectPkg)
nob_diff_report_append_kv("${_report}" "PACKAGES_FOUND" "ConfigPkg;ModulePkg")
nob_diff_report_append_kv("${_report}" "PACKAGES_NOT_FOUND" "RejectPkg")
#@@ENDCASE

#@@CASE find_package_path_controls_and_root_policy_surface
#@@PROJECT_LAYOUT RAW_CMAKELISTS
#@@OUTCOME SUCCESS
#@@FILE_TEXT prefix_old/CmpOldConfig.cmake
set(CmpOld_FOUND 1)
set(CmpOld_MODE_HINT CONFIG)
set(CmpOld_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(CmpOld_ORIGIN prefix)
#@@END_FILE_TEXT
#@@FILE_TEXT root_old/CmpOldConfig.cmake
set(CmpOld_FOUND 1)
set(CmpOld_MODE_HINT CONFIG)
set(CmpOld_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(CmpOld_ORIGIN root)
#@@END_FILE_TEXT
#@@FILE_TEXT prefix_new/CmpNewConfig.cmake
set(CmpNew_FOUND 1)
set(CmpNew_MODE_HINT CONFIG)
set(CmpNew_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(CmpNew_ORIGIN prefix)
#@@END_FILE_TEXT
#@@FILE_TEXT root_new/CmpNewConfig.cmake
set(CmpNew_FOUND 1)
set(CmpNew_MODE_HINT CONFIG)
set(CmpNew_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(CmpNew_ORIGIN root)
#@@END_FILE_TEXT
#@@FILE_TEXT explicit_root/ExplicitPkgConfig.cmake
set(ExplicitPkg_FOUND 1)
set(ExplicitPkg_MODE_HINT CONFIG)
set(ExplicitPkg_DIR "${CMAKE_CURRENT_LIST_DIR}")
#@@END_FILE_TEXT
#@@FILE_TEXT env_root/EnvOnlyPkgConfig.cmake
set(EnvOnlyPkg_FOUND 1)
set(EnvOnlyPkg_MODE_HINT CONFIG)
set(EnvOnlyPkg_DIR "${CMAKE_CURRENT_LIST_DIR}")
#@@END_FILE_TEXT
#@@FILE_TEXT no_cmake_path_root/NoCmakePathPkgConfig.cmake
set(NoCmakePathPkg_FOUND 1)
set(NoCmakePathPkg_MODE_HINT CONFIG)
set(NoCmakePathPkg_DIR "${CMAKE_CURRENT_LIST_DIR}")
#@@END_FILE_TEXT
#@@ENV CMAKE_PREFIX_PATH=
#@@QUERY FILE_TEXT build/__oracle/find_package_report.txt
cmake_minimum_required(VERSION 3.28)
project(FindPackageSpecialTwo LANGUAGES NONE)
include("${CMAKE_CURRENT_SOURCE_DIR}/__nob_diff_helpers/__nob_diff_find_package_oracle.cmake")
set(_report "${CMAKE_BINARY_DIR}/__oracle/find_package_report.txt")
nob_diff_pkg_reset_report("${_report}")
set(CMAKE_PREFIX_PATH
  "${CMAKE_CURRENT_SOURCE_DIR}/prefix_old;${CMAKE_CURRENT_SOURCE_DIR}/prefix_new;${CMAKE_CURRENT_SOURCE_DIR}/no_cmake_path_root")
set(ENV{CMAKE_PREFIX_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/env_root")
set(CmpOld_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/root_old")
cmake_policy(SET CMP0074 OLD)
find_package(CmpOld QUIET CONFIG)
set(CmpOld_VERSION_OK "${CmpOld_FOUND}")
set(CmpOld_MODE_HINT CONFIG)
set(CmpOld_DIR "${CMAKE_CURRENT_SOURCE_DIR}/prefix_old")
set(CmpNew_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/root_new")
cmake_policy(SET CMP0074 NEW)
find_package(CmpNew QUIET CONFIG)
set(CmpNew_VERSION_OK "${CmpNew_FOUND}")
set(CmpNew_MODE_HINT CONFIG)
set(CmpNew_DIR "${CMAKE_CURRENT_SOURCE_DIR}/root_new")
find_package(ExplicitPkg QUIET CONFIG NO_DEFAULT_PATH PATHS "${CMAKE_CURRENT_SOURCE_DIR}/explicit_root")
set(ExplicitPkg_MODE_HINT CONFIG)
set(ExplicitPkg_DIR "${CMAKE_CURRENT_SOURCE_DIR}/explicit_root")
find_package(EnvOnlyPkg QUIET CONFIG)
set(EnvOnlyPkg_MODE_HINT CONFIG)
set(EnvOnlyPkg_DIR "${CMAKE_CURRENT_SOURCE_DIR}/env_root")
find_package(NoCmakePathPkg QUIET CONFIG NO_CMAKE_PATH)
nob_diff_pkg_capture("${_report}" CmpOld)
nob_diff_pkg_capture("${_report}" CmpNew)
nob_diff_pkg_capture("${_report}" ExplicitPkg)
nob_diff_pkg_capture("${_report}" EnvOnlyPkg)
nob_diff_pkg_capture("${_report}" NoCmakePathPkg)
nob_diff_report_append_kv("${_report}" "PACKAGES_FOUND" "CmpNew;CmpOld;EnvOnlyPkg;ExplicitPkg")
nob_diff_report_append_kv("${_report}" "PACKAGES_NOT_FOUND" "NoCmakePathPkg")
#@@ENDCASE

#@@CASE find_package_redirect_and_registry_surface
#@@PROJECT_LAYOUT RAW_CMAKELISTS
#@@OUTCOME SUCCESS
#@@ENV_PATH HOME build/home
#@@ENV_PATH XDG_DATA_HOME build/xdg_data
#@@ENV_PATH USERPROFILE build/home
#@@FILE_TEXT redirect_src/CMakeLists.txt
set(RedirectPkg_FOUND 1)
add_library(redirect_pkg_target INTERFACE)
#@@END_FILE_TEXT
#@@QUERY FILE_TEXT build/__oracle/find_package_report.txt
cmake_minimum_required(VERSION 3.28)
project(FindPackageSpecialThree LANGUAGES NONE)
include("${CMAKE_CURRENT_SOURCE_DIR}/__nob_diff_helpers/__nob_diff_find_package_oracle.cmake")
include(FetchContent)
set(_report "${CMAKE_BINARY_DIR}/__oracle/find_package_report.txt")
nob_diff_pkg_reset_report("${_report}")
FetchContent_Declare(RedirectPkg
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/redirect_src"
  OVERRIDE_FIND_PACKAGE)
FetchContent_MakeAvailable(RedirectPkg)
find_package(RedirectPkg CONFIG QUIET)
set(RedirectPkg_MODE_HINT REDIRECT)
set(RedirectPkg_DIR "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/RegPkgConfig.cmake"
  "set(RegPkg_FOUND 1)\n"
  "set(RegPkg_MODE_HINT REGISTRY)\n"
  "set(RegPkg_DIR \\\"${CMAKE_CURRENT_LIST_DIR}\\\")\n")
cmake_policy(SET CMP0090 NEW)
set(CMAKE_EXPORT_PACKAGE_REGISTRY TRUE)
export(PACKAGE RegPkg)
find_package(RegPkg CONFIG QUIET)
set(RegPkg_MODE_HINT REGISTRY)
set(RegPkg_DIR "${CMAKE_CURRENT_BINARY_DIR}")
nob_diff_pkg_capture("${_report}" RedirectPkg)
nob_diff_pkg_capture("${_report}" RegPkg)
nob_diff_report_append_kv("${_report}" "PACKAGES_FOUND" "RedirectPkg;RegPkg")
nob_diff_report_append_kv("${_report}" "PACKAGES_NOT_FOUND" "")
if(EXISTS "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/redirectpkg-config.cmake")
  set(REDIRECT_CONFIG 1)
else()
  set(REDIRECT_CONFIG 0)
endif()
file(GLOB _regpkg_entries "$ENV{HOME}/.cmake/packages/RegPkg/*")
list(LENGTH _regpkg_entries REGISTRY_ENTRY_COUNT)
nob_diff_report_append_kv("${_report}" "REDIRECT_CONFIG" "${REDIRECT_CONFIG}")
nob_diff_report_append_kv("${_report}" "REGISTRY_ENTRY_COUNT" "${REGISTRY_ENTRY_COUNT}")
#@@ENDCASE

#@@CASE find_package_provider_interop_surface
#@@PROJECT_LAYOUT RAW_CMAKELISTS
#@@OUTCOME SUCCESS
#@@FILE_TEXT provider_top.cmake
function(dep_provider method dep_name)
  if(method STREQUAL "FIND_PACKAGE")
    if(dep_name STREQUAL "ProvidedPkg")
      set(${dep_name}_FOUND 1 PARENT_SCOPE)
      set(${dep_name}_CONFIG provider://ProvidedPkg PARENT_SCOPE)
    else()
      find_package(${ARGV1} ${ARGN} BYPASS_PROVIDER QUIET)
    endif()
  endif()
endfunction()
cmake_language(SET_DEPENDENCY_PROVIDER dep_provider SUPPORTED_METHODS FIND_PACKAGE)
#@@END_FILE_TEXT
#@@FILE_TEXT provider_root/FallbackPkgConfig.cmake
set(FallbackPkg_FOUND 1)
set(FallbackPkg_MODE_HINT CONFIG)
set(FallbackPkg_DIR "${CMAKE_CURRENT_LIST_DIR}")
#@@END_FILE_TEXT
#@@QUERY FILE_TEXT build/__oracle/find_package_report.txt
cmake_minimum_required(VERSION 3.28)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/provider_top.cmake")
project(FindPackageSpecialFour LANGUAGES NONE)
include("${CMAKE_CURRENT_SOURCE_DIR}/__nob_diff_helpers/__nob_diff_find_package_oracle.cmake")
set(_report "${CMAKE_BINARY_DIR}/__oracle/find_package_report.txt")
nob_diff_pkg_reset_report("${_report}")
set(CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/provider_root")
find_package(ProvidedPkg QUIET)
find_package(FallbackPkg CONFIG QUIET)
set(FallbackPkg_MODE_HINT CONFIG)
set(FallbackPkg_DIR "${CMAKE_CURRENT_SOURCE_DIR}/provider_root")
nob_diff_pkg_capture("${_report}" ProvidedPkg)
nob_diff_pkg_capture("${_report}" FallbackPkg)
nob_diff_report_append_kv("${_report}" "PACKAGES_FOUND" "FallbackPkg;ProvidedPkg")
nob_diff_report_append_kv("${_report}" "PACKAGES_NOT_FOUND" "")
#@@ENDCASE

#@@CASE find_package_invalid_forms
#@@PROJECT_LAYOUT RAW_CMAKELISTS
#@@OUTCOME ERROR
cmake_minimum_required(VERSION 3.28)
project(FindPackageSpecialInvalid LANGUAGES NONE)
find_package()
find_package(BadPkg BYPASS_PROVIDER QUIET)
#@@ENDCASE
