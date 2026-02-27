#ifndef CMAKE_PATH_UTILS_H_
#define CMAKE_PATH_UTILS_H_

#include "build_model_types.h"

bool cmk_path_is_absolute(String_View path);
String_View cmk_path_make_absolute(Arena *arena, String_View path);
String_View cmk_path_join(Arena *arena, String_View base, String_View rel);
String_View cmk_path_parent(Arena *arena, String_View full_path);
String_View cmk_path_basename(String_View path);
String_View cmk_path_extension(String_View name);
String_View cmk_path_stem(String_View name);
String_View cmk_path_normalize(Arena *arena, String_View input);
String_View cmk_path_relativize(Arena *arena, String_View path, String_View base_dir);
String_View cmk_path_get_component(Arena *arena, String_View input, String_View component, bool *supported);

#endif // CMAKE_PATH_UTILS_H_
