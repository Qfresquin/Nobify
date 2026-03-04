#include "eval_ctest.h"

#include "evaluator_internal.h"
#include "sv_utils.h"
#include "tinydir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

typedef struct {
    SV_List positionals;
} Ctest_Parse_Result;

typedef struct {
    const char *const *single_keywords;
    size_t single_count;
    const char *const *multi_keywords;
    size_t multi_count;
    const char *const *flag_keywords;
    size_t flag_count;
    size_t min_positionals;
    size_t max_positionals;
} Ctest_Parse_Spec;

static bool ctest_emit_diag(Evaluator_Context *ctx,
                            const Node *node,
                            Cmake_Diag_Severity severity,
                            String_View cause,
                            String_View hint) {
    return EVAL_DIAG(ctx,
                          severity,
                          nob_sv_from_cstr("eval_ctest"),
                          node->as.cmd.name,
                          eval_origin_from_node(ctx, node),
                          cause,
                          hint);
}

static bool ctest_set_field(Evaluator_Context *ctx,
                            String_View command_name,
                            const char *field_name,
                            String_View value) {
    if (!ctx || !field_name) return false;
    size_t total = sizeof("NOBIFY_CTEST::") - 1 + command_name.count + sizeof("::") - 1 + strlen(field_name);
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
    int n = snprintf(buf,
                     total + 1,
                     "NOBIFY_CTEST::%.*s::%s",
                     (int)command_name.count,
                     command_name.data ? command_name.data : "",
                     field_name);
    if (n < 0) return ctx_oom(ctx);
    return eval_var_set_current(ctx, nob_sv_from_cstr(buf), value);
}

static bool ctest_keyword_in_list(String_View tok, const char *const *items, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (eval_sv_eq_ci_lit(tok, items[i])) return true;
    }
    return false;
}

static bool ctest_is_any_known_keyword(String_View tok, const Ctest_Parse_Spec *spec) {
    return ctest_keyword_in_list(tok, spec->single_keywords, spec->single_count) ||
           ctest_keyword_in_list(tok, spec->multi_keywords, spec->multi_count) ||
           ctest_keyword_in_list(tok, spec->flag_keywords, spec->flag_count);
}

static bool ctest_publish_var_keyword(Evaluator_Context *ctx, String_View key, String_View value) {
    if (value.count == 0) return true;
    if (eval_sv_eq_ci_lit(key, "RETURN_VALUE") ||
        eval_sv_eq_ci_lit(key, "CAPTURE_CMAKE_ERROR") ||
        eval_sv_eq_ci_lit(key, "NUMBER_ERRORS") ||
        eval_sv_eq_ci_lit(key, "NUMBER_WARNINGS") ||
        eval_sv_eq_ci_lit(key, "DEFECT_COUNT")) {
        return eval_var_set_current(ctx, value, nob_sv_from_cstr("0"));
    }
    return true;
}

static bool ctest_parse_generic(Evaluator_Context *ctx,
                                const Node *node,
                                String_View command_name,
                                const SV_List *args,
                                const Ctest_Parse_Spec *spec,
                                Ctest_Parse_Result *out_res) {
    if (!ctx || !node || !args || !spec || !out_res) return false;
    memset(out_res, 0, sizeof(*out_res));

    for (size_t i = 0; i < arena_arr_len(*args); i++) {
        String_View tok = (*args)[i];

        if (ctest_keyword_in_list(tok, spec->flag_keywords, spec->flag_count)) {
            if (!ctest_set_field(ctx, command_name, nob_temp_sv_to_cstr(tok), nob_sv_from_cstr("1"))) return false;
            continue;
        }

        if (ctest_keyword_in_list(tok, spec->single_keywords, spec->single_count)) {
            if (i + 1 >= arena_arr_len(*args)) {
                (void)ctest_emit_diag(ctx,
                                      node,
                                      EV_DIAG_ERROR,
                                      nob_sv_from_cstr("ctest command keyword requires a value"),
                                      tok);
                return false;
            }
            String_View value = (*args)[++i];
            if (!ctest_set_field(ctx, command_name, nob_temp_sv_to_cstr(tok), value)) return false;
            if (!ctest_publish_var_keyword(ctx, tok, value)) return false;
            continue;
        }

        if (ctest_keyword_in_list(tok, spec->multi_keywords, spec->multi_count)) {
            SV_List items = {0};
            size_t j = i + 1;
            for (; j < arena_arr_len(*args); j++) {
                if (ctest_is_any_known_keyword((*args)[j], spec)) break;
                if (!svu_list_push_temp(ctx, &items, (*args)[j])) return false;
            }
            if (arena_arr_len(items) == 0) {
                (void)ctest_emit_diag(ctx,
                                      node,
                                      EV_DIAG_ERROR,
                                      nob_sv_from_cstr("ctest command list keyword requires one or more values"),
                                      tok);
                return false;
            }
            if (!ctest_set_field(ctx, command_name, nob_temp_sv_to_cstr(tok), eval_sv_join_semi_temp(ctx, items, arena_arr_len(items)))) {
                return false;
            }
            i = j - 1;
            continue;
        }

        if (!svu_list_push_temp(ctx, &out_res->positionals, tok)) return false;
    }

    if (arena_arr_len(out_res->positionals) < spec->min_positionals || arena_arr_len(out_res->positionals) > spec->max_positionals) {
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest command received an invalid number of positional arguments"),
                              command_name);
        return false;
    }

    for (size_t i = 0; i < arena_arr_len(out_res->positionals); i++) {
        char field[32];
        snprintf(field, sizeof(field), "POSITIONAL_%zu", i);
        if (!ctest_set_field(ctx, command_name, field, out_res->positionals[i])) return false;
    }

    return true;
}

static String_View ctest_current_binary_dir(Evaluator_Context *ctx) {
    return eval_current_binary_dir(ctx);
}

static String_View ctest_binary_root(Evaluator_Context *ctx) {
    String_View v = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_BINARY_DIR"));
    return v.count > 0 ? v : ctx->binary_dir;
}

static bool ctest_path_is_within(String_View path, String_View root) {
    if (root.count == 0) return false;
    if (path.count < root.count) return false;
    if (memcmp(path.data, root.data, root.count) != 0) return false;
    if (path.count == root.count) return true;
    char next = path.data[root.count];
    return next == '/' || next == '\\';
}

static bool ctest_remove_leaf(const char *path, bool is_dir) {
#if defined(_WIN32)
    if (is_dir) return RemoveDirectoryA(path) != 0;
    return DeleteFileA(path) != 0;
#else
    if (is_dir) return rmdir(path) == 0;
    return unlink(path) == 0;
#endif
}

static bool ctest_remove_tree(const char *path) {
    tinydir_dir dir = {0};
    if (tinydir_open(&dir, path) != 0) return false;

    bool ok = true;
    while (dir.has_next) {
        tinydir_file file = {0};
        if (tinydir_readfile(&dir, &file) != 0) {
            ok = false;
            break;
        }
        if (tinydir_next(&dir) != 0 && dir.has_next) {
            ok = false;
            break;
        }
        if (strcmp(file.name, ".") == 0 || strcmp(file.name, "..") == 0) continue;
        if (file.is_dir) {
            if (!ctest_remove_tree(file.path) || !ctest_remove_leaf(file.path, true)) {
                ok = false;
                break;
            }
        } else if (!ctest_remove_leaf(file.path, false)) {
            ok = false;
            break;
        }
    }
    tinydir_close(&dir);
    return ok;
}

static bool ctest_handle_metadata_only(Evaluator_Context *ctx,
                                       const Node *node,
                                       String_View command_name,
                                       const Ctest_Parse_Spec *spec) {
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    Ctest_Parse_Result parsed = {0};
    if (!ctest_parse_generic(ctx, node, command_name, &args, spec, &parsed)) return !eval_should_stop(ctx);
    return eval_ctest_publish_metadata(ctx, command_name, &args, nob_sv_from_cstr("MODELED"));
}

bool eval_handle_ctest_build(Evaluator_Context *ctx, const Node *node) {
    static const char *const single[] = {
        "BUILD", "CONFIGURATION", "PARALLEL_LEVEL", "FLAGS", "PROJECT_NAME", "TARGET",
        "NUMBER_ERRORS", "NUMBER_WARNINGS", "RETURN_VALUE", "CAPTURE_CMAKE_ERROR"
    };
    static const char *const flags[] = {"APPEND", "QUIET"};
    Ctest_Parse_Spec spec = {single, sizeof(single)/sizeof(single[0]), NULL, 0, flags, sizeof(flags)/sizeof(flags[0]), 0, 0};
    return ctest_handle_metadata_only(ctx, node, nob_sv_from_cstr("ctest_build"), &spec);
}

bool eval_handle_ctest_configure(Evaluator_Context *ctx, const Node *node) {
    static const char *const single[] = {
        "BUILD", "SOURCE", "OPTIONS", "RETURN_VALUE", "CAPTURE_CMAKE_ERROR"
    };
    static const char *const flags[] = {"QUIET"};
    Ctest_Parse_Spec spec = {single, sizeof(single)/sizeof(single[0]), NULL, 0, flags, sizeof(flags)/sizeof(flags[0]), 0, 0};
    return ctest_handle_metadata_only(ctx, node, nob_sv_from_cstr("ctest_configure"), &spec);
}

bool eval_handle_ctest_coverage(Evaluator_Context *ctx, const Node *node) {
    static const char *const single[] = {
        "BUILD", "RETURN_VALUE", "CAPTURE_CMAKE_ERROR"
    };
    static const char *const multi[] = {"LABELS"};
    static const char *const flags[] = {"QUIET"};
    Ctest_Parse_Spec spec = {single, sizeof(single)/sizeof(single[0]), multi, sizeof(multi)/sizeof(multi[0]), flags, sizeof(flags)/sizeof(flags[0]), 0, 0};
    return ctest_handle_metadata_only(ctx, node, nob_sv_from_cstr("ctest_coverage"), &spec);
}

bool eval_handle_ctest_empty_binary_directory(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return false;
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (arena_arr_len(args) != 1) {
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_empty_binary_directory() requires exactly one directory argument"),
                              nob_sv_from_cstr("Usage: ctest_empty_binary_directory(<dir>)"));
        return !eval_should_stop(ctx);
    }

    String_View root = eval_sv_path_normalize_temp(ctx, ctest_binary_root(ctx));
    String_View target = eval_path_resolve_for_cmake_arg(ctx, args[0], ctest_current_binary_dir(ctx), false);
    target = eval_sv_path_normalize_temp(ctx, target);
    if (eval_should_stop(ctx)) return false;

    if (target.count == 0 || root.count == 0 || !ctest_path_is_within(target, root)) {
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_empty_binary_directory() refuses to operate outside CMAKE_BINARY_DIR"),
                              target);
        return !eval_should_stop(ctx);
    }

    char *target_c = eval_sv_to_cstr_temp(ctx, target);
    EVAL_OOM_RETURN_IF_NULL(ctx, target_c, false);

    tinydir_dir dir = {0};
    if (tinydir_open(&dir, target_c) == 0) {
        tinydir_close(&dir);
        if (!ctest_remove_tree(target_c)) {
            (void)ctest_emit_diag(ctx,
                                  node,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("ctest_empty_binary_directory() failed to remove directory contents"),
                                  target);
            return !eval_should_stop(ctx);
        }
        if (!nob_mkdir_if_not_exists(target_c)) {
            (void)ctest_emit_diag(ctx,
                                  node,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("ctest_empty_binary_directory() failed to recreate directory"),
                                  target);
            return !eval_should_stop(ctx);
        }
    }

    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_empty_binary_directory"), "DIRECTORY", target)) return false;
    return eval_ctest_publish_metadata(ctx, nob_sv_from_cstr("ctest_empty_binary_directory"), &args, nob_sv_from_cstr("CLEARED"));
}

bool eval_handle_ctest_memcheck(Evaluator_Context *ctx, const Node *node) {
    static const char *const single[] = {
        "BUILD", "RETURN_VALUE", "CAPTURE_CMAKE_ERROR", "DEFECT_COUNT", "PARALLEL_LEVEL"
    };
    static const char *const flags[] = {"APPEND", "QUIET", "SCHEDULE_RANDOM"};
    Ctest_Parse_Spec spec = {single, sizeof(single)/sizeof(single[0]), NULL, 0, flags, sizeof(flags)/sizeof(flags[0]), 0, 0};
    return ctest_handle_metadata_only(ctx, node, nob_sv_from_cstr("ctest_memcheck"), &spec);
}

bool eval_handle_ctest_read_custom_files(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return false;
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (arena_arr_len(args) == 0) {
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_read_custom_files() requires one or more directories"),
                              nob_sv_from_cstr("Usage: ctest_read_custom_files(<dir>...)"));
        return !eval_should_stop(ctx);
    }

    SV_List loaded = {0};
    String_View base_dir = eval_current_source_dir_for_paths(ctx);
    for (size_t i = 0; i < arena_arr_len(args); i++) {
        String_View dir = eval_path_resolve_for_cmake_arg(ctx, args[i], base_dir, false);
        String_View custom = eval_sv_path_join(eval_temp_arena(ctx), dir, nob_sv_from_cstr("CTestCustom.cmake"));
        if (eval_should_stop(ctx)) return false;

        tinydir_file probe = {0};
        char *custom_c = eval_sv_to_cstr_temp(ctx, custom);
        EVAL_OOM_RETURN_IF_NULL(ctx, custom_c, false);
        if (tinydir_file_open(&probe, custom_c) == 0) {
            if (!eval_execute_file(ctx, custom, false, nob_sv_from_cstr(""))) return !eval_should_stop(ctx);
            if (!svu_list_push_temp(ctx, &loaded, custom)) return false;
        }
    }

    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_read_custom_files"),
                         "LOADED_FILES",
                         eval_sv_join_semi_temp(ctx, loaded, arena_arr_len(loaded)))) {
        return false;
    }
    return eval_ctest_publish_metadata(ctx, nob_sv_from_cstr("ctest_read_custom_files"), &args, nob_sv_from_cstr("MODELED"));
}

static bool ctest_parse_double(String_View sv, double *out_value) {
    if (!out_value) return false;
    if (sv.count == 0) return false;
    char buf[128];
    if (sv.count >= sizeof(buf)) return false;
    memcpy(buf, sv.data, sv.count);
    buf[sv.count] = '\0';
    char *end = NULL;
    double v = strtod(buf, &end);
    if (!end || *end != '\0') return false;
    *out_value = v;
    return true;
}

bool eval_handle_ctest_run_script(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return false;
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);

    bool new_process = false;
    String_View return_var = nob_sv_from_cstr("");
    SV_List scripts = {0};

    for (size_t i = 0; i < arena_arr_len(args); i++) {
        if (eval_sv_eq_ci_lit(args[i], "NEW_PROCESS")) {
            new_process = true;
            continue;
        }
        if (eval_sv_eq_ci_lit(args[i], "RETURN_VALUE")) {
            if (i + 1 >= arena_arr_len(args)) {
                (void)ctest_emit_diag(ctx,
                                      node,
                                      EV_DIAG_ERROR,
                                      nob_sv_from_cstr("ctest_run_script(RETURN_VALUE ...) requires a variable"),
                                      nob_sv_from_cstr("Usage: ctest_run_script(... RETURN_VALUE <var>)"));
                return !eval_should_stop(ctx);
            }
            return_var = args[++i];
            continue;
        }
        if (!svu_list_push_temp(ctx, &scripts, args[i])) return false;
    }

    if (new_process) {
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_run_script(NEW_PROCESS ...) is not implemented in evaluator v2"),
                              nob_sv_from_cstr("This batch supports only in-process script evaluation"));
        return !eval_should_stop(ctx);
    }

    if (arena_arr_len(scripts) == 0) {
        String_View current = eval_current_list_file(ctx);
        if (current.count == 0) {
            (void)ctest_emit_diag(ctx,
                                  node,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("ctest_run_script() needs a script when no current list file is available"),
                                  nob_sv_from_cstr("Usage: ctest_run_script(<script>...)"));
            return !eval_should_stop(ctx);
        }
        if (!svu_list_push_temp(ctx, &scripts, current)) return false;
    }

    String_View base_dir = eval_current_source_dir_for_paths(ctx);
    const char *rv_text = "0";
    for (size_t i = 0; i < arena_arr_len(scripts); i++) {
        String_View script = eval_path_resolve_for_cmake_arg(ctx, scripts[i], base_dir, false);
        if (!eval_execute_file(ctx, script, false, nob_sv_from_cstr(""))) {
            rv_text = "1";
            break;
        }
    }

    if (return_var.count > 0 && !eval_var_set_current(ctx, return_var, nob_sv_from_cstr(rv_text))) return false;
    if (!ctest_set_field(ctx,
                         nob_sv_from_cstr("ctest_run_script"),
                         "SCRIPTS",
                         eval_sv_join_semi_temp(ctx, scripts, arena_arr_len(scripts)))) {
        return false;
    }
    return eval_ctest_publish_metadata(ctx, nob_sv_from_cstr("ctest_run_script"), &args, nob_sv_from_cstr("MODELED"));
}

bool eval_handle_ctest_sleep(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return false;
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return !eval_should_stop(ctx);
    if (!(arena_arr_len(args) == 1 || arena_arr_len(args) == 3)) {
        (void)ctest_emit_diag(ctx,
                              node,
                              EV_DIAG_ERROR,
                              nob_sv_from_cstr("ctest_sleep() expects one or three numeric arguments"),
                              nob_sv_from_cstr("Usage: ctest_sleep(<seconds>) or ctest_sleep(<time1> <duration> <time2>)"));
        return !eval_should_stop(ctx);
    }

    double values[3] = {0};
    for (size_t i = 0; i < arena_arr_len(args); i++) {
        if (!ctest_parse_double(args[i], &values[i])) {
            (void)ctest_emit_diag(ctx,
                                  node,
                                  EV_DIAG_ERROR,
                                  nob_sv_from_cstr("ctest_sleep() requires numeric arguments"),
                                  args[i]);
            return !eval_should_stop(ctx);
        }
    }

    String_View duration = nob_sv_from_cstr("0");
    if (arena_arr_len(args) == 1) {
        duration = args[0];
    } else {
        double computed = values[0] + values[1] - values[2];
        if (computed < 0.0) computed = 0.0;
        char buf[64];
        int n = snprintf(buf, sizeof(buf), "%.3f", computed);
        if (n < 0 || (size_t)n >= sizeof(buf)) return ctx_oom(ctx);
        duration = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(buf, (size_t)n));
    }

    if (!ctest_set_field(ctx, nob_sv_from_cstr("ctest_sleep"), "DURATION", duration)) return false;
    return eval_ctest_publish_metadata(ctx, nob_sv_from_cstr("ctest_sleep"), &args, nob_sv_from_cstr("NOOP"));
}

bool eval_handle_ctest_start(Evaluator_Context *ctx, const Node *node) {
    static const char *const single[] = {"TRACK"};
    static const char *const flags[] = {"APPEND"};
    Ctest_Parse_Spec spec = {single, sizeof(single)/sizeof(single[0]), NULL, 0, flags, sizeof(flags)/sizeof(flags[0]), 1, 3};
    return ctest_handle_metadata_only(ctx, node, nob_sv_from_cstr("ctest_start"), &spec);
}

bool eval_handle_ctest_submit(Evaluator_Context *ctx, const Node *node) {
    static const char *const single[] = {"RETRY_COUNT", "RETRY_DELAY", "RETURN_VALUE", "CAPTURE_CMAKE_ERROR"};
    static const char *const multi[] = {"PARTS", "FILES"};
    static const char *const flags[] = {"QUIET"};
    Ctest_Parse_Spec spec = {single, sizeof(single)/sizeof(single[0]), multi, sizeof(multi)/sizeof(multi[0]), flags, sizeof(flags)/sizeof(flags[0]), 0, 0};
    return ctest_handle_metadata_only(ctx, node, nob_sv_from_cstr("ctest_submit"), &spec);
}

bool eval_handle_ctest_test(Evaluator_Context *ctx, const Node *node) {
    static const char *const single[] = {
        "BUILD", "RETURN_VALUE", "CAPTURE_CMAKE_ERROR", "PARALLEL_LEVEL",
        "STOP_TIME", "TEST_LOAD", "OUTPUT_JUNIT"
    };
    static const char *const flags[] = {"APPEND", "QUIET", "SCHEDULE_RANDOM"};
    Ctest_Parse_Spec spec = {single, sizeof(single)/sizeof(single[0]), NULL, 0, flags, sizeof(flags)/sizeof(flags[0]), 0, 0};
    return ctest_handle_metadata_only(ctx, node, nob_sv_from_cstr("ctest_test"), &spec);
}

bool eval_handle_ctest_update(Evaluator_Context *ctx, const Node *node) {
    static const char *const single[] = {"SOURCE", "RETURN_VALUE", "CAPTURE_CMAKE_ERROR"};
    static const char *const flags[] = {"QUIET"};
    Ctest_Parse_Spec spec = {single, sizeof(single)/sizeof(single[0]), NULL, 0, flags, sizeof(flags)/sizeof(flags[0]), 0, 0};
    return ctest_handle_metadata_only(ctx, node, nob_sv_from_cstr("ctest_update"), &spec);
}

bool eval_handle_ctest_upload(Evaluator_Context *ctx, const Node *node) {
    static const char *const single[] = {"CAPTURE_CMAKE_ERROR"};
    static const char *const multi[] = {"FILES"};
    Ctest_Parse_Spec spec = {single, sizeof(single)/sizeof(single[0]), multi, sizeof(multi)/sizeof(multi[0]), NULL, 0, 0, 0};
    return ctest_handle_metadata_only(ctx, node, nob_sv_from_cstr("ctest_upload"), &spec);
}
