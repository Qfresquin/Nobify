#include "test_daemon_protocol.h"

#include "nob.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static bool test_daemon_copy_string(char *dst, size_t dst_size, const char *src) {
    int n = 0;
    if (!dst || dst_size == 0) return false;
    if (!src) {
        dst[0] = '\0';
        return true;
    }
    n = snprintf(dst, dst_size, "%s", src);
    if (n < 0 || (size_t)n >= dst_size) return false;
    return true;
}

bool test_daemon_send_message(int fd,
                              Test_Daemon_Message_Type type,
                              uint64_t request_id,
                              const void *payload,
                              uint32_t payload_len) {
    Test_Daemon_Message_Header header = {0};
    unsigned char *packet = NULL;
    size_t total_len = sizeof(header) + payload_len;
    ssize_t sent = 0;

    if (payload_len > TEST_DAEMON_MAX_PACKET_SIZE - sizeof(header)) {
        nob_log(NOB_ERROR, "daemon packet too large: %u", payload_len);
        return false;
    }

    header.magic = TEST_DAEMON_PROTOCOL_MAGIC;
    header.version = TEST_DAEMON_PROTOCOL_VERSION;
    header.type = (uint16_t)type;
    header.request_id = request_id;
    header.payload_len = payload_len;

    packet = (unsigned char *)malloc(total_len);
    if (!packet) {
        nob_log(NOB_ERROR, "failed to allocate daemon packet");
        return false;
    }

    memcpy(packet, &header, sizeof(header));
    if (payload_len > 0 && payload) {
        memcpy(packet + sizeof(header), payload, payload_len);
    }

    sent = send(fd, packet, total_len, 0);
    free(packet);
    if (sent < 0) {
        nob_log(NOB_ERROR, "daemon send failed: %s", strerror(errno));
        return false;
    }
    if ((size_t)sent != total_len) {
        nob_log(NOB_ERROR, "daemon short send: expected %zu got %zd", total_len, sent);
        return false;
    }
    return true;
}

bool test_daemon_recv_message(int fd, Test_Daemon_Message *out_message) {
    unsigned char *buffer = NULL;
    ssize_t nread = 0;
    Test_Daemon_Message_Header header = {0};

    if (!out_message) return false;
    *out_message = (Test_Daemon_Message){0};

    buffer = (unsigned char *)malloc(TEST_DAEMON_MAX_PACKET_SIZE);
    if (!buffer) {
        nob_log(NOB_ERROR, "failed to allocate daemon receive buffer");
        return false;
    }

    nread = recv(fd, buffer, TEST_DAEMON_MAX_PACKET_SIZE, 0);
    if (nread <= 0) {
        free(buffer);
        return false;
    }
    if ((size_t)nread < sizeof(header)) {
        nob_log(NOB_ERROR, "daemon packet too small: %zd", nread);
        free(buffer);
        return false;
    }

    memcpy(&header, buffer, sizeof(header));
    if (header.magic != TEST_DAEMON_PROTOCOL_MAGIC ||
        header.version != TEST_DAEMON_PROTOCOL_VERSION) {
        nob_log(NOB_ERROR, "daemon protocol mismatch");
        free(buffer);
        return false;
    }
    if ((size_t)nread != sizeof(header) + header.payload_len) {
        nob_log(NOB_ERROR, "daemon packet truncated or malformed");
        free(buffer);
        return false;
    }

    out_message->header = header;
    if (header.payload_len > 0) {
        out_message->payload = (unsigned char *)malloc(header.payload_len);
        if (!out_message->payload) {
            nob_log(NOB_ERROR, "failed to allocate daemon payload");
            free(buffer);
            return false;
        }
        memcpy(out_message->payload, buffer + sizeof(header), header.payload_len);
    }

    free(buffer);
    return true;
}

void test_daemon_message_free(Test_Daemon_Message *message) {
    if (!message) return;
    free(message->payload);
    *message = (Test_Daemon_Message){0};
}

bool test_daemon_send_hello(int fd) {
    return test_daemon_send_message(fd, TEST_DAEMON_MESSAGE_HELLO, 0, NULL, 0);
}

bool test_daemon_expect_ack(int fd) {
    Test_Daemon_Message message = {0};
    bool ok = false;
    if (!test_daemon_recv_message(fd, &message)) return false;
    ok = message.header.type == TEST_DAEMON_MESSAGE_ACK;
    if (!ok) nob_log(NOB_ERROR, "daemon handshake did not receive ACK");
    test_daemon_message_free(&message);
    return ok;
}

bool test_daemon_send_control_request(int fd,
                                      uint64_t request_id,
                                      Test_Daemon_Control_Command command) {
    Test_Daemon_Control_Request_Payload payload = {0};
    payload.command = (uint32_t)command;
    return test_daemon_send_message(fd,
                                    TEST_DAEMON_MESSAGE_CONTROL_REQUEST,
                                    request_id,
                                    &payload,
                                    sizeof(payload));
}

static bool test_daemon_send_argv_request(int fd,
                                          Test_Daemon_Message_Type type,
                                          uint64_t request_id,
                                          int argc,
                                          const char *const *argv) {
    size_t payload_len = sizeof(uint32_t);
    unsigned char *payload = NULL;
    unsigned char *cursor = NULL;
    uint32_t argc_u32 = 0;
    bool ok = false;

    if (argc < 0) return false;
    for (int i = 0; i < argc; ++i) {
        payload_len += strlen(argv[i]) + 1;
    }
    if (payload_len > TEST_DAEMON_MAX_PACKET_SIZE - sizeof(Test_Daemon_Message_Header)) {
        nob_log(NOB_ERROR, "run request too large");
        return false;
    }

    payload = (unsigned char *)malloc(payload_len);
    if (!payload) {
        nob_log(NOB_ERROR, "failed to allocate run request payload");
        return false;
    }

    argc_u32 = (uint32_t)argc;
    memcpy(payload, &argc_u32, sizeof(argc_u32));
    cursor = payload + sizeof(argc_u32);
    for (int i = 0; i < argc; ++i) {
        size_t len = strlen(argv[i]) + 1;
        memcpy(cursor, argv[i], len);
        cursor += len;
    }

    ok = test_daemon_send_message(fd,
                                  type,
                                  request_id,
                                  payload,
                                  (uint32_t)payload_len);
    free(payload);
    return ok;
}

bool test_daemon_send_run_request(int fd,
                                  uint64_t request_id,
                                  int argc,
                                  const char *const *argv) {
    return test_daemon_send_argv_request(fd,
                                         TEST_DAEMON_MESSAGE_RUN_REQUEST,
                                         request_id,
                                         argc,
                                         argv);
}

bool test_daemon_send_watch_request(int fd,
                                    uint64_t request_id,
                                    int argc,
                                    const char *const *argv) {
    return test_daemon_send_argv_request(fd,
                                         TEST_DAEMON_MESSAGE_WATCH_REQUEST,
                                         request_id,
                                         argc,
                                         argv);
}

static bool test_daemon_decode_argv_request(const Test_Daemon_Message *message,
                                            Test_Daemon_Message_Type expected_type,
                                            int *out_argc,
                                            char ***out_argv) {
    uint32_t argc_u32 = 0;
    size_t offset = sizeof(uint32_t);
    char **argv = NULL;

    if (!message || !out_argc || !out_argv) return false;
    *out_argc = 0;
    *out_argv = NULL;

    if (message->header.type != expected_type ||
        message->header.payload_len < sizeof(uint32_t)) {
        return false;
    }

    memcpy(&argc_u32, message->payload, sizeof(argc_u32));
    argv = (char **)calloc(argc_u32 ? argc_u32 : 1u, sizeof(char *));
    if (!argv) {
        nob_log(NOB_ERROR, "failed to allocate argv payload");
        return false;
    }

    for (uint32_t i = 0; i < argc_u32; ++i) {
        size_t remaining = message->header.payload_len - offset;
        size_t len = 0;
        char *copy = NULL;

        if (offset >= message->header.payload_len) {
            nob_log(NOB_ERROR, "malformed daemon argv payload");
            test_daemon_free_argv((int)i, argv);
            return false;
        }

        while (len < remaining && message->payload[offset + len] != '\0') len++;
        if (len == remaining) {
            nob_log(NOB_ERROR, "daemon argv entry missing terminator");
            test_daemon_free_argv((int)i, argv);
            return false;
        }

        copy = (char *)malloc(len + 1);
        if (!copy) {
            nob_log(NOB_ERROR, "failed to duplicate daemon argv entry");
            test_daemon_free_argv((int)i, argv);
            return false;
        }
        memcpy(copy, message->payload + offset, len + 1);
        argv[i] = copy;
        offset += len + 1;
    }

    *out_argc = (int)argc_u32;
    *out_argv = argv;
    return true;
}

bool test_daemon_decode_run_request(const Test_Daemon_Message *message,
                                    int *out_argc,
                                    char ***out_argv) {
    return test_daemon_decode_argv_request(message,
                                           TEST_DAEMON_MESSAGE_RUN_REQUEST,
                                           out_argc,
                                           out_argv);
}

bool test_daemon_decode_watch_request(const Test_Daemon_Message *message,
                                      int *out_argc,
                                      char ***out_argv) {
    return test_daemon_decode_argv_request(message,
                                           TEST_DAEMON_MESSAGE_WATCH_REQUEST,
                                           out_argc,
                                           out_argv);
}

void test_daemon_free_argv(int argc, char **argv) {
    if (!argv) return;
    for (int i = 0; i < argc; ++i) free(argv[i]);
    free(argv);
}

bool test_daemon_result_from_runner(Test_Daemon_State daemon_state,
                                    pid_t daemon_pid,
                                    const Test_Runner_Result *runner_result,
                                    Test_Daemon_Result_Payload *out_payload) {
    if (!runner_result || !out_payload) return false;
    memset(out_payload, 0, sizeof(*out_payload));
    out_payload->daemon_state = (uint32_t)daemon_state;
    out_payload->runner_ok = runner_result->ok ? 1u : 0u;
    out_payload->exit_code = runner_result->exit_code;
    out_payload->pid = daemon_pid > 0 ? (uint32_t)daemon_pid : 0u;
    if (!test_daemon_copy_string(out_payload->socket_path,
                                 sizeof(out_payload->socket_path),
                                 TEST_DAEMON_SOCKET_PATH)) {
        return false;
    }
    if (!test_daemon_copy_string(out_payload->preserved_workspace_path,
                                 sizeof(out_payload->preserved_workspace_path),
                                 runner_result->preserved_workspace_path)) {
        return false;
    }
    if (!test_daemon_copy_string(out_payload->stdout_log_path,
                                 sizeof(out_payload->stdout_log_path),
                                 runner_result->stdout_log_path)) {
        return false;
    }
    if (!test_daemon_copy_string(out_payload->stderr_log_path,
                                 sizeof(out_payload->stderr_log_path),
                                 runner_result->stderr_log_path)) {
        return false;
    }
    if (!test_daemon_copy_string(out_payload->case_name,
                                 sizeof(out_payload->case_name),
                                 runner_result->case_name)) {
        return false;
    }
    out_payload->detail[0] = '\0';
    if (!test_daemon_copy_string(out_payload->summary,
                                 sizeof(out_payload->summary),
                                 runner_result->summary)) {
        return false;
    }
    if (!test_daemon_copy_string(out_payload->failure_summary,
                                 sizeof(out_payload->failure_summary),
                                 runner_result->failure_summary)) {
        return false;
    }
    return true;
}

bool test_daemon_send_result(int fd,
                             uint64_t request_id,
                             const Test_Daemon_Result_Payload *payload) {
    return test_daemon_send_message(fd,
                                    TEST_DAEMON_MESSAGE_RESULT,
                                    request_id,
                                    payload,
                                    (uint32_t)sizeof(*payload));
}

bool test_daemon_send_error(int fd,
                            uint64_t request_id,
                            Test_Daemon_Error_Code code,
                            Test_Daemon_State state,
                            const Test_Daemon_Request_Metadata *active_request,
                            const char *message) {
    Test_Daemon_Error_Payload payload = {0};

    payload.code = (uint32_t)code;
    payload.daemon_state = (uint32_t)state;
    if (active_request) payload.active_request = *active_request;
    if (!test_daemon_copy_string(payload.message, sizeof(payload.message), message)) {
        return false;
    }
    return test_daemon_send_message(fd,
                                    TEST_DAEMON_MESSAGE_ERROR,
                                    request_id,
                                    &payload,
                                    (uint32_t)sizeof(payload));
}

bool test_daemon_send_error_text(int fd, uint64_t request_id, const char *message) {
    return test_daemon_send_error(fd,
                                  request_id,
                                  TEST_DAEMON_ERROR_INTERNAL,
                                  TEST_DAEMON_STATE_STOPPED,
                                  NULL,
                                  message);
}

const char *test_daemon_state_name(Test_Daemon_State state) {
    switch (state) {
        case TEST_DAEMON_STATE_IDLE: return "idle";
        case TEST_DAEMON_STATE_BUSY: return "busy";
        case TEST_DAEMON_STATE_WATCHING: return "watching";
        case TEST_DAEMON_STATE_DRAINING: return "draining";
        case TEST_DAEMON_STATE_STOPPING: return "stopping";
        case TEST_DAEMON_STATE_STOPPED:
        default: return "stopped";
    }
}

const char *test_daemon_request_kind_name(Test_Daemon_Request_Kind kind) {
    switch (kind) {
        case TEST_DAEMON_REQUEST_FOREGROUND_RUN: return "foreground-run";
        case TEST_DAEMON_REQUEST_WATCH_SESSION: return "watch-session";
        case TEST_DAEMON_REQUEST_NONE:
        default: return "none";
    }
}

const char *test_daemon_admission_policy_name(Test_Daemon_Admission_Policy policy) {
    switch (policy) {
        case TEST_DAEMON_POLICY_FOREGROUND_EXCLUSIVE: return "foreground-exclusive";
        case TEST_DAEMON_POLICY_WATCH_REPLACE_RUNNING: return "watch-replace-running";
        case TEST_DAEMON_POLICY_DRAIN_STOP: return "drain-stop";
        case TEST_DAEMON_POLICY_FORCE_STOP: return "force-stop";
        case TEST_DAEMON_POLICY_NONE:
        default: return "none";
    }
}

const char *test_daemon_error_code_name(Test_Daemon_Error_Code code) {
    switch (code) {
        case TEST_DAEMON_ERROR_PROTOCOL: return "protocol";
        case TEST_DAEMON_ERROR_MALFORMED_REQUEST: return "malformed-request";
        case TEST_DAEMON_ERROR_UNKNOWN_CONTROL: return "unknown-control";
        case TEST_DAEMON_ERROR_BUSY_FOREGROUND: return "busy-foreground";
        case TEST_DAEMON_ERROR_BUSY_WATCH: return "busy-watch";
        case TEST_DAEMON_ERROR_STOPPING: return "stopping";
        case TEST_DAEMON_ERROR_PREFLIGHT_FAILED: return "preflight-failed";
        case TEST_DAEMON_ERROR_WORKER_START_FAILED: return "worker-start-failed";
        case TEST_DAEMON_ERROR_INTERNAL: return "internal";
        case TEST_DAEMON_ERROR_NONE:
        default: return "none";
    }
}

const char *test_daemon_launcher_cache_reason_name(Test_Daemon_Launcher_Cache_Reason reason) {
    switch (reason) {
        case TEST_DAEMON_LAUNCHER_CACHE_HIT: return "hit";
        case TEST_DAEMON_LAUNCHER_CACHE_COLD: return "cold";
        case TEST_DAEMON_LAUNCHER_CACHE_PATH: return "PATH";
        case TEST_DAEMON_LAUNCHER_CACHE_OVERRIDE: return "override";
    }
    return "unknown";
}

const char *test_daemon_preflight_reason_name(Test_Daemon_Preflight_Reason reason) {
    switch (reason) {
        case TEST_DAEMON_PREFLIGHT_REASON_HIT: return "hit";
        case TEST_DAEMON_PREFLIGHT_REASON_COLD: return "cold";
        case TEST_DAEMON_PREFLIGHT_REASON_FINGERPRINT: return "fingerprint";
        case TEST_DAEMON_PREFLIGHT_REASON_PATH: return "PATH";
        case TEST_DAEMON_PREFLIGHT_REASON_LLVM_TOOLS: return "LLVM_TOOLS";
        case TEST_DAEMON_PREFLIGHT_REASON_LIB_FLAGS: return "LIB_FLAGS";
    }
    return "unknown";
}
