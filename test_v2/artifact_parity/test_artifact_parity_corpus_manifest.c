#include "test_artifact_parity_corpus_manifest.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static char *artifact_parity_corpus_strdup(const char *text) {
    size_t size = 0;
    char *copy = NULL;

    if (!text) return NULL;
    size = strlen(text) + 1;
    copy = (char*)malloc(size);
    if (!copy) {
        nob_log(NOB_ERROR, "artifact parity corpus manifest: out of memory");
        return NULL;
    }
    memcpy(copy, text, size);
    return copy;
}

static void artifact_parity_corpus_string_list_free(Artifact_Parity_Corpus_String_List *list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; ++i) {
        free(list->items[i]);
    }
    nob_da_free(*list);
    list->count = 0;
    list->capacity = 0;
}

static void artifact_parity_corpus_project_free(Artifact_Parity_Corpus_Project *project) {
    if (!project) return;
    free(project->name);
    free(project->upstream_url);
    free(project->archive_url);
    free(project->pinned_ref);
    free(project->archive_prefix);
    free(project->support_tier);
    artifact_parity_corpus_string_list_free(&project->retain_paths);
    artifact_parity_corpus_string_list_free(&project->supported_phases);
    artifact_parity_corpus_string_list_free(&project->expected_imported_targets);
    memset(project, 0, sizeof(*project));
}

void artifact_parity_corpus_manifest_free(Artifact_Parity_Corpus_Project_List *projects) {
    if (!projects) return;
    for (size_t i = 0; i < projects->count; ++i) {
        artifact_parity_corpus_project_free(&projects->items[i]);
    }
    nob_da_free(*projects);
    projects->count = 0;
    projects->capacity = 0;
}

static void artifact_parity_corpus_skip_ws(Nob_String_View *sv) {
    while (sv && sv->count > 0 && isspace((unsigned char)sv->data[0])) {
        nob_sv_chop_left(sv, 1);
    }
}

static bool artifact_parity_corpus_consume_char(Nob_String_View *sv, char expected) {
    artifact_parity_corpus_skip_ws(sv);
    if (!sv || sv->count == 0 || sv->data[0] != expected) {
        nob_log(NOB_ERROR, "artifact parity corpus manifest: expected `%c`", expected);
        return false;
    }
    nob_sv_chop_left(sv, 1);
    return true;
}

static bool artifact_parity_corpus_parse_json_string(Nob_String_View *sv, char **out) {
    Nob_String_Builder sb = {0};
    bool ok = false;

    if (out) *out = NULL;
    artifact_parity_corpus_skip_ws(sv);
    if (!sv || !out || sv->count == 0 || sv->data[0] != '"') {
        nob_log(NOB_ERROR, "artifact parity corpus manifest: expected string");
        return false;
    }

    nob_sv_chop_left(sv, 1);
    while (sv->count > 0) {
        char ch = sv->data[0];
        nob_sv_chop_left(sv, 1);
        if (ch == '"') {
            nob_da_append(&sb, '\0');
            *out = artifact_parity_corpus_strdup(sb.items ? sb.items : "");
            ok = *out != NULL;
            goto defer;
        }
        if (ch == '\\') {
            char esc = 0;
            if (sv->count == 0) {
                nob_log(NOB_ERROR, "artifact parity corpus manifest: truncated escape sequence");
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
                    nob_log(NOB_ERROR,
                            "artifact parity corpus manifest: unsupported escape `\\%c`",
                            esc);
                    goto defer;
            }
        }
        nob_da_append(&sb, ch);
    }

    nob_log(NOB_ERROR, "artifact parity corpus manifest: unterminated string");

defer:
    nob_da_free(sb);
    return ok;
}

static bool artifact_parity_corpus_skip_json_value(Nob_String_View *sv);

static bool artifact_parity_corpus_skip_json_array(Nob_String_View *sv) {
    if (!artifact_parity_corpus_consume_char(sv, '[')) return false;
    artifact_parity_corpus_skip_ws(sv);
    if (sv->count > 0 && sv->data[0] == ']') {
        nob_sv_chop_left(sv, 1);
        return true;
    }
    for (;;) {
        if (!artifact_parity_corpus_skip_json_value(sv)) return false;
        artifact_parity_corpus_skip_ws(sv);
        if (sv->count > 0 && sv->data[0] == ']') {
            nob_sv_chop_left(sv, 1);
            return true;
        }
        if (!artifact_parity_corpus_consume_char(sv, ',')) return false;
    }
}

static bool artifact_parity_corpus_skip_json_object(Nob_String_View *sv) {
    char *key = NULL;

    if (!artifact_parity_corpus_consume_char(sv, '{')) return false;
    artifact_parity_corpus_skip_ws(sv);
    if (sv->count > 0 && sv->data[0] == '}') {
        nob_sv_chop_left(sv, 1);
        return true;
    }

    for (;;) {
        free(key);
        key = NULL;
        if (!artifact_parity_corpus_parse_json_string(sv, &key)) goto fail;
        if (!artifact_parity_corpus_consume_char(sv, ':')) goto fail;
        if (!artifact_parity_corpus_skip_json_value(sv)) goto fail;
        artifact_parity_corpus_skip_ws(sv);
        if (sv->count > 0 && sv->data[0] == '}') {
            nob_sv_chop_left(sv, 1);
            free(key);
            return true;
        }
        if (!artifact_parity_corpus_consume_char(sv, ',')) goto fail;
    }

fail:
    free(key);
    return false;
}

static bool artifact_parity_corpus_skip_json_atom(Nob_String_View *sv) {
    size_t count = 0;

    artifact_parity_corpus_skip_ws(sv);
    while (count < sv->count) {
        char ch = sv->data[count];
        if (isspace((unsigned char)ch) || ch == ',' || ch == ']' || ch == '}') break;
        count += 1;
    }
    if (count == 0) {
        nob_log(NOB_ERROR, "artifact parity corpus manifest: expected atom");
        return false;
    }
    nob_sv_chop_left(sv, count);
    return true;
}

static bool artifact_parity_corpus_skip_json_value(Nob_String_View *sv) {
    artifact_parity_corpus_skip_ws(sv);
    if (!sv || sv->count == 0) {
        nob_log(NOB_ERROR, "artifact parity corpus manifest: unexpected end of input");
        return false;
    }
    switch (sv->data[0]) {
        case '"': {
            char *tmp = NULL;
            bool ok = artifact_parity_corpus_parse_json_string(sv, &tmp);
            free(tmp);
            return ok;
        }
        case '{': return artifact_parity_corpus_skip_json_object(sv);
        case '[': return artifact_parity_corpus_skip_json_array(sv);
        default: return artifact_parity_corpus_skip_json_atom(sv);
    }
}

static bool artifact_parity_corpus_parse_string_array(Nob_String_View *sv,
                                                      Artifact_Parity_Corpus_String_List *out) {
    char *value = NULL;

    if (!out) return false;
    if (!artifact_parity_corpus_consume_char(sv, '[')) return false;
    artifact_parity_corpus_skip_ws(sv);
    if (sv->count > 0 && sv->data[0] == ']') {
        nob_sv_chop_left(sv, 1);
        return true;
    }

    for (;;) {
        value = NULL;
        if (!artifact_parity_corpus_parse_json_string(sv, &value)) return false;
        nob_da_append(out, value);
        artifact_parity_corpus_skip_ws(sv);
        if (sv->count > 0 && sv->data[0] == ']') {
            nob_sv_chop_left(sv, 1);
            return true;
        }
        if (!artifact_parity_corpus_consume_char(sv, ',')) return false;
    }
}

bool artifact_parity_corpus_string_list_contains(const Artifact_Parity_Corpus_String_List *list,
                                                 const char *value) {
    if (!list || !value) return false;
    for (size_t i = 0; i < list->count; ++i) {
        if (list->items[i] && strcmp(list->items[i], value) == 0) return true;
    }
    return false;
}

static bool artifact_parity_corpus_phase_name_is_supported(const char *name) {
    static const char *const allowed[] = {
        "configure",
        "build",
        "test",
        "install",
        "export",
        "package",
        "consumer",
    };

    if (!name || name[0] == '\0') return false;
    for (size_t i = 0; i < NOB_ARRAY_LEN(allowed); ++i) {
        if (strcmp(name, allowed[i]) == 0) return true;
    }
    return false;
}

bool artifact_parity_corpus_project_has_support_tier(
    const Artifact_Parity_Corpus_Project *project,
    const char *support_tier) {
    if (!project || !support_tier || !project->support_tier) return false;
    return strcmp(project->support_tier, support_tier) == 0;
}

static bool artifact_parity_corpus_support_tier_is_valid(const char *support_tier) {
    return support_tier &&
           (strcmp(support_tier, "supported") == 0 ||
            strcmp(support_tier, "known-boundary") == 0 ||
            strcmp(support_tier, "backend-gap") == 0);
}

static bool artifact_parity_corpus_project_validate(
    const Artifact_Parity_Corpus_Project *project) {
    if (!project ||
        !project->name || project->name[0] == '\0' ||
        !project->upstream_url || project->upstream_url[0] == '\0' ||
        !project->archive_url || project->archive_url[0] == '\0' ||
        !project->pinned_ref || project->pinned_ref[0] == '\0' ||
        !project->archive_prefix || project->archive_prefix[0] == '\0' ||
        !project->support_tier || project->support_tier[0] == '\0' ||
        project->retain_paths.count == 0 ||
        project->supported_phases.count == 0 ||
        project->expected_imported_targets.count == 0) {
        nob_log(NOB_ERROR, "artifact parity corpus manifest: project is missing required fields");
        return false;
    }

    if (!artifact_parity_corpus_support_tier_is_valid(project->support_tier)) {
        nob_log(NOB_ERROR,
                "artifact parity corpus manifest: invalid support tier `%s` for project `%s`",
                project->support_tier,
                project->name);
        return false;
    }

    for (size_t i = 0; i < project->supported_phases.count; ++i) {
        if (!artifact_parity_corpus_phase_name_is_supported(project->supported_phases.items[i])) {
            nob_log(NOB_ERROR,
                    "artifact parity corpus manifest: invalid phase `%s` for project `%s`",
                    project->supported_phases.items[i],
                    project->name);
            return false;
        }
    }

    return true;
}

static bool artifact_parity_corpus_parse_project(Nob_String_View *sv,
                                                 Artifact_Parity_Corpus_Project *project) {
    char *key = NULL;
    bool ok = false;

    if (!project) return false;
    if (!artifact_parity_corpus_consume_char(sv, '{')) return false;
    artifact_parity_corpus_skip_ws(sv);
    if (sv->count > 0 && sv->data[0] == '}') {
        nob_sv_chop_left(sv, 1);
        return artifact_parity_corpus_project_validate(project);
    }

    for (;;) {
        free(key);
        key = NULL;
        if (!artifact_parity_corpus_parse_json_string(sv, &key)) goto defer;
        if (!artifact_parity_corpus_consume_char(sv, ':')) goto defer;

        if (strcmp(key, "name") == 0) {
            if (!artifact_parity_corpus_parse_json_string(sv, &project->name)) goto defer;
        } else if (strcmp(key, "upstream_url") == 0) {
            if (!artifact_parity_corpus_parse_json_string(sv, &project->upstream_url)) goto defer;
        } else if (strcmp(key, "archive_url") == 0) {
            if (!artifact_parity_corpus_parse_json_string(sv, &project->archive_url)) goto defer;
        } else if (strcmp(key, "pinned_ref") == 0) {
            if (!artifact_parity_corpus_parse_json_string(sv, &project->pinned_ref)) goto defer;
        } else if (strcmp(key, "archive_prefix") == 0) {
            if (!artifact_parity_corpus_parse_json_string(sv, &project->archive_prefix)) goto defer;
        } else if (strcmp(key, "retain_paths") == 0) {
            if (!artifact_parity_corpus_parse_string_array(sv, &project->retain_paths)) goto defer;
        } else if (strcmp(key, "supported_phases") == 0) {
            if (!artifact_parity_corpus_parse_string_array(sv, &project->supported_phases)) goto defer;
        } else if (strcmp(key, "expected_imported_targets") == 0) {
            if (!artifact_parity_corpus_parse_string_array(sv, &project->expected_imported_targets)) goto defer;
        } else if (strcmp(key, "support_tier") == 0) {
            if (!artifact_parity_corpus_parse_json_string(sv, &project->support_tier)) goto defer;
        } else {
            if (!artifact_parity_corpus_skip_json_value(sv)) goto defer;
        }

        artifact_parity_corpus_skip_ws(sv);
        if (sv->count > 0 && sv->data[0] == '}') {
            nob_sv_chop_left(sv, 1);
            break;
        }
        if (!artifact_parity_corpus_consume_char(sv, ',')) goto defer;
    }

    ok = artifact_parity_corpus_project_validate(project);

defer:
    free(key);
    return ok;
}

static bool artifact_parity_corpus_parse_projects_array(
    Nob_String_View *sv,
    Artifact_Parity_Corpus_Project_List *projects) {
    Artifact_Parity_Corpus_Project project = {0};

    if (!projects) return false;
    if (!artifact_parity_corpus_consume_char(sv, '[')) return false;
    artifact_parity_corpus_skip_ws(sv);
    if (sv->count > 0 && sv->data[0] == ']') {
        nob_sv_chop_left(sv, 1);
        return true;
    }

    for (;;) {
        memset(&project, 0, sizeof(project));
        if (!artifact_parity_corpus_parse_project(sv, &project)) {
            artifact_parity_corpus_project_free(&project);
            return false;
        }
        nob_da_append(projects, project);
        artifact_parity_corpus_skip_ws(sv);
        if (sv->count > 0 && sv->data[0] == ']') {
            nob_sv_chop_left(sv, 1);
            return true;
        }
        if (!artifact_parity_corpus_consume_char(sv, ',')) return false;
    }
}

bool artifact_parity_corpus_manifest_load_path(const char *manifest_path,
                                               Artifact_Parity_Corpus_Project_List *out_projects) {
    Nob_String_Builder file = {0};
    Nob_String_View sv = {0};
    char *key = NULL;
    bool ok = false;

    if (out_projects) *out_projects = (Artifact_Parity_Corpus_Project_List){0};
    if (!manifest_path || !out_projects) return false;
    if (!nob_read_entire_file(manifest_path, &file)) return false;
    sv = nob_sb_to_sv(file);

    if (!artifact_parity_corpus_consume_char(&sv, '{')) goto defer;
    artifact_parity_corpus_skip_ws(&sv);
    if (sv.count > 0 && sv.data[0] == '}') {
        nob_sv_chop_left(&sv, 1);
        ok = true;
        goto defer;
    }

    for (;;) {
        free(key);
        key = NULL;
        if (!artifact_parity_corpus_parse_json_string(&sv, &key)) goto defer;
        if (!artifact_parity_corpus_consume_char(&sv, ':')) goto defer;
        if (strcmp(key, "projects") == 0) {
            if (!artifact_parity_corpus_parse_projects_array(&sv, out_projects)) goto defer;
        } else {
            if (!artifact_parity_corpus_skip_json_value(&sv)) goto defer;
        }
        artifact_parity_corpus_skip_ws(&sv);
        if (sv.count > 0 && sv.data[0] == '}') {
            nob_sv_chop_left(&sv, 1);
            break;
        }
        if (!artifact_parity_corpus_consume_char(&sv, ',')) goto defer;
    }

    artifact_parity_corpus_skip_ws(&sv);
    if (sv.count != 0) {
        nob_log(NOB_ERROR, "artifact parity corpus manifest: trailing data");
        goto defer;
    }

    ok = true;

defer:
    free(key);
    nob_da_free(file);
    if (!ok) artifact_parity_corpus_manifest_free(out_projects);
    return ok;
}

bool artifact_parity_corpus_manifest_load(Artifact_Parity_Corpus_Project_List *out_projects) {
    return artifact_parity_corpus_manifest_load_path(ARTIFACT_PARITY_CORPUS_MANIFEST_PATH,
                                                     out_projects);
}

const Artifact_Parity_Corpus_Project *artifact_parity_corpus_manifest_find(
    const Artifact_Parity_Corpus_Project_List *projects,
    const char *name) {
    if (!projects || !name) return NULL;
    for (size_t i = 0; i < projects->count; ++i) {
        if (projects->items[i].name && strcmp(projects->items[i].name, name) == 0) {
            return &projects->items[i];
        }
    }
    return NULL;
}

bool artifact_parity_corpus_project_archive_relpath(
    const Artifact_Parity_Corpus_Project *project,
    char *out_relpath,
    size_t out_relpath_cap) {
    if (!project || !project->name || !out_relpath) return false;
    return snprintf(out_relpath,
                    out_relpath_cap,
                    "%s/%s.tar.gz",
                    ARTIFACT_PARITY_CORPUS_ARCHIVE_ROOT,
                    project->name) < (int)out_relpath_cap;
}

bool artifact_parity_corpus_project_consumer_relpath(
    const Artifact_Parity_Corpus_Project *project,
    char *out_relpath,
    size_t out_relpath_cap) {
    if (!project || !project->name || !out_relpath) return false;
    return snprintf(out_relpath,
                    out_relpath_cap,
                    "%s/%s",
                    ARTIFACT_PARITY_CORPUS_CONSUMER_ROOT,
                    project->name) < (int)out_relpath_cap;
}

bool artifact_parity_corpus_format_string_list(
    const Artifact_Parity_Corpus_String_List *list,
    char *buffer,
    size_t buffer_size) {
    size_t used = 0;

    if (!buffer || buffer_size == 0) return false;
    buffer[0] = '\0';
    if (!list || list->count == 0) return true;

    for (size_t i = 0; i < list->count; ++i) {
        const char *item = list->items[i] ? list->items[i] : "";
        int n = snprintf(buffer + used,
                         used < buffer_size ? buffer_size - used : 0,
                         "%s%s",
                         i == 0 ? "" : ",",
                         item);
        if (n < 0 || used + (size_t)n >= buffer_size) return false;
        used += (size_t)n;
    }

    return true;
}
