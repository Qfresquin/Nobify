#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NOB_IMPLEMENTATION
#define NOB_NO_ECHO
#include "nob.h"

#if defined(_WIN32)
#include <process.h>
#define SNAPSHOT_GETPID() _getpid()
#else
#include <unistd.h>
#define SNAPSHOT_GETPID() getpid()
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define SNAPSHOT_MANIFEST_PATH "test_v2/artifact_parity/real_projects/manifest.json"
#define SNAPSHOT_ARCHIVE_ROOT "test_v2/artifact_parity/real_projects/archives"
#define SNAPSHOT_PATH_MAX PATH_MAX

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} Snapshot_String_List;

typedef struct {
    char *name;
    char *upstream_url;
    char *archive_url;
    char *pinned_ref;
    char *archive_prefix;
    Snapshot_String_List retain_paths;
} Snapshot_Project;

typedef struct {
    Snapshot_Project *items;
    size_t count;
    size_t capacity;
} Snapshot_Project_List;

static char *snapshot_strdup(const char *text) {
    size_t size = 0;
    char *copy = NULL;
    if (!text) return NULL;
    size = strlen(text) + 1;
    copy = (char*)malloc(size);
    if (!copy) {
        nob_log(NOB_ERROR, "out of memory while duplicating string");
        return NULL;
    }
    memcpy(copy, text, size);
    return copy;
}

static void snapshot_string_list_free(Snapshot_String_List *list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; ++i) {
        free(list->items[i]);
    }
    nob_da_free(*list);
    list->count = 0;
    list->capacity = 0;
}

static void snapshot_project_free(Snapshot_Project *project) {
    if (!project) return;
    free(project->name);
    free(project->upstream_url);
    free(project->archive_url);
    free(project->pinned_ref);
    free(project->archive_prefix);
    snapshot_string_list_free(&project->retain_paths);
    memset(project, 0, sizeof(*project));
}

static void snapshot_project_list_free(Snapshot_Project_List *projects) {
    if (!projects) return;
    for (size_t i = 0; i < projects->count; ++i) {
        snapshot_project_free(&projects->items[i]);
    }
    nob_da_free(*projects);
    projects->count = 0;
    projects->capacity = 0;
}

static void snapshot_skip_ws(Nob_String_View *sv) {
    while (sv && sv->count > 0 && isspace((unsigned char)sv->data[0])) {
        nob_sv_chop_left(sv, 1);
    }
}

static bool snapshot_consume_char(Nob_String_View *sv, char expected) {
    snapshot_skip_ws(sv);
    if (!sv || sv->count == 0 || sv->data[0] != expected) {
        nob_log(NOB_ERROR, "json parse error: expected '%c'", expected);
        return false;
    }
    nob_sv_chop_left(sv, 1);
    return true;
}

static bool snapshot_parse_json_string(Nob_String_View *sv, char **out) {
    Nob_String_Builder sb = {0};
    bool result = false;

    if (out) *out = NULL;
    snapshot_skip_ws(sv);
    if (!sv || !out || sv->count == 0 || sv->data[0] != '"') {
        nob_log(NOB_ERROR, "json parse error: expected string");
        return false;
    }

    nob_sv_chop_left(sv, 1);
    while (sv->count > 0) {
        char ch = sv->data[0];
        nob_sv_chop_left(sv, 1);
        if (ch == '"') {
            nob_da_append(&sb, '\0');
            *out = snapshot_strdup(sb.items ? sb.items : "");
            result = *out != NULL;
            goto defer;
        }
        if (ch == '\\') {
            char esc = 0;
            if (sv->count == 0) {
                nob_log(NOB_ERROR, "json parse error: truncated escape sequence");
                goto defer;
            }
            esc = sv->data[0];
            nob_sv_chop_left(sv, 1);
            switch (esc) {
            case '"': ch = '"'; break;
            case '\\': ch = '\\'; break;
            case '/': ch = '/'; break;
            case 'b': ch = '\b'; break;
            case 'f': ch = '\f'; break;
            case 'n': ch = '\n'; break;
            case 'r': ch = '\r'; break;
            case 't': ch = '\t'; break;
            default:
                nob_log(NOB_ERROR, "json parse error: unsupported escape \\%c", esc);
                goto defer;
            }
        }
        nob_da_append(&sb, ch);
    }

    nob_log(NOB_ERROR, "json parse error: unterminated string");

defer:
    nob_da_free(sb);
    return result;
}

static bool snapshot_skip_json_value(Nob_String_View *sv);

static bool snapshot_skip_json_array(Nob_String_View *sv) {
    if (!snapshot_consume_char(sv, '[')) return false;
    snapshot_skip_ws(sv);
    if (sv->count > 0 && sv->data[0] == ']') {
        nob_sv_chop_left(sv, 1);
        return true;
    }
    for (;;) {
        if (!snapshot_skip_json_value(sv)) return false;
        snapshot_skip_ws(sv);
        if (sv->count > 0 && sv->data[0] == ']') {
            nob_sv_chop_left(sv, 1);
            return true;
        }
        if (!snapshot_consume_char(sv, ',')) return false;
    }
}

static bool snapshot_skip_json_object(Nob_String_View *sv) {
    char *key = NULL;
    if (!snapshot_consume_char(sv, '{')) return false;
    snapshot_skip_ws(sv);
    if (sv->count > 0 && sv->data[0] == '}') {
        nob_sv_chop_left(sv, 1);
        return true;
    }
    for (;;) {
        free(key);
        key = NULL;
        if (!snapshot_parse_json_string(sv, &key)) goto fail;
        if (!snapshot_consume_char(sv, ':')) goto fail;
        if (!snapshot_skip_json_value(sv)) goto fail;
        snapshot_skip_ws(sv);
        if (sv->count > 0 && sv->data[0] == '}') {
            nob_sv_chop_left(sv, 1);
            free(key);
            return true;
        }
        if (!snapshot_consume_char(sv, ',')) goto fail;
    }

fail:
    free(key);
    return false;
}

static bool snapshot_skip_json_atom(Nob_String_View *sv) {
    size_t count = 0;
    snapshot_skip_ws(sv);
    while (count < sv->count) {
        char ch = sv->data[count];
        if (isspace((unsigned char)ch) || ch == ',' || ch == ']' || ch == '}') break;
        count += 1;
    }
    if (count == 0) {
        nob_log(NOB_ERROR, "json parse error: expected atom");
        return false;
    }
    nob_sv_chop_left(sv, count);
    return true;
}

static bool snapshot_skip_json_value(Nob_String_View *sv) {
    snapshot_skip_ws(sv);
    if (!sv || sv->count == 0) {
        nob_log(NOB_ERROR, "json parse error: unexpected end of input");
        return false;
    }
    switch (sv->data[0]) {
    case '"': {
        char *tmp = NULL;
        bool ok = snapshot_parse_json_string(sv, &tmp);
        free(tmp);
        return ok;
    }
    case '{': return snapshot_skip_json_object(sv);
    case '[': return snapshot_skip_json_array(sv);
    default: return snapshot_skip_json_atom(sv);
    }
}

static bool snapshot_parse_string_array(Nob_String_View *sv, Snapshot_String_List *out) {
    char *value = NULL;
    if (!out) return false;
    if (!snapshot_consume_char(sv, '[')) return false;
    snapshot_skip_ws(sv);
    if (sv->count > 0 && sv->data[0] == ']') {
        nob_sv_chop_left(sv, 1);
        return true;
    }
    for (;;) {
        value = NULL;
        if (!snapshot_parse_json_string(sv, &value)) return false;
        nob_da_append(out, value);
        snapshot_skip_ws(sv);
        if (sv->count > 0 && sv->data[0] == ']') {
            nob_sv_chop_left(sv, 1);
            return true;
        }
        if (!snapshot_consume_char(sv, ',')) return false;
    }
}

static bool snapshot_parse_project(Nob_String_View *sv, Snapshot_Project *project) {
    char *key = NULL;
    bool ok = false;

    if (!project) return false;
    if (!snapshot_consume_char(sv, '{')) return false;

    snapshot_skip_ws(sv);
    if (sv->count > 0 && sv->data[0] == '}') {
        nob_sv_chop_left(sv, 1);
        goto validate;
    }

    for (;;) {
        free(key);
        key = NULL;
        if (!snapshot_parse_json_string(sv, &key)) goto defer;
        if (!snapshot_consume_char(sv, ':')) goto defer;

        if (strcmp(key, "name") == 0) {
            if (!snapshot_parse_json_string(sv, &project->name)) goto defer;
        } else if (strcmp(key, "upstream_url") == 0) {
            if (!snapshot_parse_json_string(sv, &project->upstream_url)) goto defer;
        } else if (strcmp(key, "archive_url") == 0) {
            if (!snapshot_parse_json_string(sv, &project->archive_url)) goto defer;
        } else if (strcmp(key, "pinned_ref") == 0) {
            if (!snapshot_parse_json_string(sv, &project->pinned_ref)) goto defer;
        } else if (strcmp(key, "archive_prefix") == 0) {
            if (!snapshot_parse_json_string(sv, &project->archive_prefix)) goto defer;
        } else if (strcmp(key, "retain_paths") == 0) {
            if (!snapshot_parse_string_array(sv, &project->retain_paths)) goto defer;
        } else {
            if (!snapshot_skip_json_value(sv)) goto defer;
        }

        snapshot_skip_ws(sv);
        if (sv->count > 0 && sv->data[0] == '}') {
            nob_sv_chop_left(sv, 1);
            break;
        }
        if (!snapshot_consume_char(sv, ',')) goto defer;
    }

validate:
    if (!project->name || !project->upstream_url || !project->archive_url ||
        !project->pinned_ref || !project->archive_prefix || project->retain_paths.count == 0) {
        nob_log(NOB_ERROR, "manifest project is missing required fields");
        goto defer;
    }
    ok = true;

defer:
    free(key);
    return ok;
}

static bool snapshot_parse_projects_array(Nob_String_View *sv, Snapshot_Project_List *projects) {
    Snapshot_Project project = {0};
    if (!projects) return false;
    if (!snapshot_consume_char(sv, '[')) return false;
    snapshot_skip_ws(sv);
    if (sv->count > 0 && sv->data[0] == ']') {
        nob_sv_chop_left(sv, 1);
        return true;
    }
    for (;;) {
        memset(&project, 0, sizeof(project));
        if (!snapshot_parse_project(sv, &project)) {
            snapshot_project_free(&project);
            return false;
        }
        nob_da_append(projects, project);
        snapshot_skip_ws(sv);
        if (sv->count > 0 && sv->data[0] == ']') {
            nob_sv_chop_left(sv, 1);
            return true;
        }
        if (!snapshot_consume_char(sv, ',')) return false;
    }
}

static bool snapshot_load_manifest(Snapshot_Project_List *projects) {
    Nob_String_Builder file = {0};
    Nob_String_View sv = {0};
    char *key = NULL;
    bool ok = false;

    if (!projects) return false;
    if (!nob_read_entire_file(SNAPSHOT_MANIFEST_PATH, &file)) goto defer;
    sv = nob_sb_to_sv(file);

    if (!snapshot_consume_char(&sv, '{')) goto defer;
    snapshot_skip_ws(&sv);
    if (sv.count > 0 && sv.data[0] == '}') {
        nob_sv_chop_left(&sv, 1);
        ok = true;
        goto defer;
    }

    for (;;) {
        free(key);
        key = NULL;
        if (!snapshot_parse_json_string(&sv, &key)) goto defer;
        if (!snapshot_consume_char(&sv, ':')) goto defer;
        if (strcmp(key, "projects") == 0) {
            if (!snapshot_parse_projects_array(&sv, projects)) goto defer;
        } else {
            if (!snapshot_skip_json_value(&sv)) goto defer;
        }
        snapshot_skip_ws(&sv);
        if (sv.count > 0 && sv.data[0] == '}') {
            nob_sv_chop_left(&sv, 1);
            break;
        }
        if (!snapshot_consume_char(&sv, ',')) goto defer;
    }

    snapshot_skip_ws(&sv);
    if (sv.count != 0) {
        nob_log(NOB_ERROR, "json parse error: trailing data in manifest");
        goto defer;
    }

    ok = true;

defer:
    free(key);
    nob_da_free(file);
    if (!ok) snapshot_project_list_free(projects);
    return ok;
}

static bool snapshot_path_join2(char out[SNAPSHOT_PATH_MAX], const char *a, const char *b) {
    if (!out || !a || !b) return false;
    return snprintf(out, SNAPSHOT_PATH_MAX, "%s/%s", a, b) < SNAPSHOT_PATH_MAX;
}

static bool snapshot_ensure_dir_recursive(const char *path) {
    char buf[SNAPSHOT_PATH_MAX] = {0};
    size_t len = 0;

    if (!path || path[0] == '\0' || strcmp(path, ".") == 0) return true;
    if (snprintf(buf, sizeof(buf), "%s", path) >= (int)sizeof(buf)) return false;
    len = strlen(buf);
    if (len == 0) return false;

    for (size_t i = 1; i < len; ++i) {
        if (buf[i] == '/' || buf[i] == '\\') {
            char saved = buf[i];
            buf[i] = '\0';
            if (buf[0] != '\0' && !nob_mkdir_if_not_exists(buf)) return false;
            buf[i] = saved;
        }
    }

    return nob_mkdir_if_not_exists(buf);
}

static bool snapshot_remove_tree_walk(Nob_Walk_Entry entry) {
    return nob_delete_file(entry.path);
}

static bool snapshot_remove_tree_if_exists(const char *path) {
    Nob_File_Type type = NOB_FILE_OTHER;
    if (!path || path[0] == '\0') return false;
    if (!nob_file_exists(path)) return true;
    type = nob_get_file_type(path);
    if ((int)type < 0) return false;
    if (type == NOB_FILE_DIRECTORY) {
        return nob_walk_dir(path, snapshot_remove_tree_walk, .post_order = true);
    }
    return nob_delete_file(path);
}

static bool snapshot_copy_allowed_path(const char *tree_root,
                                       const char *dest_root,
                                       const char *relpath) {
    char src_path[SNAPSHOT_PATH_MAX] = {0};
    char dst_path[SNAPSHOT_PATH_MAX] = {0};
    char dst_parent[SNAPSHOT_PATH_MAX] = {0};
    Nob_File_Type type = NOB_FILE_OTHER;

    if (!snapshot_path_join2(src_path, tree_root, relpath) ||
        !snapshot_path_join2(dst_path, dest_root, relpath)) {
        return false;
    }
    if (!nob_file_exists(src_path)) {
        nob_log(NOB_ERROR, "retain path not found in upstream snapshot: %s", src_path);
        return false;
    }
    type = nob_get_file_type(src_path);
    if ((int)type < 0) return false;

    if (type == NOB_FILE_DIRECTORY) {
        if (!snapshot_ensure_dir_recursive(nob_temp_dir_name(dst_path))) return false;
        return nob_copy_directory_recursively(src_path, dst_path);
    }

    if (snprintf(dst_parent, sizeof(dst_parent), "%s", nob_temp_dir_name(dst_path)) >= (int)sizeof(dst_parent)) {
        return false;
    }
    if (!snapshot_ensure_dir_recursive(dst_parent)) return false;
    return nob_copy_file(src_path, dst_path);
}

static bool snapshot_run_download(const Snapshot_Project *project, const char *download_path) {
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "curl", "-L", "--fail", "-o", download_path, project->archive_url);
    return nob_cmd_run(&cmd);
}

static bool snapshot_run_extract(const char *archive_path, const char *dst_dir) {
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "tar", "-xzf", archive_path, "-C", dst_dir);
    return nob_cmd_run(&cmd);
}

static bool snapshot_run_pack(const char *staging_dir, const char *archive_path) {
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "tar", "-czf", archive_path, "-C", staging_dir, ".");
    return nob_cmd_run(&cmd);
}

static bool snapshot_format_date(char out[32]) {
    time_t now = 0;
    struct tm *tm_info = NULL;
    if (!out) return false;
    now = time(NULL);
    tm_info = localtime(&now);
    if (!tm_info) return false;
    return strftime(out, 32, "%Y-%m-%d", tm_info) > 0;
}

static bool snapshot_write_metadata(const Snapshot_Project *project, const char *metadata_path) {
    char date_buf[32] = {0};
    const char *json = NULL;
    if (!project || !metadata_path) return false;
    if (!snapshot_format_date(date_buf)) {
        nob_log(NOB_ERROR, "failed to format snapshot date");
        return false;
    }
    json = nob_temp_sprintf(
        "{\n"
        "  \"name\": \"%s\",\n"
        "  \"upstream_url\": \"%s\",\n"
        "  \"pinned_ref\": \"%s\",\n"
        "  \"snapshot_date\": \"%s\"\n"
        "}\n",
        project->name,
        project->upstream_url,
        project->pinned_ref,
        date_buf);
    return nob_write_entire_file(metadata_path, json, strlen(json));
}

static bool snapshot_refresh_project(const Snapshot_Project *project) {
    char archive_path[SNAPSHOT_PATH_MAX] = {0};
    char metadata_path[SNAPSHOT_PATH_MAX] = {0};
    char staging_dir[SNAPSHOT_PATH_MAX] = {0};
    char temp_root[SNAPSHOT_PATH_MAX] = {0};
    char download_path[SNAPSHOT_PATH_MAX] = {0};
    char tree_root[SNAPSHOT_PATH_MAX] = {0};
    bool ok = false;

    if (!project) return false;
    if (!snapshot_ensure_dir_recursive(SNAPSHOT_ARCHIVE_ROOT)) return false;
    if (!snapshot_path_join2(archive_path, SNAPSHOT_ARCHIVE_ROOT, nob_temp_sprintf("%s.tar.gz", project->name)) ||
        !snapshot_path_join2(metadata_path, SNAPSHOT_ARCHIVE_ROOT, nob_temp_sprintf("%s.json", project->name)) ||
        !snapshot_path_join2(staging_dir, SNAPSHOT_ARCHIVE_ROOT, nob_temp_sprintf(".staging_%s", project->name)) ||
        !snapshot_path_join2(temp_root,
                             SNAPSHOT_ARCHIVE_ROOT,
                             nob_temp_sprintf(".tmp_%s_%lld_%d",
                                              project->name,
                                              (long long)time(NULL),
                                              (int)SNAPSHOT_GETPID())) ||
        !snapshot_path_join2(download_path, temp_root, "snapshot.tar.gz") ||
        !snapshot_path_join2(tree_root, temp_root, project->archive_prefix)) {
        return false;
    }

    if (!snapshot_remove_tree_if_exists(staging_dir) ||
        !snapshot_remove_tree_if_exists(temp_root) ||
        !snapshot_ensure_dir_recursive(staging_dir) ||
        !snapshot_ensure_dir_recursive(temp_root)) {
        goto defer;
    }

    nob_log(NOB_INFO, "refreshing %s", project->name);

    if (!snapshot_run_download(project, download_path) ||
        !snapshot_run_extract(download_path, temp_root)) {
        goto defer;
    }

    for (size_t i = 0; i < project->retain_paths.count; ++i) {
        if (!snapshot_copy_allowed_path(tree_root, staging_dir, project->retain_paths.items[i])) {
            goto defer;
        }
    }

    if (!snapshot_run_pack(staging_dir, archive_path) ||
        !snapshot_write_metadata(project, metadata_path)) {
        goto defer;
    }

    ok = true;

defer:
    (void)snapshot_remove_tree_if_exists(staging_dir);
    (void)snapshot_remove_tree_if_exists(temp_root);
    return ok;
}

static void snapshot_print_usage(FILE *stream, const char *program) {
    fprintf(stream,
            "Usage: %s [--list] [project ...]\n"
            "\n"
            "Refreshes pinned real-project corpus archives from\n"
            "`test_v2/artifact_parity/real_projects/manifest.json`.\n"
            "\n"
            "Options:\n"
            "  --list    List known projects without downloading anything.\n"
            "  --help    Show this message.\n",
            program);
}

static bool snapshot_name_selected(const char *name, int argc, char **argv, bool *matched) {
    bool any_names = false;
    if (!name) return false;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') continue;
        any_names = true;
        if (strcmp(argv[i], name) == 0) {
            if (matched) *matched = true;
            return true;
        }
    }
    return !any_names;
}

int main(int argc, char **argv) {
    Snapshot_Project_List projects = {0};
    bool list_only = false;
    int result = 1;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            snapshot_print_usage(stdout, argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--list") == 0) {
            list_only = true;
            continue;
        }
        if (argv[i][0] == '-') {
            nob_log(NOB_ERROR, "unknown option: %s", argv[i]);
            snapshot_print_usage(stderr, argv[0]);
            return 1;
        }
    }

    if (!snapshot_load_manifest(&projects)) return 1;

    if (list_only) {
        for (size_t i = 0; i < projects.count; ++i) {
            printf("%s\n", projects.items[i].name);
        }
        result = 0;
        goto defer;
    }

    for (size_t i = 0; i < projects.count; ++i) {
        bool matched = false;
        if (!snapshot_name_selected(projects.items[i].name, argc, argv, &matched)) continue;
        if (!snapshot_refresh_project(&projects.items[i])) goto defer;
    }

    for (int i = 1; i < argc; ++i) {
        bool found = false;
        if (argv[i][0] == '-') continue;
        for (size_t j = 0; j < projects.count; ++j) {
            if (strcmp(argv[i], projects.items[j].name) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            nob_log(NOB_ERROR, "manifest does not define project `%s`", argv[i]);
            goto defer;
        }
    }

    result = 0;

defer:
    snapshot_project_list_free(&projects);
    return result;
}
