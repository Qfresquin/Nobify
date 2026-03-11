#ifndef BUILD_MODEL_VALIDATE_H_
#define BUILD_MODEL_VALIDATE_H_

#include "build_model_types.h"

bool bm_validate_draft(const Build_Model_Draft *draft, Arena *scratch, Diag_Sink *sink);

#endif
