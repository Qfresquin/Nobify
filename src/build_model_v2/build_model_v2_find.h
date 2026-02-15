#ifndef BUILD_MODEL_V2_FIND_H_
#define BUILD_MODEL_V2_FIND_H_

#include "build_model_v2_types.h"

typedef struct {
    String_View package_name;
    bool required;
    bool found;
} Build_Model_v2_Find_Package_Result;

void build_model_v2_register_find_package_result(Build_Model_v2 *model,
                                                 Arena *arena,
                                                 Build_Model_v2_Find_Package_Result result);

#endif // BUILD_MODEL_V2_FIND_H_

