#include "eval_host.h"

#include "evaluator_internal.h"
#include "sv_utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

void nob__cmd_append(Nob_Cmd *cmd, size_t n, ...);

typedef struct {
    String_View config;
    String_View parallel_level;
    String_View target;
    String_View project_name;
    bool saw_project_name;
} Build_Command_Options;

typedef struct {
    String_View key;
    String_View value;
} Host_Distrib_Entry;

typedef Host_Distrib_Entry *Host_Distrib_Entry_List;

typedef enum {
    HOST_CPU_FEATURE_FPU = 0,
    HOST_CPU_FEATURE_MMX,
    HOST_CPU_FEATURE_MMX_PLUS,
    HOST_CPU_FEATURE_SSE,
    HOST_CPU_FEATURE_SSE2,
    HOST_CPU_FEATURE_SSE_FP,
    HOST_CPU_FEATURE_SSE_MMX,
    HOST_CPU_FEATURE_AMD_3DNOW,
    HOST_CPU_FEATURE_AMD_3DNOW_PLUS,
    HOST_CPU_FEATURE_IA64,
    HOST_CPU_FEATURE_SERIAL_NUMBER,
} Host_Cpu_Feature;

static String_View host_format_size_t_temp(Evaluator_Context *ctx, size_t value);
static String_View host_format_ull_temp(Evaluator_Context *ctx, unsigned long long value);

static bool host_sv_eq_exact(String_View a, String_View b) {
    if (a.count != b.count) return false;
    if (a.count == 0) return true;
    return memcmp(a.data, b.data, a.count) == 0;
}

static int host_sv_compare_exact(String_View a, String_View b) {
    size_t min_count = a.count < b.count ? a.count : b.count;
    if (min_count > 0) {
        int cmp = memcmp(a.data, b.data, min_count);
        if (cmp != 0) return cmp;
    }
    if (a.count < b.count) return -1;
    if (a.count > b.count) return 1;
    return 0;
}

static String_View host_trim_sv(String_View value) {
    return svu_trim_ascii_ws(value);
}

static String_View host_format_bool_temp(Evaluator_Context *ctx, bool value) {
    return host_format_size_t_temp(ctx, value ? 1u : 0u);
}

static bool host_read_optional_file_temp(Evaluator_Context *ctx,
                                         const char *path,
                                         String_View *out_contents,
                                         bool *out_found) {
    if (!ctx || !path || !out_contents || !out_found) return false;
    *out_contents = nob_sv_from_cstr("");
    *out_found = false;

    FILE *fp = fopen(path, "rb");
    if (!fp) return true;

    *out_found = true;
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

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), (size_t)size + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);

    size_t read_n = fread(buf, 1, (size_t)size, fp);
    bool had_error = ferror(fp) != 0;
    fclose(fp);
    if (read_n < (size_t)size && had_error) return true;

    buf[read_n] = '\0';
    *out_contents = nob_sv_from_parts(buf, read_n);
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
}

static bool host_proc_cpuinfo_temp(Evaluator_Context *ctx, String_View *out_contents, bool *out_found) {
    return host_read_optional_file_temp(ctx, "/proc/cpuinfo", out_contents, out_found);
}

static bool host_parse_long_sv(String_View value, long *out_value) {
    if (!out_value || value.count == 0) return false;

    char buf[64];
    if (value.count >= sizeof(buf)) return false;
    memcpy(buf, value.data, value.count);
    buf[value.count] = '\0';

    char *end = NULL;
    long parsed = strtol(buf, &end, 10);
    if (!end || *end != '\0') return false;
    *out_value = parsed;
    return true;
}

static bool host_proc_cpuinfo_first_value_temp(Evaluator_Context *ctx,
                                               const char *wanted_key,
                                               String_View *out_value) {
    if (!ctx || !wanted_key || !out_value) return false;
    *out_value = nob_sv_from_cstr("");

    String_View contents = nob_sv_from_cstr("");
    bool found = false;
    if (!host_proc_cpuinfo_temp(ctx, &contents, &found)) return false;
    if (!found || contents.count == 0) return true;

    const size_t key_len = strlen(wanted_key);
    size_t start = 0;
    while (start < contents.count) {
        size_t end = start;
        while (end < contents.count && contents.data[end] != '\n') end++;
        String_View line = nob_sv_from_parts(contents.data + start, end - start);
        if (line.count > key_len && memcmp(line.data, wanted_key, key_len) == 0) {
            const char *colon = memchr(line.data, ':', line.count);
            if (colon) {
                size_t off = (size_t)(colon - line.data) + 1;
                *out_value = host_trim_sv(nob_sv_from_parts(line.data + off, line.count - off));
                return true;
            }
        }
        start = end + 1;
    }

    return true;
}

static bool host_proc_cpuinfo_has_flag_temp(Evaluator_Context *ctx,
                                            const char *flag_name,
                                            bool *out_value) {
    if (!ctx || !flag_name || !out_value) return false;
    *out_value = false;

    String_View flags = nob_sv_from_cstr("");
    if (!host_proc_cpuinfo_first_value_temp(ctx, "flags", &flags)) return false;
    if (flags.count == 0 && !host_proc_cpuinfo_first_value_temp(ctx, "Features", &flags)) return false;
    if (flags.count == 0) return true;

    const size_t needle_len = strlen(flag_name);
    size_t start = 0;
    while (start < flags.count) {
        while (start < flags.count && isspace((unsigned char)flags.data[start])) start++;
        size_t end = start;
        while (end < flags.count && !isspace((unsigned char)flags.data[end])) end++;
        if ((end - start) == needle_len &&
            memcmp(flags.data + start, flag_name, needle_len) == 0) {
            *out_value = true;
            return true;
        }
        start = end + 1;
    }
    return true;
}

static bool host_physical_cores_temp(Evaluator_Context *ctx, size_t *out_count) {
    if (!ctx || !out_count) return false;
    *out_count = 0;

#if defined(_WIN32)
    DWORD len = 0;
    (void)GetLogicalProcessorInformation(NULL, &len);
    if (len > 0) {
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION *info =
            (SYSTEM_LOGICAL_PROCESSOR_INFORMATION*)malloc(len);
        if (info) {
            if (GetLogicalProcessorInformation(info, &len)) {
                size_t count = 0;
                size_t item_count = len / sizeof(*info);
                for (size_t i = 0; i < item_count; i++) {
                    if (info[i].Relationship == RelationProcessorCore) count++;
                }
                free(info);
                if (count > 0) {
                    *out_count = count;
                    return true;
                }
            } else {
                free(info);
            }
        }
    }
#elif defined(__APPLE__)
    int value = 0;
    size_t size = sizeof(value);
    if (sysctlbyname("hw.physicalcpu", &value, &size, NULL, 0) == 0 && value > 0) {
        *out_count = (size_t)value;
        return true;
    }
#elif defined(__linux__)
    String_View contents = nob_sv_from_cstr("");
    bool found = false;
    if (!host_proc_cpuinfo_temp(ctx, &contents, &found)) return false;
    if (found && contents.count > 0) {
        typedef struct {
            long physical_id;
            long core_id;
        } Host_Core_Pair;
        Host_Core_Pair *pairs = NULL;
        long socket_ids[256] = {0};
        size_t socket_count = 0;
        long physical_id = -1;
        long core_id = -1;
        long cpu_cores = -1;
        long cpu_cores_first = -1;

        size_t start = 0;
        while (start <= contents.count) {
            size_t end = start;
            while (end < contents.count && contents.data[end] != '\n') end++;
            String_View line = nob_sv_from_parts(contents.data + start, end - start);
            line = host_trim_sv(line);

            if (line.count == 0 || end == contents.count) {
                if (physical_id >= 0 && core_id >= 0) {
                    bool seen = false;
                    for (size_t i = 0; i < arena_arr_len(pairs); i++) {
                        if (pairs[i].physical_id == physical_id && pairs[i].core_id == core_id) {
                            seen = true;
                            break;
                        }
                    }
                    if (!seen) {
                        Host_Core_Pair pair = { physical_id, core_id };
                        if (!arena_arr_push(eval_temp_arena(ctx), pairs, pair)) return false;
                    }
                }
                if (physical_id >= 0) {
                    bool socket_seen = false;
                    for (size_t i = 0; i < socket_count; i++) {
                        if (socket_ids[i] == physical_id) {
                            socket_seen = true;
                            break;
                        }
                    }
                    if (!socket_seen && socket_count < NOB_ARRAY_LEN(socket_ids)) {
                        socket_ids[socket_count++] = physical_id;
                    }
                }
                if (cpu_cores_first < 0 && cpu_cores > 0) cpu_cores_first = cpu_cores;
                physical_id = -1;
                core_id = -1;
                cpu_cores = -1;
                if (end == contents.count) break;
                start = end + 1;
                continue;
            }

            const char *colon = memchr(line.data, ':', line.count);
            if (colon) {
                String_View key = host_trim_sv(nob_sv_from_parts(line.data, (size_t)(colon - line.data)));
                String_View value = host_trim_sv(nob_sv_from_parts(colon + 1,
                                                                   line.count - ((size_t)(colon - line.data) + 1)));
                if (eval_sv_eq_ci_lit(key, "physical id")) (void)host_parse_long_sv(value, &physical_id);
                else if (eval_sv_eq_ci_lit(key, "core id")) (void)host_parse_long_sv(value, &core_id);
                else if (eval_sv_eq_ci_lit(key, "cpu cores")) (void)host_parse_long_sv(value, &cpu_cores);
            }
            start = end + 1;
        }

        if (arena_arr_len(pairs) > 0) {
            *out_count = arena_arr_len(pairs);
            return true;
        }
        if (socket_count > 0 && cpu_cores_first > 0) {
            *out_count = socket_count * (size_t)cpu_cores_first;
            return true;
        }
    }
#endif

    if (!eval_host_logical_cores(out_count)) return false;
    if (*out_count == 0) *out_count = 1;
    return true;
}

static bool host_cpu_feature_temp(Evaluator_Context *ctx,
                                  Host_Cpu_Feature feature,
                                  bool *out_value) {
    if (!out_value) return false;
    *out_value = false;

#if defined(__linux__)
    const char *linux_flag = NULL;
    switch (feature) {
        case HOST_CPU_FEATURE_FPU: linux_flag = "fpu"; break;
        case HOST_CPU_FEATURE_MMX: linux_flag = "mmx"; break;
        case HOST_CPU_FEATURE_MMX_PLUS: linux_flag = "mmxext"; break;
        case HOST_CPU_FEATURE_SSE: linux_flag = "sse"; break;
        case HOST_CPU_FEATURE_SSE2: linux_flag = "sse2"; break;
        case HOST_CPU_FEATURE_SSE_FP: linux_flag = "sse"; break;
        case HOST_CPU_FEATURE_SSE_MMX: linux_flag = "mmx"; break;
        case HOST_CPU_FEATURE_AMD_3DNOW: linux_flag = "3dnow"; break;
        case HOST_CPU_FEATURE_AMD_3DNOW_PLUS: linux_flag = "3dnowext"; break;
        case HOST_CPU_FEATURE_IA64: linux_flag = NULL; break;
        case HOST_CPU_FEATURE_SERIAL_NUMBER: linux_flag = "pn"; break;
    }
    if (linux_flag) return host_proc_cpuinfo_has_flag_temp(ctx, linux_flag, out_value);
#endif

#if (defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__))
    __builtin_cpu_init();
    switch (feature) {
        case HOST_CPU_FEATURE_FPU:
            *out_value = true;
            return true;
        case HOST_CPU_FEATURE_MMX:
            *out_value = __builtin_cpu_supports("mmx");
            return true;
        case HOST_CPU_FEATURE_MMX_PLUS:
            *out_value = __builtin_cpu_supports("mmx");
            return true;
        case HOST_CPU_FEATURE_SSE:
            *out_value = __builtin_cpu_supports("sse");
            return true;
        case HOST_CPU_FEATURE_SSE2:
            *out_value = __builtin_cpu_supports("sse2");
            return true;
        case HOST_CPU_FEATURE_SSE_FP:
            *out_value = __builtin_cpu_supports("sse");
            return true;
        case HOST_CPU_FEATURE_SSE_MMX:
            *out_value = __builtin_cpu_supports("sse") && __builtin_cpu_supports("mmx");
            return true;
        case HOST_CPU_FEATURE_AMD_3DNOW:
            *out_value = __builtin_cpu_supports("3dnow");
            return true;
        case HOST_CPU_FEATURE_AMD_3DNOW_PLUS:
            *out_value = false;
            return true;
        case HOST_CPU_FEATURE_IA64:
            *out_value = false;
            return true;
        case HOST_CPU_FEATURE_SERIAL_NUMBER:
            *out_value = false;
            return true;
    }
#endif

    if (feature == HOST_CPU_FEATURE_IA64) {
#if defined(__ia64__) || defined(_M_IA64)
        *out_value = true;
#else
        *out_value = false;
#endif
        return true;
    }

    if (feature == HOST_CPU_FEATURE_FPU) {
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64) || \
    defined(__arm__) || defined(__aarch64__) || defined(_M_ARM) || defined(_M_ARM64)
        *out_value = true;
#else
        *out_value = false;
#endif
        return true;
    }

    *out_value = false;
    return true;
}

static bool host_processor_name_temp(Evaluator_Context *ctx, String_View *out_value) {
    if (!ctx || !out_value) return false;
    *out_value = nob_sv_from_cstr("");

#if defined(__APPLE__)
    char buf[256] = {0};
    size_t size = sizeof(buf);
    if (sysctlbyname("machdep.cpu.brand_string", buf, &size, NULL, 0) == 0 && size > 1) {
        *out_value = sv_copy_to_temp_arena(ctx, nob_sv_from_cstr(buf));
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }
#elif defined(_WIN32)
    const char *value = eval_getenv_temp(ctx, "PROCESSOR_IDENTIFIER");
    if (value && value[0] != '\0') {
        *out_value = sv_copy_to_temp_arena(ctx, nob_sv_from_cstr(value));
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }
#elif defined(__linux__)
    String_View cpuinfo_name = nob_sv_from_cstr("");
    if (!host_proc_cpuinfo_first_value_temp(ctx, "model name", &cpuinfo_name)) return false;
    if (cpuinfo_name.count == 0 &&
        !host_proc_cpuinfo_first_value_temp(ctx, "Hardware", &cpuinfo_name)) {
        return false;
    }
    if (cpuinfo_name.count > 0) {
        *out_value = cpuinfo_name;
        return true;
    }
#endif

    String_View arch = eval_detect_host_processor();
    *out_value = sv_copy_to_temp_arena(ctx, arch);
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
}

static bool host_processor_description_temp(Evaluator_Context *ctx, String_View *out_value) {
    if (!ctx || !out_value) return false;
    *out_value = nob_sv_from_cstr("");

    String_View name = nob_sv_from_cstr("");
    if (!host_processor_name_temp(ctx, &name)) return false;
    if (name.count == 0) return true;

    size_t physical = 0;
    if (!host_physical_cores_temp(ctx, &physical)) return false;
    if (physical == 0) physical = 1;

    size_t logical = 0;
    if (!eval_host_logical_cores(&logical)) logical = physical;
    if (logical == 0) logical = physical;

    String_View physical_sv = host_format_size_t_temp(ctx, physical);
    String_View logical_sv = host_format_size_t_temp(ctx, logical);
    if (eval_should_stop(ctx)) return false;

    size_t total = physical_sv.count + sizeof(" core ") - 1 + name.count;
    bool append_logical = logical > physical;
    if (append_logical) total += sizeof(" ( logical )") - 1 + logical_sv.count;

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);

    size_t off = 0;
    memcpy(buf + off, physical_sv.data, physical_sv.count);
    off += physical_sv.count;
    memcpy(buf + off, " core ", sizeof(" core ") - 1);
    off += sizeof(" core ") - 1;
    memcpy(buf + off, name.data, name.count);
    off += name.count;
    if (append_logical) {
        memcpy(buf + off, " (", 2);
        off += 2;
        memcpy(buf + off, logical_sv.data, logical_sv.count);
        off += logical_sv.count;
        memcpy(buf + off, " logical)", sizeof(" logical)") - 1);
        off += sizeof(" logical)") - 1;
    }
    buf[off] = '\0';
    *out_value = nob_sv_from_parts(buf, off);
    return true;
}

static bool host_processor_serial_temp(Evaluator_Context *ctx, String_View *out_value) {
    (void)ctx;
    if (!out_value) return false;
    *out_value = nob_sv_from_cstr("");
    return true;
}

static bool host_os_release_path_temp(Evaluator_Context *ctx,
                                      const char *suffix,
                                      String_View *out_path) {
    if (!ctx || !suffix || !out_path) return false;
    String_View sysroot = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_SYSROOT"));
    if (sysroot.count == 0) {
        *out_path = nob_sv_from_cstr(suffix);
        return true;
    }
    const char *trimmed = (suffix[0] == '/') ? suffix + 1 : suffix;
    *out_path = eval_sv_path_join(eval_temp_arena(ctx), sysroot, nob_sv_from_cstr(trimmed));
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
}

static bool host_parse_os_release_line_temp(Evaluator_Context *ctx,
                                            String_View line,
                                            Host_Distrib_Entry *out_entry) {
    if (!ctx || !out_entry) return false;
    out_entry->key = nob_sv_from_cstr("");
    out_entry->value = nob_sv_from_cstr("");

    line = host_trim_sv(line);
    if (line.count == 0 || line.data[0] == '#') return true;

    size_t eq = SIZE_MAX;
    for (size_t i = 0; i < line.count; i++) {
        if (line.data[i] == '=') {
            eq = i;
            break;
        }
    }
    if (eq == SIZE_MAX || eq == 0 || eq + 1 >= line.count) return true;

    String_View key = host_trim_sv(nob_sv_from_parts(line.data, eq));
    String_View raw_value = host_trim_sv(nob_sv_from_parts(line.data + eq + 1, line.count - eq - 1));
    if (key.count == 0 || raw_value.count == 0) return true;

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), raw_value.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);

    size_t off = 0;
    if ((raw_value.data[0] == '"' || raw_value.data[0] == '\'') &&
        raw_value.count >= 2 &&
        raw_value.data[raw_value.count - 1] == raw_value.data[0]) {
        char quote = raw_value.data[0];
        for (size_t i = 1; i + 1 < raw_value.count; i++) {
            char c = raw_value.data[i];
            if (c == '\\' && i + 1 < raw_value.count - 1) {
                char next = raw_value.data[i + 1];
                if (next == quote || next == '\\') {
                    buf[off++] = next;
                    i++;
                    continue;
                }
            }
            buf[off++] = c;
        }
    } else {
        for (size_t i = 0; i < raw_value.count; i++) {
            char c = raw_value.data[i];
            if (c == '#') break;
            if (isspace((unsigned char)c)) break;
            buf[off++] = c;
        }
    }

    buf[off] = '\0';
    out_entry->key = key;
    out_entry->value = nob_sv_from_parts(buf, off);
    return true;
}

static bool host_os_release_entries_temp(Evaluator_Context *ctx,
                                         Host_Distrib_Entry_List *out_entries) {
    if (!ctx || !out_entries) return false;
    *out_entries = NULL;

    static const char *const k_paths[] = {"/etc/os-release", "/usr/lib/os-release"};
    String_View contents = nob_sv_from_cstr("");
    bool found = false;
    for (size_t i = 0; i < NOB_ARRAY_LEN(k_paths); i++) {
        String_View path = nob_sv_from_cstr("");
        if (!host_os_release_path_temp(ctx, k_paths[i], &path)) return false;
        char *path_c = eval_sv_to_cstr_temp(ctx, path);
        EVAL_OOM_RETURN_IF_NULL(ctx, path_c, false);
        if (!host_read_optional_file_temp(ctx, path_c, &contents, &found)) return false;
        if (found) break;
    }
    if (!found || contents.count == 0) return true;

    size_t start = 0;
    while (start < contents.count) {
        size_t end = start;
        while (end < contents.count && contents.data[end] != '\n') end++;
        String_View line = nob_sv_from_parts(contents.data + start, end - start);
        Host_Distrib_Entry entry = {0};
        if (!host_parse_os_release_line_temp(ctx, line, &entry)) return false;
        if (entry.key.count > 0 && !arena_arr_push(eval_temp_arena(ctx), *out_entries, entry)) return false;
        start = end + 1;
    }

    for (size_t i = 1; i < arena_arr_len(*out_entries); i++) {
        Host_Distrib_Entry key = (*out_entries)[i];
        size_t j = i;
        while (j > 0 && host_sv_compare_exact((*out_entries)[j - 1].key, key.key) > 0) {
            (*out_entries)[j] = (*out_entries)[j - 1];
            j--;
        }
        (*out_entries)[j] = key;
    }
    return true;
}

static bool host_distrib_query_value(Evaluator_Context *ctx,
                                     String_View result_var,
                                     String_View key,
                                     String_View *out_value) {
    if (!ctx || !out_value) return false;
    *out_value = nob_sv_from_cstr("");

    if (!eval_sv_eq_ci_lit(key, "DISTRIB_INFO") &&
        !(key.count > 8 && memcmp(key.data, "DISTRIB_", 8) == 0)) {
        return true;
    }

    Host_Distrib_Entry_List entries = NULL;
    if (!host_os_release_entries_temp(ctx, &entries)) return false;
    if (eval_should_stop(ctx)) return false;

    if (eval_sv_eq_ci_lit(key, "DISTRIB_INFO")) {
        SV_List vars = NULL;
        for (size_t i = 0; i < arena_arr_len(entries); i++) {
            size_t total = result_var.count + 1 + entries[i].key.count;
            char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
            EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);
            size_t off = 0;
            memcpy(buf + off, result_var.data, result_var.count);
            off += result_var.count;
            buf[off++] = '_';
            memcpy(buf + off, entries[i].key.data, entries[i].key.count);
            off += entries[i].key.count;
            buf[off] = '\0';

            String_View var_name = nob_sv_from_parts(buf, off);
            if (!eval_var_set_current(ctx, var_name, entries[i].value)) return false;
            if (!arena_arr_push(eval_temp_arena(ctx), vars, var_name)) return false;
        }
        *out_value = eval_sv_join_semi_temp(ctx, vars, arena_arr_len(vars));
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }

    String_View subkey = nob_sv_from_parts(key.data + 8, key.count - 8);
    for (size_t i = 0; i < arena_arr_len(entries); i++) {
        if (!host_sv_eq_exact(entries[i].key, subkey)) continue;
        *out_value = entries[i].value;
        return true;
    }
    return true;
}

static bool host_capture_command_stdout(Evaluator_Context *ctx, String_View command, String_View *out_text) {
    if (!ctx || !out_text) return false;
    *out_text = nob_sv_from_cstr("");
    if (command.count == 0) return true;

    static size_t s_capture_counter = 0;
    s_capture_counter++;

    String_View current_bin = eval_current_binary_dir(ctx);

    String_View file_name = sv_copy_to_temp_arena(
        ctx,
        nob_sv_from_cstr(nob_temp_sprintf("site_name_capture_%zu.txt", s_capture_counter)));
    if (eval_should_stop(ctx)) return false;

    String_View out_path = eval_sv_path_join(eval_temp_arena(ctx), current_bin, file_name);
    if (eval_should_stop(ctx)) return false;

    char *command_c = eval_sv_to_cstr_temp(ctx, command);
    char *out_c = eval_sv_to_cstr_temp(ctx, out_path);
    EVAL_OOM_RETURN_IF_NULL(ctx, command_c, false);
    EVAL_OOM_RETURN_IF_NULL(ctx, out_c, false);

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, command_c);
    bool ok = nob_cmd_run(&cmd, .stdout_path = out_c);
    nob_cmd_free(cmd);
    if (!ok) return true;

    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(out_c, &sb)) return true;

    size_t len = sb.count;
    while (len > 0 && (sb.items[len - 1] == '\n' || sb.items[len - 1] == '\r')) len--;
    *out_text = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb.items, len));
    nob_sb_free(sb);
    return !eval_result_is_fatal(eval_result_from_ctx(ctx));
}

static String_View host_format_size_t_temp(Evaluator_Context *ctx, size_t value) {
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%zu", value);
    if (n < 0 || (size_t)n >= sizeof(buf)) {
        (void)ctx_oom(ctx);
        return nob_sv_from_cstr("");
    }
    return sv_copy_to_temp_arena(ctx, nob_sv_from_parts(buf, (size_t)n));
}

static String_View host_format_ull_temp(Evaluator_Context *ctx, unsigned long long value) {
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%llu", value);
    if (n < 0 || (size_t)n >= sizeof(buf)) {
        (void)ctx_oom(ctx);
        return nob_sv_from_cstr("");
    }
    return sv_copy_to_temp_arena(ctx, nob_sv_from_parts(buf, (size_t)n));
}

static bool host_is_makefile_generator(String_View generator) {
    return eval_sv_eq_ci_lit(generator, "Unix Makefiles") ||
           eval_sv_eq_ci_lit(generator, "MinGW Makefiles") ||
           eval_sv_eq_ci_lit(generator, "MSYS Makefiles") ||
           eval_sv_eq_ci_lit(generator, "NMake Makefiles") ||
           eval_sv_eq_ci_lit(generator, "Watcom WMake") ||
           eval_sv_eq_ci_lit(generator, "Borland Makefiles");
}

static bool host_build_command_parse(Evaluator_Context *ctx,
                                     const Node *node,
                                     const SV_List *args,
                                     String_View *out_var,
                                     Build_Command_Options *out_opt) {
    if (!ctx || !node || !args || !out_var || !out_opt) return false;
    *out_var = nob_sv_from_cstr("");
    memset(out_opt, 0, sizeof(*out_opt));

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    if (arena_arr_len(*args) == 0) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "host", nob_sv_from_cstr("build_command() requires an output variable"), nob_sv_from_cstr("Usage: build_command(<out-var> [CONFIGURATION <cfg>] [TARGET <tgt>])"));
        return false;
    }

    *out_var = (*args)[0];

    size_t index = 1;
    size_t positional_count = 0;
    while (index < arena_arr_len(*args)) {
        String_View tok = (*args)[index];
        if (eval_sv_eq_ci_lit(tok, "CONFIGURATION") ||
            eval_sv_eq_ci_lit(tok, "PARALLEL_LEVEL") ||
            eval_sv_eq_ci_lit(tok, "PROJECT_NAME") ||
            eval_sv_eq_ci_lit(tok, "TARGET")) {
            break;
        }
        positional_count++;
        index++;
    }

    if (positional_count > 3) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "host", nob_sv_from_cstr("build_command() received too many positional arguments"), nob_sv_from_cstr("Supported legacy positionals: <makecommand> [makefile_name] [target]"));
        return false;
    }

    if (positional_count == 3) out_opt->target = (*args)[3];

    while (index < arena_arr_len(*args)) {
        String_View key = (*args)[index++];
        if (index >= arena_arr_len(*args)) {
            (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "host", nob_sv_from_cstr("build_command() keyword requires a value"), key);
            return false;
        }
        String_View value = (*args)[index++];

        if (eval_sv_eq_ci_lit(key, "CONFIGURATION")) {
            out_opt->config = value;
        } else if (eval_sv_eq_ci_lit(key, "PARALLEL_LEVEL")) {
            out_opt->parallel_level = value;
        } else if (eval_sv_eq_ci_lit(key, "TARGET")) {
            out_opt->target = value;
        } else if (eval_sv_eq_ci_lit(key, "PROJECT_NAME")) {
            out_opt->project_name = value;
            out_opt->saw_project_name = true;
        } else {
            (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_UNSUPPORTED_OPERATION, "host", nob_sv_from_cstr("build_command() received an unsupported argument"), key);
            return false;
        }
    }

    return true;
}

static String_View host_build_command_text_temp(Evaluator_Context *ctx,
                                                String_View command_name,
                                                const Build_Command_Options *opt,
                                                bool append_make_i) {
    if (!ctx || !opt) return nob_sv_from_cstr("");
    if (command_name.count == 0) command_name = nob_sv_from_cstr("cmake");

    size_t total = command_name.count + sizeof(" --build .") - 1;
    if (opt->target.count > 0) total += sizeof(" --target ") - 1 + opt->target.count;
    if (opt->config.count > 0) total += sizeof(" --config ") - 1 + opt->config.count;
    if (opt->parallel_level.count > 0) total += sizeof(" --parallel ") - 1 + opt->parallel_level.count;
    if (append_make_i) total += sizeof(" -- -i") - 1;

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    size_t off = 0;
    memcpy(buf + off, command_name.data, command_name.count);
    off += command_name.count;

    memcpy(buf + off, " --build .", sizeof(" --build .") - 1);
    off += sizeof(" --build .") - 1;

    if (opt->target.count > 0) {
        memcpy(buf + off, " --target ", sizeof(" --target ") - 1);
        off += sizeof(" --target ") - 1;
        memcpy(buf + off, opt->target.data, opt->target.count);
        off += opt->target.count;
    }
    if (opt->config.count > 0) {
        memcpy(buf + off, " --config ", sizeof(" --config ") - 1);
        off += sizeof(" --config ") - 1;
        memcpy(buf + off, opt->config.data, opt->config.count);
        off += opt->config.count;
    }
    if (opt->parallel_level.count > 0) {
        memcpy(buf + off, " --parallel ", sizeof(" --parallel ") - 1);
        off += sizeof(" --parallel ") - 1;
        memcpy(buf + off, opt->parallel_level.data, opt->parallel_level.count);
        off += opt->parallel_level.count;
    }
    if (append_make_i) {
        memcpy(buf + off, " -- -i", sizeof(" -- -i") - 1);
        off += sizeof(" -- -i") - 1;
    }

    buf[off] = '\0';
    return nob_sv_from_parts(buf, off);
}

static bool host_info_query_value(Evaluator_Context *ctx,
                                  String_View result_var,
                                  String_View key,
                                  String_View *out_value,
                                  bool *out_supported) {
    if (!ctx || !out_value || !out_supported) return false;
    *out_value = nob_sv_from_cstr("");
    *out_supported = true;

    if (eval_sv_eq_ci_lit(key, "NUMBER_OF_LOGICAL_CORES")) {
        size_t count = 0;
        if (!eval_host_logical_cores(&count)) {
            *out_supported = false;
            return true;
        }
        *out_value = host_format_size_t_temp(ctx, count);
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }
    if (eval_sv_eq_ci_lit(key, "NUMBER_OF_PHYSICAL_CORES")) {
        size_t count = 0;
        if (!host_physical_cores_temp(ctx, &count)) return false;
        *out_value = host_format_size_t_temp(ctx, count);
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }
    if (eval_sv_eq_ci_lit(key, "HOSTNAME")) {
        String_View hostname = nob_sv_from_cstr("");
        if (!eval_host_hostname_temp(ctx, &hostname)) return false;
        *out_value = hostname;
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }
    if (eval_sv_eq_ci_lit(key, "FQDN")) {
#if defined(_WIN32)
        char buf[256] = {0};
        DWORD size = (DWORD)(sizeof(buf) - 1);
        if (GetComputerNameExA(ComputerNameDnsFullyQualified, buf, &size) && size > 0) {
            *out_value = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(buf, (size_t)size));
            return !eval_result_is_fatal(eval_result_from_ctx(ctx));
        }
#endif
        String_View hostname = nob_sv_from_cstr("");
        if (!eval_host_hostname_temp(ctx, &hostname)) return false;
        *out_value = hostname;
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }
    if (eval_sv_eq_ci_lit(key, "TOTAL_VIRTUAL_MEMORY") ||
        eval_sv_eq_ci_lit(key, "AVAILABLE_VIRTUAL_MEMORY") ||
        eval_sv_eq_ci_lit(key, "TOTAL_PHYSICAL_MEMORY") ||
        eval_sv_eq_ci_lit(key, "AVAILABLE_PHYSICAL_MEMORY")) {
        Eval_Host_Memory_Info info = {0};
        if (!eval_host_memory_info(&info)) {
            *out_supported = false;
            return true;
        }

        unsigned long long value = 0;
        if (eval_sv_eq_ci_lit(key, "TOTAL_VIRTUAL_MEMORY")) value = info.total_virtual_mib;
        else if (eval_sv_eq_ci_lit(key, "AVAILABLE_VIRTUAL_MEMORY")) value = info.available_virtual_mib;
        else if (eval_sv_eq_ci_lit(key, "TOTAL_PHYSICAL_MEMORY")) value = info.total_physical_mib;
        else value = info.available_physical_mib;

        *out_value = host_format_ull_temp(ctx, value);
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }
    if (eval_sv_eq_ci_lit(key, "IS_64BIT")) {
        *out_value = host_format_bool_temp(ctx, sizeof(void*) >= 8);
        return true;
    }
    if (eval_sv_eq_ci_lit(key, "HAS_FPU") ||
        eval_sv_eq_ci_lit(key, "HAS_MMX") ||
        eval_sv_eq_ci_lit(key, "HAS_MMX_PLUS") ||
        eval_sv_eq_ci_lit(key, "HAS_SSE") ||
        eval_sv_eq_ci_lit(key, "HAS_SSE2") ||
        eval_sv_eq_ci_lit(key, "HAS_SSE_FP") ||
        eval_sv_eq_ci_lit(key, "HAS_SSE_MMX") ||
        eval_sv_eq_ci_lit(key, "HAS_AMD_3DNOW") ||
        eval_sv_eq_ci_lit(key, "HAS_AMD_3DNOW_PLUS") ||
        eval_sv_eq_ci_lit(key, "HAS_IA64") ||
        eval_sv_eq_ci_lit(key, "HAS_SERIAL_NUMBER")) {
        Host_Cpu_Feature feature = HOST_CPU_FEATURE_FPU;
        if (eval_sv_eq_ci_lit(key, "HAS_MMX")) feature = HOST_CPU_FEATURE_MMX;
        else if (eval_sv_eq_ci_lit(key, "HAS_MMX_PLUS")) feature = HOST_CPU_FEATURE_MMX_PLUS;
        else if (eval_sv_eq_ci_lit(key, "HAS_SSE")) feature = HOST_CPU_FEATURE_SSE;
        else if (eval_sv_eq_ci_lit(key, "HAS_SSE2")) feature = HOST_CPU_FEATURE_SSE2;
        else if (eval_sv_eq_ci_lit(key, "HAS_SSE_FP")) feature = HOST_CPU_FEATURE_SSE_FP;
        else if (eval_sv_eq_ci_lit(key, "HAS_SSE_MMX")) feature = HOST_CPU_FEATURE_SSE_MMX;
        else if (eval_sv_eq_ci_lit(key, "HAS_AMD_3DNOW")) feature = HOST_CPU_FEATURE_AMD_3DNOW;
        else if (eval_sv_eq_ci_lit(key, "HAS_AMD_3DNOW_PLUS")) feature = HOST_CPU_FEATURE_AMD_3DNOW_PLUS;
        else if (eval_sv_eq_ci_lit(key, "HAS_IA64")) feature = HOST_CPU_FEATURE_IA64;
        else if (eval_sv_eq_ci_lit(key, "HAS_SERIAL_NUMBER")) feature = HOST_CPU_FEATURE_SERIAL_NUMBER;

        bool value = false;
        if (!host_cpu_feature_temp(ctx, feature, &value)) return false;
        *out_value = host_format_bool_temp(ctx, value);
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }
    if (eval_sv_eq_ci_lit(key, "PROCESSOR_SERIAL_NUMBER")) {
        if (!host_processor_serial_temp(ctx, out_value)) return false;
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }
    if (eval_sv_eq_ci_lit(key, "PROCESSOR_NAME")) {
        if (!host_processor_name_temp(ctx, out_value)) return false;
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }
    if (eval_sv_eq_ci_lit(key, "PROCESSOR_DESCRIPTION")) {
        if (!host_processor_description_temp(ctx, out_value)) return false;
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }
    if (eval_sv_eq_ci_lit(key, "OS_NAME")) {
        *out_value = eval_detect_host_system_name();
        return true;
    }
    if (eval_sv_eq_ci_lit(key, "OS_RELEASE")) {
        *out_value = eval_host_os_release_temp(ctx);
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }
    if (eval_sv_eq_ci_lit(key, "OS_VERSION")) {
        *out_value = eval_host_os_version_temp(ctx);
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }
    if (eval_sv_eq_ci_lit(key, "OS_PLATFORM")) {
        *out_value = eval_detect_host_processor();
        return true;
    }
    if (eval_sv_eq_ci_lit(key, "MSYSTEM_PREFIX")) {
#if defined(_WIN32)
        const char *value = eval_getenv_temp(ctx, "MSYSTEM_PREFIX");
        if (!value) value = "";
        *out_value = sv_copy_to_temp_arena(ctx, nob_sv_from_cstr(value));
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
#else
        return true;
#endif
    }
    if (eval_sv_eq_ci_lit(key, "DISTRIB_INFO") ||
        (key.count > 8 && memcmp(key.data, "DISTRIB_", 8) == 0)) {
        if (!host_distrib_query_value(ctx, result_var, key, out_value)) return false;
        return !eval_result_is_fatal(eval_result_from_ctx(ctx));
    }

    *out_supported = false;
    return true;
}

Eval_Result eval_handle_cmake_host_system_information(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(args) < 4 ||
        !eval_sv_eq_ci_lit(args[0], "RESULT") ||
        !eval_sv_eq_ci_lit(args[2], "QUERY")) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "host", nob_sv_from_cstr("cmake_host_system_information() requires RESULT and QUERY clauses"), nob_sv_from_cstr("Usage: cmake_host_system_information(RESULT <var> QUERY <key>...)"));
        return eval_result_from_ctx(ctx);
    }

    String_View result_var = args[1];
    if (arena_arr_len(args) >= 5 && eval_sv_eq_ci_lit(args[3], "WINDOWS_REGISTRY")) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(
            ctx,
            node,
            o,
            EV_DIAG_ERROR,
            EVAL_DIAG_NOT_IMPLEMENTED,
            "host",
            nob_sv_from_cstr("cmake_host_system_information(QUERY WINDOWS_REGISTRY ...) is not implemented yet"),
            args[4]);
        (void)eval_var_set_current(ctx, result_var, nob_sv_from_cstr(""));
        return eval_result_from_ctx(ctx);
    }

    String_View *values = arena_alloc_array(eval_temp_arena(ctx), String_View, arena_arr_len(args) - 3);
    EVAL_OOM_RETURN_IF_NULL(ctx, values, eval_result_fatal());

    size_t value_count = 0;
    for (size_t i = 3; i < arena_arr_len(args); i++) {
        String_View value = nob_sv_from_cstr("");
        bool supported = false;
        if (!host_info_query_value(ctx, result_var, args[i], &value, &supported)) return eval_result_fatal();
        if (!supported) {
            (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_NOT_IMPLEMENTED, "host", nob_sv_from_cstr("cmake_host_system_information() query key is not implemented yet"), args[i]);
            value = nob_sv_from_cstr("");
        }
        values[value_count++] = value;
    }

    String_View result = eval_sv_join_semi_temp(ctx, values, value_count);
    if (eval_should_stop(ctx)) return eval_result_fatal();
    if (!eval_var_set_current(ctx, result_var, result)) return eval_result_fatal();
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_site_name(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(args) != 1) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "host", nob_sv_from_cstr("site_name() requires exactly one output variable"), nob_sv_from_cstr("Usage: site_name(<out-var>)"));
        return eval_result_from_ctx(ctx);
    }

    String_View value = nob_sv_from_cstr("");
    String_View hostname_command = eval_var_get_visible(ctx, nob_sv_from_cstr("HOSTNAME"));
    if (hostname_command.count > 0) {
        if (!host_capture_command_stdout(ctx, hostname_command, &value)) return eval_result_fatal();
        if (value.count == 0) {
            (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_WARNING, EVAL_DIAG_INVALID_VALUE, "host", nob_sv_from_cstr("site_name() HOSTNAME helper command produced no output"), hostname_command);
        }
    } else {
        if (!eval_host_hostname_temp(ctx, &value)) return eval_result_fatal();
        if (value.count == 0) {
            (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_WARNING, EVAL_DIAG_IO_FAILURE, "host", nob_sv_from_cstr("site_name() could not determine host name"), nob_sv_from_cstr("Result variable set to empty string"));
        }
    }

    if (!eval_var_set_current(ctx, args[0], value)) return eval_result_fatal();
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_build_name(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    if (arena_arr_len(args) != 1) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "host", nob_sv_from_cstr("build_name() requires exactly one output variable"), nob_sv_from_cstr("Usage: build_name(<out-var>)"));
        return eval_result_from_ctx(ctx);
    }

    if (eval_policy_is_new(ctx, EVAL_POLICY_CMP0036)) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_POLICY_CONFLICT, "host", nob_sv_from_cstr("build_name() is disallowed by CMP0036"), nob_sv_from_cstr("Set CMP0036 to OLD only for legacy compatibility"));
        return eval_result_from_ctx(ctx);
    }

    String_View system_name = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_HOST_SYSTEM_NAME"));
    if (system_name.count == 0) system_name = eval_detect_host_system_name();
    String_View compiler_id = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_CXX_COMPILER_ID"));
    if (compiler_id.count == 0) compiler_id = nob_sv_from_cstr("Unknown");

    size_t total = system_name.count + 1 + compiler_id.count;
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), total + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, eval_result_fatal());
    memcpy(buf, system_name.data, system_name.count);
    buf[system_name.count] = '-';
    memcpy(buf + system_name.count + 1, compiler_id.data, compiler_id.count);
    buf[total] = '\0';

    if (!eval_var_set_current(ctx, args[0], nob_sv_from_parts(buf, total))) return eval_result_fatal();
    return eval_result_from_ctx(ctx);
}

Eval_Result eval_handle_build_command(Evaluator_Context *ctx, const Node *node) {
    if (!ctx || eval_should_stop(ctx) || !node) return eval_result_fatal();

    Cmake_Event_Origin o = eval_origin_from_node(ctx, node);
    SV_List args = eval_resolve_args(ctx, &node->as.cmd.args);
    if (eval_should_stop(ctx)) return eval_result_from_ctx(ctx);

    String_View out_var = nob_sv_from_cstr("");
    Build_Command_Options opt = {0};
    if (!host_build_command_parse(ctx, node, &args, &out_var, &opt)) return eval_result_from_ctx(ctx);

    if (opt.saw_project_name) {
        (void)EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_WARNING, EVAL_DIAG_INVALID_VALUE, "host", nob_sv_from_cstr("build_command(PROJECT_NAME ...) is parsed but ignored by evaluator v2"), opt.project_name);
    }

    bool cmp0061_new = eval_policy_is_new(ctx, EVAL_POLICY_CMP0061);
    String_View generator = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_GENERATOR"));
    bool append_make_i = !cmp0061_new && host_is_makefile_generator(generator);

    String_View cmake_command = eval_var_get_visible(ctx, nob_sv_from_cstr("CMAKE_COMMAND"));
    String_View value = host_build_command_text_temp(ctx, cmake_command, &opt, append_make_i);
    if (eval_should_stop(ctx)) return eval_result_fatal();

    if (!eval_var_set_current(ctx, out_var, value)) return eval_result_fatal();
    return eval_result_from_ctx(ctx);
}
