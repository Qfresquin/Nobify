#ifndef TEST_CASE_PACK_H_
#define TEST_CASE_PACK_H_

#include "arena.h"
#include "arena_dyn.h"

typedef struct {
    String_View name;
    String_View script;
} Test_Case_Pack_Entry;

static inline String_View test_case_pack_trim_cr(String_View sv) {
    if (sv.count > 0 && sv.data[sv.count - 1] == '\r') {
        return nob_sv_from_parts(sv.data, sv.count - 1);
    }
    return sv;
}

static inline bool test_case_pack_parse(Arena *arena,
                                        String_View content,
                                        Test_Case_Pack_Entry **out_items) {
    if (!arena || !out_items) return false;
    *out_items = NULL;

    Nob_String_Builder script_sb = {0};
    bool ok = true;
    bool in_case = false;
    String_View current_name = {0};

    size_t pos = 0;
    while (pos < content.count) {
        size_t line_start = pos;
        while (pos < content.count && content.data[pos] != '\n') pos++;
        size_t line_end = pos;
        if (pos < content.count && content.data[pos] == '\n') pos++;

        String_View raw_line = nob_sv_from_parts(content.data + line_start, line_end - line_start);
        String_View line = test_case_pack_trim_cr(raw_line);
        String_View case_name = line;

        if (nob_sv_chop_prefix(&case_name, nob_sv_from_cstr("#@@CASE "))) {
            if (in_case) {
                ok = false;
                break;
            }
            in_case = true;
            current_name = case_name;
            script_sb.count = 0;
            continue;
        }

        if (nob_sv_eq(line, nob_sv_from_cstr("#@@ENDCASE"))) {
            char *name = NULL;
            char *script = NULL;
            Test_Case_Pack_Entry entry = {0};

            if (!in_case) {
                ok = false;
                break;
            }

            name = arena_strndup(arena, current_name.data, current_name.count);
            script = arena_strndup(arena, script_sb.items ? script_sb.items : "", script_sb.count);
            if (!name || !script) {
                ok = false;
                break;
            }

            entry.name = nob_sv_from_parts(name, current_name.count);
            entry.script = nob_sv_from_parts(script, script_sb.count);
            if (!arena_arr_push(arena, *out_items, entry)) {
                ok = false;
                break;
            }

            in_case = false;
            current_name = (String_View){0};
            script_sb.count = 0;
            continue;
        }

        if (in_case) {
            nob_sb_append_buf(&script_sb, raw_line.data, raw_line.count);
            nob_sb_append(&script_sb, '\n');
        }
    }

    if (ok && in_case) ok = false;
    if (ok) {
        for (size_t i = 0; i < arena_arr_len(*out_items); i++) {
            for (size_t j = i + 1; j < arena_arr_len(*out_items); j++) {
                if (nob_sv_eq((*out_items)[i].name, (*out_items)[j].name)) {
                    ok = false;
                    break;
                }
            }
            if (!ok) break;
        }
    }

    nob_sb_free(script_sb);
    return ok && arena_arr_len(*out_items) > 0;
}

#endif // TEST_CASE_PACK_H_
