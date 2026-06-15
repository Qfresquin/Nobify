#include "evaluator_internal.h"
#include "arena_dyn.h"
#include "eval_file_backend_archive.h"
#include "eval_file_backend_curl.h"
#include "sv_utils.h"
#include "stb_ds.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <direct.h>
#include <sys/utime.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <utime.h>
#include <unistd.h>
#include <dirent.h>
#include <glob.h>
#if defined(__linux__)
#include <sys/sysinfo.h>
#endif
#if defined(__unix__) || defined(__APPLE__) || defined(__linux__)
#include <sys/utsname.h>
#endif
#endif

bool eval_file_glob_match_sv(String_View pat, String_View str, bool ci) {
    size_t pi = 0, si = 0;
    size_t star_pi = (size_t)-1, star_si = (size_t)-1;

    while (si < str.count) {
        if (pi < pat.count) {
            char pc = pat.data[pi];
            char sc = str.data[si];

            if (pc == '*') {
                star_pi = pi++;
                star_si = si;
                continue;
            }

            if (pc == '?') {
                if (!svu_is_path_sep(sc)) {
                    pi++;
                    si++;
                    continue;
                }
            }

            if (pc == '[') {
                if (!svu_is_path_sep(sc)) {
                    size_t j = pi + 1;
                    while (j < pat.count && pat.data[j] != ']') j++;

                    if (j < pat.count) {
                        bool neg = false;
                        size_t k = pi + 1;
                        if (k < j && (pat.data[k] == '!' || pat.data[k] == '^')) {
                            neg = true;
                            k++;
                        }

                        bool matched = false;
                        char sc_cmp = ci ? (char)tolower((unsigned char)sc) : sc;

                        while (k < j) {
                            char a = pat.data[k];
                            char a_cmp = ci ? (char)tolower((unsigned char)a) : a;

                            if (k + 2 < j && pat.data[k + 1] == '-') {
                                char b = pat.data[k + 2];
                                char b_cmp = ci ? (char)tolower((unsigned char)b) : b;
                                char lo = a_cmp < b_cmp ? a_cmp : b_cmp;
                                char hi = a_cmp < b_cmp ? b_cmp : a_cmp;
                                if (sc_cmp >= lo && sc_cmp <= hi) matched = true;
                                k += 3;
                            } else {
                                if (sc_cmp == a_cmp) matched = true;
                                k++;
                            }
                        }

                        if (neg) matched = !matched;
                        if (matched) {
                            pi = j + 1;
                            si++;
                            continue;
                        }
                    }
                }
            }

            char a = ci ? (char)tolower((unsigned char)pc) : pc;
            char b = ci ? (char)tolower((unsigned char)sc) : sc;
            if (a == b) {
                pi++;
                si++;
                continue;
            }
        }

        if (star_pi != (size_t)-1) {
            if (star_si < str.count && svu_is_path_sep(str.data[star_si])) {
                return false;
            } else {
                pi = star_pi + 1;
                si = ++star_si;
                continue;
            }
        }
    }

    while (pi < pat.count && pat.data[pi] == '*') pi++;
    return pi == pat.count;
}

String_View sv_copy_to_arena(Arena *arena, String_View sv) {
    if (!arena) return nob_sv_from_cstr("");
    if (sv.count == 0 || sv.data == NULL) return nob_sv_from_cstr("");
    char *dup = (char*)arena_alloc(arena, sv.count + 1);
    if (!dup) return nob_sv_from_cstr("");
    memcpy(dup, sv.data, sv.count);
    dup[sv.count] = '\0';
    return nob_sv_from_parts(dup, sv.count);
}

char *eval_sv_to_cstr_temp(EvalExecContext *ctx, String_View sv) {
    if (!ctx) return NULL;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), sv.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, NULL);
    if (sv.count) memcpy(buf, sv.data, sv.count);
    buf[sv.count] = '\0';
    return buf;
}

bool eval_emit_event(EvalExecContext *ctx, Event ev) {
    return eval_command_tx_push_event(ctx, &ev, false);
}

bool eval_emit_event_allow_stopped(EvalExecContext *ctx, Event ev) {
    return eval_command_tx_push_event(ctx, &ev, true);
}

bool eval_sv_key_eq(String_View a, String_View b) {
    if (a.count != b.count) return false;
    if (a.count == 0) return true;
    return memcmp(a.data, b.data, a.count) == 0;
}

bool eval_sv_eq_ci_lit(String_View a, const char *lit) {
    String_View b = nob_sv_from_cstr(lit);
    if (a.count != b.count) return false;
    for (size_t i = 0; i < a.count; i++) {
        if (toupper((unsigned char)a.data[i]) != toupper((unsigned char)b.data[i])) return false;
    }
    return true;
}

String_View eval_normalize_compile_definition_item(String_View item) {
    if (item.count >= 2 && item.data && item.data[0] == '-' && (item.data[1] == 'D' || item.data[1] == 'd')) {
        return nob_sv_from_parts(item.data + 2, item.count - 2);
    }
    return item;
}

String_View eval_current_source_dir_for_paths(EvalExecContext *ctx) {
    return eval_current_source_dir(ctx);
}

static Eval_Directory_Node *eval_directory_find_node(EvalExecContext *ctx, String_View source_dir) {
    if (!ctx || source_dir.count == 0) return NULL;
    Eval_Directory_Graph *graph = &ctx->semantic_state.directories;
    for (size_t i = 0; i < arena_arr_len(graph->nodes); i++) {
        if (svu_eq_ci_sv(graph->nodes[i].source_dir, source_dir)) return &graph->nodes[i];
    }
    return NULL;
}

static const Eval_Directory_Node *eval_directory_find_node_const(const EvalExecContext *ctx, String_View source_dir) {
    if (!ctx || source_dir.count == 0) return NULL;
    const Eval_Directory_Graph *graph = &ctx->semantic_state.directories;
    for (size_t i = 0; i < arena_arr_len(graph->nodes); i++) {
        if (svu_eq_ci_sv(graph->nodes[i].source_dir, source_dir)) return &graph->nodes[i];
    }
    return NULL;
}

static const Eval_Directory_Node *eval_directory_find_node_by_binary_dir_const(const EvalExecContext *ctx,
                                                                               String_View binary_dir) {
    if (!ctx || binary_dir.count == 0) return NULL;
    const Eval_Directory_Graph *graph = &ctx->semantic_state.directories;
    for (size_t i = 0; i < arena_arr_len(graph->nodes); i++) {
        const Eval_Directory_Node *node = &graph->nodes[i];
        if (node->binary_dir.count == 0) continue;
        if (svu_eq_ci_sv(node->binary_dir, binary_dir)) return node;
    }
    return NULL;
}

static bool eval_directory_list_append_unique(EvalExecContext *ctx, SV_List *list, String_View value) {
    if (!ctx || !list || value.count == 0) return false;
    for (size_t i = 0; i < arena_arr_len(*list); i++) {
        if (eval_sv_key_eq((*list)[i], value)) return true;
    }
    value = sv_copy_to_event_arena(ctx, value);
    if (eval_should_stop(ctx)) return false;
    return EVAL_ARR_PUSH(ctx, ctx->event_arena, *list, value);
}

static bool eval_directory_binding_upsert(EvalExecContext *ctx,
                                          Var_Binding **bindings,
                                          String_View key,
                                          String_View value) {
    if (!ctx || !bindings || key.count == 0) return false;
    for (size_t i = 0; i < arena_arr_len(*bindings); i++) {
        if (!eval_sv_key_eq((*bindings)[i].key, key)) continue;
        (*bindings)[i].value = sv_copy_to_event_arena(ctx, value);
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    Var_Binding binding = {0};
    binding.key = sv_copy_to_event_arena(ctx, key);
    binding.value = sv_copy_to_event_arena(ctx, value);
    if (eval_should_stop(ctx)) return false;
    return EVAL_ARR_PUSH(ctx, ctx->event_arena, *bindings, binding);
}

bool eval_directory_register_node(EvalExecContext *ctx,
                                  String_View source_dir,
                                  String_View binary_dir,
                                  String_View parent_source_dir,
                                  String_View parent_binary_dir) {
    if (!ctx || source_dir.count == 0) return false;

    String_View normalized_source = eval_sv_path_normalize_temp(ctx, source_dir);
    if (eval_should_stop(ctx)) return false;
    if (normalized_source.count == 0) return false;
    String_View normalized_binary =
        binary_dir.count > 0 ? eval_sv_path_normalize_temp(ctx, binary_dir) : nob_sv_from_cstr("");
    if (eval_should_stop(ctx)) return false;
    String_View normalized_parent_source =
        parent_source_dir.count > 0 ? eval_sv_path_normalize_temp(ctx, parent_source_dir) : nob_sv_from_cstr("");
    if (eval_should_stop(ctx)) return false;
    String_View normalized_parent_binary =
        parent_binary_dir.count > 0 ? eval_sv_path_normalize_temp(ctx, parent_binary_dir) : nob_sv_from_cstr("");
    if (eval_should_stop(ctx)) return false;

    Eval_Directory_Node *existing = eval_directory_find_node(ctx, normalized_source);
    if (existing) {
        if (existing->binary_dir.count == 0 && normalized_binary.count > 0) {
            existing->binary_dir = sv_copy_to_event_arena(ctx, normalized_binary);
            if (eval_should_stop(ctx)) return false;
        }
        if (existing->parent_source_dir.count == 0 && normalized_parent_source.count > 0) {
            existing->parent_source_dir = sv_copy_to_event_arena(ctx, normalized_parent_source);
            if (eval_should_stop(ctx)) return false;
        }
        if (existing->parent_binary_dir.count == 0 && normalized_parent_binary.count > 0) {
            existing->parent_binary_dir = sv_copy_to_event_arena(ctx, normalized_parent_binary);
            if (eval_should_stop(ctx)) return false;
        }
        return true;
    }

    Eval_Directory_Node node = {0};
    node.source_dir = sv_copy_to_event_arena(ctx, normalized_source);
    node.binary_dir = sv_copy_to_event_arena(ctx, normalized_binary);
    node.parent_source_dir = sv_copy_to_event_arena(ctx, normalized_parent_source);
    node.parent_binary_dir = sv_copy_to_event_arena(ctx, normalized_parent_binary);
    if (eval_should_stop(ctx)) return false;
    return EVAL_ARR_PUSH(ctx, ctx->event_arena, ctx->semantic_state.directories.nodes, node);
}

bool eval_directory_register_known(EvalExecContext *ctx, String_View dir) {
    if (!ctx || dir.count == 0) return false;

    String_View source_dir = eval_sv_path_normalize_temp(ctx, dir);
    if (eval_should_stop(ctx)) return false;
    if (source_dir.count == 0) return false;

    String_View current_source = eval_current_source_dir(ctx);
    String_View current_binary = eval_current_binary_dir(ctx);
    String_View binary_dir = nob_sv_from_cstr("");
    String_View parent_source_dir = nob_sv_from_cstr("");
    String_View parent_binary_dir = nob_sv_from_cstr("");

    if (current_source.count > 0) {
        current_source = eval_sv_path_normalize_temp(ctx, current_source);
        if (eval_should_stop(ctx)) return false;
    }
    if (current_binary.count > 0) {
        current_binary = eval_sv_path_normalize_temp(ctx, current_binary);
        if (eval_should_stop(ctx)) return false;
    }

    if (svu_eq_ci_sv(source_dir, current_source)) {
        binary_dir = current_binary;
        const Eval_Exec_Context *current = eval_exec_current_const(ctx);
        if (current) {
            size_t depth = arena_arr_len(ctx->exec_contexts);
            if (depth >= 2) {
                const Eval_Exec_Context *parent = &ctx->exec_contexts[depth - 2];
                if (parent->source_dir.count > 0) {
                    parent_source_dir = eval_sv_path_normalize_temp(ctx, parent->source_dir);
                    if (eval_should_stop(ctx)) return false;
                }
                if (parent->binary_dir.count > 0) {
                    parent_binary_dir = eval_sv_path_normalize_temp(ctx, parent->binary_dir);
                    if (eval_should_stop(ctx)) return false;
                }
            }
        }
    }

    return eval_directory_register_node(ctx, source_dir, binary_dir, parent_source_dir, parent_binary_dir);
}

bool eval_directory_is_known(EvalExecContext *ctx, String_View dir) {
    if (!ctx || dir.count == 0) return false;

    String_View normalized = eval_sv_path_normalize_temp(ctx, dir);
    if (eval_should_stop(ctx)) return false;
    if (normalized.count == 0) return false;

    return eval_directory_find_node_const(ctx, normalized) != NULL;
}

String_View eval_directory_known_source_dir_temp(EvalExecContext *ctx, String_View dir) {
    if (!ctx || dir.count == 0) return nob_sv_from_cstr("");

    String_View normalized = eval_sv_path_normalize_temp(ctx, dir);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");
    if (normalized.count == 0) return nob_sv_from_cstr("");

    const Eval_Directory_Node *node = eval_directory_find_node_const(ctx, normalized);
    if (node) return node->source_dir;

    node = eval_directory_find_node_by_binary_dir_const(ctx, normalized);
    if (node) return node->source_dir;

    return nob_sv_from_cstr("");
}

bool eval_directory_parent(EvalExecContext *ctx, String_View source_dir, String_View *out_parent_source_dir) {
    if (out_parent_source_dir) *out_parent_source_dir = nob_sv_from_cstr("");
    if (!ctx || !out_parent_source_dir || source_dir.count == 0) return false;
    String_View normalized = eval_sv_path_normalize_temp(ctx, source_dir);
    if (eval_should_stop(ctx)) return false;
    const Eval_Directory_Node *node = eval_directory_find_node_const(ctx, normalized);
    if (!node) return true;
    *out_parent_source_dir = node->parent_source_dir;
    return true;
}

bool eval_directory_binary_dir(EvalExecContext *ctx, String_View source_dir, String_View *out_binary_dir) {
    if (out_binary_dir) *out_binary_dir = nob_sv_from_cstr("");
    if (!ctx || !out_binary_dir || source_dir.count == 0) return false;
    String_View normalized = eval_sv_path_normalize_temp(ctx, source_dir);
    if (eval_should_stop(ctx)) return false;
    const Eval_Directory_Node *node = eval_directory_find_node_const(ctx, normalized);
    if (!node) return true;
    *out_binary_dir = node->binary_dir;
    return true;
}

bool eval_directory_note_target(EvalExecContext *ctx, String_View source_dir, String_View target_name) {
    if (!ctx || source_dir.count == 0 || target_name.count == 0) return false;
    if (!eval_directory_register_known(ctx, source_dir)) return false;
    String_View normalized = eval_sv_path_normalize_temp(ctx, source_dir);
    if (eval_should_stop(ctx)) return false;
    Eval_Directory_Node *node = eval_directory_find_node(ctx, normalized);
    if (!node) return false;
    return eval_directory_list_append_unique(ctx, &node->declared_targets, target_name);
}

bool eval_directory_note_test(EvalExecContext *ctx, String_View source_dir, String_View test_name) {
    if (!ctx || source_dir.count == 0 || test_name.count == 0) return false;
    if (!eval_directory_register_known(ctx, source_dir)) return false;
    String_View normalized = eval_sv_path_normalize_temp(ctx, source_dir);
    if (eval_should_stop(ctx)) return false;
    Eval_Directory_Node *node = eval_directory_find_node(ctx, normalized);
    if (!node) return false;
    return eval_directory_list_append_unique(ctx, &node->declared_tests, test_name);
}

bool eval_directory_capture_current_scope(EvalExecContext *ctx) {
    if (!ctx) return false;

    String_View current_source = eval_current_source_dir_for_paths(ctx);
    if (current_source.count == 0) return true;
    if (!eval_directory_register_known(ctx, current_source)) return false;

    String_View normalized = eval_sv_path_normalize_temp(ctx, current_source);
    if (eval_should_stop(ctx)) return false;
    Eval_Directory_Node *node = eval_directory_find_node(ctx, normalized);
    if (!node) return false;

    Var_Binding *definitions = NULL;
    Eval_Scope_State *scope_state = eval_scope_slice(ctx);
    for (size_t depth = 0; depth < eval_scope_visible_depth(ctx); depth++) {
        Var_Scope *scope = &scope_state->scopes[depth];
        ptrdiff_t n = stbds_shlen(scope->vars);
        for (ptrdiff_t i = 0; i < n; i++) {
            if (!scope->vars[i].key) continue;
            if (!eval_directory_binding_upsert(ctx,
                                               &definitions,
                                               nob_sv_from_cstr(scope->vars[i].key),
                                               scope->vars[i].value)) {
                return false;
            }
        }
    }

    SV_List macro_names = NULL;
    Eval_Command_State *commands = eval_command_slice(ctx);
    for (size_t i = 0; i < arena_arr_len(commands->user_commands); i++) {
        if (commands->user_commands[i].kind != USER_CMD_MACRO) continue;
        if (!eval_directory_list_append_unique(ctx, &macro_names, commands->user_commands[i].name)) {
            return false;
        }
    }

    SV_List listfile_stack = NULL;
    for (size_t i = 0; i < arena_arr_len(ctx->exec_contexts); i++) {
        const Eval_Exec_Context *exec = &ctx->exec_contexts[i];
        if (!exec->current_file) continue;
        if (!eval_directory_list_append_unique(ctx, &listfile_stack, nob_sv_from_cstr(exec->current_file))) {
            return false;
        }
    }
    if (arena_arr_len(listfile_stack) == 0 && ctx->current_file) {
        if (!eval_directory_list_append_unique(ctx, &listfile_stack, nob_sv_from_cstr(ctx->current_file))) {
            return false;
        }
    }

    node->definition_bindings = definitions;
    node->macro_names = macro_names;
    node->listfile_stack = listfile_stack;
    return true;
}

String_View eval_detect_host_system_name(void) {
#if defined(_WIN32)
    return nob_sv_from_cstr("Windows");
#elif defined(__APPLE__)
    return nob_sv_from_cstr("Darwin");
#elif defined(__linux__)
    return nob_sv_from_cstr("Linux");
#elif defined(__unix__)
    return nob_sv_from_cstr("Unix");
#else
    return nob_sv_from_cstr("Unknown");
#endif
}

String_View eval_detect_host_processor(void) {
#if defined(__x86_64__) || defined(_M_X64)
    return nob_sv_from_cstr("x86_64");
#elif defined(__aarch64__) || defined(_M_ARM64)
    return nob_sv_from_cstr("aarch64");
#elif defined(__i386__) || defined(_M_IX86)
    return nob_sv_from_cstr("x86");
#elif defined(__arm__) || defined(_M_ARM)
    return nob_sv_from_cstr("arm");
#else
    return nob_sv_from_cstr("unknown");
#endif
}

#if defined(_WIN32)
static String_View host_copy_printf_temp(EvalExecContext *ctx, const char *fmt, ...) {
    if (!ctx || !fmt) return nob_sv_from_cstr("");

    va_list ap;
    va_start(ap, fmt);
    va_list ap_copy;
    va_copy(ap_copy, ap);
    int needed = vsnprintf(NULL, 0, fmt, ap_copy);
    va_end(ap_copy);
    if (needed < 0) {
        va_end(ap);
        return nob_sv_from_cstr("");
    }

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), (size_t)needed + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    (void)vsnprintf(buf, (size_t)needed + 1, fmt, ap);
    va_end(ap);
    return nob_sv_from_parts(buf, (size_t)needed);
}
#endif

bool eval_host_hostname_temp(EvalExecContext *ctx, String_View *out_hostname) {
    if (!out_hostname) return false;
    *out_hostname = nob_sv_from_cstr("");

#if defined(_WIN32)
    char buf[256] = {0};
    DWORD size = (DWORD)(sizeof(buf) - 1);
    if (!GetComputerNameA(buf, &size)) return true;
    *out_hostname = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(buf, (size_t)size));
    if (eval_should_stop(ctx)) return false;
    return true;
#else
    char buf[256] = {0};
    if (gethostname(buf, sizeof(buf) - 1) != 0) return true;
    buf[sizeof(buf) - 1] = '\0';
    *out_hostname = sv_copy_to_temp_arena(ctx, nob_sv_from_cstr(buf));
    if (eval_should_stop(ctx)) return false;
    return true;
#endif
}

bool eval_host_logical_cores(size_t *out_count) {
    if (!out_count) return false;
    int raw = nob_nprocs();
    if (raw <= 0) return false;
    *out_count = (size_t)raw;
    return true;
}

bool eval_host_memory_info(Eval_Host_Memory_Info *out_info) {
    if (!out_info) return false;
    memset(out_info, 0, sizeof(*out_info));

#if defined(_WIN32)
    MEMORYSTATUSEX status = {0};
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status)) return false;

    const unsigned long long mib = 1024ull * 1024ull;
    out_info->total_virtual_mib = status.ullTotalPageFile / mib;
    out_info->available_virtual_mib = status.ullAvailPageFile / mib;
    out_info->total_physical_mib = status.ullTotalPhys / mib;
    out_info->available_physical_mib = status.ullAvailPhys / mib;
    return true;
#elif defined(__linux__)
    struct sysinfo info = {0};
    if (sysinfo(&info) != 0) return false;

    unsigned long long unit = info.mem_unit > 0 ? (unsigned long long)info.mem_unit : 1ull;
    unsigned long long total_phys = (unsigned long long)info.totalram * unit;
    unsigned long long avail_phys = (unsigned long long)info.freeram * unit;
    unsigned long long total_swap = (unsigned long long)info.totalswap * unit;
    unsigned long long avail_swap = (unsigned long long)info.freeswap * unit;
    const unsigned long long mib = 1024ull * 1024ull;

    out_info->total_physical_mib = total_phys / mib;
    out_info->available_physical_mib = avail_phys / mib;
    out_info->total_virtual_mib = (total_phys + total_swap) / mib;
    out_info->available_virtual_mib = (avail_phys + avail_swap) / mib;
    return true;
#else
    return false;
#endif
}

String_View eval_host_os_release_temp(EvalExecContext *ctx) {
#if defined(_WIN32)
    OSVERSIONINFOA info = {0};
    info.dwOSVersionInfoSize = sizeof(info);
    if (!GetVersionExA(&info)) return nob_sv_from_cstr("");
    return host_copy_printf_temp(ctx, "%lu.%lu", (unsigned long)info.dwMajorVersion, (unsigned long)info.dwMinorVersion);
#elif defined(__unix__) || defined(__APPLE__) || defined(__linux__)
    struct utsname info = {0};
    if (uname(&info) != 0) return nob_sv_from_cstr("");
    return sv_copy_to_temp_arena(ctx, nob_sv_from_cstr(info.release));
#else
    return nob_sv_from_cstr("");
#endif
}

String_View eval_host_os_version_temp(EvalExecContext *ctx) {
#if defined(_WIN32)
    OSVERSIONINFOA info = {0};
    info.dwOSVersionInfoSize = sizeof(info);
    if (!GetVersionExA(&info)) return nob_sv_from_cstr("");
    return host_copy_printf_temp(ctx,
                                 "%lu.%lu.%lu",
                                 (unsigned long)info.dwMajorVersion,
                                 (unsigned long)info.dwMinorVersion,
                                 (unsigned long)info.dwBuildNumber);
#elif defined(__unix__) || defined(__APPLE__) || defined(__linux__)
    struct utsname info = {0};
    if (uname(&info) != 0) return nob_sv_from_cstr("");
    return sv_copy_to_temp_arena(ctx, nob_sv_from_cstr(info.version));
#else
    return nob_sv_from_cstr("");
#endif
}

String_View eval_property_upper_name_temp(EvalExecContext *ctx, String_View name) {
    if (!ctx) return nob_sv_from_cstr("");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), name.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    for (size_t i = 0; i < name.count; i++) {
        buf[i] = (char)toupper((unsigned char)name.data[i]);
    }
    buf[name.count] = '\0';
    return nob_sv_from_cstr(buf);
}

bool eval_property_scope_upper_temp(EvalExecContext *ctx, String_View raw_scope, String_View *out_scope_upper) {
    (void)ctx;
    if (!out_scope_upper) return false;
    *out_scope_upper = nob_sv_from_cstr("");

    if (eval_sv_eq_ci_lit(raw_scope, "GLOBAL")) *out_scope_upper = nob_sv_from_cstr("GLOBAL");
    else if (eval_sv_eq_ci_lit(raw_scope, "DIRECTORY")) *out_scope_upper = nob_sv_from_cstr("DIRECTORY");
    else if (eval_sv_eq_ci_lit(raw_scope, "TARGET")) *out_scope_upper = nob_sv_from_cstr("TARGET");
    else if (eval_sv_eq_ci_lit(raw_scope, "SOURCE")) *out_scope_upper = nob_sv_from_cstr("SOURCE");
    else if (eval_sv_eq_ci_lit(raw_scope, "INSTALL")) *out_scope_upper = nob_sv_from_cstr("INSTALL");
    else if (eval_sv_eq_ci_lit(raw_scope, "TEST")) *out_scope_upper = nob_sv_from_cstr("TEST");
    else if (eval_sv_eq_ci_lit(raw_scope, "VARIABLE")) *out_scope_upper = nob_sv_from_cstr("VARIABLE");
    else if (eval_sv_eq_ci_lit(raw_scope, "CACHE")) *out_scope_upper = nob_sv_from_cstr("CACHE");
    else if (eval_sv_eq_ci_lit(raw_scope, "CACHED_VARIABLE")) *out_scope_upper = nob_sv_from_cstr("CACHED_VARIABLE");

    return out_scope_upper->count > 0;
}

String_View eval_property_scoped_object_id_temp(EvalExecContext *ctx,
                                                const char *prefix,
                                                String_View scope_object,
                                                String_View item_object) {
    if (!ctx || !prefix) return nob_sv_from_cstr("");
    String_View pfx = nob_sv_from_cstr(prefix);
    size_t total = pfx.count + 2 + scope_object.count + 2 + item_object.count;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    memcpy(buf + off, pfx.data, pfx.count);
    off += pfx.count;
    buf[off++] = ':';
    buf[off++] = ':';
    if (scope_object.count > 0) {
        memcpy(buf + off, scope_object.data, scope_object.count);
        off += scope_object.count;
    }
    buf[off++] = ':';
    buf[off++] = ':';
    if (item_object.count > 0) {
        memcpy(buf + off, item_object.data, item_object.count);
        off += item_object.count;
    }
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

static String_View eval_file_parent_dir_view(String_View file_path) {
    if (file_path.count == 0 || !file_path.data) return nob_sv_from_cstr(".");

    size_t end = file_path.count;
    while (end > 0) {
        char c = file_path.data[end - 1];
        if (c != '/' && c != '\\') break;
        end--;
    }
    if (end == 0) return nob_sv_from_cstr("/");

    size_t slash = SIZE_MAX;
    for (size_t i = 0; i < end; i++) {
        char c = file_path.data[i];
        if (c == '/' || c == '\\') slash = i;
    }
    if (slash == SIZE_MAX) return nob_sv_from_cstr(".");
    if (slash == 0) return nob_sv_from_cstr("/");
    if (file_path.data[slash - 1] == ':') {
        return nob_sv_from_parts(file_path.data, slash + 1);
    }
    return nob_sv_from_parts(file_path.data, slash);
}

static bool eval_source_extension_allowed(String_View path) {
    if (path.count == 0 || !path.data) return false;
    size_t dot = SIZE_MAX;
    for (size_t i = 0; i < path.count; i++) {
        if (path.data[i] == '.') dot = i;
        if (path.data[i] == '/' || path.data[i] == '\\') dot = SIZE_MAX;
    }
    if (dot == SIZE_MAX || dot + 1 >= path.count) return false;
    String_View ext = nob_sv_from_parts(path.data + dot + 1, path.count - dot - 1);
    return eval_sv_eq_ci_lit(ext, "c") ||
           eval_sv_eq_ci_lit(ext, "cc") ||
           eval_sv_eq_ci_lit(ext, "cpp") ||
           eval_sv_eq_ci_lit(ext, "cxx") ||
           eval_sv_eq_ci_lit(ext, "m") ||
           eval_sv_eq_ci_lit(ext, "mm");
}

static int eval_cstr_cmp_qsort(const void *a, const void *b) {
    const char *const *aa = (const char *const *)a;
    const char *const *bb = (const char *const *)b;
    return strcmp(*aa, *bb);
}

bool eval_list_dir_sources_sorted_temp(EvalExecContext *ctx, String_View dir, SV_List *out_sources) {
    Nob_File_Paths entries = {0};
    if (!ctx || !out_sources) return false;
    *out_sources = (SV_List){0};

    char *dir_c = eval_sv_to_cstr_temp(ctx, dir);
    EVAL_OOM_RETURN_IF_NULL(ctx, dir_c, false);

    if (!nob_file_exists(dir_c) || nob_get_file_type(dir_c) != NOB_FILE_DIRECTORY) {
        return true;
    }
    if (!nob_read_entire_dir(dir_c, &entries)) return true;
    if (entries.count > 1) {
        qsort(entries.items, entries.count, sizeof(entries.items[0]), eval_cstr_cmp_qsort);
    }

    for (size_t i = 0; i < entries.count; i++) {
        const char *entry_name = entries.items[i];
        String_View name = {0};
        String_View full = {0};
        char *full_c = NULL;
        Nob_File_Type kind = NOB_FILE_OTHER;

        if (!entry_name || strcmp(entry_name, ".") == 0 || strcmp(entry_name, "..") == 0) continue;
        name = nob_sv_from_cstr(entry_name);
        if (!eval_source_extension_allowed(name)) continue;
        full = eval_sv_path_join(eval_temp_arena(ctx), dir, name);
        if (eval_should_stop(ctx)) {
            nob_da_free(entries);
            return false;
        }
        full_c = eval_sv_to_cstr_temp(ctx, full);
        if (!full_c) {
            nob_da_free(entries);
            return false;
        }
        kind = nob_get_file_type(full_c);
        if (kind == NOB_FILE_DIRECTORY) continue;
        if (!svu_list_push_temp(ctx, out_sources, full)) {
            nob_da_free(entries);
            return false;
        }
    }

    nob_da_free(entries);
    return true;
}

bool eval_service_read_file(EvalExecContext *ctx,
                            String_View path,
                            String_View *out_contents,
                            bool *out_found) {
    if (out_contents) *out_contents = nob_sv_from_cstr("");
    if (out_found) *out_found = false;
    if (!ctx || path.count == 0) return false;

    if (ctx->services && ctx->services->fs_read_file) {
        return ctx->services->fs_read_file(ctx->services->user_data,
                                           eval_temp_arena(ctx),
                                           path,
                                           out_contents,
                                           out_found);
    }

    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);
    if (!nob_file_exists(path_c)) {
        return true;
    }
    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(path_c, &sb)) return false;

    String_View contents = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items ? sb.items : "", sb.count));
    nob_sb_free(sb);
    if (eval_should_stop(ctx)) return false;

    if (out_contents) *out_contents = contents;
    if (out_found) *out_found = true;
    return true;
}

bool eval_service_write_file(EvalExecContext *ctx,
                             String_View path,
                             String_View contents,
                             bool append) {
    if (!ctx || path.count == 0) return false;
    if (ctx->services && ctx->services->fs_write_file) {
        return ctx->services->fs_write_file(ctx->services->user_data, path, contents, append);
    }

    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);

    if (!append) {
        return nob_write_entire_file(path_c, contents.data ? contents.data : "", contents.count);
    }

    Nob_String_Builder sb = {0};
    if (nob_file_exists(path_c) && !nob_read_entire_file(path_c, &sb)) return false;
    if (contents.count > 0) nob_sb_append_buf(&sb, contents.data, contents.count);
    bool ok = nob_write_entire_file(path_c, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    return ok;
}

bool eval_service_mkdir(EvalExecContext *ctx, String_View path) {
    if (!ctx || path.count == 0) return false;
    if (ctx->services && ctx->services->fs_mkdir) {
        return ctx->services->fs_mkdir(ctx->services->user_data, path);
    }

    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);
    return nob_mkdir_if_not_exists(path_c);
}

bool eval_service_file_exists(EvalExecContext *ctx, String_View path, bool *out_exists) {
    if (out_exists) *out_exists = false;
    if (!ctx || path.count == 0) return false;

    if (ctx->services && ctx->services->fs_file_exists) {
        return ctx->services->fs_file_exists(ctx->services->user_data, path, out_exists);
    }

    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);
    if (out_exists) *out_exists = nob_file_exists(path_c) != 0;
    return true;
}

bool eval_service_copy_file(EvalExecContext *ctx, String_View src, String_View dst) {
    if (!ctx || src.count == 0 || dst.count == 0) return false;
    if (ctx->services && ctx->services->fs_copy_file) {
        return ctx->services->fs_copy_file(ctx->services->user_data, src, dst);
    }

    char *src_c = eval_sv_to_cstr_temp(ctx, src);
    char *dst_c = eval_sv_to_cstr_temp(ctx, dst);
    EVAL_OOM_RETURN_IF_NULL(ctx, src_c, false);
    EVAL_OOM_RETURN_IF_NULL(ctx, dst_c, false);
    return nob_copy_file(src_c, dst_c);
}

bool eval_service_copy_directory(EvalExecContext *ctx, String_View src, String_View dst) {
    if (!ctx || src.count == 0 || dst.count == 0) return false;
    if (ctx->services && ctx->services->fs_copy_directory) {
        return ctx->services->fs_copy_directory(ctx->services->user_data, src, dst);
    }

    char *src_c = eval_sv_to_cstr_temp(ctx, src);
    char *dst_c = eval_sv_to_cstr_temp(ctx, dst);
    EVAL_OOM_RETURN_IF_NULL(ctx, src_c, false);
    EVAL_OOM_RETURN_IF_NULL(ctx, dst_c, false);
    return nob_copy_directory_recursively(src_c, dst_c);
}

static Eval_Fs_Node_Type eval_host_stat_type_from_mode(unsigned long mode) {
#if defined(S_ISLNK)
    if (S_ISLNK(mode)) return EVAL_FS_NODE_SYMLINK;
#endif
#if defined(S_ISDIR)
    if (S_ISDIR(mode)) return EVAL_FS_NODE_DIRECTORY;
#endif
#if defined(S_ISREG)
    if (S_ISREG(mode)) return EVAL_FS_NODE_FILE;
#endif
    return EVAL_FS_NODE_OTHER;
}

bool eval_service_stat(EvalExecContext *ctx,
                       String_View path,
                       bool follow_symlinks,
                       Eval_Fs_Stat *out_stat) {
    if (out_stat) *out_stat = (Eval_Fs_Stat){0};
    if (!ctx || path.count == 0 || !out_stat) return false;
    if (ctx->services && ctx->services->fs_stat) {
        return ctx->services->fs_stat(ctx->services->user_data, path, follow_symlinks, out_stat);
    }

    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);

#if defined(_WIN32)
    (void)follow_symlinks;
    WIN32_FILE_ATTRIBUTE_DATA data = {0};
    if (!GetFileAttributesExA(path_c, GetFileExInfoStandard, &data)) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return true;
        return false;
    }
    out_stat->exists = true;
    out_stat->type = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                         ? EVAL_FS_NODE_SYMLINK
                         : ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                                ? EVAL_FS_NODE_DIRECTORY
                                : EVAL_FS_NODE_FILE);
    out_stat->size = (((uint64_t)data.nFileSizeHigh) << 32) | (uint64_t)data.nFileSizeLow;
    uint64_t ft = (((uint64_t)data.ftLastWriteTime.dwHighDateTime) << 32) |
                  (uint64_t)data.ftLastWriteTime.dwLowDateTime;
    if (ft >= 116444736000000000ULL) {
        out_stat->mtime_sec = (int64_t)((ft - 116444736000000000ULL) / 10000000ULL);
        out_stat->have_mtime = true;
    }
    out_stat->mode = (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) ? 0444u : 0666u;
    if (out_stat->type == EVAL_FS_NODE_DIRECTORY) out_stat->mode |= 0111u;
    out_stat->have_mode = true;
    return true;
#else
    struct stat st = {0};
    int rc = follow_symlinks ? stat(path_c, &st) : lstat(path_c, &st);
    if (rc != 0) {
        if (errno == ENOENT || errno == ENOTDIR) return true;
        return false;
    }
    out_stat->exists = true;
    out_stat->type = eval_host_stat_type_from_mode((unsigned long)st.st_mode);
    out_stat->size = st.st_size >= 0 ? (uint64_t)st.st_size : 0;
    out_stat->mtime_sec = (int64_t)st.st_mtime;
    out_stat->mode = (uint32_t)(st.st_mode & 07777);
    out_stat->have_mtime = true;
    out_stat->have_mode = true;
    return true;
#endif
}

bool eval_service_rename(EvalExecContext *ctx, String_View old_path, String_View new_path) {
    if (!ctx || old_path.count == 0 || new_path.count == 0) return false;
    if (ctx->services && ctx->services->fs_rename) {
        return ctx->services->fs_rename(ctx->services->user_data, old_path, new_path);
    }

    char *old_c = eval_sv_to_cstr_temp(ctx, old_path);
    char *new_c = eval_sv_to_cstr_temp(ctx, new_path);
    EVAL_OOM_RETURN_IF_NULL(ctx, old_c, false);
    EVAL_OOM_RETURN_IF_NULL(ctx, new_c, false);
    return nob_rename(old_c, new_c);
}

static bool eval_host_remove_leaf_c(const char *path, bool is_dir) {
    if (!path) return false;
#if defined(_WIN32)
    if (is_dir) return RemoveDirectoryA(path) != 0;
    return DeleteFileA(path) != 0;
#else
    if (is_dir) return rmdir(path) == 0;
    return unlink(path) == 0;
#endif
}

typedef struct {
    bool ok;
} Eval_Host_Remove_Walk_State;

static bool eval_host_remove_tree_walk(Nob_Walk_Entry entry) {
    Eval_Host_Remove_Walk_State *state = (Eval_Host_Remove_Walk_State*)entry.data;
    bool ok = eval_host_remove_leaf_c(entry.path, entry.type == NOB_FILE_DIRECTORY);
    if (state) state->ok = state->ok && ok;
    return ok;
}

bool eval_service_remove(EvalExecContext *ctx, String_View path, bool recursive) {
    if (!ctx || path.count == 0) return false;
    if (ctx->services && ctx->services->fs_remove) {
        return ctx->services->fs_remove(ctx->services->user_data, path, recursive);
    }

    Eval_Fs_Stat st = {0};
    if (!eval_service_stat(ctx, path, false, &st)) return false;
    if (!st.exists) return true;

    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);
    if (!recursive || st.type != EVAL_FS_NODE_DIRECTORY) {
        return eval_host_remove_leaf_c(path_c, st.type == EVAL_FS_NODE_DIRECTORY);
    }

    Eval_Host_Remove_Walk_State state = { .ok = true };
    if (!nob_walk_dir(path_c, eval_host_remove_tree_walk, .data = &state, .post_order = true)) {
        return false;
    }
    return state.ok;
}

static bool eval_host_chmod_one_c(const char *path, uint32_t mode) {
    if (!path) return false;
#if defined(_WIN32)
    (void)mode;
    return _chmod(path, _S_IREAD | _S_IWRITE) == 0;
#else
    return chmod(path, (mode_t)mode) == 0;
#endif
}

typedef struct {
    uint32_t mode;
    bool ok;
} Eval_Host_Chmod_Walk_State;

static bool eval_host_chmod_walk(Nob_Walk_Entry entry) {
    Eval_Host_Chmod_Walk_State *state = (Eval_Host_Chmod_Walk_State*)entry.data;
    bool ok = state && eval_host_chmod_one_c(entry.path, state->mode);
    if (state) state->ok = state->ok && ok;
    return ok;
}

bool eval_service_chmod(EvalExecContext *ctx, String_View path, uint32_t mode, bool recursive) {
    if (!ctx || path.count == 0) return false;
    if (ctx->services && ctx->services->fs_chmod) {
        return ctx->services->fs_chmod(ctx->services->user_data, path, mode, recursive);
    }

    Eval_Fs_Stat st = {0};
    if (!eval_service_stat(ctx, path, false, &st) || !st.exists) return false;
    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);
    if (!recursive || st.type != EVAL_FS_NODE_DIRECTORY) {
        return eval_host_chmod_one_c(path_c, mode);
    }

    Eval_Host_Chmod_Walk_State state = {
        .mode = mode,
        .ok = true,
    };
    if (!nob_walk_dir(path_c, eval_host_chmod_walk, .data = &state)) return false;
    return state.ok;
}

bool eval_service_touch(EvalExecContext *ctx, String_View path, bool create) {
    if (!ctx || path.count == 0) return false;
    if (ctx->services && ctx->services->fs_touch) {
        return ctx->services->fs_touch(ctx->services->user_data, path, create);
    }

    Eval_Fs_Stat st = {0};
    if (!eval_service_stat(ctx, path, true, &st)) return false;
    if (!st.exists) {
        if (!create) return true;
        if (!eval_service_write_file(ctx, path, nob_sv_from_cstr(""), false)) return false;
    }

    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);
#if defined(_WIN32)
    struct _utimbuf tb = {0};
    tb.actime = time(NULL);
    tb.modtime = time(NULL);
    return _utime(path_c, &tb) == 0;
#else
    struct utimbuf tb = {0};
    tb.actime = time(NULL);
    tb.modtime = time(NULL);
    return utime(path_c, &tb) == 0;
#endif
}

bool eval_service_link(EvalExecContext *ctx,
                       String_View src,
                       String_View dst,
                       Eval_Fs_Link_Kind kind) {
    if (!ctx || src.count == 0 || dst.count == 0) return false;
    if (ctx->services && ctx->services->fs_link) {
        return ctx->services->fs_link(ctx->services->user_data, src, dst, kind);
    }

    char *src_c = eval_sv_to_cstr_temp(ctx, src);
    char *dst_c = eval_sv_to_cstr_temp(ctx, dst);
    EVAL_OOM_RETURN_IF_NULL(ctx, src_c, false);
    EVAL_OOM_RETURN_IF_NULL(ctx, dst_c, false);
#if defined(_WIN32)
    if (kind == EVAL_FS_LINK_SYMBOLIC) return CreateSymbolicLinkA(dst_c, src_c, 0) != 0;
    return CreateHardLinkA(dst_c, src_c, NULL) != 0;
#else
    if (kind == EVAL_FS_LINK_SYMBOLIC) return symlink(src_c, dst_c) == 0;
    return link(src_c, dst_c) == 0;
#endif
}

bool eval_service_readlink(EvalExecContext *ctx, String_View path, String_View *out_target) {
    if (out_target) *out_target = nob_sv_from_cstr("");
    if (!ctx || path.count == 0 || !out_target) return false;
    if (ctx->services && ctx->services->fs_readlink) {
        return ctx->services->fs_readlink(ctx->services->user_data,
                                          eval_temp_arena(ctx),
                                          path,
                                          out_target);
    }

    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);
#if defined(_WIN32)
    HANDLE h = CreateFileA(path_c,
                           FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL,
                           OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS,
                           NULL);
    if (h == INVALID_HANDLE_VALUE) return false;

    DWORD need = GetFinalPathNameByHandleA(h, NULL, 0, FILE_NAME_NORMALIZED);
    if (need == 0) {
        CloseHandle(h);
        return false;
    }

    DWORD cap = need + 1;
    char *raw = (char*)arena_alloc(eval_temp_arena(ctx), (size_t)cap);
    EVAL_OOM_RETURN_IF_NULL(ctx, raw, false);
    DWORD wrote = GetFinalPathNameByHandleA(h, raw, cap, FILE_NAME_NORMALIZED);
    CloseHandle(h);
    if (wrote == 0 || wrote >= cap) return false;

    for (DWORD i = 0; i < wrote; i++) {
        if (raw[i] == '\\') raw[i] = '/';
    }
    *out_target = nob_sv_from_parts(raw, (size_t)wrote);
    return true;
#else
    char buf[4096];
    ssize_t n = readlink(path_c, buf, sizeof(buf) - 1);
    if (n < 0) return false;
    buf[n] = '\0';
    *out_target = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(buf, (size_t)n));
    return !eval_should_stop(ctx);
#endif
}

static String_View eval_service_glob_base_dir(String_View pattern_abs) {
    size_t first_meta = pattern_abs.count;
    for (size_t i = 0; i < pattern_abs.count; i++) {
        char c = pattern_abs.data[i];
        if (c == '*' || c == '?' || c == '[') {
            first_meta = i;
            break;
        }
    }
    if (first_meta == pattern_abs.count) return svu_dirname(pattern_abs);
    if (first_meta == 0) return nob_sv_from_cstr(".");
    return svu_dirname(nob_sv_from_parts(pattern_abs.data, first_meta));
}

#if !defined(_WIN32)
static bool eval_service_posix_glob_collect(EvalExecContext *ctx,
                                            String_View pat,
                                            bool list_dirs,
                                            SV_List *io_matches) {
    char *pat_c = eval_sv_to_cstr_temp(ctx, pat);
    EVAL_OOM_RETURN_IF_NULL(ctx, pat_c, false);

    glob_t g = {0};
    int rc = glob(pat_c, 0, NULL, &g);
    if (rc == GLOB_NOMATCH) {
        globfree(&g);
        return true;
    }
    if (rc != 0) {
        globfree(&g);
        return false;
    }

    for (size_t i = 0; i < g.gl_pathc; i++) {
        const char *entry = g.gl_pathv[i];
        if (!entry) continue;
        Nob_File_Type t = nob_get_file_type(entry);
        if (!list_dirs && t == NOB_FILE_DIRECTORY) continue;

        String_View sv = sv_copy_to_temp_arena(ctx, nob_sv_from_cstr(entry));
        if (eval_should_stop(ctx)) {
            globfree(&g);
            return false;
        }
        if (!svu_list_push_temp(ctx, io_matches, sv)) {
            globfree(&g);
            return false;
        }
    }

    globfree(&g);
    return true;
}
#endif

static void eval_service_glob_walk(EvalExecContext *ctx,
                                   String_View dir_full,
                                   String_View pat,
                                   bool recurse,
                                   bool list_dirs,
                                   bool ci,
                                   size_t *io_open_failures,
                                   SV_List *io_matches) {
    if (ctx->oom || dir_full.count == 0) return;

    char *dir_c = (char*)arena_alloc(eval_temp_arena(ctx), dir_full.count + 1);
    EVAL_OOM_RETURN_VOID_IF_NULL(ctx, dir_c);
    memcpy(dir_c, dir_full.data, dir_full.count);
    dir_c[dir_full.count] = 0;

    Nob_Dir_Entry dir = {0};
    if (!nob_dir_entry_open(dir_c, &dir)) {
        if (io_open_failures) (*io_open_failures)++;
        return;
    }

    while (nob_dir_entry_next(&dir)) {
        if (strcmp(dir.name, ".") == 0 || strcmp(dir.name, "..") == 0) continue;

        String_View name = nob_sv_from_cstr(dir.name);
        String_View full = eval_sv_path_join(eval_temp_arena(ctx), dir_full, name);
        char *full_c = eval_sv_to_cstr_temp(ctx, full);
        if (!full_c) {
            nob_dir_entry_close(dir);
            return;
        }

        Nob_File_Type kind = nob_get_file_type(full_c);
        if ((int)kind < 0) continue;
        bool is_dir = kind == NOB_FILE_DIRECTORY;

        if (eval_file_glob_match_sv(pat, full, ci) && (list_dirs || !is_dir)) {
            if (!svu_list_push_temp(ctx, io_matches, full)) break;
        }

        if (recurse && is_dir) {
            eval_service_glob_walk(ctx, full, pat, recurse, list_dirs, ci, io_open_failures, io_matches);
            if (ctx->oom) break;
        }
    }

    if (dir.error && io_open_failures) (*io_open_failures)++;
    nob_dir_entry_close(dir);
}

bool eval_service_glob(EvalExecContext *ctx,
                       const Eval_Glob_Request *req,
                       Eval_Glob_Result *out) {
    if (out) *out = (Eval_Glob_Result){0};
    if (!ctx || !req || !out) return false;

    if (ctx->services && ctx->services->fs_glob) {
        return ctx->services->fs_glob(ctx->services->user_data,
                                      eval_temp_arena(ctx),
                                      req,
                                      out);
    }

    SV_List matches = NULL;
    size_t open_failures = 0;
    for (size_t i = 0; i < req->pattern_count; i++) {
        String_View pat = req->patterns[i];

#if !defined(_WIN32)
        if (!req->recursive) {
            if (eval_service_posix_glob_collect(ctx, pat, req->list_directories, &matches)) {
                if (eval_should_stop(ctx)) return false;
                continue;
            }
        }
#endif

        String_View base_dir = eval_service_glob_base_dir(pat);
        eval_service_glob_walk(ctx,
                               base_dir,
                               pat,
                               req->recursive,
                               req->list_directories,
                               req->case_insensitive,
                               &open_failures,
                               &matches);
        if (eval_should_stop(ctx)) return false;
    }

    out->matches = matches;
    out->match_count = arena_arr_len(matches);
    out->open_failure_count = open_failures;
    return true;
}

static void eval_service_normalize_slashes_in_place(char *s) {
    if (!s) return;
    for (size_t i = 0; s[i] != '\0'; i++) {
        if (s[i] == '\\') s[i] = '/';
    }
}

static bool eval_service_canonicalize_existing_cstr(EvalExecContext *ctx,
                                                    const char *path_c,
                                                    String_View *out_path) {
    if (out_path) *out_path = nob_sv_from_cstr("");
    if (!ctx || !path_c || !out_path) return false;

#if defined(_WIN32)
    HANDLE h = CreateFileA(path_c,
                           FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL,
                           OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS,
                           NULL);
    if (h == INVALID_HANDLE_VALUE) return true;

    DWORD need = GetFinalPathNameByHandleA(h, NULL, 0, FILE_NAME_NORMALIZED);
    if (need == 0) {
        CloseHandle(h);
        return true;
    }

    DWORD cap = need + 1;
    char *raw = (char*)arena_alloc(eval_temp_arena(ctx), (size_t)cap);
    EVAL_OOM_RETURN_IF_NULL(ctx, raw, false);

    DWORD wrote = GetFinalPathNameByHandleA(h, raw, cap, FILE_NAME_NORMALIZED);
    CloseHandle(h);
    if (wrote == 0 || wrote >= cap) return true;

    const char *view = raw;
    char *unc_fixed = NULL;
    if (strncmp(view, "\\\\?\\UNC\\", 8) == 0) {
        size_t rest = strlen(view + 8);
        unc_fixed = (char*)arena_alloc(eval_temp_arena(ctx), rest + 3);
        EVAL_OOM_RETURN_IF_NULL(ctx, unc_fixed, false);
        unc_fixed[0] = '/';
        unc_fixed[1] = '/';
        memcpy(unc_fixed + 2, view + 8, rest + 1);
        view = unc_fixed;
    } else if (strncmp(view, "\\\\?\\", 4) == 0) {
        view += 4;
    }

    size_t len = strlen(view);
    char *norm = (char*)arena_alloc(eval_temp_arena(ctx), len + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, norm, false);
    memcpy(norm, view, len + 1);
    eval_service_normalize_slashes_in_place(norm);
    *out_path = nob_sv_from_parts(norm, len);
    return true;
#else
    char *real = realpath(path_c, NULL);
    if (!real) return true;
    size_t len = strlen(real);
    char *norm = (char*)arena_alloc(eval_temp_arena(ctx), len + 1);
    if (!norm) {
        free(real);
        return ctx_oom(ctx);
    }
    memcpy(norm, real, len + 1);
    free(real);
    *out_path = nob_sv_from_parts(norm, len);
    return true;
#endif
}

bool eval_service_canonicalize_path(EvalExecContext *ctx,
                                    const Eval_Path_Canonicalize_Request *req,
                                    Eval_Path_Canonicalize_Result *out) {
    if (out) *out = (Eval_Path_Canonicalize_Result){0};
    if (!ctx || !req || !out || req->path.count == 0) return false;

    if (ctx->services && ctx->services->fs_canonicalize_path) {
        return ctx->services->fs_canonicalize_path(ctx->services->user_data,
                                                   eval_temp_arena(ctx),
                                                   req,
                                                   out);
    }

    char *probe = eval_sv_to_cstr_temp(ctx, req->path);
    EVAL_OOM_RETURN_IF_NULL(ctx, probe, false);
    eval_service_normalize_slashes_in_place(probe);

    for (;;) {
        String_View canon = nob_sv_from_cstr("");
        if (!eval_service_canonicalize_existing_cstr(ctx, probe, &canon)) return false;
        if (canon.count > 0) {
            out->found = true;
            out->path = canon;
            return true;
        }
        if (!req->existing_parent) return true;

        size_t len = strlen(probe);
        while (len > 0 && probe[len - 1] == '/') {
            if (len == 1) break;
            if (len == 3 && isalpha((unsigned char)probe[0]) && probe[1] == ':') break;
            probe[--len] = '\0';
        }

        char *last = strrchr(probe, '/');
        if (!last) return true;

        if (last == probe) {
            probe[1] = '\0';
            continue;
        }
        if (last == probe + 2 && isalpha((unsigned char)probe[0]) && probe[1] == ':') {
            probe[3] = '\0';
            continue;
        }
        *last = '\0';
    }
}

bool eval_service_lock_acquire(EvalExecContext *ctx,
                               const Eval_Host_Lock_Request *req,
                               Eval_Host_Lock_Result *out) {
    if (out) *out = (Eval_Host_Lock_Result){.message = nob_sv_from_cstr("")};
    if (!ctx || !req || !out || req->path.count == 0) return false;

    if (ctx->services && ctx->services->host_lock_acquire) {
        return ctx->services->host_lock_acquire(ctx->services->user_data, req, out);
    }

    char *path_c = eval_sv_to_cstr_temp(ctx, req->path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);

#if defined(_WIN32)
    HANDLE h = CreateFileA(path_c,
                           GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL,
                           OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL,
                           NULL);
    if (h == INVALID_HANDLE_VALUE) {
        out->message = nob_sv_from_cstr("failed to open lock file");
        return true;
    }

    bool ok = false;
    DWORD start_ms = GetTickCount();
    for (;;) {
        OVERLAPPED ov = {0};
        if (LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, MAXDWORD, MAXDWORD, &ov) != 0) {
            ok = true;
            break;
        }
        DWORD err = GetLastError();
        if (err != ERROR_LOCK_VIOLATION) break;
        if (req->has_timeout) {
            DWORD elapsed = GetTickCount() - start_ms;
            if ((size_t)(elapsed / 1000) >= req->timeout_sec) break;
        }
        Sleep(100);
    }
    if (!ok) {
        CloseHandle(h);
        out->message = nob_sv_from_cstr("failed to acquire lock");
        return true;
    }
    out->acquired = true;
    out->token = (uintptr_t)h;
    out->message = nob_sv_from_cstr("0");
    return true;
#else
    int fd = open(path_c, O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
        out->message = nob_sv_from_cstr("failed to open lock file");
        return true;
    }

    bool ok = false;
    time_t start = time(NULL);
    for (;;) {
        if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
            ok = true;
            break;
        }
        if (errno != EWOULDBLOCK && errno != EAGAIN) break;
        if (req->has_timeout) {
            time_t now = time(NULL);
            if ((size_t)(now - start) >= req->timeout_sec) break;
        }
        struct timespec sleep_req = {.tv_sec = 0, .tv_nsec = 100000000L};
        nanosleep(&sleep_req, NULL);
    }

    if (!ok) {
        close(fd);
        out->message = nob_sv_from_cstr("failed to acquire lock");
        return true;
    }
    out->acquired = true;
    out->token = (uintptr_t)fd;
    out->message = nob_sv_from_cstr("0");
    return true;
#endif
}

bool eval_service_lock_release(EvalExecContext *ctx, uintptr_t token) {
    if (!ctx || token == 0) return false;
    if (ctx->services && ctx->services->host_lock_release) {
        return ctx->services->host_lock_release(ctx->services->user_data, token);
    }
#if defined(_WIN32)
    HANDLE h = (HANDLE)token;
    OVERLAPPED ov = {0};
    (void)UnlockFileEx(h, 0, MAXDWORD, MAXDWORD, &ov);
    CloseHandle(h);
#else
    int fd = (int)token;
    (void)flock(fd, LOCK_UN);
    (void)close(fd);
#endif
    return true;
}

static bool eval_service_argv_push(EvalExecContext *ctx, SV_List *argv, String_View arg) {
    if (!ctx || !argv) return false;
    if (!arena_arr_push(eval_temp_arena(ctx), *argv, arg)) return ctx_oom(ctx);
    return true;
}

static bool eval_service_run_argv(EvalExecContext *ctx,
                                  SV_List argv,
                                  int *out_status_code,
                                  String_View *out_log) {
    if (out_status_code) *out_status_code = 1;
    if (out_log) *out_log = nob_sv_from_cstr("");
    if (!ctx || !argv || arena_arr_len(argv) == 0 || !out_status_code || !out_log) return false;

    Eval_Process_Run_Request proc_req = {
        .argv = argv,
        .argc = arena_arr_len(argv),
    };
    Eval_Process_Run_Result proc = {0};
    if (!eval_process_run_capture(ctx, &proc_req, &proc)) return false;

    *out_status_code = proc.started ? proc.exit_code : 1;
    if (proc.stdout_text.count > 0 && proc.stderr_text.count > 0) {
        char *buf = arena_alloc(eval_temp_arena(ctx), proc.stdout_text.count + proc.stderr_text.count + 2);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
        memcpy(buf, proc.stdout_text.data, proc.stdout_text.count);
        buf[proc.stdout_text.count] = '\n';
        memcpy(buf + proc.stdout_text.count + 1, proc.stderr_text.data, proc.stderr_text.count);
        *out_log = nob_sv_from_parts(buf, proc.stdout_text.count + proc.stderr_text.count + 1);
    } else if (proc.stdout_text.count > 0) {
        *out_log = proc.stdout_text;
    } else if (proc.stderr_text.count > 0) {
        *out_log = proc.stderr_text;
    } else {
        *out_log = proc.result_text;
    }
    return true;
}

static bool eval_service_archive_create_cli(EvalExecContext *ctx,
                                            const Eval_Archive_Create_Request *req,
                                            Eval_Backend_Result *out) {
    String_View cwd = eval_sv_path_normalize_temp(ctx, eval_process_cwd_temp(ctx));
    bool format_zip = eval_sv_eq_ci_lit(req->format, "ZIP");
    SV_List argv = NULL;

    if (format_zip) {
        if (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("zip")) ||
            !eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("-r")) ||
            !eval_service_argv_push(ctx, &argv, req->output)) {
            return false;
        }
        for (size_t i = 0; i < req->path_count; i++) {
            if (!eval_service_argv_push(ctx, &argv, req->paths[i])) return false;
        }
    } else {
        if (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("tar"))) return false;
        if (req->compression.count == 0 || eval_sv_eq_ci_lit(req->compression, "NONE")) {
            if (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("-cf"))) return false;
        } else if (eval_sv_eq_ci_lit(req->compression, "GZIP")) {
            if (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("-czf"))) return false;
        } else if (eval_sv_eq_ci_lit(req->compression, "BZIP2")) {
            if (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("-cjf"))) return false;
        } else if (eval_sv_eq_ci_lit(req->compression, "XZ")) {
            if (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("-cJf"))) return false;
        } else if (eval_sv_eq_ci_lit(req->compression, "ZSTD")) {
            if (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--zstd")) ||
                !eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("-cf"))) return false;
        } else {
            out->status_code = 1;
            out->log = nob_sv_from_cstr("unsupported COMPRESSION in local archive backend");
            return true;
        }

        if (!eval_service_argv_push(ctx, &argv, req->output)) return false;
        if (eval_sv_eq_ci_lit(req->format, "PAXR")) {
            if (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--format=pax")) ||
                !eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--pax-option=delete=atime,delete=ctime"))) return false;
        } else if (eval_sv_eq_ci_lit(req->format, "PAX")) {
            if (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--format=pax"))) return false;
        } else if (eval_sv_eq_ci_lit(req->format, "GNUTAR")) {
            if (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--format=gnu"))) return false;
        }
        if (req->has_mtime) {
            char *mtime = arena_alloc(eval_temp_arena(ctx), 64);
            EVAL_OOM_RETURN_IF_NULL(ctx, mtime, false);
            int n = snprintf(mtime, 64, "--mtime=@%lld", req->mtime_epoch);
            if (n < 0 || n >= 64) return ctx_oom(ctx);
            if (!eval_service_argv_push(ctx, &argv, nob_sv_from_parts(mtime, (size_t)n)) ||
                !eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--"))) return false;
        } else if (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--"))) {
            return false;
        }

        for (size_t i = 0; i < req->path_count; i++) {
            String_View p = req->paths[i];
            if (cwd.count > 0 && p.count > cwd.count &&
                memcmp(p.data, cwd.data, cwd.count) == 0 &&
                (p.data[cwd.count] == '/' || p.data[cwd.count] == '\\')) {
                p = nob_sv_from_parts(p.data + cwd.count + 1, p.count - cwd.count - 1);
            }
            if (!eval_service_argv_push(ctx, &argv, p)) return false;
        }
    }

    return eval_service_run_argv(ctx, argv, &out->status_code, &out->log);
}

bool eval_service_archive_create(EvalExecContext *ctx,
                                 const Eval_Archive_Create_Request *req,
                                 Eval_Backend_Result *out) {
    if (out) *out = (Eval_Backend_Result){.status_code = 1, .log = nob_sv_from_cstr("")};
    if (!ctx || !req || !out) return false;

    if (ctx->services && ctx->services->archive_create) {
        return ctx->services->archive_create(ctx->services->user_data, req, out);
    }

    Eval_File_Archive_Create_Options bopt = {0};
    bopt.output = req->output;
    bopt.paths = (String_View*)req->paths;
    bopt.format = req->format;
    bopt.compression = req->compression;
    bopt.has_compression_level = req->has_compression_level;
    bopt.compression_level = req->compression_level;
    bopt.has_mtime = req->has_mtime;
    bopt.mtime_epoch = req->mtime_epoch;
    bopt.verbose = req->verbose;

    bool replay_compatible_tar_backend = eval_sv_eq_ci_lit(req->format, "PAXR") &&
                                         (req->compression.count == 0 || eval_sv_eq_ci_lit(req->compression, "NONE")) &&
                                         !req->has_compression_level &&
                                         req->has_mtime;
    bool backend_ok = false;
    if (!replay_compatible_tar_backend) {
        backend_ok = eval_file_backend_archive_create(ctx, &bopt, &out->status_code, &out->log);
    }
    if (backend_ok) return true;
    if (ctx->oom) return false;
    return eval_service_archive_create_cli(ctx, req, out);
}

static bool eval_service_archive_extract_cli(EvalExecContext *ctx,
                                             const Eval_Archive_Extract_Request *req,
                                             Eval_Backend_Result *out) {
    SV_List argv = NULL;
    bool is_zip = req->input.count >= 4 &&
                  eval_sv_eq_ci_lit(nob_sv_from_parts(req->input.data + req->input.count - 4, 4), ".zip");
    if (is_zip) {
        if (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("unzip")) ||
            !eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("-o")) ||
            !eval_service_argv_push(ctx, &argv, req->input) ||
            !eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("-d")) ||
            !eval_service_argv_push(ctx, &argv, req->destination)) return false;
    } else {
        if (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("tar")) ||
            !eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("-xf")) ||
            !eval_service_argv_push(ctx, &argv, req->input) ||
            !eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("-C")) ||
            !eval_service_argv_push(ctx, &argv, req->destination)) return false;
    }
    return eval_service_run_argv(ctx, argv, &out->status_code, &out->log);
}

bool eval_service_archive_extract(EvalExecContext *ctx,
                                  const Eval_Archive_Extract_Request *req,
                                  Eval_Backend_Result *out) {
    if (out) *out = (Eval_Backend_Result){.status_code = 1, .log = nob_sv_from_cstr("")};
    if (!ctx || !req || !out) return false;

    if (ctx->services && ctx->services->archive_extract) {
        return ctx->services->archive_extract(ctx->services->user_data, req, out);
    }

    Eval_File_Archive_Extract_Options bopt = {0};
    bopt.input = req->input;
    bopt.destination = req->destination;
    bopt.patterns = (String_View*)req->patterns;
    bopt.list_only = req->list_only;
    bopt.verbose = req->verbose;
    bopt.touch = req->touch;

    bool backend_ok = eval_file_backend_archive_extract(ctx, &bopt, &out->status_code, &out->log);
    if (backend_ok) return true;
    if (ctx->oom) return false;
    return eval_service_archive_extract_cli(ctx, req, out);
}

static Eval_File_Netrc_Mode eval_transfer_to_file_netrc_mode(Eval_Transfer_Netrc_Mode mode) {
    switch (mode) {
        case EVAL_TRANSFER_NETRC_IGNORED: return EVAL_FILE_NETRC_IGNORED;
        case EVAL_TRANSFER_NETRC_OPTIONAL: return EVAL_FILE_NETRC_OPTIONAL;
        case EVAL_TRANSFER_NETRC_REQUIRED: return EVAL_FILE_NETRC_REQUIRED;
        case EVAL_TRANSFER_NETRC_DEFAULT:
        default: return EVAL_FILE_NETRC_DEFAULT;
    }
}

static void eval_transfer_to_curl_options(const Eval_Transfer_Options *src, Eval_File_Curl_Options *dst) {
    memset(dst, 0, sizeof(*dst));
    if (!src) return;
    dst->has_timeout = src->has_timeout;
    dst->has_inactivity_timeout = src->has_inactivity_timeout;
    dst->timeout_sec = src->timeout_sec;
    dst->inactivity_timeout_sec = src->inactivity_timeout_sec;
    dst->has_range_start = src->has_range_start;
    dst->has_range_end = src->has_range_end;
    dst->range_start = src->range_start;
    dst->range_end = src->range_end;
    dst->has_tls_verify = src->has_tls_verify;
    dst->tls_verify = src->tls_verify;
    dst->show_progress = src->show_progress;
    dst->userpwd = src->userpwd;
    dst->tls_cainfo = src->tls_cainfo;
    dst->netrc_file = src->netrc_file;
    dst->netrc_mode = eval_transfer_to_file_netrc_mode(src->netrc_mode);
    dst->http_headers = (String_View*)src->http_headers;
}

static bool eval_service_transfer_download_cli(EvalExecContext *ctx,
                                               const Eval_Transfer_Download_Request *req,
                                               Eval_Backend_Result *out) {
    if (!req->has_dst_path) {
        out->status_code = 1;
        out->log = nob_sv_from_cstr("probe-only remote DOWNLOAD requires libcurl backend");
        return true;
    }

    SV_List argv = NULL;
    char timeout_buf[64] = {0};
    char inactivity_buf[64] = {0};
    char range_buf[96] = {0};

    if (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("curl"))) return false;
    if (!req->options.show_progress &&
        !eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--silent"))) return false;
    if (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--show-error")) ||
        !eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--location")) ||
        !eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--output")) ||
        !eval_service_argv_push(ctx, &argv, req->dst_path)) return false;

    if (req->options.has_timeout) {
        snprintf(timeout_buf, sizeof(timeout_buf), "%ld", req->options.timeout_sec);
        if (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--max-time")) ||
            !eval_service_argv_push(ctx, &argv, nob_sv_from_cstr(timeout_buf))) return false;
    }
    if (req->options.has_inactivity_timeout) {
        snprintf(inactivity_buf, sizeof(inactivity_buf), "%ld", req->options.inactivity_timeout_sec);
        if (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--speed-time")) ||
            !eval_service_argv_push(ctx, &argv, nob_sv_from_cstr(inactivity_buf)) ||
            !eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--speed-limit")) ||
            !eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("1"))) return false;
    }
    if (req->options.userpwd.count > 0 &&
        (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--user")) ||
         !eval_service_argv_push(ctx, &argv, req->options.userpwd))) return false;
    if (req->options.has_tls_verify && !req->options.tls_verify &&
        !eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--insecure"))) return false;
    if (req->options.tls_cainfo.count > 0 &&
        (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--cacert")) ||
         !eval_service_argv_push(ctx, &argv, req->options.tls_cainfo))) return false;
    if (req->options.netrc_mode == EVAL_TRANSFER_NETRC_OPTIONAL) {
        if (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--netrc-optional"))) return false;
    } else if (req->options.netrc_mode == EVAL_TRANSFER_NETRC_REQUIRED) {
        if (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--netrc"))) return false;
    }
    if (req->options.netrc_file.count > 0 &&
        (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--netrc-file")) ||
         !eval_service_argv_push(ctx, &argv, req->options.netrc_file))) return false;
    for (size_t i = 0; i < req->options.http_headers_count; i++) {
        if (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--header")) ||
            !eval_service_argv_push(ctx, &argv, req->options.http_headers[i])) return false;
    }
    if (req->options.has_range_start || req->options.has_range_end) {
        if (req->options.has_range_start && req->options.has_range_end) {
            snprintf(range_buf, sizeof(range_buf), "%zu-%zu", req->options.range_start, req->options.range_end);
        } else if (req->options.has_range_start) {
            snprintf(range_buf, sizeof(range_buf), "%zu-", req->options.range_start);
        } else {
            snprintf(range_buf, sizeof(range_buf), "0-%zu", req->options.range_end);
        }
        if (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--range")) ||
            !eval_service_argv_push(ctx, &argv, nob_sv_from_cstr(range_buf))) return false;
    }
    if (!eval_service_argv_push(ctx, &argv, req->url)) return false;
    return eval_service_run_argv(ctx, argv, &out->status_code, &out->log);
}

bool eval_service_transfer_download(EvalExecContext *ctx,
                                    const Eval_Transfer_Download_Request *req,
                                    Eval_Backend_Result *out) {
    if (out) *out = (Eval_Backend_Result){.status_code = 1, .log = nob_sv_from_cstr("")};
    if (!ctx || !req || !out) return false;
    if (ctx->services && ctx->services->transfer_download) {
        return ctx->services->transfer_download(ctx->services->user_data, req, out);
    }

    Eval_File_Curl_Options bopt = {0};
    eval_transfer_to_curl_options(&req->options, &bopt);
    bool backend_ok = eval_file_backend_curl_download(ctx,
                                                      req->url,
                                                      req->dst_path,
                                                      req->has_dst_path,
                                                      &bopt,
                                                      &out->status_code,
                                                      &out->log);
    if (backend_ok) return true;
    if (ctx->oom) return false;
    return eval_service_transfer_download_cli(ctx, req, out);
}

static bool eval_service_transfer_upload_cli(EvalExecContext *ctx,
                                             const Eval_Transfer_Upload_Request *req,
                                             Eval_Backend_Result *out) {
    SV_List argv = NULL;
    char timeout_buf[64] = {0};
    char inactivity_buf[64] = {0};

    if (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("curl"))) return false;
    if (!req->options.show_progress &&
        !eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--silent"))) return false;
    if (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--show-error")) ||
        !eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--location")) ||
        !eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--upload-file")) ||
        !eval_service_argv_push(ctx, &argv, req->src_path)) return false;

    if (req->options.has_timeout) {
        snprintf(timeout_buf, sizeof(timeout_buf), "%ld", req->options.timeout_sec);
        if (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--max-time")) ||
            !eval_service_argv_push(ctx, &argv, nob_sv_from_cstr(timeout_buf))) return false;
    }
    if (req->options.has_inactivity_timeout) {
        snprintf(inactivity_buf, sizeof(inactivity_buf), "%ld", req->options.inactivity_timeout_sec);
        if (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--speed-time")) ||
            !eval_service_argv_push(ctx, &argv, nob_sv_from_cstr(inactivity_buf)) ||
            !eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--speed-limit")) ||
            !eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("1"))) return false;
    }
    if (req->options.userpwd.count > 0 &&
        (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--user")) ||
         !eval_service_argv_push(ctx, &argv, req->options.userpwd))) return false;
    if (req->options.has_tls_verify && !req->options.tls_verify &&
        !eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--insecure"))) return false;
    if (req->options.tls_cainfo.count > 0 &&
        (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--cacert")) ||
         !eval_service_argv_push(ctx, &argv, req->options.tls_cainfo))) return false;
    if (req->options.netrc_mode == EVAL_TRANSFER_NETRC_OPTIONAL) {
        if (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--netrc-optional"))) return false;
    } else if (req->options.netrc_mode == EVAL_TRANSFER_NETRC_REQUIRED) {
        if (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--netrc"))) return false;
    }
    if (req->options.netrc_file.count > 0 &&
        (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--netrc-file")) ||
         !eval_service_argv_push(ctx, &argv, req->options.netrc_file))) return false;
    for (size_t i = 0; i < req->options.http_headers_count; i++) {
        if (!eval_service_argv_push(ctx, &argv, nob_sv_from_cstr("--header")) ||
            !eval_service_argv_push(ctx, &argv, req->options.http_headers[i])) return false;
    }
    if (!eval_service_argv_push(ctx, &argv, req->url)) return false;
    return eval_service_run_argv(ctx, argv, &out->status_code, &out->log);
}

bool eval_service_transfer_upload(EvalExecContext *ctx,
                                  const Eval_Transfer_Upload_Request *req,
                                  Eval_Backend_Result *out) {
    if (out) *out = (Eval_Backend_Result){.status_code = 1, .log = nob_sv_from_cstr("")};
    if (!ctx || !req || !out) return false;
    if (ctx->services && ctx->services->transfer_upload) {
        return ctx->services->transfer_upload(ctx->services->user_data, req, out);
    }

    Eval_File_Curl_Options bopt = {0};
    eval_transfer_to_curl_options(&req->options, &bopt);
    bool backend_ok = eval_file_backend_curl_upload(ctx,
                                                    req->src_path,
                                                    req->url,
                                                    &bopt,
                                                    &out->status_code,
                                                    &out->log);
    if (backend_ok) return true;
    if (ctx->oom) return false;
    return eval_service_transfer_upload_cli(ctx, req, out);
}

bool eval_service_host_read_file(EvalExecContext *ctx,
                                 String_View path,
                                 String_View *out_contents,
                                 bool *out_found) {
    if (out_contents) *out_contents = nob_sv_from_cstr("");
    if (out_found) *out_found = false;
    if (!ctx || path.count == 0) return false;

    if (ctx->services && ctx->services->host_read_file) {
        return ctx->services->host_read_file(ctx->services->user_data,
                                             eval_temp_arena(ctx),
                                             path,
                                             out_contents,
                                             out_found);
    }

    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);

    FILE *fp = fopen(path_c, "rb");
    if (!fp) return true;
    if (out_found) *out_found = true;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return true;
    }
    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return true;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return true;
    }

    char *buf = arena_alloc(eval_temp_arena(ctx), (size_t)size + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
    size_t read_n = fread(buf, 1, (size_t)size, fp);
    bool had_error = ferror(fp) != 0;
    fclose(fp);
    if (read_n < (size_t)size && had_error) return true;

    buf[read_n] = '\0';
    if (out_contents) *out_contents = nob_sv_from_parts(buf, read_n);
    return true;
}

#if defined(_WIN32)
static bool eval_windows_registry_parse_root(String_View key, HKEY *out_root, String_View *out_subkey) {
    if (out_root) *out_root = NULL;
    if (out_subkey) *out_subkey = nob_sv_from_cstr("");
    if (!out_root || !out_subkey || key.count == 0) return false;

    size_t sep = SIZE_MAX;
    for (size_t i = 0; i < key.count; i++) {
        if (key.data[i] == '\\' || key.data[i] == '/') {
            sep = i;
            break;
        }
    }

    String_View root_sv = (sep == SIZE_MAX) ? key : nob_sv_from_parts(key.data, sep);
    String_View subkey = (sep == SIZE_MAX || sep + 1 >= key.count)
        ? nob_sv_from_cstr("")
        : nob_sv_from_parts(key.data + sep + 1, key.count - sep - 1);

    if (eval_sv_eq_ci_lit(root_sv, "HKLM") || eval_sv_eq_ci_lit(root_sv, "HKEY_LOCAL_MACHINE")) {
        *out_root = HKEY_LOCAL_MACHINE;
    } else if (eval_sv_eq_ci_lit(root_sv, "HKCU") || eval_sv_eq_ci_lit(root_sv, "HKEY_CURRENT_USER")) {
        *out_root = HKEY_CURRENT_USER;
    } else if (eval_sv_eq_ci_lit(root_sv, "HKCR") || eval_sv_eq_ci_lit(root_sv, "HKEY_CLASSES_ROOT")) {
        *out_root = HKEY_CLASSES_ROOT;
    } else if (eval_sv_eq_ci_lit(root_sv, "HKU") || eval_sv_eq_ci_lit(root_sv, "HKEY_USERS")) {
        *out_root = HKEY_USERS;
    } else if (eval_sv_eq_ci_lit(root_sv, "HKCC") || eval_sv_eq_ci_lit(root_sv, "HKEY_CURRENT_CONFIG")) {
        *out_root = HKEY_CURRENT_CONFIG;
    } else {
        return false;
    }

    *out_subkey = subkey;
    return true;
}

static size_t eval_windows_registry_collect_views(String_View view, REGSAM out_flags[2]) {
    if (!out_flags) return 0;
    if (view.count == 0 ||
        eval_sv_eq_ci_lit(view, "TARGET") ||
        eval_sv_eq_ci_lit(view, "HOST")) {
        out_flags[0] = 0;
        return 1;
    }
    if (eval_sv_eq_ci_lit(view, "64")) {
        out_flags[0] = KEY_WOW64_64KEY;
        return 1;
    }
    if (eval_sv_eq_ci_lit(view, "32")) {
        out_flags[0] = KEY_WOW64_32KEY;
        return 1;
    }
    if (eval_sv_eq_ci_lit(view, "64_32")) {
        out_flags[0] = KEY_WOW64_64KEY;
        out_flags[1] = KEY_WOW64_32KEY;
        return 2;
    }
    if (eval_sv_eq_ci_lit(view, "32_64")) {
        out_flags[0] = KEY_WOW64_32KEY;
        out_flags[1] = KEY_WOW64_64KEY;
        return 2;
    }
    if (eval_sv_eq_ci_lit(view, "BOTH")) {
        out_flags[0] = KEY_WOW64_64KEY;
        out_flags[1] = KEY_WOW64_32KEY;
        return 2;
    }

    out_flags[0] = 0;
    return 1;
}

static bool eval_windows_registry_append_unique_cstr(EvalExecContext *ctx,
                                                     SV_List *out_items,
                                                     const char *text) {
    if (!ctx || !out_items || !text || text[0] == '\0') return false;
    String_View value = sv_copy_to_temp_arena(ctx, nob_sv_from_cstr(text));
    if (eval_should_stop(ctx)) return false;
    for (size_t i = 0; i < arena_arr_len(*out_items); i++) {
        if (eval_sv_key_eq((*out_items)[i], value)) return true;
    }
    return svu_list_push_temp(ctx, out_items, value);
}

static bool eval_windows_registry_query_value_temp(EvalExecContext *ctx,
                                                   HKEY root,
                                                   String_View subkey,
                                                   REGSAM wow64_flag,
                                                   String_View value_name,
                                                   String_View separator,
                                                   String_View *out_value,
                                                   bool *out_found) {
    if (out_value) *out_value = nob_sv_from_cstr("");
    if (out_found) *out_found = false;
    if (!ctx) return false;

    char *subkey_c = eval_sv_to_cstr_temp(ctx, subkey);
    char *value_name_c = eval_sv_to_cstr_temp(ctx, value_name);
    EVAL_OOM_RETURN_IF_NULL(ctx, subkey_c, false);
    EVAL_OOM_RETURN_IF_NULL(ctx, value_name_c, false);

    HKEY handle = NULL;
    LONG open_res = RegOpenKeyExA(root, subkey.count > 0 ? subkey_c : NULL, 0, KEY_READ | wow64_flag, &handle);
    if (open_res != ERROR_SUCCESS) return true;

    DWORD type = 0;
    DWORD size = 0;
    LONG query_res = RegQueryValueExA(handle,
                                      value_name.count > 0 ? value_name_c : NULL,
                                      NULL,
                                      &type,
                                      NULL,
                                      &size);
    if (query_res != ERROR_SUCCESS) {
        RegCloseKey(handle);
        return true;
    }

    unsigned char *buf = arena_alloc(eval_temp_arena(ctx), (size_t)size + 2);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
    memset(buf, 0, (size_t)size + 2);
    query_res = RegQueryValueExA(handle,
                                 value_name.count > 0 ? value_name_c : NULL,
                                 NULL,
                                 &type,
                                 buf,
                                 &size);
    RegCloseKey(handle);
    if (query_res != ERROR_SUCCESS) return true;

    if (out_found) *out_found = true;
    if (type == REG_MULTI_SZ) {
        const char *sep = separator.count > 0 ? separator.data : ";";
        size_t sep_len = separator.count > 0 ? separator.count : 1;
        Nob_String_Builder sb = {0};
        const char *p = (const char*)buf;
        bool first = true;
        while (*p != '\0') {
            size_t len = strlen(p);
            if (!first) nob_sb_append_buf(&sb, sep, sep_len);
            nob_sb_append_buf(&sb, p, len);
            first = false;
            p += len + 1;
        }
        if (out_value) *out_value = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items ? sb.items : "", sb.count));
        nob_sb_free(sb);
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    if (type == REG_DWORD && size >= sizeof(DWORD)) {
        char tmp[32];
        DWORD value = *(DWORD*)buf;
        int n = snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)value);
        if (n < 0) return false;
        if (out_value) *out_value = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(tmp, (size_t)n));
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    if (type == REG_QWORD && size >= sizeof(unsigned long long)) {
        char tmp[32];
        unsigned long long value = *(unsigned long long*)buf;
        int n = snprintf(tmp, sizeof(tmp), "%llu", value);
        if (n < 0) return false;
        if (out_value) *out_value = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(tmp, (size_t)n));
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    if (size > 0 && out_value) {
        size_t text_len = strnlen((const char*)buf, (size_t)size);
        *out_value = sv_copy_to_temp_arena(ctx, nob_sv_from_parts((const char*)buf, text_len));
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    return true;
}

static bool eval_windows_registry_enumerate_temp(EvalExecContext *ctx,
                                                 HKEY root,
                                                 String_View subkey,
                                                 REGSAM wow64_flag,
                                                 Eval_Windows_Registry_Query_Kind kind,
                                                 SV_List *out_items) {
    if (!ctx || !out_items) return false;
    char *subkey_c = eval_sv_to_cstr_temp(ctx, subkey);
    EVAL_OOM_RETURN_IF_NULL(ctx, subkey_c, false);

    HKEY handle = NULL;
    LONG open_res = RegOpenKeyExA(root, subkey.count > 0 ? subkey_c : NULL, 0, KEY_READ | wow64_flag, &handle);
    if (open_res != ERROR_SUCCESS) return true;

    DWORD index = 0;
    for (;;) {
        char name_buf[512] = {0};
        DWORD name_len = (DWORD)(sizeof(name_buf) - 1);
        LONG res = (kind == EVAL_WINDOWS_REGISTRY_QUERY_SUBKEYS)
            ? RegEnumKeyExA(handle, index, name_buf, &name_len, NULL, NULL, NULL, NULL)
            : RegEnumValueA(handle, index, name_buf, &name_len, NULL, NULL, NULL, NULL);
        if (res == ERROR_NO_MORE_ITEMS) break;
        if (res == ERROR_SUCCESS) {
            if (!eval_windows_registry_append_unique_cstr(ctx, out_items, name_buf)) {
                RegCloseKey(handle);
                return false;
            }
        }
        index++;
    }

    RegCloseKey(handle);
    return true;
}
#endif

bool eval_service_host_query_windows_registry(
    EvalExecContext *ctx,
    const Eval_Windows_Registry_Query_Request *request,
    Eval_Windows_Registry_Query_Result *out_result) {
    if (out_result) *out_result = (Eval_Windows_Registry_Query_Result){0};
    if (!ctx || !request || !out_result || request->key.count == 0) return false;

    if (ctx->services && ctx->services->host_query_windows_registry) {
        return ctx->services->host_query_windows_registry(ctx->services->user_data,
                                                          eval_temp_arena(ctx),
                                                          request,
                                                          out_result);
    }

#if !defined(_WIN32)
    out_result->error_message = nob_sv_from_cstr("Windows registry queries are unavailable on this platform");
    return true;
#else
    HKEY root = NULL;
    String_View subkey = nob_sv_from_cstr("");
    if (!eval_windows_registry_parse_root(request->key, &root, &subkey)) {
        out_result->error_message = nob_sv_from_cstr("Invalid Windows registry root key");
        return true;
    }

    REGSAM view_flags[2] = {0};
    size_t view_count = eval_windows_registry_collect_views(request->view, view_flags);
    if (view_count == 0) {
        out_result->error_message = nob_sv_from_cstr("Invalid Windows registry view");
        return true;
    }

    if (request->kind == EVAL_WINDOWS_REGISTRY_QUERY_VALUE) {
        for (size_t i = 0; i < view_count; i++) {
            String_View value = nob_sv_from_cstr("");
            bool found = false;
            if (!eval_windows_registry_query_value_temp(ctx,
                                                        root,
                                                        subkey,
                                                        view_flags[i],
                                                        request->value_name,
                                                        request->separator,
                                                        &value,
                                                        &found)) {
                return false;
            }
            if (found) {
                out_result->found = true;
                out_result->value = value;
                return true;
            }
        }
        return true;
    }

    SV_List items = NULL;
    for (size_t i = 0; i < view_count; i++) {
        if (!eval_windows_registry_enumerate_temp(ctx,
                                                  root,
                                                  subkey,
                                                  view_flags[i],
                                                  request->kind,
                                                  &items)) {
            return false;
        }
    }

    out_result->found = arena_arr_len(items) > 0;
    out_result->value = eval_sv_join_semi_temp(ctx, items, arena_arr_len(items));
    if (eval_should_stop(ctx)) return false;
    return true;
#endif
}

bool eval_mkdirs_for_parent(EvalExecContext *ctx, String_View path) {
    if (!ctx) return false;
    String_View parent = eval_file_parent_dir_view(path);
    if (parent.count == 0 || nob_sv_eq(parent, nob_sv_from_cstr("."))) return true;

    char *tmp = eval_sv_to_cstr_temp(ctx, parent);
    EVAL_OOM_RETURN_IF_NULL(ctx, tmp, false);
    size_t len0 = strlen(tmp);
    for (size_t i = 0; i < len0; i++) {
        if (tmp[i] == '\\') tmp[i] = '/';
    }

    while (len0 > 0 && tmp[len0 - 1] == '/') {
        tmp[len0 - 1] = '\0';
        len0--;
    }
    if (len0 == 0) return true;

    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        if ((p == tmp + 2) && isalpha((unsigned char)tmp[0]) && tmp[1] == ':') continue;
        *p = '\0';
        (void)eval_service_mkdir(ctx, nob_sv_from_cstr(tmp));
        *p = '/';
    }
    return eval_service_mkdir(ctx, nob_sv_from_cstr(tmp));
}

bool eval_write_text_file(EvalExecContext *ctx, String_View path, String_View contents, bool append) {
    if (!ctx) return false;
    if (!eval_mkdirs_for_parent(ctx, path)) return false;
    return eval_service_write_file(ctx, path, contents, append);
}

bool eval_ctest_publish_metadata(EvalExecContext *ctx, String_View command_name, const SV_List *argv, String_View status) {
    if (!ctx || command_name.count == 0 || !argv) return false;

    String_View joined = eval_sv_join_semi_temp(ctx, *argv, arena_arr_len(*argv));
    if (eval_should_stop(ctx)) return false;

    size_t args_key_len = sizeof("NOBIFY_CTEST::") - 1 + command_name.count + sizeof("::ARGS") - 1;
    size_t status_key_len = sizeof("NOBIFY_CTEST::") - 1 + command_name.count + sizeof("::STATUS") - 1;

    char *args_key = (char*)arena_alloc(eval_temp_arena(ctx), args_key_len + 1);
    char *status_key = (char*)arena_alloc(eval_temp_arena(ctx), status_key_len + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, args_key, false);
    EVAL_OOM_RETURN_IF_NULL(ctx, status_key, false);

    int args_n = snprintf(args_key,
                          args_key_len + 1,
                          "NOBIFY_CTEST::%.*s::ARGS",
                          (int)command_name.count,
                          command_name.data ? command_name.data : "");
    int status_n = snprintf(status_key,
                            status_key_len + 1,
                            "NOBIFY_CTEST::%.*s::STATUS",
                            (int)command_name.count,
                            command_name.data ? command_name.data : "");
    if (args_n < 0 || status_n < 0) return ctx_oom(ctx);

    if (!eval_var_set_current(ctx, nob_sv_from_cstr("NOBIFY_CTEST_LAST_COMMAND"), command_name)) return false;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr(args_key), joined)) return false;
    if (!eval_var_set_current(ctx, nob_sv_from_cstr(status_key), status)) return false;
    return true;
}

bool eval_legacy_publish_args(EvalExecContext *ctx, String_View command_name, const SV_List *argv) {
    if (!ctx || command_name.count == 0 || !argv) return false;

    String_View joined = eval_sv_join_semi_temp(ctx, *argv, arena_arr_len(*argv));
    if (eval_should_stop(ctx)) return false;

    size_t key_len = sizeof("NOBIFY_LEGACY::") - 1 + command_name.count + sizeof("::ARGS") - 1;
    char *key = (char*)arena_alloc(eval_temp_arena(ctx), key_len + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, key, false);

    int n = snprintf(key,
                     key_len + 1,
                     "NOBIFY_LEGACY::%.*s::ARGS",
                     (int)command_name.count,
                     command_name.data ? command_name.data : "");
    if (n < 0) return ctx_oom(ctx);

    return eval_var_set_current(ctx, nob_sv_from_cstr(key), joined);
}

static String_View eval_test_global_marker_key_temp(EvalExecContext *ctx, String_View test_name) {
    if (!ctx || test_name.count == 0) return nob_sv_from_cstr("");
    size_t prefix_len = strlen("NOBIFY_TEST::");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), prefix_len + test_name.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    memcpy(buf, "NOBIFY_TEST::", prefix_len);
    memcpy(buf + prefix_len, test_name.data, test_name.count);
    buf[prefix_len + test_name.count] = '\0';
    return nob_sv_from_cstr(buf);
}

String_View eval_test_scoped_marker_key_temp(EvalExecContext *ctx,
                                             String_View scope_dir,
                                             String_View test_name) {
    if (!ctx || test_name.count == 0) return nob_sv_from_cstr("");
    if (scope_dir.count == 0) scope_dir = eval_current_source_dir_for_paths(ctx);
    scope_dir = eval_sv_path_normalize_temp(ctx, scope_dir);
    if (eval_should_stop(ctx)) return nob_sv_from_cstr("");

    size_t prefix_len = strlen("NOBIFY_TEST::DIRECTORY::");
    size_t total = prefix_len + scope_dir.count + 2 + test_name.count;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    memcpy(buf + off, "NOBIFY_TEST::DIRECTORY::", prefix_len);
    off += prefix_len;
    if (scope_dir.count > 0) {
        memcpy(buf + off, scope_dir.data, scope_dir.count);
        off += scope_dir.count;
    }
    buf[off++] = ':';
    buf[off++] = ':';
    memcpy(buf + off, test_name.data, test_name.count);
    off += test_name.count;
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

bool eval_test_exists_in_directory_scope(EvalExecContext *ctx, String_View test_name, String_View scope_dir) {
    if (!ctx || test_name.count == 0) return false;

    if (scope_dir.count == 0) scope_dir = eval_current_source_dir_for_paths(ctx);
    scope_dir = eval_sv_path_normalize_temp(ctx, scope_dir);
    if (eval_should_stop(ctx)) return false;
    if (eval_test_known_in_directory(ctx, test_name, scope_dir)) return true;

    String_View scoped_key = eval_test_scoped_marker_key_temp(ctx, scope_dir, test_name);
    if (eval_should_stop(ctx)) return false;
    if (eval_var_defined_visible(ctx, scoped_key)) return true;

    String_View global_key = eval_test_global_marker_key_temp(ctx, test_name);
    if (eval_should_stop(ctx)) return false;
    if (!eval_var_defined_visible(ctx, global_key)) return false;

    String_View current_dir = eval_current_source_dir_for_paths(ctx);
    current_dir = eval_sv_path_normalize_temp(ctx, current_dir);
    if (eval_should_stop(ctx)) return false;
    return svu_eq_ci_sv(scope_dir, current_dir);
}

static bool eval_semver_parse_component(String_View sv, int *out_value) {
    if (!out_value || sv.count == 0) return false;
    long long acc = 0;
    for (size_t i = 0; i < sv.count; i++) {
        if (sv.data[i] < '0' || sv.data[i] > '9') return false;
        acc = (acc * 10) + (long long)(sv.data[i] - '0');
        if (acc > INT_MAX) return false;
    }
    *out_value = (int)acc;
    return true;
}

bool eval_semver_parse_strict(String_View version_token, Eval_Semver *out_version) {
    if (!out_version || version_token.count == 0) return false;

    int values[4] = {0, 0, 0, 0};
    size_t value_count = 0;
    size_t pos = 0;
    while (pos < version_token.count) {
        size_t start = pos;
        while (pos < version_token.count && version_token.data[pos] != '.') pos++;
        if (value_count >= 4) return false;
        String_View part = nob_sv_from_parts(version_token.data + start, pos - start);
        if (!eval_semver_parse_component(part, &values[value_count])) return false;
        value_count++;
        if (pos == version_token.count) break;
        pos++;
        if (pos == version_token.count) return false;
    }

    if (value_count < 2 || value_count > 4) return false;
    out_version->major = values[0];
    out_version->minor = values[1];
    out_version->patch = values[2];
    out_version->tweak = values[3];
    return true;
}

int eval_semver_compare(const Eval_Semver *lhs, const Eval_Semver *rhs) {
    if (!lhs || !rhs) return 0;
    if (lhs->major != rhs->major) return (lhs->major < rhs->major) ? -1 : 1;
    if (lhs->minor != rhs->minor) return (lhs->minor < rhs->minor) ? -1 : 1;
    if (lhs->patch != rhs->patch) return (lhs->patch < rhs->patch) ? -1 : 1;
    if (lhs->tweak != rhs->tweak) return (lhs->tweak < rhs->tweak) ? -1 : 1;
    return 0;
}

String_View eval_sv_join_semi_temp(EvalExecContext *ctx, String_View *items, size_t count) {
    if (!ctx || count == 0) return nob_sv_from_cstr("");

    size_t total = 0;
    for (size_t i = 0; i < count; i++) total += items[i].count;
    total += (count - 1);

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    for (size_t i = 0; i < count; i++) {
        if (i) buf[off++] = ';';
        if (items[i].count) {
            memcpy(buf + off, items[i].data, items[i].count);
            off += items[i].count;
        }
    }
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

bool eval_sv_split_semicolon_genex_aware(Arena *arena, String_View input, SV_List *out) {
    if (!arena || !out) return false;
    if (input.count == 0) return true;

    size_t start = 0;
    size_t genex_depth = 0;
    for (size_t i = 0; i < input.count; i++) {
        if (input.data[i] == '$' && (i + 1) < input.count && input.data[i + 1] == '<') {
            genex_depth++;
            i++;
            continue;
        }
        if (input.data[i] == '>' && genex_depth > 0) {
            genex_depth--;
            continue;
        }
        if (input.data[i] == ';' && genex_depth == 0) {
            size_t backslash_count = 0;
            for (size_t j = i; j > 0 && input.data[j - 1] == '\\'; j--) backslash_count++;
            if ((backslash_count % 2) == 1) continue;
            String_View item = nob_sv_from_parts(input.data + start, i - start);
            if (!arena_arr_push(arena, *out, item)) return false;
            start = i + 1;
        }
    }

    if (start < input.count) {
        String_View item = nob_sv_from_parts(input.data + start, input.count - start);
        if (!arena_arr_push(arena, *out, item)) return false;
    }
    return true;
}
