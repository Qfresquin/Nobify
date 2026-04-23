#include "nob_codegen_internal.h"

#include "arena_dyn.h"
#include "genex.h"
#include "../genex/genex_internal.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static BM_Query_Eval_Context cg_make_query_ctx(CG_Context *ctx,
                                               BM_Target_Id current_target_id,
                                               BM_Query_Usage_Mode usage_mode,
                                               String_View config,
                                               String_View compile_language);
static bool cg_replay_output_is_clean_safe(CG_Context *ctx, String_View output, bool *out_clean_safe);
static bool cg_resolve_replay_operands_for_config(CG_Context *ctx,
                                                  BM_Replay_Action_Id id,
                                                  BM_Replay_Operand_Family family,
                                                  String_View config,
                                                  const String_View **out_values);
static bool cg_resolve_replay_path_for_config(CG_Context *ctx,
                                              BM_Replay_Action_Id id,
                                              BM_Replay_Operand_Family family,
                                              size_t operand_index,
                                              String_View config,
                                              String_View *out);
static bool cg_emit_configure_functions(CG_Context *ctx, Nob_String_Builder *out);
static bool cg_emit_test_functions(CG_Context *ctx, Nob_String_Builder *out);

static bool cg_sv_eq_lit(String_View sv, const char *lit) {
    return nob_sv_eq(sv, nob_sv_from_cstr(lit));
}

static bool cg_sv_contains(String_View sv, const char *needle) {
    size_t needle_len = needle ? strlen(needle) : 0;
    if (!needle || needle_len == 0 || sv.count < needle_len) return false;
    for (size_t i = 0; i + needle_len <= sv.count; ++i) {
        if (memcmp(sv.data + i, needle, needle_len) == 0) return true;
    }
    return false;
}

static bool cg_sv_has_prefix(String_View sv, const char *prefix) {
    String_View p = nob_sv_from_cstr(prefix);
    if (sv.count < p.count) return false;
    return memcmp(sv.data, p.data, p.count) == 0;
}

static String_View cg_trim_current_dir_prefixes(String_View path) {
    while (path.count >= 2 &&
           path.data[0] == '.' &&
           (path.data[1] == '/' || path.data[1] == '\\')) {
        path.data += 2;
        path.count -= 2;
    }
    return path;
}

static bool cg_path_has_prefix(String_View path, String_View prefix) {
    path = cg_trim_current_dir_prefixes(path);
    prefix = cg_trim_current_dir_prefixes(prefix);
    if (prefix.count == 0 || path.count < prefix.count) return false;
    if (!nob_sv_starts_with(path, prefix)) return false;
    if (path.count == prefix.count) return true;
    return path.data[prefix.count] == '/' || path.data[prefix.count] == '\\';
}

static bool cg_path_is_abs(String_View path) {
    return path.count > 0 && path.data[0] == '/';
}

static bool cg_is_genex(String_View sv) {
    return cg_sv_contains(sv, "$<");
}

static bool cg_sv_eq_ci(String_View lhs, String_View rhs) {
    if (lhs.count != rhs.count) return false;
    for (size_t i = 0; i < lhs.count; ++i) {
        if (tolower((unsigned char)lhs.data[i]) != tolower((unsigned char)rhs.data[i])) return false;
    }
    return true;
}

static bool cg_push_unique_config(CG_Context *ctx, String_View config) {
    char *copy = NULL;
    if (!ctx || config.count == 0) return true;
    for (size_t i = 0; i < arena_arr_len(ctx->known_configs); ++i) {
        if (cg_sv_eq_ci(ctx->known_configs[i], config)) return true;
    }
    copy = arena_strndup(ctx->scratch, config.data ? config.data : "", config.count);
    if (!copy) return false;
    return arena_arr_push(ctx->scratch, ctx->known_configs, nob_sv_from_parts(copy, config.count));
}

static bool cg_ends_with(String_View sv, const char *suffix) {
    return nob_sv_end_with(sv, suffix);
}

static bool cg_collect_known_configs(CG_Context *ctx) {
    BM_String_Span known_configs = {0};
    if (!ctx || !ctx->model) return false;
    known_configs = bm_query_known_configurations(ctx->model);
    for (size_t i = 0; i < known_configs.count; ++i) {
        if (!cg_push_unique_config(ctx, known_configs.items[i])) return false;
    }
    return true;
}

static bool cg_is_link_file_like(String_View sv) {
    return cg_ends_with(sv, ".a") ||
           cg_ends_with(sv, ".lib") ||
           cg_ends_with(sv, ".dll") ||
           cg_ends_with(sv, ".so") ||
           cg_ends_with(sv, ".dylib") ||
           cg_ends_with(sv, ".o") ||
           cg_ends_with(sv, ".obj");
}

static bool cg_is_bare_library_name(String_View sv) {
    if (sv.count == 0) return false;
    for (size_t i = 0; i < sv.count; ++i) {
        unsigned char c = (unsigned char)sv.data[i];
        if (!(isalnum(c) || c == '_' || c == '+' || c == '.' || c == '-')) return false;
    }
    return true;
}

static char *cg_arena_vsprintf(Arena *scratch, const char *fmt, va_list ap) {
    char *tmp = nob_temp_vsprintf(fmt, ap);
    return tmp ? arena_strdup(scratch, tmp) : NULL;
}

static char *cg_arena_sprintf(Arena *scratch, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *out = cg_arena_vsprintf(scratch, fmt, ap);
    va_end(ap);
    return out;
}

static bool cg_host_ensure_dir(const char *path) {
    char buf[4096] = {0};
    size_t len = 0;
    size_t start = 1;
    if (!path || path[0] == '\0' || strcmp(path, ".") == 0) return true;
    len = strlen(path);
    if (len >= sizeof(buf)) return false;
    memcpy(buf, path, len + 1);
    if (len >= 3 && buf[1] == ':' && (buf[2] == '/' || buf[2] == '\\')) start = 3;
    for (size_t i = start; i < len; ++i) {
        if (buf[i] != '/' && buf[i] != '\\') continue;
        buf[i] = '\0';
        if (buf[0] != '\0' && !nob_mkdir_if_not_exists(buf)) return false;
        buf[i] = '/';
    }
    return nob_mkdir_if_not_exists(buf);
}

bool cg_sb_append_c_string(Nob_String_Builder *sb, String_View sv) {
    if (!sb) return false;
    nob_sb_append(sb, '"');
    for (size_t i = 0; i < sv.count; ++i) {
        unsigned char c = (unsigned char)sv.data[i];
        switch (c) {
            case '\\': nob_sb_append_cstr(sb, "\\\\"); break;
            case '"': nob_sb_append_cstr(sb, "\\\""); break;
            case '\n': nob_sb_append_cstr(sb, "\\n"); break;
            case '\r': nob_sb_append_cstr(sb, "\\r"); break;
            case '\t': nob_sb_append_cstr(sb, "\\t"); break;
            default:
                if (c < 0x20) nob_sb_append_cstr(sb, nob_temp_sprintf("\\x%02x", c));
                else nob_sb_append(sb, (char)c);
                break;
        }
    }
    nob_sb_append(sb, '"');
    return true;
}

static String_View cg_dirname_to_arena(Arena *scratch, String_View path) {
    (void)scratch;
    size_t end = path.count;
    while (end > 1 && path.data[end - 1] == '/') end--;
    while (end > 0 && path.data[end - 1] != '/') end--;
    if (end == 0) return nob_sv_from_cstr(".");
    if (end == 1) return nob_sv_from_cstr("/");
    return nob_sv_from_parts(path.data, end - 1);
}

static String_View cg_basename_to_arena(Arena *scratch, String_View path) {
    (void)scratch;
    size_t end = path.count;
    size_t start = 0;
    while (end > 1 && path.data[end - 1] == '/') end--;
    for (size_t i = end; i > 0; --i) {
        if (path.data[i - 1] == '/') {
            start = i;
            break;
        }
    }
    return nob_sv_from_parts(path.data + start, end - start);
}

static bool cg_normalize_path_to_arena(Arena *scratch, String_View path, String_View *out) {
    String_View *segments = NULL;
    bool absolute = cg_path_is_abs(path);
    size_t i = 0;
    Nob_String_Builder sb = {0};
    if (!scratch || !out) return false;

    while (i < path.count) {
        while (i < path.count && path.data[i] == '/') i++;
        size_t start = i;
        while (i < path.count && path.data[i] != '/') i++;
        if (i == start) continue;

        String_View seg = nob_sv_from_parts(path.data + start, i - start);
        if (cg_sv_eq_lit(seg, ".")) continue;
        if (cg_sv_eq_lit(seg, "..")) {
            if (!arena_arr_empty(segments) && !cg_sv_eq_lit(arena_arr_last(segments), "..")) {
                arena_arr_set_len(segments, arena_arr_len(segments) - 1);
            } else if (!absolute) {
                if (!arena_arr_push(scratch, segments, seg)) {
                    nob_sb_free(sb);
                    return false;
                }
            }
            continue;
        }

        if (!arena_arr_push(scratch, segments, seg)) {
            nob_sb_free(sb);
            return false;
        }
    }

    if (absolute) nob_sb_append(&sb, '/');
    for (size_t idx = 0; idx < arena_arr_len(segments); ++idx) {
        if (idx > 0) nob_sb_append(&sb, '/');
        nob_sb_append_buf(&sb, segments[idx].data, segments[idx].count);
    }

    if (sb.count == 0) {
        if (absolute) nob_sb_append(&sb, '/');
        else nob_sb_append(&sb, '.');
    }

    char *copy = arena_strndup(scratch, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;

    *out = nob_sv_from_parts(copy, strlen(copy));
    return true;
}

static bool cg_join_paths_to_arena(Arena *scratch, String_View lhs, String_View rhs, String_View *out) {
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    if (!scratch || !out) return false;

    nob_sb_append_buf(&sb, lhs.data ? lhs.data : "", lhs.count);
    if (sb.count > 0 && sb.items[sb.count - 1] != '/') nob_sb_append(&sb, '/');
    nob_sb_append_buf(&sb, rhs.data ? rhs.data : "", rhs.count);

    copy = arena_strndup(scratch, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;
    return cg_normalize_path_to_arena(scratch, nob_sv_from_cstr(copy), out);
}

bool cg_absolute_from_cwd(CG_Context *ctx, String_View path, String_View *out) {
    if (!ctx || !out) return false;
    if (cg_path_is_abs(path)) return cg_normalize_path_to_arena(ctx->scratch, path, out);
    return cg_join_paths_to_arena(ctx->scratch, ctx->cwd_abs, path, out);
}

static bool cg_absolute_from_base(CG_Context *ctx,
                                  String_View base_dir,
                                  String_View path,
                                  String_View *out) {
    String_View base_abs = {0};
    String_View normalized_base = {0};
    if (!ctx || !out) return false;
    if (cg_path_is_abs(path)) return cg_normalize_path_to_arena(ctx->scratch, path, out);
    if ((ctx->opts.source_root.count > 0 && cg_path_has_prefix(path, ctx->opts.source_root)) ||
        (ctx->opts.binary_root.count > 0 && cg_path_has_prefix(path, ctx->opts.binary_root))) {
        return cg_absolute_from_cwd(ctx, path, out);
    }
    if (!cg_normalize_path_to_arena(ctx->scratch, base_dir, &normalized_base)) return false;
    if (normalized_base.count > 0 &&
        !cg_path_is_abs(normalized_base) &&
        cg_path_has_prefix(path, normalized_base)) {
        return cg_absolute_from_cwd(ctx, path, out);
    }
    if (cg_path_is_abs(base_dir)) {
        if (!cg_normalize_path_to_arena(ctx->scratch, base_dir, &base_abs)) return false;
    } else {
        if (!cg_absolute_from_cwd(ctx, base_dir, &base_abs)) return false;
    }
    return cg_join_paths_to_arena(ctx->scratch, base_abs, path, out);
}

bool cg_relative_path_to_arena(Arena *scratch,
                               String_View from_dir_abs,
                               String_View to_path_abs,
                               String_View *out) {
    String_View *from_segments = NULL;
    String_View *to_segments = NULL;
    size_t common = 0;
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    if (!scratch || !out) return false;

    if (!cg_normalize_path_to_arena(scratch, from_dir_abs, &from_dir_abs) ||
        !cg_normalize_path_to_arena(scratch, to_path_abs, &to_path_abs)) {
        return false;
    }

    size_t i = 0;
    while (i < from_dir_abs.count) {
        while (i < from_dir_abs.count && from_dir_abs.data[i] == '/') i++;
        size_t start = i;
        while (i < from_dir_abs.count && from_dir_abs.data[i] != '/') i++;
        if (i > start && !arena_arr_push(scratch, from_segments, nob_sv_from_parts(from_dir_abs.data + start, i - start))) {
            return false;
        }
    }

    i = 0;
    while (i < to_path_abs.count) {
        while (i < to_path_abs.count && to_path_abs.data[i] == '/') i++;
        size_t start = i;
        while (i < to_path_abs.count && to_path_abs.data[i] != '/') i++;
        if (i > start && !arena_arr_push(scratch, to_segments, nob_sv_from_parts(to_path_abs.data + start, i - start))) {
            return false;
        }
    }

    while (common < arena_arr_len(from_segments) &&
           common < arena_arr_len(to_segments) &&
           nob_sv_eq(from_segments[common], to_segments[common])) {
        common++;
    }

    for (size_t idx = common; idx < arena_arr_len(from_segments); ++idx) {
        if (sb.count > 0) nob_sb_append(&sb, '/');
        nob_sb_append_cstr(&sb, "..");
    }
    for (size_t idx = common; idx < arena_arr_len(to_segments); ++idx) {
        if (sb.count > 0) nob_sb_append(&sb, '/');
        nob_sb_append_buf(&sb, to_segments[idx].data, to_segments[idx].count);
    }

    if (sb.count == 0) nob_sb_append(&sb, '.');

    copy = arena_strndup(scratch, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;
    *out = nob_sv_from_parts(copy, strlen(copy));
    return true;
}

static bool cg_rebase_from_base(CG_Context *ctx,
                                String_View value,
                                String_View base_dir,
                                String_View *out) {
    String_View absolute = {0};
    if (!ctx || !out) return false;
    if (!cg_absolute_from_base(ctx, base_dir, value, &absolute)) return false;
    return cg_relative_path_to_arena(ctx->scratch, ctx->emit_dir_abs, absolute, out);
}

static bool cg_rebase_from_provenance(CG_Context *ctx,
                                      String_View value,
                                      BM_Provenance provenance,
                                      String_View *out) {
    String_View base_dir = provenance.file_path.count > 0
        ? cg_dirname_to_arena(ctx->scratch, provenance.file_path)
        : ctx->cwd_abs;
    return cg_rebase_from_base(ctx, value, base_dir, out);
}

bool cg_rebase_path_from_cwd(CG_Context *ctx, String_View value, String_View *out) {
    return cg_rebase_from_base(ctx, value, ctx->cwd_abs, out);
}

bool cg_rebase_path_from_generation_cwd(CG_Context *ctx, String_View value, String_View *out) {
    String_View absolute = {0};
    if (!ctx || !out) return false;
    if (!cg_absolute_from_base(ctx, ctx->cwd_abs, value, &absolute)) return false;
    return cg_relative_path_to_arena(ctx->scratch, ctx->emit_dir_abs, absolute, out);
}

static bool cg_rebase_from_binary_root(CG_Context *ctx, String_View value, String_View *out) {
    return cg_rebase_from_base(ctx, value, ctx->binary_root_abs, out);
}

static bool cg_absolute_from_emit(CG_Context *ctx, String_View value, String_View *out) {
    return cg_absolute_from_base(ctx, ctx->emit_dir_abs, value, out);
}

static bool cg_effective_owner_binary_dir(CG_Context *ctx,
                                          BM_Directory_Id owner,
                                          String_View *out_abs) {
    String_View owner_binary_dir = {0};
    String_View owner_source_dir = {0};
    String_View owner_binary_abs = {0};
    String_View owner_source_abs = {0};
    String_View owner_rel_from_source_root = {0};
    if (!ctx || !out_abs) return false;

    owner_binary_dir = bm_query_directory_binary_dir(ctx->model, owner);
    owner_source_dir = bm_query_directory_source_dir(ctx->model, owner);
    if (!cg_absolute_from_cwd(ctx, owner_binary_dir, &owner_binary_abs) ||
        !cg_absolute_from_cwd(ctx, owner_source_dir, &owner_source_abs)) {
        return false;
    }

    *out_abs = owner_binary_abs;
    if (!nob_sv_eq(owner_binary_abs, ctx->binary_root_abs)) return true;
    if (!cg_relative_path_to_arena(ctx->scratch,
                                   ctx->source_root_abs,
                                   owner_source_abs,
                                   &owner_rel_from_source_root)) {
        return false;
    }
    if (owner_rel_from_source_root.count == 0 ||
        cg_sv_eq_lit(owner_rel_from_source_root, ".") ||
        cg_sv_has_prefix(owner_rel_from_source_root, "..")) {
        return true;
    }

    return cg_join_paths_to_arena(ctx->scratch,
                                  ctx->binary_root_abs,
                                  owner_rel_from_source_root,
                                  out_abs);
}

static const char *cg_make_identifier(Arena *scratch, String_View name, size_t suffix) {
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    if (!scratch) return NULL;

    nob_sb_append_cstr(&sb, "t_");
    for (size_t i = 0; i < name.count; ++i) {
        unsigned char c = (unsigned char)name.data[i];
        nob_sb_append(&sb, (isalnum(c) || c == '_') ? (char)c : '_');
    }
    nob_sb_append_cstr(&sb, nob_temp_sprintf("_%zu", suffix));

    copy = arena_strndup(scratch, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    return copy;
}

static BM_Target_Id cg_resolve_alias_target(CG_Context *ctx, BM_Target_Id id) {
    size_t remaining = ctx ? ctx->target_count : 0;
    BM_Target_Id current = id;

    while (ctx && remaining-- > 0 && bm_target_id_is_valid(current) && bm_query_target_is_alias(ctx->model, current)) {
        current = bm_query_target_alias_of(ctx->model, current);
    }

    return current;
}

static bool cg_target_is_supported_concrete(BM_Target_Kind kind) {
    return kind == BM_TARGET_EXECUTABLE ||
           kind == BM_TARGET_STATIC_LIBRARY ||
           kind == BM_TARGET_SHARED_LIBRARY ||
           kind == BM_TARGET_MODULE_LIBRARY;
}

static bool cg_target_is_non_emitting(BM_Target_Kind kind) {
    return kind == BM_TARGET_INTERFACE_LIBRARY ||
           kind == BM_TARGET_UTILITY;
}

static bool cg_target_needs_pic(CG_Context *ctx, BM_Target_Kind kind) {
    if (!ctx || ctx->policy.backend != NOB_CODEGEN_BACKEND_POSIX) return false;
    return kind == BM_TARGET_SHARED_LIBRARY ||
           kind == BM_TARGET_MODULE_LIBRARY;
}

static bool cg_target_kind_is_linkable_artifact(BM_Target_Kind kind) {
    return kind == BM_TARGET_STATIC_LIBRARY ||
           kind == BM_TARGET_SHARED_LIBRARY;
}

#include "nob_codegen_platform.c"
#include "nob_codegen_resolve.c"

static bool cg_check_no_genex(const char *what, String_View sv) {
    if (!cg_is_genex(sv)) return true;
    nob_log(NOB_ERROR, "codegen: unsupported generator expression in %s: %.*s",
            what, (int)sv.count, sv.data ? sv.data : "");
    return false;
}

static bool cg_source_lang_from_effective_language(String_View language, CG_Source_Lang *out_lang) {
    if (out_lang) *out_lang = CG_SOURCE_LANG_C;
    if (cg_sv_eq_ci(language, nob_sv_from_cstr("C"))) {
        if (out_lang) *out_lang = CG_SOURCE_LANG_C;
        return true;
    }
    if (cg_sv_eq_ci(language, nob_sv_from_cstr("CXX"))) {
        if (out_lang) *out_lang = CG_SOURCE_LANG_CXX;
        return true;
    }
    return false;
}

static const BM_Target_Artifact_View *cg_target_artifact_for_config(const CG_Target_Info *info,
                                                                    BM_Target_Artifact_Role role,
                                                                    String_View config) {
    BM_Target_Artifact_View *items = NULL;
    if (!info) return NULL;
    items = role == BM_TARGET_ARTIFACT_LINKER ? info->linker_artifacts : info->runtime_artifacts;
    for (size_t i = 0; i < arena_arr_len(info->artifact_configs); ++i) {
        if (config.count > 0 && cg_sv_eq_ci(info->artifact_configs[i], config)) return &items[i];
    }
    if (arena_arr_len(items) == 0) return NULL;
    return &items[arena_arr_len(items) - 1];
}

static bool cg_target_artifact_for_config_or_empty(const CG_Target_Info *info,
                                                   BM_Target_Artifact_Role role,
                                                   String_View config,
                                                   BM_Target_Artifact_View *out) {
    const BM_Target_Artifact_View *artifact = cg_target_artifact_for_config(info, role, config);
    if (!out) return false;
    *out = artifact ? *artifact : (BM_Target_Artifact_View){0};
    return true;
}

static bool cg_init_target_artifact_branches(CG_Context *ctx, CG_Target_Info *info) {
    if (!ctx || !info) return false;
    if (!info->emits_artifact) return true;
    for (size_t branch = 0; branch <= arena_arr_len(ctx->known_configs); ++branch) {
        String_View config = branch < arena_arr_len(ctx->known_configs)
            ? ctx->known_configs[branch]
            : nob_sv_from_cstr("");
        BM_Query_Eval_Context qctx = cg_make_query_ctx(ctx, info->id, BM_QUERY_USAGE_LINK, config, nob_sv_from_cstr(""));
        BM_Target_Artifact_View runtime_raw = {0};
        BM_Target_Artifact_View linker_raw = {0};
        BM_Target_Artifact_View runtime = {0};
        BM_Target_Artifact_View linker = {0};
        if (!cg_query_target_artifact_cached(ctx, info->id, &qctx, BM_TARGET_ARTIFACT_RUNTIME, &runtime_raw) ||
            !cg_query_target_artifact_cached(ctx, info->id, &qctx, BM_TARGET_ARTIFACT_LINKER, &linker_raw) ||
            !cg_rebase_artifact_view_from_cwd(ctx, runtime_raw, &runtime) ||
            !cg_rebase_artifact_view_from_cwd(ctx, linker_raw, &linker) ||
            !arena_arr_push(ctx->scratch, info->artifact_configs, config) ||
            !arena_arr_push(ctx->scratch, info->runtime_artifacts, runtime) ||
            !arena_arr_push(ctx->scratch, info->linker_artifacts, linker)) {
            return false;
        }
    }
    if (arena_arr_len(info->runtime_artifacts) > 0) {
        const BM_Target_Artifact_View *runtime = &info->runtime_artifacts[arena_arr_len(info->runtime_artifacts) - 1];
        const BM_Target_Artifact_View *linker = &info->linker_artifacts[arena_arr_len(info->linker_artifacts) - 1];
        info->artifact_path = runtime->path;
        info->linker_artifact_path = linker->path.count > 0 ? linker->path : runtime->path;
        info->has_distinct_linker_artifact = !nob_sv_eq(info->artifact_path, info->linker_artifact_path);
    }
    return true;
}

static bool cg_compute_target_state_path(CG_Context *ctx,
                                         const CG_Target_Info *info,
                                         String_View *out) {
    String_View subpath = {0};
    if (!ctx || !info || !out) return false;
    if (info->emits_artifact) {
        *out = info->artifact_path;
        return true;
    }
    if (info->alias || info->imported || info->kind != BM_TARGET_UTILITY) {
        *out = nob_sv_from_cstr("");
        return true;
    }
    if (!cg_join_paths_to_arena(ctx->scratch,
                                nob_sv_from_cstr(".nob/targets"),
                                nob_sv_from_cstr(nob_temp_sprintf("%s.stamp", info->ident)),
                                &subpath)) {
        return false;
    }
    return cg_rebase_from_binary_root(ctx, subpath, out);
}

static bool cg_init_targets(CG_Context *ctx) {
    ctx->target_count = bm_query_target_count(ctx->model);
    ctx->targets = arena_alloc_array_zero(ctx->scratch, CG_Target_Info, ctx->target_count);
    if (ctx->target_count > 0 && !ctx->targets) return false;

    for (size_t i = 0; i < ctx->target_count; ++i) {
        BM_Target_Id id = (BM_Target_Id)i;
        CG_Target_Info *info = &ctx->targets[i];
        info->id = id;
        info->name = bm_query_target_name(ctx->model, id);
        info->kind = bm_query_target_kind(ctx->model, id);
        info->imported = bm_query_target_is_imported(ctx->model, id);
        info->alias = bm_query_target_is_alias(ctx->model, id);
        info->exclude_from_all = bm_query_target_exclude_from_all(ctx->model, id);
        info->resolved_id = info->alias ? cg_resolve_alias_target(ctx, id) : id;
        info->ident = cg_make_identifier(ctx->scratch, info->name, i);
        if (!info->ident) return false;

        if (info->alias && !bm_target_id_is_valid(info->resolved_id)) {
            nob_log(NOB_ERROR, "codegen: alias target '%.*s' has invalid alias_of target",
                    (int)info->name.count, info->name.data ? info->name.data : "");
            return false;
        }

        if (!info->alias && !info->imported &&
            !cg_target_is_supported_concrete(info->kind) &&
            !cg_target_is_non_emitting(info->kind)) {
            nob_log(NOB_ERROR, "codegen: unsupported target kind for '%.*s'",
                    (int)info->name.count, info->name.data ? info->name.data : "");
            return false;
        }

        info->emits_artifact = !info->alias && !info->imported && cg_target_is_supported_concrete(info->kind);
        if (info->emits_artifact && !cg_init_target_artifact_branches(ctx, info)) {
            return false;
        }
        if (!cg_compute_target_state_path(ctx, info, &info->state_path)) return false;
    }

    for (size_t i = 0; i < ctx->target_count; ++i) {
        if (!ctx->targets[i].alias) continue;
        BM_Target_Id resolved = ctx->targets[i].resolved_id;
        if (!bm_target_id_is_valid(resolved) || (size_t)resolved >= ctx->target_count) return false;
        ctx->targets[i].artifact_path = ctx->targets[resolved].artifact_path;
        ctx->targets[i].kind = ctx->targets[resolved].kind;
        ctx->targets[i].imported = ctx->targets[resolved].imported;
        ctx->targets[i].emits_artifact = ctx->targets[resolved].emits_artifact;
        ctx->targets[i].linker_artifact_path = ctx->targets[resolved].linker_artifact_path;
        ctx->targets[i].has_distinct_linker_artifact = ctx->targets[resolved].has_distinct_linker_artifact;
        ctx->targets[i].runtime_artifacts = ctx->targets[resolved].runtime_artifacts;
        ctx->targets[i].linker_artifacts = ctx->targets[resolved].linker_artifacts;
        ctx->targets[i].artifact_configs = ctx->targets[resolved].artifact_configs;
        ctx->targets[i].state_path = ctx->targets[resolved].state_path;
    }

    return true;
}

const CG_Target_Info *cg_target_info(const CG_Context *ctx, BM_Target_Id id) {
    if (!ctx || !bm_target_id_is_valid(id) || (size_t)id >= ctx->target_count) return NULL;
    return &ctx->targets[id];
}

static bool cg_compute_step_sentinel_path(CG_Context *ctx, BM_Build_Step_Id id, String_View *out, bool *out_uses_stamp) {
    BM_Build_Step_Effective_View view = {0};
    BM_Query_Eval_Context qctx = {0};
    BM_Build_Step_Kind kind = BM_BUILD_STEP_OUTPUT_RULE;
    String_View subpath = {0};
    if (out) *out = nob_sv_from_cstr("");
    if (out_uses_stamp) *out_uses_stamp = false;
    if (!ctx || !out) return false;

    qctx.current_target_id = bm_query_build_step_owner_target(ctx->model, id);
    qctx.usage_mode = BM_QUERY_USAGE_LINK;
    qctx.platform_id = ctx->policy.platform_id;
    qctx.build_interface_active = true;
    qctx.build_local_interface_active = true;
    kind = bm_query_build_step_kind(ctx->model, id);

    if (!bm_query_build_step_effective_view(ctx->model, id, &qctx, ctx->scratch, &view)) return false;
    if (view.outputs.count > 0) {
        if (!cg_rebase_path_from_cwd(ctx, view.outputs.items[0], out)) return false;
        return true;
    }
    if (kind != BM_BUILD_STEP_OUTPUT_RULE) return true;

    if (!cg_join_paths_to_arena(ctx->scratch,
                                nob_sv_from_cstr(".nob/steps"),
                                nob_sv_from_cstr(nob_temp_sprintf("%u.stamp", (unsigned)id)),
                                &subpath) ||
        !cg_rebase_from_binary_root(ctx, subpath, out)) {
        return false;
    }
    if (out_uses_stamp) *out_uses_stamp = true;
    return true;
}

static bool cg_init_build_steps(CG_Context *ctx) {
    if (!ctx) return false;
    ctx->build_step_count = bm_query_build_step_count(ctx->model);
    ctx->build_steps = arena_alloc_array_zero(ctx->scratch, CG_Build_Step_Info, ctx->build_step_count);
    if (ctx->build_step_count > 0 && !ctx->build_steps) return false;

    for (size_t i = 0; i < ctx->build_step_count; ++i) {
        BM_Build_Step_Id id = (BM_Build_Step_Id)i;
        CG_Build_Step_Info *info = &ctx->build_steps[i];
        info->id = id;
        info->kind = bm_query_build_step_kind(ctx->model, id);
        info->owner_directory_id = bm_query_build_step_owner_directory(ctx->model, id);
        info->owner_target_id = bm_query_build_step_owner_target(ctx->model, id);
        info->ident = cg_make_identifier(ctx->scratch,
                                         nob_sv_from_cstr(nob_temp_sprintf("step_%u", (unsigned)id)),
                                         i);
        if (!info->ident) return false;
        if (!cg_compute_step_sentinel_path(ctx, id, &info->sentinel_path, &info->uses_stamp)) return false;
    }
    return true;
}

bool cg_resolve_link_item_ref(CG_Context *ctx,
                              const BM_Query_Eval_Context *qctx,
                              BM_Link_Item_View item,
                              CG_Resolved_Target_Ref *out) {
    if (!ctx || !qctx || !out) return false;
    if (bm_target_id_is_valid(item.target_id)) {
        BM_Target_Id target_id = cg_resolve_alias_target(ctx, item.target_id);
        const CG_Target_Info *info = NULL;
        BM_String_Span imported_langs = {0};
        String_View effective_file = {0};
        String_View effective_linker_file = {0};
        BM_Target_Artifact_View runtime_artifact = {0};
        BM_Target_Artifact_View linker_artifact = {0};
        if (!bm_target_id_is_valid(target_id)) return false;
        info = cg_target_info(ctx, target_id);
        if (!info) return false;
        *out = (CG_Resolved_Target_Ref){0};
        out->original_item = item.value;
        out->target_id = target_id;
        out->resolved_target_id = info->resolved_id;
        out->kind = info->imported ? CG_RESOLVED_TARGET_IMPORTED : CG_RESOLVED_TARGET_LOCAL;
        out->target_kind = info->kind;
        out->imported = info->imported;
        out->usage_only = info->kind == BM_TARGET_INTERFACE_LIBRARY;
        if (info->imported) {
            if (!cg_query_target_file_cached(ctx, target_id, qctx, false, &effective_file) ||
                !cg_query_target_file_cached(ctx, target_id, qctx, true, &effective_linker_file) ||
                !cg_query_imported_link_languages_cached(ctx, target_id, qctx, &imported_langs)) {
                return false;
            }
            out->effective_file = effective_file;
            out->effective_linker_file = effective_linker_file;
            out->imported_link_languages = imported_langs;
            out->linkable_artifact =
                info->kind == BM_TARGET_STATIC_LIBRARY ||
                info->kind == BM_TARGET_SHARED_LIBRARY ||
                info->kind == BM_TARGET_UNKNOWN_LIBRARY;
            out->rebuild_input_path = effective_linker_file.count > 0 ? effective_linker_file : effective_file;
            return true;
        }
        if (!cg_query_target_artifact_cached(ctx, target_id, qctx, BM_TARGET_ARTIFACT_RUNTIME, &runtime_artifact) ||
            !cg_query_target_artifact_cached(ctx, target_id, qctx, BM_TARGET_ARTIFACT_LINKER, &linker_artifact) ||
            !cg_rebase_artifact_view_from_cwd(ctx, runtime_artifact, &runtime_artifact) ||
            !cg_rebase_artifact_view_from_cwd(ctx, linker_artifact, &linker_artifact)) {
            return false;
        }
        out->effective_file = runtime_artifact.path;
        out->effective_linker_file = linker_artifact.path.count > 0
            ? linker_artifact.path
            : runtime_artifact.path;
        out->rebuild_input_path = out->effective_linker_file.count > 0
            ? out->effective_linker_file
            : out->effective_file;
        out->linkable_artifact = cg_target_kind_is_linkable_artifact(info->kind);
        return true;
    }
    return cg_resolve_target_ref(ctx, qctx, item.value, out);
}

const CG_Build_Step_Info *cg_build_step_info(const CG_Context *ctx, BM_Build_Step_Id id) {
    if (!ctx || !bm_build_step_id_is_valid(id) || (size_t)id >= ctx->build_step_count) return NULL;
    return &ctx->build_steps[id];
}

static bool cg_object_path_for_index(CG_Context *ctx,
                                     String_View object_dir,
                                     const CG_Source_Info *sources,
                                     size_t source_count,
                                     size_t index,
                                     String_View *out) {
    String_View basename = {0};
    size_t duplicate_count = 0;
    char *name = NULL;
    if (out) *out = nob_sv_from_cstr("");
    if (!ctx || !sources || index >= source_count || !out) return false;

    basename = cg_basename_to_arena(ctx->scratch, sources[index].path);
    if (basename.count == 0) basename = nob_sv_from_cstr("obj");
    for (size_t i = 0; i < source_count; ++i) {
        if (nob_sv_eq(cg_basename_to_arena(ctx->scratch, sources[i].path), basename)) {
            duplicate_count++;
        }
    }

    if (duplicate_count > 1) {
        name = cg_arena_sprintf(ctx->scratch,
                                "%.*s_%zu%.*s",
                                (int)basename.count,
                                basename.data ? basename.data : "",
                                index,
                                (int)ctx->policy.object_suffix.count,
                                ctx->policy.object_suffix.data ? ctx->policy.object_suffix.data : "");
    } else {
        name = cg_arena_sprintf(ctx->scratch,
                                "%.*s%.*s",
                                (int)basename.count,
                                basename.data ? basename.data : "",
                                (int)ctx->policy.object_suffix.count,
                                ctx->policy.object_suffix.data ? ctx->policy.object_suffix.data : "");
    }
    if (!name) return false;
    return cg_join_paths_to_arena(ctx->scratch, object_dir, nob_sv_from_cstr(name), out);
}

static bool cg_collect_unique_target(Arena *scratch, BM_Target_Id **list, BM_Target_Id value) {
    for (size_t i = 0; i < arena_arr_len(*list); ++i) {
        if ((*list)[i] == value) return true;
    }
    return arena_arr_push(scratch, *list, value);
}

static bool cg_collect_unique_path(Arena *scratch, String_View **list, String_View value) {
    for (size_t i = 0; i < arena_arr_len(*list); ++i) {
        if (nob_sv_eq((*list)[i], value)) return true;
    }
    return arena_arr_push(scratch, *list, value);
}

static bool cg_target_raw_property_nonempty(const Build_Model *model,
                                            BM_Target_Id id,
                                            const char *property_name) {
    BM_String_Span span = {0};
    if (!model || !property_name) return false;
    span = bm_query_target_raw_property_items(model, id, nob_sv_from_cstr(property_name));
    for (size_t i = 0; i < span.count; ++i) {
        if (nob_sv_trim(span.items[i]).count > 0) return true;
    }
    return false;
}

static bool cg_target_link_closure_has_interface_pch(CG_Context *ctx,
                                                     BM_Target_Id id,
                                                     uint8_t *visited,
                                                     BM_Target_Id *out_offender) {
    if (out_offender) *out_offender = BM_TARGET_ID_INVALID;
    if (!ctx || !visited || !bm_target_id_is_valid(id) || (size_t)id >= ctx->target_count) return false;
    if (visited[id]) return false;
    visited[id] = 1;

    for (size_t branch = 0; branch <= arena_arr_len(ctx->known_configs); ++branch) {
        BM_Link_Item_Span libs = {0};
        String_View config = branch < arena_arr_len(ctx->known_configs) ? ctx->known_configs[branch] : nob_sv_from_cstr("");
        BM_Query_Eval_Context qctx = cg_make_query_ctx(ctx, id, BM_QUERY_USAGE_LINK, config, nob_sv_from_cstr(""));
        if (!cg_query_effective_link_items_cached(ctx, id, &qctx, &libs)) {
            return false;
        }
        for (size_t i = 0; i < libs.count; ++i) {
            CG_Resolved_Target_Ref dep = {0};
            if (!cg_resolve_link_item_ref(ctx, &qctx, libs.items[i], &dep)) continue;
            if (!bm_target_id_is_valid(dep.target_id)) continue;
            if (cg_target_raw_property_nonempty(ctx->model, dep.target_id, "INTERFACE_PRECOMPILE_HEADERS")) {
                if (out_offender) *out_offender = dep.target_id;
                return true;
            }
            if (cg_target_link_closure_has_interface_pch(ctx, dep.target_id, visited, out_offender)) return true;
        }
    }

    return false;
}

static bool cg_reject_unsupported_precompile_headers(CG_Context *ctx, const CG_Target_Info *info) {
    uint8_t *visited = NULL;
    BM_Target_Id offender = BM_TARGET_ID_INVALID;
    if (!ctx || !info) return false;
    if (info->alias || info->imported || info->kind == BM_TARGET_INTERFACE_LIBRARY || info->kind == BM_TARGET_UTILITY) {
        return true;
    }
    if (cg_target_raw_property_nonempty(ctx->model, info->id, "PRECOMPILE_HEADERS") ||
        cg_target_raw_property_nonempty(ctx->model, info->id, "PRECOMPILE_HEADERS_REUSE_FROM")) {
        nob_log(NOB_ERROR,
                "codegen: PRECOMPILE_HEADERS semantics are not supported yet for target '%.*s'",
                (int)info->name.count,
                info->name.data ? info->name.data : "");
        return false;
    }

    visited = arena_alloc_array_zero(ctx->scratch, uint8_t, ctx->target_count);
    if (!visited && ctx->target_count > 0) return false;
    if (cg_target_link_closure_has_interface_pch(ctx, info->id, visited, &offender)) {
        String_View offender_name = bm_query_target_name(ctx->model, offender);
        nob_log(NOB_ERROR,
                "codegen: target '%.*s' depends on unsupported INTERFACE_PRECOMPILE_HEADERS from '%.*s'",
                (int)info->name.count,
                info->name.data ? info->name.data : "",
                (int)offender_name.count,
                offender_name.data ? offender_name.data : "");
        return false;
    }
    return true;
}

static bool cg_reject_unsupported_platform_target_properties(CG_Context *ctx, const CG_Target_Info *info) {
    if (!ctx || !info) return false;
    if (info->alias || info->imported) return true;

    if (bm_query_target_macosx_bundle(ctx->model, info->id)) {
        nob_log(NOB_ERROR,
                "codegen: MACOSX_BUNDLE is not supported yet for target '%.*s'",
                (int)info->name.count,
                info->name.data ? info->name.data : "");
        return false;
    }
    if (bm_query_target_win32_executable(ctx->model, info->id)) {
        nob_log(NOB_ERROR,
                "codegen: WIN32_EXECUTABLE subsystem handling is not supported yet for target '%.*s'",
                (int)info->name.count,
                info->name.data ? info->name.data : "");
        return false;
    }
    return true;
}

typedef struct {
    CG_Context *ctx;
    BM_Query_Eval_Context eval_ctx;
} CG_Genex_Eval_Data;

static String_View cg_genex_target_property_cb(void *userdata, String_View target_name, String_View property_name) {
    CG_Genex_Eval_Data *data = (CG_Genex_Eval_Data*)userdata;
    BM_Target_Id id = BM_TARGET_ID_INVALID;
    String_View out = nob_sv_from_cstr("");
    if (!data || !data->ctx) return nob_sv_from_cstr("");
    id = bm_query_target_by_name(data->ctx->model, target_name);
    if (!bm_target_id_is_valid(id)) return nob_sv_from_cstr("");
    if (!bm_query_target_modeled_property_value(data->ctx->model,
                                                id,
                                                property_name,
                                                data->ctx->scratch,
                                                &out)) {
        return nob_sv_from_cstr("");
    }
    if (out.count > 0) return out;
    if (!bm_query_target_raw_property_value(data->ctx->model, id, property_name, data->ctx->scratch, &out)) {
        return nob_sv_from_cstr("");
    }
    return out;
}

static String_View cg_genex_target_file_cb(void *userdata, String_View target_name) {
    CG_Genex_Eval_Data *data = (CG_Genex_Eval_Data*)userdata;
    CG_Resolved_Target_Ref resolved = {0};
    String_View out = nob_sv_from_cstr("");
    if (!data || !data->ctx) return nob_sv_from_cstr("");
    if (!cg_resolve_target_ref(data->ctx, &data->eval_ctx, target_name, &resolved)) return nob_sv_from_cstr("");
    out = resolved.effective_file;
    if (out.count > 0 && !cg_rebase_path_from_cwd(data->ctx, out, &out)) return nob_sv_from_cstr("");
    return out;
}

static String_View cg_genex_target_linker_file_cb(void *userdata, String_View target_name) {
    CG_Genex_Eval_Data *data = (CG_Genex_Eval_Data*)userdata;
    CG_Resolved_Target_Ref resolved = {0};
    String_View out = nob_sv_from_cstr("");
    if (!data || !data->ctx) return nob_sv_from_cstr("");
    if (!cg_resolve_target_ref(data->ctx, &data->eval_ctx, target_name, &resolved)) return nob_sv_from_cstr("");
    out = resolved.effective_linker_file;
    if (out.count > 0 && !cg_rebase_path_from_cwd(data->ctx, out, &out)) return nob_sv_from_cstr("");
    return out;
}

bool cg_eval_string_for_config(CG_Context *ctx,
                               BM_Target_Id current_target_id,
                               BM_Query_Usage_Mode usage_mode,
                               String_View config,
                               String_View compile_language,
                               String_View raw,
                               String_View *out) {
    CG_Genex_Eval_Data data = {0};
    Genex_Context gx = {0};
    Genex_Result result = {0};
    if (out) *out = nob_sv_from_cstr("");
    if (!ctx || !out) return false;
    data.ctx = ctx;
    data.eval_ctx = cg_make_query_ctx(ctx, current_target_id, usage_mode, config, compile_language);

    gx.arena = ctx->scratch;
    gx.config = config;
    gx.current_target_name = bm_query_target_name(ctx->model, current_target_id);
    gx.platform_id = ctx->policy.platform_id;
    gx.compile_language = compile_language;
    gx.install_prefix = data.eval_ctx.install_prefix;
    gx.read_target_property = cg_genex_target_property_cb;
    gx.read_target_file = cg_genex_target_file_cb;
    gx.read_target_linker_file = cg_genex_target_linker_file_cb;
    gx.userdata = &data;
    gx.link_only_active = usage_mode == BM_QUERY_USAGE_LINK;
    gx.build_interface_active = data.eval_ctx.build_interface_active;
    gx.build_local_interface_active = data.eval_ctx.build_local_interface_active;
    gx.install_interface_active = data.eval_ctx.install_interface_active;
    gx.target_name_case_insensitive = false;
    gx.max_depth = 128;
    gx.max_target_property_depth = 64;

    result = genex_eval(&gx, raw);
    if (result.status != GENEX_OK) return false;
    *out = result.value;
    return true;
}

static bool cg_resolve_model_string_with_query_ctx(CG_Context *ctx,
                                                   const BM_Query_Eval_Context *qctx,
                                                   String_View raw,
                                                   String_View *out) {
    if (out) *out = nob_sv_from_cstr("");
    if (!ctx || !qctx || !out) return false;
    return bm_query_resolve_string_with_context(ctx->model, qctx, ctx->scratch, raw, out);
}

static BM_Query_Eval_Context cg_make_query_ctx(CG_Context *ctx,
                                               BM_Target_Id current_target_id,
                                               BM_Query_Usage_Mode usage_mode,
                                               String_View config,
                                               String_View compile_language) {
    BM_Query_Eval_Context qctx = {0};
    qctx.current_target_id = current_target_id;
    qctx.usage_mode = usage_mode;
    qctx.config = config;
    qctx.compile_language = compile_language;
    qctx.platform_id = ctx ? ctx->policy.platform_id : nob_sv_from_cstr("");
    qctx.build_interface_active = true;
    qctx.build_local_interface_active = true;
    qctx.install_interface_active = false;
    return qctx;
}

static bool cg_parse_int_sv(String_View value, int *out) {
    char buf[32];
    char *end = NULL;
    long parsed = 0;
    if (out) *out = 0;
    if (!out || value.count == 0 || value.count >= sizeof(buf)) return false;
    memcpy(buf, value.data, value.count);
    buf[value.count] = '\0';
    parsed = strtol(buf, &end, 10);
    if (!end || *end != '\0') return false;
    *out = (int)parsed;
    return true;
}

static String_View cg_compile_language_sv(CG_Source_Lang lang) {
    return lang == CG_SOURCE_LANG_CXX ? nob_sv_from_cstr("CXX") : nob_sv_from_cstr("C");
}

static String_View cg_language_standard_flag(Arena *scratch,
                                             CG_Source_Lang lang,
                                             int standard,
                                             bool extensions) {
    const char *family = extensions ? "gnu" : "c";
    const char *mapped = NULL;
    char *copy = NULL;
    Nob_String_Builder sb = {0};
    if (!scratch || standard <= 0) return nob_sv_from_cstr("");
    if (lang == CG_SOURCE_LANG_CXX) family = extensions ? "gnu++" : "c++";

    if (lang == CG_SOURCE_LANG_C && standard == 23) {
        mapped = extensions ? "gnu2x" : "c2x";
    } else {
        nob_sb_append_cstr(&sb, family);
        nob_sb_append_cstr(&sb, nob_temp_sprintf("%d", standard));
        copy = arena_strndup(scratch, sb.items ? sb.items : "", sb.count);
        nob_sb_free(sb);
        return copy ? nob_sv_from_parts(copy, strlen(copy)) : nob_sv_from_cstr("");
    }
    copy = arena_strdup(scratch, mapped);
    return copy ? nob_sv_from_cstr(copy) : nob_sv_from_cstr("");
}

static bool cg_collect_standard_arg(CG_Context *ctx,
                                    BM_Target_Id id,
                                    String_View config,
                                    CG_Source_Lang lang,
                                    String_View **out_args) {
    BM_Query_Eval_Context qctx = cg_make_query_ctx(ctx, id, BM_QUERY_USAGE_COMPILE, config, cg_compile_language_sv(lang));
    BM_String_Span features = {0};
    int required_standard = 0;
    bool extensions = true;
    String_View direct_standard = lang == CG_SOURCE_LANG_CXX
        ? bm_query_target_cxx_standard(ctx->model, id)
        : bm_query_target_c_standard(ctx->model, id);
    if (!ctx || !out_args) return false;

    if (direct_standard.count > 0) (void)cg_parse_int_sv(direct_standard, &required_standard);
    if (!cg_query_effective_values_cached(ctx, id, &qctx, CG_EFFECTIVE_COMPILE_FEATURES, &features)) return false;
    for (size_t i = 0; i < features.count; ++i) {
        const BM_Compile_Feature_Info *info = bm_compile_feature_lookup(features.items[i]);
        if (!info) continue;
        if ((lang == CG_SOURCE_LANG_C && info->lang != BM_COMPILE_FEATURE_LANG_C) ||
            (lang == CG_SOURCE_LANG_CXX && info->lang != BM_COMPILE_FEATURE_LANG_CXX)) {
            continue;
        }
        if (info->standard > required_standard) required_standard = info->standard;
    }
    if (required_standard <= 0) return true;

    if (lang == CG_SOURCE_LANG_CXX) {
        if (bm_query_target_raw_property_items(ctx->model, id, nob_sv_from_cstr("CXX_EXTENSIONS")).count > 0) {
            extensions = bm_query_target_cxx_extensions(ctx->model, id);
        }
    } else {
        if (bm_query_target_raw_property_items(ctx->model, id, nob_sv_from_cstr("C_EXTENSIONS")).count > 0) {
            extensions = bm_query_target_c_extensions(ctx->model, id);
        }
    }

    {
        String_View flag = cg_language_standard_flag(ctx->scratch, lang, required_standard, extensions);
        char *arg = NULL;
        if (flag.count == 0) return false;
        arg = cg_arena_sprintf(ctx->scratch, "-std=%.*s", (int)flag.count, flag.data ? flag.data : "");
        if (!arg || !arena_arr_push(ctx->scratch, *out_args, nob_sv_from_cstr(arg))) return false;
    }
    return true;
}

static bool cg_target_needs_cxx_linker_for_config(CG_Context *ctx,
                                                  BM_Target_Id id,
                                                  String_View config,
                                                  bool *out) {
    BM_Query_Eval_Context qctx = {0};
    String_View link_language = nob_sv_from_cstr("");
    if (out) *out = false;
    if (!ctx || !out || !bm_target_id_is_valid(id)) return false;

    id = cg_resolve_alias_target(ctx, id);
    if (!bm_target_id_is_valid(id)) return false;

    qctx = cg_make_query_ctx(ctx, id, BM_QUERY_USAGE_LINK, config, nob_sv_from_cstr(""));
    if (!cg_query_effective_link_language_cached(ctx, id, &qctx, &link_language)) return false;
    *out = cg_sv_eq_ci(link_language, nob_sv_from_cstr("CXX"));
    return true;
}

static bool cg_collect_build_dependencies(CG_Context *ctx, BM_Target_Id id, BM_Target_Id **out) {
    BM_Target_Id_Span explicit_deps = bm_query_target_dependencies_explicit(ctx->model, id);
    if (!ctx || !out) return false;

    for (size_t i = 0; i < explicit_deps.count; ++i) {
        BM_Target_Id dep = cg_resolve_alias_target(ctx, explicit_deps.items[i]);
        const CG_Target_Info *dep_info = cg_target_info(ctx, dep);
        if (!bm_target_id_is_valid(dep) || !dep_info || dep_info->imported) continue;
        if (!cg_collect_unique_target(ctx->scratch, out, dep)) return false;
    }

    for (size_t branch = 0; branch <= arena_arr_len(ctx->known_configs); ++branch) {
        BM_Link_Item_Span libs = {0};
        String_View config = branch < arena_arr_len(ctx->known_configs) ? ctx->known_configs[branch] : nob_sv_from_cstr("");
        BM_Query_Eval_Context qctx = cg_make_query_ctx(ctx, id, BM_QUERY_USAGE_LINK, config, nob_sv_from_cstr(""));
        if (!cg_query_effective_link_items_cached(ctx, id, &qctx, &libs)) {
            return false;
        }
        for (size_t i = 0; i < libs.count; ++i) {
            CG_Resolved_Target_Ref dep = {0};
            if (!cg_resolve_link_item_ref(ctx, &qctx, libs.items[i], &dep)) continue;
            if (dep.imported) continue;
            if (!cg_collect_unique_target(ctx->scratch, out, dep.target_id)) return false;
        }
    }

    return true;
}

static bool cg_collect_target_steps(CG_Context *ctx,
                                    BM_Target_Id id,
                                    BM_Build_Step_Kind kind,
                                    BM_Build_Step_Id **out) {
    if (!ctx || !out) return false;
    for (size_t i = 0; i < ctx->build_step_count; ++i) {
        if (bm_query_build_step_owner_target(ctx->model, (BM_Build_Step_Id)i) != id) continue;
        if (bm_query_build_step_kind(ctx->model, (BM_Build_Step_Id)i) != kind) continue;
        if (!arena_arr_push(ctx->scratch, *out, (BM_Build_Step_Id)i)) return false;
    }
    return true;
}

static bool cg_collect_unique_build_step(Arena *scratch, BM_Build_Step_Id **list, BM_Build_Step_Id value) {
    for (size_t i = 0; i < arena_arr_len(*list); ++i) {
        if ((*list)[i] == value) return true;
    }
    return arena_arr_push(scratch, *list, value);
}

static bool cg_collect_generated_source_steps(CG_Context *ctx,
                                              const CG_Source_Info *sources,
                                              size_t source_count,
                                              BM_Build_Step_Id **out) {
    if (!ctx || !out) return false;
    for (size_t i = 0; i < source_count; ++i) {
        if (!bm_build_step_id_is_valid(sources[i].producer_step_id)) continue;
        if (!cg_collect_unique_build_step(ctx->scratch, out, sources[i].producer_step_id)) return false;
    }
    return true;
}

static bool cg_collect_compile_sources(CG_Context *ctx, BM_Target_Id id, CG_Source_Info **out) {
    size_t source_count = bm_query_target_source_count(ctx->model, id);

    for (size_t i = 0; i < source_count; ++i) {
        String_View src = bm_query_target_source_effective(ctx->model, id, i);
        String_View source_language = bm_query_target_source_effective_language(ctx->model, id, i);
        String_View raw_source_language = nob_sv_trim(bm_query_target_source_language(ctx->model, id, i));
        String_View rebased = {0};
        CG_Source_Lang lang = CG_SOURCE_LANG_C;
        bool dup = false;

        if (!bm_query_target_source_is_compile_input(ctx->model, id, i)) continue;
        if (!cg_check_no_genex("target source", src)) return false;
        if (source_language.count == 0) {
            if (raw_source_language.count > 0) {
                nob_log(NOB_ERROR,
                        "codegen: unsupported source LANGUAGE on target '%.*s': %.*s",
                        (int)bm_query_target_name(ctx->model, id).count,
                        bm_query_target_name(ctx->model, id).data ? bm_query_target_name(ctx->model, id).data : "",
                        (int)raw_source_language.count,
                        raw_source_language.data ? raw_source_language.data : "");
                return false;
            }
            continue;
        }
        if (!cg_source_lang_from_effective_language(source_language, &lang)) {
            nob_log(NOB_ERROR,
                    "codegen: unsupported effective source language on target '%.*s': %.*s",
                    (int)bm_query_target_name(ctx->model, id).count,
                    bm_query_target_name(ctx->model, id).data ? bm_query_target_name(ctx->model, id).data : "",
                    (int)source_language.count,
                    source_language.data ? source_language.data : "");
            return false;
        }

        if (!cg_rebase_path_from_cwd(ctx, src, &rebased)) return false;
        for (size_t j = 0; j < arena_arr_len(*out); ++j) {
            if (nob_sv_eq((*out)[j].path, rebased)) {
                dup = true;
                break;
            }
        }
        if (!dup && !arena_arr_push(ctx->scratch, *out, ((CG_Source_Info){
                .path = rebased,
                .lang = lang,
                .source_index = i,
                .producer_step_id = bm_query_target_source_producer_step(ctx->model, id, i),
            }))) {
            return false;
        }
    }

    return true;
}

static bool cg_append_split_item(Arena *scratch,
                                 BM_String_Item_View **out,
                                 BM_String_Item_View item,
                                 String_View value) {
    size_t start = 0;
    if (!scratch || !out) return false;
    if (value.count == 0) return true;
    for (size_t i = 0; i <= value.count; ++i) {
        bool sep = (i == value.count) || (value.data[i] == ';');
        BM_String_Item_View copy = item;
        String_View piece = {0};
        if (!sep) continue;
        piece = nob_sv_trim(nob_sv_from_parts(value.data + start, i - start));
        start = i + 1;
        if (piece.count == 0) continue;
        copy.value = piece;
        if (!arena_arr_push(scratch, *out, copy)) return false;
    }
    return true;
}

static bool cg_collect_source_compile_items(CG_Context *ctx,
                                            BM_Target_Id id,
                                            String_View config,
                                            CG_Source_Lang lang,
                                            BM_String_Item_Span items,
                                            BM_String_Item_View **out) {
    for (size_t i = 0; i < items.count; ++i) {
        String_View value = items.items[i].value;
        if (cg_is_genex(value) &&
            !cg_eval_string_for_config(ctx,
                                       id,
                                       BM_QUERY_USAGE_COMPILE,
                                       config,
                                       cg_compile_language_sv(lang),
                                       value,
                                       &value)) {
            return false;
        }
        if (!cg_append_split_item(ctx->scratch, out, items.items[i], value)) return false;
    }
    return true;
}

static bool cg_collect_source_compile_args(CG_Context *ctx,
                                           BM_Target_Id id,
                                           String_View config,
                                           const CG_Source_Info *source,
                                           String_View **out) {
    BM_String_Item_Span includes = {0};
    BM_String_Item_Span defs = {0};
    BM_String_Item_Span opts = {0};
    BM_String_Item_View *source_includes = NULL;
    BM_String_Item_View *source_defs = NULL;
    BM_String_Item_View *source_opts = NULL;
    if (!ctx || !source || !out) return false;

    includes = bm_query_target_source_include_directories(ctx->model, id, source->source_index);
    defs = bm_query_target_source_compile_definitions(ctx->model, id, source->source_index);
    opts = bm_query_target_source_compile_options(ctx->model, id, source->source_index);

    if (!cg_collect_source_compile_items(ctx, id, config, source->lang, includes, &source_includes) ||
        !cg_collect_source_compile_items(ctx, id, config, source->lang, defs, &source_defs) ||
        !cg_collect_source_compile_items(ctx, id, config, source->lang, opts, &source_opts)) {
        return false;
    }

    for (size_t i = 0; i < arena_arr_len(source_includes); ++i) {
        String_View path = {0};
        char *arg = NULL;
        if (!cg_rebase_from_provenance(ctx, source_includes[i].value, source_includes[i].provenance, &path)) return false;
        if (cg_policy_is_windows(ctx)) {
            arg = cg_arena_sprintf(ctx->scratch, "/I%.*s", (int)path.count, path.data ? path.data : "");
        } else {
            arg = cg_arena_sprintf(ctx->scratch, "-I%.*s", (int)path.count, path.data ? path.data : "");
        }
        if (!arg || !cg_collect_unique_path(ctx->scratch, out, nob_sv_from_cstr(arg))) return false;
    }

    for (size_t i = 0; i < arena_arr_len(source_defs); ++i) {
        String_View item = source_defs[i].value;
        char *arg = NULL;
        if (cg_policy_is_windows(ctx)) {
            if (cg_sv_has_prefix(item, "-D")) item = nob_sv_from_parts(item.data + 2, item.count - 2);
            arg = cg_arena_sprintf(ctx->scratch, "/D%.*s", (int)item.count, item.data ? item.data : "");
        } else if (cg_sv_has_prefix(item, "-D")) {
            arg = arena_strndup(ctx->scratch, item.data, item.count);
        } else {
            arg = cg_arena_sprintf(ctx->scratch, "-D%.*s", (int)item.count, item.data ? item.data : "");
        }
        if (!arg || !cg_collect_unique_path(ctx->scratch, out, nob_sv_from_cstr(arg))) return false;
    }

    for (size_t i = 0; i < arena_arr_len(source_opts); ++i) {
        if (cg_policy_is_windows(ctx) && cg_sv_has_prefix(source_opts[i].value, "-std=")) continue;
        if (!cg_collect_unique_path(ctx->scratch, out, source_opts[i].value)) return false;
    }

    return true;
}

static bool cg_collect_compile_args(CG_Context *ctx,
                                    BM_Target_Id id,
                                    String_View config,
                                    const CG_Source_Info *source,
                                    String_View **out) {
    BM_String_Item_Span includes = {0};
    BM_String_Item_Span defs = {0};
    BM_String_Item_Span opts = {0};
    BM_Query_Eval_Context qctx = {0};
    if (!ctx || !source || !out) return false;
    qctx = cg_make_query_ctx(ctx, id, BM_QUERY_USAGE_COMPILE, config, cg_compile_language_sv(source->lang));
    if (!cg_query_effective_items_cached(ctx, id, &qctx, CG_EFFECTIVE_INCLUDE_DIRECTORIES, &includes) ||
        !cg_query_effective_items_cached(ctx, id, &qctx, CG_EFFECTIVE_COMPILE_DEFINITIONS, &defs) ||
        !cg_query_effective_items_cached(ctx, id, &qctx, CG_EFFECTIVE_COMPILE_OPTIONS, &opts) ||
        !cg_collect_standard_arg(ctx, id, config, source->lang, out)) {
        return false;
    }

    for (size_t i = 0; i < includes.count; ++i) {
        String_View path = {0};
        if (!cg_rebase_from_provenance(ctx, includes.items[i].value, includes.items[i].provenance, &path)) return false;
        if (cg_policy_is_windows(ctx)) {
            char *arg = cg_arena_sprintf(ctx->scratch, "/I%.*s", (int)path.count, path.data ? path.data : "");
            if (!arg || !cg_collect_unique_path(ctx->scratch, out, nob_sv_from_cstr(arg))) return false;
        } else if (includes.items[i].flags & BM_ITEM_FLAG_SYSTEM) {
            if (!cg_collect_unique_path(ctx->scratch, out, nob_sv_from_cstr("-isystem")) ||
                !cg_collect_unique_path(ctx->scratch, out, path)) {
                return false;
            }
        } else {
            char *arg = cg_arena_sprintf(ctx->scratch, "-I%.*s", (int)path.count, path.data ? path.data : "");
            if (!arg || !cg_collect_unique_path(ctx->scratch, out, nob_sv_from_cstr(arg))) return false;
        }
    }

    for (size_t i = 0; i < defs.count; ++i) {
        String_View item = defs.items[i].value;
        char *arg = NULL;
        if (cg_policy_is_windows(ctx)) {
            if (cg_sv_has_prefix(item, "-D")) {
                item = nob_sv_from_parts(item.data + 2, item.count - 2);
            }
            arg = cg_arena_sprintf(ctx->scratch, "/D%.*s", (int)item.count, item.data ? item.data : "");
        } else if (cg_sv_has_prefix(item, "-D")) {
            arg = arena_strndup(ctx->scratch, item.data, item.count);
        } else {
            arg = cg_arena_sprintf(ctx->scratch, "-D%.*s", (int)item.count, item.data ? item.data : "");
        }
        if (!arg || !cg_collect_unique_path(ctx->scratch, out, nob_sv_from_cstr(arg))) return false;
    }

    for (size_t i = 0; i < opts.count; ++i) {
        if (cg_policy_is_windows(ctx) && cg_sv_has_prefix(opts.items[i].value, "-std=")) continue;
        if (!cg_collect_unique_path(ctx->scratch, out, opts.items[i].value)) return false;
    }

    return cg_collect_source_compile_args(ctx, id, config, source, out);
}

static bool cg_collect_link_dir_args(CG_Context *ctx,
                                     BM_Target_Id id,
                                     String_View config,
                                     String_View **out) {
    BM_String_Item_Span dirs = {0};
    BM_Query_Eval_Context qctx = cg_make_query_ctx(ctx, id, BM_QUERY_USAGE_LINK, config, nob_sv_from_cstr(""));
    if (!cg_query_effective_items_cached(ctx, id, &qctx, CG_EFFECTIVE_LINK_DIRECTORIES, &dirs)) return false;

    for (size_t i = 0; i < dirs.count; ++i) {
        String_View path = {0};
        char *arg = NULL;
        if (cg_sv_has_prefix(dirs.items[i].value, "-L")) {
            if (cg_policy_is_windows(ctx)) {
                path = nob_sv_trim(nob_sv_from_parts(dirs.items[i].value.data + 2, dirs.items[i].value.count - 2));
                arg = cg_arena_sprintf(ctx->scratch, "/LIBPATH:%.*s", (int)path.count, path.data ? path.data : "");
                if (!arg || !cg_collect_unique_path(ctx->scratch, out, nob_sv_from_cstr(arg))) return false;
            } else if (!cg_collect_unique_path(ctx->scratch, out, dirs.items[i].value)) {
                return false;
            }
            continue;
        }
        if (!cg_rebase_from_provenance(ctx, dirs.items[i].value, dirs.items[i].provenance, &path)) return false;
        arg = cg_policy_is_windows(ctx)
            ? cg_arena_sprintf(ctx->scratch, "/LIBPATH:%.*s", (int)path.count, path.data ? path.data : "")
            : cg_arena_sprintf(ctx->scratch, "-L%.*s", (int)path.count, path.data ? path.data : "");
        if (!arg || !cg_collect_unique_path(ctx->scratch, out, nob_sv_from_cstr(arg))) return false;
    }

    return true;
}

static bool cg_collect_link_option_args(CG_Context *ctx,
                                        BM_Target_Id id,
                                        String_View config,
                                        String_View **out) {
    BM_String_Item_Span opts = {0};
    BM_Query_Eval_Context qctx = cg_make_query_ctx(ctx, id, BM_QUERY_USAGE_LINK, config, nob_sv_from_cstr(""));
    if (!cg_query_effective_items_cached(ctx, id, &qctx, CG_EFFECTIVE_LINK_OPTIONS, &opts)) return false;

    for (size_t i = 0; i < opts.count; ++i) {
        if (!cg_collect_unique_path(ctx->scratch, out, opts.items[i].value)) return false;
    }

    return true;
}

static bool cg_collect_link_library_args(CG_Context *ctx,
                                         BM_Target_Id id,
                                         String_View config,
                                         String_View **out_args,
                                         String_View **out_rebuild_inputs) {
    BM_Link_Item_Span libs = {0};
    BM_Query_Eval_Context qctx = cg_make_query_ctx(ctx, id, BM_QUERY_USAGE_LINK, config, nob_sv_from_cstr(""));
    BM_Target_Id *seen_targets = NULL;
    if (!cg_query_effective_link_items_cached(ctx, id, &qctx, &libs)) return false;

    for (size_t i = 0; i < libs.count; ++i) {
        CG_Resolved_Target_Ref dep = {0};
        String_View item = libs.items[i].value;

        if (cg_resolve_link_item_ref(ctx, &qctx, libs.items[i], &dep)) {
            String_View dep_path = {0};
            if (dep.usage_only) continue;
            if (dep.target_kind == BM_TARGET_MODULE_LIBRARY) {
                nob_log(NOB_ERROR, "codegen: target '%.*s' is not linkable in the selected backend",
                        (int)item.count, item.data ? item.data : "");
                return false;
            }
            if (dep.imported) {
                String_View rebased_dep_path = {0};
                if (dep.target_kind == BM_TARGET_EXECUTABLE) {
                    nob_log(NOB_ERROR, "codegen: imported executable '%.*s' is not a valid link input",
                            (int)item.count, item.data ? item.data : "");
                    return false;
                }
                dep_path = dep.effective_linker_file.count > 0 ? dep.effective_linker_file : dep.effective_file;
                if (dep_path.count == 0) {
                    nob_log(NOB_ERROR,
                            "codegen: imported target '%.*s' has no linker file for config '%.*s'",
                            (int)item.count,
                            item.data ? item.data : "",
                            (int)config.count,
                            config.data ? config.data : "");
                    return false;
                }
                if (!cg_rebase_path_from_cwd(ctx, dep_path, &rebased_dep_path)) return false;
                if (!cg_collect_unique_target(ctx->scratch, &seen_targets, dep.target_id)) return false;
                if ((out_args && !cg_collect_unique_path(ctx->scratch, out_args, rebased_dep_path)) ||
                    (out_rebuild_inputs && !cg_collect_unique_path(ctx->scratch, out_rebuild_inputs, rebased_dep_path))) {
                    return false;
                }
                continue;
            }
            if (!dep.linkable_artifact) {
                nob_log(NOB_ERROR, "codegen: local link target '%.*s' is not linkable in the selected backend",
                        (int)item.count, item.data ? item.data : "");
                return false;
            }
            if (!cg_collect_unique_target(ctx->scratch, &seen_targets, dep.target_id)) return false;
            if ((out_args && !cg_collect_unique_path(ctx->scratch, out_args, dep.rebuild_input_path)) ||
                (out_rebuild_inputs && !cg_collect_unique_path(ctx->scratch, out_rebuild_inputs, dep.rebuild_input_path))) {
                return false;
            }
            continue;
        }

        if (cg_sv_has_prefix(item, "-")) {
            if (out_args && !cg_collect_unique_path(ctx->scratch, out_args, item)) return false;
            continue;
        }

        if (cg_sv_contains(item, "/")) {
            String_View path = {0};
            if (!cg_rebase_from_provenance(ctx, item, libs.items[i].provenance, &path)) return false;
            if (out_args && !cg_collect_unique_path(ctx->scratch, out_args, path)) return false;
            continue;
        }

        if (cg_is_link_file_like(item)) {
            if (out_args && !cg_collect_unique_path(ctx->scratch, out_args, item)) return false;
            continue;
        }

        if (cg_is_bare_library_name(item)) {
            char *arg = cg_policy_is_windows(ctx)
                ? cg_arena_sprintf(ctx->scratch, "%.*s.lib", (int)item.count, item.data ? item.data : "")
                : cg_arena_sprintf(ctx->scratch, "-l%.*s", (int)item.count, item.data ? item.data : "");
            if (!arg || (out_args && !cg_collect_unique_path(ctx->scratch, out_args, nob_sv_from_cstr(arg)))) return false;
            continue;
        }

        nob_log(NOB_ERROR, "codegen: unsupported link library item: %.*s",
                (int)item.count, item.data ? item.data : "");
        return false;
    }

    return true;
}

bool cg_emit_cmd_append_sv(Nob_String_Builder *out, const char *cmd_var, String_View arg) {
    if (!out || !cmd_var) return false;
    nob_sb_append_cstr(out, "        nob_cmd_append(&");
    nob_sb_append_cstr(out, cmd_var);
    nob_sb_append_cstr(out, ", ");
    if (!cg_sb_append_c_string(out, arg)) return false;
    nob_sb_append_cstr(out, ");\n");
    return true;
}

bool cg_emit_cmd_append_expr(Nob_String_Builder *out, const char *cmd_var, const char *expr) {
    if (!out || !cmd_var || !expr) return false;
    nob_sb_append_cstr(out, "        nob_cmd_append(&");
    nob_sb_append_cstr(out, cmd_var);
    nob_sb_append_cstr(out, ", ");
    nob_sb_append_cstr(out, expr);
    nob_sb_append_cstr(out, ");\n");
    return true;
}

static bool cg_emit_cmd_append_toolchain(Nob_String_Builder *out, const char *cmd_var, bool use_cxx) {
    if (!out || !cmd_var) return false;
    nob_sb_append_cstr(out, "        append_toolchain_cmd(&");
    nob_sb_append_cstr(out, cmd_var);
    nob_sb_append_cstr(out, ", ");
    nob_sb_append_cstr(out, use_cxx ? "true" : "false");
    nob_sb_append_cstr(out, ");\n");
    return true;
}

static bool cg_emit_cmd_append_archive_tool(Nob_String_Builder *out, const char *cmd_var) {
    if (!out || !cmd_var) return false;
    nob_sb_append_cstr(out, "        append_archive_tool_cmd(&");
    nob_sb_append_cstr(out, cmd_var);
    nob_sb_append_cstr(out, ");\n");
    return true;
}

static bool cg_emit_cmd_append_link_tool(CG_Context *ctx,
                                         Nob_String_Builder *out,
                                         const char *cmd_var,
                                         bool use_cxx) {
    if (!ctx || !out || !cmd_var) return false;
    if (ctx->policy.use_compiler_driver_for_executable_link ||
        ctx->policy.use_compiler_driver_for_shared_link ||
        ctx->policy.use_compiler_driver_for_module_link) {
        return cg_emit_cmd_append_toolchain(out, cmd_var, use_cxx);
    }
    nob_sb_append_cstr(out, "        append_link_tool_cmd(&");
    nob_sb_append_cstr(out, cmd_var);
    nob_sb_append_cstr(out, ", ");
    nob_sb_append_cstr(out, use_cxx ? "true" : "false");
    nob_sb_append_cstr(out, ");\n");
    return true;
}

static bool cg_emit_runtime_config_branches_prefix_for_var(CG_Context *ctx,
                                                           Nob_String_Builder *out,
                                                           size_t branch_index,
                                                           const char *config_var);

static bool cg_emit_runtime_config_branches_prefix(CG_Context *ctx,
                                                   Nob_String_Builder *out,
                                                   size_t branch_index) {
    return cg_emit_runtime_config_branches_prefix_for_var(ctx, out, branch_index, "g_build_config");
}

static bool cg_emit_runtime_config_branches_prefix_for_var(CG_Context *ctx,
                                                           Nob_String_Builder *out,
                                                           size_t branch_index,
                                                           const char *config_var) {
    if (!ctx || !out) return false;
    if (branch_index < arena_arr_len(ctx->known_configs)) {
        nob_sb_append_cstr(out, branch_index == 0 ? "        if (config_matches(" : "        } else if (config_matches(");
        nob_sb_append_cstr(out, config_var ? config_var : "g_build_config");
        nob_sb_append_cstr(out, ", ");
        if (!cg_sb_append_c_string(out, ctx->known_configs[branch_index])) return false;
        nob_sb_append_cstr(out, ")) {\n");
        return true;
    }
    if (arena_arr_len(ctx->known_configs) == 0) return true;
    nob_sb_append_cstr(out, "        } else {\n");
    return true;
}

static bool cg_emit_runtime_config_branches_suffix(CG_Context *ctx, Nob_String_Builder *out) {
    if (!ctx || !out) return false;
    if (arena_arr_len(ctx->known_configs) > 0) nob_sb_append_cstr(out, "        }\n");
    return true;
}

static bool cg_emit_compile_args_runtime(CG_Context *ctx,
                                         BM_Target_Id id,
                                         const CG_Source_Info *source,
                                         const char *cmd_var,
                                         Nob_String_Builder *out) {
    if (!ctx || !cmd_var || !out) return false;
    for (size_t branch = 0; branch <= arena_arr_len(ctx->known_configs); ++branch) {
        String_View *args = NULL;
        String_View config = branch < arena_arr_len(ctx->known_configs) ? ctx->known_configs[branch] : nob_sv_from_cstr("");
        if (!cg_collect_compile_args(ctx, id, config, source, &args) ||
            !cg_emit_runtime_config_branches_prefix(ctx, out, branch)) {
            return false;
        }
        for (size_t i = 0; i < arena_arr_len(args); ++i) {
            if (!cg_emit_cmd_append_sv(out, cmd_var, args[i])) return false;
        }
    }
    return cg_emit_runtime_config_branches_suffix(ctx, out);
}

static bool cg_emit_link_args_runtime(CG_Context *ctx,
                                      const CG_Target_Info *info,
                                      const CG_Source_Info *sources,
                                      size_t source_count,
                                      String_View object_dir,
                                      Nob_String_Builder *out) {
    if (!ctx || !info || !out) return false;
    (void)sources;
    for (size_t branch = 0; branch <= arena_arr_len(ctx->known_configs); ++branch) {
        String_View config = branch < arena_arr_len(ctx->known_configs) ? ctx->known_configs[branch] : nob_sv_from_cstr("");
        String_View *link_dir_args = NULL;
        String_View *link_opt_args = NULL;
        String_View *link_lib_args = NULL;
        String_View *link_rebuild_inputs = NULL;
        bool needs_cxx_linker = false;
        BM_Target_Artifact_View artifact = {0};
        BM_Target_Artifact_View linker_artifact = {0};
        if (!cg_collect_link_dir_args(ctx, info->id, config, &link_dir_args) ||
            !cg_collect_link_option_args(ctx, info->id, config, &link_opt_args) ||
            !cg_collect_link_library_args(ctx, info->id, config, &link_lib_args, &link_rebuild_inputs) ||
            !cg_target_needs_cxx_linker_for_config(ctx, info->id, config, &needs_cxx_linker) ||
            !cg_target_artifact_for_config_or_empty(info, BM_TARGET_ARTIFACT_RUNTIME, config, &artifact) ||
            !cg_target_artifact_for_config_or_empty(info, BM_TARGET_ARTIFACT_LINKER, config, &linker_artifact) ||
            !cg_emit_runtime_config_branches_prefix(ctx, out, branch)) {
            return false;
        }

        nob_sb_append_cstr(out, "        Nob_Cmd link_cmd = {0};\n");
        if (!cg_emit_cmd_append_link_tool(ctx, out, "link_cmd", needs_cxx_linker)) return false;
        if (cg_policy_is_windows(ctx)) {
            char *out_arg = cg_arena_sprintf(ctx->scratch,
                                             "/OUT:%.*s",
                                             (int)artifact.path.count,
                                             artifact.path.data ? artifact.path.data : "");
            if (!out_arg) return false;
            if (!cg_emit_cmd_append_sv(out, "link_cmd", nob_sv_from_cstr("/NOLOGO"))) return false;
            if (info->kind == BM_TARGET_SHARED_LIBRARY) {
                if (!cg_emit_cmd_append_sv(out, "link_cmd", ctx->policy.shared_link_flag)) return false;
            } else if (info->kind == BM_TARGET_MODULE_LIBRARY) {
                if (!cg_emit_cmd_append_sv(out, "link_cmd", ctx->policy.module_link_flag)) return false;
            }
            if (!cg_emit_cmd_append_sv(out, "link_cmd", nob_sv_from_cstr(out_arg))) return false;
            if (linker_artifact.path.count > 0 && !nob_sv_eq(artifact.path, linker_artifact.path)) {
                char *implib_arg = cg_arena_sprintf(ctx->scratch,
                                                    "/IMPLIB:%.*s",
                                                    (int)linker_artifact.path.count,
                                                    linker_artifact.path.data ? linker_artifact.path.data : "");
                if (!implib_arg || !cg_emit_cmd_append_sv(out, "link_cmd", nob_sv_from_cstr(implib_arg))) {
                    return false;
                }
            }
        } else {
            if (info->kind == BM_TARGET_SHARED_LIBRARY) {
                if (!cg_emit_cmd_append_sv(out, "link_cmd", ctx->policy.shared_link_flag)) return false;
            } else if (info->kind == BM_TARGET_MODULE_LIBRARY) {
                if (!cg_emit_cmd_append_sv(out, "link_cmd", ctx->policy.module_link_flag)) return false;
            }
            if (!cg_emit_cmd_append_sv(out, "link_cmd", nob_sv_from_cstr("-o")) ||
                !cg_emit_cmd_append_sv(out, "link_cmd", artifact.path)) {
                return false;
            }
        }
        for (size_t i = 0; i < source_count; ++i) {
            String_View obj_path = {0};
            if (!cg_object_path_for_index(ctx, object_dir, sources, arena_arr_len(sources), i, &obj_path)) {
                return false;
            }
            if (!cg_emit_cmd_append_sv(out, "link_cmd", obj_path)) return false;
        }
        for (size_t i = 0; i < arena_arr_len(link_dir_args); ++i) {
            if (!cg_emit_cmd_append_sv(out, "link_cmd", link_dir_args[i])) return false;
        }
        for (size_t i = 0; i < arena_arr_len(link_opt_args); ++i) {
            if (!cg_emit_cmd_append_sv(out, "link_cmd", link_opt_args[i])) return false;
        }
        for (size_t i = 0; i < arena_arr_len(link_lib_args); ++i) {
            if (!cg_emit_cmd_append_sv(out, "link_cmd", link_lib_args[i])) return false;
        }
        nob_sb_append_cstr(out, "        {\n");
        nob_sb_append_cstr(out, "            bool ok = nob_cmd_run(&link_cmd);\n");
        nob_sb_append_cstr(out, "            nob_cmd_free(link_cmd);\n");
        nob_sb_append_cstr(out, "            if (!ok) return false;\n");
        nob_sb_append_cstr(out, "        }\n");
        nob_sb_append_cstr(out, "        if (!require_paths((const char*[]){");
        if (!cg_sb_append_c_string(out, artifact.path)) return false;
        if (linker_artifact.path.count > 0 && !nob_sv_eq(artifact.path, linker_artifact.path)) {
            nob_sb_append_cstr(out, ", ");
            if (!cg_sb_append_c_string(out, linker_artifact.path)) return false;
        }
        nob_sb_append_cstr(out, "}, ");
        nob_sb_append_cstr(out, (linker_artifact.path.count > 0 && !nob_sv_eq(artifact.path, linker_artifact.path)) ? "2" : "1");
        nob_sb_append_cstr(out, ")) return false;\n");
    }
    return cg_emit_runtime_config_branches_suffix(ctx, out);
}

static bool cg_emit_target_forward_decls(CG_Context *ctx, Nob_String_Builder *out) {
    if (!ctx || !out) return false;
    for (size_t i = 0; i < ctx->target_count; ++i) {
        nob_sb_append_cstr(out, "static bool build_");
        nob_sb_append_cstr(out, ctx->targets[i].ident);
        nob_sb_append_cstr(out, "(void);\n");
    }
    nob_sb_append_cstr(out, "\n");
    return true;
}

static bool cg_emit_step_forward_decls(CG_Context *ctx, Nob_String_Builder *out) {
    if (!ctx || !out) return false;
    for (size_t i = 0; i < ctx->build_step_count; ++i) {
        nob_sb_append_cstr(out, "static bool run_");
        nob_sb_append_cstr(out, ctx->build_steps[i].ident);
        nob_sb_append_cstr(out, "(void);\n");
    }
    nob_sb_append_cstr(out, "\n");
    return true;
}

static bool cg_emit_preferred_tool_resolver(Nob_String_Builder *out,
                                            const char *fn_name,
                                            const char *env_name,
                                            String_View embedded_path,
                                            const char *fallback) {
    if (!out || !fn_name || !env_name || !fallback) return false;
    nob_sb_append_cstr(out, "static const char *");
    nob_sb_append_cstr(out, fn_name);
    nob_sb_append_cstr(out, "(void) {\n");
    nob_sb_append_cstr(out, "    const char *tool = getenv(");
    if (!cg_sb_append_c_string(out, nob_sv_from_cstr(env_name))) return false;
    nob_sb_append_cstr(out, ");\n");
    nob_sb_append_cstr(out, "    if (tool && tool[0] != '\\0') return tool;\n");
    nob_sb_append_cstr(out, "    tool = ");
    if (!cg_sb_append_c_string(out, embedded_path)) return false;
    nob_sb_append_cstr(out, ";\n");
    nob_sb_append_cstr(out, "    if (tool && tool[0] != '\\0') return tool;\n");
    nob_sb_append_cstr(out, "    return ");
    if (!cg_sb_append_c_string(out, nob_sv_from_cstr(fallback))) return false;
    nob_sb_append_cstr(out, ";\n");
    nob_sb_append_cstr(out, "}\n\n");
    return true;
}

#include "nob_codegen_runtime.c"

static bool cg_emit_target_function(CG_Context *ctx, const CG_Target_Info *info, Nob_String_Builder *out) {
    BM_Target_Id *deps = NULL;
    BM_Build_Step_Id *pre_build_steps = NULL;
    BM_Build_Step_Id *generated_steps = NULL;
    BM_Build_Step_Id *pre_link_steps = NULL;
    BM_Build_Step_Id *post_build_steps = NULL;
    CG_Source_Info *sources = NULL;
    String_View object_dir = {0};
    String_View artifact_dir = {0};
    String_View linker_artifact_dir = {0};
    String_View *artifact_dirs = NULL;
    bool needs_pic = false;
    if (!ctx || !info || !out) return false;

    nob_sb_append_cstr(out, "static bool build_");
    nob_sb_append_cstr(out, info->ident);
    nob_sb_append_cstr(out, "(void) {\n");
    nob_sb_append_cstr(out, "    static int build_state = 0;\n");
    nob_sb_append_cstr(out, "    if (build_state == 2) return true;\n");
    nob_sb_append_cstr(out, "    if (build_state == 1) {\n");
    nob_sb_append_cstr(out, "        nob_log(NOB_ERROR, \"codegen: dependency cycle while building ");
    nob_sb_append_buf(out, info->name.data ? info->name.data : "", info->name.count);
    nob_sb_append_cstr(out, "\");\n");
    nob_sb_append_cstr(out, "        return false;\n");
    nob_sb_append_cstr(out, "    }\n");
    nob_sb_append_cstr(out, "    build_state = 1;\n");

    if (info->alias) {
        const CG_Target_Info *resolved = cg_target_info(ctx, info->resolved_id);
        if (!resolved) return false;
        nob_sb_append_cstr(out, "    if (!build_");
        nob_sb_append_cstr(out, resolved->ident);
        nob_sb_append_cstr(out, "()) return false;\n");
        nob_sb_append_cstr(out, "    build_state = 2;\n");
        nob_sb_append_cstr(out, "    return true;\n");
        nob_sb_append_cstr(out, "}\n\n");
        return true;
    }

    if (info->imported) {
        nob_sb_append_cstr(out, "    nob_log(NOB_ERROR, \"codegen: imported target is not buildable in the minimal backend\");\n");
        nob_sb_append_cstr(out, "    return false;\n");
        nob_sb_append_cstr(out, "}\n\n");
        return true;
    }

    if (info->kind == BM_TARGET_INTERFACE_LIBRARY) {
        nob_sb_append_cstr(out, "    build_state = 2;\n");
        nob_sb_append_cstr(out, "    return true;\n");
        nob_sb_append_cstr(out, "}\n\n");
        return true;
    }

    if (!cg_reject_unsupported_precompile_headers(ctx, info)) return false;
    if (!cg_reject_unsupported_platform_target_properties(ctx, info)) return false;

    if (!cg_collect_build_dependencies(ctx, info->id, &deps)) return false;
    for (size_t i = 0; i < arena_arr_len(deps); ++i) {
        const CG_Target_Info *dep = cg_target_info(ctx, deps[i]);
        if (!dep) return false;
        nob_sb_append_cstr(out, "    if (!build_");
        nob_sb_append_cstr(out, dep->ident);
        nob_sb_append_cstr(out, "()) return false;\n");
    }

    if (info->kind == BM_TARGET_UTILITY) {
        BM_Build_Step_Id *custom_steps = NULL;
        if (!cg_collect_target_steps(ctx, info->id, BM_BUILD_STEP_CUSTOM_TARGET, &custom_steps)) return false;
        for (size_t i = 0; i < arena_arr_len(custom_steps); ++i) {
            const CG_Build_Step_Info *step = cg_build_step_info(ctx, custom_steps[i]);
            if (!step) return false;
            nob_sb_append_cstr(out, "    if (!run_");
            nob_sb_append_cstr(out, step->ident);
            nob_sb_append_cstr(out, "()) return false;\n");
        }
        nob_sb_append_cstr(out, "    build_state = 2;\n");
        nob_sb_append_cstr(out, "    return true;\n");
        nob_sb_append_cstr(out, "}\n\n");
        return true;
    }

    if (!cg_collect_target_steps(ctx, info->id, BM_BUILD_STEP_TARGET_PRE_BUILD, &pre_build_steps) ||
        !cg_collect_target_steps(ctx, info->id, BM_BUILD_STEP_TARGET_PRE_LINK, &pre_link_steps) ||
        !cg_collect_target_steps(ctx, info->id, BM_BUILD_STEP_TARGET_POST_BUILD, &post_build_steps) ||
        !cg_collect_compile_sources(ctx, info->id, &sources) ||
        !cg_collect_generated_source_steps(ctx, sources, arena_arr_len(sources), &generated_steps)) {
        return false;
    }

    if (arena_arr_empty(sources)) {
        nob_log(NOB_ERROR, "codegen: target '%.*s' has no compilable C/C++ sources",
                (int)info->name.count, info->name.data ? info->name.data : "");
        return false;
    }
    needs_pic = cg_target_needs_pic(ctx, info->kind);

    {
        String_View object_subdir = {0};
        if (!cg_join_paths_to_arena(ctx->scratch,
                                    nob_sv_from_cstr(".nob/obj"),
                                    nob_sv_from_cstr(info->ident),
                                    &object_subdir)) {
            return false;
        }
        if (!cg_rebase_from_binary_root(ctx, object_subdir, &object_dir)) {
            return false;
        }
    }
    if (object_dir.count == 0) {
        return false;
    }
    for (size_t branch = 0; branch < arena_arr_len(info->runtime_artifacts); ++branch) {
        artifact_dir = cg_dirname_to_arena(ctx->scratch, info->runtime_artifacts[branch].path);
        if (artifact_dir.count > 0 && !cg_collect_unique_path(ctx->scratch, &artifact_dirs, artifact_dir)) return false;
        linker_artifact_dir = cg_dirname_to_arena(ctx->scratch, info->linker_artifacts[branch].path);
        if (linker_artifact_dir.count > 0 && !cg_collect_unique_path(ctx->scratch, &artifact_dirs, linker_artifact_dir)) return false;
    }

    for (size_t i = 0; i < arena_arr_len(pre_build_steps); ++i) {
        const CG_Build_Step_Info *step = cg_build_step_info(ctx, pre_build_steps[i]);
        if (!step) return false;
        nob_sb_append_cstr(out, "    if (!run_");
        nob_sb_append_cstr(out, step->ident);
        nob_sb_append_cstr(out, "()) return false;\n");
    }
    for (size_t i = 0; i < arena_arr_len(generated_steps); ++i) {
        const CG_Build_Step_Info *step = cg_build_step_info(ctx, generated_steps[i]);
        if (!step) return false;
        nob_sb_append_cstr(out, "    if (!run_");
        nob_sb_append_cstr(out, step->ident);
        nob_sb_append_cstr(out, "()) return false;\n");
    }

    nob_sb_append_cstr(out, "    if (!ensure_dir(");
    if (!cg_sb_append_c_string(out, object_dir)) return false;
    nob_sb_append_cstr(out, ")) return false;\n");
    for (size_t i = 0; i < arena_arr_len(artifact_dirs); ++i) {
        nob_sb_append_cstr(out, "    if (!ensure_dir(");
        if (!cg_sb_append_c_string(out, artifact_dirs[i])) return false;
        nob_sb_append_cstr(out, ")) return false;\n");
    }

    for (size_t i = 0; i < arena_arr_len(sources); ++i) {
        String_View obj_path = {0};
        if (!cg_object_path_for_index(ctx, object_dir, sources, arena_arr_len(sources), i, &obj_path)) {
            return false;
        }

        nob_sb_append_cstr(out, "    if (nob_needs_rebuild(");
        if (!cg_sb_append_c_string(out, obj_path)) return false;
        nob_sb_append_cstr(out, ", (const char*[]){");
        if (!cg_sb_append_c_string(out, ctx->emit_path_abs)) return false;
        nob_sb_append_cstr(out, ", ");
        if (!cg_sb_append_c_string(out, sources[i].path)) return false;
        nob_sb_append_cstr(out, "}, 2)) {\n");
        nob_sb_append_cstr(out, "        Nob_Cmd cc_cmd = {0};\n");
        if (!cg_emit_cmd_append_toolchain(out, "cc_cmd", sources[i].lang == CG_SOURCE_LANG_CXX)) return false;
        if (!cg_emit_compile_args_runtime(ctx, info->id, &sources[i], "cc_cmd", out)) return false;
        if (cg_policy_is_windows(ctx)) {
            char *fo_arg = cg_arena_sprintf(ctx->scratch,
                                            "/Fo:%.*s",
                                            (int)obj_path.count,
                                            obj_path.data ? obj_path.data : "");
            if (!fo_arg ||
                !cg_emit_cmd_append_sv(out, "cc_cmd", nob_sv_from_cstr("/nologo")) ||
                !cg_emit_cmd_append_sv(out, "cc_cmd", nob_sv_from_cstr("/c")) ||
                !cg_emit_cmd_append_sv(out, "cc_cmd", sources[i].path) ||
                !cg_emit_cmd_append_sv(out, "cc_cmd", nob_sv_from_cstr(fo_arg))) {
                return false;
            }
        } else {
            if (needs_pic && !cg_emit_cmd_append_sv(out, "cc_cmd", nob_sv_from_cstr("-fPIC"))) return false;
            if (!cg_emit_cmd_append_sv(out, "cc_cmd", nob_sv_from_cstr("-c")) ||
                !cg_emit_cmd_append_sv(out, "cc_cmd", sources[i].path) ||
                !cg_emit_cmd_append_sv(out, "cc_cmd", nob_sv_from_cstr("-o")) ||
                !cg_emit_cmd_append_sv(out, "cc_cmd", obj_path)) {
                return false;
            }
        }
        nob_sb_append_cstr(out, "        {\n");
        nob_sb_append_cstr(out, "            bool ok = nob_cmd_run(&cc_cmd);\n");
        nob_sb_append_cstr(out, "            nob_cmd_free(cc_cmd);\n");
        nob_sb_append_cstr(out, "            if (!ok) return false;\n");
        nob_sb_append_cstr(out, "        }\n");
        nob_sb_append_cstr(out, "    }\n");
    }

    for (size_t i = 0; i < arena_arr_len(pre_link_steps); ++i) {
        const CG_Build_Step_Info *step = cg_build_step_info(ctx, pre_link_steps[i]);
        if (!step) return false;
        nob_sb_append_cstr(out, "    if (!run_");
        nob_sb_append_cstr(out, step->ident);
        nob_sb_append_cstr(out, "()) return false;\n");
    }

    if (info->kind == BM_TARGET_STATIC_LIBRARY) {
        for (size_t branch = 0; branch <= arena_arr_len(ctx->known_configs); ++branch) {
            String_View config = branch < arena_arr_len(ctx->known_configs) ? ctx->known_configs[branch] : nob_sv_from_cstr("");
            BM_Target_Artifact_View artifact = {0};
            if (!cg_target_artifact_for_config_or_empty(info, BM_TARGET_ARTIFACT_RUNTIME, config, &artifact) ||
                !cg_emit_runtime_config_branches_prefix(ctx, out, branch)) {
                return false;
            }
            nob_sb_append_cstr(out, "        Nob_Cmd ar_cmd = {0};\n");
            if (!cg_emit_cmd_append_archive_tool(out, "ar_cmd")) return false;
            if (cg_policy_is_windows(ctx)) {
                char *out_arg = cg_arena_sprintf(ctx->scratch,
                                                 "/OUT:%.*s",
                                                 (int)artifact.path.count,
                                                 artifact.path.data ? artifact.path.data : "");
                if (!out_arg ||
                    !cg_emit_cmd_append_sv(out, "ar_cmd", nob_sv_from_cstr("/NOLOGO")) ||
                    !cg_emit_cmd_append_sv(out, "ar_cmd", nob_sv_from_cstr(out_arg))) {
                    return false;
                }
            } else {
                if (!cg_emit_cmd_append_sv(out, "ar_cmd", nob_sv_from_cstr("rcs")) ||
                    !cg_emit_cmd_append_sv(out, "ar_cmd", artifact.path)) {
                    return false;
                }
            }
            for (size_t i = 0; i < arena_arr_len(sources); ++i) {
                String_View obj_path = {0};
                if (!cg_object_path_for_index(ctx, object_dir, sources, arena_arr_len(sources), i, &obj_path)) {
                    return false;
                }
                if (!cg_emit_cmd_append_sv(out, "ar_cmd", obj_path)) return false;
            }
            nob_sb_append_cstr(out, "        {\n");
            nob_sb_append_cstr(out, "            bool ok = nob_cmd_run(&ar_cmd);\n");
            nob_sb_append_cstr(out, "            nob_cmd_free(ar_cmd);\n");
            nob_sb_append_cstr(out, "            if (!ok) return false;\n");
            nob_sb_append_cstr(out, "        }\n");
        }
        if (!cg_emit_runtime_config_branches_suffix(ctx, out)) return false;
    } else if (info->kind == BM_TARGET_EXECUTABLE ||
               info->kind == BM_TARGET_SHARED_LIBRARY ||
               info->kind == BM_TARGET_MODULE_LIBRARY) {
        if (!cg_emit_link_args_runtime(ctx, info, sources, arena_arr_len(sources), object_dir, out)) return false;
    }

    for (size_t i = 0; i < arena_arr_len(post_build_steps); ++i) {
        const CG_Build_Step_Info *step = cg_build_step_info(ctx, post_build_steps[i]);
        if (!step) return false;
        nob_sb_append_cstr(out, "    if (!run_");
        nob_sb_append_cstr(out, step->ident);
        nob_sb_append_cstr(out, "()) return false;\n");
    }

    nob_sb_append_cstr(out, "    build_state = 2;\n");
    nob_sb_append_cstr(out, "    return true;\n");
    nob_sb_append_cstr(out, "}\n\n");
    return true;
}

static bool cg_emit_build_request(CG_Context *ctx, Nob_String_Builder *out) {
    if (!ctx || !out) return false;
    nob_sb_append_cstr(out, "static bool build_default_targets(void) {\n");
    for (size_t i = 0; i < ctx->target_count; ++i) {
        const CG_Target_Info *info = &ctx->targets[i];
        bool default_buildable = info->emits_artifact || info->kind == BM_TARGET_UTILITY;
        if (info->alias || info->imported || info->exclude_from_all || !default_buildable) continue;
        nob_sb_append_cstr(out, "    if (!build_");
        nob_sb_append_cstr(out, info->ident);
        nob_sb_append_cstr(out, "()) return false;\n");
    }
    nob_sb_append_cstr(out, "    return true;\n");
    nob_sb_append_cstr(out, "}\n\n");

    nob_sb_append_cstr(out, "static bool build_request(const char *name) {\n");
    nob_sb_append_cstr(out, "    if (!name) return false;\n");
    for (size_t i = 0; i < ctx->target_count; ++i) {
        nob_sb_append_cstr(out, "    if (strcmp(name, ");
        if (!cg_sb_append_c_string(out, ctx->targets[i].name)) return false;
        nob_sb_append_cstr(out, ") == 0) return build_");
        nob_sb_append_cstr(out, ctx->targets[i].ident);
        nob_sb_append_cstr(out, "();\n");
    }
    nob_sb_append_cstr(out, "    nob_log(NOB_ERROR, \"unknown target: %s\", name);\n");
    nob_sb_append_cstr(out, "    return false;\n");
    nob_sb_append_cstr(out, "}\n\n");
    return true;
}

static bool cg_emit_clean_function(CG_Context *ctx, Nob_String_Builder *out) {
    String_View *paths = NULL;
    String_View *preserve_dirs = NULL;
    if (!ctx || !out) return false;

    {
        String_View backend_root = {0};
        if (!cg_rebase_from_binary_root(ctx, nob_sv_from_cstr(".nob"), &backend_root) ||
            !cg_collect_unique_path(ctx->scratch, &paths, backend_root)) {
            return false;
        }
    }

    for (size_t i = 0; i < ctx->target_count; ++i) {
        if (!ctx->targets[i].emits_artifact) continue;
        for (size_t branch = 0; branch < arena_arr_len(ctx->targets[i].runtime_artifacts); ++branch) {
            const BM_Target_Artifact_View artifacts[2] = {
                ctx->targets[i].runtime_artifacts[branch],
                ctx->targets[i].linker_artifacts[branch],
            };
            for (size_t artifact_index = 0; artifact_index < NOB_ARRAY_LEN(artifacts); ++artifact_index) {
                String_View dir = {0};
                String_View dir_abs = {0};
                if (artifacts[artifact_index].path.count == 0) continue;
                if (artifact_index == 1 && nob_sv_eq(artifacts[0].path, artifacts[1].path)) continue;
                dir = cg_dirname_to_arena(ctx->scratch, artifacts[artifact_index].path);
                if (dir.count == 0 || cg_sv_eq_lit(dir, "/")) {
                    if (!cg_collect_unique_path(ctx->scratch, &paths, artifacts[artifact_index].path)) return false;
                } else {
                    if (!cg_collect_unique_path(ctx->scratch, &preserve_dirs, dir)) return false;
                    if (!cg_absolute_from_emit(ctx, dir, &dir_abs)) return false;
                    if (nob_sv_eq(dir_abs, ctx->binary_root_abs) || cg_sv_eq_lit(dir, ".")) {
                        if (!cg_collect_unique_path(ctx->scratch, &paths, artifacts[artifact_index].path)) return false;
                    } else if (!cg_collect_unique_path(ctx->scratch, &paths, dir)) {
                        return false;
                    }
                }
            }
        }
    }

    for (size_t step_index = 0; step_index < ctx->build_step_count; ++step_index) {
        const CG_Build_Step_Info *step = &ctx->build_steps[step_index];
        for (size_t branch = 0; branch <= arena_arr_len(ctx->known_configs); ++branch) {
            String_View config = branch < arena_arr_len(ctx->known_configs)
                ? ctx->known_configs[branch]
                : nob_sv_from_cstr("");
            BM_Query_Eval_Context qctx = {0};
            BM_Build_Step_Effective_View view = {0};
            qctx.current_target_id = step->owner_target_id;
            qctx.usage_mode = BM_QUERY_USAGE_LINK;
            qctx.config = config;
            qctx.platform_id = ctx->policy.platform_id;
            qctx.build_interface_active = true;
            qctx.build_local_interface_active = true;
            if (!bm_query_build_step_effective_view(ctx->model, step->id, &qctx, ctx->scratch, &view)) return false;
            for (size_t output = 0; output < view.outputs.count; ++output) {
                String_View rebased = {0};
                if (!cg_rebase_path_from_cwd(ctx, view.outputs.items[output], &rebased) ||
                    !cg_collect_unique_path(ctx->scratch, &paths, rebased)) {
                    return false;
                }
            }
            for (size_t byproduct = 0; byproduct < view.byproducts.count; ++byproduct) {
                String_View rebased = {0};
                if (!cg_rebase_path_from_cwd(ctx, view.byproducts.items[byproduct], &rebased) ||
                    !cg_collect_unique_path(ctx->scratch, &paths, rebased)) {
                    return false;
                }
            }
            if (step->uses_stamp && step->sentinel_path.count > 0 &&
                !cg_collect_unique_path(ctx->scratch, &paths, step->sentinel_path)) {
                return false;
            }
        }
    }

    for (size_t replay_index = 0; replay_index < bm_query_replay_action_count(ctx->model); ++replay_index) {
        BM_Replay_Action_Id id = (BM_Replay_Action_Id)replay_index;
        if (bm_query_replay_action_phase(ctx->model, id) != BM_REPLAY_PHASE_CONFIGURE) continue;
        for (size_t branch = 0; branch <= arena_arr_len(ctx->known_configs); ++branch) {
            const String_View *outputs = NULL;
            String_View config = branch < arena_arr_len(ctx->known_configs)
                ? ctx->known_configs[branch]
                : nob_sv_from_cstr("");
            if (!cg_resolve_replay_operands_for_config(ctx,
                                                       id,
                                                       BM_REPLAY_OPERAND_OUTPUTS,
                                                       config,
                                                       &outputs)) {
                return false;
            }
            for (size_t output_index = 0; output_index < arena_arr_len(outputs); ++output_index) {
                String_View rebased = {0};
                bool clean_safe = false;
                if (!cg_resolve_replay_path_for_config(ctx,
                                                       id,
                                                       BM_REPLAY_OPERAND_OUTPUTS,
                                                       output_index,
                                                       config,
                                                       &rebased)) {
                    return false;
                }
                if (rebased.count == 0) continue;
                if (!cg_replay_output_is_clean_safe(ctx, rebased, &clean_safe)) return false;
                if (!clean_safe) continue;
                if (!cg_collect_unique_path(ctx->scratch, &paths, rebased)) return false;
            }
        }
    }

    for (size_t package_index = 0; package_index < bm_query_cpack_package_count(ctx->model); ++package_index) {
        BM_CPack_Package_Id package_id = (BM_CPack_Package_Id)package_index;
        String_View output_dir = bm_query_cpack_package_output_directory(ctx->model, package_id, ctx->scratch);
        bool clean_safe = false;
        String_View rebased = {0};
        if (output_dir.count == 0) continue;
        if (!cg_replay_output_is_clean_safe(ctx, output_dir, &clean_safe)) return false;
        if (!clean_safe) continue;
        if (!cg_rebase_path_from_cwd(ctx, output_dir, &rebased) ||
            !cg_collect_unique_path(ctx->scratch, &paths, rebased)) {
            return false;
        }
    }

    nob_sb_append_cstr(out, "static bool clean_all(void) {\n");
    if (arena_arr_len(paths) == 0) {
        nob_sb_append_cstr(out, "    return true;\n");
        nob_sb_append_cstr(out, "}\n\n");
        return true;
    }
    for (size_t i = 0; i < arena_arr_len(paths); ++i) {
        nob_sb_append_cstr(out, "    if (!remove_path_recursive(");
        if (!cg_sb_append_c_string(out, paths[i])) return false;
        nob_sb_append_cstr(out, ")) return false;\n");
    }
    for (size_t i = 0; i < arena_arr_len(preserve_dirs); ++i) {
        nob_sb_append_cstr(out, "    if (!ensure_dir(");
        if (!cg_sb_append_c_string(out, preserve_dirs[i])) return false;
        nob_sb_append_cstr(out, ")) return false;\n");
    }
    nob_sb_append_cstr(out, "    return true;\n");
    nob_sb_append_cstr(out, "}\n\n");
    return true;
}

#include "nob_codegen_cmake_writer.c"
#include "nob_codegen_replay.c"
#include "nob_codegen_install.c"
#include "nob_codegen_export.c"
#include "nob_codegen_package.c"

static bool cg_emit_main(CG_Context *ctx, Nob_String_Builder *out) {
    if (!ctx || !out) return false;
    nob_sb_append_cstr(out,
        "int main(int argc, char **argv) {\n"
        "    int argi = 1;\n"
        "    while (argi < argc) {\n"
        "        if (strcmp(argv[argi], \"--config\") != 0) break;\n"
        "        if (argi + 1 >= argc) {\n"
        "            nob_log(NOB_ERROR, \"--config expects a value\");\n"
        "            return 1;\n"
        "        }\n"
        "        g_build_config = argv[argi + 1];\n"
        "        argi += 2;\n"
        "    }\n"
        "    if (argi < argc && strcmp(argv[argi], \"configure\") == 0) {\n"
        "        if (argi + 1 != argc) {\n"
        "            nob_log(NOB_ERROR, \"configure: unexpected argument '%s'\", argv[argi + 1]);\n"
        "            return 1;\n"
        "        }\n"
        "        return configure_all(true) ? 0 : 1;\n"
        "    }\n"
        "    if (argi < argc && strcmp(argv[argi], \"clean\") == 0) {\n"
        "        return clean_all() ? 0 : 1;\n"
        "    }\n"
        "    if (argi < argc && strcmp(argv[argi], \"build\") == 0) {\n"
        "        ++argi;\n"
        "        if (!ensure_configured()) return 1;\n"
        "        if (argi < argc) {\n"
        "            for (int i = argi; i < argc; ++i) {\n"
        "                if (!build_request(argv[i])) return 1;\n"
        "            }\n"
        "            return 0;\n"
        "        }\n");
    nob_sb_append_cstr(out,
        "        return build_default_targets() ? 0 : 1;\n"
        "    }\n"
        "    if (argi < argc && strcmp(argv[argi], \"test\") == 0) {\n"
        "        ++argi;\n"
        "        if (!ensure_configured()) return 1;\n"
        "        return run_test_phase((const char **)(argv + argi), (size_t)(argc - argi), g_build_config) ? 0 : 1;\n"
        "    }\n"
        "    if (argi < argc && strcmp(argv[argi], \"install\") == 0) {\n"
        "        const char *install_prefix = \"install\";\n"
        "        const char *install_component = NULL;\n"
        "        bool saw_prefix = false;\n"
        "        bool saw_component = false;\n"
        "        ++argi;\n"
        "        while (argi < argc) {\n"
        "            if (strcmp(argv[argi], \"--prefix\") == 0) {\n"
        "                if (saw_prefix) {\n"
        "                    nob_log(NOB_ERROR, \"install: duplicate --prefix\");\n"
        "                    return 1;\n"
        "                }\n"
        "                if (argi + 1 >= argc) {\n"
        "                    nob_log(NOB_ERROR, \"install: --prefix expects a value\");\n"
        "                    return 1;\n"
        "                }\n"
        "                install_prefix = argv[argi + 1];\n"
        "                saw_prefix = true;\n"
        "                argi += 2;\n"
        "                continue;\n"
        "            }\n"
        "            if (strcmp(argv[argi], \"--component\") == 0) {\n"
        "                if (saw_component) {\n"
        "                    nob_log(NOB_ERROR, \"install: duplicate --component\");\n"
        "                    return 1;\n"
        "                }\n"
        "                if (argi + 1 >= argc) {\n"
        "                    nob_log(NOB_ERROR, \"install: --component expects a value\");\n"
        "                    return 1;\n"
        "                }\n"
        "                install_component = argv[argi + 1];\n"
        "                saw_component = true;\n"
        "                argi += 2;\n"
        "                continue;\n"
        "            }\n"
        "            nob_log(NOB_ERROR, \"install: unexpected argument '%s'\", argv[argi]);\n"
        "            return 1;\n"
        "        }\n"
        "        if (!ensure_configured()) return 1;\n"
        "        return install_all(install_prefix, install_component) ? 0 : 1;\n"
        "    }\n"
        "    if (argi < argc && strcmp(argv[argi], \"export\") == 0) {\n"
        "        if (argi + 1 != argc) {\n"
        "            nob_log(NOB_ERROR, \"export: unexpected argument '%s'\", argv[argi + 1]);\n"
        "            return 1;\n"
        "        }\n"
        "        if (!ensure_configured()) return 1;\n"
        "        return export_all() ? 0 : 1;\n"
        "    }\n"
        "    if (argi < argc && strcmp(argv[argi], \"package\") == 0) {\n"
        "        const char *package_generator = NULL;\n"
        "        const char *package_output_dir = NULL;\n"
        "        bool saw_generator = false;\n"
        "        bool saw_output_dir = false;\n"
        "        ++argi;\n"
        "        while (argi < argc) {\n"
        "            if (strcmp(argv[argi], \"--generator\") == 0) {\n"
        "                if (saw_generator) {\n"
        "                    nob_log(NOB_ERROR, \"package: duplicate --generator\");\n"
        "                    return 1;\n"
        "                }\n"
        "                if (argi + 1 >= argc) {\n"
        "                    nob_log(NOB_ERROR, \"package: --generator expects a value\");\n"
        "                    return 1;\n"
        "                }\n"
        "                package_generator = argv[argi + 1];\n"
        "                saw_generator = true;\n"
        "                argi += 2;\n"
        "                continue;\n"
        "            }\n"
        "            if (strcmp(argv[argi], \"--output-dir\") == 0) {\n"
        "                if (saw_output_dir) {\n"
        "                    nob_log(NOB_ERROR, \"package: duplicate --output-dir\");\n"
        "                    return 1;\n"
        "                }\n"
        "                if (argi + 1 >= argc) {\n"
        "                    nob_log(NOB_ERROR, \"package: --output-dir expects a value\");\n"
        "                    return 1;\n"
        "                }\n"
        "                package_output_dir = argv[argi + 1];\n"
        "                saw_output_dir = true;\n"
        "                argi += 2;\n"
        "                continue;\n"
        "            }\n"
        "            nob_log(NOB_ERROR, \"package: unexpected argument '%s'\", argv[argi]);\n"
        "            return 1;\n"
        "        }\n"
        "        if (!ensure_configured()) return 1;\n"
        "        return package_all(package_generator, package_output_dir) ? 0 : 1;\n"
        "    }\n"
        "    if (argi < argc) {\n"
        "        if (!ensure_configured()) return 1;\n"
        "        for (int i = argi; i < argc; ++i) {\n"
            "            if (!build_request(argv[i])) return 1;\n"
        "        }\n"
        "        return 0;\n"
        "    }\n"
        "    if (!ensure_configured()) return 1;\n");

    nob_sb_append_cstr(out,
        "    return build_default_targets() ? 0 : 1;\n"
        "}\n");
    return true;
}

#include "nob_codegen_validate.c"

static bool cg_init_context(CG_Context *ctx,
                            const Build_Model *model,
                            Arena *scratch,
                            const Nob_Codegen_Options *opts) {
    const char *cwd = NULL;
    String_View input_path = {0};
    String_View output_path = {0};
    String_View input_dir = {0};
    String_View source_root = {0};
    String_View binary_root = {0};
    if (!ctx || !model || !scratch || !opts) return false;
    memset(ctx, 0, sizeof(*ctx));
    ctx->model = model;
    ctx->scratch = scratch;

    input_path = opts->input_path.count > 0 ? opts->input_path : nob_sv_from_cstr("CMakeLists.txt");
    input_dir = cg_dirname_to_arena(scratch, input_path);
    output_path = opts->output_path;
    if (output_path.count == 0) {
        if (!cg_join_paths_to_arena(scratch, input_dir, nob_sv_from_cstr("nob.c"), &output_path)) return false;
    }
    source_root = opts->source_root.count > 0 ? opts->source_root : input_dir;
    binary_root = opts->binary_root.count > 0 ? opts->binary_root : source_root;

    ctx->opts = (Nob_Codegen_Options){
        .input_path = input_path,
        .output_path = output_path,
        .source_root = source_root,
        .binary_root = binary_root,
        .embedded_cmake_bin = opts->embedded_cmake_bin,
        .embedded_cpack_bin = opts->embedded_cpack_bin,
        .embedded_gzip_bin = opts->embedded_gzip_bin,
        .embedded_xz_bin = opts->embedded_xz_bin,
        .target_platform = opts->target_platform,
        .backend = opts->backend,
    };
    if (!cg_init_backend_policy(ctx)) return false;

    cwd = nob_get_current_dir_temp();
    if (!cwd) {
        nob_log(NOB_ERROR, "codegen: failed to read current directory");
        return false;
    }

    if (!cg_absolute_from_cwd(ctx, nob_sv_from_cstr(cwd), &ctx->cwd_abs) ||
        !cg_absolute_from_cwd(ctx, ctx->opts.source_root, &ctx->source_root_abs) ||
        !cg_absolute_from_cwd(ctx, ctx->opts.binary_root, &ctx->binary_root_abs) ||
        !cg_absolute_from_cwd(ctx, ctx->opts.output_path, &ctx->emit_path_abs)) {
        return false;
    }
    if (ctx->opts.embedded_cmake_bin.count > 0 &&
        !cg_absolute_from_cwd(ctx, ctx->opts.embedded_cmake_bin, &ctx->embedded_cmake_bin_abs)) {
        return false;
    }
    if (ctx->opts.embedded_cpack_bin.count > 0 &&
        !cg_absolute_from_cwd(ctx, ctx->opts.embedded_cpack_bin, &ctx->embedded_cpack_bin_abs)) {
        return false;
    }
    if (ctx->opts.embedded_gzip_bin.count > 0 &&
        !cg_absolute_from_cwd(ctx, ctx->opts.embedded_gzip_bin, &ctx->embedded_gzip_bin_abs)) {
        return false;
    }
    if (ctx->opts.embedded_xz_bin.count > 0 &&
        !cg_absolute_from_cwd(ctx, ctx->opts.embedded_xz_bin, &ctx->embedded_xz_bin_abs)) {
        return false;
    }

    ctx->emit_dir_abs = cg_dirname_to_arena(ctx->scratch, ctx->emit_path_abs);
    ctx->query_session = bm_query_session_create(ctx->scratch, ctx->model);
    if (!ctx->query_session) return false;
    if (!cg_collect_known_configs(ctx) || !cg_init_targets(ctx) || !cg_init_build_steps(ctx)) return false;
    if (!cg_validate_model_for_backend(ctx)) return false;
    cg_collect_helper_requirements(ctx);
    return true;
}

bool nob_codegen_render(const Build_Model *model,
                        Arena *scratch,
                        const Nob_Codegen_Options *opts,
                        Nob_String_Builder *out) {
    CG_Context ctx = {0};
    if (!out || !opts) return false;
    out->count = 0;

    if (!cg_init_context(&ctx, model, scratch, opts)) {
        nob_log(NOB_ERROR, "codegen: failed to initialize render context");
        return false;
    }

    nob_sb_append_cstr(out,
        "#define NOB_IMPLEMENTATION\n"
        "#include \"nob.h\"\n"
        "\n"
        "#include <ctype.h>\n"
        "#include <errno.h>\n"
        "#include <stdio.h>\n"
        "#include <stdint.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "#include <time.h>\n"
        "#include <sys/stat.h>\n"
        "#if !defined(_WIN32)\n"
        "#include <fcntl.h>\n"
        "#include <sys/file.h>\n"
        "#include <unistd.h>\n"
        "#endif\n"
        "\n");

    if (!cg_emit_target_forward_decls(&ctx, out) ||
        !cg_emit_step_forward_decls(&ctx, out) ||
        !cg_emit_support_helpers(&ctx, out)) {
        nob_log(NOB_ERROR, "codegen: failed while emitting forward declarations or support helpers");
        return false;
    }

    for (size_t i = 0; i < ctx.build_step_count; ++i) {
        if (!cg_emit_step_function(&ctx, &ctx.build_steps[i], out)) {
            nob_log(NOB_ERROR, "codegen: failed while emitting build step %" PRIu64, (uint64_t)ctx.build_steps[i].id);
            return false;
        }
    }

    for (size_t i = 0; i < ctx.target_count; ++i) {
        if (!cg_emit_target_function(&ctx, &ctx.targets[i], out)) {
            nob_log(NOB_ERROR, "codegen: failed while emitting target %" PRIu64, (uint64_t)ctx.targets[i].id);
            return false;
        }
    }

    if (!cg_emit_configure_functions(&ctx, out) ||
        !cg_emit_build_request(&ctx, out) ||
        !cg_emit_test_functions(&ctx, out) ||
        !cg_emit_clean_function(&ctx, out) ||
        !cg_emit_install_function(&ctx, out) ||
        !cg_emit_export_function(&ctx, out) ||
        !cg_emit_package_function(&ctx, out) ||
        !cg_emit_main(&ctx, out)) {
        nob_log(NOB_ERROR, "codegen: failed while emitting top-level command functions");
        return false;
    }

    return true;
}

bool nob_codegen_write_file(const Build_Model *model,
                            Arena *scratch,
                            const Nob_Codegen_Options *opts) {
    Nob_String_Builder sb = {0};
    const char *out_path = NULL;
    const char *out_dir = NULL;
    if (!opts) return false;
    out_path = nob_temp_sv_to_cstr(opts->output_path);
    if (!out_path) return false;
    out_dir = nob_temp_dir_name(out_path);
    if (out_dir && strcmp(out_dir, ".") != 0 && !cg_host_ensure_dir(out_dir)) {
        nob_log(NOB_ERROR, "codegen: failed to create output directory %s", out_dir);
        return false;
    }
    if (!nob_codegen_render(model, scratch, opts, &sb)) {
        nob_sb_free(sb);
        return false;
    }
    bool ok = nob_write_entire_file(out_path, sb.items ? sb.items : "", sb.count);
    if (!ok) nob_log(NOB_ERROR, "codegen: failed to write generated file %s", out_path);
    nob_sb_free(sb);
    return ok;
}
