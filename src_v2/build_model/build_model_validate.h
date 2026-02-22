#ifndef BUILD_MODEL_VALIDATE_V2_H_
#define BUILD_MODEL_VALIDATE_V2_H_

#include "build_model.h"

bool build_model_validate(const Build_Model *model, void *diagnostics);
bool build_model_check_cycles(const Build_Model *model, void *diagnostics);
bool build_model_check_cycles_ex(const Build_Model *model, Arena *scratch, void *diagnostics);

#endif // BUILD_MODEL_VALIDATE_V2_H_
