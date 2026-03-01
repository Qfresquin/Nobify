#include "test_v2_assert.h"
#include "test_v2_suite.h"
#include "test_workspace.h"

#include "arena.h"
#include "arena_dyn.h"
#include "diagnostics.h"
#include "evaluator.h"
#include "event_ir.h"
#include "lexer.h"
#include "parser.h"

#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#endif
#else
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

typedef struct {
    String_View name;
    String_View script;
} Evaluator_Case;

typedef struct {
    Evaluator_Case *items;
    size_t count;
    size_t capacity;
} Evaluator_Case_List;

static void evaluator_set_source_date_epoch_value(const char *value) {
#if defined(_WIN32)
    _putenv_s("SOURCE_DATE_EPOCH", value ? value : "");
#else
    if (value) {
        setenv("SOURCE_DATE_EPOCH", value, 1);
    } else {
        unsetenv("SOURCE_DATE_EPOCH");
    }
#endif
}

static bool evaluator_remove_link_like_path(const char *path) {
    if (!path) return false;
#if defined(_WIN32)
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        DWORD err = GetLastError();
        return err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND;
    }
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        if (RemoveDirectoryA(path)) return true;
    }
    if (DeleteFileA(path)) return true;
    if (RemoveDirectoryA(path)) return true;
    return false;
#else
    struct stat st = {0};
    if (lstat(path, &st) != 0) {
        return errno == ENOENT;
    }
    if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
        return rmdir(path) == 0;
    }
    return unlink(path) == 0;
#endif
}

static bool evaluator_prepare_symlink_escape_fixture(void) {
    const char *outside_dir = "../evaluator_symlink_outside";
    const char *outside_file = "../evaluator_symlink_outside/outside.txt";
    const char *inside_link = "temp_symlink_escape_link";

    if (!nob_mkdir_if_not_exists(outside_dir)) {
        nob_log(NOB_ERROR, "evaluator fixture: failed to create %s", outside_dir);
        return false;
    }
    if (!nob_write_entire_file(outside_file, "outside\n", 8)) {
        nob_log(NOB_ERROR, "evaluator fixture: failed to write %s", outside_file);
        return false;
    }

    if (!evaluator_remove_link_like_path(inside_link)) {
        nob_log(NOB_ERROR, "evaluator fixture: failed to clean %s", inside_link);
        return false;
    }

#if defined(_WIN32)
    DWORD flags = SYMBOLIC_LINK_FLAG_DIRECTORY;
    if (CreateSymbolicLinkA(inside_link, "..\\evaluator_symlink_outside",
                            flags | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0) {
        return true;
    }
    if (CreateSymbolicLinkA(inside_link, "..\\evaluator_symlink_outside", flags) != 0) {
        return true;
    }

    int mklink_rc = system("cmd /C mklink /J temp_symlink_escape_link ..\\evaluator_symlink_outside >NUL 2>NUL");
    if (mklink_rc == 0) return true;

    nob_log(NOB_ERROR, "evaluator fixture: failed to create symlink/junction at %s", inside_link);
    return false;
#else
    if (symlink("../evaluator_symlink_outside", inside_link) == 0) return true;
    if (errno == EEXIST) return true;

    nob_log(NOB_ERROR, "evaluator fixture: failed to create symlink %s -> %s: %s",
            inside_link, outside_dir, strerror(errno));
    return false;
#endif
}

static bool token_list_append(Arena *arena, Token_List *list, Token token) {
    if (!arena || !list) return false;
    if (!arena_da_reserve(arena, (void**)&list->items, &list->capacity, sizeof(list->items[0]), list->count + 1)) return false;
    list->items[list->count++] = token;
    return true;
}

static Ast_Root parse_cmake(Arena *arena, const char *script) {
    Lexer lx = lexer_init(nob_sv_from_cstr(script ? script : ""));
    Token_List toks = {0};
    for (;;) {
        Token t = lexer_next(&lx);
        if (t.kind == TOKEN_END) break;
        if (!token_list_append(arena, &toks, t)) return (Ast_Root){0};
    }
    return parse_tokens(arena, toks);
}

static bool evaluator_load_text_file_to_arena(Arena *arena, const char *path, String_View *out) {
    if (!arena || !path || !out) return false;

    Nob_String_Builder sb = {0};
    if (!nob_read_entire_file(path, &sb)) return false;

    char *text = arena_strndup(arena, sb.items, sb.count);
    size_t len = sb.count;
    nob_sb_free(sb);
    if (!text) return false;

    *out = nob_sv_from_parts(text, len);
    return true;
}

static String_View evaluator_normalize_newlines_to_arena(Arena *arena, String_View in) {
    if (!arena) return nob_sv_from_cstr("");

    char *buf = (char*)arena_alloc(arena, in.count + 1);
    if (!buf) return nob_sv_from_cstr("");

    size_t out_count = 0;
    for (size_t i = 0; i < in.count; i++) {
        char c = in.data[i];
        if (c == '\r') continue;
        buf[out_count++] = c;
    }

    buf[out_count] = '\0';
    return nob_sv_from_parts(buf, out_count);
}

static bool evaluator_case_list_append(Arena *arena, Evaluator_Case_List *list, Evaluator_Case value) {
    if (!arena || !list) return false;
    if (!arena_da_reserve(arena, (void**)&list->items, &list->capacity, sizeof(list->items[0]), list->count + 1)) return false;
    list->items[list->count++] = value;
    return true;
}

static bool sv_starts_with_cstr(String_View sv, const char *prefix) {
    String_View p = nob_sv_from_cstr(prefix);
    if (sv.count < p.count) return false;
    return memcmp(sv.data, p.data, p.count) == 0;
}

static bool sv_contains_sv(String_View haystack, String_View needle) {
    if (needle.count == 0) return true;
    if (haystack.count < needle.count) return false;
    for (size_t i = 0; i + needle.count <= haystack.count; i++) {
        if (memcmp(haystack.data + i, needle.data, needle.count) == 0) return true;
    }
    return false;
}

static String_View sv_trim_cr(String_View sv) {
    if (sv.count > 0 && sv.data[sv.count - 1] == '\r') {
        return nob_sv_from_parts(sv.data, sv.count - 1);
    }
    return sv;
}

static bool parse_case_pack_to_arena(Arena *arena, String_View content, Evaluator_Case_List *out) {
    if (!arena || !out) return false;
    *out = (Evaluator_Case_List){0};

    Nob_String_Builder script_sb = {0};
    bool in_case = false;
    String_View current_name = {0};

    size_t pos = 0;
    while (pos < content.count) {
        size_t line_start = pos;
        while (pos < content.count && content.data[pos] != '\n') pos++;
        size_t line_end = pos;
        if (pos < content.count && content.data[pos] == '\n') pos++;

        String_View raw_line = nob_sv_from_parts(content.data + line_start, line_end - line_start);
        String_View line = sv_trim_cr(raw_line);

        if (sv_starts_with_cstr(line, "#@@CASE ")) {
            if (in_case) {
                nob_sb_free(script_sb);
                return false;
            }
            in_case = true;
            current_name = nob_sv_from_parts(line.data + 8, line.count - 8);
            script_sb.count = 0;
            continue;
        }

        if (nob_sv_eq(line, nob_sv_from_cstr("#@@ENDCASE"))) {
            if (!in_case) {
                nob_sb_free(script_sb);
                return false;
            }

            char *name = arena_strndup(arena, current_name.data, current_name.count);
            char *script = arena_strndup(arena, script_sb.items ? script_sb.items : "", script_sb.count);
            if (!name || !script) {
                nob_sb_free(script_sb);
                return false;
            }

            if (!evaluator_case_list_append(arena, out, (Evaluator_Case){
                .name = nob_sv_from_parts(name, current_name.count),
                .script = nob_sv_from_parts(script, script_sb.count),
            })) {
                nob_sb_free(script_sb);
                return false;
            }

            in_case = false;
            current_name = (String_View){0};
            script_sb.count = 0;
            continue;
        }

        if (in_case) {
            nob_sb_append_buf(&script_sb, raw_line.data, raw_line.count);
            nob_sb_append(&script_sb, '\n');
        }
    }

    nob_sb_free(script_sb);
    if (in_case) return false;

    for (size_t i = 0; i < out->count; i++) {
        for (size_t j = i + 1; j < out->count; j++) {
            if (nob_sv_eq(out->items[i].name, out->items[j].name)) return false;
        }
    }

    return out->count > 0;
}

static void snapshot_append_escaped_sv(Nob_String_Builder *sb, String_View sv) {
    nob_sb_append_cstr(sb, "'");
    for (size_t i = 0; i < sv.count; i++) {
        char c = sv.data[i];
        if (c == '\\') {
            nob_sb_append_cstr(sb, "\\\\");
        } else if (c == '\n') {
            nob_sb_append_cstr(sb, "\\n");
        } else if (c == '\r') {
            nob_sb_append_cstr(sb, "\\r");
        } else if (c == '\t') {
            nob_sb_append_cstr(sb, "\\t");
        } else if (c == '\'') {
            nob_sb_append_cstr(sb, "\\'");
        } else {
            nob_sb_append(sb, c);
        }
    }
    nob_sb_append_cstr(sb, "'");
}

static void snapshot_append_escaped_sv_list(Nob_String_Builder *sb, String_View *items, size_t count) {
    nob_sb_append_cstr(sb, "[");
    if (!items) {
        nob_sb_append_cstr(sb, "]");
        return;
    }
    for (size_t i = 0; i < count; i++) {
        if (i > 0) nob_sb_append_cstr(sb, ",");
        snapshot_append_escaped_sv(sb, items[i]);
    }
    nob_sb_append_cstr(sb, "]");
}

static const char *event_kind_name(Cmake_Event_Kind kind) {
    switch (kind) {
        case EV_DIAGNOSTIC: return "EV_DIAGNOSTIC";
        case EV_PROJECT_DECLARE: return "EV_PROJECT_DECLARE";
        case EV_VAR_SET: return "EV_VAR_SET";
        case EV_SET_CACHE_ENTRY: return "EV_SET_CACHE_ENTRY";
        case EV_TARGET_DECLARE: return "EV_TARGET_DECLARE";
        case EV_TARGET_ADD_SOURCE: return "EV_TARGET_ADD_SOURCE";
        case EV_TARGET_PROP_SET: return "EV_TARGET_PROP_SET";
        case EV_TARGET_INCLUDE_DIRECTORIES: return "EV_TARGET_INCLUDE_DIRECTORIES";
        case EV_TARGET_COMPILE_DEFINITIONS: return "EV_TARGET_COMPILE_DEFINITIONS";
        case EV_TARGET_COMPILE_OPTIONS: return "EV_TARGET_COMPILE_OPTIONS";
        case EV_TARGET_LINK_LIBRARIES: return "EV_TARGET_LINK_LIBRARIES";
        case EV_TARGET_LINK_OPTIONS: return "EV_TARGET_LINK_OPTIONS";
        case EV_TARGET_LINK_DIRECTORIES: return "EV_TARGET_LINK_DIRECTORIES";
        case EV_CUSTOM_COMMAND_TARGET: return "EV_CUSTOM_COMMAND_TARGET";
        case EV_CUSTOM_COMMAND_OUTPUT: return "EV_CUSTOM_COMMAND_OUTPUT";
        case EV_DIR_PUSH: return "EV_DIR_PUSH";
        case EV_DIR_POP: return "EV_DIR_POP";
        case EV_DIRECTORY_INCLUDE_DIRECTORIES: return "EV_DIRECTORY_INCLUDE_DIRECTORIES";
        case EV_DIRECTORY_LINK_DIRECTORIES: return "EV_DIRECTORY_LINK_DIRECTORIES";
        case EV_GLOBAL_COMPILE_DEFINITIONS: return "EV_GLOBAL_COMPILE_DEFINITIONS";
        case EV_GLOBAL_COMPILE_OPTIONS: return "EV_GLOBAL_COMPILE_OPTIONS";
        case EV_GLOBAL_LINK_OPTIONS: return "EV_GLOBAL_LINK_OPTIONS";
        case EV_GLOBAL_LINK_LIBRARIES: return "EV_GLOBAL_LINK_LIBRARIES";
        case EV_TESTING_ENABLE: return "EV_TESTING_ENABLE";
        case EV_TEST_ADD: return "EV_TEST_ADD";
        case EV_INSTALL_ADD_RULE: return "EV_INSTALL_ADD_RULE";
        case EV_CPACK_ADD_INSTALL_TYPE: return "EV_CPACK_ADD_INSTALL_TYPE";
        case EV_CPACK_ADD_COMPONENT_GROUP: return "EV_CPACK_ADD_COMPONENT_GROUP";
        case EV_CPACK_ADD_COMPONENT: return "EV_CPACK_ADD_COMPONENT";
        case EV_FIND_PACKAGE: return "EV_FIND_PACKAGE";
    }
    return "EV_UNKNOWN";
}

static const char *target_type_name(Cmake_Target_Type type) {
    switch (type) {
        case EV_TARGET_EXECUTABLE: return "EXECUTABLE";
        case EV_TARGET_LIBRARY_STATIC: return "LIB_STATIC";
        case EV_TARGET_LIBRARY_SHARED: return "LIB_SHARED";
        case EV_TARGET_LIBRARY_MODULE: return "LIB_MODULE";
        case EV_TARGET_LIBRARY_INTERFACE: return "LIB_INTERFACE";
        case EV_TARGET_LIBRARY_OBJECT: return "LIB_OBJECT";
        case EV_TARGET_LIBRARY_UNKNOWN: return "LIB_UNKNOWN";
    }
    return "UNKNOWN";
}

static const char *visibility_name(Cmake_Visibility vis) {
    switch (vis) {
        case EV_VISIBILITY_UNSPECIFIED: return "UNSPECIFIED";
        case EV_VISIBILITY_PRIVATE: return "PRIVATE";
        case EV_VISIBILITY_PUBLIC: return "PUBLIC";
        case EV_VISIBILITY_INTERFACE: return "INTERFACE";
    }
    return "UNKNOWN";
}

static const char *diag_severity_name(Cmake_Diag_Severity sev) {
    switch (sev) {
        case EV_DIAG_WARNING: return "WARNING";
        case EV_DIAG_ERROR: return "ERROR";
    }
    return "UNKNOWN";
}

static const char *prop_op_name(Cmake_Target_Property_Op op) {
    switch (op) {
        case EV_PROP_SET: return "SET";
        case EV_PROP_APPEND_LIST: return "APPEND_LIST";
        case EV_PROP_APPEND_STRING: return "APPEND_STRING";
    }
    return "UNKNOWN";
}

static void append_event_line(Nob_String_Builder *sb, size_t index, const Cmake_Event *ev) {
    nob_sb_append_cstr(sb, nob_temp_sprintf("EV[%zu] kind=%s file=", index, event_kind_name(ev->kind)));
    snapshot_append_escaped_sv(sb, ev->origin.file_path);
    nob_sb_append_cstr(sb, nob_temp_sprintf(" line=%zu col=%zu", ev->origin.line, ev->origin.col));

    switch (ev->kind) {
        case EV_DIAGNOSTIC:
            nob_sb_append_cstr(sb, nob_temp_sprintf(" sev=%s component=", diag_severity_name(ev->as.diag.severity)));
            snapshot_append_escaped_sv(sb, ev->as.diag.component);
            nob_sb_append_cstr(sb, " command=");
            snapshot_append_escaped_sv(sb, ev->as.diag.command);
            nob_sb_append_cstr(sb, " code=");
            snapshot_append_escaped_sv(sb, ev->as.diag.code);
            nob_sb_append_cstr(sb, " class=");
            snapshot_append_escaped_sv(sb, ev->as.diag.error_class);
            nob_sb_append_cstr(sb, " cause=");
            snapshot_append_escaped_sv(sb, ev->as.diag.cause);
            nob_sb_append_cstr(sb, " hint=");
            snapshot_append_escaped_sv(sb, ev->as.diag.hint);
            break;

        case EV_PROJECT_DECLARE:
            nob_sb_append_cstr(sb, " name=");
            snapshot_append_escaped_sv(sb, ev->as.project_declare.name);
            nob_sb_append_cstr(sb, " version=");
            snapshot_append_escaped_sv(sb, ev->as.project_declare.version);
            nob_sb_append_cstr(sb, " description=");
            snapshot_append_escaped_sv(sb, ev->as.project_declare.description);
            nob_sb_append_cstr(sb, " languages=");
            snapshot_append_escaped_sv(sb, ev->as.project_declare.languages);
            break;

        case EV_VAR_SET:
            nob_sb_append_cstr(sb, " key=");
            snapshot_append_escaped_sv(sb, ev->as.var_set.key);
            nob_sb_append_cstr(sb, " value=");
            snapshot_append_escaped_sv(sb, ev->as.var_set.value);
            break;

        case EV_SET_CACHE_ENTRY:
            nob_sb_append_cstr(sb, " key=");
            snapshot_append_escaped_sv(sb, ev->as.cache_entry.key);
            nob_sb_append_cstr(sb, " value=");
            snapshot_append_escaped_sv(sb, ev->as.cache_entry.value);
            break;

        case EV_TARGET_DECLARE:
            nob_sb_append_cstr(sb, " name=");
            snapshot_append_escaped_sv(sb, ev->as.target_declare.name);
            nob_sb_append_cstr(sb, nob_temp_sprintf(" type=%s", target_type_name(ev->as.target_declare.type)));
            break;

        case EV_TARGET_ADD_SOURCE:
            nob_sb_append_cstr(sb, " target=");
            snapshot_append_escaped_sv(sb, ev->as.target_add_source.target_name);
            nob_sb_append_cstr(sb, " path=");
            snapshot_append_escaped_sv(sb, ev->as.target_add_source.path);
            break;

        case EV_TARGET_PROP_SET:
            nob_sb_append_cstr(sb, " target=");
            snapshot_append_escaped_sv(sb, ev->as.target_prop_set.target_name);
            nob_sb_append_cstr(sb, " key=");
            snapshot_append_escaped_sv(sb, ev->as.target_prop_set.key);
            nob_sb_append_cstr(sb, " value=");
            snapshot_append_escaped_sv(sb, ev->as.target_prop_set.value);
            nob_sb_append_cstr(sb, nob_temp_sprintf(" op=%s", prop_op_name(ev->as.target_prop_set.op)));
            break;

        case EV_TARGET_INCLUDE_DIRECTORIES:
            nob_sb_append_cstr(sb, " target=");
            snapshot_append_escaped_sv(sb, ev->as.target_include_directories.target_name);
            nob_sb_append_cstr(sb, " path=");
            snapshot_append_escaped_sv(sb, ev->as.target_include_directories.path);
            nob_sb_append_cstr(sb, nob_temp_sprintf(" vis=%s is_system=%d is_before=%d",
                visibility_name(ev->as.target_include_directories.visibility),
                ev->as.target_include_directories.is_system ? 1 : 0,
                ev->as.target_include_directories.is_before ? 1 : 0));
            break;

        case EV_TARGET_COMPILE_DEFINITIONS:
            nob_sb_append_cstr(sb, " target=");
            snapshot_append_escaped_sv(sb, ev->as.target_compile_definitions.target_name);
            nob_sb_append_cstr(sb, " item=");
            snapshot_append_escaped_sv(sb, ev->as.target_compile_definitions.item);
            nob_sb_append_cstr(sb, nob_temp_sprintf(" vis=%s", visibility_name(ev->as.target_compile_definitions.visibility)));
            break;

        case EV_TARGET_COMPILE_OPTIONS:
            nob_sb_append_cstr(sb, " target=");
            snapshot_append_escaped_sv(sb, ev->as.target_compile_options.target_name);
            nob_sb_append_cstr(sb, " item=");
            snapshot_append_escaped_sv(sb, ev->as.target_compile_options.item);
            nob_sb_append_cstr(sb, nob_temp_sprintf(" vis=%s is_before=%d",
                visibility_name(ev->as.target_compile_options.visibility),
                ev->as.target_compile_options.is_before ? 1 : 0));
            break;

        case EV_TARGET_LINK_LIBRARIES:
            nob_sb_append_cstr(sb, " target=");
            snapshot_append_escaped_sv(sb, ev->as.target_link_libraries.target_name);
            nob_sb_append_cstr(sb, " item=");
            snapshot_append_escaped_sv(sb, ev->as.target_link_libraries.item);
            nob_sb_append_cstr(sb, nob_temp_sprintf(" vis=%s", visibility_name(ev->as.target_link_libraries.visibility)));
            break;

        case EV_TARGET_LINK_OPTIONS:
            nob_sb_append_cstr(sb, " target=");
            snapshot_append_escaped_sv(sb, ev->as.target_link_options.target_name);
            nob_sb_append_cstr(sb, " item=");
            snapshot_append_escaped_sv(sb, ev->as.target_link_options.item);
            nob_sb_append_cstr(sb, nob_temp_sprintf(" vis=%s is_before=%d",
                visibility_name(ev->as.target_link_options.visibility),
                ev->as.target_link_options.is_before ? 1 : 0));
            break;

        case EV_TARGET_LINK_DIRECTORIES:
            nob_sb_append_cstr(sb, " target=");
            snapshot_append_escaped_sv(sb, ev->as.target_link_directories.target_name);
            nob_sb_append_cstr(sb, " path=");
            snapshot_append_escaped_sv(sb, ev->as.target_link_directories.path);
            nob_sb_append_cstr(sb, nob_temp_sprintf(" vis=%s", visibility_name(ev->as.target_link_directories.visibility)));
            break;

        case EV_CUSTOM_COMMAND_TARGET:
            nob_sb_append_cstr(sb, " target=");
            snapshot_append_escaped_sv(sb, ev->as.custom_command_target.target_name);
            nob_sb_append_cstr(sb, nob_temp_sprintf(" pre_build=%d commands=",
                ev->as.custom_command_target.pre_build ? 1 : 0));
            snapshot_append_escaped_sv_list(sb,
                                            ev->as.custom_command_target.commands,
                                            ev->as.custom_command_target.command_count);
            nob_sb_append_cstr(sb, " working_dir=");
            snapshot_append_escaped_sv(sb, ev->as.custom_command_target.working_dir);
            nob_sb_append_cstr(sb, " comment=");
            snapshot_append_escaped_sv(sb, ev->as.custom_command_target.comment);
            nob_sb_append_cstr(sb, " outputs=");
            snapshot_append_escaped_sv(sb, ev->as.custom_command_target.outputs);
            nob_sb_append_cstr(sb, " byproducts=");
            snapshot_append_escaped_sv(sb, ev->as.custom_command_target.byproducts);
            nob_sb_append_cstr(sb, " depends=");
            snapshot_append_escaped_sv(sb, ev->as.custom_command_target.depends);
            nob_sb_append_cstr(sb, " main_dependency=");
            snapshot_append_escaped_sv(sb, ev->as.custom_command_target.main_dependency);
            nob_sb_append_cstr(sb, " depfile=");
            snapshot_append_escaped_sv(sb, ev->as.custom_command_target.depfile);
            nob_sb_append_cstr(sb, nob_temp_sprintf(" append=%d verbatim=%d uses_terminal=%d command_expand_lists=%d depends_explicit_only=%d codegen=%d",
                ev->as.custom_command_target.append ? 1 : 0,
                ev->as.custom_command_target.verbatim ? 1 : 0,
                ev->as.custom_command_target.uses_terminal ? 1 : 0,
                ev->as.custom_command_target.command_expand_lists ? 1 : 0,
                ev->as.custom_command_target.depends_explicit_only ? 1 : 0,
                ev->as.custom_command_target.codegen ? 1 : 0));
            break;

        case EV_CUSTOM_COMMAND_OUTPUT:
            nob_sb_append_cstr(sb, " commands=");
            snapshot_append_escaped_sv_list(sb,
                                            ev->as.custom_command_output.commands,
                                            ev->as.custom_command_output.command_count);
            nob_sb_append_cstr(sb, " working_dir=");
            snapshot_append_escaped_sv(sb, ev->as.custom_command_output.working_dir);
            nob_sb_append_cstr(sb, " comment=");
            snapshot_append_escaped_sv(sb, ev->as.custom_command_output.comment);
            nob_sb_append_cstr(sb, " outputs=");
            snapshot_append_escaped_sv(sb, ev->as.custom_command_output.outputs);
            nob_sb_append_cstr(sb, " byproducts=");
            snapshot_append_escaped_sv(sb, ev->as.custom_command_output.byproducts);
            nob_sb_append_cstr(sb, " depends=");
            snapshot_append_escaped_sv(sb, ev->as.custom_command_output.depends);
            nob_sb_append_cstr(sb, " main_dependency=");
            snapshot_append_escaped_sv(sb, ev->as.custom_command_output.main_dependency);
            nob_sb_append_cstr(sb, " depfile=");
            snapshot_append_escaped_sv(sb, ev->as.custom_command_output.depfile);
            nob_sb_append_cstr(sb, nob_temp_sprintf(" append=%d verbatim=%d uses_terminal=%d command_expand_lists=%d depends_explicit_only=%d codegen=%d",
                ev->as.custom_command_output.append ? 1 : 0,
                ev->as.custom_command_output.verbatim ? 1 : 0,
                ev->as.custom_command_output.uses_terminal ? 1 : 0,
                ev->as.custom_command_output.command_expand_lists ? 1 : 0,
                ev->as.custom_command_output.depends_explicit_only ? 1 : 0,
                ev->as.custom_command_output.codegen ? 1 : 0));
            break;

        case EV_DIR_PUSH:
            nob_sb_append_cstr(sb, " source_dir=");
            snapshot_append_escaped_sv(sb, ev->as.dir_push.source_dir);
            nob_sb_append_cstr(sb, " binary_dir=");
            snapshot_append_escaped_sv(sb, ev->as.dir_push.binary_dir);
            break;

        case EV_DIR_POP:
            break;

        case EV_DIRECTORY_INCLUDE_DIRECTORIES:
            nob_sb_append_cstr(sb, " path=");
            snapshot_append_escaped_sv(sb, ev->as.directory_include_directories.path);
            nob_sb_append_cstr(sb, nob_temp_sprintf(" is_system=%d is_before=%d",
                ev->as.directory_include_directories.is_system ? 1 : 0,
                ev->as.directory_include_directories.is_before ? 1 : 0));
            break;

        case EV_DIRECTORY_LINK_DIRECTORIES:
            nob_sb_append_cstr(sb, " path=");
            snapshot_append_escaped_sv(sb, ev->as.directory_link_directories.path);
            nob_sb_append_cstr(sb, nob_temp_sprintf(" is_before=%d",
                ev->as.directory_link_directories.is_before ? 1 : 0));
            break;

        case EV_GLOBAL_COMPILE_DEFINITIONS:
            nob_sb_append_cstr(sb, " item=");
            snapshot_append_escaped_sv(sb, ev->as.global_compile_definitions.item);
            break;

        case EV_GLOBAL_COMPILE_OPTIONS:
            nob_sb_append_cstr(sb, " item=");
            snapshot_append_escaped_sv(sb, ev->as.global_compile_options.item);
            break;

        case EV_GLOBAL_LINK_OPTIONS:
            nob_sb_append_cstr(sb, " item=");
            snapshot_append_escaped_sv(sb, ev->as.global_link_options.item);
            break;

        case EV_GLOBAL_LINK_LIBRARIES:
            nob_sb_append_cstr(sb, " item=");
            snapshot_append_escaped_sv(sb, ev->as.global_link_libraries.item);
            break;

        case EV_TESTING_ENABLE:
            nob_sb_append_cstr(sb, nob_temp_sprintf(" enabled=%d",
                ev->as.testing_enable.enabled ? 1 : 0));
            break;

        case EV_TEST_ADD:
            nob_sb_append_cstr(sb, " name=");
            snapshot_append_escaped_sv(sb, ev->as.test_add.name);
            nob_sb_append_cstr(sb, " command=");
            snapshot_append_escaped_sv(sb, ev->as.test_add.command);
            nob_sb_append_cstr(sb, " working_dir=");
            snapshot_append_escaped_sv(sb, ev->as.test_add.working_dir);
            nob_sb_append_cstr(sb, nob_temp_sprintf(" command_expand_lists=%d",
                ev->as.test_add.command_expand_lists ? 1 : 0));
            break;

        case EV_INSTALL_ADD_RULE:
            nob_sb_append_cstr(sb, nob_temp_sprintf(" rule_type=%d", (int)ev->as.install_add_rule.rule_type));
            nob_sb_append_cstr(sb, " item=");
            snapshot_append_escaped_sv(sb, ev->as.install_add_rule.item);
            nob_sb_append_cstr(sb, " destination=");
            snapshot_append_escaped_sv(sb, ev->as.install_add_rule.destination);
            break;

        case EV_CPACK_ADD_INSTALL_TYPE:
            nob_sb_append_cstr(sb, " name=");
            snapshot_append_escaped_sv(sb, ev->as.cpack_add_install_type.name);
            nob_sb_append_cstr(sb, " display_name=");
            snapshot_append_escaped_sv(sb, ev->as.cpack_add_install_type.display_name);
            break;

        case EV_CPACK_ADD_COMPONENT_GROUP:
            nob_sb_append_cstr(sb, " name=");
            snapshot_append_escaped_sv(sb, ev->as.cpack_add_component_group.name);
            nob_sb_append_cstr(sb, " display_name=");
            snapshot_append_escaped_sv(sb, ev->as.cpack_add_component_group.display_name);
            nob_sb_append_cstr(sb, " description=");
            snapshot_append_escaped_sv(sb, ev->as.cpack_add_component_group.description);
            nob_sb_append_cstr(sb, " parent_group=");
            snapshot_append_escaped_sv(sb, ev->as.cpack_add_component_group.parent_group);
            nob_sb_append_cstr(sb, nob_temp_sprintf(" expanded=%d bold_title=%d",
                ev->as.cpack_add_component_group.expanded ? 1 : 0,
                ev->as.cpack_add_component_group.bold_title ? 1 : 0));
            break;

        case EV_CPACK_ADD_COMPONENT:
            nob_sb_append_cstr(sb, " name=");
            snapshot_append_escaped_sv(sb, ev->as.cpack_add_component.name);
            nob_sb_append_cstr(sb, " display_name=");
            snapshot_append_escaped_sv(sb, ev->as.cpack_add_component.display_name);
            nob_sb_append_cstr(sb, " description=");
            snapshot_append_escaped_sv(sb, ev->as.cpack_add_component.description);
            nob_sb_append_cstr(sb, " group=");
            snapshot_append_escaped_sv(sb, ev->as.cpack_add_component.group);
            nob_sb_append_cstr(sb, " depends=");
            snapshot_append_escaped_sv(sb, ev->as.cpack_add_component.depends);
            nob_sb_append_cstr(sb, " install_types=");
            snapshot_append_escaped_sv(sb, ev->as.cpack_add_component.install_types);
            nob_sb_append_cstr(sb, " archive_file=");
            snapshot_append_escaped_sv(sb, ev->as.cpack_add_component.archive_file);
            nob_sb_append_cstr(sb, " plist=");
            snapshot_append_escaped_sv(sb, ev->as.cpack_add_component.plist);
            nob_sb_append_cstr(sb, nob_temp_sprintf(" required=%d hidden=%d disabled=%d downloaded=%d",
                ev->as.cpack_add_component.required ? 1 : 0,
                ev->as.cpack_add_component.hidden ? 1 : 0,
                ev->as.cpack_add_component.disabled ? 1 : 0,
                ev->as.cpack_add_component.downloaded ? 1 : 0));
            break;

        case EV_FIND_PACKAGE:
            nob_sb_append_cstr(sb, " package=");
            snapshot_append_escaped_sv(sb, ev->as.find_package.package_name);
            nob_sb_append_cstr(sb, " mode=");
            snapshot_append_escaped_sv(sb, ev->as.find_package.mode);
            nob_sb_append_cstr(sb, nob_temp_sprintf(" required=%d found=%d location=",
                ev->as.find_package.required ? 1 : 0,
                ev->as.find_package.found ? 1 : 0));
            snapshot_append_escaped_sv(sb, ev->as.find_package.location);
            break;
    }

    nob_sb_append_cstr(sb, "\n");
}

static bool evaluator_snapshot_from_ast(Ast_Root root, const char *current_file, Nob_String_Builder *out_sb) {
    if (!out_sb) return false;

    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(4 * 1024 * 1024);
    if (!temp_arena || !event_arena) {
        arena_destroy(temp_arena);
        arena_destroy(event_arena);
        return false;
    }

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    if (!stream) {
        arena_destroy(temp_arena);
        arena_destroy(event_arena);
        return false;
    }

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = current_file;

    Evaluator_Context *ctx = evaluator_create(&init);
    if (!ctx) {
        arena_destroy(temp_arena);
        arena_destroy(event_arena);
        return false;
    }

    (void)evaluator_run(ctx, root);

    nob_sb_append_cstr(out_sb, nob_temp_sprintf("DIAG errors=%zu warnings=%zu\n", diag_error_count(), diag_warning_count()));
    nob_sb_append_cstr(out_sb, nob_temp_sprintf("EVENTS count=%zu\n", stream->count));

    for (size_t i = 0; i < stream->count; i++) {
        append_event_line(out_sb, i, &stream->items[i]);
    }

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    return true;
}

static bool render_evaluator_case_snapshot_to_sb(Arena *arena,
                                                  Evaluator_Case eval_case,
                                                  Nob_String_Builder *out_sb) {
    diag_reset();
    Ast_Root root = parse_cmake(arena, eval_case.script.data);

    Nob_String_Builder case_sb = {0};
    bool ok = evaluator_snapshot_from_ast(root, "CMakeLists.txt", &case_sb);
    if (!ok || case_sb.count == 0) {
        nob_sb_free(case_sb);
        return false;
    }

    nob_sb_append_buf(out_sb, case_sb.items, case_sb.count);
    nob_sb_free(case_sb);
    return true;
}

static bool render_evaluator_casepack_snapshot_to_arena(Arena *arena,
                                                         Evaluator_Case_List cases,
                                                         String_View *out) {
    if (!arena || !out) return false;

    Nob_String_Builder sb = {0};
    nob_sb_append_cstr(&sb, "MODULE evaluator\n");
    nob_sb_append_cstr(&sb, nob_temp_sprintf("CASES %zu\n\n", cases.count));

    for (size_t i = 0; i < cases.count; i++) {
        nob_sb_append_cstr(&sb, "=== CASE ");
        nob_sb_append_buf(&sb, cases.items[i].name.data, cases.items[i].name.count);
        nob_sb_append_cstr(&sb, " ===\n");

        if (!render_evaluator_case_snapshot_to_sb(arena, cases.items[i], &sb)) {
            nob_sb_free(sb);
            return false;
        }

        nob_sb_append_cstr(&sb, "=== END CASE ===\n");
        if (i + 1 < cases.count) nob_sb_append_cstr(&sb, "\n");
    }

    size_t len = sb.count;
    char *text = arena_strndup(arena, sb.items, sb.count);
    nob_sb_free(sb);
    if (!text) return false;

    *out = nob_sv_from_parts(text, len);
    return true;
}

static bool assert_evaluator_golden_casepack(const char *input_path, const char *expected_path) {
    Arena *arena = arena_create(4 * 1024 * 1024);
    if (!arena) return false;

    String_View input = {0};
    String_View expected = {0};
    String_View actual = {0};
    bool ok = true;
    const char *prev_sde = getenv("SOURCE_DATE_EPOCH");
    bool had_prev_sde = prev_sde != NULL;
    char *prev_sde_copy = NULL;
    if (had_prev_sde) {
        size_t n = strlen(prev_sde);
        prev_sde_copy = arena_strndup(arena, prev_sde, n);
        if (!prev_sde_copy) {
            ok = false;
            goto done;
        }
    }

    if (!evaluator_prepare_symlink_escape_fixture()) {
        ok = false;
        goto done;
    }

    if (!evaluator_load_text_file_to_arena(arena, input_path, &input)) {
        nob_log(NOB_ERROR, "golden: failed to read input: %s", input_path);
        ok = false;
        goto done;
    }

    Evaluator_Case_List cases = {0};
    if (!parse_case_pack_to_arena(arena, input, &cases)) {
        nob_log(NOB_ERROR, "golden: invalid case-pack: %s", input_path);
        ok = false;
        goto done;
    }
    if (cases.count != 107) {
        nob_log(NOB_ERROR, "golden: unexpected evaluator case count: got=%zu expected=107", cases.count);
        ok = false;
        goto done;
    }

    evaluator_set_source_date_epoch_value("0");

    if (!render_evaluator_casepack_snapshot_to_arena(arena, cases, &actual)) {
        nob_log(NOB_ERROR, "golden: failed to render snapshot");
        ok = false;
        goto done;
    }

    String_View actual_norm = evaluator_normalize_newlines_to_arena(arena, actual);

    const char *update = getenv("CMK2NOB_UPDATE_GOLDEN");
    if (update && strcmp(update, "1") == 0) {
        if (!nob_write_entire_file(expected_path, actual_norm.data, actual_norm.count)) {
            nob_log(NOB_ERROR, "golden: failed to update expected: %s", expected_path);
            ok = false;
        }
        goto done;
    }

    if (!evaluator_load_text_file_to_arena(arena, expected_path, &expected)) {
        nob_log(NOB_ERROR, "golden: failed to read expected: %s", expected_path);
        ok = false;
        goto done;
    }

    String_View expected_norm = evaluator_normalize_newlines_to_arena(arena, expected);
    if (!nob_sv_eq(actual_norm, expected_norm)) {
        nob_log(NOB_ERROR, "golden mismatch for %s", input_path);
        nob_log(NOB_ERROR, "--- expected (%s) ---\n%.*s", expected_path, (int)expected_norm.count, expected_norm.data);
        nob_log(NOB_ERROR, "--- actual ---\n%.*s", (int)actual_norm.count, actual_norm.data);
        ok = false;
    }

done:
    if (had_prev_sde) evaluator_set_source_date_epoch_value(prev_sde_copy ? prev_sde_copy : "");
    else evaluator_set_source_date_epoch_value(NULL);
    arena_destroy(arena);
    return ok;
}

static const char *EVALUATOR_GOLDEN_DIR = "test_v2/evaluator/golden";

TEST(evaluator_golden_all_cases) {
    ASSERT(assert_evaluator_golden_casepack(
        nob_temp_sprintf("%s/evaluator_all.cmake", EVALUATOR_GOLDEN_DIR),
        nob_temp_sprintf("%s/evaluator_all.txt", EVALUATOR_GOLDEN_DIR)));
    TEST_PASS();
}

TEST(evaluator_public_api_profile_and_report_snapshot) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);
    ASSERT(evaluator_set_compat_profile(ctx, EVAL_PROFILE_STRICT));

    Ast_Root root = parse_cmake(temp_arena, "unknown_public_api_command()\n");
    (void)evaluator_run(ctx, root);

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    const Eval_Run_Report *snapshot = evaluator_get_run_report_snapshot(ctx);
    ASSERT(report != NULL);
    ASSERT(snapshot != NULL);
    ASSERT(report->error_count >= 1);
    ASSERT(snapshot->error_count == report->error_count);

    bool found_diag_error = false;
    for (size_t i = 0; i < stream->count; i++) {
        if (stream->items[i].kind != EV_DIAGNOSTIC) continue;
        if (stream->items[i].as.diag.severity == EV_DIAG_ERROR) {
            found_diag_error = true;
            break;
        }
    }
    ASSERT(found_diag_error);

    Command_Capability cap = {0};
    ASSERT(evaluator_get_command_capability(nob_sv_from_cstr("file"), &cap));
    ASSERT(cap.implemented_level == EVAL_CMD_IMPL_PARTIAL);

    Command_Capability missing = {0};
    ASSERT(!evaluator_get_command_capability(nob_sv_from_cstr("unknown_public_api_command"), &missing));
    ASSERT(missing.implemented_level == EVAL_CMD_IMPL_MISSING);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_flow_commands_reject_extra_arguments) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "break(oops)\n"
        "continue(oops)\n"
        "block()\n"
        "endblock(oops)\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 3);

    size_t usage_hint_hits = 0;
    for (size_t i = 0; i < stream->count; i++) {
        if (stream->items[i].kind != EV_DIAGNOSTIC) continue;
        if (stream->items[i].as.diag.severity != EV_DIAG_ERROR) continue;
        if (!nob_sv_eq(stream->items[i].as.diag.cause, nob_sv_from_cstr("Command does not accept arguments"))) continue;
        usage_hint_hits++;
    }
    ASSERT(usage_hint_hits == 3);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_enable_testing_does_not_set_build_testing_variable) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(BUILD_TESTING 0)\n"
        "enable_testing()\n"
        "add_executable(enable_testing_probe main.c)\n"
        "target_compile_definitions(enable_testing_probe PRIVATE BUILD_TESTING=${BUILD_TESTING})\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_enable_event = false;
    bool saw_build_testing_zero = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind == EV_TESTING_ENABLE && ev->as.testing_enable.enabled) {
            saw_enable_event = true;
        }
        if (ev->kind == EV_TARGET_COMPILE_DEFINITIONS &&
            nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("enable_testing_probe")) &&
            nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("BUILD_TESTING=0"))) {
            saw_build_testing_zero = true;
        }
    }

    ASSERT(saw_enable_event);
    ASSERT(saw_build_testing_zero);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_enable_testing_rejects_extra_arguments) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(temp_arena, "enable_testing(extra)\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_arity_error = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("Command does not accept arguments")) &&
            nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("Usage: enable_testing()"))) {
            saw_arity_error = true;
            break;
        }
    }
    ASSERT(saw_arity_error);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_add_definitions_routes_d_flags_to_compile_definitions) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_definitions(-DLEGACY=1 /DWIN_DEF -fPIC /EHsc -D)\n"
        "add_executable(defs_probe main.c)\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_global_def_legacy = false;
    bool saw_global_def_win = false;
    bool saw_global_opt_fpic = false;
    bool saw_global_opt_eh = false;
    bool saw_global_opt_dash_d = false;
    bool saw_target_def_legacy = false;
    bool saw_target_def_win = false;
    bool saw_target_opt_fpic = false;
    bool saw_target_opt_eh = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind == EV_GLOBAL_COMPILE_DEFINITIONS) {
            if (nob_sv_eq(ev->as.global_compile_definitions.item, nob_sv_from_cstr("LEGACY=1"))) saw_global_def_legacy = true;
            if (nob_sv_eq(ev->as.global_compile_definitions.item, nob_sv_from_cstr("WIN_DEF"))) saw_global_def_win = true;
        } else if (ev->kind == EV_GLOBAL_COMPILE_OPTIONS) {
            if (nob_sv_eq(ev->as.global_compile_options.item, nob_sv_from_cstr("-fPIC"))) saw_global_opt_fpic = true;
            if (nob_sv_eq(ev->as.global_compile_options.item, nob_sv_from_cstr("/EHsc"))) saw_global_opt_eh = true;
            if (nob_sv_eq(ev->as.global_compile_options.item, nob_sv_from_cstr("-D"))) saw_global_opt_dash_d = true;
        } else if (ev->kind == EV_TARGET_COMPILE_DEFINITIONS &&
                   nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("defs_probe"))) {
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("LEGACY=1"))) saw_target_def_legacy = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("WIN_DEF"))) saw_target_def_win = true;
        } else if (ev->kind == EV_TARGET_COMPILE_OPTIONS &&
                   nob_sv_eq(ev->as.target_compile_options.target_name, nob_sv_from_cstr("defs_probe"))) {
            if (nob_sv_eq(ev->as.target_compile_options.item, nob_sv_from_cstr("-fPIC"))) saw_target_opt_fpic = true;
            if (nob_sv_eq(ev->as.target_compile_options.item, nob_sv_from_cstr("/EHsc"))) saw_target_opt_eh = true;
        }
    }

    ASSERT(saw_global_def_legacy);
    ASSERT(saw_global_def_win);
    ASSERT(saw_global_opt_fpic);
    ASSERT(saw_global_opt_eh);
    ASSERT(saw_global_opt_dash_d);
    ASSERT(saw_target_def_legacy);
    ASSERT(saw_target_def_win);
    ASSERT(saw_target_opt_fpic);
    ASSERT(saw_target_opt_eh);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_target_compile_definitions_normalizes_dash_d_items) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_executable(norm_defs main.c)\n"
        "target_compile_definitions(norm_defs PRIVATE -DFOO -D BAR -DBAZ=1 QUX=2)\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_foo = false;
    bool saw_bar = false;
    bool saw_baz = false;
    bool saw_qux = false;
    bool saw_dash_prefixed = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("norm_defs"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("FOO"))) saw_foo = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("BAR"))) saw_bar = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("BAZ=1"))) saw_baz = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("QUX=2"))) saw_qux = true;
        if (nob_sv_starts_with(ev->as.target_compile_definitions.item, nob_sv_from_cstr("-D"))) saw_dash_prefixed = true;
    }

    ASSERT(saw_foo);
    ASSERT(saw_bar);
    ASSERT(saw_baz);
    ASSERT(saw_qux);
    ASSERT(!saw_dash_prefixed);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_add_custom_command_target_validates_signature_and_target) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_custom_target(gen)\n"
        "add_custom_command(TARGET gen POST_BUILD COMMAND echo ok BYPRODUCTS ok.txt)\n"
        "add_custom_command(TARGET missing POST_BUILD COMMAND echo bad)\n"
        "add_custom_command(TARGET gen COMMAND echo bad_no_stage)\n"
        "add_custom_command(TARGET gen PRE_BUILD PRE_LINK COMMAND echo bad_multi_stage)\n"
        "add_custom_command(TARGET gen POST_BUILD DEPENDS dep1 COMMAND echo bad_depends)\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 4);

    size_t custom_target_events = 0;
    bool saw_missing_target = false;
    bool saw_missing_stage = false;
    bool saw_multi_stage = false;
    bool saw_unexpected_depends = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind == EV_CUSTOM_COMMAND_TARGET) custom_target_events++;
        if (ev->kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("add_custom_command(TARGET ...) target was not declared"))) {
            saw_missing_target = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("add_custom_command(TARGET ...) requires PRE_BUILD, PRE_LINK or POST_BUILD"))) {
            saw_missing_stage = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("add_custom_command(TARGET ...) accepts exactly one build stage"))) {
            saw_multi_stage = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("Unexpected argument in add_custom_command()")) &&
                   nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("DEPENDS"))) {
            saw_unexpected_depends = true;
        }
    }

    ASSERT(custom_target_events == 1);
    ASSERT(saw_missing_target);
    ASSERT(saw_missing_stage);
    ASSERT(saw_multi_stage);
    ASSERT(saw_unexpected_depends);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_add_custom_command_output_validates_conflicts) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_custom_command(OUTPUT bad_pairs.c IMPLICIT_DEPENDS C only.c CXX COMMAND gen)\n"
        "add_custom_command(OUTPUT bad_conflict.c IMPLICIT_DEPENDS C in.c DEPFILE in.d COMMAND gen)\n"
        "add_custom_command(OUTPUT bad_pool.c JOB_POOL pool USES_TERMINAL COMMAND gen)\n"
        "add_custom_command(OUTPUT good.c COMMAND python gen.py DEPENDS schema.idl BYPRODUCTS gen.log MAIN_DEPENDENCY schema.idl)\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 3);

    bool saw_pairs_error = false;
    bool saw_conflict_error = false;
    bool saw_pool_error = false;
    bool saw_valid_output_event = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind == EV_CUSTOM_COMMAND_OUTPUT &&
            nob_sv_eq(ev->as.custom_command_output.outputs, nob_sv_from_cstr("good.c")) &&
            nob_sv_eq(ev->as.custom_command_output.depends, nob_sv_from_cstr("schema.idl;schema.idl"))) {
            saw_valid_output_event = true;
        }
        if (ev->kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("IMPLICIT_DEPENDS requires language/file pairs"))) {
            saw_pairs_error = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("add_custom_command(OUTPUT ...) cannot combine DEPFILE with IMPLICIT_DEPENDS"))) {
            saw_conflict_error = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("add_custom_command(OUTPUT ...) JOB_POOL is incompatible with USES_TERMINAL"))) {
            saw_pool_error = true;
        }
    }

    ASSERT(saw_pairs_error);
    ASSERT(saw_conflict_error);
    ASSERT(saw_pool_error);
    ASSERT(saw_valid_output_event);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_return_in_macro_is_error_and_does_not_unwind_macro_body) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(MAC_RET start)\n"
        "macro(mc_ret)\n"
        "  set(MAC_RET before)\n"
        "  return()\n"
        "  set(MAC_RET after)\n"
        "endmacro()\n"
        "mc_ret()\n"
        "add_executable(ret_macro main.c)\n"
        "target_compile_definitions(ret_macro PRIVATE MAC_RET=${MAC_RET})\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_macro_return_error = false;
    bool saw_macro_ret_after = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind == EV_DIAGNOSTIC &&
            ev->as.diag.severity == EV_DIAG_ERROR &&
            nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("return() cannot be used inside macro()"))) {
            saw_macro_return_error = true;
        }
        if (ev->kind == EV_TARGET_COMPILE_DEFINITIONS &&
            nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("ret_macro")) &&
            nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("MAC_RET=after"))) {
            saw_macro_ret_after = true;
        }
    }

    ASSERT(saw_macro_return_error);
    ASSERT(saw_macro_ret_after);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_return_cmp0140_old_ignores_args_and_new_enables_propagate) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(RET_OLD root_old)\n"
        "set(RET_NEW root_new)\n"
        "cmake_policy(SET CMP0140 OLD)\n"
        "function(ret_old)\n"
        "  set(RET_OLD changed_old)\n"
        "  return(PROPAGATE RET_OLD)\n"
        "endfunction()\n"
        "ret_old()\n"
        "cmake_policy(SET CMP0140 NEW)\n"
        "function(ret_new)\n"
        "  set(RET_NEW changed_new)\n"
        "  return(PROPAGATE RET_NEW)\n"
        "endfunction()\n"
        "ret_new()\n"
        "add_executable(ret_cmp0140 main.c)\n"
        "target_compile_definitions(ret_cmp0140 PRIVATE RET_OLD=${RET_OLD} RET_NEW=${RET_NEW})\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_ret_old_root = false;
    bool saw_ret_new_changed = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("ret_cmp0140")) &&
            nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("RET_OLD=root_old"))) {
            saw_ret_old_root = true;
        }
        if (nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("ret_cmp0140")) &&
            nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("RET_NEW=changed_new"))) {
            saw_ret_new_changed = true;
        }
    }

    ASSERT(saw_ret_old_root);
    ASSERT(saw_ret_new_changed);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_list_transform_genex_strip_and_output_variable) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(L \"$<CONFIG:Debug>;a$<IF:$<BOOL:1>,b,c>;x\")\n"
        "list(TRANSFORM L GENEX_STRIP OUTPUT_VARIABLE L_STRIPPED)\n"
        "list(TRANSFORM L APPEND \"_S\" AT 0 OUTPUT_VARIABLE L_APPENDED)\n"
        "add_executable(list_transform_ov main.c)\n"
        "target_compile_definitions(list_transform_ov PRIVATE \"L=${L}\" \"L_STRIPPED=${L_STRIPPED}\" \"L_APPENDED=${L_APPENDED}\")\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_original = false;
    bool saw_stripped = false;
    bool saw_appended = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item,
                      nob_sv_from_cstr("L=$<CONFIG:Debug>;a$<IF:$<BOOL:1>,b,c>;x"))) {
            saw_original = true;
        } else if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("L_STRIPPED=;a;x"))) {
            saw_stripped = true;
        } else if (nob_sv_eq(ev->as.target_compile_definitions.item,
                             nob_sv_from_cstr("L_APPENDED=$<CONFIG:Debug>_S;a$<IF:$<BOOL:1>,b,c>;x"))) {
            saw_appended = true;
        }
    }

    ASSERT(saw_original);
    ASSERT(saw_stripped);
    ASSERT(saw_appended);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_list_transform_output_variable_requires_single_output_var) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(L \"a;b\")\n"
        "list(TRANSFORM L TOUPPER OUTPUT_VARIABLE)\n"
        "list(TRANSFORM L TOUPPER AT 0 OUTPUT_VARIABLE OUT EXTRA)\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 2);

    size_t output_arity_errors = 0;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (!nob_sv_eq(ev->as.diag.cause,
                       nob_sv_from_cstr("list(TRANSFORM OUTPUT_VARIABLE) expects exactly one output variable"))) {
            continue;
        }
        output_arity_errors++;
    }
    ASSERT(output_arity_errors == 2);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_math_rejects_empty_and_incomplete_invocations) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "math()\n"
        "math(EXPR)\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 2);

    bool found_empty_error = false;
    bool found_expr_arity_error = false;
    for (size_t i = 0; i < stream->count; i++) {
        if (stream->items[i].kind != EV_DIAGNOSTIC) continue;
        if (stream->items[i].as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(stream->items[i].as.diag.cause, nob_sv_from_cstr("math() requires a subcommand"))) {
            found_empty_error = true;
        }
        if (nob_sv_eq(stream->items[i].as.diag.cause,
                      nob_sv_from_cstr("math(EXPR) requires output variable and expression"))) {
            found_expr_arity_error = true;
        }
    }
    ASSERT(found_empty_error);
    ASSERT(found_expr_arity_error);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_set_target_properties_rejects_alias_target) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_library(real STATIC real.c)\n"
        "add_library(alias_real ALIAS real)\n"
        "set_target_properties(alias_real PROPERTIES OUTPUT_NAME x)\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool found_alias_error = false;
    bool emitted_prop_for_alias = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind == EV_DIAGNOSTIC &&
            ev->as.diag.severity == EV_DIAG_ERROR &&
            nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("set_target_properties() cannot be used on ALIAS targets"))) {
            found_alias_error = true;
        }
        if (ev->kind == EV_TARGET_PROP_SET &&
            nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("alias_real"))) {
            emitted_prop_for_alias = true;
        }
    }
    ASSERT(found_alias_error);
    ASSERT(!emitted_prop_for_alias);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_add_executable_imported_and_alias_signatures) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_executable(tool IMPORTED GLOBAL)\n"
        "add_executable(tool_alias ALIAS tool)\n"
        "if(TARGET tool_alias)\n"
        "  set(HAS_TOOL_ALIAS 1)\n"
        "endif()\n"
        "add_executable(alias_probe main.c)\n"
        "target_compile_definitions(alias_probe PRIVATE HAS_TOOL_ALIAS=${HAS_TOOL_ALIAS})\n"
        "add_executable(tool_alias_bad ALIAS missing_tool)\n"
        "add_executable(tool_alias2 ALIAS tool_alias)\n"
        "add_executable(tool_bad IMPORTED source.c)\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 3);

    bool saw_imported = false;
    bool saw_imported_global = false;
    bool saw_has_alias = false;
    bool saw_alias_missing_err = false;
    bool saw_alias_of_alias_err = false;
    bool saw_imported_sources_err = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind == EV_TARGET_PROP_SET) {
            if (nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("tool")) &&
                nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("IMPORTED")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("1"))) {
                saw_imported = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("tool")) &&
                nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("IMPORTED_GLOBAL")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("1"))) {
                saw_imported_global = true;
            }
        }
        if (ev->kind == EV_TARGET_COMPILE_DEFINITIONS &&
            nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("alias_probe")) &&
            nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("HAS_TOOL_ALIAS=1"))) {
            saw_has_alias = true;
        }
        if (ev->kind == EV_DIAGNOSTIC && ev->as.diag.severity == EV_DIAG_ERROR) {
            if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("ALIAS target does not exist"))) {
                saw_alias_missing_err = true;
            } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("ALIAS target cannot reference another ALIAS target"))) {
                saw_alias_of_alias_err = true;
            } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("add_executable(IMPORTED ...) does not accept source files"))) {
                saw_imported_sources_err = true;
            }
        }
    }

    ASSERT(saw_imported);
    ASSERT(saw_imported_global);
    ASSERT(saw_has_alias);
    ASSERT(saw_alias_missing_err);
    ASSERT(saw_alias_of_alias_err);
    ASSERT(saw_imported_sources_err);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_add_library_imported_alias_and_default_type) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(BUILD_SHARED_LIBS ON)\n"
        "add_library(auto_lib auto.c)\n"
        "add_library(imp_lib UNKNOWN IMPORTED GLOBAL)\n"
        "add_library(base_lib STATIC base.c)\n"
        "add_library(base_alias ALIAS base_lib)\n"
        "add_library(bad_alias ALIAS base_alias)\n"
        "add_library(bad_import IMPORTED)\n"
        "add_library(iface INTERFACE EXCLUDE_FROM_ALL)\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 2);

    bool saw_auto_shared = false;
    bool saw_imported = false;
    bool saw_imported_global = false;
    bool saw_iface_exclude = false;
    bool saw_iface_bad_source = false;
    bool saw_bad_alias_err = false;
    bool saw_bad_import_err = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind == EV_TARGET_DECLARE) {
            if (nob_sv_eq(ev->as.target_declare.name, nob_sv_from_cstr("auto_lib")) &&
                ev->as.target_declare.type == EV_TARGET_LIBRARY_SHARED) {
                saw_auto_shared = true;
            }
        }
        if (ev->kind == EV_TARGET_PROP_SET) {
            if (nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("imp_lib")) &&
                nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("IMPORTED")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("1"))) {
                saw_imported = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("imp_lib")) &&
                nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("IMPORTED_GLOBAL")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("1"))) {
                saw_imported_global = true;
            }
            if (nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("iface")) &&
                nob_sv_eq(ev->as.target_prop_set.key, nob_sv_from_cstr("EXCLUDE_FROM_ALL")) &&
                nob_sv_eq(ev->as.target_prop_set.value, nob_sv_from_cstr("1"))) {
                saw_iface_exclude = true;
            }
        }
        if (ev->kind == EV_TARGET_ADD_SOURCE &&
            nob_sv_eq(ev->as.target_add_source.target_name, nob_sv_from_cstr("iface")) &&
            nob_sv_eq(ev->as.target_add_source.path, nob_sv_from_cstr("EXCLUDE_FROM_ALL"))) {
            saw_iface_bad_source = true;
        }
        if (ev->kind == EV_DIAGNOSTIC && ev->as.diag.severity == EV_DIAG_ERROR) {
            if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("ALIAS target cannot reference another ALIAS target"))) {
                saw_bad_alias_err = true;
            } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("add_library(IMPORTED ...) requires an explicit library type"))) {
                saw_bad_import_err = true;
            }
        }
    }

    ASSERT(saw_auto_shared);
    ASSERT(saw_imported);
    ASSERT(saw_imported_global);
    ASSERT(saw_iface_exclude);
    ASSERT(!saw_iface_bad_source);
    ASSERT(saw_bad_alias_err);
    ASSERT(saw_bad_import_err);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_set_property_target_rejects_alias_and_unknown_target) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_library(real STATIC real.c)\n"
        "add_library(alias_real ALIAS real)\n"
        "set_property(TARGET alias_real PROPERTY OUTPUT_NAME bad_alias)\n"
        "set_property(TARGET missing_t PROPERTY OUTPUT_NAME bad_missing)\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 2);

    bool saw_alias_error = false;
    bool saw_missing_error = false;
    bool emitted_for_alias = false;
    bool emitted_for_missing = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind == EV_DIAGNOSTIC && ev->as.diag.severity == EV_DIAG_ERROR) {
            if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("set_property(TARGET ...) cannot be used on ALIAS targets"))) {
                saw_alias_error = true;
            } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("set_property(TARGET ...) target was not declared"))) {
                saw_missing_error = true;
            }
        }
        if (ev->kind == EV_TARGET_PROP_SET &&
            nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("alias_real"))) {
            emitted_for_alias = true;
        }
        if (ev->kind == EV_TARGET_PROP_SET &&
            nob_sv_eq(ev->as.target_prop_set.target_name, nob_sv_from_cstr("missing_t"))) {
            emitted_for_missing = true;
        }
    }

    ASSERT(saw_alias_error);
    ASSERT(saw_missing_error);
    ASSERT(!emitted_for_alias);
    ASSERT(!emitted_for_missing);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_set_property_source_test_directory_clauses_parse_and_apply) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_executable(src_t main.c)\n"
        "add_test(NAME smoke COMMAND src_t)\n"
        "set_property(SOURCE foo.c DIRECTORY src PROPERTY LANGUAGE C)\n"
        "set_property(SOURCE bar.c TARGET_DIRECTORY src_t PROPERTY LANGUAGE CXX)\n"
        "set_property(TEST smoke DIRECTORY . PROPERTY LABELS fast)\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_source_dir_var_set = false;
    bool saw_source_target_dir_var_set = false;
    bool saw_test_dir_var_set = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind != EV_VAR_SET) continue;
        if (nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("C")) &&
            nob_sv_eq(ev->as.var_set.key,
                      nob_sv_from_cstr("NOBIFY_PROPERTY_SOURCE::DIRECTORY::src::foo.c::LANGUAGE"))) {
            saw_source_dir_var_set = true;
        } else if (nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("CXX")) &&
                   nob_sv_eq(ev->as.var_set.key,
                             nob_sv_from_cstr("NOBIFY_PROPERTY_SOURCE::TARGET_DIRECTORY::src_t::bar.c::LANGUAGE"))) {
            saw_source_target_dir_var_set = true;
        } else if (nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("fast")) &&
                   nob_sv_eq(ev->as.var_set.key,
                             nob_sv_from_cstr("NOBIFY_PROPERTY_TEST::DIRECTORY::.::smoke::LABELS"))) {
            saw_test_dir_var_set = true;
        }
    }

    ASSERT(saw_source_dir_var_set);
    ASSERT(saw_source_target_dir_var_set);
    ASSERT(saw_test_dir_var_set);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_set_property_allows_zero_objects_and_validates_test_lookup) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_library(real STATIC real.c)\n"
        "add_test(NAME smoke COMMAND real)\n"
        "set_property(TARGET PROPERTY OUTPUT_NAME ignored)\n"
        "set_property(SOURCE PROPERTY LANGUAGE C)\n"
        "set_property(INSTALL PROPERTY FOO bar)\n"
        "set_property(TEST PROPERTY LABELS fast)\n"
        "set_property(CACHE PROPERTY VALUE cache_ignore)\n"
        "set_property(TEST smoke PROPERTY LABELS ok)\n"
        "set_property(TEST missing PROPERTY LABELS bad)\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_missing_test_error = false;
    bool saw_smoke_label_set = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind == EV_DIAGNOSTIC &&
            ev->as.diag.severity == EV_DIAG_ERROR &&
            nob_sv_eq(ev->as.diag.cause,
                      nob_sv_from_cstr("set_property(TEST ...) test was not declared in selected directory scope")) &&
            nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("missing"))) {
            saw_missing_test_error = true;
        }
        if (ev->kind == EV_VAR_SET &&
            nob_sv_eq(ev->as.var_set.key, nob_sv_from_cstr("NOBIFY_PROPERTY_TEST::smoke::LABELS")) &&
            nob_sv_eq(ev->as.var_set.value, nob_sv_from_cstr("ok"))) {
            saw_smoke_label_set = true;
        }
    }

    ASSERT(saw_missing_test_error);
    ASSERT(saw_smoke_label_set);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_set_property_cache_requires_existing_entry) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CACHED_X old CACHE STRING \"doc\")\n"
        "set_property(CACHE CACHED_X PROPERTY VALUE new_ok)\n"
        "set_property(CACHE MISSING_X PROPERTY VALUE bad)\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_missing_cache_error = false;
    bool saw_cache_value_update = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind == EV_DIAGNOSTIC && ev->as.diag.severity == EV_DIAG_ERROR &&
            nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("set_property(CACHE ...) cache entry does not exist"))) {
            saw_missing_cache_error = true;
        }
        if (ev->kind == EV_SET_CACHE_ENTRY &&
            nob_sv_eq(ev->as.cache_entry.key, nob_sv_from_cstr("CACHED_X")) &&
            nob_sv_eq(ev->as.cache_entry.value, nob_sv_from_cstr("new_ok"))) {
            saw_cache_value_update = true;
        }
    }

    ASSERT(saw_missing_cache_error);
    ASSERT(saw_cache_value_update);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_message_mode_severity_mapping) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "message(NOTICE n)\n"
        "message(STATUS s)\n"
        "message(VERBOSE v)\n"
        "message(DEBUG d)\n"
        "message(TRACE t)\n"
        "message(WARNING w)\n"
        "message(AUTHOR_WARNING aw)\n"
        "message(DEPRECATION dep)\n"
        "message(SEND_ERROR se)\n"
        "message(CHECK_START probe)\n"
        "message(CHECK_PASS ok)\n"
        "message(CHECK_START probe2)\n"
        "message(CHECK_FAIL fail)\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 3);
    ASSERT(report->error_count == 1);

    size_t warning_diag_count = 0;
    size_t error_diag_count = 0;
    bool saw_check_pass_cause = false;
    bool saw_check_fail_cause = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity == EV_DIAG_WARNING) warning_diag_count++;
        if (ev->as.diag.severity == EV_DIAG_ERROR) error_diag_count++;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("probe - ok"))) saw_check_pass_cause = true;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("probe2 - fail"))) saw_check_fail_cause = true;
    }

    ASSERT(warning_diag_count == 3);
    ASSERT(error_diag_count == 1);
    ASSERT(!saw_check_pass_cause);
    ASSERT(!saw_check_fail_cause);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_message_check_pass_without_start_is_error) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(temp_arena, "message(CHECK_PASS done)\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool found = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause,
                      nob_sv_from_cstr("message(CHECK_PASS/CHECK_FAIL) requires a preceding CHECK_START"))) {
            found = true;
            break;
        }
    }
    ASSERT(found);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_message_deprecation_respects_control_variables) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_WARN_DEPRECATED FALSE)\n"
        "message(DEPRECATION hidden)\n"
        "set(CMAKE_WARN_DEPRECATED TRUE)\n"
        "message(DEPRECATION shown)\n"
        "set(CMAKE_ERROR_DEPRECATED TRUE)\n"
        "message(DEPRECATION err)\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 1);
    ASSERT(report->error_count == 1);

    bool saw_hidden = false;
    bool saw_shown_warn = false;
    bool saw_err_error = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind != EV_DIAGNOSTIC) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("hidden"))) saw_hidden = true;
        if (ev->as.diag.severity == EV_DIAG_WARNING &&
            nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("shown"))) {
            saw_shown_warn = true;
        }
        if (ev->as.diag.severity == EV_DIAG_ERROR &&
            nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("err"))) {
            saw_err_error = true;
        }
    }
    ASSERT(!saw_hidden);
    ASSERT(saw_shown_warn);
    ASSERT(saw_err_error);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_message_configure_log_persists_yaml_file) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "message(CHECK_START feature-probe)\n"
        "message(CONFIGURE_LOG probe-start)\n"
        "message(CHECK_PASS yes)\n"
        "message(CONFIGURE_LOG probe-end)\n");
    ASSERT(evaluator_run(ctx, root));

    String_View log_text = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, "./CMakeFiles/CMakeConfigureLog.yaml", &log_text));
    ASSERT(sv_contains_sv(log_text, nob_sv_from_cstr("kind: \"message-v1\"")));
    ASSERT(sv_contains_sv(log_text, nob_sv_from_cstr("probe-start")));
    ASSERT(sv_contains_sv(log_text, nob_sv_from_cstr("probe-end")));
    ASSERT(sv_contains_sv(log_text, nob_sv_from_cstr("feature-probe")));

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_set_and_unset_env_forms) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(ENV{NOBIFY_ENV_A} valueA)\n"
        "set(ENV{NOBIFY_ENV_A})\n"
        "set(ENV{NOBIFY_ENV_B} valueB ignored)\n"
        "add_executable(env_forms main.c)\n"
        "target_compile_definitions(env_forms PRIVATE A=$ENV{NOBIFY_ENV_A} B=$ENV{NOBIFY_ENV_B})\n"
        "unset(ENV{NOBIFY_ENV_B})\n"
        "target_compile_definitions(env_forms PRIVATE B2=$ENV{NOBIFY_ENV_B})\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 1);
    ASSERT(report->error_count == 0);

    bool saw_extra_args_warn = false;
    bool saw_a_empty = false;
    bool saw_b_value = false;
    bool saw_b2_empty = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind == EV_DIAGNOSTIC &&
            ev->as.diag.severity == EV_DIAG_WARNING &&
            nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("set(ENV{...}) ignores extra arguments after value"))) {
            saw_extra_args_warn = true;
        }
        if (ev->kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("A="))) saw_a_empty = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("B=valueB"))) saw_b_value = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("B2="))) saw_b2_empty = true;
    }
    ASSERT(saw_extra_args_warn);
    ASSERT(saw_a_empty);
    ASSERT(saw_b_value);
    ASSERT(saw_b2_empty);

    const char *env_b = getenv("NOBIFY_ENV_B");
    ASSERT(env_b == NULL);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_unset_env_rejects_options) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(temp_arena, "unset(ENV{NOBIFY_ENV_OPT} CACHE)\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_error = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("unset(ENV{...}) does not accept options"))) {
            saw_error = true;
            break;
        }
    }
    ASSERT(saw_error);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_set_cache_cmp0126_old_and_new_semantics) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CACHE_OLD local_old)\n"
        "set(CACHE_OLD cache_old CACHE STRING \"doc\")\n"
        "add_executable(cache_old_t main.c)\n"
        "target_compile_definitions(cache_old_t PRIVATE OLD_CA=${CACHE_OLD})\n"
        "cmake_policy(SET CMP0126 NEW)\n"
        "set(CACHE_NEW local_new)\n"
        "set(CACHE_NEW cache_new CACHE STRING \"doc\" FORCE)\n"
        "add_executable(cache_new_t main.c)\n"
        "target_compile_definitions(cache_new_t PRIVATE NEW_CB=${CACHE_NEW})\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    bool saw_old_binding_from_cache = false;
    bool saw_new_binding_from_local = false;
    bool saw_cache_old_set = false;
    bool saw_cache_new_set = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind == EV_SET_CACHE_ENTRY) {
            if (nob_sv_eq(ev->as.cache_entry.key, nob_sv_from_cstr("CACHE_OLD")) &&
                nob_sv_eq(ev->as.cache_entry.value, nob_sv_from_cstr("cache_old"))) {
                saw_cache_old_set = true;
            }
            if (nob_sv_eq(ev->as.cache_entry.key, nob_sv_from_cstr("CACHE_NEW")) &&
                nob_sv_eq(ev->as.cache_entry.value, nob_sv_from_cstr("cache_new"))) {
                saw_cache_new_set = true;
            }
        }
        if (ev->kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("OLD_CA=cache_old"))) {
            saw_old_binding_from_cache = true;
        }
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("NEW_CB=local_new"))) {
            saw_new_binding_from_local = true;
        }
    }
    ASSERT(saw_cache_old_set);
    ASSERT(saw_cache_new_set);
    ASSERT(saw_old_binding_from_cache);
    ASSERT(saw_new_binding_from_local);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_set_cache_policy_version_defaults_cmp0126_to_new) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "cmake_minimum_required(VERSION 3.28)\n"
        "set(CACHE_VER local_ver)\n"
        "set(CACHE_VER cache_ver CACHE STRING \"doc\")\n"
        "add_executable(cache_ver_t main.c)\n"
        "target_compile_definitions(cache_ver_t PRIVATE VER=${CACHE_VER})\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    bool saw_cache_ver_set = false;
    bool saw_local_binding = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind == EV_SET_CACHE_ENTRY &&
            nob_sv_eq(ev->as.cache_entry.key, nob_sv_from_cstr("CACHE_VER")) &&
            nob_sv_eq(ev->as.cache_entry.value, nob_sv_from_cstr("cache_ver"))) {
            saw_cache_ver_set = true;
        }
        if (ev->kind == EV_TARGET_COMPILE_DEFINITIONS &&
            nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("VER=local_ver"))) {
            saw_local_binding = true;
        }
    }
    ASSERT(saw_cache_ver_set);
    ASSERT(saw_local_binding);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_find_package_no_module_names_configs_path_suffixes_and_registry_view) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(MAKE_DIRECTORY fp_cfg_root/sfx)\n"
        "file(WRITE fp_cfg_root/sfx/AltCfg.cmake [=[set(DemoFP_FOUND 1)\n"
        "set(DemoFP_SOURCE config)\n"
        "]=])\n"
        "find_package(DemoFP NO_MODULE NAMES AltName CONFIGS AltCfg.cmake PATH_SUFFIXES sfx PATHS fp_cfg_root REGISTRY_VIEW HOST QUIET)\n"
        "add_executable(fp_cfg_probe main.c)\n"
        "target_compile_definitions(fp_cfg_probe PRIVATE FOUND=${DemoFP_FOUND} SRC=${DemoFP_SOURCE} RV=${DemoFP_FIND_REGISTRY_VIEW})\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    bool saw_find_event = false;
    bool saw_found = false;
    bool saw_src_config = false;
    bool saw_rv_host = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind == EV_FIND_PACKAGE &&
            nob_sv_eq(ev->as.find_package.package_name, nob_sv_from_cstr("DemoFP"))) {
            saw_find_event = true;
            ASSERT(ev->as.find_package.found);
            ASSERT(nob_sv_eq(ev->as.find_package.mode, nob_sv_from_cstr("CONFIG")));
        }
        if (ev->kind == EV_TARGET_COMPILE_DEFINITIONS &&
            nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("fp_cfg_probe"))) {
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("FOUND=1"))) saw_found = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("SRC=config"))) saw_src_config = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("RV=HOST"))) saw_rv_host = true;
        }
    }

    ASSERT(saw_find_event);
    ASSERT(saw_found);
    ASSERT(saw_src_config);
    ASSERT(saw_rv_host);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_find_package_auto_prefers_config_when_requested) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(MAKE_DIRECTORY fp_pref/mod)\n"
        "file(MAKE_DIRECTORY fp_pref/cfg)\n"
        "file(WRITE fp_pref/mod/FindPrefPkg.cmake [=[set(PrefPkg_FOUND 1)\n"
        "set(PrefPkg_FROM module)\n"
        "]=])\n"
        "file(WRITE fp_pref/cfg/PrefPkgConfig.cmake [=[set(PrefPkg_FOUND 1)\n"
        "set(PrefPkg_FROM config)\n"
        "]=])\n"
        "set(CMAKE_MODULE_PATH fp_pref/mod)\n"
        "set(CMAKE_PREFIX_PATH fp_pref/cfg)\n"
        "set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)\n"
        "find_package(PrefPkg QUIET)\n"
        "add_executable(fp_pref_probe main.c)\n"
        "target_compile_definitions(fp_pref_probe PRIVATE FROM=${PrefPkg_FROM})\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    bool saw_config_location = false;
    bool saw_from_config = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind == EV_FIND_PACKAGE &&
            nob_sv_eq(ev->as.find_package.package_name, nob_sv_from_cstr("PrefPkg"))) {
            saw_config_location =
                nob_sv_eq(ev->as.find_package.location, nob_sv_from_cstr("./fp_pref/cfg/PrefPkgConfig.cmake"));
        }
        if (ev->kind == EV_TARGET_COMPILE_DEFINITIONS &&
            nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("fp_pref_probe")) &&
            nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("FROM=config"))) {
            saw_from_config = true;
        }
    }

    ASSERT(saw_config_location);
    ASSERT(saw_from_config);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_project_full_signature_and_variable_surface) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    ASSERT(nob_mkdir_if_not_exists("subproj"));
    const char *sub_cmake =
        "project(SubProj DESCRIPTION subdesc HOMEPAGE_URL https://sub LANGUAGES NONE)\n"
        "add_executable(sub_probe sub.c)\n"
        "target_compile_definitions(sub_probe PRIVATE SUB_TOP=${PROJECT_IS_TOP_LEVEL} SUB_NAMED_TOP=${SubProj_IS_TOP_LEVEL} ROOT_NAME=${CMAKE_PROJECT_NAME} SUB_HOME=${PROJECT_HOMEPAGE_URL})\n";
    ASSERT(nob_write_entire_file("subproj/CMakeLists.txt", sub_cmake, strlen(sub_cmake)));

    Ast_Root root = parse_cmake(
        temp_arena,
        "project(MainProj VERSION 1.2.3.4 DESCRIPTION rootdesc HOMEPAGE_URL https://root LANGUAGES C)\n"
        "add_executable(root_probe main.c)\n"
        "target_compile_definitions(root_probe PRIVATE ROOT_TOP=${PROJECT_IS_TOP_LEVEL} ROOT_NAMED_TOP=${MainProj_IS_TOP_LEVEL} ROOT_MAJOR=${PROJECT_VERSION_MAJOR} ROOT_MINOR=${PROJECT_VERSION_MINOR} ROOT_PATCH=${PROJECT_VERSION_PATCH} ROOT_TWEAK=${PROJECT_VERSION_TWEAK} ROOT_CMAKE_VER=${CMAKE_PROJECT_VERSION} ROOT_HOME=${PROJECT_HOMEPAGE_URL})\n"
        "add_subdirectory(subproj)\n"
        "target_compile_definitions(root_probe PRIVATE ROOT_NAME_AFTER=${CMAKE_PROJECT_NAME} ROOT_HOME_AFTER=${CMAKE_PROJECT_HOMEPAGE_URL})\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    bool saw_main_project_event = false;
    bool saw_sub_project_event = false;
    bool saw_root_top = false;
    bool saw_root_named_top = false;
    bool saw_root_major = false;
    bool saw_root_minor = false;
    bool saw_root_patch = false;
    bool saw_root_tweak = false;
    bool saw_root_cmake_ver = false;
    bool saw_root_home = false;
    bool saw_root_name_after = false;
    bool saw_root_home_after = false;
    bool saw_sub_top_false = false;
    bool saw_sub_named_top_false = false;
    bool saw_sub_root_name = false;
    bool saw_sub_home = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind == EV_PROJECT_DECLARE) {
            if (nob_sv_eq(ev->as.project_declare.name, nob_sv_from_cstr("MainProj")) &&
                nob_sv_eq(ev->as.project_declare.version, nob_sv_from_cstr("1.2.3.4")) &&
                nob_sv_eq(ev->as.project_declare.description, nob_sv_from_cstr("rootdesc")) &&
                nob_sv_eq(ev->as.project_declare.languages, nob_sv_from_cstr("C"))) {
                saw_main_project_event = true;
            } else if (nob_sv_eq(ev->as.project_declare.name, nob_sv_from_cstr("SubProj")) &&
                       nob_sv_eq(ev->as.project_declare.version, nob_sv_from_cstr("")) &&
                       nob_sv_eq(ev->as.project_declare.description, nob_sv_from_cstr("subdesc")) &&
                       nob_sv_eq(ev->as.project_declare.languages, nob_sv_from_cstr(""))) {
                saw_sub_project_event = true;
            }
        }

        if (ev->kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("root_probe"))) {
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_TOP=TRUE"))) saw_root_top = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_NAMED_TOP=TRUE"))) saw_root_named_top = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_MAJOR=1"))) saw_root_major = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_MINOR=2"))) saw_root_minor = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_PATCH=3"))) saw_root_patch = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_TWEAK=4"))) saw_root_tweak = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_CMAKE_VER=1.2.3.4"))) saw_root_cmake_ver = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_HOME=https://root"))) saw_root_home = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_NAME_AFTER=MainProj"))) saw_root_name_after = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_HOME_AFTER=https://root"))) saw_root_home_after = true;
        }
        if (nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("sub_probe"))) {
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("SUB_TOP=FALSE")) ||
                nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("SUB_TOP=0")) ||
                nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("SUB_TOP=OFF"))) {
                saw_sub_top_false = true;
            }
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("SUB_NAMED_TOP=FALSE")) ||
                nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("SUB_NAMED_TOP=0")) ||
                nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("SUB_NAMED_TOP=OFF"))) {
                saw_sub_named_top_false = true;
            }
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("ROOT_NAME=MainProj"))) saw_sub_root_name = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("SUB_HOME=https://sub"))) saw_sub_home = true;
        }
    }

    ASSERT(saw_main_project_event);
    ASSERT(saw_sub_project_event);
    ASSERT(saw_root_top);
    ASSERT(saw_root_named_top);
    ASSERT(saw_root_major);
    ASSERT(saw_root_minor);
    ASSERT(saw_root_patch);
    ASSERT(saw_root_tweak);
    ASSERT(saw_root_cmake_ver);
    ASSERT(saw_root_home);
    ASSERT(saw_root_name_after);
    ASSERT(saw_root_home_after);
    ASSERT(saw_sub_top_false);
    ASSERT(saw_sub_named_top_false);
    ASSERT(saw_sub_root_name);
    ASSERT(saw_sub_home);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_project_cmp0048_new_clears_and_old_preserves_version_vars_without_version_arg) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "cmake_policy(SET CMP0048 NEW)\n"
        "set(PROJECT_VERSION stale)\n"
        "set(PROJECT_VERSION_MAJOR stale_major)\n"
        "project(NewNoVer LANGUAGES NONE)\n"
        "add_executable(project_new_nover main.c)\n"
        "target_compile_definitions(project_new_nover PRIVATE NEW_VER=${PROJECT_VERSION} NEW_MAJ=${PROJECT_VERSION_MAJOR})\n"
        "cmake_policy(SET CMP0048 OLD)\n"
        "set(PROJECT_VERSION keep)\n"
        "set(PROJECT_VERSION_MAJOR keep_major)\n"
        "project(OldNoVer LANGUAGES NONE)\n"
        "add_executable(project_old_nover main.c)\n"
        "target_compile_definitions(project_old_nover PRIVATE OLD_VER=${PROJECT_VERSION} OLD_MAJ=${PROJECT_VERSION_MAJOR})\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    bool saw_new_ver_empty = false;
    bool saw_new_maj_empty = false;
    bool saw_old_ver_keep = false;
    bool saw_old_maj_keep = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("project_new_nover"))) {
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("NEW_VER="))) saw_new_ver_empty = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("NEW_MAJ="))) saw_new_maj_empty = true;
        } else if (nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("project_old_nover"))) {
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("OLD_VER=keep"))) saw_old_ver_keep = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("OLD_MAJ=keep_major"))) saw_old_maj_keep = true;
        }
    }

    ASSERT(saw_new_ver_empty);
    ASSERT(saw_new_maj_empty);
    ASSERT(saw_old_ver_keep);
    ASSERT(saw_old_maj_keep);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_project_rejects_invalid_signature_forms) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "project(BadVer VERSION 1.a)\n"
        "project(BadLang LANGUAGES NONE C)\n"
        "project(BadMissingVersion VERSION)\n"
        "project(BadDesc DESCRIPTION)\n"
        "project(BadHome HOMEPAGE_URL)\n"
        "project(BadUnexpected VERSION 1.0 C)\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 6);

    bool saw_bad_version = false;
    bool saw_none_mix = false;
    bool saw_missing_version = false;
    bool saw_missing_desc = false;
    bool saw_missing_home = false;
    bool saw_unexpected = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("project(VERSION ...) expects numeric components"))) {
            saw_bad_version = true;
        } else if (nob_sv_eq(ev->as.diag.cause,
                              nob_sv_from_cstr("project() LANGUAGES NONE cannot be combined with other languages"))) {
            saw_none_mix = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("project(VERSION ...) requires a version value"))) {
            saw_missing_version = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("project(DESCRIPTION ...) requires a value"))) {
            saw_missing_desc = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("project(HOMEPAGE_URL ...) requires a value"))) {
            saw_missing_home = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("project() received unexpected argument in keyword signature"))) {
            saw_unexpected = true;
        }
    }

    ASSERT(saw_bad_version);
    ASSERT(saw_none_mix);
    ASSERT(saw_missing_version);
    ASSERT(saw_missing_desc);
    ASSERT(saw_missing_home);
    ASSERT(saw_unexpected);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_policy_known_unknown_and_if_predicate) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "cmake_policy(SET CMP0077 NEW)\n"
        "cmake_policy(GET CMP0077 POL_OUT)\n"
        "if(POLICY CMP0077)\n"
        "  set(IF_KNOWN 1)\n"
        "endif()\n"
        "if(POLICY CMP9999)\n"
        "  set(IF_UNKNOWN 1)\n"
        "endif()\n"
        "cmake_policy(GET CMP9999 BAD_OUT)\n"
        "add_executable(policy_pred main.c)\n"
        "target_compile_definitions(policy_pred PRIVATE POL_OUT=${POL_OUT} IF_KNOWN=${IF_KNOWN} IF_UNKNOWN=${IF_UNKNOWN})\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 1);

    bool saw_pol_out = false;
    bool saw_if_known = false;
    bool saw_if_unknown_empty = false;

    bool saw_unknown_policy_error = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind == EV_DIAGNOSTIC && ev->as.diag.severity == EV_DIAG_ERROR) {
            if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_policy(GET ...) requires a known CMP policy id"))) {
                saw_unknown_policy_error = true;
            }
        }
        if (ev->kind == EV_TARGET_COMPILE_DEFINITIONS) {
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("POL_OUT=NEW"))) {
                saw_pol_out = true;
            } else if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("IF_KNOWN=1"))) {
                saw_if_known = true;
            } else if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("IF_UNKNOWN="))) {
                saw_if_unknown_empty = true;
            }
        }
    }
    ASSERT(saw_unknown_policy_error);
    ASSERT(saw_pol_out);
    ASSERT(saw_if_known);
    ASSERT(saw_if_unknown_empty);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_policy_strict_arity_and_version_validation) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "cmake_policy(PUSH EXTRA)\n"
        "cmake_policy(POP EXTRA)\n"
        "cmake_policy(SET CMP0077 NEW EXTRA)\n"
        "cmake_policy(GET CMP0077)\n"
        "cmake_policy(VERSION 3.10 3.11)\n"
        "cmake_policy(VERSION 2.3)\n"
        "cmake_policy(VERSION 3.29)\n"
        "cmake_policy(VERSION 3.20...3.10)\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 8);

    bool saw_push_arity = false;
    bool saw_pop_arity = false;
    bool saw_set_arity = false;
    bool saw_get_arity = false;
    bool saw_version_arity = false;
    bool saw_min_floor = false;
    bool saw_min_running = false;
    bool saw_max_lt_min = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_policy(PUSH) does not accept extra arguments"))) {
            saw_push_arity = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_policy(POP) does not accept extra arguments"))) {
            saw_pop_arity = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_policy(SET ...) expects exactly policy id and value"))) {
            saw_set_arity = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_policy(GET ...) expects exactly policy id and output variable"))) {
            saw_get_arity = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_policy(VERSION ...) expects exactly one version argument"))) {
            saw_version_arity = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_policy(VERSION ...) requires minimum version >= 2.4"))) {
            saw_min_floor = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_policy(VERSION ...) min version exceeds evaluator baseline"))) {
            saw_min_running = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_policy(VERSION ...) requires max version >= min version"))) {
            saw_max_lt_min = true;
        }
    }

    ASSERT(saw_push_arity);
    ASSERT(saw_pop_arity);
    ASSERT(saw_set_arity);
    ASSERT(saw_get_arity);
    ASSERT(saw_version_arity);
    ASSERT(saw_min_floor);
    ASSERT(saw_min_running);
    ASSERT(saw_max_lt_min);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_cmake_minimum_required_inside_function_applies_policy_not_variable) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "cmake_policy(VERSION 3.10)\n"
        "function(set_local_min)\n"
        "  cmake_minimum_required(VERSION 3.28)\n"
        "endfunction()\n"
        "set_local_min()\n"
        "cmake_policy(GET CMP0124 OUT_POL)\n"
        "add_executable(minreq_func main.c)\n"
        "target_compile_definitions(minreq_func PRIVATE OUT_POL=${OUT_POL} MIN_VER=${CMAKE_MINIMUM_REQUIRED_VERSION})\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 0);

    bool saw_out_pol = false;
    bool saw_min_ver_empty = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("OUT_POL=NEW"))) {
            saw_out_pol = true;
        }
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("MIN_VER="))) {
            saw_min_ver_empty = true;
        }
    }
    ASSERT(saw_out_pol);
    ASSERT(saw_min_ver_empty);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_cpack_commands_require_cpackcomponent_module_and_parse_component_extras) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Evaluator_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Evaluator_Context *ctx = evaluator_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "project(CPackGate)\n"
        "cpack_add_component(core)\n"
        "include(CPackComponent)\n"
        "cpack_add_component(core DISPLAY_NAME Core ARCHIVE_FILE core.txz PLIST core.plist)\n");
    ASSERT(evaluator_run(ctx, root));

    const Eval_Run_Report *report = evaluator_get_run_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_gate_error = false;
    bool saw_component_event = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->kind == EV_DIAGNOSTIC &&
            ev->as.diag.severity == EV_DIAG_ERROR &&
            nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("Unknown command")) &&
            nob_sv_eq(ev->as.diag.hint,
                      nob_sv_from_cstr("include(CPackComponent) must be called before using this command"))) {
            saw_gate_error = true;
        }
        if (ev->kind == EV_CPACK_ADD_COMPONENT &&
            nob_sv_eq(ev->as.cpack_add_component.name, nob_sv_from_cstr("core")) &&
            nob_sv_eq(ev->as.cpack_add_component.archive_file, nob_sv_from_cstr("core.txz")) &&
            nob_sv_eq(ev->as.cpack_add_component.plist, nob_sv_from_cstr("core.plist"))) {
            saw_component_event = true;
        }
    }
    ASSERT(saw_gate_error);
    ASSERT(saw_component_event);

    evaluator_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

void run_evaluator_v2_tests(int *passed, int *failed) {
    Test_Workspace ws = {0};
    char prev_cwd[_TINYDIR_PATH_MAX] = {0};
    bool prepared = test_ws_prepare(&ws, "evaluator");
    bool entered = false;

    if (!prepared) {
        nob_log(NOB_ERROR, "evaluator suite: failed to prepare isolated workspace");
        if (failed) (*failed)++;
        return;
    }

    entered = test_ws_enter(&ws, prev_cwd, sizeof(prev_cwd));
    if (!entered) {
        nob_log(NOB_ERROR, "evaluator suite: failed to enter isolated workspace");
        if (failed) (*failed)++;
        (void)test_ws_cleanup(&ws);
        return;
    }

    test_evaluator_golden_all_cases(passed, failed);
    test_evaluator_public_api_profile_and_report_snapshot(passed, failed);
    test_evaluator_flow_commands_reject_extra_arguments(passed, failed);
    test_evaluator_enable_testing_does_not_set_build_testing_variable(passed, failed);
    test_evaluator_enable_testing_rejects_extra_arguments(passed, failed);
    test_evaluator_add_definitions_routes_d_flags_to_compile_definitions(passed, failed);
    test_evaluator_target_compile_definitions_normalizes_dash_d_items(passed, failed);
    test_evaluator_add_custom_command_target_validates_signature_and_target(passed, failed);
    test_evaluator_add_custom_command_output_validates_conflicts(passed, failed);
    test_evaluator_return_in_macro_is_error_and_does_not_unwind_macro_body(passed, failed);
    test_evaluator_return_cmp0140_old_ignores_args_and_new_enables_propagate(passed, failed);
    test_evaluator_list_transform_genex_strip_and_output_variable(passed, failed);
    test_evaluator_list_transform_output_variable_requires_single_output_var(passed, failed);
    test_evaluator_math_rejects_empty_and_incomplete_invocations(passed, failed);
    test_evaluator_set_target_properties_rejects_alias_target(passed, failed);
    test_evaluator_add_executable_imported_and_alias_signatures(passed, failed);
    test_evaluator_add_library_imported_alias_and_default_type(passed, failed);
    test_evaluator_set_property_target_rejects_alias_and_unknown_target(passed, failed);
    test_evaluator_set_property_source_test_directory_clauses_parse_and_apply(passed, failed);
    test_evaluator_set_property_cache_requires_existing_entry(passed, failed);
    test_evaluator_set_property_allows_zero_objects_and_validates_test_lookup(passed, failed);
    test_evaluator_message_mode_severity_mapping(passed, failed);
    test_evaluator_message_check_pass_without_start_is_error(passed, failed);
    test_evaluator_message_deprecation_respects_control_variables(passed, failed);
    test_evaluator_message_configure_log_persists_yaml_file(passed, failed);
    test_evaluator_set_and_unset_env_forms(passed, failed);
    test_evaluator_unset_env_rejects_options(passed, failed);
    test_evaluator_set_cache_cmp0126_old_and_new_semantics(passed, failed);
    test_evaluator_set_cache_policy_version_defaults_cmp0126_to_new(passed, failed);
    test_evaluator_find_package_no_module_names_configs_path_suffixes_and_registry_view(passed, failed);
    test_evaluator_find_package_auto_prefers_config_when_requested(passed, failed);
    test_evaluator_project_full_signature_and_variable_surface(passed, failed);
    test_evaluator_project_cmp0048_new_clears_and_old_preserves_version_vars_without_version_arg(passed, failed);
    test_evaluator_project_rejects_invalid_signature_forms(passed, failed);
    test_evaluator_policy_known_unknown_and_if_predicate(passed, failed);
    test_evaluator_policy_strict_arity_and_version_validation(passed, failed);
    test_evaluator_cmake_minimum_required_inside_function_applies_policy_not_variable(passed, failed);
    test_evaluator_cpack_commands_require_cpackcomponent_module_and_parse_component_extras(passed, failed);

    if (!test_ws_leave(prev_cwd)) {
        if (failed) (*failed)++;
    }
    if (!test_ws_cleanup(&ws)) {
        nob_log(NOB_ERROR, "evaluator suite: failed to cleanup isolated workspace");
        if (failed) (*failed)++;
    }
}
