#include "test_v2_assert.h"
#include "test_v2_suite.h"

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
            nob_sb_append_cstr(sb, nob_temp_sprintf(" vis=%s", visibility_name(ev->as.target_compile_options.visibility)));
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
            nob_sb_append_cstr(sb, nob_temp_sprintf(" vis=%s", visibility_name(ev->as.target_link_options.visibility)));
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
    if (cases.count != 96) {
        nob_log(NOB_ERROR, "golden: unexpected evaluator case count: got=%zu expected=96", cases.count);
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

void run_evaluator_v2_tests(int *passed, int *failed) {
    test_evaluator_golden_all_cases(passed, failed);
}
