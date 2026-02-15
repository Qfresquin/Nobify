#ifndef BUILD_MODEL_V2_TYPES_H_
#define BUILD_MODEL_V2_TYPES_H_

#include "build_model_types.h"

typedef struct {
    String_View name;
    String_View version;
} Build_Model_v2_Project;

typedef struct {
    Build_Model_v2_Project project;
    Build_Target **targets;
    size_t target_count;
    size_t target_capacity;
} Build_Model_v2;

#endif // BUILD_MODEL_V2_TYPES_H_

