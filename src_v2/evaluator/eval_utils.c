#include "evaluator_internal.h"
#include "arena_dyn.h"
#include "sv_utils.h"
#include "stb_ds.h"
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#else
#include <unistd.h>
#if defined(__linux__)
#include <sys/sysinfo.h>
#endif
#if defined(__unix__) || defined(__APPLE__) || defined(__linux__)
#include <sys/utsname.h>
#endif
#endif

String_View sv_copy_to_arena(Arena *arena, String_View sv) {
    if (!arena) return nob_sv_from_cstr("");
    if (sv.count == 0 || sv.data == NULL) return nob_sv_from_cstr("");
    char *dup = arena_strndup(arena, sv.data, sv.count);
    if (!dup) return nob_sv_from_cstr("");
    return nob_sv_from_cstr(dup);
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
