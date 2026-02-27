#ifndef FIND_SEARCH_UTILS_H_
#define FIND_SEARCH_UTILS_H_

#include "build_model_types.h"

void find_search_split_env_path(Arena *arena, String_View value, String_List *out_dirs);
void find_search_collect_program_name_variants(Arena *arena, String_View name, String_List *out_names);
void find_search_collect_library_name_variants(Arena *arena, String_View name, String_List *out_names);
bool find_search_candidates(Arena *arena,
                            const String_List *dirs,
                            const String_List *suffixes,
                            const String_List *names,
                            String_View *out_path);
bool find_search_path_candidates(Arena *arena,
                                 const String_List *dirs,
                                 const String_List *suffixes,
                                 const String_List *names,
                                 String_View *out_dir);

#endif // FIND_SEARCH_UTILS_H_
