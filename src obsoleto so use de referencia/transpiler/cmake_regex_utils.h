#ifndef CMAKE_REGEX_UTILS_H_
#define CMAKE_REGEX_UTILS_H_

#include "build_model_types.h"

bool cmk_regex_match_first(Arena *arena, String_View pattern, String_View input, String_View *out_match);
bool cmk_regex_match_groups(Arena *arena, String_View pattern, String_View input, String_List *out_groups);
String_View cmk_regex_replace_backrefs(Arena *arena, String_View pattern, String_View input, String_View replacement);

#endif // CMAKE_REGEX_UTILS_H_
