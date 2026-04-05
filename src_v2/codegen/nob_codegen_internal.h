#ifndef NOB_CODEGEN_INTERNAL_H_
#define NOB_CODEGEN_INTERNAL_H_

#include "nob_codegen.h"

typedef struct {
    BM_Target_Id id;
    BM_Target_Id resolved_id;
    BM_Target_Kind kind;
    bool imported;
    bool alias;
    bool exclude_from_all;
    bool emits_artifact;
    String_View name;
    const char *ident;
    String_View artifact_path;
    String_View state_path;
    bool needs_cxx_linker_known;
    bool needs_cxx_linker;
} CG_Target_Info;

typedef struct {
    BM_Build_Step_Id id;
    BM_Build_Step_Kind kind;
    BM_Directory_Id owner_directory_id;
    BM_Target_Id owner_target_id;
    const char *ident;
    String_View sentinel_path;
    bool uses_stamp;
} CG_Build_Step_Info;

typedef enum {
    CG_SOURCE_LANG_C = 0,
    CG_SOURCE_LANG_CXX,
} CG_Source_Lang;

typedef struct {
    String_View path;
    CG_Source_Lang lang;
    BM_Build_Step_Id producer_step_id;
} CG_Source_Info;

typedef struct {
    const Build_Model *model;
    Arena *scratch;
    Nob_Codegen_Options opts;
    String_View cwd_abs;
    String_View source_root_abs;
    String_View binary_root_abs;
    String_View emit_path_abs;
    String_View emit_dir_abs;
    String_View embedded_cmake_bin_abs;
    String_View embedded_cpack_bin_abs;
    String_View *known_configs;
    CG_Target_Info *targets;
    size_t target_count;
    CG_Build_Step_Info *build_steps;
    size_t build_step_count;
} CG_Context;

const CG_Target_Info *cg_target_info(const CG_Context *ctx, BM_Target_Id id);
const CG_Build_Step_Info *cg_build_step_info(const CG_Context *ctx, BM_Build_Step_Id id);

bool cg_sb_append_c_string(Nob_String_Builder *sb, String_View sv);
bool cg_rebase_path_from_cwd(CG_Context *ctx, String_View in, String_View *out);
bool cg_emit_cmd_append_sv(Nob_String_Builder *out, const char *cmd_var, String_View arg);
bool cg_emit_cmd_append_expr(Nob_String_Builder *out, const char *cmd_var, const char *expr);
bool cg_eval_string_for_config(CG_Context *ctx,
                               BM_Target_Id current_target_id,
                               BM_Query_Usage_Mode usage_mode,
                               String_View config,
                               String_View compile_language,
                               String_View raw,
                               String_View *out);
bool cg_emit_step_function(CG_Context *ctx,
                           const CG_Build_Step_Info *info,
                           Nob_String_Builder *out);

#endif
