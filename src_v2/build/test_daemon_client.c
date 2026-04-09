#include "test_daemon_client.h"

#include "nob.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static uint64_t test_daemon_client_next_request_id(void) {
    static uint64_t counter = 0;
    counter++;
    return ((uint64_t)getpid() << 32) ^ (uint64_t)time(NULL) ^ counter;
}

static bool test_daemon_client_copy_cstr(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return false;
    if (snprintf(dst, dst_size, "%s", src ? src : "") >= (int)dst_size) return false;
    return true;
}

static bool test_daemon_client_copy_status(Test_Daemon_Status *out_status,
                                           const Test_Daemon_Result_Payload *payload) {
    if (!out_status || !payload) return false;
    memset(out_status, 0, sizeof(*out_status));
    out_status->state = (Test_Daemon_State)payload->daemon_state;
    out_status->pid = (pid_t)payload->pid;
    out_status->daemon_reachable = true;
    out_status->socket_present = true;
    out_status->pid_file_present = payload->pid != 0;
    out_status->pid_alive = payload->pid != 0;
    out_status->active_request = payload->active_request;
    out_status->cache_launcher_kind = (Test_Runner_Launcher_Kind)payload->cache_launcher_kind;
    out_status->cache_launcher_reason = (Test_Daemon_Launcher_Cache_Reason)payload->cache_launcher_reason;
    out_status->cache_preflight_reason = (Test_Daemon_Preflight_Reason)payload->cache_preflight_reason;
    return test_daemon_client_copy_cstr(out_status->socket_path,
                                        sizeof(out_status->socket_path),
                                        payload->socket_path) &&
           test_daemon_client_copy_cstr(out_status->preserved_workspace_path,
                                        sizeof(out_status->preserved_workspace_path),
                                        payload->preserved_workspace_path) &&
           test_daemon_client_copy_cstr(out_status->stdout_log_path,
                                        sizeof(out_status->stdout_log_path),
                                        payload->stdout_log_path) &&
           test_daemon_client_copy_cstr(out_status->stderr_log_path,
                                        sizeof(out_status->stderr_log_path),
                                        payload->stderr_log_path) &&
           test_daemon_client_copy_cstr(out_status->case_name,
                                        sizeof(out_status->case_name),
                                        payload->case_name) &&
           test_daemon_client_copy_cstr(out_status->detail,
                                        sizeof(out_status->detail),
                                        payload->detail) &&
           test_daemon_client_copy_cstr(out_status->summary,
                                        sizeof(out_status->summary),
                                        payload->summary) &&
           test_daemon_client_copy_cstr(out_status->failure_summary,
                                        sizeof(out_status->failure_summary),
                                        payload->failure_summary);
}

static bool test_daemon_client_copy_result(Test_Runner_Result *out_result,
                                           const Test_Daemon_Result_Payload *payload) {
    if (!out_result || !payload) return false;
    *out_result = (Test_Runner_Result){0};
    out_result->ok = payload->runner_ok != 0;
    out_result->exit_code = payload->exit_code;
    return test_daemon_client_copy_cstr(out_result->case_name,
                                        sizeof(out_result->case_name),
                                        payload->case_name) &&
           test_daemon_client_copy_cstr(out_result->preserved_workspace_path,
                                        sizeof(out_result->preserved_workspace_path),
                                        payload->preserved_workspace_path) &&
           test_daemon_client_copy_cstr(out_result->stdout_log_path,
                                        sizeof(out_result->stdout_log_path),
                                        payload->stdout_log_path) &&
           test_daemon_client_copy_cstr(out_result->stderr_log_path,
                                        sizeof(out_result->stderr_log_path),
                                        payload->stderr_log_path) &&
           test_daemon_client_copy_cstr(out_result->summary,
                                        sizeof(out_result->summary),
                                        payload->summary) &&
           test_daemon_client_copy_cstr(out_result->failure_summary,
                                        sizeof(out_result->failure_summary),
                                        payload->failure_summary);
}

static int test_daemon_client_connect(void) {
    struct sockaddr_un addr = {0};
    int fd = -1;

    fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    addr.sun_family = AF_UNIX;
    if (snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", TEST_DAEMON_SOCKET_PATH) >= (int)sizeof(addr.sun_path)) {
        close(fd);
        errno = ENAMETOOLONG;
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }

    if (!test_daemon_send_hello(fd) || !test_daemon_expect_ack(fd)) {
        close(fd);
        errno = EPROTO;
        return -1;
    }

    return fd;
}

static bool test_daemon_client_read_pid_file(pid_t *out_pid) {
    FILE *file = NULL;
    long value = 0;

    if (out_pid) *out_pid = 0;
    if (!nob_file_exists(TEST_DAEMON_PID_PATH)) return false;

    file = fopen(TEST_DAEMON_PID_PATH, "rb");
    if (!file) return false;
    if (fscanf(file, "%ld", &value) != 1) {
        fclose(file);
        return false;
    }
    fclose(file);
    if (out_pid) *out_pid = (pid_t)value;
    return value > 0;
}

static bool test_daemon_client_pid_alive(pid_t pid) {
    if (pid <= 0) return false;
    return kill(pid, 0) == 0 || errno == EPERM;
}

bool test_daemon_client_inspect_local_status(Test_Daemon_Status *out_status) {
    pid_t pid = 0;
    bool pid_file_present = false;
    bool socket_present = false;
    bool pid_alive = false;

    if (!out_status) return false;
    *out_status = (Test_Daemon_Status){0};
    out_status->state = TEST_DAEMON_STATE_STOPPED;
    out_status->cache_launcher_kind = TEST_RUNNER_LAUNCHER_NONE;
    out_status->cache_launcher_reason = TEST_DAEMON_LAUNCHER_CACHE_COLD;
    out_status->cache_preflight_reason = TEST_DAEMON_PREFLIGHT_REASON_COLD;

    pid_file_present = test_daemon_client_read_pid_file(&pid);
    socket_present = nob_file_exists(TEST_DAEMON_SOCKET_PATH);
    pid_alive = pid_file_present && test_daemon_client_pid_alive(pid);

    out_status->pid = pid;
    out_status->socket_present = socket_present;
    out_status->pid_file_present = pid_file_present;
    out_status->pid_alive = pid_alive;
    out_status->stale_socket = socket_present && !pid_alive;
    out_status->stale_pid_file = pid_file_present && !pid_alive;

    if (!test_daemon_client_copy_cstr(out_status->socket_path,
                                      sizeof(out_status->socket_path),
                                      TEST_DAEMON_SOCKET_PATH) ||
        !test_daemon_client_copy_cstr(out_status->summary,
                                      sizeof(out_status->summary),
                                      "stopped")) {
        return false;
    }

    if (pid_alive) {
        return test_daemon_client_copy_cstr(out_status->detail,
                                            sizeof(out_status->detail),
                                            "pid file is alive but the daemon control socket is unresponsive");
    }
    if (out_status->stale_socket && out_status->stale_pid_file) {
        return test_daemon_client_copy_cstr(out_status->detail,
                                            sizeof(out_status->detail),
                                            "stale socket and pid artifacts are present");
    }
    if (out_status->stale_socket) {
        return test_daemon_client_copy_cstr(out_status->detail,
                                            sizeof(out_status->detail),
                                            "stale daemon socket artifact is present");
    }
    if (out_status->stale_pid_file) {
        return test_daemon_client_copy_cstr(out_status->detail,
                                            sizeof(out_status->detail),
                                            "stale daemon pid artifact is present");
    }
    return test_daemon_client_copy_cstr(out_status->detail, sizeof(out_status->detail), "");
}

static void test_daemon_client_log_error_payload(const Test_Daemon_Error_Payload *payload) {
    const Test_Daemon_Request_Metadata *active = NULL;

    if (!payload) return;
    active = &payload->active_request;
    nob_log(NOB_ERROR,
            "[daemon:%s] %s",
            test_daemon_error_code_name((Test_Daemon_Error_Code)payload->code),
            payload->message);
    if ((Test_Daemon_Request_Kind)active->kind == TEST_DAEMON_REQUEST_NONE) return;

    nob_log(NOB_ERROR,
            "daemon state=%s active=%s policy=%s module=%s profile=%s case=%s duration=%lluus",
            test_daemon_state_name((Test_Daemon_State)payload->daemon_state),
            test_daemon_request_kind_name((Test_Daemon_Request_Kind)active->kind),
            test_daemon_admission_policy_name((Test_Daemon_Admission_Policy)active->admission_policy),
            active->module_name[0] != '\0' ? active->module_name : "<none>",
            active->profile_name[0] != '\0' ? active->profile_name : "<none>",
            active->case_name[0] != '\0' ? active->case_name : "<all>",
            (unsigned long long)active->active_duration_usec);
}

static bool test_daemon_client_request_control(Test_Daemon_Control_Command command,
                                               Test_Daemon_Status *out_status,
                                               bool *out_ok) {
    Test_Daemon_Message message = {0};
    Test_Daemon_Result_Payload payload = {0};
    int fd = -1;
    bool ok = false;

    if (out_ok) *out_ok = false;
    fd = test_daemon_client_connect();
    if (fd < 0) return false;

    if (!test_daemon_send_control_request(fd,
                                          test_daemon_client_next_request_id(),
                                          command)) {
        goto defer;
    }
    if (!test_daemon_recv_message(fd, &message)) goto defer;

    if (message.header.type == TEST_DAEMON_MESSAGE_ERROR) {
        const Test_Daemon_Error_Payload *error_payload = (const Test_Daemon_Error_Payload *)message.payload;
        if (message.header.payload_len != sizeof(*error_payload)) {
            nob_log(NOB_ERROR, "unexpected daemon error payload size");
            goto defer;
        }
        test_daemon_client_log_error_payload(error_payload);
        goto defer;
    }
    if (message.header.type != TEST_DAEMON_MESSAGE_RESULT ||
        message.header.payload_len != sizeof(payload)) {
        nob_log(NOB_ERROR, "unexpected daemon control reply");
        goto defer;
    }

    memcpy(&payload, message.payload, sizeof(payload));
    if (out_status && !test_daemon_client_copy_status(out_status, &payload)) goto defer;
    ok = true;

defer:
    if (fd >= 0) close(fd);
    test_daemon_message_free(&message);
    if (out_ok) *out_ok = ok;
    return ok;
}

bool test_daemon_client_ping(void) {
    bool ok = false;
    return test_daemon_client_request_control(TEST_DAEMON_CONTROL_PING, NULL, &ok) && ok;
}

bool test_daemon_client_query_status(Test_Daemon_Status *out_status) {
    bool ok = false;
    return test_daemon_client_request_control(TEST_DAEMON_CONTROL_STATUS, out_status, &ok) && ok;
}

bool test_daemon_client_stop(Test_Daemon_Status *out_status) {
    bool ok = false;
    return test_daemon_client_request_control(TEST_DAEMON_CONTROL_STOP, out_status, &ok) && ok;
}

bool test_daemon_client_stop_force(Test_Daemon_Status *out_status) {
    bool ok = false;
    return test_daemon_client_request_control(TEST_DAEMON_CONTROL_STOP_FORCE, out_status, &ok) && ok;
}

bool test_daemon_client_cleanup_stale_artifacts(void) {
    Test_Daemon_Status status = {0};
    bool removed = false;

    if (!test_daemon_client_inspect_local_status(&status)) return false;
    if (status.pid_alive) return false;

    if (status.socket_present) {
        if (unlink(TEST_DAEMON_SOCKET_PATH) == 0 || errno == ENOENT) removed = true;
    }
    if (status.pid_file_present) {
        if (unlink(TEST_DAEMON_PID_PATH) == 0 || errno == ENOENT) removed = true;
    }
    return removed || (!nob_file_exists(TEST_DAEMON_SOCKET_PATH) && !nob_file_exists(TEST_DAEMON_PID_PATH));
}

static bool test_daemon_client_build_run_argv(const Test_Runner_Request *request,
                                              int *out_argc,
                                              const char *argv_buf[5]) {
    const Test_Runner_Module_Def *module = NULL;
    const Test_Runner_Profile_Def *profile = NULL;
    int argc = 0;

    if (!request || !out_argc || !argv_buf) return false;

    if (request->action == TEST_RUNNER_ACTION_RUN_MODULE) {
        module = test_runner_get_module_def(request->module_id);
        if (!module) return false;
        argv_buf[argc++] = module->name;
    } else if (request->action != TEST_RUNNER_ACTION_RUN_AGGREGATE) {
        nob_log(NOB_ERROR, "daemon run path only supports module or smoke execution");
        return false;
    }

    if (request->verbose) argv_buf[argc++] = "--verbose";

    if (request->profile_id != TEST_RUNNER_PROFILE_FAST) {
        profile = test_runner_get_profile_def(request->profile_id);
        if (!profile || !profile->front_door_flag) {
            nob_log(NOB_ERROR, "request profile cannot be represented on daemon front door");
            return false;
        }
        argv_buf[argc++] = profile->front_door_flag;
    }

    if (request->case_name[0] != '\0') {
        argv_buf[argc++] = "--case";
        argv_buf[argc++] = request->case_name;
    }

    *out_argc = argc;
    return true;
}

static bool test_daemon_client_build_watch_argv(const Test_Runner_Watch_Request *request,
                                                int *out_argc,
                                                const char *argv_buf[3]) {
    const Test_Runner_Module_Def *module = NULL;
    const Test_Runner_Profile_Def *profile = NULL;
    int argc = 0;

    if (!request || !out_argc || !argv_buf) return false;

    if (request->mode == TEST_RUNNER_WATCH_MODE_AUTO) {
        argv_buf[argc++] = "auto";
    } else {
        module = test_runner_get_module_def(request->module_id);
        if (!module) return false;
        argv_buf[argc++] = module->name;
    }

    if (request->verbose) argv_buf[argc++] = "--verbose";

    if (request->profile_explicit) {
        profile = test_runner_get_profile_def(request->profile_id);
        if (!profile || !profile->front_door_flag) {
            nob_log(NOB_ERROR, "watch profile cannot be represented on daemon front door");
            return false;
        }
        argv_buf[argc++] = profile->front_door_flag;
    }

    *out_argc = argc;
    return true;
}

static void test_daemon_client_log_watch_result(const Test_Daemon_Result_Payload *payload) {
    const char *status = (payload && payload->runner_ok != 0) ? "PASS" : "FAIL";

    if (!payload) return;
    if (payload->summary[0] != '\0') {
        fprintf(stderr, "[watch] %s: %s\n", status, payload->summary);
    } else {
        fprintf(stderr, "[watch] %s\n", status);
    }
    if (payload->preserved_workspace_path[0] != '\0') {
        fprintf(stderr, "[watch] workspace: %s\n", payload->preserved_workspace_path);
    }
    if (payload->stdout_log_path[0] != '\0') {
        fprintf(stderr, "[watch] stdout log: %s\n", payload->stdout_log_path);
    }
    if (payload->stderr_log_path[0] != '\0') {
        fprintf(stderr, "[watch] stderr log: %s\n", payload->stderr_log_path);
    }
    if (payload->failure_summary[0] != '\0') {
        fprintf(stderr, "%s", payload->failure_summary);
        if (payload->failure_summary[strlen(payload->failure_summary) - 1] != '\n') {
            fputc('\n', stderr);
        }
    }
    fflush(stderr);
}

bool test_daemon_client_run_request(const Test_Runner_Request *request,
                                    Test_Runner_Result *out_result) {
    Test_Daemon_Message message = {0};
    Test_Daemon_Result_Payload payload = {0};
    const char *argv_buf[5] = {0};
    int argc = 0;
    int fd = -1;
    bool ok = false;

    if (out_result) *out_result = (Test_Runner_Result){0};
    if (!request) return false;
    if (!test_daemon_client_build_run_argv(request, &argc, argv_buf)) return false;

    fd = test_daemon_client_connect();
    if (fd < 0) return false;

    if (!test_daemon_send_run_request(fd,
                                      test_daemon_client_next_request_id(),
                                      argc,
                                      argv_buf)) {
        goto defer;
    }

    for (;;) {
        if (!test_daemon_recv_message(fd, &message)) goto defer;
        switch (message.header.type) {
            case TEST_DAEMON_MESSAGE_ACK:
                break;

            case TEST_DAEMON_MESSAGE_STDOUT:
                if (message.header.payload_len > 0) {
                    fwrite(message.payload, 1, message.header.payload_len, stdout);
                    fflush(stdout);
                }
                break;

            case TEST_DAEMON_MESSAGE_STDERR:
            case TEST_DAEMON_MESSAGE_INFO:
                if (message.header.payload_len > 0) {
                    fwrite(message.payload, 1, message.header.payload_len, stderr);
                    fflush(stderr);
                }
                break;

            case TEST_DAEMON_MESSAGE_ERROR: {
                const Test_Daemon_Error_Payload *error_payload =
                    (const Test_Daemon_Error_Payload *)message.payload;
                if (message.header.payload_len != sizeof(*error_payload)) {
                    nob_log(NOB_ERROR, "unexpected daemon error payload size");
                    goto defer;
                }
                test_daemon_client_log_error_payload(error_payload);
                goto defer;
            }

            case TEST_DAEMON_MESSAGE_RESULT:
                if (message.header.payload_len != sizeof(payload)) {
                    nob_log(NOB_ERROR, "daemon result payload size mismatch");
                    goto defer;
                }
                memcpy(&payload, message.payload, sizeof(payload));
                if (out_result && !test_daemon_client_copy_result(out_result, &payload)) goto defer;
                if (payload.runner_ok == 0 && payload.failure_summary[0] != '\0') {
                    fprintf(stderr, "%s", payload.failure_summary);
                    if (payload.failure_summary[strlen(payload.failure_summary) - 1] != '\n') {
                        fputc('\n', stderr);
                    }
                    fflush(stderr);
                }
                ok = payload.runner_ok != 0;
                goto defer;

            default:
                nob_log(NOB_ERROR, "unexpected daemon message type: %u", message.header.type);
                goto defer;
        }
        test_daemon_message_free(&message);
    }

defer:
    if (fd >= 0) close(fd);
    test_daemon_message_free(&message);
    return ok;
}

bool test_daemon_client_watch(const Test_Runner_Watch_Request *request) {
    Test_Daemon_Message message = {0};
    Test_Daemon_Result_Payload payload = {0};
    const char *argv_buf[3] = {0};
    int argc = 0;
    int fd = -1;
    bool ok = false;
    bool saw_event = false;
    bool saw_result = false;
    bool all_results_ok = true;

    if (!request) return false;
    if (!test_daemon_client_build_watch_argv(request, &argc, argv_buf)) return false;

    fd = test_daemon_client_connect();
    if (fd < 0) return false;

    if (!test_daemon_send_watch_request(fd,
                                        test_daemon_client_next_request_id(),
                                        argc,
                                        argv_buf)) {
        goto defer;
    }

    for (;;) {
        if (!test_daemon_recv_message(fd, &message)) {
            ok = saw_result ? all_results_ok : saw_event;
            goto defer;
        }
        switch (message.header.type) {
            case TEST_DAEMON_MESSAGE_ACK:
                saw_event = true;
                break;

            case TEST_DAEMON_MESSAGE_STDOUT:
                saw_event = true;
                if (message.header.payload_len > 0) {
                    fwrite(message.payload, 1, message.header.payload_len, stdout);
                    fflush(stdout);
                }
                break;

            case TEST_DAEMON_MESSAGE_STDERR:
            case TEST_DAEMON_MESSAGE_INFO:
                saw_event = true;
                if (message.header.payload_len > 0) {
                    fwrite(message.payload, 1, message.header.payload_len, stderr);
                    fflush(stderr);
                }
                break;

            case TEST_DAEMON_MESSAGE_RESULT:
                saw_event = true;
                saw_result = true;
                if (message.header.payload_len != sizeof(payload)) {
                    nob_log(NOB_ERROR, "daemon watch result payload size mismatch");
                    goto defer;
                }
                memcpy(&payload, message.payload, sizeof(payload));
                if (payload.runner_ok == 0) all_results_ok = false;
                test_daemon_client_log_watch_result(&payload);
                break;

            case TEST_DAEMON_MESSAGE_ERROR: {
                const Test_Daemon_Error_Payload *error_payload =
                    (const Test_Daemon_Error_Payload *)message.payload;
                if (message.header.payload_len != sizeof(*error_payload)) {
                    nob_log(NOB_ERROR, "unexpected daemon error payload size");
                    goto defer;
                }
                test_daemon_client_log_error_payload(error_payload);
                goto defer;
            }

            default:
                nob_log(NOB_ERROR, "unexpected daemon watch message type: %u", message.header.type);
                goto defer;
        }
        test_daemon_message_free(&message);
    }

defer:
    if (fd >= 0) close(fd);
    test_daemon_message_free(&message);
    return ok;
}
