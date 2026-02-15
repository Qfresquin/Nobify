#include "diagnostics.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

static struct {
    bool strict_mode;
    size_t warning_count;
    size_t error_count;
    struct {
        char **names;
        size_t *counts;
        size_t count;
        size_t capacity;
        size_t total;
    } unsupported;
} g_diag = {0};

static char *diag_strndup_local(const char *data, size_t len) {
    char *out = (char*)malloc(len + 1);
    if (!out) return NULL;
    if (len > 0 && data) memcpy(out, data, len);
    out[len] = '\0';
    return out;
}

int diag_to_nob_level(Diag_Severity sev) {
    return sev == DIAG_SEV_ERROR ? NOB_ERROR : NOB_WARNING;
}

const char *diag_safe_str(const char *s) {
    return (s && s[0] != '\0') ? s : "-";
}

void diag_reset(void) {
    g_diag.warning_count = 0;
    g_diag.error_count = 0;
}

void diag_set_strict(bool strict_mode) {
    g_diag.strict_mode = strict_mode;
}

bool diag_is_strict(void) {
    return g_diag.strict_mode;
}

size_t diag_warning_count(void) {
    return g_diag.warning_count;
}

size_t diag_error_count(void) {
    return g_diag.error_count;
}

bool diag_has_warnings(void) {
    return g_diag.warning_count > 0;
}

bool diag_has_errors(void) {
    return g_diag.error_count > 0;
}

void diag_telemetry_reset(void) {
    for (size_t i = 0; i < g_diag.unsupported.count; i++) {
        free(g_diag.unsupported.names[i]);
    }
    free(g_diag.unsupported.names);
    free(g_diag.unsupported.counts);
    g_diag.unsupported.names = NULL;
    g_diag.unsupported.counts = NULL;
    g_diag.unsupported.count = 0;
    g_diag.unsupported.capacity = 0;
    g_diag.unsupported.total = 0;
}

void diag_telemetry_record_unsupported_sv(String_View cmd_name) {
    if (cmd_name.count == 0 || !cmd_name.data) return;
    g_diag.unsupported.total++;
    for (size_t i = 0; i < g_diag.unsupported.count; i++) {
        String_View curr = sv_from_cstr(g_diag.unsupported.names[i]);
        if (nob_sv_eq(curr, cmd_name)) {
            g_diag.unsupported.counts[i]++;
            return;
        }
    }

    if (g_diag.unsupported.count == g_diag.unsupported.capacity) {
        size_t old_cap = g_diag.unsupported.capacity;
        size_t new_cap = old_cap == 0 ? 8 : old_cap * 2;
        char **new_names = (char**)malloc(new_cap * sizeof(char*));
        size_t *new_counts = (size_t*)malloc(new_cap * sizeof(size_t));
        if (!new_names || !new_counts) {
            free(new_names);
            free(new_counts);
            return;
        }
        if (old_cap > 0) {
            memcpy(new_names, g_diag.unsupported.names, old_cap * sizeof(char*));
            memcpy(new_counts, g_diag.unsupported.counts, old_cap * sizeof(size_t));
        }
        free(g_diag.unsupported.names);
        free(g_diag.unsupported.counts);
        g_diag.unsupported.names = new_names;
        g_diag.unsupported.counts = new_counts;
        g_diag.unsupported.capacity = new_cap;
    }

    char *name_copy = diag_strndup_local(cmd_name.data, cmd_name.count);
    if (!name_copy) return;
    size_t idx = g_diag.unsupported.count++;
    g_diag.unsupported.names[idx] = name_copy;
    g_diag.unsupported.counts[idx] = 1;
}

size_t diag_telemetry_unsupported_total(void) {
    return g_diag.unsupported.total;
}

size_t diag_telemetry_unsupported_unique(void) {
    return g_diag.unsupported.count;
}

size_t diag_telemetry_unsupported_count_for(const char *cmd_name) {
    if (!cmd_name) return 0;
    String_View key = sv_from_cstr(cmd_name);
    for (size_t i = 0; i < g_diag.unsupported.count; i++) {
        if (nob_sv_eq(sv_from_cstr(g_diag.unsupported.names[i]), key)) {
            return g_diag.unsupported.counts[i];
        }
    }
    return 0;
}

void diag_telemetry_emit_summary(void) {
    if (g_diag.unsupported.total == 0) return;
    nob_log(NOB_WARNING,
            "TELEMETRY|unsupported_commands|total=%zu|unique=%zu",
            g_diag.unsupported.total, g_diag.unsupported.count);
    for (size_t i = 0; i < g_diag.unsupported.count; i++) {
        nob_log(NOB_WARNING,
                "TELEMETRY|unsupported_command|name=%s|count=%zu",
                g_diag.unsupported.names[i], g_diag.unsupported.counts[i]);
    }
}

bool diag_telemetry_write_report(const char *path, const char *source_label) {
    if (!path || path[0] == '\0') return false;
    FILE *f = fopen(path, "a");
    if (!f) return false;

    time_t now = time(NULL);
    fprintf(f, "run_ts=%lld source=%s total=%zu unique=%zu\n",
            (long long)now,
            source_label && source_label[0] ? source_label : "-",
            g_diag.unsupported.total,
            g_diag.unsupported.count);
    for (size_t i = 0; i < g_diag.unsupported.count; i++) {
        fprintf(f, "  cmd=%s count=%zu\n", g_diag.unsupported.names[i], g_diag.unsupported.counts[i]);
    }
    fclose(f);
    return true;
}

void diag_log(
    Diag_Severity sev,
    const char *component,
    const char *source,
    size_t line,
    size_t col,
    const char *command,
    const char *cause,
    const char *hint
) {
    Diag_Severity effective = sev;
    if (g_diag.strict_mode && sev == DIAG_SEV_WARNING) {
        effective = DIAG_SEV_ERROR;
    }

    if (sev == DIAG_SEV_WARNING) g_diag.warning_count++;
    if (effective == DIAG_SEV_ERROR) g_diag.error_count++;

    nob_log(
        diag_to_nob_level(effective),
        "DIAG|%s|component=%s|source=%s|line=%zu|col=%zu|command=%s|cause=%s|hint=%s",
        effective == DIAG_SEV_ERROR ? "ERROR" : "WARNING",
        diag_safe_str(component),
        diag_safe_str(source),
        line,
        col,
        diag_safe_str(command),
        diag_safe_str(cause),
        diag_safe_str(hint)
    );
}
