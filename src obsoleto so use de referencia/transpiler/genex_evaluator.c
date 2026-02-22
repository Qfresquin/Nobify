#include "genex_evaluator.h"
#include "build_model.h"

#include <ctype.h>
#include <string.h>

static String_View gx_trim(String_View sv) {
    size_t begin = 0;
    while (begin < sv.count && isspace((unsigned char)sv.data[begin])) begin++;
    size_t end = sv.count;
    while (end > begin && isspace((unsigned char)sv.data[end - 1])) end--;
    return nob_sv_from_parts(sv.data + begin, end - begin);
}

static bool gx_sv_eq_ci(String_View a, String_View b) {
    if (a.count != b.count) return false;
    for (size_t i = 0; i < a.count; i++) {
        unsigned char ca = (unsigned char)a.data[i];
        unsigned char cb = (unsigned char)b.data[i];
        if (toupper(ca) != toupper(cb)) return false;
    }
    return true;
}

static bool gx_sv_ends_with_ci(String_View sv, String_View suffix) {
    if (suffix.count > sv.count) return false;
    String_View tail = nob_sv_from_parts(sv.data + (sv.count - suffix.count), suffix.count);
    return gx_sv_eq_ci(tail, suffix);
}

static bool gx_cmake_string_is_false(String_View value) {
    String_View v = gx_trim(value);
    if (v.count == 0) return true;
    if (nob_sv_eq(v, sv_from_cstr("0"))) return true;
    if (gx_sv_eq_ci(v, sv_from_cstr("FALSE"))) return true;
    if (gx_sv_eq_ci(v, sv_from_cstr("OFF"))) return true;
    if (gx_sv_eq_ci(v, sv_from_cstr("NO"))) return true;
    if (gx_sv_eq_ci(v, sv_from_cstr("N"))) return true;
    if (gx_sv_eq_ci(v, sv_from_cstr("IGNORE"))) return true;
    if (gx_sv_eq_ci(v, sv_from_cstr("NOTFOUND"))) return true;
    if (gx_sv_ends_with_ci(v, sv_from_cstr("-NOTFOUND"))) return true;
    return false;
}

static size_t gx_split_genex_args(String_View input, String_View *out, size_t out_cap) {
    size_t count = 0;
    size_t start = 0;
    int genex_depth = 0;

    for (size_t i = 0; i <= input.count; i++) {
        bool is_end = (i == input.count);
        if (!is_end) {
            if (input.data[i] == '$' && (i + 1) < input.count && input.data[i + 1] == '<') {
                genex_depth++;
                i++;
                continue;
            }
            if (input.data[i] == '>' && genex_depth > 0) {
                genex_depth--;
                continue;
            }
        }
        if (!is_end && !(input.data[i] == ',' && genex_depth == 0)) continue;
        if (count < out_cap) {
            out[count] = gx_trim(nob_sv_from_parts(input.data + start, i - start));
        }
        count++;
        start = i + 1;
    }

    return count;
}

static String_View gx_platform_id_for_model(Build_Model *model) {
    if (!model) return sv_from_cstr("");
    if (model->is_windows) return sv_from_cstr("Windows");
    if (model->is_apple) return sv_from_cstr("Darwin");
    if (model->is_linux) return sv_from_cstr("Linux");
    if (model->is_unix) return sv_from_cstr("Unix");
    return sv_from_cstr("");
}

static String_View gx_build_target_property(const Genex_Eval_Context *ctx, Build_Target *target, String_View prop) {
    if (!ctx || !target || prop.count == 0) return sv_from_cstr("");
    if (ctx->get_target_property) {
        return ctx->get_target_property(ctx->userdata, target, prop);
    }
    return build_target_get_property_computed(target, prop, ctx->default_config);
}

String_View genex_evaluate(const Genex_Eval_Context *ctx, String_View content) {
    if (!ctx || !ctx->arena || !ctx->model) return sv_from_cstr("");

    if (nob_sv_eq(content, sv_from_cstr("CONFIG"))) {
        return ctx->default_config;
    }

    if (nob_sv_starts_with(content, sv_from_cstr("CONFIG:"))) {
        String_View cfg_expr = nob_sv_from_parts(content.data + 7, content.count - 7);
        String_View args[16] = {0};
        size_t arg_count = gx_split_genex_args(cfg_expr, args, 16);
        String_View current_cfg = ctx->default_config;
        for (size_t i = 0; i < arg_count && i < 16; i++) {
            String_View entry = args[i];
            size_t item_start = 0;
            for (size_t k = 0; k <= entry.count; k++) {
                bool sep = (k == entry.count) || (entry.data[k] == ';');
                if (!sep) continue;
                String_View candidate = gx_trim(nob_sv_from_parts(entry.data + item_start, k - item_start));
                item_start = k + 1;
                if (candidate.count == 0) continue;
                if (gx_sv_eq_ci(candidate, current_cfg)) {
                    return sv_from_cstr("1");
                }
            }
        }
        return sv_from_cstr("0");
    }

    if (nob_sv_starts_with(content, sv_from_cstr("LOWER_CASE:"))) {
        String_View value = nob_sv_from_parts(content.data + 11, content.count - 11);
        char *lower = arena_strndup(ctx->arena, value.data, value.count);
        if (!lower) return sv_from_cstr("");
        for (size_t i = 0; i < value.count; i++) {
            lower[i] = (char)tolower((unsigned char)lower[i]);
        }
        return sv_from_cstr(lower);
    }

    if (nob_sv_starts_with(content, sv_from_cstr("UPPER_CASE:"))) {
        String_View value = nob_sv_from_parts(content.data + 11, content.count - 11);
        char *upper = arena_strndup(ctx->arena, value.data, value.count);
        if (!upper) return sv_from_cstr("");
        for (size_t i = 0; i < value.count; i++) {
            upper[i] = (char)toupper((unsigned char)upper[i]);
        }
        return sv_from_cstr(upper);
    }

    if (nob_sv_starts_with(content, sv_from_cstr("TARGET_FILE:"))) {
        String_View target_name = nob_sv_from_parts(content.data + 12, content.count - 12);
        Build_Target *target = build_model_find_target(ctx->model, target_name);
        if (!target) return sv_from_cstr("");
        if (target->type == TARGET_INTERFACE_LIB || target->type == TARGET_ALIAS || target->type == TARGET_OBJECT_LIB) {
            return sv_from_cstr("");
        }

        if (target->type == TARGET_IMPORTED) {
            String_View imported_location = gx_build_target_property(ctx, target, sv_from_cstr("IMPORTED_LOCATION"));
            if (imported_location.count > 0) return imported_location;
            return sv_from_cstr("");
        }

        String_View output_name = gx_build_target_property(ctx, target, sv_from_cstr("OUTPUT_NAME"));
        if (output_name.count == 0) output_name = target->name;

        String_View output_dir = gx_build_target_property(ctx, target, sv_from_cstr("OUTPUT_DIRECTORY"));
        if (output_dir.count == 0) output_dir = sv_from_cstr("build");

        if (target->type == TARGET_EXECUTABLE) {
            String_View runtime_dir = gx_build_target_property(ctx, target, sv_from_cstr("RUNTIME_OUTPUT_DIRECTORY"));
            if (runtime_dir.count > 0) output_dir = runtime_dir;
        } else if (target->type == TARGET_STATIC_LIB) {
            String_View archive_dir = gx_build_target_property(ctx, target, sv_from_cstr("ARCHIVE_OUTPUT_DIRECTORY"));
            if (archive_dir.count > 0) output_dir = archive_dir;
        }

        String_View prefix = gx_build_target_property(ctx, target, sv_from_cstr("PREFIX"));
        String_View suffix = gx_build_target_property(ctx, target, sv_from_cstr("SUFFIX"));

        String_Builder sb = {0};
        sb_append_buf(&sb, output_dir.data, output_dir.count);
        if (sb.count > 0 && sb.items[sb.count - 1] != '/' && sb.items[sb.count - 1] != '\\') {
            sb_append(&sb, '/');
        }
        sb_append_buf(&sb, prefix.data, prefix.count);
        sb_append_buf(&sb, output_name.data, output_name.count);
        sb_append_buf(&sb, suffix.data, suffix.count);

        String_View out = sv_from_cstr(arena_strndup(ctx->arena, sb.items ? sb.items : "", sb.count));
        nob_sb_free(sb);
        return out;
    }

    if (nob_sv_starts_with(content, sv_from_cstr("TARGET_OBJECTS:"))) {
        String_View target_name = gx_trim(nob_sv_from_parts(content.data + 15, content.count - 15));
        if (target_name.count == 0) return sv_from_cstr("");
        const char *expr = nob_temp_sprintf("$<TARGET_OBJECTS:%s>", nob_temp_sv_to_cstr(target_name));
        return sv_from_cstr(arena_strndup(ctx->arena, expr, strlen(expr)));
    }

    if (nob_sv_starts_with(content, sv_from_cstr("BOOL:"))) {
        String_View value = nob_sv_from_parts(content.data + 5, content.count - 5);
        return gx_cmake_string_is_false(value) ? sv_from_cstr("0") : sv_from_cstr("1");
    }

    if (nob_sv_starts_with(content, sv_from_cstr("IF:"))) {
        String_View args_expr = nob_sv_from_parts(content.data + 3, content.count - 3);
        String_View args[3] = {0};
        size_t arg_count = gx_split_genex_args(args_expr, args, 3);
        if (arg_count < 3) return sv_from_cstr("");
        bool cond = !gx_cmake_string_is_false(args[0]);
        return cond ? args[1] : args[2];
    }

    if (nob_sv_starts_with(content, sv_from_cstr("TARGET_PROPERTY:"))) {
        String_View args_expr = nob_sv_from_parts(content.data + 16, content.count - 16);
        String_View args[2] = {0};
        size_t arg_count = gx_split_genex_args(args_expr, args, 2);
        if (arg_count < 2 || args[0].count == 0 || args[1].count == 0) return sv_from_cstr("");

        Build_Target *target = build_model_find_target(ctx->model, args[0]);
        if (!target) return sv_from_cstr("");

        return gx_build_target_property(ctx, target, args[1]);
    }

    if (nob_sv_eq(content, sv_from_cstr("PLATFORM_ID"))) {
        return gx_platform_id_for_model(ctx->model);
    }

    if (nob_sv_starts_with(content, sv_from_cstr("PLATFORM_ID:"))) {
        String_View platform = gx_platform_id_for_model(ctx->model);
        String_View args_expr = nob_sv_from_parts(content.data + 12, content.count - 12);
        String_View args[16] = {0};
        size_t arg_count = gx_split_genex_args(args_expr, args, 16);
        for (size_t i = 0; i < arg_count && i < 16; i++) {
            String_View entry = args[i];
            size_t item_start = 0;
            for (size_t k = 0; k <= entry.count; k++) {
                bool sep = (k == entry.count) || (entry.data[k] == ';');
                if (!sep) continue;
                String_View candidate = gx_trim(nob_sv_from_parts(entry.data + item_start, k - item_start));
                item_start = k + 1;
                if (candidate.count == 0) continue;
                if (gx_sv_eq_ci(candidate, platform)) {
                    return sv_from_cstr("1");
                }
            }
        }
        return sv_from_cstr("0");
    }

    return sv_from_cstr("");
}
