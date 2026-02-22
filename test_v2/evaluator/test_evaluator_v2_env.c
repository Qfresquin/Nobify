#include "test_evaluator_v2_env.h"

#include <string.h>

static bool token_list_append(Arena *arena, Token_List *list, Token token) {
    if (!arena || !list) return false;
    if (!arena_da_reserve(arena, (void**)&list->items, &list->capacity, sizeof(list->items[0]), list->count + 1)) return false;
    list->items[list->count++] = token;
    return true;
}

static Ast_Root parse_script(Arena *arena, const char *script) {
    Lexer lx = lexer_init(nob_sv_from_cstr(script));
    Token_List toks = {0};
    for (;;) {
        Token t = lexer_next(&lx);
        if (t.kind == TOKEN_END) break;
        if (!token_list_append(arena, &toks, t)) return (Ast_Root){0};
    }
    return parse_tokens(arena, toks);
}

bool env_init(Eval_Test_Env *env) {
    memset(env, 0, sizeof(*env));
    env->temp_arena = arena_create(2 * 1024 * 1024);
    env->event_arena = arena_create(2 * 1024 * 1024);
    if (!env->temp_arena || !env->event_arena) return false;

    Evaluator_Init init = {0};
    init.arena = env->temp_arena;
    init.event_arena = env->event_arena;
    init.stream = &env->stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";
    env->ctx = evaluator_create(&init);
    return env->ctx != NULL;
}

void env_free(Eval_Test_Env *env) {
    if (!env) return;
    if (env->event_arena) arena_destroy(env->event_arena);
    if (env->temp_arena) arena_destroy(env->temp_arena);
    memset(env, 0, sizeof(*env));
}

bool run_script(Eval_Test_Env *env, const char *script) {
    Ast_Root ast = parse_script(env->temp_arena, script);
    return evaluator_run(env->ctx, ast);
}

size_t count_events(const Eval_Test_Env *env, Cmake_Event_Kind kind) {
    size_t n = 0;
    for (size_t i = 0; i < env->stream.count; i++) {
        if (env->stream.items[i].kind == kind) n++;
    }
    return n;
}

bool has_event_item(const Eval_Test_Env *env, Cmake_Event_Kind kind, const char *item) {
    String_View needle = nob_sv_from_cstr(item);
    for (size_t i = 0; i < env->stream.count; i++) {
        const Cmake_Event *ev = &env->stream.items[i];
        if (ev->kind != kind) continue;
        if (kind == EV_GLOBAL_COMPILE_DEFINITIONS && nob_sv_eq(ev->as.global_compile_definitions.item, needle)) return true;
        if (kind == EV_GLOBAL_COMPILE_OPTIONS && nob_sv_eq(ev->as.global_compile_options.item, needle)) return true;
        if (kind == EV_TARGET_COMPILE_DEFINITIONS && nob_sv_eq(ev->as.target_compile_definitions.item, needle)) return true;
        if (kind == EV_TARGET_COMPILE_OPTIONS && nob_sv_eq(ev->as.target_compile_options.item, needle)) return true;
    }
    return false;
}

size_t count_target_prop_events(const Eval_Test_Env *env,
                                const char *target_name,
                                const char *key,
                                const char *value,
                                Cmake_Target_Property_Op op) {
    String_View tgt = nob_sv_from_cstr(target_name);
    String_View k = nob_sv_from_cstr(key);
    String_View v = nob_sv_from_cstr(value);
    size_t n = 0;
    for (size_t i = 0; i < env->stream.count; i++) {
        const Cmake_Event *ev = &env->stream.items[i];
        if (ev->kind != EV_TARGET_PROP_SET) continue;
        if (!nob_sv_eq(ev->as.target_prop_set.target_name, tgt)) continue;
        if (!nob_sv_eq(ev->as.target_prop_set.key, k)) continue;
        if (!nob_sv_eq(ev->as.target_prop_set.value, v)) continue;
        if (ev->as.target_prop_set.op != op) continue;
        n++;
    }
    return n;
}

size_t count_diag_warnings_for_command(const Eval_Test_Env *env, const char *command) {
    String_View cmd = nob_sv_from_cstr(command);
    size_t n = 0;
    for (size_t i = 0; i < env->stream.count; i++) {
        const Cmake_Event *ev = &env->stream.items[i];
        if (ev->kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity != EV_DIAG_WARNING) continue;
        if (nob_sv_eq(ev->as.diag.command, cmd)) n++;
    }
    return n;
}

size_t count_diag_errors_for_command(const Eval_Test_Env *env, const char *command) {
    String_View cmd = nob_sv_from_cstr(command);
    size_t n = 0;
    for (size_t i = 0; i < env->stream.count; i++) {
        const Cmake_Event *ev = &env->stream.items[i];
        if (ev->kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.command, cmd)) n++;
    }
    return n;
}

bool has_diag_cause_contains(const Eval_Test_Env *env, Cmake_Diag_Severity sev, const char *needle) {
    String_View n = nob_sv_from_cstr(needle);
    for (size_t i = 0; i < env->stream.count; i++) {
        const Cmake_Event *ev = &env->stream.items[i];
        if (ev->kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity != sev) continue;
        if (n.count == 0 || ev->as.diag.cause.count < n.count) continue;
        for (size_t j = 0; j + n.count <= ev->as.diag.cause.count; j++) {
            if (memcmp(ev->as.diag.cause.data + j, n.data, n.count) == 0) return true;
        }
    }
    return false;
}
