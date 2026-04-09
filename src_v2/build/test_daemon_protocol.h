#ifndef TEST_DAEMON_PROTOCOL_H_
#define TEST_DAEMON_PROTOCOL_H_

#include "test_runner_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#define TEST_DAEMON_ROOT "Temp_tests/daemon"
#define TEST_DAEMON_SOCKET_PATH TEST_DAEMON_ROOT "/nob_testd.sock"
#define TEST_DAEMON_PID_PATH TEST_DAEMON_ROOT "/nob_testd.pid"
#define TEST_DAEMON_LOG_PATH TEST_DAEMON_ROOT "/nob_testd.log"

#define TEST_DAEMON_PROTOCOL_MAGIC 0x3144544eu
#define TEST_DAEMON_PROTOCOL_VERSION 1u
#define TEST_DAEMON_MAX_PACKET_SIZE 65536u
#define TEST_DAEMON_LABEL_CAPACITY 64u

typedef enum {
    TEST_DAEMON_MESSAGE_HELLO = 1,
    TEST_DAEMON_MESSAGE_RUN_REQUEST,
    TEST_DAEMON_MESSAGE_WATCH_REQUEST,
    TEST_DAEMON_MESSAGE_CONTROL_REQUEST,
    TEST_DAEMON_MESSAGE_ACK,
    TEST_DAEMON_MESSAGE_STDOUT,
    TEST_DAEMON_MESSAGE_STDERR,
    TEST_DAEMON_MESSAGE_INFO,
    TEST_DAEMON_MESSAGE_RESULT,
    TEST_DAEMON_MESSAGE_ERROR,
} Test_Daemon_Message_Type;

typedef enum {
    TEST_DAEMON_CONTROL_PING = 1,
    TEST_DAEMON_CONTROL_STOP,
    TEST_DAEMON_CONTROL_STATUS,
    TEST_DAEMON_CONTROL_STOP_FORCE,
} Test_Daemon_Control_Command;

typedef enum {
    TEST_DAEMON_STATE_STOPPED = 0,
    TEST_DAEMON_STATE_IDLE,
    TEST_DAEMON_STATE_BUSY,
    TEST_DAEMON_STATE_WATCHING,
    TEST_DAEMON_STATE_DRAINING,
    TEST_DAEMON_STATE_STOPPING,
} Test_Daemon_State;

typedef enum {
    TEST_DAEMON_REQUEST_NONE = 0,
    TEST_DAEMON_REQUEST_FOREGROUND_RUN,
    TEST_DAEMON_REQUEST_WATCH_SESSION,
} Test_Daemon_Request_Kind;

typedef enum {
    TEST_DAEMON_POLICY_NONE = 0,
    TEST_DAEMON_POLICY_FOREGROUND_EXCLUSIVE,
    TEST_DAEMON_POLICY_WATCH_REPLACE_RUNNING,
    TEST_DAEMON_POLICY_DRAIN_STOP,
    TEST_DAEMON_POLICY_FORCE_STOP,
} Test_Daemon_Admission_Policy;

typedef enum {
    TEST_DAEMON_ERROR_NONE = 0,
    TEST_DAEMON_ERROR_PROTOCOL,
    TEST_DAEMON_ERROR_MALFORMED_REQUEST,
    TEST_DAEMON_ERROR_UNKNOWN_CONTROL,
    TEST_DAEMON_ERROR_BUSY_FOREGROUND,
    TEST_DAEMON_ERROR_BUSY_WATCH,
    TEST_DAEMON_ERROR_STOPPING,
    TEST_DAEMON_ERROR_PREFLIGHT_FAILED,
    TEST_DAEMON_ERROR_WORKER_START_FAILED,
    TEST_DAEMON_ERROR_INTERNAL,
} Test_Daemon_Error_Code;

typedef enum {
    TEST_DAEMON_LAUNCHER_CACHE_HIT = 0,
    TEST_DAEMON_LAUNCHER_CACHE_COLD,
    TEST_DAEMON_LAUNCHER_CACHE_PATH,
    TEST_DAEMON_LAUNCHER_CACHE_OVERRIDE,
} Test_Daemon_Launcher_Cache_Reason;

typedef enum {
    TEST_DAEMON_PREFLIGHT_REASON_HIT = 0,
    TEST_DAEMON_PREFLIGHT_REASON_COLD,
    TEST_DAEMON_PREFLIGHT_REASON_FINGERPRINT,
    TEST_DAEMON_PREFLIGHT_REASON_PATH,
    TEST_DAEMON_PREFLIGHT_REASON_LLVM_TOOLS,
    TEST_DAEMON_PREFLIGHT_REASON_LIB_FLAGS,
} Test_Daemon_Preflight_Reason;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint64_t request_id;
    uint32_t payload_len;
    uint32_t reserved;
} Test_Daemon_Message_Header;

typedef struct {
    Test_Daemon_Message_Header header;
    unsigned char *payload;
} Test_Daemon_Message;

typedef struct {
    uint32_t command;
    uint32_t reserved;
} Test_Daemon_Control_Request_Payload;

typedef struct {
    uint32_t kind;
    uint32_t admission_policy;
    uint32_t watch_mode;
    uint32_t client_attached;
    uint32_t module_id;
    uint32_t profile_id;
    uint32_t pending_cancel;
    uint32_t pending_rerun;
    uint32_t pending_drain;
    uint32_t force_stop;
    uint32_t kill_escalation_armed;
    uint32_t kill_escalation_sent;
    uint64_t request_id;
    uint64_t active_duration_usec;
    char module_name[TEST_DAEMON_LABEL_CAPACITY];
    char profile_name[TEST_DAEMON_LABEL_CAPACITY];
    char case_name[TEST_RUNNER_CASE_NAME_CAPACITY];
} Test_Daemon_Request_Metadata;

typedef struct {
    uint32_t daemon_state;
    uint32_t runner_ok;
    int32_t exit_code;
    uint32_t pid;
    uint32_t cache_launcher_kind;
    uint32_t cache_launcher_reason;
    uint32_t cache_preflight_reason;
    uint32_t reserved;
    Test_Daemon_Request_Metadata active_request;
    char socket_path[TEST_RUNNER_PATH_CAPACITY];
    char preserved_workspace_path[TEST_RUNNER_PATH_CAPACITY];
    char stdout_log_path[TEST_RUNNER_PATH_CAPACITY];
    char stderr_log_path[TEST_RUNNER_PATH_CAPACITY];
    char case_name[TEST_RUNNER_CASE_NAME_CAPACITY];
    char detail[TEST_RUNNER_SUMMARY_CAPACITY];
    char summary[TEST_RUNNER_SUMMARY_CAPACITY];
    char failure_summary[TEST_RUNNER_FAILURE_SUMMARY_CAPACITY];
} Test_Daemon_Result_Payload;

typedef struct {
    uint32_t code;
    uint32_t daemon_state;
    uint32_t reserved0;
    uint32_t reserved1;
    Test_Daemon_Request_Metadata active_request;
    char message[TEST_RUNNER_SUMMARY_CAPACITY];
} Test_Daemon_Error_Payload;

bool test_daemon_send_message(int fd,
                              Test_Daemon_Message_Type type,
                              uint64_t request_id,
                              const void *payload,
                              uint32_t payload_len);
bool test_daemon_recv_message(int fd, Test_Daemon_Message *out_message);
void test_daemon_message_free(Test_Daemon_Message *message);

bool test_daemon_send_hello(int fd);
bool test_daemon_expect_ack(int fd);
bool test_daemon_send_control_request(int fd,
                                      uint64_t request_id,
                                      Test_Daemon_Control_Command command);
bool test_daemon_send_run_request(int fd,
                                  uint64_t request_id,
                                  int argc,
                                  const char *const *argv);
bool test_daemon_send_watch_request(int fd,
                                    uint64_t request_id,
                                    int argc,
                                    const char *const *argv);
bool test_daemon_decode_run_request(const Test_Daemon_Message *message,
                                    int *out_argc,
                                    char ***out_argv);
bool test_daemon_decode_watch_request(const Test_Daemon_Message *message,
                                      int *out_argc,
                                      char ***out_argv);
void test_daemon_free_argv(int argc, char **argv);

bool test_daemon_result_from_runner(Test_Daemon_State daemon_state,
                                    pid_t daemon_pid,
                                    const Test_Runner_Result *runner_result,
                                    Test_Daemon_Result_Payload *out_payload);
bool test_daemon_send_result(int fd,
                             uint64_t request_id,
                             const Test_Daemon_Result_Payload *payload);
bool test_daemon_send_error(int fd,
                            uint64_t request_id,
                            Test_Daemon_Error_Code code,
                            Test_Daemon_State state,
                            const Test_Daemon_Request_Metadata *active_request,
                            const char *message);
bool test_daemon_send_error_text(int fd, uint64_t request_id, const char *message);

const char *test_daemon_state_name(Test_Daemon_State state);
const char *test_daemon_request_kind_name(Test_Daemon_Request_Kind kind);
const char *test_daemon_admission_policy_name(Test_Daemon_Admission_Policy policy);
const char *test_daemon_error_code_name(Test_Daemon_Error_Code code);
const char *test_daemon_launcher_cache_reason_name(Test_Daemon_Launcher_Cache_Reason reason);
const char *test_daemon_preflight_reason_name(Test_Daemon_Preflight_Reason reason);

#endif // TEST_DAEMON_PROTOCOL_H_
