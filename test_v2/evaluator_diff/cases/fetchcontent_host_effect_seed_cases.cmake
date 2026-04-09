#@@CASE fetchcontent_local_materialization_surface
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/fc_saved_dep/CMakeLists.txt
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/from_saved.txt" "saved-subdir\n")
#@@END_FILE_TEXT
#@@FILE_TEXT source/fc_archive_src/CMakeLists.txt
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/from_archive.txt" "archive-subdir\n")
#@@END_FILE_TEXT
include(FetchContent)
set(FETCHCONTENT_BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}/fc_base")
set(FC_ARCHIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/fc_archive_dep.tar")
file(ARCHIVE_CREATE
  OUTPUT "${FC_ARCHIVE_PATH}"
  PATHS "${CMAKE_CURRENT_SOURCE_DIR}/fc_archive_src/CMakeLists.txt"
  FORMAT paxr
  MTIME 0)
file(SHA256 "${FC_ARCHIVE_PATH}" FC_ARCHIVE_HASH)
FetchContent_Declare(SavedDep
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fc_saved_dep")
FetchContent_Declare(ArchiveDep
  URL "${FC_ARCHIVE_PATH}"
  URL_HASH "SHA256=${FC_ARCHIVE_HASH}")
FetchContent_MakeAvailable(SavedDep ArchiveDep)
FetchContent_MakeAvailable(SavedDep ArchiveDep)
#@@ENDCASE

#@@CASE fetchcontent_host_effect_local_archive_makeavailable_surface
#@@OUTCOME SUCCESS
#@@QUERY VAR URL_POP
#@@QUERY VAR URL_VALUE
#@@QUERY TARGET_EXISTS url_from_fetch
#@@QUERY FILE_EXISTS build/fc_base/urldep-src/CMakeLists.txt
#@@QUERY FILE_EXISTS build/fc_base/urldep-src/value.txt
file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/fc_url_root")
file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/fc_url_root/CMakeLists.txt" "add_library(url_from_fetch INTERFACE)\n")
file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/fc_url_root/value.txt" "archive-local\n")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E tar cf "${CMAKE_CURRENT_BINARY_DIR}/fc_url_dep.tar" "."
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/fc_url_root"
  COMMAND_ERROR_IS_FATAL ANY)
include(FetchContent)
set(FETCHCONTENT_BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}/fc_base")
file(SHA256 "${CMAKE_CURRENT_BINARY_DIR}/fc_url_dep.tar" FC_URL_HASH)
FetchContent_Declare(UrlDep
  URL "${CMAKE_CURRENT_BINARY_DIR}/fc_url_dep.tar"
  URL_HASH "SHA256=${FC_URL_HASH}")
FetchContent_MakeAvailable(UrlDep)
FetchContent_GetProperties(UrlDep SOURCE_DIR URL_SRC BINARY_DIR URL_BIN POPULATED URL_POP)
file(READ "${urldep_SOURCE_DIR}/value.txt" URL_VALUE)
#@@ENDCASE

#@@CASE fetchcontent_host_effect_custom_download_and_patch_surface
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/fc_custom_seed/CMakeLists.txt
add_library(custom_download_original INTERFACE)
#@@END_FILE_TEXT
#@@FILE_TEXT source/fc_custom_seed/version.txt
custom-download
#@@END_FILE_TEXT
#@@FILE_TEXT source/fc_custom_patch/CMakeLists.txt
add_library(custom_download_patched INTERFACE)
#@@END_FILE_TEXT
#@@QUERY VAR CUSTOM_POP
#@@QUERY FILE_TEXT build/fc_custom_src/version.txt
#@@QUERY TARGET_EXISTS custom_download_patched
include(FetchContent)
set(FETCHCONTENT_BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}/fc_custom_base")
FetchContent_Declare(CustomDownload
  DOWNLOAD_COMMAND "${CMAKE_COMMAND}" -E copy_directory
                   "${CMAKE_CURRENT_SOURCE_DIR}/fc_custom_seed"
                   "${CMAKE_CURRENT_BINARY_DIR}/fc_custom_src"
  PATCH_COMMAND "${CMAKE_COMMAND}" -E copy
                "${CMAKE_CURRENT_SOURCE_DIR}/fc_custom_patch/CMakeLists.txt"
                "${CMAKE_CURRENT_BINARY_DIR}/fc_custom_src/CMakeLists.txt"
  SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/fc_custom_src")
FetchContent_MakeAvailable(CustomDownload)
FetchContent_GetProperties(CustomDownload POPULATED CUSTOM_POP)
#@@ENDCASE

#@@CASE fetchcontent_host_effect_saved_details_and_idempotence_surface
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/fc_saved_dep/CMakeLists.txt
add_library(saved_dep_lib INTERFACE)
#@@END_FILE_TEXT
#@@FILE_TEXT source/fc_saved_dep/value.txt
saved-details
#@@END_FILE_TEXT
#@@QUERY VAR SAVED_POP
#@@QUERY VAR SAVED_VALUE
#@@QUERY TARGET_EXISTS saved_dep_lib
include(FetchContent)
set(FETCHCONTENT_BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}/fc_saved_base")
FetchContent_Declare(SavedDep
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fc_saved_dep")
FetchContent_MakeAvailable(SavedDep)
FetchContent_MakeAvailable(SavedDep)
FetchContent_GetProperties(SavedDep SOURCE_DIR SAVED_SRC BINARY_DIR SAVED_BIN POPULATED SAVED_POP)
file(READ "${saveddep_SOURCE_DIR}/value.txt" SAVED_VALUE)
#@@ENDCASE

#@@CASE fetchcontent_host_effect_invalid_forms
#@@OUTCOME ERROR
include(FetchContent)
FetchContent_MakeAvailable()
FetchContent_GetProperties()
FetchContent_Populate()
#@@ENDCASE
