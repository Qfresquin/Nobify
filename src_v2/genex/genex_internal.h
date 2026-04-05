#ifndef GENEX_INTERNAL_H_
#define GENEX_INTERNAL_H_

#include "genex.h"

#define GX_DEFAULT_MAX_CALLBACK_VALUE_LEN (1024 * 1024)

typedef struct {
    String_View target_name;
    String_View property_name;
} Genex_Target_Property_Stack_Entry;

typedef struct {
    Genex_Target_Property_Stack_Entry entries[128];
    size_t count;
} Genex_Target_Property_Stack;

typedef struct {
    String_View *items;
    size_t count;
} Gx_Sv_List;

String_View gx_copy_to_arena(Arena *arena, String_View sv);
String_View gx_copy_cstr_to_arena(Arena *arena, const char *s);
String_View gx_trim(String_View sv);
bool gx_sv_eq_ci(String_View a, String_View b);
bool gx_sv_ends_with_ci(String_View sv, String_View suffix);
bool gx_is_escaped(String_View input, size_t idx);
bool gx_is_genex_open_at(String_View input, size_t i);
bool gx_is_unescaped_char_at(String_View input, size_t i, char ch);
bool gx_sv_contains_genex_unescaped(String_View input);
bool gx_cmake_string_is_false(String_View value);
bool gx_list_matches_value_ci(String_View entry_list, String_View needle);
String_View gx_path_dirname(String_View path);
String_View gx_path_basename(String_View path);
Gx_Sv_List gx_split_top_level_alloc(const Genex_Context *ctx, String_View input, char delimiter);
bool gx_find_top_level_colon(String_View body, size_t *out_colon);
bool gx_find_matching_genex_end(String_View input, size_t start_dollar, size_t *out_end);
bool gx_target_name_eq(const Genex_Context *ctx, String_View a, String_View b);
bool gx_tp_stack_contains(const Genex_Context *ctx,
                          const Genex_Target_Property_Stack *stack,
                          String_View target_name,
                          String_View property_name);
bool gx_tp_stack_push(Genex_Target_Property_Stack *stack,
                      String_View target_name,
                      String_View property_name);
void gx_tp_stack_pop(Genex_Target_Property_Stack *stack);

Genex_Result gx_result(Genex_Status status, String_View value, String_View diag_message);
size_t gx_max_callback_value_len(const Genex_Context *ctx);
Genex_Result gx_validate_callback_value(const Genex_Context *ctx,
                                        String_View raw_value,
                                        String_View raw_expr,
                                        const char *which_callback);
Genex_Result gx_eval_root(const Genex_Context *ctx, String_View input);

#endif // GENEX_INTERNAL_H_
