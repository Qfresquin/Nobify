#ifndef BUILD_MODEL_V2_TARGET_H_
#define BUILD_MODEL_V2_TARGET_H_

#include "build_model_v2_types.h"

Build_Target *build_model_v2_add_target(Build_Model_v2 *model,
                                        Arena *arena,
                                        String_View name,
                                        Target_Type type);

#endif // BUILD_MODEL_V2_TARGET_H_

