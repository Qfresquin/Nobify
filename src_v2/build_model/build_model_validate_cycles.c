#include "build_model_internal.h"

static bool bm_cycle_visit(const Build_Model_Draft *draft,
                           BM_Target_Id id,
                           Arena *scratch,
                           uint8_t *colors,
                           Diag_Sink *sink,
                           bool *had_error) {
    const BM_Target_Record *target = &draft->targets[id];
    colors[id] = 1;

    for (size_t dep = 0; dep < arena_arr_len(target->explicit_dependency_names); ++dep) {
        BM_Target_Id dep_id = bm_draft_find_target_id(draft, target->explicit_dependency_names[dep]);
        if (dep_id == BM_TARGET_ID_INVALID) continue;

        if (colors[dep_id] == 1) {
            *had_error = true;
            bm_diag_error(sink,
                          target->provenance,
                          "build_model_validate",
                          "cycles",
                          "explicit target dependency cycle detected",
                          "remove or break the dependency cycle between targets");
            continue;
        }

        if (colors[dep_id] == 0 && !bm_cycle_visit(draft, dep_id, scratch, colors, sink, had_error)) {
            return false;
        }
    }

    colors[id] = 2;
    (void)scratch;
    return true;
}

bool bm_validate_explicit_cycles(const Build_Model_Draft *draft,
                                 Arena *scratch,
                                 Diag_Sink *sink,
                                 bool *had_error) {
    size_t target_count = draft ? arena_arr_len(draft->targets) : 0;
    uint8_t *colors = NULL;

    if (!draft || !scratch || !had_error) return false;
    if (target_count == 0) return true;

    colors = arena_alloc_array_zero(scratch, uint8_t, target_count);
    if (!colors) return false;

    for (size_t i = 0; i < target_count; ++i) {
        if (colors[i] != 0) continue;
        if (!bm_cycle_visit(draft, (BM_Target_Id)i, scratch, colors, sink, had_error)) return false;
    }
    return true;
}
