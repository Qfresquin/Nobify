#ifndef TEST_DAEMON_CLIENT_H_
#define TEST_DAEMON_CLIENT_H_

#include "test_daemon_protocol.h"

#include <stdbool.h>
#include <sys/types.h>

typedef struct {
    Test_Daemon_State state;
    pid_t pid;
    char socket_path[TEST_RUNNER_PATH_CAPACITY];
    char summary[TEST_RUNNER_SUMMARY_CAPACITY];
} Test_Daemon_Status;

bool test_daemon_client_run_request(const Test_Runner_Request *request,
                                    Test_Runner_Result *out_result);
bool test_daemon_client_watch(const Test_Runner_Watch_Request *request);
bool test_daemon_client_query_status(Test_Daemon_Status *out_status);
bool test_daemon_client_stop(Test_Daemon_Status *out_status);
bool test_daemon_client_ping(void);
bool test_daemon_client_cleanup_stale_artifacts(void);

#endif // TEST_DAEMON_CLIENT_H_
