#include "eval_opt_parser.h"

#include "arena_dyn.h"

#include <ctype.h>
#include <string.h>

static bool opt_sv_eq_ci_lit(String_View sv, const char *lit) {
    if (!lit) return false;
    size_t n = strlen(lit);
    if (sv.count != n) return false;
    for (size_t i = 0; i < n; i++) {
        char a = (char)tolower((unsigned char)sv.data[i]);
        char b = (char)tolower((unsigned char)lit[i]);
        if (a != b) return false;
    }
    return true;
}

static bool opt_sv_list_push_temp(Evaluator_Context *ctx, SV_List *list, String_View sv) {
    if (!ctx || !list) return false;
    if (!arena_da_reserve(eval_temp_arena(ctx),
                          (void**)&list->items,
                          &list->capacity,
                          sizeof(list->items[0]),
                          list->count + 1)) {
        return ctx_oom(ctx);
    }
    list->items[list->count++] = sv;
    return true;
}

static int opt_find_spec_idx(String_View tok, const Eval_Opt_Spec *specs, size_t spec_count) {
    if (!specs) return -1;
    for (size_t i = 0; i < spec_count; i++) {
        if (opt_sv_eq_ci_lit(tok, specs[i].name)) return (int)i;
    }
    return -1;
}

bool eval_opt_token_is_keyword(String_View tok,
                               const Eval_Opt_Spec *specs,
                               size_t spec_count) {
    return opt_find_spec_idx(tok, specs, spec_count) >= 0;
}

bool eval_opt_parse_walk(Evaluator_Context *ctx,
                         SV_List args,
                         size_t start,
                         const Eval_Opt_Spec *specs,
                         size_t spec_count,
                         Eval_Opt_Parse_Config cfg,
                         Eval_Opt_On_Option_Fn on_option,
                         Eval_Opt_On_Positional_Fn on_positional,
                         void *userdata) {
    if (!ctx || !specs || !on_option) return false;

    for (size_t i = start; i < args.count;) {
        String_View tok = args.items[i];
        int spec_idx = opt_find_spec_idx(tok, specs, spec_count);
        if (spec_idx < 0) {
            if (cfg.unknown_as_positional) {
                if (on_positional && !on_positional(ctx, userdata, tok, i)) return false;
            } else if (cfg.warn_unknown) {
                eval_emit_diag(ctx,
                               EV_DIAG_WARNING,
                               cfg.component,
                               cfg.command,
                               cfg.origin,
                               nob_sv_from_cstr("Unknown option token"),
                               tok);
                if (eval_should_stop(ctx)) return false;
            }
            i++;
            continue;
        }

        const Eval_Opt_Spec *spec = &specs[spec_idx];
        SV_List values = {0};
        size_t token_index = i;
        i++;

        if (spec->kind == EVAL_OPT_SINGLE) {
            if (i >= args.count) {
                eval_emit_diag(ctx,
                               EV_DIAG_ERROR,
                               cfg.component,
                               cfg.command,
                               cfg.origin,
                               nob_sv_from_cstr("Missing value after option"),
                               tok);
                return false;
            }
            if (!opt_sv_list_push_temp(ctx, &values, args.items[i])) return false;
            i++;
        } else if (spec->kind == EVAL_OPT_OPTIONAL_SINGLE) {
            if (i < args.count && opt_find_spec_idx(args.items[i], specs, spec_count) < 0) {
                if (!opt_sv_list_push_temp(ctx, &values, args.items[i])) return false;
                i++;
            }
        } else if (spec->kind == EVAL_OPT_MULTI) {
            while (i < args.count && opt_find_spec_idx(args.items[i], specs, spec_count) < 0) {
                if (!opt_sv_list_push_temp(ctx, &values, args.items[i])) return false;
                i++;
            }
        }

        if (!on_option(ctx, userdata, spec->id, values, token_index)) return false;
        if (eval_should_stop(ctx)) return false;
    }

    return !eval_should_stop(ctx);
}
