#ifndef EVAL_CMAKE_PATH_INTERNAL_H_
#define EVAL_CMAKE_PATH_INTERNAL_H_

#include "evaluator_internal.h"

String_View cmk_path_root_path_temp(EvalExecContext *ctx, String_View path);
String_View cmk_path_filename_sv(String_View path);
String_View cmk_path_extension_from_name_sv(String_View name, bool last_only);
String_View cmk_path_stem_from_name_sv(String_View name, bool last_only);
String_View cmk_path_relative_part_temp(EvalExecContext *ctx, String_View path);
String_View cmk_path_parent_part_sv(String_View path);
String_View cmk_path_normalize_temp(EvalExecContext *ctx, String_View input);
String_View cmk_path_set_temp(EvalExecContext *ctx, String_View input, bool normalize);
String_View cmk_path_append_temp(EvalExecContext *ctx, String_View path, String_View input);
String_View cmk_path_current_source_dir(EvalExecContext *ctx);
String_View cmk_path_make_absolute_temp(EvalExecContext *ctx, String_View value, String_View base_dir);
String_View cmk_path_relativize_temp(EvalExecContext *ctx, String_View path, String_View base_dir);
String_View cmk_path_to_cmake_seps_temp(EvalExecContext *ctx, String_View in);
String_View cmk_path_to_native_seps_temp(EvalExecContext *ctx, String_View in);
bool cmk_path_split_char_list_temp(EvalExecContext *ctx, String_View in, char sep, SV_List *out);
String_View cmk_path_join_char_list_temp(EvalExecContext *ctx, SV_List list, char sep);
String_View cmk_path_component_get_temp(EvalExecContext *ctx,
                                        String_View input,
                                        String_View component,
                                        bool last_only,
                                        bool *out_ok);
String_View cmk_path_remove_filename_temp(EvalExecContext *ctx, String_View value);
String_View cmk_path_replace_filename_temp(EvalExecContext *ctx, String_View value, String_View input);
String_View cmk_path_transform_extension_temp(EvalExecContext *ctx,
                                              String_View value,
                                              bool last_only,
                                              bool replace_mode,
                                              String_View replacement);
bool cmk_path_is_component_supports_last_only(String_View component);
bool cmk_path_is_absolute_sv(String_View path);
bool cmk_path_is_prefix_temp(EvalExecContext *ctx, String_View prefix, String_View input, bool normalize);
String_View cmk_path_compare_canonical_temp(EvalExecContext *ctx, String_View in);

#endif // EVAL_CMAKE_PATH_INTERNAL_H_
