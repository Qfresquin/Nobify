#ifndef DIAGNOSTICS_H_
#define DIAGNOSTICS_H_

#include "nob.h"

typedef enum {
    DIAG_SEV_WARNING,
    DIAG_SEV_ERROR
} Diag_Severity;

int diag_to_nob_level(Diag_Severity sev);
const char *diag_safe_str(const char *s);

void diag_reset(void);
void diag_set_strict(bool strict_mode);
bool diag_is_strict(void);
size_t diag_warning_count(void);
size_t diag_error_count(void);
bool diag_has_warnings(void);
bool diag_has_errors(void);

void diag_telemetry_reset(void);
void diag_telemetry_record_unsupported_sv(String_View cmd_name);
size_t diag_telemetry_unsupported_total(void);
size_t diag_telemetry_unsupported_unique(void);
size_t diag_telemetry_unsupported_count_for(const char *cmd_name);
void diag_telemetry_emit_summary(void);
bool diag_telemetry_write_report(const char *path, const char *source_label);

void diag_log(
    Diag_Severity sev,
    const char *component,
    const char *source,
    size_t line,
    size_t col,
    const char *command,
    const char *cause,
    const char *hint
);

#endif // DIAGNOSTICS_H_
