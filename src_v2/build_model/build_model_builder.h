#ifndef BUILD_MODEL_BUILDER_V2_H_
#define BUILD_MODEL_BUILDER_V2_H_

#include "build_model.h"
#include "../transpiler/event_ir.h"

typedef struct Build_Model_Builder Build_Model_Builder;

// `diagnostics` is reserved for a future diagnostic sink adapter.
Build_Model_Builder *builder_create(Arena *arena, void *diagnostics);
bool builder_apply_event(Build_Model_Builder *builder, const Cmake_Event *ev);
bool builder_apply_stream(Build_Model_Builder *builder, const Cmake_Event_Stream *stream);
// Returns NULL after any fatal builder error. Partial model state must be treated as invalid.
Build_Model *builder_finish(Build_Model_Builder *builder);
bool builder_has_fatal_error(const Build_Model_Builder *builder);

#endif // BUILD_MODEL_BUILDER_V2_H_
