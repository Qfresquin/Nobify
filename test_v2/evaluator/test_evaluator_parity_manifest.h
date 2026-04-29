#ifndef TEST_EVALUATOR_PARITY_MANIFEST_H_
#define TEST_EVALUATOR_PARITY_MANIFEST_H_

#include <stddef.h>

typedef enum {
    EPM_KIND_NATIVE = 0,
    EPM_KIND_STRUCTURAL,
} Evaluator_Parity_Manifest_Kind;

typedef enum {
    EPM_AUDIT_FULL = 0,
    EPM_AUDIT_PARTIAL,
    EPM_AUDIT_MISSING,
} Evaluator_Parity_Audit_Status;

typedef enum {
    EPM_EVIDENCE_POSITIVE_DIFF = 0,
    EPM_EVIDENCE_HOST_OR_SPECIAL_DIFF,
    EPM_EVIDENCE_INTERNAL_ONLY,
    EPM_EVIDENCE_KNOWN_DIVERGENCE,
    EPM_EVIDENCE_NONE,
} Evaluator_Parity_Evidence_Level;

typedef struct {
    const char *name;
    Evaluator_Parity_Manifest_Kind kind;
    const char *registry_tag;
    Evaluator_Parity_Audit_Status audit_status;
    const char *diff_owner;
    const char *evidence_pack;
    Evaluator_Parity_Evidence_Level evidence_level;
    const char *known_divergence_key;
} Evaluator_Parity_Manifest_Row;

static const Evaluator_Parity_Manifest_Row s_evaluator_parity_manifest[] = {
#include "test_evaluator_parity_manifest.inc"
};

#define EVALUATOR_PARITY_MANIFEST_EXPECTED_FULL 76u
#define EVALUATOR_PARITY_MANIFEST_EXPECTED_PARTIAL 60u
#define EVALUATOR_PARITY_MANIFEST_EXPECTED_NATIVE_DIVERGENCES 60u

static const size_t s_evaluator_parity_manifest_count =
    sizeof(s_evaluator_parity_manifest) / sizeof(s_evaluator_parity_manifest[0]);

#endif // TEST_EVALUATOR_PARITY_MANIFEST_H_
