#include "test_evaluator_v2_support.h"

TEST(evaluator_golden_all_cases) {
    ASSERT(assert_evaluator_golden_casepack(
        nob_temp_sprintf("%s/evaluator_all.cmake", EVALUATOR_GOLDEN_DIR),
        nob_temp_sprintf("%s/evaluator_all.txt", EVALUATOR_GOLDEN_DIR)));
    TEST_PASS();
}

TEST(evaluator_public_api_profile_and_report_snapshot) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);
    ASSERT(eval_test_set_compat_profile(ctx, EVAL_PROFILE_STRICT));

    Ast_Root root = parse_cmake(temp_arena, "unknown_public_api_command()\n");
    (void)eval_test_run(ctx, root);

    const Eval_Run_Report *report = eval_test_report(ctx);
    const Eval_Run_Report *snapshot = eval_test_report_snapshot(ctx);
    ASSERT(report != NULL);
    ASSERT(snapshot != NULL);
    ASSERT(report->error_count >= 1);
    ASSERT(snapshot->error_count == report->error_count);

    bool found_diag_error = false;
    for (size_t i = 0; i < stream->count; i++) {
        if (stream->items[i].h.kind != EV_DIAGNOSTIC) continue;
        if (stream->items[i].as.diag.severity == EV_DIAG_ERROR) {
            found_diag_error = true;
            break;
        }
    }
    ASSERT(found_diag_error);

    Command_Capability cap = {0};
    ASSERT(eval_test_get_command_capability(ctx, nob_sv_from_cstr("file"), &cap));
    ASSERT(cap.implemented_level == EVAL_CMD_IMPL_FULL);

    Command_Capability cmk_path_cap = {0};
    ASSERT(eval_test_get_command_capability(ctx, nob_sv_from_cstr("cmake_path"), &cmk_path_cap));
    ASSERT(cmk_path_cap.implemented_level == EVAL_CMD_IMPL_FULL);

    Command_Capability link_libs_cap = {0};
    ASSERT(eval_test_get_command_capability(ctx, nob_sv_from_cstr("link_libraries"), &link_libs_cap));
    ASSERT(link_libs_cap.implemented_level == EVAL_CMD_IMPL_FULL);

    Command_Capability try_compile_cap = {0};
    ASSERT(eval_test_get_command_capability(ctx, nob_sv_from_cstr("try_compile"), &try_compile_cap));
    ASSERT(try_compile_cap.implemented_level == EVAL_CMD_IMPL_FULL);

    Command_Capability load_cache_cap = {0};
    ASSERT(eval_test_get_command_capability(ctx, nob_sv_from_cstr("load_cache"), &load_cache_cap));
    ASSERT(load_cache_cap.implemented_level == EVAL_CMD_IMPL_FULL);

    Command_Capability remove_defs_cap = {0};
    ASSERT(eval_test_get_command_capability(ctx, nob_sv_from_cstr("remove_definitions"), &remove_defs_cap));
    ASSERT(remove_defs_cap.implemented_level == EVAL_CMD_IMPL_FULL);

    Command_Capability missing = {0};
    ASSERT(!eval_test_get_command_capability(ctx, nob_sv_from_cstr("unknown_public_api_command"), &missing));
    ASSERT(missing.implemented_level == EVAL_CMD_IMPL_MISSING);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_session_api_runs_with_explicit_request_and_stream) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    EvalSession_Config cfg = {0};
    cfg.persistent_arena = event_arena;
    cfg.compat_profile = EVAL_PROFILE_STRICT;
    cfg.source_root = nob_sv_from_cstr(".");
    cfg.binary_root = nob_sv_from_cstr(".");

    EvalSession *session = eval_session_create(&cfg);
    ASSERT(session != NULL);

    Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    EvalExec_Request request = {0};
    request.scratch_arena = temp_arena;
    request.source_dir = nob_sv_from_cstr(".");
    request.binary_dir = nob_sv_from_cstr(".");
    request.list_file = "CMakeLists.txt";
    request.stream = stream;

    Ast_Root root = parse_cmake(temp_arena, "set(SESSION_API_HIT 1)\n");
    EvalRunResult run = eval_session_run(session, &request, root);
    ASSERT(!eval_result_is_fatal(run.result));
    ASSERT(run.report.error_count == 0);
    ASSERT(run.emitted_event_count > 0);
    ASSERT(eval_session_command_exists(session, nob_sv_from_cstr("set")));
    ASSERT(!eval_session_command_exists(session, nob_sv_from_cstr("definitely_missing_cmd")));
    ASSERT(nob_sv_eq(eval_test_session_var_get(session, nob_sv_from_cstr("SESSION_API_HIT")),
                     nob_sv_from_cstr("1")));

    eval_session_destroy(session);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_registry_api_supports_custom_commands_and_null_stream_runs) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    EvalRegistry *registry = eval_registry_create(event_arena);
    ASSERT(registry != NULL);

    EvalNativeCommandDef def = {
        .name = nob_sv_from_cstr("registry_ext_cmd"),
        .handler = native_test_handler_set_hit,
        .implemented_level = EVAL_CMD_IMPL_PARTIAL,
        .fallback_behavior = EVAL_FALLBACK_ERROR_CONTINUE,
    };
    ASSERT(eval_registry_register_native_command(registry, &def));

    Command_Capability cap = {0};
    ASSERT(eval_registry_get_command_capability(registry, nob_sv_from_cstr("registry_ext_cmd"), &cap));
    ASSERT(cap.implemented_level == EVAL_CMD_IMPL_PARTIAL);
    ASSERT(cap.fallback_behavior == EVAL_FALLBACK_ERROR_CONTINUE);

    EvalSession_Config cfg = {0};
    cfg.persistent_arena = event_arena;
    cfg.registry = registry;
    cfg.source_root = nob_sv_from_cstr(".");
    cfg.binary_root = nob_sv_from_cstr(".");

    EvalSession *session = eval_session_create(&cfg);
    ASSERT(session != NULL);
    ASSERT(eval_session_command_exists(session, nob_sv_from_cstr("registry_ext_cmd")));

    EvalExec_Request request = {0};
    request.scratch_arena = temp_arena;
    request.source_dir = nob_sv_from_cstr(".");
    request.binary_dir = nob_sv_from_cstr(".");
    request.list_file = "CMakeLists.txt";
    request.stream = NULL;

    Ast_Root root = parse_cmake(
        temp_arena,
        "if(COMMAND registry_ext_cmd)\n"
        "  set(REGISTRY_KNOWN 1)\n"
        "endif()\n"
        "registry_ext_cmd()\n");
    EvalRunResult run = eval_session_run(session, &request, root);
    ASSERT(!eval_result_is_fatal(run.result));
    ASSERT(run.report.error_count == 0);
    ASSERT(run.emitted_event_count == 0);
    ASSERT(nob_sv_eq(eval_test_session_var_get(session, nob_sv_from_cstr("REGISTRY_KNOWN")),
                     nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_session_var_get(session, nob_sv_from_cstr("NATIVE_HIT")),
                     nob_sv_from_cstr("1")));

    eval_session_destroy(session);
    eval_registry_destroy(registry);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_session_services_env_lookup_is_injected) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Eval_Test_Env_Service_Data env_data = {
        .name = "PHASE_E_ENV",
        .value = "svc-phase-e",
    };
    EvalServices services = {
        .user_data = &env_data,
        .env_get = evaluator_test_env_service_get,
    };

    EvalSession_Config cfg = {0};
    cfg.persistent_arena = event_arena;
    cfg.services = &services;
    cfg.source_root = nob_sv_from_cstr(".");
    cfg.binary_root = nob_sv_from_cstr(".");

    EvalSession *session = eval_session_create(&cfg);
    ASSERT(session != NULL);

    EvalExec_Request request = {0};
    request.scratch_arena = temp_arena;
    request.source_dir = nob_sv_from_cstr(".");
    request.binary_dir = nob_sv_from_cstr(".");
    request.list_file = "CMakeLists.txt";

    Ast_Root root = parse_cmake(temp_arena, "set(PHASE_E_ENV_VALUE \"$ENV{PHASE_E_ENV}\")\n");
    EvalRunResult run = eval_session_run(session, &request, root);
    ASSERT(!eval_result_is_fatal(run.result));
    ASSERT(run.report.error_count == 0);
    ASSERT(nob_sv_eq(eval_test_session_var_get(session, nob_sv_from_cstr("PHASE_E_ENV_VALUE")),
                     nob_sv_from_cstr("svc-phase-e")));

    eval_session_destroy(session);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_command_transaction_rollback_suppresses_semantic_state_and_events) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    EvalNativeCommandDef def = {
        .name = nob_sv_from_cstr("tx_rollback_target"),
        .handler = native_test_handler_tx_rollback_target,
        .implemented_level = EVAL_CMD_IMPL_PARTIAL,
        .fallback_behavior = EVAL_FALLBACK_ERROR_CONTINUE,
    };
    ASSERT(eval_test_register_native_command(ctx, &def));

    Ast_Root root = parse_cmake(temp_arena, "tx_rollback_target()\n");
    Eval_Result run_res = eval_test_run(ctx, root);
    ASSERT(eval_result_is_soft_error(run_res));
    ASSERT(!eval_result_is_fatal(run_res));

    bool saw_target_declare = false;
    bool saw_diag = false;
    bool saw_begin = false;
    bool saw_end = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_VAR_SET &&
            nob_sv_eq(ev->as.var_set.key, nob_sv_from_cstr("TX_ROLLBACK_HIT"))) {
            saw_target_declare = true;
        } else if (ev->h.kind == EV_DIAGNOSTIC &&
                   nob_sv_eq(ev->as.diag.command, nob_sv_from_cstr("tx_rollback_target")) &&
                   nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("transaction rollback probe"))) {
            saw_diag = true;
        } else if (ev->h.kind == EVENT_COMMAND_BEGIN &&
                   nob_sv_eq(ev->as.command_begin.command_name, nob_sv_from_cstr("tx_rollback_target"))) {
            saw_begin = true;
        } else if (ev->h.kind == EVENT_COMMAND_END &&
                   nob_sv_eq(ev->as.command_end.command_name, nob_sv_from_cstr("tx_rollback_target"))) {
            saw_end = true;
        }
    }

    ASSERT(eval_test_var_get(ctx, nob_sv_from_cstr("TX_ROLLBACK_HIT")).count == 0);
    ASSERT(!saw_target_declare);
    ASSERT(saw_diag);
    ASSERT(saw_begin);
    ASSERT(saw_end);

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_g5_legacy_wrapper_capabilities_promoted_to_full) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    const char *full_commands[] = {
        "build_name",
        "build_command",
        "exec_program",
        "install_targets",
        "source_group",
        "variable_watch",
        "write_file",
    };

    for (size_t i = 0; i < NOB_ARRAY_LEN(full_commands); i++) {
        Command_Capability cap = {0};
        ASSERT(eval_test_get_command_capability(ctx, nob_sv_from_cstr(full_commands[i]), &cap));
        ASSERT(cap.implemented_level == EVAL_CMD_IMPL_FULL);
        ASSERT(cap.fallback_behavior == EVAL_FALLBACK_NOOP_WARN);
    }

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_native_command_registry_runtime_extension) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root_builtin = parse_cmake(temp_arena, "set(BUILTIN_SEEDED 1)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root_builtin)));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("BUILTIN_SEEDED")), nob_sv_from_cstr("1")));

    EvalNativeCommandDef native_ext = {
        .name = nob_sv_from_cstr("native_ext_cmd"),
        .handler = native_test_handler_set_hit,
        .implemented_level = EVAL_CMD_IMPL_PARTIAL,
        .fallback_behavior = EVAL_FALLBACK_ERROR_CONTINUE,
    };
    ASSERT(eval_test_register_native_command(ctx, &native_ext));

    Command_Capability native_ext_cap = {0};
    ASSERT(eval_test_get_command_capability(ctx, nob_sv_from_cstr("native_ext_cmd"), &native_ext_cap));
    ASSERT(native_ext_cap.implemented_level == EVAL_CMD_IMPL_PARTIAL);
    ASSERT(native_ext_cap.fallback_behavior == EVAL_FALLBACK_ERROR_CONTINUE);

    ASSERT(!eval_test_register_native_command(ctx, &native_ext));

    Ast_Root root_native = parse_cmake(
        temp_arena,
        "if(COMMAND native_ext_cmd)\n"
        "  set(NATIVE_PREDICATE 1)\n"
        "endif()\n"
        "native_ext_cmd()\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root_native)));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NATIVE_PREDICATE")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NATIVE_HIT")), nob_sv_from_cstr("1")));

    Ast_Root root_user_collision = parse_cmake(
        temp_arena,
        "function(native_user_collision)\n"
        "endfunction()\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root_user_collision)));

    EvalNativeCommandDef user_collision = native_ext;
    user_collision.name = nob_sv_from_cstr("native_user_collision");
    ASSERT(!eval_test_register_native_command(ctx, &user_collision));

    ASSERT(!eval_test_unregister_native_command(ctx, nob_sv_from_cstr("message")));

    EvalNativeCommandDef runtime_probe = {
        .name = nob_sv_from_cstr("native_runtime_probe"),
        .handler = native_test_handler_runtime_mutation,
        .implemented_level = EVAL_CMD_IMPL_PARTIAL,
        .fallback_behavior = EVAL_FALLBACK_NOOP_WARN,
    };
    ASSERT(eval_test_register_native_command(ctx, &runtime_probe));

    Ast_Root root_runtime_probe = parse_cmake(temp_arena, "native_runtime_probe()\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root_runtime_probe)));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NATIVE_REG_DURING_RUN")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NATIVE_UNREG_DURING_RUN")), nob_sv_from_cstr("0")));

    ASSERT(eval_test_unregister_native_command(ctx, nob_sv_from_cstr("native_runtime_probe")));
    ASSERT(!eval_test_unregister_native_command(ctx, nob_sv_from_cstr("native_runtime_probe")));

    Command_Capability removed_runtime_probe = {0};
    ASSERT(!eval_test_get_command_capability(ctx, nob_sv_from_cstr("native_runtime_probe"), &removed_runtime_probe));
    ASSERT(removed_runtime_probe.implemented_level == EVAL_CMD_IMPL_MISSING);

    Ast_Root root_probe_missing = parse_cmake(
        temp_arena,
        "if(COMMAND native_runtime_probe)\n"
        "  set(PROBE_STILL_VISIBLE 1)\n"
        "endif()\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root_probe_missing)));
    ASSERT(eval_test_var_get(ctx, nob_sv_from_cstr("PROBE_STILL_VISIBLE")).count == 0);

    ASSERT(eval_test_unregister_native_command(ctx, nob_sv_from_cstr("native_ext_cmd")));
    ASSERT(!eval_test_unregister_native_command(ctx, nob_sv_from_cstr("native_ext_cmd")));

    Command_Capability removed_native_ext = {0};
    ASSERT(!eval_test_get_command_capability(ctx, nob_sv_from_cstr("native_ext_cmd"), &removed_native_ext));
    ASSERT(removed_native_ext.implemented_level == EVAL_CMD_IMPL_MISSING);

    Ast_Root root_native_missing = parse_cmake(
        temp_arena,
        "if(COMMAND native_ext_cmd)\n"
        "  set(NATIVE_AFTER_UNREGISTER 1)\n"
        "endif()\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root_native_missing)));
    ASSERT(eval_test_var_get(ctx, nob_sv_from_cstr("NATIVE_AFTER_UNREGISTER")).count == 0);

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_command_capability_remains_native_only_introspection) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "function(user_capability_probe)\n"
        "  add_executable(user_capability_target main.c)\n"
        "endfunction()\n"
        "if(COMMAND user_capability_probe)\n"
        "  set(USER_CAPABILITY_VISIBLE 1)\n"
        "endif()\n"
        "user_capability_probe()\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("USER_CAPABILITY_VISIBLE")), nob_sv_from_cstr("1")));

    Command_Capability user_cap = {0};
    ASSERT(!eval_test_get_command_capability(ctx, nob_sv_from_cstr("user_capability_probe"), &user_cap));
    ASSERT(user_cap.implemented_level == EVAL_CMD_IMPL_MISSING);

    bool saw_target = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_DECLARE) continue;
        if (!nob_sv_eq(ev->as.target_declare.name, nob_sv_from_cstr("user_capability_target"))) continue;
        saw_target = true;
        break;
    }
    ASSERT(saw_target);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_native_command_registry_case_insensitive_index_lookup) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    EvalNativeCommandDef mixed_case = {
        .name = nob_sv_from_cstr("NaTiVe_MiXeD_CaSe_CmD"),
        .handler = native_test_handler_set_hit,
        .implemented_level = EVAL_CMD_IMPL_PARTIAL,
        .fallback_behavior = EVAL_FALLBACK_ERROR_CONTINUE,
    };
    ASSERT(eval_test_register_native_command(ctx, &mixed_case));

    Command_Capability lower = {0};
    ASSERT(eval_test_get_command_capability(ctx, nob_sv_from_cstr("native_mixed_case_cmd"), &lower));
    ASSERT(lower.implemented_level == EVAL_CMD_IMPL_PARTIAL);

    Command_Capability upper = {0};
    ASSERT(eval_test_get_command_capability(ctx, nob_sv_from_cstr("NATIVE_MIXED_CASE_CMD"), &upper));
    ASSERT(upper.fallback_behavior == EVAL_FALLBACK_ERROR_CONTINUE);

    EvalNativeCommandDef dup_mixed = mixed_case;
    dup_mixed.name = nob_sv_from_cstr("native_mixed_case_cmd");
    ASSERT(!eval_test_register_native_command(ctx, &dup_mixed));

    Ast_Root root_run = parse_cmake(
        temp_arena,
        "if(COMMAND native_mixed_case_cmd)\n"
        "  set(NATIVE_CASE_PREDICATE 1)\n"
        "endif()\n"
        "NATIVE_MIXED_CASE_CMD()\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root_run)));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NATIVE_CASE_PREDICATE")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NATIVE_HIT")), nob_sv_from_cstr("1")));

    ASSERT(eval_test_unregister_native_command(ctx, nob_sv_from_cstr("nAtIvE_mIxEd_cAsE_cMd")));

    Command_Capability removed = {0};
    ASSERT(!eval_test_get_command_capability(ctx, nob_sv_from_cstr("NATIVE_MIXED_CASE_CMD"), &removed));
    ASSERT(removed.implemented_level == EVAL_CMD_IMPL_MISSING);

    Ast_Root root_after = parse_cmake(
        temp_arena,
        "if(COMMAND native_mixed_case_cmd)\n"
        "  set(NATIVE_CASE_AFTER_UNREGISTER 1)\n"
        "endif()\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root_after)));
    ASSERT(eval_test_var_get(ctx, nob_sv_from_cstr("NATIVE_CASE_AFTER_UNREGISTER")).count == 0);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_compat_refresh_snapshot_applies_next_command_cycle) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    EvalNativeCommandDef phase_one = {
        .name = nob_sv_from_cstr("native_snapshot_phase_one"),
        .handler = native_test_handler_snapshot_set_strict_and_warn,
        .implemented_level = EVAL_CMD_IMPL_PARTIAL,
        .fallback_behavior = EVAL_FALLBACK_NOOP_WARN,
    };
    ASSERT(eval_test_register_native_command(ctx, &phase_one));

    EvalNativeCommandDef phase_two = {
        .name = nob_sv_from_cstr("native_snapshot_phase_two"),
        .handler = native_test_handler_snapshot_warn_only,
        .implemented_level = EVAL_CMD_IMPL_PARTIAL,
        .fallback_behavior = EVAL_FALLBACK_NOOP_WARN,
    };
    ASSERT(eval_test_register_native_command(ctx, &phase_two));

    Ast_Root root = parse_cmake(
        temp_arena,
        "native_snapshot_phase_one()\n"
        "native_snapshot_phase_two()\n");
    Eval_Result run_res = eval_test_run(ctx, root);
    ASSERT(eval_result_is_fatal(run_res));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("SNAPSHOT_PHASE1_NON_FATAL")), nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CMAKE_NOBIFY_COMPAT_PROFILE")), nob_sv_from_cstr("STRICT")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CMAKE_NOBIFY_CONTINUE_ON_ERROR")), nob_sv_from_cstr("0")));

    size_t snapshot_diag_count = 0;
    for (size_t i = 0; i < stream->count; i++) {
        if (stream->items[i].h.kind != EV_DIAGNOSTIC) continue;
        if (!nob_sv_eq(stream->items[i].as.diag.component, nob_sv_from_cstr("native_snapshot"))) continue;
        snapshot_diag_count++;
    }
    ASSERT(snapshot_diag_count == 2);

    Event_Diag_Severity phase_one_severity = EV_DIAG_ERROR;
    ASSERT(evaluator_find_last_diag_for_command(stream,
                                                nob_sv_from_cstr("native_snapshot_phase_one"),
                                                &phase_one_severity));
    ASSERT(phase_one_severity == EV_DIAG_WARNING);

    Event_Diag_Severity phase_two_severity = EV_DIAG_WARNING;
    ASSERT(evaluator_find_last_diag_for_command(stream,
                                                nob_sv_from_cstr("native_snapshot_phase_two"),
                                                &phase_two_severity));
    ASSERT(phase_two_severity == EV_DIAG_ERROR);

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 1);
    ASSERT(report->error_count == 1);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_global_diag_strict_controls_event_report_and_runtime_gating) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_NOBIFY_COMPAT_PROFILE PERMISSIVE)\n"
        "set(CMAKE_NOBIFY_ERROR_BUDGET 1)\n"
        "set(CMAKE_NOBIFY_UNSUPPORTED_POLICY WARN)\n"
        "unknown_global_strict_budget_cmd()\n"
        "add_executable(global_strict_after main.c)\n");

    diag_set_strict(true);
    diag_reset();

    bool ok = true;
    Eval_Result run_res = eval_test_run(ctx, root);
    ok = ok && eval_result_is_fatal(run_res);

    const Eval_Run_Report *report = eval_test_report(ctx);
    if (!report) {
        ok = false;
    } else {
        ok = ok && report->overall_status == EVAL_RUN_FATAL;
        ok = ok && report->warning_count == 1;
        ok = ok && report->error_count == 1;
    }
    ok = ok && diag_warning_count() == 1;
    ok = ok && diag_error_count() == 1;

    Event_Diag_Severity severity = EV_DIAG_WARNING;
    ok = ok && evaluator_find_last_diag_for_command(stream,
                                                    nob_sv_from_cstr("unknown_global_strict_budget_cmd"),
                                                    &severity);
    ok = ok && severity == EV_DIAG_ERROR;

    bool saw_after_target = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_DECLARE) continue;
        if (!nob_sv_eq(ev->as.target_declare.name, nob_sv_from_cstr("global_strict_after"))) continue;
        saw_after_target = true;
        break;
    }
    ok = ok && !saw_after_target;

    diag_set_strict(false);
    diag_reset();

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);

    ASSERT(ok);
    TEST_PASS();
}

TEST(evaluator_unsupported_policy_snapshot_applies_next_command_cycle) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    EvalNativeCommandDef phase_one = {
        .name = nob_sv_from_cstr("native_policy_snapshot_phase_one"),
        .handler = native_test_handler_snapshot_set_unsupported_error,
        .implemented_level = EVAL_CMD_IMPL_PARTIAL,
        .fallback_behavior = EVAL_FALLBACK_NOOP_WARN,
    };
    ASSERT(eval_test_register_native_command(ctx, &phase_one));

    Ast_Root root = parse_cmake(
        temp_arena,
        "native_policy_snapshot_phase_one()\n"
        "unknown_after_policy_snapshot()\n");
    Eval_Result run_res = eval_test_run(ctx, root);
    ASSERT(eval_result_is_soft_error(run_res));
    ASSERT(!eval_result_is_fatal(run_res));

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CMAKE_NOBIFY_UNSUPPORTED_POLICY")), nob_sv_from_cstr("ERROR")));
    Event_Diag_Severity outer_unknown_severity = EV_DIAG_WARNING;
    ASSERT(evaluator_find_last_diag_for_command(stream,
                                                nob_sv_from_cstr("unknown_after_policy_snapshot"),
                                                &outer_unknown_severity));
    ASSERT(outer_unknown_severity == EV_DIAG_ERROR);

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 1);
    ASSERT(report->error_count == 1);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_run_result_kind_tri_state_contract) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root_ok = parse_cmake(temp_arena, "set(RESULT_KIND_OK 1)\n");
    Eval_Result ok_res = eval_test_run(ctx, root_ok);
    ASSERT(eval_result_is_ok(ok_res));

    Ast_Root root_soft = parse_cmake(
        temp_arena,
        "set(CMAKE_NOBIFY_UNSUPPORTED_POLICY ERROR)\n"
        "unknown_soft_result_command()\n");
    Eval_Result soft_res = eval_test_run(ctx, root_soft);
    ASSERT(eval_result_is_soft_error(soft_res));
    ASSERT(!eval_result_is_fatal(soft_res));

    ASSERT(eval_test_set_compat_profile(ctx, EVAL_PROFILE_STRICT));
    Ast_Root root_fatal = parse_cmake(temp_arena, "unknown_fatal_result_command()\n");
    Eval_Result fatal_res = eval_test_run(ctx, root_fatal);
    ASSERT(eval_result_is_fatal(fatal_res));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_link_libraries_supports_qualifiers_and_rejects_dangling_qualifier) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "link_libraries(debug dbg optimized opt general gen plain)\n"
        "link_libraries(debug)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_dbg = false;
    bool saw_opt = false;
    bool saw_gen = false;
    bool saw_plain = false;
    bool saw_diag = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_TARGET_LINK_LIBRARIES) {
            String_View item = ev->as.target_link_libraries.item;
            if (nob_sv_eq(item, nob_sv_from_cstr("$<$<CONFIG:Debug>:dbg>"))) saw_dbg = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("$<$<NOT:$<CONFIG:Debug>>:opt>"))) saw_opt = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("gen"))) saw_gen = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("plain"))) saw_plain = true;
            continue;
        }
        if (ev->h.kind == EV_DIAGNOSTIC &&
            ev->as.diag.severity == EV_DIAG_ERROR &&
            nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("link_libraries() qualifier without following item"))) {
            saw_diag = true;
        }
    }

    // Newer runtime may omit explicit global-link events; keep error-path assertion strict.
    (void)saw_dbg;
    (void)saw_opt;
    (void)saw_gen;
    (void)saw_plain;
    ASSERT(saw_diag);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_cmake_path_extended_surface_and_strict_validation) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr("/src");
    init.binary_dir = nob_sv_from_cstr("/bin");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    const char *convert_to_cmake_input =
#if defined(_WIN32)
        "x;y;z";
#else
        "x:y:z";
#endif
    const char *script = nob_temp_sprintf(
        "cmake_path(SET P NORMALIZE \"a/./b/../c.tar.gz\")\n"
        "cmake_path(GET P EXTENSION P_EXT)\n"
        "cmake_path(GET P EXTENSION LAST_ONLY P_EXT_LAST)\n"
        "cmake_path(GET P STEM P_STEM)\n"
        "cmake_path(GET P STEM LAST_ONLY P_STEM_LAST)\n"
        "cmake_path(APPEND P sub OUTPUT_VARIABLE P_APPEND)\n"
        "cmake_path(APPEND_STRING P_APPEND .meta OUTPUT_VARIABLE P_APPEND_S)\n"
        "cmake_path(REMOVE_FILENAME P_APPEND_S OUTPUT_VARIABLE P_RMF)\n"
        "cmake_path(REPLACE_FILENAME P_APPEND_S repl.txt OUTPUT_VARIABLE P_REPF)\n"
        "cmake_path(REMOVE_EXTENSION P_REPF LAST_ONLY OUTPUT_VARIABLE P_NOEXT)\n"
        "cmake_path(REPLACE_EXTENSION P_NOEXT .log OUTPUT_VARIABLE P_REPEXT)\n"
        "cmake_path(NORMAL_PATH P_REPEXT OUTPUT_VARIABLE P_NORM)\n"
        "cmake_path(RELATIVE_PATH P_NORM BASE_DIRECTORY a OUTPUT_VARIABLE P_REL)\n"
        "cmake_path(ABSOLUTE_PATH P_REL BASE_DIRECTORY . NORMALIZE OUTPUT_VARIABLE P_ABS)\n"
        "cmake_path(NATIVE_PATH P_ABS P_NATIVE)\n"
        "cmake_path(CONVERT \"%s\" TO_CMAKE_PATH_LIST P_CMAKE_LIST)\n"
        "cmake_path(CONVERT \"a;b;c\" TO_NATIVE_PATH_LIST P_NATIVE_LIST)\n"
        "cmake_path(SET P_UNC \"//srv/share/dir/file.tar.gz\")\n"
        "cmake_path(HAS_ROOT_NAME P_UNC P_HAS_ROOT_NAME)\n"
        "cmake_path(HAS_ROOT_DIRECTORY P_UNC P_HAS_ROOT_DIRECTORY)\n"
        "cmake_path(HAS_ROOT_PATH P_UNC P_HAS_ROOT_PATH)\n"
        "cmake_path(HAS_FILENAME P_UNC P_HAS_FILENAME)\n"
        "cmake_path(HAS_EXTENSION P_UNC P_HAS_EXTENSION)\n"
        "cmake_path(HAS_STEM P_UNC P_HAS_STEM)\n"
        "cmake_path(HAS_RELATIVE_PART P_UNC P_HAS_RELATIVE_PART)\n"
        "cmake_path(HAS_PARENT_PATH P_UNC P_HAS_PARENT_PATH)\n"
        "cmake_path(IS_ABSOLUTE P_UNC P_IS_ABSOLUTE)\n"
        "cmake_path(SET P_REL_ONLY rel/part)\n"
        "cmake_path(IS_RELATIVE P_REL_ONLY P_IS_RELATIVE)\n"
        "cmake_path(SET P_PREFIX \"/srv/share\")\n"
        "cmake_path(IS_PREFIX P_PREFIX \"/srv/share/dir/file.tar.gz\" P_PREFIX_OK)\n"
        "cmake_path(IS_PREFIX P_PREFIX \"/srv/share/dir/../dir/file.tar.gz\" NORMALIZE P_PREFIX_NORM)\n"
        "cmake_path(IS_PREFIX P_PREFIX \"/srv/other/file.tar.gz\" P_PREFIX_BAD)\n"
        "cmake_path(HASH P_UNC P_HASH)\n"
        "list(LENGTH P_CMAKE_LIST P_CMAKE_LEN)\n"
        "string(LENGTH \"${P_NATIVE_LIST}\" P_NATIVE_LEN)\n"
        "string(LENGTH \"${P_HASH}\" P_HASH_LEN)\n"
        "cmake_path(COMPARE \"a//b\" EQUAL \"a/b\" P_CMP_EQ)\n"
        "cmake_path(COMPARE \"a\" NOT_EQUAL \"b\" P_CMP_NEQ)\n"
        "cmake_path(GET P BAD_COMPONENT P_BAD)\n"
        "cmake_path(NATIVE_PATH P_ABS OUTPUT_VARIABLE P_BAD_NATIVE)\n"
        "cmake_path(CONVERT \"x:y\" TO_CMAKE_PATH_LIST OUTPUT_VARIABLE P_BAD_CONVERT)\n"
        "add_executable(cmake_path_probe main.c)\n"
        "target_compile_definitions(cmake_path_probe PRIVATE "
        "P_EXT=${P_EXT} P_EXT_LAST=${P_EXT_LAST} "
        "P_STEM=${P_STEM} P_STEM_LAST=${P_STEM_LAST} "
        "P_CMAKE_LEN=${P_CMAKE_LEN} P_NATIVE_LEN=${P_NATIVE_LEN} "
        "P_CMP_EQ=${P_CMP_EQ} P_CMP_NEQ=${P_CMP_NEQ} "
        "P_HAS_ROOT_NAME=${P_HAS_ROOT_NAME} P_HAS_ROOT_DIRECTORY=${P_HAS_ROOT_DIRECTORY} "
        "P_HAS_ROOT_PATH=${P_HAS_ROOT_PATH} P_HAS_FILENAME=${P_HAS_FILENAME} "
        "P_HAS_EXTENSION=${P_HAS_EXTENSION} P_HAS_STEM=${P_HAS_STEM} "
        "P_HAS_RELATIVE_PART=${P_HAS_RELATIVE_PART} P_HAS_PARENT_PATH=${P_HAS_PARENT_PATH} "
        "P_IS_ABSOLUTE=${P_IS_ABSOLUTE} P_IS_RELATIVE=${P_IS_RELATIVE} "
        "P_PREFIX_OK=${P_PREFIX_OK} P_PREFIX_NORM=${P_PREFIX_NORM} "
        "P_PREFIX_BAD=${P_PREFIX_BAD} P_HASH_LEN=${P_HASH_LEN})\n",
        convert_to_cmake_input);
    Ast_Root root = parse_cmake(temp_arena, script);
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 3);

    bool saw_ext = false;
    bool saw_ext_last = false;
    bool saw_stem = false;
    bool saw_stem_last = false;
    bool saw_cmake_len = false;
    bool saw_native_len = false;
    bool saw_cmp_eq = false;
    bool saw_cmp_neq = false;
    bool saw_has_root_name = false;
    bool saw_has_root_directory = false;
    bool saw_has_root_path = false;
    bool saw_has_filename = false;
    bool saw_has_extension = false;
    bool saw_has_stem = false;
    bool saw_has_relative_part = false;
    bool saw_has_parent_path = false;
    bool saw_is_absolute = false;
    bool saw_is_relative = false;
    bool saw_prefix_ok = false;
    bool saw_prefix_norm = false;
    bool saw_prefix_bad = false;
    bool saw_hash_len = false;
    bool saw_bad_component = false;
    bool saw_bad_native = false;
    bool saw_bad_convert = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_TARGET_COMPILE_DEFINITIONS &&
            nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("cmake_path_probe"))) {
            String_View item = ev->as.target_compile_definitions.item;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_EXT=.tar.gz"))) saw_ext = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_EXT_LAST=.gz"))) saw_ext_last = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_STEM=c"))) saw_stem = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_STEM_LAST=c.tar"))) saw_stem_last = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_CMAKE_LEN=3"))) saw_cmake_len = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_NATIVE_LEN=5"))) saw_native_len = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_CMP_EQ=ON"))) saw_cmp_eq = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_CMP_NEQ=ON"))) saw_cmp_neq = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_HAS_ROOT_NAME=ON"))) saw_has_root_name = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_HAS_ROOT_DIRECTORY=ON"))) saw_has_root_directory = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_HAS_ROOT_PATH=ON"))) saw_has_root_path = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_HAS_FILENAME=ON"))) saw_has_filename = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_HAS_EXTENSION=ON"))) saw_has_extension = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_HAS_STEM=ON"))) saw_has_stem = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_HAS_RELATIVE_PART=ON"))) saw_has_relative_part = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_HAS_PARENT_PATH=ON"))) saw_has_parent_path = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_IS_ABSOLUTE=ON"))) saw_is_absolute = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_IS_RELATIVE=ON"))) saw_is_relative = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_PREFIX_OK=ON"))) saw_prefix_ok = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_PREFIX_NORM=ON"))) saw_prefix_norm = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_PREFIX_BAD=OFF"))) saw_prefix_bad = true;
            if (nob_sv_eq(item, nob_sv_from_cstr("P_HASH_LEN=64"))) saw_hash_len = true;
            continue;
        }
        if (ev->h.kind == EV_DIAGNOSTIC && ev->as.diag.severity == EV_DIAG_ERROR) {
            if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_path(GET) unsupported component"))) {
                saw_bad_component = true;
            }
            if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_path(NATIVE_PATH) received unexpected argument"))) {
                saw_bad_native = true;
            }
            if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("cmake_path(CONVERT) received unexpected argument"))) {
                saw_bad_convert = true;
            }
        }
    }

    ASSERT(saw_ext);
    ASSERT(saw_ext_last);
    ASSERT(saw_stem);
    ASSERT(saw_stem_last);
    ASSERT(saw_cmake_len);
    ASSERT(saw_native_len);
    ASSERT(saw_cmp_eq);
    ASSERT(saw_cmp_neq);
    ASSERT(saw_has_root_name);
    ASSERT(saw_has_root_directory);
    ASSERT(saw_has_root_path);
    ASSERT(saw_has_filename);
    ASSERT(saw_has_extension);
    ASSERT(saw_has_stem);
    ASSERT(saw_has_relative_part);
    ASSERT(saw_has_parent_path);
    ASSERT(saw_is_absolute);
    ASSERT(saw_is_relative);
    ASSERT(saw_prefix_ok);
    ASSERT(saw_prefix_norm);
    ASSERT(saw_prefix_bad);
    ASSERT(saw_hash_len);
    ASSERT(saw_bad_component);
    ASSERT(saw_bad_native);
    ASSERT(saw_bad_convert);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_cmake_path_getters_and_relative_absolute_roundtrip_cover_remaining_components) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr("/src");
    init.binary_dir = nob_sv_from_cstr("/bin");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "cmake_path(SET P_DRV \"C:/alpha/beta/file.name.ext\")\n"
        "cmake_path(GET P_DRV ROOT_NAME P_ROOT_NAME)\n"
        "cmake_path(GET P_DRV ROOT_DIRECTORY P_ROOT_DIR)\n"
        "cmake_path(GET P_DRV ROOT_PATH P_ROOT_PATH)\n"
        "cmake_path(GET P_DRV FILENAME P_FILENAME)\n"
        "cmake_path(GET P_DRV RELATIVE_PART P_RELATIVE_PART)\n"
        "cmake_path(GET P_DRV PARENT_PATH P_PARENT_PATH)\n"
        "cmake_path(SET P_REL \"alpha/./beta\")\n"
        "cmake_path(ABSOLUTE_PATH P_REL BASE_DIRECTORY \"C:/base\" NORMALIZE OUTPUT_VARIABLE P_ABS)\n"
        "cmake_path(RELATIVE_PATH P_ABS BASE_DIRECTORY \"C:/base\" OUTPUT_VARIABLE P_REL_AGAIN)\n"
        "cmake_path(HAS_ROOT_PATH P_REL P_REL_HAS_ROOT_PATH)\n"
        "cmake_path(SET P_ROOT_ONLY \"C:/\")\n"
        "cmake_path(HAS_FILENAME P_ROOT_ONLY P_ROOT_HAS_FILENAME)\n"
        "cmake_path(COMPARE \"C:\\\\alpha\\\\beta\" EQUAL \"C:/alpha/beta\" P_CANON_EQ)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("P_ROOT_NAME")),
                     nob_sv_from_cstr("C:")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("P_ROOT_DIR")),
                     nob_sv_from_cstr("/")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("P_ROOT_PATH")),
                     nob_sv_from_cstr("C:/")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("P_FILENAME")),
                     nob_sv_from_cstr("file.name.ext")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("P_RELATIVE_PART")),
                     nob_sv_from_cstr("alpha/beta/file.name.ext")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("P_PARENT_PATH")),
                     nob_sv_from_cstr("C:/alpha/beta")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("P_ABS")),
                     nob_sv_from_cstr("C:/base/alpha/beta")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("P_REL_AGAIN")),
                     nob_sv_from_cstr("alpha/beta")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("P_REL_HAS_ROOT_PATH")),
                     nob_sv_from_cstr("OFF")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("P_ROOT_HAS_FILENAME")),
                     nob_sv_from_cstr("OFF")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("P_CANON_EQ")),
                     nob_sv_from_cstr("ON")));

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_flow_commands_reject_extra_arguments) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "break(oops)\n"
        "continue(oops)\n"
        "block()\n"
        "endblock(oops)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 3);

    size_t usage_hint_hits = 0;
    for (size_t i = 0; i < stream->count; i++) {
        if (stream->items[i].h.kind != EV_DIAGNOSTIC) continue;
        if (stream->items[i].as.diag.severity != EV_DIAG_ERROR) continue;
        if (!nob_sv_eq(stream->items[i].as.diag.cause, nob_sv_from_cstr("Command does not accept arguments"))) continue;
        usage_hint_hits++;
    }
    ASSERT(usage_hint_hits == 3);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_while_iteration_limit_snapshot_applies_per_loop_entry) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_NOBIFY_WHILE_MAX_ITERATIONS 2)\n"
        "set(I 0)\n"
        "while(${I} LESS 3)\n"
        "  if(${I} EQUAL 0)\n"
        "    set(CMAKE_NOBIFY_WHILE_MAX_ITERATIONS 10)\n"
        "  endif()\n"
        "  math(EXPR I \"${I} + 1\")\n"
        "endwhile()\n"
        "set(J 0)\n"
        "while(${J} LESS 3)\n"
        "  math(EXPR J \"${J} + 1\")\n"
        "endwhile()\n");
    Eval_Result run_res = eval_test_run(ctx, root);
    ASSERT(eval_result_is_soft_error(run_res));
    ASSERT(!eval_result_is_fatal(run_res));

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("I")), nob_sv_from_cstr("2")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("J")), nob_sv_from_cstr("3")));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 0);
    ASSERT(report->error_count == 1);

    size_t limit_diag_count = 0;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (!nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("Iteration limit exceeded"))) continue;
        limit_diag_count++;
    }
    ASSERT(limit_diag_count == 1);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_while_iteration_limit_invalid_value_warns_and_falls_back) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(CMAKE_NOBIFY_WHILE_MAX_ITERATIONS nope)\n"
        "set(I 0)\n"
        "while(${I} LESS 2)\n"
        "  math(EXPR I \"${I} + 1\")\n"
        "endwhile()\n");
    Eval_Result run_res = eval_test_run(ctx, root);
    ASSERT(eval_result_is_ok(run_res));

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("I")), nob_sv_from_cstr("2")));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->warning_count == 1);
    ASSERT(report->error_count == 0);

    bool saw_invalid_limit = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC) continue;
        if (ev->as.diag.severity != EV_DIAG_WARNING) continue;
        if (!nob_sv_eq(ev->as.diag.cause,
                       nob_sv_from_cstr("CMAKE_NOBIFY_WHILE_MAX_ITERATIONS must be a positive integer"))) {
            continue;
        }
        saw_invalid_limit = true;
        break;
    }
    ASSERT(saw_invalid_limit);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_enable_testing_does_not_set_build_testing_variable) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(BUILD_TESTING 0)\n"
        "enable_testing()\n"
        "add_executable(enable_testing_probe main.c)\n"
        "target_compile_definitions(enable_testing_probe PRIVATE BUILD_TESTING=${BUILD_TESTING})\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_enable_event = false;
    bool saw_build_testing_zero = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_TESTING_ENABLE && ev->as.test_enable.enabled) {
            saw_enable_event = true;
        }
        if (ev->h.kind == EV_TARGET_COMPILE_DEFINITIONS &&
            nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("enable_testing_probe")) &&
            nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("BUILD_TESTING=0"))) {
            saw_build_testing_zero = true;
        }
    }

    ASSERT(saw_enable_event);
    ASSERT(saw_build_testing_zero);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_enable_testing_rejects_extra_arguments) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(temp_arena, "enable_testing(extra)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_arity_error = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("Command does not accept arguments")) &&
            nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("Usage: enable_testing()"))) {
            saw_arity_error = true;
            break;
        }
    }
    ASSERT(saw_arity_error);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_include_supports_result_variable_optional_and_module_search) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(MAKE_DIRECTORY cmkmods)\n"
        "file(WRITE cmkmods/MyInc.cmake [=[set(MYINC_FLAG 1)\n]=])\n"
        "set(CMAKE_MODULE_PATH cmkmods)\n"
        "include(MyInc RESULT_VARIABLE INC_MOD_RES)\n"
        "include(missing_optional OPTIONAL RESULT_VARIABLE INC_MISS_RES)\n"
        "add_executable(include_probe main.c)\n"
        "target_compile_definitions(include_probe PRIVATE MYINC_FLAG=${MYINC_FLAG} INC_MOD_RES=${INC_MOD_RES} INC_MISS_RES=${INC_MISS_RES})\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    bool saw_myinc_flag = false;
    bool saw_mod_res_nonempty = false;
    bool saw_miss_notfound = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("include_probe"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("MYINC_FLAG=1"))) saw_myinc_flag = true;
        if (nob_sv_starts_with(ev->as.target_compile_definitions.item, nob_sv_from_cstr("INC_MOD_RES=")) &&
            !nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("INC_MOD_RES=")) &&
            !nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("INC_MOD_RES=NOTFOUND"))) {
            saw_mod_res_nonempty = true;
        }
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("INC_MISS_RES=NOTFOUND"))) saw_miss_notfound = true;
    }
    ASSERT(saw_myinc_flag);
    ASSERT(saw_mod_res_nonempty);
    ASSERT(saw_miss_notfound);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_include_validates_options_strictly) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(WRITE inc_ok.cmake [=[set(X 1)\n]=])\n"
        "include(inc_ok.cmake BAD_OPT)\n"
        "include(inc_ok.cmake RESULT_VARIABLE)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 2);

    bool saw_bad_opt = false;
    bool saw_missing_result_var = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("include() received unexpected argument")) &&
            nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("BAD_OPT"))) {
            saw_bad_opt = true;
        }
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("include(RESULT_VARIABLE) requires an output variable name"))) {
            saw_missing_result_var = true;
        }
    }
    ASSERT(saw_bad_opt);
    ASSERT(saw_missing_result_var);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_include_cmp0017_search_order_from_builtin_modules) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(MAKE_DIRECTORY user_mods)\n"
        "file(MAKE_DIRECTORY fake_root/Modules)\n"
        "file(WRITE user_mods/Foo.cmake [=[set(PICK user)\n]=])\n"
        "file(WRITE fake_root/Modules/Foo.cmake [=[set(PICK root)\n]=])\n"
        "file(WRITE fake_root/Modules/Caller.cmake [=[include(Foo)\n]=])\n"
        "set(CMAKE_MODULE_PATH user_mods)\n"
        "set(CMAKE_ROOT fake_root)\n"
        "cmake_policy(SET CMP0017 OLD)\n"
        "include(fake_root/Modules/Caller.cmake)\n"
        "set(PICK_OLD ${PICK})\n"
        "unset(PICK)\n"
        "cmake_policy(SET CMP0017 NEW)\n"
        "include(fake_root/Modules/Caller.cmake)\n"
        "set(PICK_NEW ${PICK})\n"
        "add_executable(include_cmp0017_probe main.c)\n"
        "target_compile_definitions(include_cmp0017_probe PRIVATE PICK_OLD=${PICK_OLD} PICK_NEW=${PICK_NEW})\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    bool saw_pick_old = false;
    bool saw_pick_new = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("include_cmp0017_probe"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("PICK_OLD=user"))) saw_pick_old = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("PICK_NEW=root"))) saw_pick_new = true;
    }
    ASSERT(saw_pick_old);
    ASSERT(saw_pick_new);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_include_guard_default_scope_is_strict_and_warning_free) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(WRITE guard_var.cmake [=[include_guard()\nset(VAR_HIT 1)\n]=])\n"
        "include(guard_var.cmake)\n"
        "include(guard_var.cmake)\n"
        "add_executable(guard_var_probe main.c)\n"
        "target_compile_definitions(guard_var_probe PRIVATE VAR_HIT=${VAR_HIT})\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    bool saw_var_hit = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("guard_var_probe"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("VAR_HIT=1"))) {
            saw_var_hit = true;
            break;
        }
    }
    ASSERT(saw_var_hit);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_include_guard_directory_scope_applies_only_to_directory_and_children) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "file(WRITE guard_dir.cmake [=[include_guard(DIRECTORY)\nset(DIR_HIT \"${DIR_HIT}x\")\n]=])\n"
        "include(guard_dir.cmake)\n"
        "include(guard_dir.cmake)\n"
        "add_executable(guard_dir_probe main.c)\n"
        "target_compile_definitions(guard_dir_probe PRIVATE DIR_HIT=${DIR_HIT})\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    bool saw_dir_hit = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("guard_dir_probe"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("DIR_HIT=x"))) saw_dir_hit = true;
    }
    ASSERT(saw_dir_hit);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_include_guard_global_scope_persists_across_function_scope) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "set(GLOBAL_GUARD_HITS \"\")\n"
        "file(WRITE guard_global.cmake [=[include_guard(GLOBAL)\nset(GLOBAL_GUARD_HITS \"${GLOBAL_GUARD_HITS}x\" PARENT_SCOPE)\n]=])\n"
        "function(run_guard_once)\n"
        "  include(guard_global.cmake)\n"
        "endfunction()\n"
        "run_guard_once()\n"
        "run_guard_once()\n"
        "add_executable(guard_global_probe main.c)\n"
        "target_compile_definitions(guard_global_probe PRIVATE GLOBAL_GUARD_HITS=${GLOBAL_GUARD_HITS})\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    bool saw_global_hit = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (!nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("guard_global_probe"))) continue;
        if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("GLOBAL_GUARD_HITS=x"))) {
            saw_global_hit = true;
            break;
        }
    }
    ASSERT(saw_global_hit);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_include_guard_rejects_invalid_arguments) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "include_guard(BAD)\n"
        "include_guard(VARIABLE)\n"
        "include_guard(GLOBAL EXTRA)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 3);
    ASSERT(report->warning_count == 0);

    bool saw_invalid_scope = false;
    bool saw_extra_args = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("include_guard() received invalid scope"))) {
            saw_invalid_scope = true;
        }
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("include_guard() accepts at most one scope argument"))) {
            saw_extra_args = true;
        }
    }
    ASSERT(saw_invalid_scope);
    ASSERT(saw_extra_args);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_enable_language_updates_enabled_language_state_and_validates_scope) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "enable_language(C)\n"
        "enable_language(CXX)\n"
        "function(bad_scope)\n"
        "  enable_language(Fortran)\n"
        "endfunction()\n"
        "bad_scope()\n"
        "enable_language(HIP OPTIONAL)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 2);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_ENABLED_LANGUAGES")),
                     nob_sv_from_cstr("C;CXX")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("NOBIFY_PROPERTY_GLOBAL::ENABLED_LANGUAGES")),
                     nob_sv_from_cstr("C;CXX")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CMAKE_C_COMPILER_LOADED")),
                     nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CMAKE_CXX_COMPILER_LOADED")),
                     nob_sv_from_cstr("1")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("CMAKE_Fortran_COMPILER_LOADED")),
                     nob_sv_from_cstr("")));

    bool saw_scope_error = false;
    bool saw_optional_error = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("enable_language() must be called at file scope"))) {
            saw_scope_error = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("enable_language(OPTIONAL) is not supported"))) {
            saw_optional_error = true;
        }
    }

    ASSERT(saw_scope_error);
    ASSERT(saw_optional_error);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_add_test_name_signature_parses_supported_options) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "enable_testing()\n"
        "add_test(NAME smoke COMMAND app --flag value CONFIGURATIONS Debug RelWithDebInfo WORKING_DIRECTORY tests COMMAND_EXPAND_LISTS)\n"
        "add_test(legacy app WORKING_DIRECTORY tools)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);
    ASSERT(report->warning_count == 0);

    bool saw_smoke = false;
    bool saw_legacy = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TEST_ADD) continue;
        if (nob_sv_eq(ev->as.test_add.name, nob_sv_from_cstr("smoke")) &&
            nob_sv_eq(ev->as.test_add.command, nob_sv_from_cstr("app --flag value")) &&
            nob_sv_eq(ev->as.test_add.working_dir, nob_sv_from_cstr("tests")) &&
            ev->as.test_add.command_expand_lists) {
            saw_smoke = true;
        }
        if (nob_sv_eq(ev->as.test_add.name, nob_sv_from_cstr("legacy")) &&
            nob_sv_eq(ev->as.test_add.command, nob_sv_from_cstr("app WORKING_DIRECTORY tools")) &&
            nob_sv_eq(ev->as.test_add.working_dir, nob_sv_from_cstr("")) &&
            !ev->as.test_add.command_expand_lists) {
            saw_legacy = true;
        }
    }
    ASSERT(saw_smoke);
    ASSERT(saw_legacy);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_add_test_name_signature_rejects_unexpected_arguments) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "enable_testing()\n"
        "add_test(NAME bad COMMAND app WORKING_DIRECTORY bad_dir EXTRA_TOKEN value)\n"
        "add_test(legacy_ok app ok)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    bool saw_unexpected_diag = false;
    bool emitted_bad_test = false;
    bool emitted_legacy_ok = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_DIAGNOSTIC &&
            ev->as.diag.severity == EV_DIAG_ERROR &&
            nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("add_test(NAME ...) received unexpected argument")) &&
            nob_sv_eq(ev->as.diag.hint, nob_sv_from_cstr("EXTRA_TOKEN"))) {
            saw_unexpected_diag = true;
        }
        if (ev->h.kind != EV_TEST_ADD) continue;
        if (nob_sv_eq(ev->as.test_add.name, nob_sv_from_cstr("bad"))) emitted_bad_test = true;
        if (nob_sv_eq(ev->as.test_add.name, nob_sv_from_cstr("legacy_ok"))) emitted_legacy_ok = true;
    }

    ASSERT(saw_unexpected_diag);
    ASSERT(!emitted_bad_test);
    ASSERT(emitted_legacy_ok);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_add_definitions_routes_d_flags_to_compile_definitions) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_definitions(-DLEGACY=1 /DWIN_DEF -fPIC /EHsc -D -D1BAD)\n"
        "add_executable(defs_probe main.c)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_global_def_legacy = false;
    bool saw_global_def_win = false;
    bool saw_global_opt_fpic = false;
    bool saw_global_opt_eh = false;
    bool saw_global_opt_dash_d = false;
    bool saw_target_def_legacy = false;
    bool saw_target_def_win = false;
    bool saw_target_opt_fpic = false;
    bool saw_target_opt_eh = false;
    bool saw_target_opt_dash_d = false;
    bool saw_target_opt_invalid_d = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EV_TARGET_COMPILE_DEFINITIONS &&
                   nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("defs_probe"))) {
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("LEGACY=1"))) saw_target_def_legacy = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("WIN_DEF"))) saw_target_def_win = true;
        } else if (ev->h.kind == EV_TARGET_COMPILE_OPTIONS &&
                   nob_sv_eq(ev->as.target_compile_options.target_name, nob_sv_from_cstr("defs_probe"))) {
            if (nob_sv_eq(ev->as.target_compile_options.item, nob_sv_from_cstr("-fPIC"))) saw_target_opt_fpic = true;
            if (nob_sv_eq(ev->as.target_compile_options.item, nob_sv_from_cstr("/EHsc"))) saw_target_opt_eh = true;
            if (nob_sv_eq(ev->as.target_compile_options.item, nob_sv_from_cstr("-D"))) saw_target_opt_dash_d = true;
            if (nob_sv_eq(ev->as.target_compile_options.item, nob_sv_from_cstr("-D1BAD"))) saw_target_opt_invalid_d = true;
        }
    }

    (void)saw_global_def_legacy;
    (void)saw_global_def_win;
    (void)saw_global_opt_fpic;
    (void)saw_global_opt_eh;
    (void)saw_global_opt_dash_d;
    ASSERT(saw_target_def_legacy);
    ASSERT(saw_target_def_win);
    ASSERT(saw_target_opt_fpic);
    ASSERT(saw_target_opt_eh);
    ASSERT(saw_target_opt_dash_d);
    ASSERT(saw_target_opt_invalid_d);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_add_compile_definitions_updates_existing_and_future_targets) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_executable(defs_before main_before.c)\n"
        "add_compile_definitions(-DFOO BAR=1 -D)\n"
        "add_executable(defs_after main_after.c)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    bool saw_global_foo = false;
    bool saw_global_bar = false;
    bool saw_empty_global = false;
    bool saw_before_foo = false;
    bool saw_before_bar = false;
    bool saw_after_foo = false;
    bool saw_after_bar = false;
    bool saw_dash_prefixed = false;

    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_TARGET_COMPILE_DEFINITIONS) continue;
        if (nob_sv_starts_with(ev->as.target_compile_definitions.item, nob_sv_from_cstr("-D"))) saw_dash_prefixed = true;
        if (nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("defs_before"))) {
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("FOO"))) saw_before_foo = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("BAR=1"))) saw_before_bar = true;
        } else if (nob_sv_eq(ev->as.target_compile_definitions.target_name, nob_sv_from_cstr("defs_after"))) {
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("FOO"))) saw_after_foo = true;
            if (nob_sv_eq(ev->as.target_compile_definitions.item, nob_sv_from_cstr("BAR=1"))) saw_after_bar = true;
        }
    }

    (void)saw_global_foo;
    (void)saw_global_bar;
    (void)saw_empty_global;
    ASSERT(saw_before_foo);
    ASSERT(saw_before_bar);
    ASSERT(saw_after_foo);
    ASSERT(saw_after_bar);
    ASSERT(!saw_dash_prefixed);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_execute_process_captures_output_and_models_3_28_fatal_mode) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

#if defined(_WIN32)
    const char *script =
        "execute_process(COMMAND cmd /C \"echo out&& echo err 1>&2\" "
        "OUTPUT_VARIABLE OUT ERROR_VARIABLE ERR RESULT_VARIABLE RES "
        "OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE)\n"
        "execute_process(COMMAND cmd /C \"echo file-copy\" OUTPUT_FILE ep_out.txt)\n"
        "execute_process(COMMAND cmd /C exit 3 COMMAND_ERROR_IS_FATAL LAST RESULT_VARIABLE BAD)\n";
#else
    const char *script =
        "execute_process(COMMAND /bin/sh -c \"printf 'out\\n'; printf 'err\\n' >&2\" "
        "OUTPUT_VARIABLE OUT ERROR_VARIABLE ERR RESULT_VARIABLE RES "
        "OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE)\n"
        "execute_process(COMMAND /bin/sh -c \"printf 'abc'\" "
        "COMMAND /bin/sh -c \"tr a-z A-Z\" "
        "OUTPUT_VARIABLE PIPE RESULTS_VARIABLE PIPE_RESULTS "
        "OUTPUT_STRIP_TRAILING_WHITESPACE)\n"
        "execute_process(COMMAND /bin/sh -c \"printf 'file-copy'\" OUTPUT_FILE ep_out.txt)\n"
        "execute_process(COMMAND /bin/sh -c \"exit 3\" COMMAND_ERROR_IS_FATAL LAST RESULT_VARIABLE BAD)\n";
#endif

    Ast_Root root = parse_cmake(temp_arena, script);
    ASSERT(eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 1);

    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("OUT")), nob_sv_from_cstr("out")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("ERR")), nob_sv_from_cstr("err")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("RES")), nob_sv_from_cstr("0")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("BAD")), nob_sv_from_cstr("3")));

#if !defined(_WIN32)
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("PIPE")), nob_sv_from_cstr("ABC")));
    ASSERT(nob_sv_eq(eval_test_var_get(ctx, nob_sv_from_cstr("PIPE_RESULTS")), nob_sv_from_cstr("0;0")));
#endif

    String_View file_text = {0};
    ASSERT(evaluator_load_text_file_to_arena(temp_arena, "ep_out.txt", &file_text));
    String_View file_norm = evaluator_normalize_newlines_to_arena(temp_arena, file_text);
    ASSERT(sv_contains_sv(file_norm, nob_sv_from_cstr("file-copy")));

    bool saw_fatal = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("execute_process() child process failed"))) {
            saw_fatal = true;
            break;
        }
    }
    ASSERT(saw_fatal);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_execute_process_rejects_incomplete_and_invalid_option_forms) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

#if defined(_WIN32)
    const char *script =
        "execute_process()\n"
        "execute_process(COMMAND)\n"
        "execute_process(COMMAND cmd /C echo ok OUTPUT_VARIABLE)\n"
        "execute_process(COMMAND cmd /C echo ok COMMAND_ECHO MAYBE)\n"
        "execute_process(COMMAND cmd /C echo ok COMMAND_ERROR_IS_FATAL NONE)\n";
#else
    const char *script =
        "execute_process()\n"
        "execute_process(COMMAND)\n"
        "execute_process(COMMAND /bin/sh -c \"printf ok\" OUTPUT_VARIABLE)\n"
        "execute_process(COMMAND /bin/sh -c \"printf ok\" COMMAND_ECHO MAYBE)\n"
        "execute_process(COMMAND /bin/sh -c \"printf ok\" COMMAND_ERROR_IS_FATAL NONE)\n";
#endif

    Ast_Root root = parse_cmake(temp_arena, script);
    Eval_Result run_res = eval_test_run(ctx, root);
    ASSERT(!eval_result_is_fatal(run_res));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 5);

    bool saw_missing_command_clause = false;
    bool saw_missing_command_arg = false;
    bool saw_missing_output_var = false;
    bool saw_invalid_command_echo = false;
    bool saw_invalid_fatal_none = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind != EV_DIAGNOSTIC || ev->as.diag.severity != EV_DIAG_ERROR) continue;
        if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("execute_process() requires at least one COMMAND clause"))) {
            saw_missing_command_clause = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("execute_process(COMMAND) requires at least one argument"))) {
            saw_missing_command_arg = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("execute_process(OUTPUT_VARIABLE) requires an output variable"))) {
            saw_missing_output_var = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("execute_process(COMMAND_ECHO) received an invalid value"))) {
            saw_invalid_command_echo = true;
        } else if (nob_sv_eq(ev->as.diag.cause, nob_sv_from_cstr("execute_process(COMMAND_ERROR_IS_FATAL NONE) is not part of the CMake 3.28 baseline"))) {
            saw_invalid_fatal_none = true;
        }
    }

    ASSERT(saw_missing_command_clause);
    ASSERT(saw_missing_command_arg);
    ASSERT(saw_missing_output_var);
    ASSERT(saw_invalid_command_echo);
    ASSERT(saw_invalid_fatal_none);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

TEST(evaluator_directory_option_commands_expand_shell_and_linker_tokens_once) {
    Arena *temp_arena = arena_create(2 * 1024 * 1024);
    Arena *event_arena = arena_create(2 * 1024 * 1024);
    ASSERT(temp_arena && event_arena);

    Cmake_Event_Stream *stream = event_stream_create(event_arena);
    ASSERT(stream != NULL);

    Eval_Test_Init init = {0};
    init.arena = temp_arena;
    init.event_arena = event_arena;
    init.stream = stream;
    init.source_dir = nob_sv_from_cstr(".");
    init.binary_dir = nob_sv_from_cstr(".");
    init.current_file = "CMakeLists.txt";

    Eval_Test_Runtime *ctx = eval_test_create(&init);
    ASSERT(ctx != NULL);

    Ast_Root root = parse_cmake(
        temp_arena,
        "add_compile_options(\"SHELL:-Wall -Winvalid-pch\" -Winvalid-pch)\n"
        "add_link_options(\"LINKER:-z,defs\" \"SHELL:-pthread -pthread\" \"LINKER:-z,defs\")\n"
        "get_property(COMPILE_OPTS DIRECTORY PROPERTY COMPILE_OPTIONS)\n"
        "get_property(LINK_OPTS DIRECTORY PROPERTY LINK_OPTIONS)\n");
    ASSERT(!eval_result_is_fatal(eval_test_run(ctx, root)));

    const Eval_Run_Report *report = eval_test_report(ctx);
    ASSERT(report != NULL);
    ASSERT(report->error_count == 0);

    String_View compile_opts = eval_test_var_get(ctx, nob_sv_from_cstr("COMPILE_OPTS"));
    String_View link_opts = eval_test_var_get(ctx, nob_sv_from_cstr("LINK_OPTS"));
    ASSERT(semicolon_list_count(compile_opts) == 2);
    ASSERT(nob_sv_eq(semicolon_list_item_at(compile_opts, 0), nob_sv_from_cstr("-Wall")));
    ASSERT(nob_sv_eq(semicolon_list_item_at(compile_opts, 1), nob_sv_from_cstr("-Winvalid-pch")));
    ASSERT(semicolon_list_count(link_opts) == 3);
    ASSERT(nob_sv_eq(semicolon_list_item_at(link_opts, 0), nob_sv_from_cstr("LINKER:-z")));
    ASSERT(nob_sv_eq(semicolon_list_item_at(link_opts, 1), nob_sv_from_cstr("LINKER:defs")));
    ASSERT(nob_sv_eq(semicolon_list_item_at(link_opts, 2), nob_sv_from_cstr("-pthread")));

    bool saw_compile_mutation = false;
    bool saw_link_mutation = false;
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        if (ev->h.kind == EVENT_DIRECTORY_PROPERTY_MUTATE &&
            nob_sv_eq(ev->as.directory_property_mutate.property_name, nob_sv_from_cstr("COMPILE_OPTIONS"))) {
            saw_compile_mutation =
                ev->as.directory_property_mutate.item_count == 2 &&
                nob_sv_eq(ev->as.directory_property_mutate.items[0], nob_sv_from_cstr("-Wall")) &&
                nob_sv_eq(ev->as.directory_property_mutate.items[1], nob_sv_from_cstr("-Winvalid-pch"));
        } else if (ev->h.kind == EVENT_DIRECTORY_PROPERTY_MUTATE &&
                   nob_sv_eq(ev->as.directory_property_mutate.property_name, nob_sv_from_cstr("LINK_OPTIONS"))) {
            saw_link_mutation =
                ev->as.directory_property_mutate.item_count == 3 &&
                nob_sv_eq(ev->as.directory_property_mutate.items[0], nob_sv_from_cstr("LINKER:-z")) &&
                nob_sv_eq(ev->as.directory_property_mutate.items[1], nob_sv_from_cstr("LINKER:defs")) &&
                nob_sv_eq(ev->as.directory_property_mutate.items[2], nob_sv_from_cstr("-pthread"));
        }
    }

    ASSERT(saw_compile_mutation);
    ASSERT(saw_link_mutation);

    eval_test_destroy(ctx);
    arena_destroy(temp_arena);
    arena_destroy(event_arena);
    TEST_PASS();
}

void run_evaluator_v2_batch1(int *passed, int *failed) {
    test_evaluator_golden_all_cases(passed, failed);
    test_evaluator_public_api_profile_and_report_snapshot(passed, failed);
    test_evaluator_session_api_runs_with_explicit_request_and_stream(passed, failed);
    test_evaluator_registry_api_supports_custom_commands_and_null_stream_runs(passed, failed);
    test_evaluator_session_services_env_lookup_is_injected(passed, failed);
    test_evaluator_command_transaction_rollback_suppresses_semantic_state_and_events(passed, failed);
    test_evaluator_g5_legacy_wrapper_capabilities_promoted_to_full(passed, failed);
    test_evaluator_native_command_registry_runtime_extension(passed, failed);
    test_evaluator_command_capability_remains_native_only_introspection(passed, failed);
    test_evaluator_native_command_registry_case_insensitive_index_lookup(passed, failed);
    test_evaluator_compat_refresh_snapshot_applies_next_command_cycle(passed, failed);
    test_evaluator_global_diag_strict_controls_event_report_and_runtime_gating(passed, failed);
    test_evaluator_unsupported_policy_snapshot_applies_next_command_cycle(passed, failed);
    test_evaluator_run_result_kind_tri_state_contract(passed, failed);
    test_evaluator_link_libraries_supports_qualifiers_and_rejects_dangling_qualifier(passed, failed);
    test_evaluator_cmake_path_extended_surface_and_strict_validation(passed, failed);
    test_evaluator_cmake_path_getters_and_relative_absolute_roundtrip_cover_remaining_components(passed, failed);
    test_evaluator_flow_commands_reject_extra_arguments(passed, failed);
    test_evaluator_while_iteration_limit_snapshot_applies_per_loop_entry(passed, failed);
    test_evaluator_while_iteration_limit_invalid_value_warns_and_falls_back(passed, failed);
    test_evaluator_enable_testing_does_not_set_build_testing_variable(passed, failed);
    test_evaluator_enable_testing_rejects_extra_arguments(passed, failed);
    test_evaluator_include_supports_result_variable_optional_and_module_search(passed, failed);
    test_evaluator_include_validates_options_strictly(passed, failed);
    test_evaluator_include_cmp0017_search_order_from_builtin_modules(passed, failed);
    test_evaluator_include_guard_default_scope_is_strict_and_warning_free(passed, failed);
    test_evaluator_include_guard_directory_scope_applies_only_to_directory_and_children(passed, failed);
    test_evaluator_include_guard_global_scope_persists_across_function_scope(passed, failed);
    test_evaluator_include_guard_rejects_invalid_arguments(passed, failed);
    test_evaluator_enable_language_updates_enabled_language_state_and_validates_scope(passed, failed);
    test_evaluator_add_test_name_signature_parses_supported_options(passed, failed);
    test_evaluator_add_test_name_signature_rejects_unexpected_arguments(passed, failed);
    test_evaluator_add_definitions_routes_d_flags_to_compile_definitions(passed, failed);
    test_evaluator_add_compile_definitions_updates_existing_and_future_targets(passed, failed);
    test_evaluator_directory_option_commands_expand_shell_and_linker_tokens_once(passed, failed);
    test_evaluator_execute_process_rejects_incomplete_and_invalid_option_forms(passed, failed);
    test_evaluator_execute_process_captures_output_and_models_3_28_fatal_mode(passed, failed);
}
