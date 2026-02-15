#ifndef TOOLCHAIN_DRIVER_H_
#define TOOLCHAIN_DRIVER_H_

#include "build_model.h"

typedef struct {
    Arena *arena;
    String_View build_dir;
    unsigned long timeout_ms;
    bool debug;
} Toolchain_Driver;

typedef struct {
    String_View compiler;
    String_View src_path;
    String_View out_path;
    const String_List *compile_definitions;
    const String_List *compile_options;
    const String_List *link_options;
    const String_List *link_libraries;
} Toolchain_Compile_Request;

typedef struct {
    bool ok;
    String_View output;
} Toolchain_Compile_Result;

String_View toolchain_default_c_compiler(void);
bool toolchain_compiler_available(const Toolchain_Driver *drv, String_View compiler);
bool toolchain_compiler_looks_msvc(String_View compiler);
bool toolchain_try_compile(const Toolchain_Driver *drv, const Toolchain_Compile_Request *req, Toolchain_Compile_Result *out);
bool toolchain_try_run(const Toolchain_Driver *drv, const Toolchain_Compile_Request *req, const String_List *run_args, Toolchain_Compile_Result *compile_out, int *run_exit_code, String_View *run_output);
bool toolchain_probe_check_c_source_compiles(const Toolchain_Driver *drv, String_View source, bool *used_probe);
bool toolchain_probe_check_c_source_runs(const Toolchain_Driver *drv, String_View source, bool *used_probe);
bool toolchain_probe_check_library_exists(const Toolchain_Driver *drv, String_View library, String_View function_name, String_View location, bool *used_probe);
bool toolchain_probe_check_symbol_exists(const Toolchain_Driver *drv, String_View symbol, String_View headers, bool *used_probe);
bool toolchain_probe_check_include_files(const Toolchain_Driver *drv, String_View headers, bool *used_probe);

#endif // TOOLCHAIN_DRIVER_H_
