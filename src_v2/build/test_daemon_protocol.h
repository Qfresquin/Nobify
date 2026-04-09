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
} Test_Daemon_Control_Command;

typedef enum {
    TEST_DAEMON_STATE_STOPPED = 0,
    TEST_DAEMON_STATE_IDLE,
    TEST_DAEMON_STATE_BUSY,
} Test_Daemon_State;

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
    uint32_t daemon_state;
    uint32_t runner_ok;
    int32_t exit_code;
    uint32_t pid;
    char socket_path[TEST_RUNNER_PATH_CAPACITY];
    char preserved_workspace_path[TEST_RUNNER_PATH_CAPACITY];
    char stdout_log_path[TEST_RUNNER_PATH_CAPACITY];
    char stderr_log_path[TEST_RUNNER_PATH_CAPACITY];
    char summary[TEST_RUNNER_SUMMARY_CAPACITY];
} Test_Daemon_Result_Payload;

typedef struct {
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
bool test_daemon_send_error_text(int fd, uint64_t request_id, const char *message);

const char *test_daemon_state_name(Test_Daemon_State state);

#endif // TEST_DAEMON_PROTOCOL_H_
