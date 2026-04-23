#@@CASE backend_build_controlled_artifacts
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/src/core.c
int core_value(void) { return 41; }
#@@END_FILE_TEXT
#@@FILE_TEXT source/src/main.c
int core_value(void);
int main(void) { return core_value() == 41 ? 0 : 1; }
#@@END_FILE_TEXT
add_library(core STATIC src/core.c)
set_target_properties(core PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/artifacts/lib")
add_executable(app src/main.c)
target_link_libraries(app PRIVATE core)
set_target_properties(app PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/artifacts/bin")
#@@ENDCASE

#@@CASE backend_install_supported_surface
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/src/core.c
int core_value(void) { return 51; }
#@@END_FILE_TEXT
#@@FILE_TEXT source/src/main.c
int core_value(void);
int main(void) { return core_value() == 51 ? 0 : 1; }
#@@END_FILE_TEXT
#@@FILE_TEXT source/include/core.h
#define CORE_VALUE 51
#@@END_FILE_TEXT
#@@FILE_TEXT source/scripts/helper.sh
#!/bin/sh
exit 0
#@@END_FILE_TEXT
#@@FILE_TEXT source/assets/asset.txt
asset
#@@END_FILE_TEXT
#@@FILE_TEXT source/README.txt
install readme
#@@END_FILE_TEXT
add_library(core STATIC src/core.c)
set_target_properties(core PROPERTIES PUBLIC_HEADER include/core.h)
add_executable(app src/main.c)
target_link_libraries(app PRIVATE core)
install(TARGETS app core EXPORT DemoTargets
  RUNTIME DESTINATION bin
  ARCHIVE DESTINATION lib
  PUBLIC_HEADER DESTINATION include/demo)
install(FILES README.txt DESTINATION share/demo)
install(PROGRAMS scripts/helper.sh DESTINATION bin)
install(DIRECTORY assets/ DESTINATION share/demo_assets)
install(EXPORT DemoTargets DESTINATION lib/cmake/Demo FILE DemoTargets.cmake NAMESPACE Demo::)
#@@ENDCASE

#@@CASE backend_package_supported_archives
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/src/core.c
int core_value(void) { return 61; }
#@@END_FILE_TEXT
#@@FILE_TEXT source/src/main.c
int core_value(void);
int main(void) { return core_value() == 61 ? 0 : 1; }
#@@END_FILE_TEXT
#@@FILE_TEXT source/include/core.h
#define CORE_VALUE 61
#@@END_FILE_TEXT
#@@FILE_TEXT source/README.txt
package readme
#@@END_FILE_TEXT
add_library(core STATIC src/core.c)
set_target_properties(core PROPERTIES PUBLIC_HEADER include/core.h)
add_executable(app src/main.c)
target_link_libraries(app PRIVATE core)
install(TARGETS app core EXPORT DemoTargets
  RUNTIME DESTINATION bin
  ARCHIVE DESTINATION lib
  PUBLIC_HEADER DESTINATION include/demo)
install(FILES README.txt DESTINATION share/demo)
install(EXPORT DemoTargets DESTINATION lib/cmake/Demo FILE DemoTargets.cmake NAMESPACE Demo::)
set(CPACK_GENERATOR "TGZ;TXZ;ZIP")
set(CPACK_PACKAGE_NAME "DemoPkg")
set(CPACK_PACKAGE_VERSION "1.2.3")
set(CPACK_PACKAGE_FILE_NAME "demo-pkg")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/packages")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
include(CPack)
#@@ENDCASE

#@@CASE backend_configure_generation_supported_surface
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/cfg_template.in
VALUE=@GEN_VALUE@
#@@END_FILE_TEXT
set(GEN_VALUE 42)
write_file("${CMAKE_CURRENT_BINARY_DIR}/generated/legacy.txt" legacy-body)
make_directory("${CMAKE_CURRENT_BINARY_DIR}/generated/legacy_dir/sub")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/write_append.txt" "alpha")
file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/generated/write_append.txt" "beta")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/file_dir/sub")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cfg_template.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/configured.txt" @ONLY)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/generated.txt" CONTENT "GEN")
#@@ENDCASE

#@@CASE backend_configure_host_effect_supported_surface
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/download_src.txt
download-body
#@@END_FILE_TEXT
#@@FILE_TEXT source/archive_input/root.txt
root
#@@END_FILE_TEXT
#@@FILE_TEXT source/archive_input/sub/child.txt
child
#@@END_FILE_TEXT
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/download_src.txt" DL_HASH)
file(DOWNLOAD
  "file://${CMAKE_CURRENT_SOURCE_DIR}/download_src.txt"
  "${CMAKE_CURRENT_BINARY_DIR}/replay/downloaded.txt"
  EXPECTED_HASH "SHA256=${DL_HASH}")
file(ARCHIVE_CREATE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/replay/sample.tar"
  PATHS "${CMAKE_CURRENT_SOURCE_DIR}/archive_input"
  FORMAT paxr
  MTIME 0)
file(ARCHIVE_EXTRACT
  INPUT "${CMAKE_CURRENT_BINARY_DIR}/replay/sample.tar"
  DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/replay/archive_out")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/replay/generated.txt" CONTENT "GEN")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/replay/lock_dir")
file(LOCK "${CMAKE_CURRENT_BINARY_DIR}/replay/lock_dir" DIRECTORY)
file(LOCK "${CMAKE_CURRENT_BINARY_DIR}/replay/lock_dir" DIRECTORY RELEASE)
#@@ENDCASE

#@@CASE backend_row52_config_language_platform_surface
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/src/c_part.c
#ifndef LANG_C
#error LANG_C missing
#endif
#ifdef LANG_CXX
#error LANG_CXX leaked into C
#endif
#ifndef CFG_RELWITHDEBINFO
#error CFG_RELWITHDEBINFO missing
#endif
#if defined(__linux__) && !defined(PLATFORM_LINUX)
#error PLATFORM_LINUX missing
#endif
int c_marker(void) { return 17; }
#@@END_FILE_TEXT
#@@FILE_TEXT source/src/main.cpp
extern "C" int c_marker(void);
#ifndef LANG_CXX
#error LANG_CXX missing
#endif
#ifdef LANG_C
#error LANG_C leaked into CXX
#endif
#ifndef CFG_RELWITHDEBINFO
#error CFG_RELWITHDEBINFO missing
#endif
#if defined(__linux__) && !defined(PLATFORM_LINUX)
#error PLATFORM_LINUX missing
#endif
int main(void) { return c_marker() == 17 ? 0 : 1; }
#@@END_FILE_TEXT
#@@FILE_TEXT source/imports/libbase.so
base
#@@END_FILE_TEXT
#@@FILE_TEXT source/imports/libdebug.so
debug
#@@END_FILE_TEXT
cmake_minimum_required(VERSION 3.28)
set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
project(Row52 LANGUAGES C CXX)
add_library(iface INTERFACE)
target_compile_definitions(iface INTERFACE
  "$<$<CONFIG:RelWithDebInfo>:CFG_RELWITHDEBINFO>"
  "$<$<COMPILE_LANGUAGE:C>:LANG_C>"
  "$<$<COMPILE_LANGUAGE:CXX>:LANG_CXX>"
  "$<$<PLATFORM_ID:Linux>:PLATFORM_LINUX>")
add_library(ext SHARED IMPORTED)
set_target_properties(ext PROPERTIES
  IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/imports/libbase.so"
  IMPORTED_LOCATION_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/imports/libdebug.so"
  MAP_IMPORTED_CONFIG_RELWITHDEBINFO Debug)
file(GENERATE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/reports/config-$<CONFIG>.txt"
  CONTENT "$<TARGET_FILE_NAME:ext>\n")
add_executable(app src/c_part.c src/main.cpp)
target_link_libraries(app PRIVATE iface)
set_target_properties(app PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/artifacts/bin")
#@@ENDCASE

#@@CASE backend_row52_config_catalog_strequal_surface
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/src/main.c
int main(void) { return 0; }
#@@END_FILE_TEXT
cmake_minimum_required(VERSION 3.28)
set(CMAKE_BUILD_TYPE Profile CACHE STRING "" FORCE)
project(Row52Catalog LANGUAGES C)
file(GENERATE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/reports/$<$<STREQUAL:$<CONFIG>,Profile>:profile>$<$<NOT:$<STREQUAL:$<CONFIG>,Profile>>:other>.txt"
  CONTENT "$<$<STREQUAL:$<CONFIG>,Profile>:profile>$<$<NOT:$<STREQUAL:$<CONFIG>,Profile>>:other>\n")
add_executable(app src/main.c)
set_target_properties(app PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/artifacts/bin")
#@@ENDCASE

#@@CASE backend_row52_config_language_platform_replay_closure_surface
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/src/c_part.c
#ifndef LANG_C
#error LANG_C missing
#endif
#ifdef LANG_CXX
#error LANG_CXX leaked into C
#endif
#ifndef CFG_RELWITHDEBINFO
#error CFG_RELWITHDEBINFO missing
#endif
#if defined(__linux__) && !defined(PLATFORM_LINUX)
#error PLATFORM_LINUX missing
#endif
int c_marker(void) { return 52; }
#@@END_FILE_TEXT
#@@FILE_TEXT source/src/main.cpp
extern "C" int c_marker(void);
#ifndef LANG_CXX
#error LANG_CXX missing
#endif
#ifdef LANG_C
#error LANG_C leaked into CXX
#endif
#ifndef CFG_RELWITHDEBINFO
#error CFG_RELWITHDEBINFO missing
#endif
#if defined(__linux__) && !defined(PLATFORM_LINUX)
#error PLATFORM_LINUX missing
#endif
int main(void) { return c_marker() == 52 ? 0 : 1; }
#@@END_FILE_TEXT
#@@FILE_TEXT source/imports/libbase.so
base
#@@END_FILE_TEXT
#@@FILE_TEXT source/imports/libdebug.so
debug
#@@END_FILE_TEXT
cmake_minimum_required(VERSION 3.28)
set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
project(Row52Closure LANGUAGES C CXX)
add_library(iface INTERFACE)
target_compile_definitions(iface INTERFACE
  "$<$<CONFIG:RelWithDebInfo>:CFG_RELWITHDEBINFO>"
  "$<$<COMPILE_LANGUAGE:C>:LANG_C>"
  "$<$<COMPILE_LANGUAGE:CXX>:LANG_CXX>"
  "$<$<PLATFORM_ID:Linux>:PLATFORM_LINUX>")
add_library(ext SHARED IMPORTED)
set_target_properties(ext PROPERTIES
  IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/imports/libbase.so"
  IMPORTED_LOCATION_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/imports/libdebug.so"
  MAP_IMPORTED_CONFIG_RELWITHDEBINFO Debug)
file(GENERATE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/reports/row52.txt"
  CONTENT "config=$<CONFIG>\next=$<TARGET_FILE_NAME:ext>\nplatform=$<$<PLATFORM_ID:Linux>:linux>\n")
file(GENERATE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/replay/$<IF:$<CONFIG:RelWithDebInfo>,relwithdebinfo,other>.txt"
  CONTENT "replay=$<IF:$<CONFIG:RelWithDebInfo>,relwithdebinfo,other>\n")
add_executable(app src/c_part.c src/main.cpp)
target_link_libraries(app PRIVATE iface)
set_target_properties(app PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/artifacts/bin")
#@@ENDCASE

#@@CASE backend_row53_output_naming_artifact_path_closure_surface
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/src/core.c
int core_value(void) { return 53; }
#@@END_FILE_TEXT
#@@FILE_TEXT source/src/dyn.c
int dyn_value(void) { return 7; }
#@@END_FILE_TEXT
#@@FILE_TEXT source/src/main.c
int main(void) { return 0; }
#@@END_FILE_TEXT
cmake_minimum_required(VERSION 3.28)
set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
project(Row53Closure LANGUAGES C)
add_library(core STATIC src/core.c)
set_target_properties(core PROPERTIES
  OUTPUT_NAME core_generic
  ARCHIVE_OUTPUT_NAME_RELWITHDEBINFO core_rel_archive
  ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_BINARY_DIR}/artifacts/lib/rel"
  PREFIX row_
  SUFFIX .pack)
add_library(dyn SHARED src/dyn.c)
set_target_properties(dyn PROPERTIES
  OUTPUT_NAME dyn_generic
  LIBRARY_OUTPUT_NAME_RELWITHDEBINFO dyn_rel
  LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_BINARY_DIR}/artifacts/shlib/rel")
add_executable(app src/main.c)
set_target_properties(app PROPERTIES
  OUTPUT_NAME app_generic
  RUNTIME_OUTPUT_NAME_RELWITHDEBINFO app_rel
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_BINARY_DIR}/artifacts/$<IF:$<CONFIG:RelWithDebInfo>,bin,other>/rel")
file(GENERATE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/reports/row53.txt"
  CONTENT "config=$<CONFIG>\ncore_file=$<TARGET_FILE_NAME:core>\ncore_dir=$<TARGET_FILE_DIR:core>\ncore_link=$<TARGET_LINKER_FILE_NAME:core>\ndyn_file=$<TARGET_FILE_NAME:dyn>\ndyn_dir=$<TARGET_FILE_DIR:dyn>\ndyn_link=$<TARGET_LINKER_FILE_NAME:dyn>\napp_file=$<TARGET_FILE_NAME:app>\napp_dir=$<TARGET_FILE_DIR:app>\n")
#@@ENDCASE

#@@CASE backend_row54_custom_command_target_graph_closure_surface
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/src/tool.c
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static int ensure_parent_dir(const char *path) {
    char buf[1024];
    size_t len = path ? strlen(path) : 0;
    if (len == 0 || len >= sizeof(buf)) return 1;
    memcpy(buf, path, len + 1);
    for (size_t i = 1; i < len; ++i) {
        if (buf[i] != '/') continue;
        buf[i] = '\0';
        if (buf[0] != '\0' && mkdir(buf, 0777) != 0 && errno != EEXIST) return 1;
        buf[i] = '/';
    }
    return 0;
}

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static int write_generated(const char *source, const char *byproduct, const char *config) {
    FILE *f = NULL;
    if (ensure_parent_dir(source) != 0 || ensure_parent_dir(byproduct) != 0) return 1;
    f = fopen(source, "wb");
    if (!f) return 1;
    fprintf(f, "int generated_base(void) { return 50; }\n");
    fprintf(f, "const char *generated_config(void) { return \"%s\"; }\n", config);
    fclose(f);
    f = fopen(byproduct, "wb");
    if (!f) return 1;
    fprintf(f, "byproduct=%s\n", config);
    fclose(f);
    return 0;
}

static int append_generated(const char *source) {
    FILE *f = fopen(source, "ab");
    if (!f) return 1;
    fprintf(f, "int generated_append(void) { return 4; }\n");
    fclose(f);
    return 0;
}

static int write_report(const char *out, const char *config, const char *target_file, const char *byproduct) {
    char line[256] = {0};
    FILE *bp = NULL;
    FILE *f = NULL;
    if (ensure_parent_dir(out) != 0) return 1;
    f = fopen(out, "wb");
    if (!f) return 1;
    fprintf(f, "config=%s\n", config);
    fprintf(f, "target_file_exists=%d\n", file_exists(target_file));
    bp = fopen(byproduct, "rb");
    if (bp) {
        if (fgets(line, sizeof(line), bp)) fputs(line, f);
        fclose(bp);
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 5 && strcmp(argv[1], "gen") == 0) return write_generated(argv[2], argv[3], argv[4]);
    if (argc >= 3 && strcmp(argv[1], "extend") == 0) return append_generated(argv[2]);
    if (argc >= 6 && strcmp(argv[1], "report") == 0) return write_report(argv[2], argv[3], argv[4], argv[5]);
    if (argc >= 4 && strcmp(argv[1], "all") == 0) return write_report(argv[2], argv[3], argv[0], argv[2]);
    return 2;
}
#@@END_FILE_TEXT
#@@FILE_TEXT source/src/main.c
#include <string.h>
int generated_base(void);
int generated_append(void);
const char *generated_config(void);
int main(void) {
    return generated_base() + generated_append() == 54 &&
           strcmp(generated_config(), "RelWithDebInfo") == 0 ? 0 : 1;
}
#@@END_FILE_TEXT
cmake_minimum_required(VERSION 3.28)
set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
project(Row54Closure LANGUAGES C)
add_executable(tool src/tool.c)
set_target_properties(tool PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/artifacts/tools")
add_custom_command(
  OUTPUT generated/generated.c
  BYPRODUCTS generated/generated.by
  COMMAND tool gen "generated/generated.c" "generated/generated.by" "$<CONFIG>"
  DEPENDS tool
  WORKING_DIRECTORY .
  COMMENT "$<$<CONFIG:RelWithDebInfo>:row54 RelWithDebInfo>"
  VERBATIM)
add_custom_command(
  OUTPUT generated/generated.c
  COMMAND tool extend "generated/generated.c"
  APPEND)
add_custom_command(
  OUTPUT reports/row54-$<CONFIG>.txt
  COMMAND tool report "reports/row54-$<CONFIG>.txt" "$<CONFIG>" "$<TARGET_FILE:tool>" "generated/generated.by"
  DEPENDS generated/generated.c
  WORKING_DIRECTORY .
  VERBATIM)
add_custom_target(row54_all ALL
  COMMAND tool all "reports/all-$<CONFIG>.txt" "$<CONFIG>"
  BYPRODUCTS reports/all-$<CONFIG>.txt
  DEPENDS reports/row54-$<CONFIG>.txt
  WORKING_DIRECTORY .
  VERBATIM)
add_executable(app src/main.c "${CMAKE_CURRENT_BINARY_DIR}/generated/generated.c")
set_target_properties(app PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/artifacts/bin")
#@@ENDCASE

#@@CASE backend_row55_explicit_dependency_ordering_closure_surface
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/src/tool.c
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static int file_exists(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0;
}

static int ensure_parent_dir(const char *path) {
    char buf[512];
    size_t len = path ? strlen(path) : 0;
    if (!path || len >= sizeof(buf)) return 1;
    memcpy(buf, path, len + 1);
    for (size_t i = 1; i < len; ++i) {
        if (buf[i] != '/') continue;
        buf[i] = '\0';
        mkdir(buf, 0777);
        buf[i] = '/';
    }
    return 0;
}

static int append_line(const char *path, const char *line) {
    FILE *f = NULL;
    if (ensure_parent_dir(path) != 0) return 1;
    f = fopen(path, "ab");
    if (!f) return 1;
    fprintf(f, "%s\n", line);
    fclose(f);
    return 0;
}

static int write_marker(const char *path, const char *value) {
    FILE *f = NULL;
    if (!path || path[0] == '\0') return 0;
    if (ensure_parent_dir(path) != 0) return 1;
    f = fopen(path, "wb");
    if (!f) return 1;
    fprintf(f, "%s\n", value);
    fclose(f);
    return 0;
}

static int note(const char *order, const char *label, const char *marker) {
    return append_line(order, label) == 0 && write_marker(marker, label) == 0 ? 0 : 1;
}

static int gen(const char *out, const char *byproduct, const char *config, const char *order) {
    FILE *f = NULL;
    if (append_line(order, "generated") != 0 || ensure_parent_dir(out) != 0) return 1;
    f = fopen(out, "wb");
    if (!f) return 1;
    fprintf(f, "int generated_base(void) { return 50; }\n");
    fprintf(f, "int generated_append(void) { return 4; }\n");
    fprintf(f, "const char *generated_config(void) { return \"%s\"; }\n", config);
    fclose(f);
    return write_marker(byproduct, config);
}

static int extend(const char *out, const char *order) {
    FILE *f = NULL;
    if (append_line(order, "append") != 0) return 1;
    f = fopen(out, "ab");
    if (!f) return 1;
    fprintf(f, "int generated_append(void) { return 4; }\n");
    fclose(f);
    return 0;
}

static int report(const char *out,
                  const char *config,
                  const char *app,
                  const char *order,
                  const char *rel_dep,
                  const char *debug_dep,
                  const char *imported_marker) {
    char line[256];
    FILE *src = NULL;
    FILE *dst = NULL;
    if (append_line(order, "report") != 0 || ensure_parent_dir(out) != 0) return 1;
    dst = fopen(out, "wb");
    if (!dst) return 1;
    fprintf(dst, "config=%s\n", config);
    fprintf(dst, "app_exists=%d\n", file_exists(app));
    fprintf(dst, "rel_dep_exists=%d\n", file_exists(rel_dep));
    fprintf(dst, "debug_dep_exists=%d\n", file_exists(debug_dep));
    fprintf(dst, "imported_marker_exists=%d\n", file_exists(imported_marker));
    fprintf(dst, "order:\n");
    src = fopen(order, "rb");
    if (src) {
        while (fgets(line, sizeof(line), src)) fputs(line, dst);
        fclose(src);
    }
    fclose(dst);
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 5 && strcmp(argv[1], "note") == 0) return note(argv[2], argv[3], argv[4]);
    if (argc >= 6 && strcmp(argv[1], "gen") == 0) return gen(argv[2], argv[3], argv[4], argv[5]);
    if (argc >= 4 && strcmp(argv[1], "extend") == 0) return extend(argv[2], argv[3]);
    if (argc >= 9 && strcmp(argv[1], "report") == 0) return report(argv[2], argv[3], argv[4], argv[5], argv[6], argv[7], argv[8]);
    return 2;
}
#@@END_FILE_TEXT
#@@FILE_TEXT source/src/main.c
#include <string.h>
int generated_base(void);
int generated_append(void);
const char *generated_config(void);
int config_dep_value(void);
int main(void) {
    return generated_base() + generated_append() + config_dep_value() == 55 &&
           strcmp(generated_config(), "RelWithDebInfo") == 0 ? 0 : 1;
}
#@@END_FILE_TEXT
#@@FILE_TEXT source/src/rel_dep.c
int config_dep_value(void) { return 1; }
#@@END_FILE_TEXT
#@@FILE_TEXT source/src/debug_dep.c
int config_dep_value(void) { return 99; }
#@@END_FILE_TEXT
cmake_minimum_required(VERSION 3.28)
set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
project(Row55Closure LANGUAGES C)
file(MAKE_DIRECTORY
  "${CMAKE_CURRENT_BINARY_DIR}/artifacts/debug"
  "${CMAKE_CURRENT_BINARY_DIR}/artifacts/rel")
add_executable(tool src/tool.c)
set_target_properties(tool PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/artifacts/tools")
add_custom_target(prepare
  COMMAND tool note reports/order.txt prepare reports/prepare.txt
  BYPRODUCTS reports/prepare.txt
  WORKING_DIRECTORY .
  VERBATIM)
add_custom_target(imported_prepare
  COMMAND tool note reports/order.txt imported reports/imported.txt
  BYPRODUCTS reports/imported.txt
  WORKING_DIRECTORY .
  VERBATIM)
add_library(iface INTERFACE)
add_dependencies(iface prepare)
add_library(ext STATIC IMPORTED GLOBAL)
set_target_properties(ext PROPERTIES IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/missing/libext.a")
add_dependencies(ext imported_prepare)
add_library(rel_dep STATIC src/rel_dep.c)
add_library(debug_dep STATIC src/debug_dep.c)
set_target_properties(rel_dep PROPERTIES
  EXCLUDE_FROM_ALL TRUE
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/artifacts/rel")
set_target_properties(debug_dep PROPERTIES
  EXCLUDE_FROM_ALL TRUE
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/artifacts/debug")
add_custom_command(
  OUTPUT generated/generated.c
  BYPRODUCTS generated/generated.by
  COMMAND tool gen generated/generated.c generated/generated.by "$<CONFIG>" reports/order.txt
  DEPENDS tool reports/prepare.txt
  WORKING_DIRECTORY .
  VERBATIM)
add_executable(app src/main.c "${CMAKE_CURRENT_BINARY_DIR}/generated/generated.c")
add_dependencies(app iface)
target_link_libraries(app PRIVATE "$<$<CONFIG:RelWithDebInfo>:rel_dep>" "$<$<CONFIG:Debug>:debug_dep>")
set_target_properties(app PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/artifacts/bin")
add_custom_command(TARGET app PRE_LINK
  COMMAND tool note reports/order.txt pre-hook ""
  WORKING_DIRECTORY .
  VERBATIM)
add_custom_command(TARGET app POST_BUILD
  COMMAND tool note reports/order.txt post-hook ""
  WORKING_DIRECTORY .
  VERBATIM)
add_custom_target(row55_all ALL
  COMMAND tool report reports/row55.txt "$<CONFIG>" "$<TARGET_FILE:app>" reports/order.txt artifacts/rel/librel_dep.a artifacts/debug/libdebug_dep.a reports/imported.txt
  DEPENDS app
  WORKING_DIRECTORY .
  VERBATIM)
add_dependencies(row55_all ext)
#@@ENDCASE

#@@CASE backend_reject_target_precompile_headers
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/src/main.c
int main(void) { return 0; }
#@@END_FILE_TEXT
#@@FILE_TEXT source/include/pch.h
#define PCH_READY 1
#@@END_FILE_TEXT
add_executable(app src/main.c)
target_precompile_headers(app PRIVATE include/pch.h)
#@@ENDCASE

#@@CASE backend_reject_export_append
#@@OUTCOME SUCCESS
#@@FILE_TEXT source/src/core.c
int core_value(void) { return 0; }
#@@END_FILE_TEXT
add_library(core STATIC src/core.c)
export(TARGETS core FILE CoreTargets.cmake APPEND)
#@@ENDCASE
