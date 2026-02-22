#ifndef BUILD_MODEL_QUERY_V2_H_
#define BUILD_MODEL_QUERY_V2_H_

#include "build_model.h"

// --- Core Lookup ---
const Build_Target *bm_get_target(const Build_Model *m, String_View name);
const Build_Target *bm_query_target_by_name(const Build_Model *model, String_View name);

// --- Target Properties ---
String_View bm_target_name(const Build_Target *t);
Target_Type bm_target_type(const Build_Target *t);
Target_Type bm_query_target_type(const Build_Target *target);

// Flat array accessors.
size_t bm_target_source_count(const Build_Target *t);
String_View bm_target_source_at(const Build_Target *t, size_t index);
size_t bm_target_include_count(const Build_Target *t);
String_View bm_target_include_at(const Build_Target *t, size_t index);

void bm_query_target_sources(const Build_Target *target, const String_View **out_items, size_t *out_count);
void bm_query_target_includes(const Build_Target *target, const String_View **out_items, size_t *out_count);
void bm_query_target_deps(const Build_Target *target, const String_View **out_items, size_t *out_count);
void bm_query_target_link_libraries(const Build_Target *target, const String_View **out_items, size_t *out_count);
bool bm_query_target_effective_link_libraries(const Build_Target *target,
                                              Arena *scratch_arena,
                                              const Logic_Eval_Context *logic_ctx,
                                              const String_View **out_items,
                                              size_t *out_count);

// --- Project Metadata ---
String_View bm_query_project_name(const Build_Model *model);
String_View bm_query_project_version(const Build_Model *model);

// --- Global Config ---
bool bm_is_windows(const Build_Model *m);
bool bm_is_linux(const Build_Model *m);
bool bm_is_unix(const Build_Model *m);
bool bm_is_apple(const Build_Model *m);

// --- Graph Helpers ---
// Returned buffer is arena-owned (scratch_arena). Caller does not free.
String_View *bm_query_transitive_libs(const Build_Model *model,
                                      const Build_Target *target,
                                      Arena *scratch_arena,
                                      size_t *out_count);

#endif // BUILD_MODEL_QUERY_V2_H_
