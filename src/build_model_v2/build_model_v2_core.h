#ifndef BUILD_MODEL_V2_CORE_H_
#define BUILD_MODEL_V2_CORE_H_

#include "build_model_v2_types.h"

Build_Model_v2 *build_model_v2_create(Arena *arena);
void build_model_v2_set_project(Build_Model_v2 *model, String_View name, String_View version);

#endif // BUILD_MODEL_V2_CORE_H_

