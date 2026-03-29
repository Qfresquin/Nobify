#include "eval_string_internal.h"

#include <stdio.h>

typedef enum {
    STRING_JSON_NULL = 0,
    STRING_JSON_BOOL,
    STRING_JSON_NUMBER,
    STRING_JSON_STRING,
    STRING_JSON_ARRAY,
    STRING_JSON_OBJECT,
} String_Json_Type;

static void string_json_skip_ws(String_View json, size_t *io) {
    if (!io) return;
    while (*io < json.count && isspace((unsigned char)json.data[*io])) (*io)++;
}

static bool string_json_parse_string_token(String_View json,
                                           size_t *io,
                                           size_t *out_inner_start,
                                           size_t *out_inner_end) {
    if (!io || *io >= json.count || json.data[*io] != '"') return false;
    size_t i = *io + 1;
    size_t inner_start = i;
    while (i < json.count) {
        char c = json.data[i];
        if (c == '"') {
            if (out_inner_start) *out_inner_start = inner_start;
            if (out_inner_end) *out_inner_end = i;
            *io = i + 1;
            return true;
        }
        if (c == '\\') {
            i++;
            if (i >= json.count) return false;
            char e = json.data[i];
            if (e == 'u') {
                if (i + 4 >= json.count) return false;
                for (size_t k = 1; k <= 4; k++) {
                    if (eval_string_hex_nibble(json.data[i + k]) < 0) return false;
                }
                i += 5;
                continue;
            }
            if (!(e == '"' || e == '\\' || e == '/' || e == 'b' ||
                  e == 'f' || e == 'n' || e == 'r' || e == 't')) return false;
        }
        i++;
    }
    return false;
}

static bool string_json_decode_string_temp(EvalExecContext *ctx,
                                           String_View raw,
                                           String_View *out) {
    if (!ctx || !out) return false;
    *out = nob_sv_from_cstr("");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), raw.count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, false);

    size_t w = 0;
    for (size_t i = 0; i < raw.count; i++) {
        char c = raw.data[i];
        if (c != '\\') {
            buf[w++] = c;
            continue;
        }
        if (i + 1 >= raw.count) return false;
        char e = raw.data[++i];
        switch (e) {
            case '"': buf[w++] = '"'; break;
            case '\\': buf[w++] = '\\'; break;
            case '/': buf[w++] = '/'; break;
            case 'b': buf[w++] = '\b'; break;
            case 'f': buf[w++] = '\f'; break;
            case 'n': buf[w++] = '\n'; break;
            case 'r': buf[w++] = '\r'; break;
            case 't': buf[w++] = '\t'; break;
            case 'u': {
                if (i + 4 >= raw.count) return false;
                int h0 = eval_string_hex_nibble(raw.data[i + 1]);
                int h1 = eval_string_hex_nibble(raw.data[i + 2]);
                int h2 = eval_string_hex_nibble(raw.data[i + 3]);
                int h3 = eval_string_hex_nibble(raw.data[i + 4]);
                if (h0 < 0 || h1 < 0 || h2 < 0 || h3 < 0) return false;
                unsigned int cp = (unsigned int)((h0 << 12) | (h1 << 8) | (h2 << 4) | h3);
                buf[w++] = (cp <= 0x7F) ? (char)cp : '?';
                i += 4;
                break;
            }
            default: return false;
        }
    }

    buf[w] = '\0';
    *out = nob_sv_from_parts(buf, w);
    return true;
}

static bool string_json_parse_number_token(String_View json, size_t *io) {
    if (!io || *io >= json.count) return false;
    size_t i = *io;
    if (json.data[i] == '-') i++;
    if (i >= json.count) return false;

    if (json.data[i] == '0') {
        i++;
    } else if (json.data[i] >= '1' && json.data[i] <= '9') {
        i++;
        while (i < json.count && isdigit((unsigned char)json.data[i])) i++;
    } else {
        return false;
    }

    if (i < json.count && json.data[i] == '.') {
        i++;
        if (i >= json.count || !isdigit((unsigned char)json.data[i])) return false;
        while (i < json.count && isdigit((unsigned char)json.data[i])) i++;
    }

    if (i < json.count && (json.data[i] == 'e' || json.data[i] == 'E')) {
        i++;
        if (i < json.count && (json.data[i] == '+' || json.data[i] == '-')) i++;
        if (i >= json.count || !isdigit((unsigned char)json.data[i])) return false;
        while (i < json.count && isdigit((unsigned char)json.data[i])) i++;
    }

    *io = i;
    return true;
}

static bool string_json_match_lit(String_View json, size_t *io, const char *lit) {
    if (!io || !lit) return false;
    size_t n = strlen(lit);
    if (*io + n > json.count) return false;
    if (memcmp(json.data + *io, lit, n) != 0) return false;
    *io += n;
    return true;
}

static String_View string_json_type_name(String_Json_Type t) {
    switch (t) {
        case STRING_JSON_NULL: return nob_sv_from_cstr("NULL");
        case STRING_JSON_BOOL: return nob_sv_from_cstr("BOOLEAN");
        case STRING_JSON_NUMBER: return nob_sv_from_cstr("NUMBER");
        case STRING_JSON_STRING: return nob_sv_from_cstr("STRING");
        case STRING_JSON_ARRAY: return nob_sv_from_cstr("ARRAY");
        case STRING_JSON_OBJECT: return nob_sv_from_cstr("OBJECT");
        default: return nob_sv_from_cstr("");
    }
}

typedef struct String_Json_Value String_Json_Value;
typedef struct {
    String_View key;
    String_Json_Value *value;
} String_Json_Object_Entry;

struct String_Json_Value {
    String_Json_Type type;
    bool bool_value;
    String_View scalar;
    String_Json_Value **array_items;
    size_t array_count;
    size_t array_capacity;
    String_Json_Object_Entry *object_items;
    size_t object_count;
    size_t object_capacity;
};

typedef struct {
    String_View message;
    size_t path_prefix_count;
} String_Json_Error;

static String_View string_sb_to_temp_sv(EvalExecContext *ctx, Nob_String_Builder *sb) {
    if (!ctx || !sb) return nob_sv_from_cstr("");
    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), sb->count + 1);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));
    if (sb->count > 0) memcpy(buf, sb->items, sb->count);
    buf[sb->count] = '\0';
    return nob_sv_from_parts(buf, sb->count);
}

static String_View string_json_notfound_temp(EvalExecContext *ctx, String_View *path, size_t path_count) {
    if (!ctx || path_count == 0) return nob_sv_from_cstr("NOTFOUND");
    Nob_String_Builder sb = {0};
    for (size_t i = 0; i < path_count; i++) {
        if (i > 0) nob_sb_append(&sb, '-');
        nob_sb_append_buf(&sb, path[i].data, path[i].count);
    }
    nob_sb_append_cstr(&sb, "-NOTFOUND");
    String_View out = string_sb_to_temp_sv(ctx, &sb);
    nob_sb_free(sb);
    return out;
}

static String_View string_json_message_with_token_temp(EvalExecContext *ctx, const char *prefix, String_View token) {
    if (!ctx) return nob_sv_from_cstr("");
    Nob_String_Builder sb = {0};
    if (prefix) nob_sb_append_cstr(&sb, prefix);
    if (token.count > 0) {
        if (sb.count > 0) nob_sb_append_cstr(&sb, ": ");
        nob_sb_append_buf(&sb, token.data, token.count);
    }
    String_View out = string_sb_to_temp_sv(ctx, &sb);
    nob_sb_free(sb);
    return out;
}

static bool string_json_emit_or_store_error(EvalExecContext *ctx,
                                            const Node *node,
                                            Cmake_Event_Origin o,
                                            bool has_error_var,
                                            String_View error_var,
                                            String_View out_var,
                                            String_View message,
                                            String_View *path,
                                            size_t path_prefix_count) {
    if (!ctx || !node) { if (eval_should_stop(ctx)) return false; return true; }
    if (has_error_var) {
        (void)eval_var_set_current(ctx, out_var, string_json_notfound_temp(ctx, path, path_prefix_count));
        (void)eval_var_set_current(ctx, error_var, message);
        if (eval_should_stop(ctx)) return false;
        return true;
    }
    EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_INVALID_VALUE, "string", message, nob_sv_from_cstr(""));
    if (eval_should_stop(ctx)) return false;
    return true;
}

static String_Json_Value *string_jsonv_new(EvalExecContext *ctx, String_Json_Type type) {
    if (!ctx) return NULL;
    String_Json_Value *v = (String_Json_Value*)arena_alloc(eval_temp_arena(ctx), sizeof(*v));
    EVAL_OOM_RETURN_IF_NULL(ctx, v, NULL);
    memset(v, 0, sizeof(*v));
    v->type = type;
    return v;
}

static bool string_jsonv_array_push(EvalExecContext *ctx, String_Json_Value *arr, String_Json_Value *item) {
    if (!ctx || !arr || arr->type != STRING_JSON_ARRAY || !item) return false;
    if (!EVAL_ARR_PUSH(ctx, eval_temp_arena(ctx), arr->array_items, item)) return false;
    arr->array_count = arena_arr_len(arr->array_items);
    arr->array_capacity = arena_arr_cap(arr->array_items);
    return true;
}

static bool string_jsonv_object_push(EvalExecContext *ctx, String_Json_Value *obj, String_View key, String_Json_Value *item) {
    if (!ctx || !obj || obj->type != STRING_JSON_OBJECT || !item) return false;
    if (!EVAL_ARR_PUSH(ctx, eval_temp_arena(ctx), obj->object_items, ((String_Json_Object_Entry){ .key = key, .value = item }))) {
        return false;
    }
    obj->object_count = arena_arr_len(obj->object_items);
    obj->object_capacity = arena_arr_cap(obj->object_items);
    return true;
}

static bool string_jsonv_parse_value_temp(EvalExecContext *ctx,
                                          String_View json,
                                          size_t *io,
                                          String_Json_Value **out,
                                          String_View *err_msg);

static bool string_jsonv_parse_object_temp(EvalExecContext *ctx,
                                           String_View json,
                                           size_t *io,
                                           String_Json_Value **out,
                                           String_View *err_msg) {
    if (!ctx || !io || !out || !err_msg) return false;
    if (*io >= json.count || json.data[*io] != '{') return false;
    (*io)++;
    String_Json_Value *obj = string_jsonv_new(ctx, STRING_JSON_OBJECT);
    if (!obj) return false;

    string_json_skip_ws(json, io);
    if (*io < json.count && json.data[*io] == '}') {
        (*io)++;
        *out = obj;
        return true;
    }

    for (;;) {
        size_t k0 = 0, k1 = 0;
        if (!string_json_parse_string_token(json, io, &k0, &k1)) {
            *err_msg = nob_sv_from_cstr("string(JSON) failed to parse object key");
            return false;
        }
        String_View key_raw = nob_sv_from_parts(json.data + k0, k1 - k0);
        String_View key_dec = nob_sv_from_cstr("");
        if (!string_json_decode_string_temp(ctx, key_raw, &key_dec)) {
            *err_msg = nob_sv_from_cstr("string(JSON) failed to decode object key");
            return false;
        }
        string_json_skip_ws(json, io);
        if (*io >= json.count || json.data[*io] != ':') {
            *err_msg = nob_sv_from_cstr("string(JSON) expected ':' after object key");
            return false;
        }
        (*io)++;
        string_json_skip_ws(json, io);

        String_Json_Value *child = NULL;
        if (!string_jsonv_parse_value_temp(ctx, json, io, &child, err_msg)) return false;
        if (!string_jsonv_object_push(ctx, obj, key_dec, child)) return false;

        string_json_skip_ws(json, io);
        if (*io >= json.count) {
            *err_msg = nob_sv_from_cstr("string(JSON) object is not terminated");
            return false;
        }
        if (json.data[*io] == ',') {
            (*io)++;
            string_json_skip_ws(json, io);
            continue;
        }
        if (json.data[*io] == '}') {
            (*io)++;
            *out = obj;
            return true;
        }
        *err_msg = nob_sv_from_cstr("string(JSON) expected ',' or '}' in object");
        return false;
    }
}

static bool string_jsonv_parse_array_temp(EvalExecContext *ctx,
                                          String_View json,
                                          size_t *io,
                                          String_Json_Value **out,
                                          String_View *err_msg) {
    if (!ctx || !io || !out || !err_msg) return false;
    if (*io >= json.count || json.data[*io] != '[') return false;
    (*io)++;
    String_Json_Value *arr = string_jsonv_new(ctx, STRING_JSON_ARRAY);
    if (!arr) return false;

    string_json_skip_ws(json, io);
    if (*io < json.count && json.data[*io] == ']') {
        (*io)++;
        *out = arr;
        return true;
    }

    for (;;) {
        String_Json_Value *child = NULL;
        if (!string_jsonv_parse_value_temp(ctx, json, io, &child, err_msg)) return false;
        if (!string_jsonv_array_push(ctx, arr, child)) return false;

        string_json_skip_ws(json, io);
        if (*io >= json.count) {
            *err_msg = nob_sv_from_cstr("string(JSON) array is not terminated");
            return false;
        }
        if (json.data[*io] == ',') {
            (*io)++;
            string_json_skip_ws(json, io);
            continue;
        }
        if (json.data[*io] == ']') {
            (*io)++;
            *out = arr;
            return true;
        }
        *err_msg = nob_sv_from_cstr("string(JSON) expected ',' or ']' in array");
        return false;
    }
}

static bool string_jsonv_parse_value_temp(EvalExecContext *ctx,
                                          String_View json,
                                          size_t *io,
                                          String_Json_Value **out,
                                          String_View *err_msg) {
    if (!ctx || !io || !out || !err_msg) return false;
    *out = NULL;
    string_json_skip_ws(json, io);
    if (*io >= json.count) {
        *err_msg = nob_sv_from_cstr("string(JSON) unexpected end of input");
        return false;
    }

    char c = json.data[*io];
    if (c == '{') return string_jsonv_parse_object_temp(ctx, json, io, out, err_msg);
    if (c == '[') return string_jsonv_parse_array_temp(ctx, json, io, out, err_msg);
    if (c == '"') {
        size_t s0 = 0, s1 = 0;
        if (!string_json_parse_string_token(json, io, &s0, &s1)) {
            *err_msg = nob_sv_from_cstr("string(JSON) invalid string value");
            return false;
        }
        String_View raw = nob_sv_from_parts(json.data + s0, s1 - s0);
        String_View dec = nob_sv_from_cstr("");
        if (!string_json_decode_string_temp(ctx, raw, &dec)) {
            *err_msg = nob_sv_from_cstr("string(JSON) failed to decode string value");
            return false;
        }
        String_Json_Value *v = string_jsonv_new(ctx, STRING_JSON_STRING);
        if (!v) return false;
        v->scalar = dec;
        *out = v;
        return true;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        size_t s = *io;
        if (!string_json_parse_number_token(json, io)) {
            *err_msg = nob_sv_from_cstr("string(JSON) invalid number");
            return false;
        }
        String_Json_Value *v = string_jsonv_new(ctx, STRING_JSON_NUMBER);
        if (!v) return false;
        v->scalar = nob_sv_from_parts(json.data + s, *io - s);
        *out = v;
        return true;
    }
    if (c == 't') {
        if (!string_json_match_lit(json, io, "true")) {
            *err_msg = nob_sv_from_cstr("string(JSON) invalid literal");
            return false;
        }
        String_Json_Value *v = string_jsonv_new(ctx, STRING_JSON_BOOL);
        if (!v) return false;
        v->bool_value = true;
        *out = v;
        return true;
    }
    if (c == 'f') {
        if (!string_json_match_lit(json, io, "false")) {
            *err_msg = nob_sv_from_cstr("string(JSON) invalid literal");
            return false;
        }
        String_Json_Value *v = string_jsonv_new(ctx, STRING_JSON_BOOL);
        if (!v) return false;
        v->bool_value = false;
        *out = v;
        return true;
    }
    if (c == 'n') {
        if (!string_json_match_lit(json, io, "null")) {
            *err_msg = nob_sv_from_cstr("string(JSON) invalid literal");
            return false;
        }
        String_Json_Value *v = string_jsonv_new(ctx, STRING_JSON_NULL);
        if (!v) return false;
        *out = v;
        return true;
    }

    *err_msg = nob_sv_from_cstr("string(JSON) invalid token");
    return false;
}

static bool string_jsonv_parse_root_temp(EvalExecContext *ctx,
                                         String_View json,
                                         String_Json_Value **out,
                                         String_View *err_msg) {
    if (!ctx || !out || !err_msg) return false;
    size_t i = 0;
    if (!string_jsonv_parse_value_temp(ctx, json, &i, out, err_msg)) return false;
    string_json_skip_ws(json, &i);
    if (i != json.count) {
        *err_msg = nob_sv_from_cstr("string(JSON) extra trailing input");
        return false;
    }
    return true;
}

static bool string_jsonv_parse_index_sv(String_View token, bool bound_check, size_t bound, size_t *out) {
    if (!out) return false;
    long long idx = -1;
    if (!eval_string_parse_i64(token, &idx) || idx < 0) return false;
    if (bound_check && (size_t)idx >= bound) return false;
    *out = (size_t)idx;
    return true;
}

static bool string_jsonv_object_find(String_Json_Value *obj, String_View key, size_t *out_index) {
    if (!obj || obj->type != STRING_JSON_OBJECT || !out_index) return false;
    for (size_t i = 0; i < obj->object_count; i++) {
        if (nob_sv_eq(obj->object_items[i].key, key)) {
            *out_index = i;
            return true;
        }
    }
    return false;
}

static int string_json_sv_cmp(String_View lhs, String_View rhs) {
    size_t n = lhs.count < rhs.count ? lhs.count : rhs.count;
    if (n > 0) {
        int cmp = memcmp(lhs.data, rhs.data, n);
        if (cmp != 0) return cmp;
    }
    if (lhs.count < rhs.count) return -1;
    if (lhs.count > rhs.count) return 1;
    return 0;
}

static bool string_jsonv_object_member_key_at_temp(EvalExecContext *ctx,
                                                   String_Json_Value *obj,
                                                   size_t member_index,
                                                   String_View *out_key) {
    if (!ctx || !obj || obj->type != STRING_JSON_OBJECT || !out_key) return false;
    if (member_index >= obj->object_count) return false;

    size_t *sorted = (size_t*)arena_alloc(eval_temp_arena(ctx), sizeof(size_t) * obj->object_count);
    EVAL_OOM_RETURN_IF_NULL(ctx, sorted, false);
    for (size_t i = 0; i < obj->object_count; i++) sorted[i] = i;

    for (size_t i = 1; i < obj->object_count; i++) {
        size_t cur = sorted[i];
        size_t j = i;
        while (j > 0 &&
               string_json_sv_cmp(obj->object_items[cur].key,
                                  obj->object_items[sorted[j - 1]].key) < 0) {
            sorted[j] = sorted[j - 1];
            j--;
        }
        sorted[j] = cur;
    }

    *out_key = obj->object_items[sorted[member_index]].key;
    return true;
}

static bool string_jsonv_resolve_path(String_Json_Value *root,
                                      String_View *path,
                                      size_t path_count,
                                      String_Json_Value **out,
                                      String_Json_Error *err,
                                      EvalExecContext *ctx) {
    if (!root || !out || !err || !ctx) return false;
    String_Json_Value *current = root;
    for (size_t i = 0; i < path_count; i++) {
        String_View tok = path[i];
        if (current->type == STRING_JSON_OBJECT) {
            size_t idx = 0;
            if (!string_jsonv_object_find(current, tok, &idx)) {
                err->message = string_json_message_with_token_temp(ctx, "string(JSON) object member not found", tok);
                err->path_prefix_count = i + 1;
                return false;
            }
            current = current->object_items[idx].value;
            continue;
        }
        if (current->type == STRING_JSON_ARRAY) {
            size_t idx = 0;
            if (!string_jsonv_parse_index_sv(tok, true, current->array_count, &idx)) {
                err->message = string_json_message_with_token_temp(ctx, "string(JSON) array index out of range", tok);
                err->path_prefix_count = i + 1;
                return false;
            }
            current = current->array_items[idx];
            continue;
        }
        err->message = string_json_message_with_token_temp(ctx, "string(JSON) path traverses non-container value", tok);
        err->path_prefix_count = i + 1;
        return false;
    }
    *out = current;
    return true;
}

static bool string_jsonv_equal(const String_Json_Value *a, const String_Json_Value *b) {
    if (!a || !b || a->type != b->type) return false;
    switch (a->type) {
        case STRING_JSON_NULL: return true;
        case STRING_JSON_BOOL: return a->bool_value == b->bool_value;
        case STRING_JSON_NUMBER:
        case STRING_JSON_STRING: return nob_sv_eq(a->scalar, b->scalar);
        case STRING_JSON_ARRAY:
            if (a->array_count != b->array_count) return false;
            for (size_t i = 0; i < a->array_count; i++) {
                if (!string_jsonv_equal(a->array_items[i], b->array_items[i])) return false;
            }
            return true;
        case STRING_JSON_OBJECT:
            if (a->object_count != b->object_count) return false;
            for (size_t i = 0; i < a->object_count; i++) {
                size_t j = 0;
                if (!string_jsonv_object_find((String_Json_Value*)b, a->object_items[i].key, &j)) return false;
                if (!string_jsonv_equal(a->object_items[i].value, b->object_items[j].value)) return false;
            }
            return true;
        default:
            return false;
    }
}

static void string_jsonv_append_indent(Nob_String_Builder *sb, size_t level) {
    for (size_t i = 0; i < level; i++) {
        nob_sb_append(sb, ' ');
        nob_sb_append(sb, ' ');
    }
}

static void string_jsonv_append_escaped_string(Nob_String_Builder *sb, String_View s) {
    static const char hex[] = "0123456789abcdef";
    nob_sb_append(sb, '"');
    for (size_t i = 0; i < s.count; i++) {
        unsigned char c = (unsigned char)s.data[i];
        switch (c) {
            case '"': nob_sb_append_cstr(sb, "\\\""); break;
            case '\\': nob_sb_append_cstr(sb, "\\\\"); break;
            case '\b': nob_sb_append_cstr(sb, "\\b"); break;
            case '\f': nob_sb_append_cstr(sb, "\\f"); break;
            case '\n': nob_sb_append_cstr(sb, "\\n"); break;
            case '\r': nob_sb_append_cstr(sb, "\\r"); break;
            case '\t': nob_sb_append_cstr(sb, "\\t"); break;
            default:
                if (c < 0x20) {
                    char u[6] = {'\\', 'u', '0', '0', hex[(c >> 4) & 0x0F], hex[c & 0x0F]};
                    nob_sb_append_buf(sb, u, sizeof(u));
                } else {
                    nob_sb_append(sb, (char)c);
                }
                break;
        }
    }
    nob_sb_append(sb, '"');
}

static void string_jsonv_serialize_append(Nob_String_Builder *sb, const String_Json_Value *v, size_t indent) {
    if (!sb || !v) return;
    switch (v->type) {
        case STRING_JSON_NULL:
            nob_sb_append_cstr(sb, "null");
            return;
        case STRING_JSON_BOOL:
            nob_sb_append_cstr(sb, v->bool_value ? "true" : "false");
            return;
        case STRING_JSON_NUMBER:
            nob_sb_append_buf(sb, v->scalar.data, v->scalar.count);
            return;
        case STRING_JSON_STRING:
            string_jsonv_append_escaped_string(sb, v->scalar);
            return;
        case STRING_JSON_ARRAY:
            if (v->array_count == 0) {
                nob_sb_append_cstr(sb, "[]");
                return;
            }
            nob_sb_append_cstr(sb, "[\n");
            for (size_t i = 0; i < v->array_count; i++) {
                string_jsonv_append_indent(sb, indent + 1);
                string_jsonv_serialize_append(sb, v->array_items[i], indent + 1);
                if (i + 1 < v->array_count) nob_sb_append_cstr(sb, ",");
                nob_sb_append_cstr(sb, "\n");
            }
            string_jsonv_append_indent(sb, indent);
            nob_sb_append_cstr(sb, "]");
            return;
        case STRING_JSON_OBJECT:
            if (v->object_count == 0) {
                nob_sb_append_cstr(sb, "{}");
                return;
            }
            nob_sb_append_cstr(sb, "{\n");
            for (size_t i = 0; i < v->object_count; i++) {
                string_jsonv_append_indent(sb, indent + 1);
                string_jsonv_append_escaped_string(sb, v->object_items[i].key);
                nob_sb_append_cstr(sb, " : ");
                string_jsonv_serialize_append(sb, v->object_items[i].value, indent + 1);
                if (i + 1 < v->object_count) nob_sb_append_cstr(sb, ",");
                nob_sb_append_cstr(sb, "\n");
            }
            string_jsonv_append_indent(sb, indent);
            nob_sb_append_cstr(sb, "}");
            return;
        default:
            return;
    }
}

static String_View string_jsonv_serialize_temp(EvalExecContext *ctx, const String_Json_Value *v) {
    if (!ctx || !v) return nob_sv_from_cstr("");
    Nob_String_Builder sb = {0};
    string_jsonv_serialize_append(&sb, v, 0);
    String_View out = string_sb_to_temp_sv(ctx, &sb);
    nob_sb_free(sb);
    return out;
}

static bool string_handle_json_command(EvalExecContext *ctx,
                                       const Node *node,
                                       Cmake_Event_Origin o,
                                       SV_List a) {
    if (!ctx || !node) { if (eval_should_stop(ctx)) return false; return true; }
    if (arena_arr_len(a) < 4) {
        EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(JSON) requires out-var, mode and JSON string"), nob_sv_from_cstr("Usage: string(JSON <out-var> [ERROR_VARIABLE <err-var>] <mode> <json> ...)"));
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    size_t pos = 1;
    String_View out_var = a[pos++];
    bool has_error_var = false;
    String_View error_var = nob_sv_from_cstr("");
    if (pos < arena_arr_len(a) && eval_sv_eq_ci_lit(a[pos], "ERROR_VARIABLE")) {
        if (pos + 1 >= arena_arr_len(a)) {
            EVAL_NODE_ORIGIN_DIAG_EMIT_SEV(ctx, node, o, EV_DIAG_ERROR, EVAL_DIAG_MISSING_REQUIRED, "string", nob_sv_from_cstr("string(JSON ERROR_VARIABLE) requires output variable"), nob_sv_from_cstr("Usage: string(JSON <out-var> ERROR_VARIABLE <err-var> <mode> <json> ...)"));
            if (eval_should_stop(ctx)) return false;
            return true;
        }
        has_error_var = true;
        error_var = a[pos + 1];
        pos += 2;
        (void)eval_var_set_current(ctx, error_var, nob_sv_from_cstr("NOTFOUND"));
    }
    if (pos >= arena_arr_len(a)) {
        return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                               nob_sv_from_cstr("string(JSON) missing mode argument"),
                                               NULL, 0);
    }

    String_View mode = a[pos++];
    if (!(eval_sv_eq_ci_lit(mode, "GET") ||
          eval_sv_eq_ci_lit(mode, "TYPE") ||
          eval_sv_eq_ci_lit(mode, "MEMBER") ||
          eval_sv_eq_ci_lit(mode, "LENGTH") ||
          eval_sv_eq_ci_lit(mode, "REMOVE") ||
          eval_sv_eq_ci_lit(mode, "SET") ||
          eval_sv_eq_ci_lit(mode, "EQUAL"))) {
        return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                               string_json_message_with_token_temp(ctx, "string(JSON) received unsupported mode", mode),
                                               NULL, 0);
    }

    if (eval_sv_eq_ci_lit(mode, "EQUAL")) {
        if (pos + 1 >= arena_arr_len(a) || pos + 2 != arena_arr_len(a)) {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                   nob_sv_from_cstr("string(JSON EQUAL) expects exactly two JSON strings"),
                                                   NULL, 0);
        }
        String_View err = nob_sv_from_cstr("");
        String_Json_Value *lhs = NULL;
        if (!string_jsonv_parse_root_temp(ctx, a[pos], &lhs, &err)) {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var, err, NULL, 0);
        }
        String_Json_Value *rhs = NULL;
        if (!string_jsonv_parse_root_temp(ctx, a[pos + 1], &rhs, &err)) {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var, err, NULL, 0);
        }
        (void)eval_var_set_current(ctx, out_var, string_jsonv_equal(lhs, rhs) ? nob_sv_from_cstr("ON") : nob_sv_from_cstr("OFF"));
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    if (pos >= arena_arr_len(a)) {
        return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                               nob_sv_from_cstr("string(JSON) missing JSON string argument"),
                                               NULL, 0);
    }

    String_View err = nob_sv_from_cstr("");
    String_Json_Value *root = NULL;
    if (!string_jsonv_parse_root_temp(ctx, a[pos++], &root, &err)) {
        return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var, err, NULL, 0);
    }

    String_View *path = (pos < arena_arr_len(a)) ? &a[pos] : NULL;
    size_t path_count = (pos < arena_arr_len(a)) ? (arena_arr_len(a) - pos) : 0;

    if (eval_sv_eq_ci_lit(mode, "GET") || eval_sv_eq_ci_lit(mode, "TYPE") || eval_sv_eq_ci_lit(mode, "LENGTH")) {
        String_Json_Error jerr = {0};
        String_Json_Value *target = NULL;
        if (!string_jsonv_resolve_path(root, path, path_count, &target, &jerr, ctx)) {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                   jerr.message, path, jerr.path_prefix_count);
        }
        if (eval_sv_eq_ci_lit(mode, "GET")) {
            if (target->type == STRING_JSON_STRING || target->type == STRING_JSON_NUMBER) {
                (void)eval_var_set_current(ctx, out_var, target->scalar);
            } else if (target->type == STRING_JSON_BOOL) {
                (void)eval_var_set_current(ctx, out_var, target->bool_value ? nob_sv_from_cstr("ON") : nob_sv_from_cstr("OFF"));
            } else if (target->type == STRING_JSON_NULL) {
                (void)eval_var_set_current(ctx, out_var, nob_sv_from_cstr(""));
            } else {
                (void)eval_var_set_current(ctx, out_var, string_jsonv_serialize_temp(ctx, target));
            }
            if (eval_should_stop(ctx)) return false;
            return true;
        }
        if (eval_sv_eq_ci_lit(mode, "TYPE")) {
            (void)eval_var_set_current(ctx, out_var, string_json_type_name(target->type));
            if (eval_should_stop(ctx)) return false;
            return true;
        }
        if (target->type != STRING_JSON_OBJECT && target->type != STRING_JSON_ARRAY) {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                   nob_sv_from_cstr("string(JSON LENGTH) requires ARRAY or OBJECT"),
                                                   path, path_count);
        }
        char num[64];
        snprintf(num, sizeof(num), "%zu", target->type == STRING_JSON_ARRAY ? target->array_count : target->object_count);
        (void)eval_var_set_current(ctx, out_var, nob_sv_from_cstr(num));
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    if (eval_sv_eq_ci_lit(mode, "MEMBER")) {
        if (path_count < 1) {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                   nob_sv_from_cstr("string(JSON MEMBER) requires index argument"),
                                                   path, 0);
        }
        String_View index_sv = path[path_count - 1];
        String_Json_Error jerr = {0};
        String_Json_Value *target = NULL;
        if (!string_jsonv_resolve_path(root, path, path_count - 1, &target, &jerr, ctx)) {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                   jerr.message, path, jerr.path_prefix_count);
        }
        if (target->type != STRING_JSON_OBJECT) {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                   nob_sv_from_cstr("string(JSON MEMBER) requires OBJECT target"),
                                                   path, path_count - 1);
        }
        size_t idx = 0;
        if (!string_jsonv_parse_index_sv(index_sv, true, target->object_count, &idx)) {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                   string_json_message_with_token_temp(ctx, "string(JSON MEMBER) invalid index", index_sv),
                                                   path, path_count);
        }
        String_View member_key = nob_sv_from_cstr("");
        if (!string_jsonv_object_member_key_at_temp(ctx, target, idx, &member_key)) return false;
        (void)eval_var_set_current(ctx, out_var, member_key);
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    if (eval_sv_eq_ci_lit(mode, "REMOVE")) {
        if (path_count < 1) {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                   nob_sv_from_cstr("string(JSON REMOVE) requires at least one path token"),
                                                   path, 0);
        }
        String_Json_Error jerr = {0};
        String_Json_Value *parent = NULL;
        if (!string_jsonv_resolve_path(root, path, path_count - 1, &parent, &jerr, ctx)) {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                   jerr.message, path, jerr.path_prefix_count);
        }
        String_View rem = path[path_count - 1];
        if (parent->type == STRING_JSON_OBJECT) {
            size_t idx = 0;
            if (string_jsonv_object_find(parent, rem, &idx)) {
                for (size_t i = idx + 1; i < parent->object_count; i++) parent->object_items[i - 1] = parent->object_items[i];
                parent->object_count--;
            }
        } else if (parent->type == STRING_JSON_ARRAY) {
            size_t idx = 0;
            if (!string_jsonv_parse_index_sv(rem, true, parent->array_count, &idx)) {
                return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                       string_json_message_with_token_temp(ctx, "string(JSON REMOVE) invalid index", rem),
                                                       path, path_count);
            }
            for (size_t i = idx + 1; i < parent->array_count; i++) parent->array_items[i - 1] = parent->array_items[i];
            parent->array_count--;
        } else {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                   nob_sv_from_cstr("string(JSON REMOVE) requires OBJECT or ARRAY target"),
                                                   path, path_count - 1);
        }
        (void)eval_var_set_current(ctx, out_var, string_jsonv_serialize_temp(ctx, root));
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    if (eval_sv_eq_ci_lit(mode, "SET")) {
        if (path_count < 2) {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                   nob_sv_from_cstr("string(JSON SET) requires path and value arguments"),
                                                   path, 0);
        }
        String_View new_json = path[path_count - 1];
        String_Json_Value *new_value = NULL;
        if (!string_jsonv_parse_root_temp(ctx, new_json, &new_value, &err)) {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var, err, path, path_count - 1);
        }
        String_View key_or_index = path[path_count - 2];
        String_Json_Error jerr = {0};
        String_Json_Value *parent = NULL;
        if (!string_jsonv_resolve_path(root, path, path_count - 2, &parent, &jerr, ctx)) {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                   jerr.message, path, jerr.path_prefix_count);
        }
        if (parent->type == STRING_JSON_OBJECT) {
            size_t idx = 0;
            if (string_jsonv_object_find(parent, key_or_index, &idx)) {
                parent->object_items[idx].value = new_value;
            } else {
                if (!string_jsonv_object_push(ctx, parent, key_or_index, new_value)) { if (eval_should_stop(ctx)) return false; return true; }
            }
        } else if (parent->type == STRING_JSON_ARRAY) {
            size_t idx = 0;
            if (!string_jsonv_parse_index_sv(key_or_index, false, 0, &idx)) {
                return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                       string_json_message_with_token_temp(ctx, "string(JSON SET) invalid array index", key_or_index),
                                                       path, path_count - 1);
            }
            if (idx < parent->array_count) {
                parent->array_items[idx] = new_value;
            } else {
                if (!string_jsonv_array_push(ctx, parent, new_value)) { if (eval_should_stop(ctx)) return false; return true; }
            }
        } else {
            return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                                   nob_sv_from_cstr("string(JSON SET) requires OBJECT or ARRAY target"),
                                                   path, path_count - 2);
        }
        (void)eval_var_set_current(ctx, out_var, string_jsonv_serialize_temp(ctx, root));
        if (eval_should_stop(ctx)) return false;
        return true;
    }

    return string_json_emit_or_store_error(ctx, node, o, has_error_var, error_var, out_var,
                                           nob_sv_from_cstr("Unsupported string(JSON) mode"),
                                           path, 0);
}

Eval_Result eval_string_handle_json(EvalExecContext *ctx, const Node *node, Cmake_Event_Origin o, SV_List a) {
    if (!string_handle_json_command(ctx, node, o, a)) {
        return eval_result_from_ctx(ctx);
    }
    return eval_result_from_ctx(ctx);
}
