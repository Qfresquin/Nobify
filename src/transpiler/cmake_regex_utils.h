#ifndef CMAKE_REGEX_UTILS_H_
#define CMAKE_REGEX_UTILS_H_

#include "build_model.h"

String_View cmk_regex_replace_backrefs(Arena *arena, String_View pattern, String_View input, String_View replacement);

#endif // CMAKE_REGEX_UTILS_H_
