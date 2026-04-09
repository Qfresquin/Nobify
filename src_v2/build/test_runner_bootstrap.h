#ifndef TEST_RUNNER_BOOTSTRAP_H_
#define TEST_RUNNER_BOOTSTRAP_H_

#define TEST_RUNNER_CORE_SOURCE "src_v2/build/test_runner_core.c"
#define TEST_DAEMON_PROTOCOL_SOURCE "src_v2/build/test_daemon_protocol.c"
#define TEST_DAEMON_CLIENT_SOURCE "src_v2/build/test_daemon_client.c"
#define TEST_DAEMON_RUNTIME_SOURCE "src_v2/build/test_daemon_runtime.c"

#define TEST_RUNNER_NOB_BOOTSTRAP_REBUILD(binary_path, source_path) \
    "cc", "-x", "c", "-D_GNU_SOURCE", "-Wall", "-Wextra", "-std=c11", "-O2", \
    "-ggdb", "-Ivendor", "-Itest_v2", "-o", binary_path, source_path, \
    TEST_RUNNER_CORE_SOURCE, TEST_DAEMON_PROTOCOL_SOURCE, TEST_DAEMON_CLIENT_SOURCE

#define TEST_RUNNER_DAEMON_BOOTSTRAP_BUILD(binary_path, source_path) \
    "cc", "-x", "c", "-D_GNU_SOURCE", "-Wall", "-Wextra", "-std=c11", "-O2", \
    "-ggdb", "-Ivendor", "-Itest_v2", "-o", binary_path, source_path, \
    TEST_RUNNER_CORE_SOURCE, TEST_DAEMON_PROTOCOL_SOURCE, TEST_DAEMON_RUNTIME_SOURCE, \
    "-L/lib/x86_64-linux-gnu", "-l:libsystemd.so.0"

#define TEST_RUNNER_REBUILD_DEPS \
    "vendor/nob.h", \
    "src_v2/build/test_runner_bootstrap.h", \
    "src_v2/build/test_runner_core.h", \
    "src_v2/build/test_runner_core.c", \
    "src_v2/build/test_runner_registry.c", \
    "src_v2/build/test_runner_exec.c", \
    "src_v2/build/test_runner_preflight.c", \
    "src_v2/build/test_runner_front_door.c", \
    "test_v2/test_fs.h", \
    "test_v2/test_workspace.h", \
    "vendor/tinydir.h"

#define TEST_RUNNER_NOB_REBUILD_DEPS \
    TEST_RUNNER_REBUILD_DEPS, \
    "src_v2/build/test_daemon_protocol.h", \
    "src_v2/build/test_daemon_protocol.c", \
    "src_v2/build/test_daemon_client.h", \
    "src_v2/build/test_daemon_client.c"

#define TEST_RUNNER_DAEMON_BUILD_DEPS \
    TEST_RUNNER_REBUILD_DEPS, \
    "src_v2/build/test_daemon_protocol.h", \
    "src_v2/build/test_daemon_protocol.c", \
    "src_v2/build/test_daemon_runtime.h", \
    "src_v2/build/test_daemon_runtime.c", \
    "src_v2/build/test_daemon_sd_event_compat.h"

#endif // TEST_RUNNER_BOOTSTRAP_H_
