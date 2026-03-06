#ifndef EVAL_CMAKE_PATH_INTERNAL_H_
#define EVAL_CMAKE_PATH_INTERNAL_H_

#include "evaluator_internal.h"

#include <stddef.h>

size_t path_last_separator_index(String_View path);
ptrdiff_t cmk_path_dot_index(String_View name, bool last_only);
String_View cmk_path_root_path_temp(Evaluator_Context *ctx, String_View path);
String_View cmk_path_filename_sv(String_View path);
String_View cmk_path_extension_from_name_sv(String_View name, bool last_only);
String_View cmk_path_stem_from_name_sv(String_View name, bool last_only);
String_View cmk_path_relative_part_temp(Evaluator_Context *ctx, String_View path);
String_View cmk_path_parent_part_sv(String_View path);
String_View cmk_path_normalize_temp(Evaluator_Context *ctx, String_View input);
String_View cmk_path_current_source_dir(Evaluator_Context *ctx);
String_View cmk_path_make_absolute_temp(Evaluator_Context *ctx, String_View value, String_View base_dir);
String_View cmk_path_relativize_temp(Evaluator_Context *ctx, String_View path, String_View base_dir);
String_View cmk_path_to_cmake_seps_temp(Evaluator_Context *ctx, String_View in);
String_View cmk_path_to_native_seps_temp(Evaluator_Context *ctx, String_View in);
bool cmk_path_split_char_list_temp(Evaluator_Context *ctx, String_View in, char sep, SV_List *out);
String_View cmk_path_join_char_list_temp(Evaluator_Context *ctx, SV_List list, char sep);
String_View cmk_path_component_get_temp(Evaluator_Context *ctx,
                                        String_View input,
                                        String_View component,
                                        bool last_only,
                                        bool *out_ok);
bool cmk_path_is_component_supports_last_only(String_View component);
String_View cmk_path_compare_canonical_temp(Evaluator_Context *ctx, String_View in);

#endif // EVAL_CMAKE_PATH_INTERNAL_H_
