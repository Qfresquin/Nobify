#include "build_model_query_internal.h"

static const BM_Imported_Config_Record *bm_find_imported_config_record(const BM_Target_Record *target,
                                                                       String_View config) {
    if (!target) return NULL;
    for (size_t i = 0; i < arena_arr_len(target->imported_configs); ++i) {
        if (bm_sv_eq_ci_query(target->imported_configs[i].config, config)) {
            return &target->imported_configs[i];
        }
    }
    return NULL;
}

static const BM_Imported_Config_Map_Record *bm_find_imported_config_map_record(const BM_Target_Record *target,
                                                                               String_View config) {
    if (!target) return NULL;
    for (size_t i = 0; i < arena_arr_len(target->imported_config_maps); ++i) {
        if (bm_sv_eq_ci_query(target->imported_config_maps[i].config, config)) {
            return &target->imported_config_maps[i];
        }
    }
    return NULL;
}

static const BM_Imported_Config_Record *bm_select_imported_config_record(const BM_Target_Record *target,
                                                                         String_View active_config) {
    const BM_Imported_Config_Record *record = NULL;
    const BM_Imported_Config_Map_Record *map_record = NULL;
    if (!target) return NULL;

    if (active_config.count > 0) {
        record = bm_find_imported_config_record(target, active_config);
        if (record) return record;

        map_record = bm_find_imported_config_map_record(target, active_config);
        if (map_record) {
            for (size_t i = 0; i < arena_arr_len(map_record->mapped_configs); ++i) {
                record = bm_find_imported_config_record(target, map_record->mapped_configs[i]);
                if (record) return record;
            }
        }
    }

    return bm_find_imported_config_record(target, nob_sv_from_cstr(""));
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
    const BM_Imported_Config_Record *config_record = NULL;
    if (!scratch || !out || !target) return false;
    *out = nob_sv_from_cstr("");

    if (!target->imported) {
        return bm_query_target_local_file_internal(model, resolved_id, ctx, linker_file, scratch, out);
    }

    config_record = bm_select_imported_config_record(target, ctx ? ctx->config : nob_sv_from_cstr(""));
    if (!config_record) return true;

    if (linker_file) {
        if (config_record->effective_linker_file.count > 0) {
            *out = config_record->effective_linker_file;
            return true;
        }
        *out = config_record->effective_file;
        return true;
    }

    *out = config_record->effective_file;
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
    BM_Target_Id resolved_id = bm_resolve_alias_target_id(model, id);
    const BM_Target_Record *target = bm_model_target(model, resolved_id);
    const BM_Imported_Config_Record *config_record = NULL;
    String_View *copy = NULL;
    if (!out) return false;
    *out = (BM_String_Span){0};
    if (!scratch || !target) return false;

    config_record = bm_select_imported_config_record(target, ctx ? ctx->config : nob_sv_from_cstr(""));
    if (!config_record) return true;
    for (size_t i = 0; i < arena_arr_len(config_record->link_languages); ++i) {
        if (!arena_arr_push(scratch, copy, config_record->link_languages[i])) return false;
    }
    *out = bm_string_span(copy);
    return true;
}
