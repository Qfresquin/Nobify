#include "build_model_query_internal.h"

static bool bm_query_platform_eq(String_View platform_id, const char *name) {
    return bm_sv_eq_ci_query(nob_sv_trim(platform_id), nob_sv_from_cstr(name ? name : ""));
}

static bool bm_query_platform_is_windows(const BM_Query_Eval_Context *ctx) {
    return ctx && bm_query_platform_eq(ctx->platform_id, "Windows");
}

static bool bm_query_platform_is_darwin(const BM_Query_Eval_Context *ctx) {
    return ctx && bm_query_platform_eq(ctx->platform_id, "Darwin");
}

static String_View bm_uppercase_copy_query(Arena *scratch, String_View value) {
    char *copy = NULL;
    if (!scratch || value.count == 0) return nob_sv_from_cstr("");
    copy = arena_strndup(scratch, value.data ? value.data : "", value.count);
    if (!copy) return nob_sv_from_cstr("");
    for (size_t i = 0; i < value.count; ++i) copy[i] = (char)toupper((unsigned char)copy[i]);
    return nob_sv_from_parts(copy, value.count);
}

static String_View bm_property_name_with_config_prefix(Arena *scratch,
                                                       const char *prefix,
                                                       String_View config) {
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    if (!scratch || !prefix || config.count == 0) return nob_sv_from_cstr("");
    nob_sb_append_cstr(&sb, prefix);
    nob_sb_append_buf(&sb, config.data ? config.data : "", config.count);
    copy = arena_strndup(scratch, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    return copy ? nob_sv_from_parts(copy, sb.count) : nob_sv_from_cstr("");
}

static bool bm_query_target_raw_property_first(const Build_Model *model,
                                               BM_Target_Id id,
                                               String_View property_name,
                                               String_View *out) {
    BM_String_Span span = bm_query_target_raw_property_items(model, id, property_name);
    if (out) *out = span.count > 0 ? span.items[0] : nob_sv_from_cstr("");
    return true;
}

static bool bm_query_target_imported_property_for_config(const Build_Model *model,
                                                         BM_Target_Id id,
                                                         String_View active_config,
                                                         const char *config_prefix,
                                                         const char *base_name,
                                                         Arena *scratch,
                                                         String_View *out) {
    String_View upper_cfg = nob_sv_from_cstr("");
    String_View config_name = {0};
    String_View property_name = {0};
    String_View mapped_property = {0};
    if (!scratch || !out || !base_name || !config_prefix) return false;
    *out = nob_sv_from_cstr("");

    if (active_config.count > 0) {
        upper_cfg = bm_uppercase_copy_query(scratch, active_config);
        property_name = bm_property_name_with_config_prefix(scratch, config_prefix, upper_cfg);
        if (property_name.count > 0) {
            bm_query_target_raw_property_first(model, id, property_name, out);
            if (out->count > 0) return true;
        }

        mapped_property = bm_property_name_with_config_prefix(scratch, "MAP_IMPORTED_CONFIG_", upper_cfg);
        if (mapped_property.count > 0) {
            BM_String_Span mapped = bm_query_target_raw_property_items(model, id, mapped_property);
            for (size_t i = 0; i < mapped.count; ++i) {
                size_t start = 0;
                for (size_t k = 0; k <= mapped.items[i].count; ++k) {
                    bool sep = (k == mapped.items[i].count) || (mapped.items[i].data[k] == ';');
                    if (!sep) continue;
                    config_name = nob_sv_trim(nob_sv_from_parts(mapped.items[i].data + start, k - start));
                    start = k + 1;
                    if (config_name.count == 0) continue;
                    property_name = bm_property_name_with_config_prefix(
                        scratch,
                        config_prefix,
                        bm_uppercase_copy_query(scratch, config_name));
                    if (property_name.count == 0) continue;
                    bm_query_target_raw_property_first(model, id, property_name, out);
                    if (out->count > 0) return true;
                }
            }
        }
    }

    return bm_query_target_raw_property_first(model, id, nob_sv_from_cstr(base_name), out);
}

static bool bm_query_target_local_file_internal(const Build_Model *model,
                                                BM_Target_Id id,
                                                const BM_Query_Eval_Context *ctx,
                                                bool linker_file,
                                                Arena *scratch,
                                                String_View *out) {
    BM_Target_Id resolved_id = bm_resolve_alias_target_id(model, id);
    BM_Target_Kind kind = bm_query_target_kind(model, resolved_id);
    BM_Directory_Id owner = bm_query_target_owner_directory(model, resolved_id);
    String_View output_name = bm_query_target_output_name(model, resolved_id);
    String_View prefix = bm_query_target_prefix(model, resolved_id);
    String_View suffix = bm_query_target_suffix(model, resolved_id);
    String_View owner_binary_dir = bm_query_directory_binary_dir(model, owner);
    String_View output_dir = nob_sv_from_cstr("");
    bool is_windows = bm_query_platform_is_windows(ctx);
    bool is_darwin = bm_query_platform_is_darwin(ctx);
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    String_View basename = {0};
    if (!scratch || !out || !bm_target_id_is_valid(resolved_id)) return false;
    *out = nob_sv_from_cstr("");

    if (kind == BM_TARGET_EXECUTABLE) {
        output_dir = bm_query_target_runtime_output_directory(model, resolved_id);
        if (output_name.count == 0) output_name = bm_query_target_name(model, resolved_id);
        if (suffix.count == 0 && is_windows) suffix = nob_sv_from_cstr(".exe");
        nob_sb_append_buf(&sb, prefix.data ? prefix.data : "", prefix.count);
        nob_sb_append_buf(&sb, output_name.data ? output_name.data : "", output_name.count);
        nob_sb_append_buf(&sb, suffix.data ? suffix.data : "", suffix.count);
    } else if (kind == BM_TARGET_STATIC_LIBRARY) {
        output_dir = bm_query_target_archive_output_directory(model, resolved_id);
        if (output_name.count == 0) output_name = bm_query_target_name(model, resolved_id);
        if (prefix.count == 0 && !is_windows) prefix = nob_sv_from_cstr("lib");
        if (suffix.count == 0) suffix = is_windows ? nob_sv_from_cstr(".lib") : nob_sv_from_cstr(".a");
        nob_sb_append_buf(&sb, prefix.data ? prefix.data : "", prefix.count);
        nob_sb_append_buf(&sb, output_name.data ? output_name.data : "", output_name.count);
        nob_sb_append_buf(&sb, suffix.data ? suffix.data : "", suffix.count);
    } else if (kind == BM_TARGET_SHARED_LIBRARY || kind == BM_TARGET_MODULE_LIBRARY) {
        if (is_windows) {
            output_dir = linker_file
                ? bm_query_target_archive_output_directory(model, resolved_id)
                : bm_query_target_runtime_output_directory(model, resolved_id);
        } else {
            output_dir = bm_query_target_library_output_directory(model, resolved_id);
        }
        if (output_name.count == 0) output_name = bm_query_target_name(model, resolved_id);
        if (prefix.count == 0 && !is_windows) prefix = nob_sv_from_cstr("lib");
        if (suffix.count == 0) {
            if (is_windows) {
                suffix = linker_file ? nob_sv_from_cstr(".lib") : nob_sv_from_cstr(".dll");
            } else if (kind == BM_TARGET_SHARED_LIBRARY && is_darwin) {
                suffix = nob_sv_from_cstr(".dylib");
            } else {
                suffix = nob_sv_from_cstr(".so");
            }
        }
        nob_sb_append_buf(&sb, prefix.data ? prefix.data : "", prefix.count);
        nob_sb_append_buf(&sb, output_name.data ? output_name.data : "", output_name.count);
        nob_sb_append_buf(&sb, suffix.data ? suffix.data : "", suffix.count);
    } else {
        nob_sb_free(sb);
        return true;
    }

    copy = arena_strndup(scratch, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;
    basename = nob_sv_from_parts(copy, strlen(copy));

    if (output_dir.count == 0) {
        *out = bm_join_relative_path_query(scratch, owner_binary_dir, basename);
    } else {
        *out = bm_join_relative_path_query(scratch,
                                           bm_join_relative_path_query(scratch, owner_binary_dir, output_dir),
                                           basename);
    }
    return true;
}

static bool bm_query_target_effective_file_internal(const Build_Model *model,
                                                    BM_Target_Id id,
                                                    const BM_Query_Eval_Context *ctx,
                                                    bool linker_file,
                                                    Arena *scratch,
                                                    String_View *out) {
    BM_Target_Id resolved_id = bm_resolve_alias_target_id(model, id);
    const BM_Target_Record *target = bm_model_target(model, resolved_id);
    String_View property_value = nob_sv_from_cstr("");
    String_View source_base = {0};
    if (!scratch || !out || !target) return false;
    *out = nob_sv_from_cstr("");

    if (!target->imported) {
        return bm_query_target_local_file_internal(model, resolved_id, ctx, linker_file, scratch, out);
    }

    if (linker_file) {
        if (!bm_query_target_imported_property_for_config(model,
                                                          resolved_id,
                                                          ctx ? ctx->config : nob_sv_from_cstr(""),
                                                          "IMPORTED_IMPLIB_",
                                                          "IMPORTED_IMPLIB",
                                                          scratch,
                                                          &property_value)) {
            return false;
        }
        if (property_value.count == 0 &&
            !bm_query_target_imported_property_for_config(model,
                                                          resolved_id,
                                                          ctx ? ctx->config : nob_sv_from_cstr(""),
                                                          "IMPORTED_LOCATION_",
                                                          "IMPORTED_LOCATION",
                                                          scratch,
                                                          &property_value)) {
            return false;
        }
    } else if (!bm_query_target_imported_property_for_config(model,
                                                             resolved_id,
                                                             ctx ? ctx->config : nob_sv_from_cstr(""),
                                                             "IMPORTED_LOCATION_",
                                                             "IMPORTED_LOCATION",
                                                             scratch,
                                                             &property_value)) {
        return false;
    }

    if (property_value.count == 0) return true;
    source_base = bm_query_directory_source_dir(model, bm_query_target_owner_directory(model, resolved_id));
    *out = bm_join_relative_path_query(scratch, source_base, property_value);
    return true;
}

bool bm_query_target_effective_file(const Build_Model *model,
                                    BM_Target_Id id,
                                    const BM_Query_Eval_Context *ctx,
                                    Arena *scratch,
                                    String_View *out) {
    return bm_query_target_effective_file_internal(model, id, ctx, false, scratch, out);
}

bool bm_query_target_effective_linker_file(const Build_Model *model,
                                           BM_Target_Id id,
                                           const BM_Query_Eval_Context *ctx,
                                           Arena *scratch,
                                           String_View *out) {
    return bm_query_target_effective_file_internal(model, id, ctx, true, scratch, out);
}

bool bm_query_target_imported_link_languages(const Build_Model *model,
                                             BM_Target_Id id,
                                             const BM_Query_Eval_Context *ctx,
                                             Arena *scratch,
                                             BM_String_Span *out) {
    String_View property_value = nob_sv_from_cstr("");
    String_View *values = NULL;
    if (!out) return false;
    out->items = NULL;
    out->count = 0;
    if (!scratch) return false;
    if (!bm_query_target_imported_property_for_config(model,
                                                      id,
                                                      ctx ? ctx->config : nob_sv_from_cstr(""),
                                                      "IMPORTED_LINK_INTERFACE_LANGUAGES_",
                                                      "IMPORTED_LINK_INTERFACE_LANGUAGES",
                                                      scratch,
                                                      &property_value)) {
        return false;
    }
    if (property_value.count == 0) return true;
    for (size_t i = 0, start = 0; i <= property_value.count; ++i) {
        bool sep = (i == property_value.count) || (property_value.data[i] == ';');
        String_View piece = {0};
        if (!sep) continue;
        piece = nob_sv_trim(nob_sv_from_parts(property_value.data + start, i - start));
        start = i + 1;
        if (piece.count == 0) continue;
        if (!bm_append_string_copy(scratch, &values, piece)) return false;
    }
    out->items = values;
    out->count = arena_arr_len(values);
    return true;
}
