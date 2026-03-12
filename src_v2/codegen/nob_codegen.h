#ifndef NOB_CODEGEN_H_
#define NOB_CODEGEN_H_

#include "build_model_query.h"

typedef struct Nob_Codegen_Options {
    String_View input_path;
    String_View output_path;
} Nob_Codegen_Options;

bool nob_codegen_render(const Build_Model *model,
                        Arena *scratch,
                        const Nob_Codegen_Options *opts,
                        Nob_String_Builder *out);
bool nob_codegen_write_file(const Build_Model *model,
                            Arena *scratch,
                            const Nob_Codegen_Options *opts);

#endif
