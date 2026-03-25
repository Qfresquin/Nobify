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

static bool opt_emit_missing_value(EvalExecContext *ctx,
                                   Eval_Opt_Parse_Config cfg,
                                   const Eval_Opt_Spec *spec,
                                   String_View option_tok) {
    if (!ctx || !spec) return false;
    String_View cause = spec->missing_value_cause
        ? nob_sv_from_cstr(spec->missing_value_cause)
        : nob_sv_from_cstr("Missing value after option");
    String_View hint = spec->missing_value_hint
        ? nob_sv_from_cstr(spec->missing_value_hint)
        : option_tok;
    EVAL_DIAG_EMIT_SEV(ctx,
                       EV_DIAG_ERROR,
                       EVAL_DIAG_MISSING_REQUIRED,
                       cfg.component,
                       cfg.command,
                       cfg.origin,
                       cause,
                       hint);
    return false;
}

static bool opt_emit_duplicate_option(EvalExecContext *ctx,
                                      Eval_Opt_Parse_Config cfg,
                                      const Eval_Opt_Spec *spec,
                                      String_View option_tok) {
    if (!ctx || !spec) return false;
    String_View cause = spec->duplicate_cause
        ? nob_sv_from_cstr(spec->duplicate_cause)
        : nob_sv_from_cstr("Duplicate option is not allowed here");
    String_View hint = spec->duplicate_hint
        ? nob_sv_from_cstr(spec->duplicate_hint)
        : option_tok;
    EVAL_DIAG_EMIT_SEV(ctx,
                       EV_DIAG_ERROR,
                       EVAL_DIAG_DUPLICATE_ARGUMENT,
                       cfg.component,
                       cfg.command,
                       cfg.origin,
                       cause,
                       hint);
    return false;
}

bool eval_opt_parse_walk(EvalExecContext *ctx,
                         SV_List args,
                         size_t start,
                         const Eval_Opt_Spec *specs,
                         size_t spec_count,
                         Eval_Opt_Parse_Config cfg,
                         Eval_Opt_On_Option_Fn on_option,
                         Eval_Opt_On_Positional_Fn on_positional,
                         void *userdata) {
    if (!ctx || !specs || !on_option) return false;
    bool *seen = NULL;
    if (spec_count > 0) {
        seen = (bool*)arena_alloc(eval_temp_arena(ctx), spec_count * sizeof(*seen));
        EVAL_OOM_RETURN_IF_NULL(ctx, seen, false);
        memset(seen, 0, spec_count * sizeof(*seen));
    }

    for (size_t i = start; i < arena_arr_len(args);) {
        String_View tok = args[i];
        int spec_idx = opt_find_spec_idx(tok, specs, spec_count);
        if (spec_idx < 0) {
            if (cfg.unknown_as_positional) {
                if (on_positional && !on_positional(ctx, userdata, tok, i)) return false;
            } else if (cfg.warn_unknown) {
                EVAL_DIAG_EMIT_SEV(ctx, EV_DIAG_WARNING, EVAL_DIAG_UNEXPECTED_ARGUMENT, cfg.component, cfg.command, cfg.origin, nob_sv_from_cstr("Unknown option token"), tok);
                if (eval_should_stop(ctx)) return false;
            }
            i++;
            continue;
        }

        const Eval_Opt_Spec *spec = &specs[spec_idx];
        if (spec->reject_duplicates && seen[spec_idx]) {
            return opt_emit_duplicate_option(ctx, cfg, spec, tok);
        }

        SV_List values = NULL;
        size_t token_index = i;
        i++;

        if (spec->kind == EVAL_OPT_SINGLE) {
            if (i >= arena_arr_len(args)) {
                return opt_emit_missing_value(ctx, cfg, spec, tok);
            }
            if (spec->value_must_not_be_keyword &&
                eval_opt_token_is_keyword(args[i], specs, spec_count)) {
                return opt_emit_missing_value(ctx, cfg, spec, tok);
            }
            if (!eval_sv_arr_push_temp(ctx, &values, args[i])) return false;
            i++;
        } else if (spec->kind == EVAL_OPT_OPTIONAL_SINGLE) {
            if (i < arena_arr_len(args) && opt_find_spec_idx(args[i], specs, spec_count) < 0) {
                if (!eval_sv_arr_push_temp(ctx, &values, args[i])) return false;
                i++;
            }
        } else if (spec->kind == EVAL_OPT_MULTI) {
            while (i < arena_arr_len(args) && opt_find_spec_idx(args[i], specs, spec_count) < 0) {
                if (!eval_sv_arr_push_temp(ctx, &values, args[i])) return false;
                i++;
            }
        } else if (spec->kind == EVAL_OPT_TAIL) {
            while (i < arena_arr_len(args)) {
                if (!eval_sv_arr_push_temp(ctx, &values, args[i])) return false;
                i++;
            }
        }

        if (arena_arr_len(values) < spec->min_values) {
            return opt_emit_missing_value(ctx, cfg, spec, tok);
        }

        seen[spec_idx] = true;
        if (!on_option(ctx, userdata, spec->id, values, token_index)) return false;
        if (eval_should_stop(ctx)) return false;
    }

    if (eval_should_stop(ctx)) return false;

    return true;
}
