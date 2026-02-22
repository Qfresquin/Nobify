#ifndef BUILD_MODEL_VALIDATE_V2_H_
#define BUILD_MODEL_VALIDATE_V2_H_



// `diagnostics` is reserved for a future diagnostic sink adapter.
bool build_model_validate(const Build_Model *model, void *diagnostics);
bool build_model_check_cycles(const Build_Model *model, void *diagnostics);

#endif // BUILD_MODEL_VALIDATE_V2_H_
