#ifndef TEST_EVALUATOR_V2_SNAPSHOT_H_
#define TEST_EVALUATOR_V2_SNAPSHOT_H_

#include "arena.h"
#include "event_ir.h"
#include "nob.h"

#include <stdbool.h>

bool evaluator_load_text_file_to_arena(Arena *arena, const char *path, String_View *out);
String_View evaluator_normalize_newlines_to_arena(Arena *arena, String_View in);
bool evaluator_render_snapshot_to_arena(Arena *arena,
                                        const Cmake_Event_Stream *stream,
                                        const char *workspace_root,
                                        String_View *out);

#endif // TEST_EVALUATOR_V2_SNAPSHOT_H_
