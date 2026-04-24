#include "test_runner_bootstrap.h"

#define NOB_REBUILD_URSELF(binary_path, source_path) \
    TEST_RUNNER_NOB_BOOTSTRAP_REBUILD(binary_path, source_path)
#define NOB_IMPLEMENTATION
#include "nob.h"

#include "test_daemon_client.h"
#include "test_runner_core.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// Caminhos relativos à raiz do projeto
static const char *APP_SRC = "src_v2/app/nobify.c";
static const char *APP_BIN = "build/nobify";
static const char *TEST_DAEMON_SRC = "src_v2/build/nob_testd.c";
static const char *TEST_DAEMON_BIN = "build/nob_testd";
static const char *SNAPSHOT_TOOL_SRC = "test_v2/artifact_parity/tools/update_snapshot.c";
static const char *SNAPSHOT_TOOL_BIN = "build/update_artifact_parity_snapshots";

static bool build_test_daemon_binary(void) {
    Nob_Cmd cmd = {0};
    bool ok = false;

    if (!nob_mkdir_if_not_exists("build")) return false;

    nob_cmd_append(&cmd, TEST_RUNNER_DAEMON_BOOTSTRAP_BUILD(TEST_DAEMON_BIN, TEST_DAEMON_SRC));
    ok = nob_cmd_run(&cmd);
    nob_cmd_free(cmd);
    return ok;
}

static bool ensure_test_daemon_binary_exists(void) {
    const char *inputs[] = {
        TEST_DAEMON_SRC,
        TEST_DAEMON_PROTOCOL_SOURCE,
        TEST_DAEMON_RUNTIME_SOURCE,
        TEST_RUNNER_DAEMON_BUILD_DEPS,
    };
    int rebuild = nob_needs_rebuild(TEST_DAEMON_BIN, inputs, NOB_ARRAY_LEN(inputs));
    if (rebuild < 0) return false;
    if (!rebuild) return true;
    nob_log(NOB_INFO, "bootstrapping %s", TEST_DAEMON_BIN);
    return build_test_daemon_binary();
}

static bool wait_for_test_daemon_ready(void) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (test_daemon_client_ping()) return true;
        usleep(100 * 1000);
    }
    return false;
}

static void format_duration_usec(uint64_t duration_usec, char *buffer, size_t buffer_size) {
    double value = 0.0;
    const char *unit = "ms";

    if (!buffer || buffer_size == 0) return;
    if (duration_usec >= 1000000ull) {
        value = (double)duration_usec / 1000000.0;
        unit = "s";
    } else {
        value = (double)duration_usec / 1000.0;
    }

    if (snprintf(buffer, buffer_size, "%.2f%s", value, unit) >= (int)buffer_size) {
        buffer[0] = '\0';
    }
}

static void log_multiline_status_block(const char *label, const char *text) {
    char buffer[TEST_RUNNER_FAILURE_SUMMARY_CAPACITY + 1] = {0};
    char *line = NULL;
    char *state = NULL;

    if (!label || !text || text[0] == '\0') return;
    if (snprintf(buffer, sizeof(buffer), "%s", text) >= (int)sizeof(buffer)) return;

    nob_log(NOB_INFO, "%s", label);
    for (line = strtok_r(buffer, "\n", &state); line; line = strtok_r(NULL, "\n", &state)) {
        nob_log(NOB_INFO, "  %s", line);
    }
}

static void log_test_daemon_status_details(const Test_Daemon_Status *status) {
    char duration[64] = {0};
    const Test_Daemon_Request_Metadata *active = NULL;

    if (!status) return;
    active = &status->active_request;

    nob_log(NOB_INFO,
            "test daemon status: %s (pid=%ld socket=%s)",
            test_daemon_state_name(status->state),
            (long)status->pid,
            status->socket_path[0] != '\0' ? status->socket_path : TEST_DAEMON_SOCKET_PATH);
    if (status->summary[0] != '\0') nob_log(NOB_INFO, "status summary: %s", status->summary);
    if (status->detail[0] != '\0') nob_log(NOB_INFO, "status detail: %s", status->detail);

    nob_log(NOB_INFO,
            "transport: reachable=%s socket=%s pid-file=%s pid-alive=%s",
            status->daemon_reachable ? "yes" : "no",
            status->socket_present ? "present" : "absent",
            status->pid_file_present ? "present" : "absent",
            status->pid_alive ? "yes" : "no");

    if (active->kind != TEST_DAEMON_REQUEST_NONE) {
        format_duration_usec(active->active_duration_usec, duration, sizeof(duration));
        nob_log(NOB_INFO,
                "active request: kind=%s policy=%s module=%s profile=%s case=%s duration=%s client=%s",
                test_daemon_request_kind_name((Test_Daemon_Request_Kind)active->kind),
                test_daemon_admission_policy_name((Test_Daemon_Admission_Policy)active->admission_policy),
                active->module_name[0] != '\0' ? active->module_name : "<none>",
                active->profile_name[0] != '\0' ? active->profile_name : "<none>",
                active->case_name[0] != '\0' ? active->case_name : "<all>",
                duration[0] != '\0' ? duration : "0ms",
                active->client_attached ? "attached" : "detached");
        nob_log(NOB_INFO,
                "active flags: cancel=%s rerun=%s drain=%s force-stop=%s kill-escalation=%s/%s request-id=%llu",
                active->pending_cancel ? "yes" : "no",
                active->pending_rerun ? "yes" : "no",
                active->pending_drain ? "yes" : "no",
                active->force_stop ? "yes" : "no",
                active->kill_escalation_armed ? "armed" : "idle",
                active->kill_escalation_sent ? "sent" : "not-sent",
                (unsigned long long)active->request_id);
    }

    nob_log(NOB_INFO,
            "cache telemetry: launcher=%s launcher-cache=%s preflight=%s",
            test_runner_launcher_kind_name(status->cache_launcher_kind),
            test_daemon_launcher_cache_reason_name(status->cache_launcher_reason),
            test_daemon_preflight_reason_name(status->cache_preflight_reason));

    if (status->preserved_workspace_path[0] != '\0') {
        nob_log(NOB_INFO, "last workspace: %s", status->preserved_workspace_path);
    }
    if (status->stdout_log_path[0] != '\0') {
        nob_log(NOB_INFO, "last stdout log: %s", status->stdout_log_path);
    }
    if (status->stderr_log_path[0] != '\0') {
        nob_log(NOB_INFO, "last stderr log: %s", status->stderr_log_path);
    }
    if (status->failure_summary[0] != '\0') {
        log_multiline_status_block("last failure summary:", status->failure_summary);
    }
}

static bool wait_for_test_daemon_shutdown(unsigned timeout_ms) {
    const unsigned sleep_ms = 100;
    unsigned waited_ms = 0;

    while (waited_ms < timeout_ms) {
        Test_Daemon_Status status = {0};

        if (!test_daemon_client_ping()) {
            if (!test_daemon_client_inspect_local_status(&status)) return true;
            if (!status.pid_alive) return true;
        }

        usleep((useconds_t)sleep_ms * 1000u);
        waited_ms += sleep_ms;
    }

    return false;
}

static bool force_kill_test_daemon_pid(pid_t pid) {
    bool used_sigkill = false;

    if (pid <= 0) return false;
    if (kill(pid, SIGTERM) < 0 && errno != ESRCH) {
        nob_log(NOB_ERROR, "failed to signal unresponsive test daemon pid %ld: %s", (long)pid, strerror(errno));
        return false;
    }

    for (int attempt = 0; attempt < 50; ++attempt) {
        if (kill(pid, 0) < 0 && errno == ESRCH) goto cleaned_up;
        usleep(100 * 1000);
    }

    if (kill(pid, SIGKILL) < 0 && errno != ESRCH) {
        nob_log(NOB_ERROR, "failed to SIGKILL unresponsive test daemon pid %ld: %s", (long)pid, strerror(errno));
        return false;
    }
    used_sigkill = true;

    for (int attempt = 0; attempt < 50; ++attempt) {
        if (kill(pid, 0) < 0 && errno == ESRCH) goto cleaned_up;
        usleep(100 * 1000);
    }

    nob_log(NOB_ERROR, "unresponsive test daemon pid %ld did not exit after forced kill", (long)pid);
    return false;

cleaned_up:
    if (!test_daemon_client_cleanup_stale_artifacts()) {
        nob_log(NOB_WARNING, "forced daemon kill succeeded, but stale artifacts may still be present");
    }
    nob_log(NOB_INFO,
            "test daemon stopped via fallback pid kill (pid=%ld sigkill=%s)",
            (long)pid,
            used_sigkill ? "yes" : "no");
    return true;
}

static bool start_test_daemon(void) {
    pid_t pid = -1;
    int log_fd = -1;
    int null_fd = -1;
    Test_Daemon_Status local_status = {0};

    if (test_daemon_client_ping()) {
        nob_log(NOB_INFO, "test daemon already running");
        return true;
    }

    if (!test_daemon_client_inspect_local_status(&local_status)) return false;
    if (local_status.pid_alive) {
        nob_log(NOB_ERROR,
                "test daemon pid %ld is alive but unresponsive; run `./build/nob test daemon stop --force` first",
                (long)local_status.pid);
        return false;
    }
    if (!ensure_test_daemon_binary_exists()) return false;
    if (!nob_mkdir_if_not_exists("Temp_tests")) return false;
    if (!nob_mkdir_if_not_exists(TEST_DAEMON_ROOT)) return false;

    pid = fork();
    if (pid < 0) {
        nob_log(NOB_ERROR, "failed to fork daemon bootstrap: %s", strerror(errno));
        return false;
    }
    if (pid == 0) {
        if (setsid() < 0) _exit(127);

        log_fd = open(TEST_DAEMON_LOG_PATH, O_CREAT | O_WRONLY | O_APPEND, 0666);
        if (log_fd < 0) _exit(127);
        null_fd = open("/dev/null", O_RDONLY);
        if (null_fd < 0) _exit(127);

        if (dup2(null_fd, STDIN_FILENO) < 0 ||
            dup2(log_fd, STDOUT_FILENO) < 0 ||
            dup2(log_fd, STDERR_FILENO) < 0) {
            _exit(127);
        }

        close(null_fd);
        close(log_fd);
        execl(TEST_DAEMON_BIN, TEST_DAEMON_BIN, "serve", (char *)NULL);
        _exit(127);
    }

    if (!wait_for_test_daemon_ready()) {
        nob_log(NOB_ERROR, "timed out waiting for %s to accept connections", TEST_DAEMON_BIN);
        (void)waitpid(pid, NULL, WNOHANG);
        return false;
    }
    (void)waitpid(pid, NULL, WNOHANG);
    return true;
}

static bool ensure_test_daemon_running(void) {
    if (test_daemon_client_ping()) return true;
    return start_test_daemon();
}

static bool show_test_daemon_status(void) {
    Test_Daemon_Status status = {0};
    if (!test_daemon_client_query_status(&status)) {
        if (!test_daemon_client_inspect_local_status(&status)) return false;
    }

    log_test_daemon_status_details(&status);
    return true;
}

static bool stop_test_daemon(bool force) {
    Test_Daemon_Status status = {0};
    unsigned timeout_ms = force ? 12000u : 60000u;

    if (!test_daemon_client_query_status(&status)) {
        if (!test_daemon_client_inspect_local_status(&status)) return false;
        if (status.pid_alive) {
            if (!force) {
                nob_log(NOB_ERROR,
                        "test daemon pid %ld is unresponsive; use `./build/nob test daemon stop --force`",
                        (long)status.pid);
                return false;
            }
            return force_kill_test_daemon_pid(status.pid);
        }
        if (status.stale_socket || status.stale_pid_file) {
            (void)test_daemon_client_cleanup_stale_artifacts();
        }
        nob_log(NOB_INFO, "test daemon already stopped");
        return true;
    }

    if (!(force ? test_daemon_client_stop_force(&status) : test_daemon_client_stop(&status))) return false;
    if (!wait_for_test_daemon_shutdown(timeout_ms)) {
        nob_log(NOB_ERROR,
                "timed out waiting for the test daemon to stop; inspect `./build/nob test daemon status`");
        return false;
    }

    (void)test_daemon_client_cleanup_stale_artifacts();
    nob_log(NOB_INFO, "test daemon stopped");
    return true;
}

static bool restart_test_daemon(bool force) {
    Test_Daemon_Status status = {0};

    if (test_daemon_client_query_status(&status) || test_daemon_client_inspect_local_status(&status)) {
        if (status.daemon_reachable || status.pid_alive || status.stale_socket || status.stale_pid_file) {
            if (!stop_test_daemon(force)) return false;
        }
    }
    return start_test_daemon();
}

static bool handle_test_daemon_command(int argc, char **argv) {
    const char *subcmd = argc > 3 ? argv[3] : "status";
    bool force = false;

    if (argc > 5) {
        nob_log(NOB_ERROR, "unexpected extra daemon arguments");
        return false;
    }
    if (argc == 5) {
        if (strcmp(argv[4], "--force") != 0) {
            nob_log(NOB_ERROR, "unexpected daemon flag: %s", argv[4]);
            return false;
        }
        force = true;
    }
    if (force && strcmp(subcmd, "stop") != 0 && strcmp(subcmd, "restart") != 0) {
        nob_log(NOB_ERROR, "`--force` is only supported for `daemon stop` and `daemon restart`");
        return false;
    }

    if (strcmp(subcmd, "start") == 0) return start_test_daemon();
    if (strcmp(subcmd, "stop") == 0) return stop_test_daemon(force);
    if (strcmp(subcmd, "restart") == 0) return restart_test_daemon(force);
    if (strcmp(subcmd, "status") == 0) return show_test_daemon_status();

    nob_log(NOB_ERROR, "unknown daemon subcommand: %s", subcmd);
    return false;
}

static bool run_test_front_door(int argc, char **argv) {
    Test_Runner_Request request = {0};
    Test_Runner_Watch_Request watch_request = {0};
    Test_Runner_Result result = {0};

    if (argc > 2 && strcmp(argv[2], "daemon") == 0) {
        return handle_test_daemon_command(argc, argv);
    }

    if (argc > 2 && strcmp(argv[2], "watch") == 0) {
        if (!test_runner_parse_watch_front_door(argv[0], argc - 3, argv + 3, &watch_request)) return false;
        if (!ensure_test_daemon_running()) return false;
        return test_daemon_client_watch(&watch_request);
    }

    if (!test_runner_parse_front_door(argv[0], argc - 2, argv + 2, &request)) return false;
    switch (request.action) {
        case TEST_RUNNER_ACTION_RUN_TIDY_AGGREGATE:
        case TEST_RUNNER_ACTION_RUN_TIDY_SEMANTIC:
        case TEST_RUNNER_ACTION_RUN_TIDY_MODULE:
            return test_runner_execute(&request, &result);

        case TEST_RUNNER_ACTION_CLEAN:
            if (!stop_test_daemon(request.force)) return false;
            return test_runner_execute(&request, &result);

        case TEST_RUNNER_ACTION_RUN_AGGREGATE:
        case TEST_RUNNER_ACTION_RUN_MODULE:
            if (!ensure_test_daemon_running()) return false;
            return test_daemon_client_run_request(&request, &result);
    }

    nob_log(NOB_ERROR, "unsupported test action: %d", (int)request.action);
    return false;
}

static void append_common_flags(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "-D_GNU_SOURCE",                // Necessário para algumas funções POSIX
        "-Wall", "-Wextra", "-std=c11", // Flags de aviso e padrão C
        "-O3",                          // Build otimizado
        "-ggdb",                        // Símbolos para depuração/valgrind+gdb
        "-DHAVE_CONFIG_H",
        "-DPCRE2_CODE_UNIT_WIDTH=8",   // Configuração do PCRE2
        "-Ivendor");

    // Includes do projeto
    nob_cmd_append(cmd,
        "-Isrc_v2/arena",
        "-Isrc_v2/lexer",
        "-Isrc_v2/parser",
        "-Isrc_v2/diagnostics",
        "-Isrc_v2/transpiler",
        "-Isrc_v2/build_model",
        "-Isrc_v2/codegen",
        "-Isrc_v2/evaluator",
        "-Isrc_v2/genex");

    const char *use_libcurl = getenv("NOBIFY_USE_LIBCURL");
    const char *use_libarchive = getenv("NOBIFY_USE_LIBARCHIVE");
    if (use_libcurl && strcmp(use_libcurl, "1") == 0) {
        nob_cmd_append(cmd, "-DEVAL_HAVE_LIBCURL=1");
    }
    if (use_libarchive && strcmp(use_libarchive, "1") == 0) {
        nob_cmd_append(cmd, "-DEVAL_HAVE_LIBARCHIVE=1");
    }
}

static void append_evaluator_sources(Nob_Cmd *cmd) {
    nob_cmd_append(cmd,
        "src_v2/arena/arena.c",
        "src_v2/lexer/lexer.c",
        "src_v2/parser/parser.c",
        "src_v2/diagnostics/diagnostics.c",
        "src_v2/transpiler/event_ir.c",
        "src_v2/build_model/build_model_builder.c",
        "src_v2/build_model/build_model_builder_directory.c",
        "src_v2/build_model/build_model_builder_project.c",
        "src_v2/build_model/build_model_builder_target.c",
        "src_v2/build_model/build_model_builder_replay.c",
        "src_v2/build_model/build_model_builder_test.c",
        "src_v2/build_model/build_model_builder_install.c",
        "src_v2/build_model/build_model_builder_export.c",
        "src_v2/build_model/build_model_builder_package.c",
        "src_v2/build_model/bm_compile_features.c",
        "src_v2/build_model/build_model_validate.c",
        "src_v2/build_model/build_model_validate_cycles.c",
        "src_v2/build_model/build_model_freeze.c",
        "src_v2/build_model/build_model_query.c",
        "src_v2/codegen/nob_codegen.c",
        "src_v2/codegen/nob_codegen_steps.c",
        "src_v2/genex/genex.c",
        "src_v2/evaluator/stb_ds_impl.c",
        "src_v2/evaluator/eval_exec_core.c",
        "src_v2/evaluator/eval_nested_exec.c",
        "src_v2/evaluator/eval_user_command.c",
        "src_v2/evaluator/evaluator.c",
        "src_v2/evaluator/eval_cpack.c",
        "src_v2/evaluator/eval_cmake_path.c",
        "src_v2/evaluator/eval_cmake_path_utils.c",
        "src_v2/evaluator/eval_custom.c",
        "src_v2/evaluator/eval_ctest.c",
        "src_v2/evaluator/eval_directory.c",
        "src_v2/evaluator/eval_diag.c",
        "src_v2/evaluator/eval_diag_classify.c",
        "src_v2/evaluator/eval_dispatcher.c",
        "src_v2/evaluator/eval_command_caps.c",
        "src_v2/evaluator/eval_expr.c",
        "src_v2/evaluator/eval_fetchcontent.c",
        "src_v2/evaluator/eval_hash.c",
        "src_v2/evaluator/eval_file.c",
        "src_v2/evaluator/eval_file_path.c",
        "src_v2/evaluator/eval_file_glob.c",
        "src_v2/evaluator/eval_file_rw.c",
        "src_v2/evaluator/eval_file_copy.c",
        "src_v2/evaluator/eval_file_extra.c",
        "src_v2/evaluator/eval_file_runtime_deps.c",
        "src_v2/evaluator/eval_file_fsops.c",
        "src_v2/evaluator/eval_file_backend_curl.c",
        "src_v2/evaluator/eval_file_backend_archive.c",
        "src_v2/evaluator/eval_file_transfer.c",
        "src_v2/evaluator/eval_file_generate_lock_archive.c",
        "src_v2/evaluator/eval_flow.c",
        "src_v2/evaluator/eval_flow_block.c",
        "src_v2/evaluator/eval_flow_cmake_language.c",
        "src_v2/evaluator/eval_flow_process.c",
        "src_v2/evaluator/eval_host.c",
        "src_v2/evaluator/eval_include.c",
        "src_v2/evaluator/eval_install.c",
        "src_v2/evaluator/eval_legacy.c",
        "src_v2/evaluator/eval_meta.c",
        "src_v2/evaluator/eval_opt_parser.c",
        "src_v2/evaluator/eval_package_find_item.c",
        "src_v2/evaluator/eval_package.c",
        "src_v2/evaluator/eval_property.c",
        "src_v2/evaluator/eval_project.c",
        "src_v2/evaluator/eval_list.c",
        "src_v2/evaluator/eval_list_helpers.c",
        "src_v2/evaluator/eval_math.c",
        "src_v2/evaluator/eval_compat.c",
        "src_v2/evaluator/eval_policy_engine.c",
        "src_v2/evaluator/eval_report.c",
        "src_v2/evaluator/eval_runtime_process.c",
        "src_v2/evaluator/eval_string_text.c",
        "src_v2/evaluator/eval_string_regex.c",
        "src_v2/evaluator/eval_string_json.c",
        "src_v2/evaluator/eval_string_misc.c",
        "src_v2/evaluator/eval_string.c",
        "src_v2/evaluator/eval_target_property_query.c",
        "src_v2/evaluator/eval_target_usage.c",
        "src_v2/evaluator/eval_target_source_group.c",
        "src_v2/evaluator/eval_target.c",
        "src_v2/evaluator/eval_test.c",
        "src_v2/evaluator/eval_try_compile.c",
        "src_v2/evaluator/eval_try_compile_parse.c",
        "src_v2/evaluator/eval_try_compile_exec.c",
        "src_v2/evaluator/eval_try_run.c",
        "src_v2/evaluator/eval_utils.c",
        "src_v2/evaluator/eval_utils_path.c",
        "src_v2/evaluator/eval_vars.c",
        "src_v2/evaluator/eval_vars_parse.c");
}

// No Linux, não compilamos PCRE manualmente, apenas linkamos a lib do sistema.
static void append_linker_flags(Nob_Cmd *cmd) {
    nob_cmd_append(cmd, "-lpcre2-posix");
    nob_cmd_append(cmd, "-lpcre2-8"); // Linka com a libpcre2 instalada via apt
    nob_cmd_append(cmd, "-lm");       // Linka math lib (geralmente útil)

    const char *use_libcurl = getenv("NOBIFY_USE_LIBCURL");
    const char *use_libarchive = getenv("NOBIFY_USE_LIBARCHIVE");
    if (use_libcurl && strcmp(use_libcurl, "1") == 0) {
        nob_cmd_append(cmd, "-lcurl");
    }
    if (use_libarchive && strcmp(use_libarchive, "1") == 0) {
        nob_cmd_append(cmd, "-larchive");
    }
}

static bool build_app(void) {
    bool ok = false;
    if (!nob_mkdir_if_not_exists("build")) return false;
    if (!nob_mkdir_if_not_exists("build/v2")) return false;

    Nob_Cmd cmd = {0};
    nob_cc(&cmd);
    
    // 1. Flags de compilação
    append_common_flags(&cmd);
    
    // 2. Output e Main
    nob_cmd_append(&cmd, "-o", APP_BIN, APP_SRC);
    
    // 3. Fontes do projeto
    append_evaluator_sources(&cmd);
    
    // 4. Libs externas (Linker)
    append_linker_flags(&cmd);

    ok = nob_cmd_run(&cmd);
    nob_cmd_free(cmd);
    return ok;
}

static bool build_snapshot_tool(void) {
    Nob_Cmd cmd = {0};
    bool ok = false;

    if (!nob_mkdir_if_not_exists("build")) return false;

    nob_cc(&cmd);
    nob_cmd_append(&cmd, "-Wall", "-Wextra", "-std=c11", "-O2", "-ggdb", "-Ivendor");
    nob_cmd_append(&cmd,
                   "-o",
                   SNAPSHOT_TOOL_BIN,
                   SNAPSHOT_TOOL_SRC,
                   "test_v2/artifact_parity/test_artifact_parity_corpus_manifest.c");
    ok = nob_cmd_run(&cmd);
    nob_cmd_free(cmd);
    return ok;
}

static bool run_snapshot_tool(int argc, char **argv) {
    Nob_Cmd cmd = {0};
    bool ok = false;
    if (!build_snapshot_tool()) return false;
    nob_cmd_append(&cmd, SNAPSHOT_TOOL_BIN);
    for (int i = 2; i < argc; ++i) {
        nob_cmd_append(&cmd, argv[i]);
    }
    ok = nob_cmd_run(&cmd);
    nob_cmd_free(cmd);
    return ok;
}

static bool run_valgrind(int argc, char **argv) {
    bool ok = false;
    if (!build_app()) return false;

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "valgrind");
    
    // Flags do Valgrind
    nob_cmd_append(&cmd, "--leak-check=full");
    nob_cmd_append(&cmd, "--show-leak-kinds=all");
    nob_cmd_append(&cmd, "--track-origins=yes");
    nob_cmd_append(&cmd, "--vgdb=yes");
    nob_cmd_append(&cmd, "--vgdb-error=0");
    // nob_cmd_append(&cmd, "-s"); // Estatísticas resumidas
    
    // O seu programa
    nob_cmd_append(&cmd, APP_BIN);

    // REPASSA OS ARGUMENTOS EXTRAS (começando do índice 2)
    // Exemplo: ./nob valgrind arg1 arg2
    // argv[0]="./nob", argv[1]="valgrind", argv[2]="arg1"...
    for (int i = 2; i < argc; ++i) {
        nob_cmd_append(&cmd, argv[i]);
    }

    ok = nob_cmd_run(&cmd);
    nob_cmd_free(cmd);
    return ok;
}

static bool clean_all(void) {
    bool ok = true;
    if (nob_file_exists(SNAPSHOT_TOOL_BIN)) {
        ok = nob_delete_file(SNAPSHOT_TOOL_BIN) && ok;
    }
    if (nob_file_exists(TEST_DAEMON_BIN)) {
        ok = nob_delete_file(TEST_DAEMON_BIN) && ok;
    }
    if (nob_file_exists(APP_BIN)) {
        ok = nob_delete_file(APP_BIN) && ok;
    }
    return ok;
}

int main(int argc, char **argv) {
    const char *cmd = (argc > 1) ? argv[1] : "build";

    NOB_GO_REBUILD_URSELF_PLUS(argc, argv, TEST_RUNNER_NOB_REBUILD_DEPS);

    if (strcmp(cmd, "build") == 0) return build_app() ? 0 : 1;
    if (strcmp(cmd, "test") == 0) return run_test_front_door(argc, argv) ? 0 : 1;
    if (strcmp(cmd, "build-update-artifact-parity-snapshots") == 0) return build_snapshot_tool() ? 0 : 1;
    if (strcmp(cmd, "update-artifact-parity-snapshots") == 0) return run_snapshot_tool(argc, argv) ? 0 : 1;
    if (strcmp(cmd, "clean") == 0) return clean_all() ? 0 : 1;
    
    // Passa argc e argv para a função
    if (strcmp(cmd, "valgrind") == 0) return run_valgrind(argc, argv) ? 0 : 1;

    nob_log(NOB_INFO,
            "Usage: %s [build|test|build-update-artifact-parity-snapshots|update-artifact-parity-snapshots|clean|valgrind]",
            argv[0]);
    nob_log(NOB_INFO,
            "Test front door: %s test [smoke|<module>] [--verbose] [--asan|--ubsan|--msan|--san|--cov]",
            argv[0]);
    nob_log(NOB_INFO,
            "Test utility commands: %s test clean [--force] | %s test tidy <all|module|semantic> [--verbose]",
            argv[0],
            argv[0]);
    nob_log(NOB_INFO,
            "Watch mode: %s test watch <module|auto> [--verbose] [--asan|--ubsan|--msan|--san|--cov]",
            argv[0]);
    nob_log(NOB_INFO,
            "Daemon lifecycle: %s test daemon [start|stop [--force]|restart [--force]|status]",
            argv[0]);
    return 1;
}
