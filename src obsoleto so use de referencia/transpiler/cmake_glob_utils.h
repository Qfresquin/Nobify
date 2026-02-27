#ifndef CMAKE_GLOB_UTILS_H_
#define CMAKE_GLOB_UTILS_H_

#include "build_model_types.h"

bool cmk_glob_match(String_View pattern, String_View path);
bool cmk_glob_collect_recursive(Arena *arena,
                                String_View base_dir,
                                String_View rel_prefix,
                                String_View pattern,
                                bool list_directories,
                                String_View relative_base,
                                String_Builder *list,
                                bool *first);

#endif // CMAKE_GLOB_UTILS_H_
