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

static bool bm_query_push_unique_imported_config(Arena *scratch,
                                                 String_View **configs,
                                                 String_View config) {
    if (!scratch || !configs || config.count == 0) return true;
    for (size_t i = 0; i < arena_arr_len(*configs); ++i) {
        if (bm_sv_eq_ci_query((*configs)[i], config)) return true;
    }
    return arena_arr_push(scratch, *configs, config);
}

typedef enum {
    BM_QUERY_ARTIFACT_ARCHIVE = 0,
    BM_QUERY_ARTIFACT_LIBRARY,
    BM_QUERY_ARTIFACT_RUNTIME,
} BM_Query_Artifact_Category;

static String_View bm_query_basename_sv(String_View path) {
    if (path.count == 0) return nob_sv_from_cstr("");
    for (size_t i = path.count; i-- > 0;) {
        char c = path.data[i];
        if (c == '/' || c == '\\') {
            return nob_sv_from_parts(path.data + i + 1, path.count - i - 1);
        }
    }
    return path;
}

static String_View bm_query_dirname_sv_local(String_View path) {
    if (path.count == 0) return nob_sv_from_cstr("");
    for (size_t i = path.count; i-- > 0;) {
        char c = path.data[i];
        if (c != '/' && c != '\\') continue;
        if (i == 0) return nob_sv_from_parts(path.data, 1);
        return nob_sv_from_parts(path.data, i);
    }
    return nob_sv_from_cstr("");
}

static bool bm_query_target_kind_is_local_artifact(BM_Target_Kind kind) {
    return kind == BM_TARGET_EXECUTABLE ||
           kind == BM_TARGET_STATIC_LIBRARY ||
           kind == BM_TARGET_SHARED_LIBRARY ||
           kind == BM_TARGET_MODULE_LIBRARY;
}

static BM_Query_Artifact_Category bm_query_artifact_category(BM_Target_Kind kind,
                                                             BM_Target_Artifact_Role role,
                                                             bool is_windows) {
    if (kind == BM_TARGET_EXECUTABLE) return BM_QUERY_ARTIFACT_RUNTIME;
    if (kind == BM_TARGET_STATIC_LIBRARY) return BM_QUERY_ARTIFACT_ARCHIVE;
    if (kind == BM_TARGET_SHARED_LIBRARY || kind == BM_TARGET_MODULE_LIBRARY) {
        if (is_windows) {
            return role == BM_TARGET_ARTIFACT_LINKER
                ? BM_QUERY_ARTIFACT_ARCHIVE
                : BM_QUERY_ARTIFACT_RUNTIME;
        }
        return BM_QUERY_ARTIFACT_LIBRARY;
    }
    return BM_QUERY_ARTIFACT_RUNTIME;
}

static const char *bm_query_artifact_category_name(BM_Query_Artifact_Category category) {
    switch (category) {
        case BM_QUERY_ARTIFACT_ARCHIVE: return "ARCHIVE";
        case BM_QUERY_ARTIFACT_LIBRARY: return "LIBRARY";
        case BM_QUERY_ARTIFACT_RUNTIME: return "RUNTIME";
    }
    return "RUNTIME";
}

static bool bm_query_find_artifact_property(const BM_Target_Record *target,
                                            String_View name,
                                            String_View *out) {
    if (out) *out = nob_sv_from_cstr("");
    if (!target || !out) return false;
    for (size_t i = 0; i < arena_arr_len(target->artifact_properties); ++i) {
        if (bm_sv_eq_ci_query(target->artifact_properties[i].name, name)) {
            *out = target->artifact_properties[i].value;
            return true;
        }
    }
    return false;
}

static bool bm_query_find_artifact_property_lit(const BM_Target_Record *target,
                                                const char *name,
                                                String_View *out) {
    return bm_query_find_artifact_property(target, nob_sv_from_cstr(name ? name : ""), out);
}

static bool bm_query_find_artifact_property_config_suffix(const BM_Target_Record *target,
                                                          const char *base,
                                                          String_View config,
                                                          String_View *out) {
    if (!base || config.count == 0) return false;
    return bm_query_find_artifact_property(
        target,
        nob_sv_from_cstr(nob_temp_sprintf("%s_%.*s", base, (int)config.count, config.data ? config.data : "")),
        out);
}

static bool bm_query_find_artifact_property_config_prefix(const BM_Target_Record *target,
                                                          String_View config,
                                                          const char *base,
                                                          String_View *out) {
    if (!base || config.count == 0) return false;
    return bm_query_find_artifact_property(
        target,
        nob_sv_from_cstr(nob_temp_sprintf("%.*s_%s", (int)config.count, config.data ? config.data : "", base)),
        out);
}

static bool bm_query_resolve_artifact_string(const Build_Model *model,
                                             BM_Target_Id target_id,
                                             const BM_Query_Eval_Context *ctx,
                                             Arena *scratch,
                                             String_View raw,
                                             String_View *out) {
    BM_Query_Eval_Context normalized = ctx ? *ctx : (BM_Query_Eval_Context){0};
    if (!out) return false;
    *out = nob_sv_from_cstr("");
    if (!bm_target_id_is_valid(normalized.current_target_id)) normalized.current_target_id = target_id;
    return bm_query_resolve_string_with_context(model, &normalized, scratch, raw, out);
}

static bool bm_query_artifact_default_naming(BM_Target_Kind kind,
                                             BM_Query_Artifact_Category category,
                                             bool is_windows,
                                             bool is_darwin,
                                             String_View *out_prefix,
                                             String_View *out_suffix) {
    if (out_prefix) *out_prefix = nob_sv_from_cstr("");
    if (out_suffix) *out_suffix = nob_sv_from_cstr("");
    switch (category) {
        case BM_QUERY_ARTIFACT_ARCHIVE:
            if (out_prefix) *out_prefix = is_windows ? nob_sv_from_cstr("") : nob_sv_from_cstr("lib");
            if (out_suffix) *out_suffix = is_windows ? nob_sv_from_cstr(".lib") : nob_sv_from_cstr(".a");
            return true;
        case BM_QUERY_ARTIFACT_LIBRARY:
            if (out_prefix) *out_prefix = is_windows ? nob_sv_from_cstr("") : nob_sv_from_cstr("lib");
            if (out_suffix) *out_suffix = (kind == BM_TARGET_SHARED_LIBRARY && is_darwin)
                ? nob_sv_from_cstr(".dylib")
                : nob_sv_from_cstr(".so");
            return true;
        case BM_QUERY_ARTIFACT_RUNTIME:
            if (out_suffix) {
                if (kind == BM_TARGET_EXECUTABLE && is_windows) {
                    *out_suffix = nob_sv_from_cstr(".exe");
                } else if ((kind == BM_TARGET_SHARED_LIBRARY || kind == BM_TARGET_MODULE_LIBRARY) && is_windows) {
                    *out_suffix = nob_sv_from_cstr(".dll");
                } else {
                    *out_suffix = nob_sv_from_cstr("");
                }
            }
            return true;
    }
    return false;
}

static bool bm_query_target_local_artifact_internal(const Build_Model *model,
                                                    BM_Target_Id id,
                                                    BM_Target_Artifact_Role role,
                                                    const BM_Query_Eval_Context *ctx,
                                                    Arena *scratch,
                                                    BM_Target_Artifact_View *out) {
    BM_Target_Id resolved_id = bm_resolve_alias_target_id(model, id);
    const BM_Target_Record *target = bm_model_target(model, resolved_id);
    BM_Target_Kind kind = bm_query_target_kind(model, resolved_id);
    BM_Directory_Id owner = bm_query_target_owner_directory(model, resolved_id);
    String_View owner_binary_dir = bm_query_directory_binary_dir(model, owner);
    String_View owner_source_dir = bm_query_directory_source_dir(model, owner);
    String_View config = ctx ? ctx->config : nob_sv_from_cstr("");
    bool is_windows = bm_query_platform_is_windows(ctx);
    bool is_darwin = bm_query_platform_is_darwin(ctx);
    BM_Query_Artifact_Category category = bm_query_artifact_category(kind, role, is_windows);
    const char *category_name = bm_query_artifact_category_name(category);
    String_View raw_output_name = {0};
    String_View raw_directory = {0};
    String_View raw_prefix = {0};
    String_View raw_suffix = {0};
    String_View default_prefix = {0};
    String_View default_suffix = {0};
    String_View directory = {0};
    bool has_prefix = false;
    bool has_suffix = false;
    Nob_String_Builder file_sb = {0};
    char *file_copy = NULL;
    if (out) *out = (BM_Target_Artifact_View){0};
    if (!model || !scratch || !out || !target || !bm_target_id_is_valid(resolved_id)) return false;
    if (!bm_query_target_kind_is_local_artifact(kind)) return true;

    if (!bm_query_find_artifact_property_config_suffix(target,
                                                       nob_temp_sprintf("%s_OUTPUT_NAME", category_name),
                                                       config,
                                                       &raw_output_name) &&
        !bm_query_find_artifact_property_lit(target,
                                             nob_temp_sprintf("%s_OUTPUT_NAME", category_name),
                                             &raw_output_name) &&
        !bm_query_find_artifact_property_config_suffix(target, "OUTPUT_NAME", config, &raw_output_name) &&
        !bm_query_find_artifact_property_config_prefix(target, config, "OUTPUT_NAME", &raw_output_name) &&
        !bm_query_find_artifact_property_lit(target, "OUTPUT_NAME", &raw_output_name)) {
        raw_output_name = target->name;
    }
    if (!bm_query_find_artifact_property_config_suffix(target,
                                                       nob_temp_sprintf("%s_OUTPUT_DIRECTORY", category_name),
                                                       config,
                                                       &raw_directory) &&
        !bm_query_find_artifact_property_lit(target,
                                             nob_temp_sprintf("%s_OUTPUT_DIRECTORY", category_name),
                                             &raw_directory)) {
        raw_directory = nob_sv_from_cstr("");
    }

    has_prefix = bm_query_find_artifact_property_lit(target, "PREFIX", &raw_prefix);
    has_suffix = bm_query_find_artifact_property_lit(target, "SUFFIX", &raw_suffix);

    if (!bm_query_artifact_default_naming(kind, category, is_windows, is_darwin, &default_prefix, &default_suffix)) {
        return false;
    }
    if (!has_prefix) raw_prefix = default_prefix;
    if (!has_suffix) raw_suffix = default_suffix;

    if (!bm_query_resolve_artifact_string(model, resolved_id, ctx, scratch, raw_output_name, &out->output_name) ||
        !bm_query_resolve_artifact_string(model, resolved_id, ctx, scratch, raw_prefix, &out->prefix) ||
        !bm_query_resolve_artifact_string(model, resolved_id, ctx, scratch, raw_suffix, &out->suffix) ||
        !bm_query_resolve_artifact_string(model, resolved_id, ctx, scratch, raw_directory, &directory)) {
        nob_sb_free(file_sb);
        return false;
    }
    if (out->output_name.count == 0) out->output_name = target->name;

    nob_sb_append_buf(&file_sb, out->prefix.data ? out->prefix.data : "", out->prefix.count);
    nob_sb_append_buf(&file_sb, out->output_name.data ? out->output_name.data : "", out->output_name.count);
    nob_sb_append_buf(&file_sb, out->suffix.data ? out->suffix.data : "", out->suffix.count);
    file_copy = arena_strndup(scratch, file_sb.items ? file_sb.items : "", file_sb.count);
    nob_sb_free(file_sb);
    if (!file_copy) return false;
    out->file_name = nob_sv_from_cstr(file_copy);
    if (directory.count == 0) {
        out->directory = owner_binary_dir;
    } else if (bm_sv_is_abs_path_query(directory) ||
               bm_query_path_has_prefix(directory, owner_binary_dir) ||
               bm_query_path_has_prefix(directory, owner_source_dir)) {
        out->directory = directory;
    } else {
        out->directory = bm_join_relative_path_query(scratch, owner_binary_dir, directory);
    }
    out->path = bm_join_relative_path_query(scratch, out->directory, out->file_name);
    out->emits = out->path.count > 0;
    return true;
}

static bool bm_query_target_effective_file_internal(const Build_Model *model,
                                                    BM_Target_Id id,
                                                    const BM_Query_Eval_Context *ctx,
                                                    bool linker_file,
                                                    Arena *scratch,
                                                    String_View *out) {
    BM_Target_Artifact_View artifact = {0};
    if (!bm_query_target_effective_artifact(model,
                                            id,
                                            linker_file ? BM_TARGET_ARTIFACT_LINKER : BM_TARGET_ARTIFACT_RUNTIME,
                                            ctx,
                                            scratch,
                                            &artifact)) {
        return false;
    }
    if (out) *out = artifact.path;
    return true;
}

bool bm_query_target_effective_artifact(const Build_Model *model,
                                        BM_Target_Id id,
                                        BM_Target_Artifact_Role role,
                                        const BM_Query_Eval_Context *ctx,
                                        Arena *scratch,
                                        BM_Target_Artifact_View *out) {
    BM_Target_Id resolved_id = bm_resolve_alias_target_id(model, id);
    const BM_Target_Record *target = bm_model_target(model, resolved_id);
    const BM_Imported_Config_Record *config_record = NULL;
    String_View path = {0};
    if (out) *out = (BM_Target_Artifact_View){0};
    if (!scratch || !out || !target) return false;

    if (!target->imported) {
        return bm_query_target_local_artifact_internal(model, resolved_id, role, ctx, scratch, out);
    }

    config_record = bm_select_imported_config_record(target, ctx ? ctx->config : nob_sv_from_cstr(""));
    if (!config_record) return true;

    if (role == BM_TARGET_ARTIFACT_LINKER) {
        if (config_record->effective_linker_file.count > 0) {
            path = config_record->effective_linker_file;
        } else {
            path = config_record->effective_file;
        }
    } else {
        path = config_record->effective_file;
    }

    out->path = path;
    out->directory = bm_query_dirname_sv_local(path);
    out->file_name = bm_query_basename_sv(path);
    out->emits = path.count > 0;
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

bool bm_query_target_imported_known_configurations(const Build_Model *model,
                                                   BM_Target_Id id,
                                                   Arena *scratch,
                                                   BM_String_Span *out) {
    BM_Target_Id resolved_id = bm_resolve_alias_target_id(model, id);
    const BM_Target_Record *target = bm_model_target(model, resolved_id);
    String_View *configs = NULL;
    if (!out) return false;
    *out = (BM_String_Span){0};
    if (!scratch || !target) return false;

    for (size_t i = 0; i < arena_arr_len(target->imported_configs); ++i) {
        if (!bm_query_push_unique_imported_config(scratch, &configs, target->imported_configs[i].config)) return false;
    }
    for (size_t i = 0; i < arena_arr_len(target->imported_config_maps); ++i) {
        if (!bm_query_push_unique_imported_config(scratch, &configs, target->imported_config_maps[i].config)) return false;
        for (size_t j = 0; j < arena_arr_len(target->imported_config_maps[i].mapped_configs); ++j) {
            if (!bm_query_push_unique_imported_config(scratch,
                                                      &configs,
                                                      target->imported_config_maps[i].mapped_configs[j])) {
                return false;
            }
        }
    }
    *out = bm_string_span(configs);
    return true;
}
