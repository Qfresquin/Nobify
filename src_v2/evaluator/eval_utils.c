#include "evaluator_internal.h"
#include "arena_dyn.h"
#include "subprocess.h"
#include "tinydir.h"
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

char *eval_sv_to_cstr_temp(Evaluator_Context *ctx, String_View sv) {
    if (!ctx) return NULL;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), sv.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, NULL);
    if (sv.count) memcpy(buf, sv.data, sv.count);
    buf[sv.count] = '\0';
    return buf;
}

bool eval_emit_event(Evaluator_Context *ctx, Cmake_Event ev) {
    if (!ctx) return false;
    if (!event_stream_push(eval_event_arena(ctx), ctx->stream, ev)) {
        return ctx_oom(ctx);
    }
    return true;
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

String_View eval_current_source_dir_for_paths(Evaluator_Context *ctx) {
    String_View cur_src = eval_var_get(ctx, nob_sv_from_cstr("CMAKE_CURRENT_SOURCE_DIR"));
    if (cur_src.count == 0 && ctx) cur_src = ctx->source_dir;
    return cur_src;
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
static String_View host_copy_printf_temp(Evaluator_Context *ctx, const char *fmt, ...) {
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

bool eval_host_hostname_temp(Evaluator_Context *ctx, String_View *out_hostname) {
    if (!out_hostname) return false;
    *out_hostname = nob_sv_from_cstr("");

#if defined(_WIN32)
    char buf[256] = {0};
    DWORD size = (DWORD)(sizeof(buf) - 1);
    if (!GetComputerNameA(buf, &size)) return true;
    *out_hostname = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(buf, (size_t)size));
    return !eval_should_stop(ctx);
#else
    char buf[256] = {0};
    if (gethostname(buf, sizeof(buf) - 1) != 0) return true;
    buf[sizeof(buf) - 1] = '\0';
    *out_hostname = sv_copy_to_temp_arena(ctx, nob_sv_from_cstr(buf));
    return !eval_should_stop(ctx);
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

String_View eval_host_os_release_temp(Evaluator_Context *ctx) {
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

String_View eval_host_os_version_temp(Evaluator_Context *ctx) {
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

String_View eval_property_upper_name_temp(Evaluator_Context *ctx, String_View name) {
    if (!ctx) return nob_sv_from_cstr("");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), name.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    for (size_t i = 0; i < name.count; i++) {
        buf[i] = (char)toupper((unsigned char)name.data[i]);
    }
    buf[name.count] = '\0';
    return nob_sv_from_cstr(buf);
}

bool eval_property_scope_upper_temp(Evaluator_Context *ctx, String_View raw_scope, String_View *out_scope_upper) {
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

String_View eval_property_store_key_temp(Evaluator_Context *ctx,
                                         String_View scope_upper,
                                         String_View object_id,
                                         String_View prop_upper) {
    static const char prefix[] = "NOBIFY_PROPERTY_";
    if (!ctx) return nob_sv_from_cstr("");

    bool has_obj = object_id.count > 0;
    size_t total = (sizeof(prefix) - 1) + scope_upper.count + 2 + prop_upper.count;
    if (has_obj) total += 2 + object_id.count;

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    memcpy(buf + off, prefix, sizeof(prefix) - 1);
    off += sizeof(prefix) - 1;
    memcpy(buf + off, scope_upper.data, scope_upper.count);
    off += scope_upper.count;
    buf[off++] = ':';
    buf[off++] = ':';
    if (has_obj) {
        memcpy(buf + off, object_id.data, object_id.count);
        off += object_id.count;
        buf[off++] = ':';
        buf[off++] = ':';
    }
    memcpy(buf + off, prop_upper.data, prop_upper.count);
    off += prop_upper.count;
    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

String_View eval_property_scoped_object_id_temp(Evaluator_Context *ctx,
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

const Eval_Property_Definition *eval_property_definition_find(Evaluator_Context *ctx,
                                                              String_View scope_upper,
                                                              String_View property_name) {
    if (!ctx) return NULL;
    String_View property_upper = eval_property_upper_name_temp(ctx, property_name);
    if (eval_should_stop(ctx)) return NULL;

    for (size_t i = 0; i < ctx->property_definitions.count; i++) {
        const Eval_Property_Definition *def = &ctx->property_definitions.items[i];
        if (!eval_sv_key_eq(def->scope_upper, scope_upper)) continue;
        if (!eval_sv_key_eq(def->property_upper, property_upper)) continue;
        return def;
    }

    if (eval_sv_eq_ci_lit(scope_upper, "CACHE")) {
        String_View cached_scope = nob_sv_from_cstr("CACHED_VARIABLE");
        for (size_t i = 0; i < ctx->property_definitions.count; i++) {
            const Eval_Property_Definition *def = &ctx->property_definitions.items[i];
            if (!eval_sv_key_eq(def->scope_upper, cached_scope)) continue;
            if (!eval_sv_key_eq(def->property_upper, property_upper)) continue;
            return def;
        }
    }

    return NULL;
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

static bool eval_path_norm_eq_temp(Evaluator_Context *ctx, String_View a, String_View b) {
    String_View an = eval_sv_path_normalize_temp(ctx, a);
    if (eval_should_stop(ctx)) return false;
    String_View bn = eval_sv_path_normalize_temp(ctx, b);
    if (eval_should_stop(ctx)) return false;
    return svu_eq_ci_sv(an, bn);
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

bool eval_list_dir_sources_sorted_temp(Evaluator_Context *ctx, String_View dir, SV_List *out_sources) {
    if (!ctx || !out_sources) return false;
    *out_sources = (SV_List){0};

    char *dir_c = eval_sv_to_cstr_temp(ctx, dir);
    EVAL_OOM_RETURN_IF_NULL(ctx, dir_c, false);

    tinydir_dir td = {0};
    if (tinydir_open_sorted(&td, dir_c) != 0) return true;

    for (size_t i = 0; i < td.n_files; i++) {
        tinydir_file tf = {0};
        if (tinydir_readfile_n(&td, &tf, i) != 0) continue;
        if (tf.is_dir) continue;

        String_View name = nob_sv_from_cstr(tf.name);
        if (!eval_source_extension_allowed(name)) continue;
        String_View full = eval_sv_path_join(eval_temp_arena(ctx), dir, name);
        if (eval_should_stop(ctx)) {
            tinydir_close(&td);
            return false;
        }
        if (!svu_list_push_temp(ctx, out_sources, full)) {
            tinydir_close(&td);
            return false;
        }
    }

    tinydir_close(&td);
    return true;
}

bool eval_mkdirs_for_parent(Evaluator_Context *ctx, String_View path) {
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
        (void)nob_mkdir_if_not_exists(tmp);
        *p = '/';
    }
    return nob_mkdir_if_not_exists(tmp);
}

bool eval_write_text_file(Evaluator_Context *ctx, String_View path, String_View contents, bool append) {
    if (!ctx) return false;
    if (!eval_mkdirs_for_parent(ctx, path)) return false;

    char *path_c = eval_sv_to_cstr_temp(ctx, path);
    EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);

    if (!append) {
        return nob_write_entire_file(path_c, contents.data ? contents.data : "", contents.count);
    }

    Nob_String_Builder sb = {0};
    if (nob_file_exists(path_c)) {
        if (!nob_read_entire_file(path_c, &sb)) return false;
    }
    if (contents.count > 0) nob_sb_append_buf(&sb, contents.data, contents.count);
    return nob_write_entire_file(path_c, sb.items ? sb.items : "", sb.count);
}

bool eval_ctest_publish_metadata(Evaluator_Context *ctx, String_View command_name, const SV_List *argv, String_View status) {
    if (!ctx || command_name.count == 0 || !argv) return false;

    String_View joined = eval_sv_join_semi_temp(ctx, argv->items, argv->count);
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

    if (!eval_var_set(ctx, nob_sv_from_cstr("NOBIFY_CTEST_LAST_COMMAND"), command_name)) return false;
    if (!eval_var_set(ctx, nob_sv_from_cstr(args_key), joined)) return false;
    if (!eval_var_set(ctx, nob_sv_from_cstr(status_key), status)) return false;
    return true;
}

bool eval_test_exists_in_directory_scope(Evaluator_Context *ctx, String_View test_name, String_View scope_dir) {
    if (!ctx || !ctx->stream || test_name.count == 0) return false;
    for (size_t ei = 0; ei < ctx->stream->count; ei++) {
        const Cmake_Event *ev = &ctx->stream->items[ei];
        if (ev->kind != EV_TEST_ADD) continue;
        if (!nob_sv_eq(ev->as.test_add.name, test_name)) continue;
        String_View ev_dir = eval_file_parent_dir_view(ev->origin.file_path);
        if (eval_path_norm_eq_temp(ctx, ev_dir, scope_dir)) return true;
        if (eval_should_stop(ctx)) return false;
    }
    return false;
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

String_View eval_sv_join_semi_temp(Evaluator_Context *ctx, String_View *items, size_t count) {
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
            String_View item = nob_sv_from_parts(input.data + start, i - start);
            if (!arena_da_reserve(arena, (void**)&out->items, &out->capacity, sizeof(out->items[0]), out->count + 1)) {
                return false;
            }
            out->items[out->count++] = item;
            start = i + 1;
        }
    }

    if (start < input.count) {
        String_View item = nob_sv_from_parts(input.data + start, input.count - start);
        if (!arena_da_reserve(arena, (void**)&out->items, &out->capacity, sizeof(out->items[0]), out->count + 1)) {
            return false;
        }
        out->items[out->count++] = item;
    }
    return true;
}

bool eval_split_shell_like_temp(Evaluator_Context *ctx, String_View input, SV_List *out) {
    if (!ctx || !out) return false;

    size_t i = 0;
    while (i < input.count) {
        while (i < input.count && isspace((unsigned char)input.data[i])) i++;
        if (i >= input.count) break;

        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), input.count + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);

        size_t off = 0;
        bool touched = false;
        char quote = '\0';
        while (i < input.count) {
            char c = input.data[i];
            if (quote != '\0') {
                if (c == quote) {
                    quote = '\0';
                    touched = true;
                    i++;
                    continue;
                }
                if (c == '\\' && quote == '"' && i + 1 < input.count) {
                    buf[off++] = input.data[i + 1];
                    touched = true;
                    i += 2;
                    continue;
                }
                buf[off++] = c;
                touched = true;
                i++;
                continue;
            }

            if (isspace((unsigned char)c)) break;
            if (c == '"' || c == '\'') {
                quote = c;
                touched = true;
                i++;
                continue;
            }
            if (c == '\\' && i + 1 < input.count) {
                buf[off++] = input.data[i + 1];
                touched = true;
                i += 2;
                continue;
            }
            buf[off++] = c;
            touched = true;
            i++;
        }

        buf[off] = '\0';
        if (touched) {
            if (!svu_list_push_temp(ctx, out, nob_sv_from_cstr(buf))) return false;
        }
    }

    return true;
}

static bool eval_split_windows_command_temp(Evaluator_Context *ctx, String_View input, SV_List *out) {
    if (!ctx || !out) return false;

    size_t i = 0;
    while (i < input.count) {
        while (i < input.count && isspace((unsigned char)input.data[i])) i++;
        if (i >= input.count) break;

        char *buf = (char*)arena_alloc(eval_temp_arena(ctx), input.count + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);

        size_t off = 0;
        bool in_quotes = false;
        while (i < input.count) {
            char c = input.data[i];
            if (!in_quotes && isspace((unsigned char)c)) break;

            if (c == '\\') {
                size_t slash_count = 0;
                while (i + slash_count < input.count && input.data[i + slash_count] == '\\') slash_count++;

                bool next_is_quote = (i + slash_count < input.count && input.data[i + slash_count] == '"');
                if (next_is_quote) {
                    size_t literal_slashes = slash_count / 2;
                    for (size_t si = 0; si < literal_slashes; si++) buf[off++] = '\\';
                    if ((slash_count % 2) == 0) {
                        in_quotes = !in_quotes;
                    } else {
                        buf[off++] = '"';
                    }
                    i += slash_count + 1;
                    continue;
                }

                for (size_t si = 0; si < slash_count; si++) buf[off++] = '\\';
                i += slash_count;
                continue;
            }

            if (c == '"') {
                if (in_quotes && i + 1 < input.count && input.data[i + 1] == '"') {
                    buf[off++] = '"';
                    i += 2;
                    continue;
                }
                in_quotes = !in_quotes;
                i++;
                continue;
            }

            buf[off++] = c;
            i++;
        }

        buf[off] = '\0';
        if (!svu_list_push_temp(ctx, out, nob_sv_from_parts(buf, off))) return false;
    }

    return true;
}

bool eval_split_command_line_temp(Evaluator_Context *ctx,
                                  Eval_Cmdline_Mode mode,
                                  String_View input,
                                  SV_List *out_tokens) {
    if (!ctx || !out_tokens) return false;

    if (mode == EVAL_CMDLINE_NATIVE) {
#if defined(_WIN32)
        mode = EVAL_CMDLINE_WINDOWS;
#else
        mode = EVAL_CMDLINE_UNIX;
#endif
    }

    if (mode == EVAL_CMDLINE_WINDOWS) return eval_split_windows_command_temp(ctx, input, out_tokens);
    return eval_split_shell_like_temp(ctx, input, out_tokens);
}

static double eval_process_now_seconds(void) {
    struct timespec ts = {0};
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC) return 0.0;
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static void eval_process_sleep_millis(unsigned millis) {
#if defined(_WIN32)
    Sleep(millis);
#else
    usleep((useconds_t)millis * 1000u);
#endif
}

static String_View eval_process_sb_to_owned_sv(Evaluator_Context *ctx, Nob_String_Builder *sb) {
    if (!ctx || !sb || sb->count == 0) return nob_sv_from_cstr("");
    char *copy = arena_strndup(ctx->arena, sb->items, sb->count);
    EVAL_OOM_RETURN_IF_NULL(ctx, copy, nob_sv_from_cstr(""));
    return nob_sv_from_parts(copy, sb->count);
}

bool eval_process_run_capture(Evaluator_Context *ctx,
                              const Eval_Process_Run_Request *req,
                              Eval_Process_Run_Result *out) {
    if (!ctx || !req || !out || req->argv.count == 0) return false;

    *out = (Eval_Process_Run_Result){
        .result_text = nob_sv_from_cstr("1"),
    };

    const char **argv = arena_alloc_array(ctx->arena, const char *, req->argv.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, argv, false);
    for (size_t i = 0; i < req->argv.count; i++) {
        argv[i] = eval_sv_to_cstr_temp(ctx, req->argv.items[i]);
        EVAL_OOM_RETURN_IF_NULL(ctx, argv[i], false);
    }
    argv[req->argv.count] = NULL;

    bool changed_cwd = false;
    char old_cwd[4096] = {0};
    if (req->working_directory.count > 0) {
        const char *cwd_c = eval_sv_to_cstr_temp(ctx, req->working_directory);
        EVAL_OOM_RETURN_IF_NULL(ctx, cwd_c, false);
#if defined(_WIN32)
        if (!_getcwd(old_cwd, sizeof(old_cwd))) {
            out->result_text = nob_sv_from_cstr("failed to capture working directory");
            return true;
        }
        if (_chdir(cwd_c) != 0) {
            out->result_text = nob_sv_from_cstr("failed to enter WORKING_DIRECTORY");
            return true;
        }
#else
        if (!getcwd(old_cwd, sizeof(old_cwd))) {
            out->result_text = nob_sv_from_cstr("failed to capture working directory");
            return true;
        }
        if (chdir(cwd_c) != 0) {
            out->result_text = nob_sv_from_cstr("failed to enter WORKING_DIRECTORY");
            return true;
        }
#endif
        changed_cwd = true;
    }

    struct subprocess_s proc = {0};
    int options = subprocess_option_inherit_environment |
                  subprocess_option_search_user_path |
                  subprocess_option_enable_async;
#if defined(_WIN32)
    options |= subprocess_option_no_window;
#endif
    if (subprocess_create(argv, options, &proc) != 0) {
        if (changed_cwd) {
#if defined(_WIN32)
            if (_chdir(old_cwd) != 0) {
            }
#else
            if (chdir(old_cwd) != 0) {
            }
#endif
        }
        out->result_text = nob_sv_from_cstr("process failed to start");
        return true;
    }
    out->started = true;

    if (changed_cwd) {
#if defined(_WIN32)
        if (_chdir(old_cwd) != 0) {
            (void)subprocess_terminate(&proc);
            (void)subprocess_join(&proc, NULL);
            (void)subprocess_destroy(&proc);
            out->result_text = nob_sv_from_cstr("failed to restore working directory");
            out->started = false;
            return true;
        }
#else
        if (chdir(old_cwd) != 0) {
            (void)subprocess_terminate(&proc);
            (void)subprocess_join(&proc, NULL);
            (void)subprocess_destroy(&proc);
            out->result_text = nob_sv_from_cstr("failed to restore working directory");
            out->started = false;
            return true;
        }
#endif
    }

    FILE *child_stdin = subprocess_stdin(&proc);
    if (child_stdin) {
        if (req->stdin_data.count > 0) {
            size_t written = fwrite(req->stdin_data.data, 1, req->stdin_data.count, child_stdin);
            if (written != req->stdin_data.count) {
                fclose(child_stdin);
                proc.stdin_file = NULL;
                (void)subprocess_terminate(&proc);
                (void)subprocess_join(&proc, NULL);
                (void)subprocess_destroy(&proc);
                out->result_text = nob_sv_from_cstr("failed to write process input");
                out->started = false;
                return true;
            }
        }
        fclose(child_stdin);
        proc.stdin_file = NULL;
    }

    Nob_String_Builder out_sb = {0};
    Nob_String_Builder err_sb = {0};
    double deadline = 0.0;
    if (req->has_timeout) {
        deadline = eval_process_now_seconds() + req->timeout_seconds;
    }

    for (;;) {
        char buf[512];
        unsigned n_out = subprocess_read_stdout(&proc, buf, sizeof(buf));
        if (n_out > 0) {
            nob_sb_append_buf(&out_sb, buf, (size_t)n_out);
        }

        unsigned n_err = subprocess_read_stderr(&proc, buf, sizeof(buf));
        if (n_err > 0) {
            nob_sb_append_buf(&err_sb, buf, (size_t)n_err);
        }

        if (ctx->oom) {
            nob_sb_free(out_sb);
            nob_sb_free(err_sb);
            (void)subprocess_terminate(&proc);
            (void)subprocess_join(&proc, NULL);
            (void)subprocess_destroy(&proc);
            return false;
        }

        int alive = subprocess_alive(&proc);
        if (req->has_timeout && alive && eval_process_now_seconds() >= deadline) {
            out->timed_out = true;
            (void)subprocess_terminate(&proc);
            alive = subprocess_alive(&proc);
        }

        if (!alive && n_out == 0 && n_err == 0) break;
        if (n_out == 0 && n_err == 0) eval_process_sleep_millis(10);
    }

    int exit_code = 1;
    if (subprocess_join(&proc, &exit_code) != 0) {
        nob_sb_free(out_sb);
        nob_sb_free(err_sb);
        (void)subprocess_destroy(&proc);
        out->result_text = nob_sv_from_cstr("failed to wait for process");
        out->started = false;
        return true;
    }
    (void)subprocess_destroy(&proc);

    out->exit_code = exit_code;
    out->stdout_text = eval_process_sb_to_owned_sv(ctx, &out_sb);
    out->stderr_text = eval_process_sb_to_owned_sv(ctx, &err_sb);
    nob_sb_free(out_sb);
    nob_sb_free(err_sb);
    if (eval_should_stop(ctx)) return false;

    if (out->timed_out) {
        out->result_text = nob_sv_from_cstr("Process terminated due to timeout");
    } else {
        char *result_buf = arena_alloc(ctx->arena, 32);
        EVAL_OOM_RETURN_IF_NULL(ctx, result_buf, false);
        int n = snprintf(result_buf, 32, "%d", exit_code);
        if (n < 0 || n >= 32) return ctx_oom(ctx);
        out->result_text = nob_sv_from_parts(result_buf, (size_t)n);
    }

    return true;
}

bool eval_sv_is_abs_path(String_View p) {
    if (p.count == 0) return false;
    if ((p.count >= 2) &&
        (p.data[0] == '/' || p.data[0] == '\\') &&
        (p.data[1] == '/' || p.data[1] == '\\')) {
        return true; // UNC/network path: //server/share or \\server\share
    }
    if (p.count > 1 && p.data[1] == ':') return true;
    if (p.data[0] == '/' || p.data[0] == '\\') return true;
    return false;
}

String_View eval_sv_path_join(Arena *arena, String_View a, String_View b) {
    if (!arena) return nob_sv_from_cstr("");
    if (a.count == 0) return sv_copy_to_arena(arena, b);
    if (b.count == 0) return sv_copy_to_arena(arena, a);

    bool need_slash = !svu_is_path_sep(a.data[a.count - 1]);
    size_t total = a.count + (need_slash ? 1 : 0) + b.count;

    char *buf = (char*)arena_alloc(arena, total + 1);
    if (!buf) return nob_sv_from_cstr("");

    size_t off = 0;
    memcpy(buf + off, a.data, a.count);
    off += a.count;
    if (need_slash) buf[off++] = '/';
    memcpy(buf + off, b.data, b.count);
    off += b.count;
    buf[off] = '\0';

    return nob_sv_from_cstr(buf);
}

String_View eval_sv_path_normalize_temp(Evaluator_Context *ctx, String_View input) {
    if (!ctx) return nob_sv_from_cstr("");
    if (input.count == 0) return nob_sv_from_cstr(".");

    bool is_unc = input.count >= 2 && svu_is_path_sep(input.data[0]) && svu_is_path_sep(input.data[1]);
    bool has_drive = input.count >= 2 &&
                     isalpha((unsigned char)input.data[0]) &&
                     input.data[1] == ':';
    bool absolute = false;
    size_t pos = 0;

    if (is_unc) {
        pos = 2;
        while (pos < input.count && svu_is_path_sep(input.data[pos])) pos++;
    } else if (has_drive) {
        pos = 2;
        if (pos < input.count && svu_is_path_sep(input.data[pos])) {
            absolute = true;
            while (pos < input.count && svu_is_path_sep(input.data[pos])) pos++;
        }
    } else if (svu_is_path_sep(input.data[0])) {
        absolute = true;
        while (pos < input.count && svu_is_path_sep(input.data[pos])) pos++;
    }

    SV_List segments = {0};
    size_t unc_root_segments = 0;
    while (pos < input.count) {
        size_t start = pos;
        while (pos < input.count && !svu_is_path_sep(input.data[pos])) pos++;
        String_View seg = nob_sv_from_parts(input.data + start, pos - start);
        while (pos < input.count && svu_is_path_sep(input.data[pos])) pos++;

        if (seg.count == 0 || nob_sv_eq(seg, nob_sv_from_cstr("."))) continue;
        if (nob_sv_eq(seg, nob_sv_from_cstr(".."))) {
            if (segments.count > 0 &&
                !nob_sv_eq(segments.items[segments.count - 1], nob_sv_from_cstr("..")) &&
                (!is_unc || segments.count > unc_root_segments)) {
                segments.count--;
                continue;
            }
            if (!absolute) {
                if (!svu_list_push_temp(ctx, &segments, seg)) return nob_sv_from_cstr("");
            }
            continue;
        }

        if (!svu_list_push_temp(ctx, &segments, seg)) return nob_sv_from_cstr("");
        if (is_unc && unc_root_segments < 2) unc_root_segments++;
    }

    size_t total = 0;
    if (is_unc) total += 2;
    else if (has_drive) total += 2;
    if (absolute && !is_unc && !has_drive) total += 1;
    if (absolute && has_drive) total += 1;

    for (size_t i = 0; i < segments.count; i++) {
        if (i > 0 || is_unc || absolute || (has_drive && absolute)) total += 1;
        total += segments.items[i].count;
    }

    if (segments.count == 0) {
        if (is_unc) total += 0;
        else if (has_drive && absolute) {
            if (total == 2) total += 1;
        } else if (!has_drive && !absolute) {
            total += 1;
        }
    }

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    size_t off = 0;

    if (is_unc) {
        buf[off++] = '/';
        buf[off++] = '/';
    } else if (has_drive) {
        buf[off++] = input.data[0];
        buf[off++] = ':';
        if (absolute) buf[off++] = '/';
    } else if (absolute) {
        buf[off++] = '/';
    }

    for (size_t i = 0; i < segments.count; i++) {
        if (off > 0 && buf[off - 1] != '/') buf[off++] = '/';
        memcpy(buf + off, segments.items[i].data, segments.items[i].count);
        off += segments.items[i].count;
    }

    if (segments.count == 0) {
        if (has_drive && absolute) {
            if (off == 2) buf[off++] = '/';
        } else if (!is_unc && !has_drive && !absolute) {
            buf[off++] = '.';
        }
    }

    buf[off] = '\0';
    return nob_sv_from_cstr(buf);
}

String_View eval_path_resolve_for_cmake_arg(Evaluator_Context *ctx,
                                            String_View raw_path,
                                            String_View base_dir,
                                            bool preserve_generator_expressions) {
    if (!ctx) return nob_sv_from_cstr("");
    if (raw_path.count == 0) return nob_sv_from_cstr("");

    if (preserve_generator_expressions &&
        raw_path.count >= 2 &&
        raw_path.data[0] == '$' &&
        raw_path.data[1] == '<') {
        return raw_path;
    }

    String_View resolved = raw_path;
    if (!eval_sv_is_abs_path(resolved)) {
        resolved = eval_sv_path_join(eval_temp_arena(ctx), base_dir, resolved);
    }
    return eval_sv_path_normalize_temp(ctx, resolved);
}

const char *eval_getenv_temp(Evaluator_Context *ctx, const char *name) {
    if (!name || name[0] == '\0') return NULL;

#if defined(_WIN32)
    if (!ctx) return NULL;

    size_t name_len = strlen(name);
    char *lookup_name = (char*)arena_alloc(eval_temp_arena(ctx), name_len + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, lookup_name, NULL);
    for (size_t i = 0; i < name_len; i++) {
        lookup_name[i] = (char)toupper((unsigned char)name[i]);
    }
    lookup_name[name_len] = '\0';

    SetLastError(ERROR_SUCCESS);
    DWORD needed = GetEnvironmentVariableA(lookup_name, NULL, 0);
    if (needed == 0) {
        DWORD err = GetLastError();
        if (err == ERROR_ENVVAR_NOT_FOUND) return NULL;

        if (err == ERROR_SUCCESS) {
            char *empty = (char*)arena_alloc(eval_temp_arena(ctx), 1);
            EVAL_OOM_RETURN_IF_NULL(ctx, empty, NULL);
            empty[0] = '\0';
            return empty;
        }

        return NULL;
    }

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), (size_t)needed);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, NULL);

    DWORD written = GetEnvironmentVariableA(lookup_name, buf, needed);
    if (written >= needed) {
        char *retry = (char*)arena_alloc(eval_temp_arena(ctx), (size_t)written + 1);
        EVAL_OOM_RETURN_IF_NULL(ctx, retry, NULL);
        DWORD retry_written = GetEnvironmentVariableA(lookup_name, retry, written + 1);
        if (retry_written == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) return NULL;
        return retry;
    }

    if (written == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) return NULL;
    return buf;
#else
    (void)ctx;
    return getenv(name);
#endif
}

bool eval_has_env(Evaluator_Context *ctx, const char *name) {
    if (!name || name[0] == '\0') return false;

#if defined(_WIN32)
    if (!ctx) return false;
    const char *lookup_name = name;
    size_t name_len = strlen(name);
    char *arena_name = (char*)arena_alloc(eval_temp_arena(ctx), name_len + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, arena_name, false);
    for (size_t i = 0; i < name_len; i++) {
        arena_name[i] = (char)toupper((unsigned char)name[i]);
    }
    arena_name[name_len] = '\0';
    lookup_name = arena_name;

    SetLastError(ERROR_SUCCESS);
    DWORD needed = GetEnvironmentVariableA(lookup_name, NULL, 0);
    bool exists = (needed > 0) || (GetLastError() == ERROR_SUCCESS);
    return exists;
#else
    (void)ctx;
    return getenv(name) != NULL;
#endif
}
