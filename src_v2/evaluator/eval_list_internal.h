#ifndef EVAL_LIST_INTERNAL_H_
#define EVAL_LIST_INTERNAL_H_

#include "evaluator_internal.h"

#include <pcre2posix.h>

typedef enum {
    LIST_SORT_COMPARE_STRING = 0,
    LIST_SORT_COMPARE_FILE_BASENAME,
    LIST_SORT_COMPARE_NATURAL,
} List_Sort_Compare;

typedef enum {
    LIST_SORT_CASE_SENSITIVE = 0,
    LIST_SORT_CASE_INSENSITIVE,
} List_Sort_Case;

typedef enum {
    LIST_SORT_ORDER_ASCENDING = 0,
    LIST_SORT_ORDER_DESCENDING,
} List_Sort_Order;

typedef struct {
    List_Sort_Compare compare;
    List_Sort_Case case_mode;
    List_Sort_Order order;
} List_Sort_Options;

size_t list_count_items(String_View list_sv);
bool list_item_in_set(String_View item, String_View *set, size_t set_count);
bool list_split_semicolon_preserve_empty(Evaluator_Context *ctx, String_View input, SV_List *out);
bool list_sv_parse_i64(String_View sv, long long *out);
String_View list_join_items_temp(Evaluator_Context *ctx, String_View *items, size_t count);
bool list_set_var_from_items(Evaluator_Context *ctx, String_View var, String_View *items, size_t count);
bool list_load_var_items(Evaluator_Context *ctx, String_View var, SV_List *out);
bool list_normalize_index(size_t item_count, long long raw_index, bool allow_end, size_t *out_index);
String_View list_concat_temp(Evaluator_Context *ctx, String_View a, String_View b);
String_View list_to_case_temp(Evaluator_Context *ctx, String_View in, bool upper);
String_View list_strip_ws_view(String_View in);
String_View list_genex_strip_temp(Evaluator_Context *ctx, String_View in);
bool list_compile_regex(Evaluator_Context *ctx,
                        const Node *node,
                        Cmake_Event_Origin o,
                        String_View pattern,
                        regex_t *out_re);
bool list_regex_replace_one_temp(Evaluator_Context *ctx,
                                 regex_t *re,
                                 String_View replacement,
                                 String_View input,
                                 String_View *out);
int list_sort_item_cmp(String_View a, String_View b, const List_Sort_Options *opt);

#endif // EVAL_LIST_INTERNAL_H_
