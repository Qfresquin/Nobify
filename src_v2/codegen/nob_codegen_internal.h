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

typedef enum {
    CG_EFFECTIVE_INCLUDE_DIRECTORIES = 0,
    CG_EFFECTIVE_COMPILE_DEFINITIONS,
    CG_EFFECTIVE_COMPILE_OPTIONS,
    CG_EFFECTIVE_COMPILE_FEATURES,
    CG_EFFECTIVE_LINK_DIRECTORIES,
    CG_EFFECTIVE_LINK_OPTIONS,
    CG_EFFECTIVE_LINK_LIBRARIES,
} CG_Effective_Query_Family;

typedef enum {
    CG_RESOLVED_TARGET_LOCAL = 0,
    CG_RESOLVED_TARGET_IMPORTED,
} CG_Resolved_Target_Kind;

typedef struct {
    BM_Target_Id target_id;
    BM_Query_Usage_Mode usage_mode;
    String_View config;
    String_View compile_language;
    CG_Effective_Query_Family family;
    BM_String_Item_Span items;
    bool ready;
} CG_Effective_Item_Cache_Entry;

typedef struct {
    BM_Target_Id target_id;
    BM_Query_Usage_Mode usage_mode;
    String_View config;
    String_View compile_language;
    CG_Effective_Query_Family family;
    BM_String_Span values;
    bool ready;
} CG_Effective_Value_Cache_Entry;

typedef struct {
    BM_Target_Id target_id;
    String_View config;
    bool linker_file;
    String_View path;
    bool ready;
} CG_Target_File_Cache_Entry;

typedef struct {
    BM_Target_Id target_id;
    String_View config;
    BM_String_Span languages;
    bool ready;
} CG_Imported_Link_Lang_Cache_Entry;

typedef struct {
    String_View original_item;
    BM_Target_Id target_id;
    BM_Target_Id resolved_target_id;
    CG_Resolved_Target_Kind kind;
    BM_Target_Kind target_kind;
    bool imported;
    bool usage_only;
    bool linkable_artifact;
    String_View effective_file;
    String_View effective_linker_file;
    String_View rebuild_input_path;
    BM_String_Span imported_link_languages;
    String_View failure_message;
} CG_Resolved_Target_Ref;

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
    CG_Effective_Item_Cache_Entry *effective_item_cache;
    CG_Effective_Value_Cache_Entry *effective_value_cache;
    CG_Target_File_Cache_Entry *target_file_cache;
    CG_Imported_Link_Lang_Cache_Entry *imported_link_lang_cache;
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
bool cg_query_effective_items_cached(CG_Context *ctx,
                                     BM_Target_Id id,
                                     const BM_Query_Eval_Context *qctx,
                                     CG_Effective_Query_Family family,
                                     BM_String_Item_Span *out);
bool cg_query_effective_values_cached(CG_Context *ctx,
                                      BM_Target_Id id,
                                      const BM_Query_Eval_Context *qctx,
                                      CG_Effective_Query_Family family,
                                      BM_String_Span *out);
bool cg_query_target_file_cached(CG_Context *ctx,
                                 BM_Target_Id id,
                                 const BM_Query_Eval_Context *qctx,
                                 bool linker_file,
                                 String_View *out);
bool cg_query_imported_link_languages_cached(CG_Context *ctx,
                                             BM_Target_Id id,
                                             const BM_Query_Eval_Context *qctx,
                                             BM_String_Span *out);
bool cg_resolve_target_ref(CG_Context *ctx,
                           const BM_Query_Eval_Context *qctx,
                           String_View item,
                           CG_Resolved_Target_Ref *out);
bool cg_emit_step_function(CG_Context *ctx,
                           const CG_Build_Step_Info *info,
                           Nob_String_Builder *out);

#endif
