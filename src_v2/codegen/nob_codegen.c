#include "nob_codegen.h"

#include "arena_dyn.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

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
    bool needs_cxx_linker_known;
    bool needs_cxx_linker;
} CG_Target_Info;

typedef enum {
    CG_SOURCE_LANG_C = 0,
    CG_SOURCE_LANG_CXX,
} CG_Source_Lang;

typedef struct {
    String_View path;
    CG_Source_Lang lang;
} CG_Source_Info;

typedef struct {
    const Build_Model *model;
    Arena *scratch;
    Nob_Codegen_Options opts;
    String_View cwd_abs;
    String_View emit_path_abs;
    String_View emit_dir_abs;
    CG_Target_Info *targets;
    size_t target_count;
} CG_Context;

static const CG_Target_Info *cg_target_info(const CG_Context *ctx, BM_Target_Id id);
static bool cg_target_needs_cxx_linker_recursive(CG_Context *ctx, BM_Target_Id id, bool *out);

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

static bool cg_path_is_abs(String_View path) {
    return path.count > 0 && path.data[0] == '/';
}

static bool cg_is_genex(String_View sv) {
    return cg_sv_contains(sv, "$<");
}

static bool cg_ends_with(String_View sv, const char *suffix) {
    return nob_sv_end_with(sv, suffix);
}

static bool cg_is_header_like(String_View sv) {
    return cg_ends_with(sv, ".h") ||
           cg_ends_with(sv, ".hh") ||
           cg_ends_with(sv, ".hpp") ||
           cg_ends_with(sv, ".hxx") ||
           cg_ends_with(sv, ".inl") ||
           cg_ends_with(sv, ".inc");
}

static bool cg_is_c_source(String_View sv) {
    return cg_ends_with(sv, ".c");
}

static bool cg_is_cxx_source(String_View sv) {
    return cg_ends_with(sv, ".cc") ||
           cg_ends_with(sv, ".cpp") ||
           cg_ends_with(sv, ".cxx") ||
           cg_ends_with(sv, ".c++");
}

static bool cg_is_link_file_like(String_View sv) {
    return cg_ends_with(sv, ".a") ||
           cg_ends_with(sv, ".so") ||
           cg_ends_with(sv, ".dylib") ||
           cg_ends_with(sv, ".o");
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

static bool cg_sb_append_c_string(Nob_String_Builder *sb, String_View sv) {
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

static bool cg_absolute_from_cwd(CG_Context *ctx, String_View path, String_View *out) {
    if (!ctx || !out) return false;
    if (cg_path_is_abs(path)) return cg_normalize_path_to_arena(ctx->scratch, path, out);
    return cg_join_paths_to_arena(ctx->scratch, ctx->cwd_abs, path, out);
}

static bool cg_relative_path_to_arena(Arena *scratch,
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
    if (cg_path_is_abs(value)) {
        if (!cg_normalize_path_to_arena(ctx->scratch, value, &absolute)) return false;
    } else {
        String_View base_abs = {0};
        if (!cg_absolute_from_cwd(ctx, base_dir, &base_abs)) return false;
        if (!cg_join_paths_to_arena(ctx->scratch, base_abs, value, &absolute)) return false;
    }
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
    return kind == BM_TARGET_INTERFACE_LIBRARY;
}

static bool cg_target_needs_pic(BM_Target_Kind kind) {
    return kind == BM_TARGET_STATIC_LIBRARY ||
           kind == BM_TARGET_SHARED_LIBRARY ||
           kind == BM_TARGET_MODULE_LIBRARY;
}

static bool cg_target_kind_is_linkable_artifact(BM_Target_Kind kind) {
    return kind == BM_TARGET_STATIC_LIBRARY ||
           kind == BM_TARGET_SHARED_LIBRARY;
}

static bool cg_check_no_genex(const char *what, String_View sv) {
    if (!cg_is_genex(sv)) return true;
    nob_log(NOB_ERROR, "codegen: unsupported generator expression in %s: %.*s",
            what, (int)sv.count, sv.data ? sv.data : "");
    return false;
}

static bool cg_classify_source_lang(String_View src, CG_Source_Lang *out_lang) {
    if (out_lang) *out_lang = CG_SOURCE_LANG_C;
    if (cg_is_c_source(src)) {
        if (out_lang) *out_lang = CG_SOURCE_LANG_C;
        return true;
    }
    if (cg_is_cxx_source(src)) {
        if (out_lang) *out_lang = CG_SOURCE_LANG_CXX;
        return true;
    }
    return false;
}

static bool cg_compute_target_artifact_path(CG_Context *ctx, BM_Target_Id id, String_View *out) {
    BM_Target_Kind kind = bm_query_target_kind(ctx->model, id);
    BM_Directory_Id owner = bm_query_target_owner_directory(ctx->model, id);
    String_View owner_binary_dir = bm_query_directory_binary_dir(ctx->model, owner);
    String_View output_name = bm_query_target_output_name(ctx->model, id);
    String_View prefix = bm_query_target_prefix(ctx->model, id);
    String_View suffix = bm_query_target_suffix(ctx->model, id);
    String_View output_dir = {0};
    Nob_String_Builder name_sb = {0};
    String_View basename = {0};
    String_View rebased_dir = {0};
    char *copy = NULL;
    if (!ctx || !out) return false;

    if (!cg_check_no_genex("target OUTPUT_NAME", output_name) ||
        !cg_check_no_genex("target PREFIX", prefix) ||
        !cg_check_no_genex("target SUFFIX", suffix)) {
        nob_sb_free(name_sb);
        return false;
    }

    if (kind == BM_TARGET_EXECUTABLE) {
        output_dir = bm_query_target_runtime_output_directory(ctx->model, id);
        if (output_dir.count == 0) output_dir = nob_sv_from_cstr("build");
        if (output_name.count == 0) output_name = bm_query_target_name(ctx->model, id);
        nob_sb_append_buf(&name_sb, output_name.data ? output_name.data : "", output_name.count);
    } else if (kind == BM_TARGET_STATIC_LIBRARY) {
        output_dir = bm_query_target_archive_output_directory(ctx->model, id);
        if (output_dir.count == 0) output_dir = nob_sv_from_cstr("build");
        if (output_name.count == 0) output_name = bm_query_target_name(ctx->model, id);
        if (prefix.count == 0) prefix = nob_sv_from_cstr("lib");
        if (suffix.count == 0) suffix = nob_sv_from_cstr(".a");
        nob_sb_append_buf(&name_sb, prefix.data ? prefix.data : "", prefix.count);
        nob_sb_append_buf(&name_sb, output_name.data ? output_name.data : "", output_name.count);
        nob_sb_append_buf(&name_sb, suffix.data ? suffix.data : "", suffix.count);
    } else if (kind == BM_TARGET_SHARED_LIBRARY || kind == BM_TARGET_MODULE_LIBRARY) {
        output_dir = bm_query_target_library_output_directory(ctx->model, id);
        if (output_dir.count == 0) output_dir = nob_sv_from_cstr("build");
        if (output_name.count == 0) output_name = bm_query_target_name(ctx->model, id);
        if (prefix.count == 0) prefix = nob_sv_from_cstr("lib");
        if (suffix.count == 0) suffix = nob_sv_from_cstr(".so");
        nob_sb_append_buf(&name_sb, prefix.data ? prefix.data : "", prefix.count);
        nob_sb_append_buf(&name_sb, output_name.data ? output_name.data : "", output_name.count);
        nob_sb_append_buf(&name_sb, suffix.data ? suffix.data : "", suffix.count);
    } else {
        *out = (String_View){0};
        nob_sb_free(name_sb);
        return true;
    }

    if (!cg_check_no_genex("target output directory", output_dir)) {
        nob_sb_free(name_sb);
        return false;
    }

    copy = arena_strndup(ctx->scratch, name_sb.items ? name_sb.items : "", name_sb.count);
    nob_sb_free(name_sb);
    if (!copy) return false;
    basename = nob_sv_from_cstr(copy);

    if (!cg_rebase_from_base(ctx, output_dir, owner_binary_dir, &rebased_dir)) return false;
    return cg_join_paths_to_arena(ctx->scratch, rebased_dir, basename, out);
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
        if (info->emits_artifact && !cg_compute_target_artifact_path(ctx, id, &info->artifact_path)) return false;
    }

    for (size_t i = 0; i < ctx->target_count; ++i) {
        if (!ctx->targets[i].alias) continue;
        BM_Target_Id resolved = ctx->targets[i].resolved_id;
        if (!bm_target_id_is_valid(resolved) || (size_t)resolved >= ctx->target_count) return false;
        ctx->targets[i].artifact_path = ctx->targets[resolved].artifact_path;
        ctx->targets[i].kind = ctx->targets[resolved].kind;
        ctx->targets[i].imported = ctx->targets[resolved].imported;
        ctx->targets[i].emits_artifact = ctx->targets[resolved].emits_artifact;
    }

    for (size_t i = 0; i < ctx->target_count; ++i) {
        bool needs_cxx_linker = false;
        if (!cg_target_needs_cxx_linker_recursive(ctx, (BM_Target_Id)i, &needs_cxx_linker)) return false;
        ctx->targets[i].needs_cxx_linker = needs_cxx_linker;
        ctx->targets[i].needs_cxx_linker_known = true;
    }

    return true;
}

static const CG_Target_Info *cg_target_info(const CG_Context *ctx, BM_Target_Id id) {
    if (!ctx || !bm_target_id_is_valid(id) || (size_t)id >= ctx->target_count) return NULL;
    return &ctx->targets[id];
}

static bool cg_resolve_link_item_target(CG_Context *ctx, String_View item, BM_Target_Id *out) {
    BM_Target_Id id = bm_query_target_by_name(ctx->model, item);
    if (out) *out = BM_TARGET_ID_INVALID;
    if (!bm_target_id_is_valid(id)) return false;
    id = cg_resolve_alias_target(ctx, id);
    if (!bm_target_id_is_valid(id)) return false;
    if (out) *out = id;
    return true;
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

static bool cg_target_has_cxx_sources(CG_Context *ctx, BM_Target_Id id, bool *out) {
    BM_String_Span sources = {0};
    if (out) *out = false;
    if (!ctx || !out) return false;

    sources = bm_query_target_sources_raw(ctx->model, id);
    for (size_t i = 0; i < sources.count; ++i) {
        CG_Source_Lang lang = CG_SOURCE_LANG_C;
        if (cg_is_header_like(sources.items[i])) continue;
        if (!cg_classify_source_lang(sources.items[i], &lang)) continue;
        if (lang == CG_SOURCE_LANG_CXX) {
            *out = true;
            return true;
        }
    }

    return true;
}

static bool cg_target_needs_cxx_linker_recursive(CG_Context *ctx, BM_Target_Id id, bool *out) {
    const CG_Target_Info *info = NULL;
    BM_String_Item_Span libs = {0};
    if (out) *out = false;
    if (!ctx || !out) return false;

    info = cg_target_info(ctx, id);
    if (!info) return false;
    if (info->needs_cxx_linker_known) {
        *out = info->needs_cxx_linker;
        return true;
    }

    ctx->targets[id].needs_cxx_linker_known = true;
    ctx->targets[id].needs_cxx_linker = false;

    if (info->alias) {
        bool resolved_needs_cxx = false;
        if (!bm_target_id_is_valid(info->resolved_id)) return false;
        if (!cg_target_needs_cxx_linker_recursive(ctx, info->resolved_id, &resolved_needs_cxx)) return false;
        ctx->targets[id].needs_cxx_linker = resolved_needs_cxx;
        *out = resolved_needs_cxx;
        return true;
    }

    if (!info->imported && !cg_target_has_cxx_sources(ctx, id, &ctx->targets[id].needs_cxx_linker)) {
        return false;
    }
    if (ctx->targets[id].needs_cxx_linker) {
        *out = true;
        return true;
    }

    libs = bm_query_target_link_libraries_raw(ctx->model, id);
    for (size_t i = 0; i < libs.count; ++i) {
        BM_Target_Id dep_id = BM_TARGET_ID_INVALID;
        bool dep_needs_cxx = false;
        if (!cg_resolve_link_item_target(ctx, libs.items[i].value, &dep_id)) continue;
        if (!cg_target_needs_cxx_linker_recursive(ctx, dep_id, &dep_needs_cxx)) return false;
        if (dep_needs_cxx) {
            ctx->targets[id].needs_cxx_linker = true;
            *out = true;
            return true;
        }
    }

    *out = ctx->targets[id].needs_cxx_linker;
    return true;
}

static bool cg_collect_build_dependencies(CG_Context *ctx, BM_Target_Id id, BM_Target_Id **out) {
    BM_Target_Id_Span explicit_deps = bm_query_target_dependencies_explicit(ctx->model, id);
    BM_String_Item_Span raw_link_libs = bm_query_target_link_libraries_raw(ctx->model, id);
    if (!ctx || !out) return false;

    for (size_t i = 0; i < explicit_deps.count; ++i) {
        BM_Target_Id dep = cg_resolve_alias_target(ctx, explicit_deps.items[i]);
        if (bm_target_id_is_valid(dep) && !cg_collect_unique_target(ctx->scratch, out, dep)) return false;
    }

    for (size_t i = 0; i < raw_link_libs.count; ++i) {
        BM_Target_Id dep = BM_TARGET_ID_INVALID;
        if (!cg_resolve_link_item_target(ctx, raw_link_libs.items[i].value, &dep)) continue;
        if (!cg_collect_unique_target(ctx->scratch, out, dep)) return false;
    }

    return true;
}

static bool cg_collect_compile_sources(CG_Context *ctx, BM_Target_Id id, CG_Source_Info **out) {
    BM_String_Span sources = bm_query_target_sources_raw(ctx->model, id);
    BM_Directory_Id owner = bm_query_target_owner_directory(ctx->model, id);
    String_View owner_source_dir = bm_query_directory_source_dir(ctx->model, owner);

    for (size_t i = 0; i < sources.count; ++i) {
        String_View src = sources.items[i];
        String_View rebased = {0};
        CG_Source_Lang lang = CG_SOURCE_LANG_C;
        bool dup = false;

        if (!cg_check_no_genex("target source", src)) return false;
        if (cg_is_header_like(src)) continue;
        if (!cg_classify_source_lang(src, &lang)) {
            nob_log(NOB_ERROR, "codegen: unsupported source language on target '%.*s': %.*s",
                    (int)bm_query_target_name(ctx->model, id).count,
                    bm_query_target_name(ctx->model, id).data ? bm_query_target_name(ctx->model, id).data : "",
                    (int)src.count, src.data ? src.data : "");
            return false;
        }

        if (!cg_rebase_from_base(ctx, src, owner_source_dir, &rebased)) return false;
        for (size_t j = 0; j < arena_arr_len(*out); ++j) {
            if (nob_sv_eq((*out)[j].path, rebased)) {
                dup = true;
                break;
            }
        }
        if (!dup && !arena_arr_push(ctx->scratch, *out, ((CG_Source_Info){.path = rebased, .lang = lang}))) {
            return false;
        }
    }

    return true;
}

static bool cg_collect_compile_args(CG_Context *ctx, BM_Target_Id id, String_View **out) {
    BM_String_Item_Span includes = {0};
    BM_String_Item_Span defs = {0};
    BM_String_Item_Span opts = {0};
    if (!bm_query_target_effective_include_directories_items(ctx->model, id, ctx->scratch, &includes) ||
        !bm_query_target_effective_compile_definitions_items(ctx->model, id, ctx->scratch, &defs) ||
        !bm_query_target_effective_compile_options_items(ctx->model, id, ctx->scratch, &opts)) {
        return false;
    }

    for (size_t i = 0; i < includes.count; ++i) {
        String_View path = {0};
        if (!cg_check_no_genex("include directory", includes.items[i].value)) return false;
        if (!cg_rebase_from_provenance(ctx, includes.items[i].value, includes.items[i].provenance, &path)) return false;
        if (includes.items[i].flags & BM_ITEM_FLAG_SYSTEM) {
            if (!arena_arr_push(ctx->scratch, *out, nob_sv_from_cstr("-isystem")) ||
                !arena_arr_push(ctx->scratch, *out, path)) {
                return false;
            }
        } else {
            char *arg = cg_arena_sprintf(ctx->scratch, "-I%.*s", (int)path.count, path.data ? path.data : "");
            if (!arg || !arena_arr_push(ctx->scratch, *out, nob_sv_from_cstr(arg))) return false;
        }
    }

    for (size_t i = 0; i < defs.count; ++i) {
        String_View item = defs.items[i].value;
        char *arg = NULL;
        if (!cg_check_no_genex("compile definition", item)) return false;
        if (cg_sv_has_prefix(item, "-D")) arg = arena_strndup(ctx->scratch, item.data, item.count);
        else arg = cg_arena_sprintf(ctx->scratch, "-D%.*s", (int)item.count, item.data ? item.data : "");
        if (!arg || !arena_arr_push(ctx->scratch, *out, nob_sv_from_cstr(arg))) return false;
    }

    for (size_t i = 0; i < opts.count; ++i) {
        if (!cg_check_no_genex("compile option", opts.items[i].value)) return false;
        if (!arena_arr_push(ctx->scratch, *out, opts.items[i].value)) return false;
    }

    return true;
}

static bool cg_collect_link_dir_args(CG_Context *ctx, BM_Target_Id id, String_View **out) {
    BM_String_Item_Span dirs = {0};
    if (!bm_query_target_effective_link_directories_items(ctx->model, id, ctx->scratch, &dirs)) return false;

    for (size_t i = 0; i < dirs.count; ++i) {
        String_View path = {0};
        char *arg = NULL;
        if (!cg_check_no_genex("link directory", dirs.items[i].value)) return false;
        if (cg_sv_has_prefix(dirs.items[i].value, "-L")) {
            if (!arena_arr_push(ctx->scratch, *out, dirs.items[i].value)) return false;
            continue;
        }
        if (!cg_rebase_from_provenance(ctx, dirs.items[i].value, dirs.items[i].provenance, &path)) return false;
        arg = cg_arena_sprintf(ctx->scratch, "-L%.*s", (int)path.count, path.data ? path.data : "");
        if (!arg || !arena_arr_push(ctx->scratch, *out, nob_sv_from_cstr(arg))) return false;
    }

    return true;
}

static bool cg_collect_link_option_args(CG_Context *ctx, BM_Target_Id id, String_View **out) {
    BM_String_Item_Span opts = {0};
    if (!bm_query_target_effective_link_options_items(ctx->model, id, ctx->scratch, &opts)) return false;

    for (size_t i = 0; i < opts.count; ++i) {
        if (!cg_check_no_genex("link option", opts.items[i].value)) return false;
        if (!arena_arr_push(ctx->scratch, *out, opts.items[i].value)) return false;
    }

    return true;
}

static bool cg_collect_link_library_args(CG_Context *ctx,
                                         BM_Target_Id id,
                                         String_View **out_args,
                                         String_View **out_rebuild_inputs) {
    BM_String_Item_Span libs = {0};
    if (!bm_query_target_effective_link_libraries_items(ctx->model, id, ctx->scratch, &libs)) return false;

    for (size_t i = 0; i < libs.count; ++i) {
        BM_Target_Id dep_id = BM_TARGET_ID_INVALID;
        String_View item = libs.items[i].value;

        if (!cg_check_no_genex("link library", item)) return false;

        if (cg_resolve_link_item_target(ctx, item, &dep_id)) {
            const CG_Target_Info *dep = cg_target_info(ctx, dep_id);
            if (!dep) return false;
            if (dep->imported) {
                nob_log(NOB_ERROR, "codegen: imported target '%.*s' is not supported in link libraries",
                        (int)item.count, item.data ? item.data : "");
                return false;
            }
            if (dep->kind == BM_TARGET_INTERFACE_LIBRARY) continue;
            if (!cg_target_kind_is_linkable_artifact(dep->kind)) {
                nob_log(NOB_ERROR, "codegen: local link target '%.*s' is not linkable in the POSIX backend",
                        (int)item.count, item.data ? item.data : "");
                return false;
            }
            if (!arena_arr_push(ctx->scratch, *out_args, dep->artifact_path) ||
                !arena_arr_push(ctx->scratch, *out_rebuild_inputs, dep->artifact_path)) {
                return false;
            }
            continue;
        }

        if (cg_sv_has_prefix(item, "-")) {
            if (!arena_arr_push(ctx->scratch, *out_args, item)) return false;
            continue;
        }

        if (cg_sv_contains(item, "/")) {
            String_View path = {0};
            if (!cg_rebase_from_provenance(ctx, item, libs.items[i].provenance, &path)) return false;
            if (!arena_arr_push(ctx->scratch, *out_args, path)) return false;
            continue;
        }

        if (cg_is_link_file_like(item)) {
            if (!arena_arr_push(ctx->scratch, *out_args, item)) return false;
            continue;
        }

        if (cg_is_bare_library_name(item)) {
            char *arg = cg_arena_sprintf(ctx->scratch, "-l%.*s", (int)item.count, item.data ? item.data : "");
            if (!arg || !arena_arr_push(ctx->scratch, *out_args, nob_sv_from_cstr(arg))) return false;
            continue;
        }

        nob_log(NOB_ERROR, "codegen: unsupported link library item: %.*s",
                (int)item.count, item.data ? item.data : "");
        return false;
    }

    return true;
}

static bool cg_emit_cmd_append_sv(Nob_String_Builder *out, const char *cmd_var, String_View arg) {
    if (!out || !cmd_var) return false;
    nob_sb_append_cstr(out, "        nob_cmd_append(&");
    nob_sb_append_cstr(out, cmd_var);
    nob_sb_append_cstr(out, ", ");
    if (!cg_sb_append_c_string(out, arg)) return false;
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

static bool cg_emit_support_helpers(Nob_String_Builder *out) {
    if (!out) return false;
    nob_sb_append_cstr(out,
        "static void append_toolchain_cmd(Nob_Cmd *cmd, bool use_cxx) {\n"
        "    const char *tool = getenv(use_cxx ? \"CXX\" : \"CC\");\n"
        "    if (!tool || tool[0] == '\\0') tool = use_cxx ? \"c++\" : \"cc\";\n"
        "    nob_cmd_append(cmd, tool);\n"
        "}\n"
        "\n"
        "static bool ensure_dir(const char *path) {\n"
        "    Nob_Cmd cmd = {0};\n"
        "    bool ok = false;\n"
        "    nob_cmd_append(&cmd, \"mkdir\", \"-p\", path);\n"
        "    ok = nob_cmd_run(&cmd);\n"
        "    nob_cmd_free(cmd);\n"
        "    return ok;\n"
        "}\n"
        "\n"
        "static bool install_copy_file(const char *src_path, const char *dst_path) {\n"
        "    return nob_copy_file(src_path, dst_path);\n"
        "}\n"
        "\n");
    return true;
}

static bool cg_emit_target_function(CG_Context *ctx, const CG_Target_Info *info, Nob_String_Builder *out) {
    BM_Target_Id *deps = NULL;
    CG_Source_Info *sources = NULL;
    String_View *compile_args = NULL;
    String_View *link_dir_args = NULL;
    String_View *link_opt_args = NULL;
    String_View *link_lib_args = NULL;
    String_View *link_rebuild_inputs = NULL;
    String_View object_dir = {0};
    String_View artifact_dir = {0};
    bool needs_cxx_linker = false;
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

    if (!cg_collect_build_dependencies(ctx, info->id, &deps) ||
        !cg_collect_compile_sources(ctx, info->id, &sources) ||
        !cg_collect_compile_args(ctx, info->id, &compile_args)) {
        return false;
    }

    if (arena_arr_empty(sources)) {
        nob_log(NOB_ERROR, "codegen: target '%.*s' has no compilable C/C++ sources",
                (int)info->name.count, info->name.data ? info->name.data : "");
        return false;
    }
    needs_cxx_linker = info->needs_cxx_linker;
    needs_pic = cg_target_needs_pic(info->kind);

    if (!cg_join_paths_to_arena(ctx->scratch,
                                nob_sv_from_cstr("build/obj"),
                                nob_sv_from_cstr(info->ident),
                                &object_dir)) {
        return false;
    }
    artifact_dir = cg_dirname_to_arena(ctx->scratch, info->artifact_path);

    for (size_t i = 0; i < arena_arr_len(deps); ++i) {
        const CG_Target_Info *dep = cg_target_info(ctx, deps[i]);
        if (!dep) return false;
        nob_sb_append_cstr(out, "    if (!build_");
        nob_sb_append_cstr(out, dep->ident);
        nob_sb_append_cstr(out, "()) return false;\n");
    }

    nob_sb_append_cstr(out, "    if (!ensure_dir(");
    if (!cg_sb_append_c_string(out, object_dir)) return false;
    nob_sb_append_cstr(out, ")) return false;\n");
    nob_sb_append_cstr(out, "    if (!ensure_dir(");
    if (!cg_sb_append_c_string(out, artifact_dir)) return false;
    nob_sb_append_cstr(out, ")) return false;\n");

    for (size_t i = 0; i < arena_arr_len(sources); ++i) {
        String_View obj_path = {0};
        if (!cg_join_paths_to_arena(ctx->scratch,
                                    object_dir,
                                    nob_sv_from_cstr(nob_temp_sprintf("%zu.o", i)),
                                    &obj_path)) {
            return false;
        }

        nob_sb_append_cstr(out, "    if (nob_needs_rebuild(");
        if (!cg_sb_append_c_string(out, obj_path)) return false;
        nob_sb_append_cstr(out, ", (const char*[]){__FILE__, ");
        if (!cg_sb_append_c_string(out, sources[i].path)) return false;
        nob_sb_append_cstr(out, "}, 2)) {\n");
        nob_sb_append_cstr(out, "        Nob_Cmd cc_cmd = {0};\n");
        if (!cg_emit_cmd_append_toolchain(out, "cc_cmd", sources[i].lang == CG_SOURCE_LANG_CXX)) return false;
        for (size_t arg = 0; arg < arena_arr_len(compile_args); ++arg) {
            if (!cg_emit_cmd_append_sv(out, "cc_cmd", compile_args[arg])) return false;
        }
        if (needs_pic && !cg_emit_cmd_append_sv(out, "cc_cmd", nob_sv_from_cstr("-fPIC"))) return false;
        if (!cg_emit_cmd_append_sv(out, "cc_cmd", nob_sv_from_cstr("-c")) ||
            !cg_emit_cmd_append_sv(out, "cc_cmd", sources[i].path) ||
            !cg_emit_cmd_append_sv(out, "cc_cmd", nob_sv_from_cstr("-o")) ||
            !cg_emit_cmd_append_sv(out, "cc_cmd", obj_path)) {
            return false;
        }
        nob_sb_append_cstr(out, "        {\n");
        nob_sb_append_cstr(out, "            bool ok = nob_cmd_run(&cc_cmd);\n");
        nob_sb_append_cstr(out, "            nob_cmd_free(cc_cmd);\n");
        nob_sb_append_cstr(out, "            if (!ok) return false;\n");
        nob_sb_append_cstr(out, "        }\n");
        nob_sb_append_cstr(out, "    }\n");
    }

    if (info->kind == BM_TARGET_STATIC_LIBRARY) {
        nob_sb_append_cstr(out, "    if (nob_needs_rebuild(");
        if (!cg_sb_append_c_string(out, info->artifact_path)) return false;
        nob_sb_append_cstr(out, ", (const char*[]){__FILE__");
        for (size_t i = 0; i < arena_arr_len(sources); ++i) {
            String_View obj_path = {0};
            if (!cg_join_paths_to_arena(ctx->scratch,
                                        object_dir,
                                        nob_sv_from_cstr(nob_temp_sprintf("%zu.o", i)),
                                        &obj_path)) {
                return false;
            }
            nob_sb_append_cstr(out, ", ");
            if (!cg_sb_append_c_string(out, obj_path)) return false;
        }
        nob_sb_append_cstr(out, "}, ");
        nob_sb_append_cstr(out, nob_temp_sprintf("%zu", arena_arr_len(sources) + 1));
        nob_sb_append_cstr(out, ")) {\n");
        nob_sb_append_cstr(out, "        Nob_Cmd ar_cmd = {0};\n");
        nob_sb_append_cstr(out, "        nob_cmd_append(&ar_cmd, \"ar\", \"rcs\", ");
        if (!cg_sb_append_c_string(out, info->artifact_path)) return false;
        for (size_t i = 0; i < arena_arr_len(sources); ++i) {
            String_View obj_path = {0};
            if (!cg_join_paths_to_arena(ctx->scratch,
                                        object_dir,
                                        nob_sv_from_cstr(nob_temp_sprintf("%zu.o", i)),
                                        &obj_path)) {
                return false;
            }
            nob_sb_append_cstr(out, ", ");
            if (!cg_sb_append_c_string(out, obj_path)) return false;
        }
        nob_sb_append_cstr(out, ");\n");
        nob_sb_append_cstr(out, "        {\n");
        nob_sb_append_cstr(out, "            bool ok = nob_cmd_run(&ar_cmd);\n");
        nob_sb_append_cstr(out, "            nob_cmd_free(ar_cmd);\n");
        nob_sb_append_cstr(out, "            if (!ok) return false;\n");
        nob_sb_append_cstr(out, "        }\n");
        nob_sb_append_cstr(out, "    }\n");
    } else if (info->kind == BM_TARGET_EXECUTABLE ||
               info->kind == BM_TARGET_SHARED_LIBRARY ||
               info->kind == BM_TARGET_MODULE_LIBRARY) {
        if (!cg_collect_link_dir_args(ctx, info->id, &link_dir_args) ||
            !cg_collect_link_option_args(ctx, info->id, &link_opt_args) ||
            !cg_collect_link_library_args(ctx, info->id, &link_lib_args, &link_rebuild_inputs)) {
            return false;
        }

        nob_sb_append_cstr(out, "    if (nob_needs_rebuild(");
        if (!cg_sb_append_c_string(out, info->artifact_path)) return false;
        nob_sb_append_cstr(out, ", (const char*[]){__FILE__");
        for (size_t i = 0; i < arena_arr_len(sources); ++i) {
            String_View obj_path = {0};
            if (!cg_join_paths_to_arena(ctx->scratch,
                                        object_dir,
                                        nob_sv_from_cstr(nob_temp_sprintf("%zu.o", i)),
                                        &obj_path)) {
                return false;
            }
            nob_sb_append_cstr(out, ", ");
            if (!cg_sb_append_c_string(out, obj_path)) return false;
        }
        for (size_t i = 0; i < arena_arr_len(link_rebuild_inputs); ++i) {
            nob_sb_append_cstr(out, ", ");
            if (!cg_sb_append_c_string(out, link_rebuild_inputs[i])) return false;
        }
        nob_sb_append_cstr(out, "}, ");
        nob_sb_append_cstr(out, nob_temp_sprintf("%zu", arena_arr_len(sources) + arena_arr_len(link_rebuild_inputs) + 1));
        nob_sb_append_cstr(out, ")) {\n");
        nob_sb_append_cstr(out, "        Nob_Cmd link_cmd = {0};\n");
        if (!cg_emit_cmd_append_toolchain(out, "link_cmd", needs_cxx_linker)) return false;
        if ((info->kind == BM_TARGET_SHARED_LIBRARY || info->kind == BM_TARGET_MODULE_LIBRARY) &&
            !cg_emit_cmd_append_sv(out, "link_cmd", nob_sv_from_cstr("-shared"))) {
            return false;
        }
        if (!cg_emit_cmd_append_sv(out, "link_cmd", nob_sv_from_cstr("-o")) ||
            !cg_emit_cmd_append_sv(out, "link_cmd", info->artifact_path)) {
            return false;
        }
        for (size_t i = 0; i < arena_arr_len(sources); ++i) {
            String_View obj_path = {0};
            if (!cg_join_paths_to_arena(ctx->scratch,
                                        object_dir,
                                        nob_sv_from_cstr(nob_temp_sprintf("%zu.o", i)),
                                        &obj_path)) {
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
        nob_sb_append_cstr(out, "    }\n");
    }

    nob_sb_append_cstr(out, "    build_state = 2;\n");
    nob_sb_append_cstr(out, "    return true;\n");
    nob_sb_append_cstr(out, "}\n\n");
    return true;
}

static bool cg_emit_build_request(CG_Context *ctx, Nob_String_Builder *out) {
    if (!ctx || !out) return false;
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
    String_View *dirs = NULL;
    if (!ctx || !out) return false;

    if (!cg_collect_unique_path(ctx->scratch, &dirs, nob_sv_from_cstr("build")) ||
        !cg_collect_unique_path(ctx->scratch, &dirs, nob_sv_from_cstr("install"))) {
        return false;
    }

    for (size_t i = 0; i < ctx->target_count; ++i) {
        String_View dir = {0};
        if (!ctx->targets[i].emits_artifact) continue;
        dir = cg_dirname_to_arena(ctx->scratch, ctx->targets[i].artifact_path);
        if (dir.count == 0 ||
            cg_sv_eq_lit(dir, ".") ||
            cg_sv_eq_lit(dir, "/")) {
            continue;
        }
        if (!cg_collect_unique_path(ctx->scratch, &dirs, dir)) return false;
    }

    nob_sb_append_cstr(out, "static bool clean_all(void) {\n");
    nob_sb_append_cstr(out, "    Nob_Cmd cmd = {0};\n");
    nob_sb_append_cstr(out, "    bool ok = false;\n");
    nob_sb_append_cstr(out, "    nob_cmd_append(&cmd, \"rm\", \"-rf\"");
    for (size_t i = 0; i < arena_arr_len(dirs); ++i) {
        nob_sb_append_cstr(out, ", ");
        if (!cg_sb_append_c_string(out, dirs[i])) return false;
    }
    nob_sb_append_cstr(out, ");\n");
    nob_sb_append_cstr(out, "    ok = nob_cmd_run(&cmd);\n");
    nob_sb_append_cstr(out, "    nob_cmd_free(cmd);\n");
    nob_sb_append_cstr(out, "    return ok;\n");
    nob_sb_append_cstr(out, "}\n\n");
    return true;
}

static bool cg_emit_install_function(CG_Context *ctx, Nob_String_Builder *out) {
    size_t rule_count = 0;
    if (!ctx || !out) return false;

    rule_count = bm_query_install_rule_count(ctx->model);
    nob_sb_append_cstr(out, "static bool install_all(void) {\n");
    if (rule_count == 0) {
        nob_sb_append_cstr(out, "    return true;\n");
        nob_sb_append_cstr(out, "}\n\n");
        return true;
    }

    nob_sb_append_cstr(out, "    if (!ensure_dir(\"install\")) return false;\n");
    for (size_t i = 0; i < rule_count; ++i) {
        BM_Install_Rule_Id id = (BM_Install_Rule_Id)i;
        BM_Install_Rule_Kind kind = bm_query_install_rule_kind(ctx->model, id);
        String_View destination = bm_query_install_rule_destination(ctx->model, id);
        String_View install_dir = nob_sv_from_cstr("install");

        if (destination.count > 0 &&
            !cg_join_paths_to_arena(ctx->scratch,
                                    nob_sv_from_cstr("install"),
                                    destination,
                                    &install_dir)) {
            return false;
        }

        if (kind == BM_INSTALL_RULE_TARGET) {
            BM_Target_Id target_id = bm_query_install_rule_target(ctx->model, id);
            const CG_Target_Info *info = cg_target_info(ctx, target_id);
            String_View basename = {0};
            String_View install_path = {0};
            if (!info || !info->emits_artifact) {
                nob_log(NOB_ERROR, "codegen: unsupported install target rule");
                return false;
            }
            basename = cg_basename_to_arena(ctx->scratch, info->artifact_path);
            if (!cg_join_paths_to_arena(ctx->scratch, install_dir, basename, &install_path)) return false;

            nob_sb_append_cstr(out, "    if (!build_");
            nob_sb_append_cstr(out, info->ident);
            nob_sb_append_cstr(out, "()) return false;\n");
            nob_sb_append_cstr(out, "    if (!ensure_dir(");
            if (!cg_sb_append_c_string(out, install_dir)) return false;
            nob_sb_append_cstr(out, ")) return false;\n");
            nob_sb_append_cstr(out, "    if (!install_copy_file(");
            if (!cg_sb_append_c_string(out, info->artifact_path)) return false;
            nob_sb_append_cstr(out, ", ");
            if (!cg_sb_append_c_string(out, install_path)) return false;
            nob_sb_append_cstr(out, ")) return false;\n");
            continue;
        }

        if (kind == BM_INSTALL_RULE_FILE) {
            BM_Directory_Id owner = bm_query_install_rule_owner_directory(ctx->model, id);
            String_View owner_source_dir = bm_query_directory_source_dir(ctx->model, owner);
            String_View item = bm_query_install_rule_item_raw(ctx->model, id);
            String_View src_path = {0};
            String_View basename = {0};
            String_View install_path = {0};

            if (!cg_check_no_genex("install(FILES)", item)) return false;
            if (cg_sv_has_prefix(item, "SCRIPT::") ||
                cg_sv_has_prefix(item, "CODE::") ||
                cg_sv_has_prefix(item, "EXPORT::") ||
                cg_sv_has_prefix(item, "EXPORT_ANDROID_MK::")) {
                nob_log(NOB_ERROR,
                        "codegen: unsupported install(FILES) pseudo-item: %.*s",
                        (int)item.count,
                        item.data ? item.data : "");
                return false;
            }

            if (!cg_rebase_from_base(ctx, item, owner_source_dir, &src_path)) return false;
            basename = cg_basename_to_arena(ctx->scratch, item);
            if (!cg_join_paths_to_arena(ctx->scratch, install_dir, basename, &install_path)) return false;

            nob_sb_append_cstr(out, "    if (!ensure_dir(");
            if (!cg_sb_append_c_string(out, install_dir)) return false;
            nob_sb_append_cstr(out, ")) return false;\n");
            nob_sb_append_cstr(out, "    if (!install_copy_file(");
            if (!cg_sb_append_c_string(out, src_path)) return false;
            nob_sb_append_cstr(out, ", ");
            if (!cg_sb_append_c_string(out, install_path)) return false;
            nob_sb_append_cstr(out, ")) return false;\n");
            continue;
        }

        nob_log(NOB_ERROR,
                "codegen: unsupported install rule kind in minimal backend: %d",
                (int)kind);
        return false;
    }

    nob_sb_append_cstr(out, "    return true;\n");
    nob_sb_append_cstr(out, "}\n\n");
    return true;
}

static bool cg_emit_package_function(CG_Context *ctx, Nob_String_Builder *out) {
    bool has_package_data = false;
    if (!ctx || !out) return false;

    has_package_data =
        bm_query_package_count(ctx->model) > 0 ||
        bm_query_cpack_install_type_count(ctx->model) > 0 ||
        bm_query_cpack_component_group_count(ctx->model) > 0 ||
        bm_query_cpack_component_count(ctx->model) > 0;

    nob_sb_append_cstr(out, "static bool package_all(void) {\n");
    if (!has_package_data) {
        nob_sb_append_cstr(out, "    return true;\n");
    } else {
        nob_sb_append_cstr(out,
            "    nob_log(NOB_ERROR, \"codegen: package command is not implemented in the minimal backend\");\n"
            "    return false;\n");
    }
    nob_sb_append_cstr(out, "}\n\n");
    return true;
}

static bool cg_emit_main(CG_Context *ctx, Nob_String_Builder *out) {
    if (!ctx || !out) return false;
    nob_sb_append_cstr(out,
        "int main(int argc, char **argv) {\n"
        "    if (argc > 1 && strcmp(argv[1], \"clean\") == 0) {\n"
        "        return clean_all() ? 0 : 1;\n"
        "    }\n"
        "    if (argc > 1 && strcmp(argv[1], \"install\") == 0) {\n"
        "        return install_all() ? 0 : 1;\n"
        "    }\n"
        "    if (argc > 1 && strcmp(argv[1], \"package\") == 0) {\n"
        "        return package_all() ? 0 : 1;\n"
        "    }\n"
        "    if (argc > 1) {\n"
        "        for (int i = 1; i < argc; ++i) {\n"
        "            if (!build_request(argv[i])) return 1;\n"
        "        }\n"
        "        return 0;\n"
        "    }\n");

    for (size_t i = 0; i < ctx->target_count; ++i) {
        const CG_Target_Info *info = &ctx->targets[i];
        if (info->alias || info->imported || info->exclude_from_all || !info->emits_artifact) continue;
        nob_sb_append_cstr(out, "    if (!build_");
        nob_sb_append_cstr(out, info->ident);
        nob_sb_append_cstr(out, "()) return 1;\n");
    }

    nob_sb_append_cstr(out,
        "    return 0;\n"
        "}\n");
    return true;
}

static bool cg_init_context(CG_Context *ctx,
                            const Build_Model *model,
                            Arena *scratch,
                            const Nob_Codegen_Options *opts) {
    const char *cwd = NULL;
    if (!ctx || !model || !scratch || !opts) return false;
    memset(ctx, 0, sizeof(*ctx));
    ctx->model = model;
    ctx->scratch = scratch;
    ctx->opts = *opts;

    cwd = nob_get_current_dir_temp();
    if (!cwd) {
        nob_log(NOB_ERROR, "codegen: failed to read current directory");
        return false;
    }

    if (!cg_absolute_from_cwd(ctx, nob_sv_from_cstr(cwd), &ctx->cwd_abs) ||
        !cg_absolute_from_cwd(ctx, opts->output_path, &ctx->emit_path_abs)) {
        return false;
    }

    ctx->emit_dir_abs = cg_dirname_to_arena(ctx->scratch, ctx->emit_path_abs);
    return cg_init_targets(ctx);
}

bool nob_codegen_render(const Build_Model *model,
                        Arena *scratch,
                        const Nob_Codegen_Options *opts,
                        Nob_String_Builder *out) {
    CG_Context ctx = {0};
    if (!out || !opts) return false;
    out->count = 0;

    if (!cg_init_context(&ctx, model, scratch, opts)) return false;

    nob_sb_append_cstr(out,
        "#define NOB_IMPLEMENTATION\n"
        "#include \"nob.h\"\n"
        "\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "\n");

    if (!cg_emit_target_forward_decls(&ctx, out) ||
        !cg_emit_support_helpers(out)) {
        return false;
    }

    for (size_t i = 0; i < ctx.target_count; ++i) {
        if (!cg_emit_target_function(&ctx, &ctx.targets[i], out)) return false;
    }

    if (!cg_emit_build_request(&ctx, out) ||
        !cg_emit_clean_function(&ctx, out) ||
        !cg_emit_install_function(&ctx, out) ||
        !cg_emit_package_function(&ctx, out) ||
        !cg_emit_main(&ctx, out)) {
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
    if (out_dir && strcmp(out_dir, ".") != 0) {
        Nob_Cmd mkdir_cmd = {0};
        bool mkdir_ok = false;
        nob_cmd_append(&mkdir_cmd, "mkdir", "-p", out_dir);
        mkdir_ok = nob_cmd_run(&mkdir_cmd);
        nob_cmd_free(mkdir_cmd);
        if (!mkdir_ok) return false;
    }
    if (!nob_codegen_render(model, scratch, opts, &sb)) {
        nob_sb_free(sb);
        return false;
    }
    bool ok = nob_write_entire_file(out_path, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    return ok;
}
