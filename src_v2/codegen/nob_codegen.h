#ifndef NOB_CODEGEN_H_
#define NOB_CODEGEN_H_

#include "build_model_query.h"

typedef struct Nob_Codegen_Options {
    String_View input_path;
    String_View output_path;
    String_View source_root;
    String_View binary_root;
    String_View embedded_cmake_bin;
    String_View embedded_cpack_bin;
} Nob_Codegen_Options;

bool nob_codegen_render(const Build_Model *model,
                        Arena *scratch,
                        const Nob_Codegen_Options *opts,
                        Nob_String_Builder *out);
bool nob_codegen_write_file(const Build_Model *model,
                            Arena *scratch,
                            const Nob_Codegen_Options *opts);

#endif
