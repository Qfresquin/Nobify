static void log_test_front_door_usage(const char *argv0) {
    nob_log(NOB_INFO,
            "Usage: %s test [smoke|<module>] [--verbose] [--asan|--ubsan|--msan|--san|--cov]",
            argv0);
    nob_log(NOB_INFO,
            "Usage: %s test clean [--force]",
            argv0);
    nob_log(NOB_INFO,
            "Usage: %s test tidy <all|module> [--verbose]",
            argv0);
    nob_log(NOB_INFO,
            "Usage: %s test watch <module|auto> [--verbose] [--asan|--ubsan|--msan|--san|--cov]",
            argv0);
    nob_log(NOB_INFO,
            "Supported modules: arena|lexer|parser|build-model|evaluator|evaluator-diff|"
            "evaluator-codegen-diff|evaluator-integration|pipeline|codegen|artifact-parity|"
            "artifact-parity-corpus");
    nob_log(NOB_INFO,
            "T6 fronts all human-facing test commands through `./build/nob test ...`. "
            "Default aggregate naming is `smoke`, watch output is compact/failure-first by default, "
            "and `--verbose` enables full roots/reroute detail. Use `%s test daemon start|stop [--force]|restart|status` "
            "for lifecycle control.",
            argv0);
}

static bool log_test_front_door_migration(const char *argv0,
                                          const char *legacy,
                                          const char *replacement) {
    nob_log(NOB_ERROR,
            "legacy command `%s` is no longer supported; use `%s` instead",
            legacy,
            replacement);
    log_test_front_door_usage(argv0);
    return false;
}

bool test_runner_parse_front_door(const char *argv0,
                                  int argc,
                                  char **argv,
                                  Test_Runner_Request *out_request) {
    const Test_Runner_Module_Def *module = NULL;
    const Test_Runner_Profile_Internal *profile = &TEST_RUNNER_PROFILES[TEST_RUNNER_PROFILE_FAST];
    const char *profile_flag = NULL;
    Test_Runner_Action action = TEST_RUNNER_ACTION_RUN_AGGREGATE;
    bool verbose = false;
    bool force = false;
    bool selector_seen = false;
    bool tidy_mode = false;
    bool tidy_target_seen = false;

    if (!out_request) return false;
    *out_request = (Test_Runner_Request){0};

    for (int i = 0; i < argc; ++i) {
        const char *arg = argv[i];
        const Test_Runner_Profile_Internal *candidate_profile = NULL;
        const char *legacy_tidy_module = NULL;

        if (cstr_equals(arg, "--verbose")) {
            verbose = true;
            continue;
        }

        if (cstr_equals(arg, "--force")) {
            if (force) {
                nob_log(NOB_ERROR, "duplicate test flag: %s", arg);
                log_test_front_door_usage(argv0);
                return false;
            }
            force = true;
            continue;
        }

        candidate_profile = find_test_profile_by_front_door_flag(arg);
        if (candidate_profile) {
            if (profile_flag) {
                nob_log(NOB_ERROR, "conflicting test profile flags: %s and %s", profile_flag, arg);
                log_test_front_door_usage(argv0);
                return false;
            }
            profile = candidate_profile;
            profile_flag = arg;
            continue;
        }

        if (cstr_equals(arg, "clean-tests")) {
            return log_test_front_door_migration(argv0, arg, "./build/nob test clean");
        }

        if (cstr_equals(arg, "clang-tidy-v2")) {
            return log_test_front_door_migration(argv0, arg, "./build/nob test tidy all");
        }

        if (starts_with(arg, "clang-tidy-")) {
            legacy_tidy_module = arg + strlen("clang-tidy-");
            if (test_runner_find_module_def_by_name(legacy_tidy_module)) {
                return log_test_front_door_migration(argv0,
                                                     arg,
                                                     nob_temp_sprintf("./build/nob test tidy %s", legacy_tidy_module));
            }
            return log_test_front_door_migration(argv0, arg, "./build/nob test tidy <module>");
        }

        if (starts_with(arg, "test-")) {
            return log_test_front_door_migration(argv0, arg, "./build/nob test smoke or ./build/nob test <module>");
        }

        if (arg[0] == '-') {
            nob_log(NOB_ERROR, "unexpected test flag: %s", arg);
            log_test_front_door_usage(argv0);
            return false;
        }

        if (cstr_equals(arg, "clean")) {
            if (selector_seen) {
                nob_log(NOB_ERROR, "multiple test selectors are not allowed");
                log_test_front_door_usage(argv0);
                return false;
            }
            selector_seen = true;
            action = TEST_RUNNER_ACTION_CLEAN;
            continue;
        }

        if (cstr_equals(arg, "tidy")) {
            if (selector_seen) {
                nob_log(NOB_ERROR, "multiple test selectors are not allowed");
                log_test_front_door_usage(argv0);
                return false;
            }
            selector_seen = true;
            tidy_mode = true;
            continue;
        }

        if (action == TEST_RUNNER_ACTION_CLEAN) {
            nob_log(NOB_ERROR, "`./build/nob test clean` does not accept extra selectors");
            log_test_front_door_usage(argv0);
            return false;
        }

        if (tidy_mode) {
            if (tidy_target_seen) {
                nob_log(NOB_ERROR, "multiple tidy selectors are not allowed");
                log_test_front_door_usage(argv0);
                return false;
            }
            if (cstr_equals(arg, "all")) {
                action = TEST_RUNNER_ACTION_RUN_TIDY_AGGREGATE;
                tidy_target_seen = true;
                continue;
            }
            module = test_runner_find_module_def_by_name(arg);
            if (!module) {
                nob_log(NOB_ERROR, "unknown tidy module: %s", arg);
                log_test_front_door_usage(argv0);
                return false;
            }
            action = TEST_RUNNER_ACTION_RUN_TIDY_MODULE;
            out_request->module_id = module->id;
            tidy_target_seen = true;
            continue;
        }

        if (cstr_equals(arg, "smoke")) {
            if (selector_seen) {
                nob_log(NOB_ERROR, "multiple test selectors are not allowed");
                log_test_front_door_usage(argv0);
                return false;
            }
            selector_seen = true;
            action = TEST_RUNNER_ACTION_RUN_AGGREGATE;
            continue;
        }

        if (selector_seen) {
            nob_log(NOB_ERROR, "multiple test selectors are not allowed");
            log_test_front_door_usage(argv0);
            return false;
        }

        module = test_runner_find_module_def_by_name(arg);
        if (!module) {
            nob_log(NOB_ERROR, "unknown test module: %s", arg);
            log_test_front_door_usage(argv0);
            return false;
        }
        action = TEST_RUNNER_ACTION_RUN_MODULE;
        out_request->module_id = module->id;
        selector_seen = true;
    }

    if (tidy_mode && !tidy_target_seen) {
        nob_log(NOB_ERROR, "`./build/nob test tidy` requires either `all` or a module name");
        log_test_front_door_usage(argv0);
        return false;
    }

    if (action == TEST_RUNNER_ACTION_CLEAN) {
        if (verbose || profile_flag) {
            nob_log(NOB_ERROR, "`./build/nob test clean` does not accept `--verbose` or profile flags");
            log_test_front_door_usage(argv0);
            return false;
        }
        out_request->action = action;
        out_request->force = force;
        return true;
    }

    if (action == TEST_RUNNER_ACTION_RUN_TIDY_AGGREGATE ||
        action == TEST_RUNNER_ACTION_RUN_TIDY_MODULE) {
        if (force) {
            nob_log(NOB_ERROR, "`./build/nob test tidy ...` does not accept `--force`");
            log_test_front_door_usage(argv0);
            return false;
        }
        if (profile_flag) {
            nob_log(NOB_ERROR, "`./build/nob test tidy ...` does not accept profile flags");
            log_test_front_door_usage(argv0);
            return false;
        }
        out_request->action = action;
        out_request->profile_id = TEST_RUNNER_PROFILE_DEFAULT;
        out_request->verbose = verbose;
        out_request->skip_preflight = false;
        return true;
    }

    if (force) {
        nob_log(NOB_ERROR, "`./build/nob test [smoke|<module>]` does not accept `--force`");
        log_test_front_door_usage(argv0);
        return false;
    }

    if (!profile_flag) {
        if (action == TEST_RUNNER_ACTION_RUN_MODULE && module) {
            profile = &TEST_RUNNER_PROFILES[module->default_local_profile];
        } else {
            profile = &TEST_RUNNER_PROFILES[TEST_RUNNER_PROFILE_FAST];
        }
    }

    out_request->action = action;
    out_request->profile_id = profile->def.id;
    out_request->verbose = verbose;
    out_request->skip_preflight = false;
    return true;
}

bool test_runner_parse_watch_front_door(const char *argv0,
                                        int argc,
                                        char **argv,
                                        Test_Runner_Watch_Request *out_request) {
    const Test_Runner_Module_Def *module = NULL;
    const Test_Runner_Profile_Internal *profile = NULL;
    const char *profile_flag = NULL;
    bool verbose = false;
    bool selection_seen = false;

    if (!out_request) return false;
    *out_request = (Test_Runner_Watch_Request){0};

    for (int i = 0; i < argc; ++i) {
        const char *arg = argv[i];
        const Test_Runner_Profile_Internal *candidate_profile = NULL;

        if (cstr_equals(arg, "--verbose")) {
            verbose = true;
            continue;
        }

        if (cstr_equals(arg, "--force")) {
            nob_log(NOB_ERROR, "watch mode does not support `--force`; use `./build/nob test daemon stop --force`");
            log_test_front_door_usage(argv0);
            return false;
        }

        candidate_profile = find_test_profile_by_front_door_flag(arg);
        if (candidate_profile) {
            if (profile_flag) {
                nob_log(NOB_ERROR, "conflicting test profile flags: %s and %s", profile_flag, arg);
                log_test_front_door_usage(argv0);
                return false;
            }
            profile = candidate_profile;
            profile_flag = arg;
            continue;
        }

        if (cstr_equals(arg, "clean-tests") || cstr_equals(arg, "clean")) {
            nob_log(NOB_ERROR, "watch mode does not support cleaning; use `./build/nob test clean`");
            log_test_front_door_usage(argv0);
            return false;
        }

        if (cstr_equals(arg, "clang-tidy-v2")) {
            nob_log(NOB_ERROR, "watch mode does not support tidy; use `./build/nob test tidy all`");
            log_test_front_door_usage(argv0);
            return false;
        }

        if (starts_with(arg, "clang-tidy-") || cstr_equals(arg, "tidy")) {
            nob_log(NOB_ERROR, "watch mode does not support tidy; use `./build/nob test tidy <module>`");
            log_test_front_door_usage(argv0);
            return false;
        }

        if (starts_with(arg, "test-")) {
            return log_test_front_door_migration(argv0, arg, "./build/nob test watch <module|auto>");
        }

        if (arg[0] == '-') {
            nob_log(NOB_ERROR, "unexpected test watch flag: %s", arg);
            log_test_front_door_usage(argv0);
            return false;
        }

        if (selection_seen) {
            nob_log(NOB_ERROR, "multiple watch selectors are not allowed");
            log_test_front_door_usage(argv0);
            return false;
        }

        if (cstr_equals(arg, "auto")) {
            out_request->mode = TEST_RUNNER_WATCH_MODE_AUTO;
            selection_seen = true;
            continue;
        }

        module = test_runner_find_module_def_by_name(arg);
        if (!module) {
            nob_log(NOB_ERROR, "unknown watch module: %s", arg);
            log_test_front_door_usage(argv0);
            return false;
        }
        out_request->mode = TEST_RUNNER_WATCH_MODE_MODULE;
        out_request->module_id = module->id;
        selection_seen = true;
    }

    if (!selection_seen) {
        nob_log(NOB_ERROR, "`./build/nob test watch` requires either a module name or `auto`");
        log_test_front_door_usage(argv0);
        return false;
    }

    if (profile) {
        out_request->profile_id = profile->def.id;
        out_request->profile_explicit = true;
    } else if (out_request->mode == TEST_RUNNER_WATCH_MODE_MODULE && module) {
        out_request->profile_id = module->default_local_profile;
        out_request->profile_explicit = false;
    } else {
        out_request->profile_id = TEST_RUNNER_PROFILE_FAST;
        out_request->profile_explicit = false;
    }

    out_request->verbose = verbose;
    return true;
}
