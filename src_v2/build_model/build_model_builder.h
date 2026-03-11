#ifndef BUILD_MODEL_BUILDER_H_
#define BUILD_MODEL_BUILDER_H_

#include "build_model_types.h"

BM_Builder *bm_builder_create(Arena *arena, Diag_Sink *sink);
bool bm_builder_apply_event(BM_Builder *builder, const Event *ev);
bool bm_builder_apply_stream(BM_Builder *builder, const Event_Stream *stream);
const Build_Model_Draft *bm_builder_finalize(BM_Builder *builder);
bool bm_builder_has_fatal_error(const BM_Builder *builder);

typedef BM_Builder Build_Model_Builder;

Build_Model_Builder *builder_create(Arena *arena, void *diagnostics);
bool builder_apply_event(Build_Model_Builder *builder, const Cmake_Event *ev);
bool builder_apply_stream(Build_Model_Builder *builder, const Cmake_Event_Stream *stream);
const Build_Model_Draft *builder_finalize(Build_Model_Builder *builder);
bool builder_has_fatal_error(const Build_Model_Builder *builder);

#endif
