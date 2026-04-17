#ifndef NOB_CODEGEN_INTERNAL_H_
#define NOB_CODEGEN_INTERNAL_H_

#include "nob_codegen.h"

typedef struct CG_Context CG_Context;

typedef struct {
    String_View prefix;
    String_View suffix;
} CG_Artifact_Naming;

typedef struct {
    Nob_Codegen_Platform platform;
    Nob_Codegen_Backend backend;
    String_View platform_id;
    CG_Artifact_Naming executable;
    CG_Artifact_Naming static_library;
    CG_Artifact_Naming shared_runtime;
    CG_Artifact_Naming shared_linker;
    CG_Artifact_Naming module_runtime;
    CG_Artifact_Naming module_linker;
    String_View object_suffix;
    String_View c_compiler_default;
    String_View cxx_compiler_default;
    String_View archive_tool_default;
    String_View link_tool_default;
    String_View shared_link_flag;
    String_View module_link_flag;
    bool use_compiler_driver_for_executable_link;
    bool use_compiler_driver_for_shared_link;
    bool use_compiler_driver_for_module_link;
    bool shared_has_distinct_linker_artifact;
    bool module_has_distinct_linker_artifact;
    bool execution_supported;
} CG_Backend_Policy;

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
    String_View linker_artifact_path;
    String_View state_path;
    bool has_distinct_linker_artifact;
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
    size_t source_index;
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

typedef bool (*CG_CMake_Target_Properties_Emitter)(CG_Context *ctx,
                                                   BM_Export_Id export_id,
                                                   BM_Target_Id target_id,
                                                   String_View exported_name,
                                                   String_View export_namespace,
                                                   String_View config,
                                                   void *userdata,
                                                   Nob_String_Builder *sb);

typedef struct {
    String_View config;
    String_View generator;
    String_View output_dir;
    String_View package_name;
    String_View package_version;
    String_View package_file_name;
    String_View staging_root;
    String_View payload_root;
    String_View metadata_output_path;
    bool include_toplevel_directory;
} CG_Package_Request;

typedef enum {
    CG_HELPER_NONE = 0,
    CG_HELPER_CONFIG_MATCHES = 1ull << 0,
    CG_HELPER_COMPILE_TOOLCHAIN = 1ull << 1,
    CG_HELPER_ARCHIVE_TOOL = 1ull << 2,
    CG_HELPER_LINK_TOOL = 1ull << 3,
    CG_HELPER_CMAKE_RESOLVER = 1ull << 4,
    CG_HELPER_CPACK_RESOLVER = 1ull << 5,
    CG_HELPER_FILESYSTEM = 1ull << 6,
    CG_HELPER_RUN_CMD = 1ull << 7,
    CG_HELPER_REQUIRE_PATHS = 1ull << 8,
    CG_HELPER_WRITE_STAMP = 1ull << 9,
    CG_HELPER_INSTALL_COPY_FILE = 1ull << 10,
    CG_HELPER_INSTALL_COPY_DIRECTORY = 1ull << 11,
    CG_HELPER_GZIP_RESOLVER = 1ull << 12,
    CG_HELPER_XZ_RESOLVER = 1ull << 13,
    CG_HELPER_PACKAGE_ARCHIVE = 1ull << 14,
    CG_HELPER_TAR_RESOLVER = 1ull << 15,
    CG_HELPER_REPLAY_SHA256 = 1ull << 16,
} CG_Helper_Flags;

typedef struct CG_Context {
    const Build_Model *model;
    Arena *scratch;
    Nob_Codegen_Options opts;
    CG_Backend_Policy policy;
    String_View cwd_abs;
    String_View source_root_abs;
    String_View binary_root_abs;
    String_View emit_path_abs;
    String_View emit_dir_abs;
    String_View embedded_cmake_bin_abs;
    String_View embedded_cpack_bin_abs;
    String_View embedded_gzip_bin_abs;
    String_View embedded_xz_bin_abs;
    String_View *known_configs;
    CG_Target_Info *targets;
    size_t target_count;
    CG_Build_Step_Info *build_steps;
    size_t build_step_count;
    BM_Query_Session *query_session;
    uint64_t helper_bits;
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
bool cg_query_effective_link_items_cached(CG_Context *ctx,
                                          BM_Target_Id id,
                                          const BM_Query_Eval_Context *qctx,
                                          BM_Link_Item_Span *out);
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
bool cg_resolve_link_item_ref(CG_Context *ctx,
                              const BM_Query_Eval_Context *qctx,
                              BM_Link_Item_View item,
                              CG_Resolved_Target_Ref *out);
bool cg_resolve_target_ref(CG_Context *ctx,
                           const BM_Query_Eval_Context *qctx,
                           String_View item,
                           CG_Resolved_Target_Ref *out);
bool cg_emit_step_function(CG_Context *ctx,
                           const CG_Build_Step_Info *info,
                           Nob_String_Builder *out);
bool cg_target_export_name(CG_Context *ctx, BM_Target_Id id, String_View *out);
bool cg_target_exported_name(CG_Context *ctx,
                             BM_Target_Id id,
                             String_View export_namespace,
                             String_View *out);
bool cg_cmake_append_escaped(Nob_String_Builder *sb, String_View value);
bool cg_join_sv_list(Arena *scratch, String_View *items, String_View *out);
bool cg_export_noconfig_file_name(CG_Context *ctx,
                                  BM_Export_Id export_id,
                                  String_View *out);
bool cg_export_noconfig_output_file_path(CG_Context *ctx,
                                         BM_Export_Id export_id,
                                         String_View *out);
bool cg_export_target_in_span(BM_Target_Id_Span span, BM_Target_Id id);
bool cg_emit_cmake_imported_target_declaration(BM_Target_Kind kind,
                                               String_View exported_name,
                                               Nob_String_Builder *sb);
bool cg_build_cmake_targets_file_contents(CG_Context *ctx,
                                          BM_Export_Id export_id,
                                          String_View config,
                                          bool include_noconfig,
                                          CG_CMake_Target_Properties_Emitter emit_properties,
                                          void *userdata,
                                          String_View *out);
bool cg_build_cmake_targets_noconfig_file_contents(CG_Context *ctx,
                                                   BM_Export_Id export_id,
                                                   String_View config,
                                                   CG_CMake_Target_Properties_Emitter emit_properties,
                                                   void *userdata,
                                                   String_View *out);

#endif
