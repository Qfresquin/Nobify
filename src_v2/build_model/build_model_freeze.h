#ifndef BUILD_MODEL_FREEZE_H_
#define BUILD_MODEL_FREEZE_H_

#include "build_model_types.h"

const Build_Model *bm_freeze_draft(const Build_Model_Draft *draft,
                                   Arena *out_arena,
                                   Diag_Sink *sink);

#endif
