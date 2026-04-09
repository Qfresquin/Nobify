#include "test_daemon_runtime.h"

#include "test_daemon_protocol.h"
#include "test_daemon_sd_event_compat.h"
#include "test_runner_core.h"

#include "nob.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef SYS_pidfd_open
#define SYS_pidfd_open 434
#endif

#ifndef EPOLLRDHUP
#define EPOLLRDHUP 0
#endif

#define TEMP_TESTS_ROOT "Temp_tests"
#define TEST_DAEMON_KILL_GRACE_USEC (5u * 1000u * 1000u)
#define TEST_DAEMON_WATCH_DEBOUNCE_USEC (250u * 1000u)
#define TEST_DAEMON_WATCH_MASK \
    (IN_CLOSE_WRITE | IN_ATTRIB | IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_MODIFY | IN_MOVE_SELF | \
     IN_MOVED_FROM | IN_MOVED_TO | IN_Q_OVERFLOW | IN_IGNORED)

typedef struct {
    bool valid;
    Test_Runner_Profile_Id profile_id;
    uint64_t fingerprint;
    char path_env[TEST_RUNNER_PATH_CAPACITY];
    char llvm_cov_env[TEST_RUNNER_PATH_CAPACITY];
    char llvm_profdata_env[TEST_RUNNER_PATH_CAPACITY];
    char use_libcurl[32];
    char use_libarchive[32];
} Test_Daemon_Preflight_Cache_Entry;

typedef struct {
    bool valid;
    char override_env[TEST_RUNNER_PATH_CAPACITY];
    char path_env[TEST_RUNNER_PATH_CAPACITY];
    char resolved_launcher[TEST_RUNNER_PATH_CAPACITY];
} Test_Daemon_Launcher_Cache;

typedef enum {
    TEST_DAEMON_WATCH_ROUTE_MATCH = 0,
    TEST_DAEMON_WATCH_ROUTE_BROAD,
    TEST_DAEMON_WATCH_ROUTE_RESCAN,
    TEST_DAEMON_WATCH_ROUTE_NO_MATCH,
} Test_Daemon_Watch_Route_Kind;

typedef struct {
    Test_Runner_Launcher_Kind launcher_kind;
    Test_Daemon_Launcher_Cache_Reason launcher_cache_reason;
    Test_Daemon_Preflight_Reason preflight_reason;
} Test_Daemon_Request_Telemetry;

typedef struct {
    int wd;
    char *path;
} Test_Daemon_Watch_Directory;

typedef struct {
    Test_Daemon_Watch_Directory *items;
    size_t count;
    size_t capacity;
} Test_Daemon_Watch_Directories;

typedef struct {
    bool active;
    bool rerun_pending;
    bool cancel_requested;
    bool rescan_required;
    bool closing;
    bool exit_after_rerun;
    bool batch_is_baseline;
    uint64_t request_id;
    uint64_t started_usec;
    int inotify_fd;
    sd_event_source *inotify_source;
    sd_event_source *client_source;
    sd_event_source *debounce_source;
    Test_Runner_Watch_Request request;
    Nob_File_Paths roots;
    Nob_File_Paths changed_paths;
    Test_Daemon_Watch_Directories watched_dirs;
    Test_Runner_Module_Id batch_modules[TEST_RUNNER_MODULE_COUNT];
    size_t batch_count;
    size_t batch_index;
} Test_Daemon_Watch_Session;

typedef struct {
    sd_event *event;
    sd_event_source *listen_source;
    sd_event_source *signal_int_source;
    sd_event_source *signal_term_source;
    sd_event_source *worker_stdout_source;
    sd_event_source *worker_stderr_source;
    sd_event_source *worker_pidfd_source;
    sd_event_source *kill_timer_source;
    int listen_fd;
    int current_client_fd;
    int worker_stdout_fd;
    int worker_stderr_fd;
    int worker_pidfd;
    pid_t worker_pid;
    int worker_exit_code;
    bool worker_exited;
    bool stdout_closed;
    bool stderr_closed;
    bool worker_cancel_requested;
    bool shutting_down;
    bool force_stop_requested;
    bool kill_escalation_sent;
    bool startup_recovered_socket;
    bool startup_recovered_pid;
    uint64_t current_request_id;
    uint64_t worker_started_usec;
    char worker_result_path[TEST_RUNNER_PATH_CAPACITY];
    char status_detail[TEST_RUNNER_SUMMARY_CAPACITY];
    Test_Daemon_State state;
    Test_Daemon_Preflight_Cache_Entry preflight_cache[TEST_RUNNER_PROFILE_COUNT];
    Test_Daemon_Launcher_Cache launcher_cache;
    Test_Daemon_Request_Telemetry current_telemetry;
    Test_Daemon_Request_Telemetry last_telemetry;
    Test_Daemon_Admission_Policy current_policy;
    Test_Runner_Request current_worker_request;
    bool current_worker_request_valid;
    Test_Runner_Result last_result;
    bool last_result_valid;
    Nob_String_Builder buffered_stdout;
    Nob_String_Builder buffered_stderr;
    Test_Daemon_Watch_Session watch;
} Test_Daemon_Server;

static int daemon_on_kill_timer(sd_event_source *source,
                                uint64_t usec,
                                void *userdata);
static bool daemon_spawn_worker(Test_Daemon_Server *server,
                                const Test_Runner_Request *request,
                                uint64_t request_id);
static bool daemon_finish_active_run(Test_Daemon_Server *server);
static void daemon_handle_client_disconnect(Test_Daemon_Server *server);
static void daemon_watch_reset_session(Test_Daemon_Server *server);
static bool daemon_begin_worker_cancel(Test_Daemon_Server *server,
                                       bool mark_watch_cancel,
                                       bool closing_session);
static Test_Runner_Profile_Id daemon_watch_profile_for_module(const Test_Daemon_Server *server,
                                                              Test_Runner_Module_Id module_id);
static void daemon_reset_request_telemetry(Test_Daemon_Server *server);
static bool daemon_send_status_result(int fd,
                                      uint64_t request_id,
                                      const Test_Daemon_Server *server,
                                      const char *summary);
static bool daemon_send_structured_error(Test_Daemon_Server *server,
                                         int fd,
                                         uint64_t request_id,
                                         Test_Daemon_Error_Code code,
                                         const char *message);
static int daemon_on_watch_inotify(sd_event_source *source,
                                   int fd,
                                   uint32_t revents,
                                   void *userdata);
static int daemon_on_watch_debounce(sd_event_source *source,
                                    uint64_t usec,
                                    void *userdata);

static uint64_t daemon_now_usec(void) {
    struct timespec ts = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

static bool daemon_copy_string(char *dst, size_t dst_size, const char *src) {
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

static void daemon_close_fd(int *fd) {
    if (!fd || *fd < 0) return;
    close(*fd);
    *fd = -1;
}

static void daemon_set_status_detail(Test_Daemon_Server *server, const char *fmt, ...) {
    va_list args;
    int n = 0;

    if (!server || !fmt) return;
    va_start(args, fmt);
    n = vsnprintf(server->status_detail, sizeof(server->status_detail), fmt, args);
    va_end(args);
    if (n < 0 || (size_t)n >= sizeof(server->status_detail)) {
        server->status_detail[0] = '\0';
    }
}

static bool daemon_watch_is_verbose(const Test_Daemon_Server *server) {
    return server && server->watch.active && server->watch.request.verbose;
}

static bool daemon_watch_is_compact(const Test_Daemon_Server *server) {
    return server && server->watch.active && !server->watch.request.verbose;
}

static void daemon_clear_buffered_worker_output(Test_Daemon_Server *server) {
    if (!server) return;
    nob_sb_free(server->buffered_stdout);
    nob_sb_free(server->buffered_stderr);
    server->buffered_stdout = (Nob_String_Builder){0};
    server->buffered_stderr = (Nob_String_Builder){0};
}

static bool daemon_buffer_worker_output(Test_Daemon_Server *server,
                                        Test_Daemon_Message_Type type,
                                        const void *data,
                                        size_t size) {
    Nob_String_Builder *buffer = NULL;

    if (!server || !data || size == 0) return true;
    buffer = type == TEST_DAEMON_MESSAGE_STDOUT ? &server->buffered_stdout : &server->buffered_stderr;
    nob_da_append_many(buffer, data, size);
    return true;
}

static bool daemon_flush_buffered_worker_output(Test_Daemon_Server *server) {
    bool ok = true;

    if (!server) return false;
    if (server->buffered_stderr.count > 0) {
        ok = test_daemon_send_message(server->current_client_fd,
                                      TEST_DAEMON_MESSAGE_STDERR,
                                      server->current_request_id,
                                      server->buffered_stderr.items,
                                      (uint32_t)server->buffered_stderr.count);
        if (!ok) goto defer;
    }
    if (server->buffered_stdout.count > 0) {
        ok = test_daemon_send_message(server->current_client_fd,
                                      TEST_DAEMON_MESSAGE_STDOUT,
                                      server->current_request_id,
                                      server->buffered_stdout.items,
                                      (uint32_t)server->buffered_stdout.count);
        if (!ok) goto defer;
    }

defer:
    if (!ok) daemon_handle_client_disconnect(server);
    daemon_clear_buffered_worker_output(server);
    return ok;
}

static Test_Daemon_State daemon_effective_state(const Test_Daemon_Server *server) {
    if (!server) return TEST_DAEMON_STATE_STOPPED;
    if (server->shutting_down) {
        if (server->worker_pid > 0 || server->watch.active) return TEST_DAEMON_STATE_DRAINING;
        return TEST_DAEMON_STATE_STOPPING;
    }
    if (server->watch.active) return TEST_DAEMON_STATE_WATCHING;
    if (server->worker_pid > 0) return TEST_DAEMON_STATE_BUSY;
    return server->state;
}

static void daemon_fill_request_names(const Test_Daemon_Server *server,
                                      Test_Daemon_Request_Metadata *out_metadata) {
    const Test_Runner_Module_Def *module = NULL;
    const Test_Runner_Profile_Def *profile = NULL;

    if (!server || !out_metadata) return;

    if (server->current_worker_request_valid) {
        if (server->current_worker_request.action == TEST_RUNNER_ACTION_RUN_MODULE) {
            module = test_runner_get_module_def(server->current_worker_request.module_id);
            if (module) (void)daemon_copy_string(out_metadata->module_name, sizeof(out_metadata->module_name), module->name);
        } else if (server->current_worker_request.action == TEST_RUNNER_ACTION_RUN_AGGREGATE) {
            (void)daemon_copy_string(out_metadata->module_name, sizeof(out_metadata->module_name), "smoke");
        }
        profile = test_runner_get_profile_def(server->current_worker_request.profile_id);
        if (profile) (void)daemon_copy_string(out_metadata->profile_name, sizeof(out_metadata->profile_name), profile->name);
        out_metadata->module_id = module ? (uint32_t)module->id : UINT32_MAX;
        out_metadata->profile_id = profile ? (uint32_t)profile->id : UINT32_MAX;
        return;
    }

    if (!server->watch.active) {
        out_metadata->module_id = UINT32_MAX;
        out_metadata->profile_id = UINT32_MAX;
        return;
    }

    if (server->watch.request.mode == TEST_RUNNER_WATCH_MODE_MODULE) {
        module = test_runner_get_module_def(server->watch.request.module_id);
        if (module) {
            out_metadata->module_id = (uint32_t)module->id;
            (void)daemon_copy_string(out_metadata->module_name, sizeof(out_metadata->module_name), module->name);
        }
        profile = test_runner_get_profile_def(daemon_watch_profile_for_module(server, server->watch.request.module_id));
        if (profile) {
            out_metadata->profile_id = (uint32_t)profile->id;
            (void)daemon_copy_string(out_metadata->profile_name, sizeof(out_metadata->profile_name), profile->name);
        }
        return;
    }

    out_metadata->module_id = UINT32_MAX;
    out_metadata->profile_id = server->watch.request.profile_explicit ? (uint32_t)server->watch.request.profile_id : UINT32_MAX;
    (void)daemon_copy_string(out_metadata->module_name, sizeof(out_metadata->module_name), "auto");
    if (server->watch.request.profile_explicit) {
        profile = test_runner_get_profile_def(server->watch.request.profile_id);
        if (profile) {
            (void)daemon_copy_string(out_metadata->profile_name, sizeof(out_metadata->profile_name), profile->name);
        }
    } else {
        (void)daemon_copy_string(out_metadata->profile_name,
                                 sizeof(out_metadata->profile_name),
                                 "module-defaults");
    }
}

static bool daemon_fill_request_metadata(const Test_Daemon_Server *server,
                                         Test_Daemon_Request_Metadata *out_metadata) {
    uint64_t now_usec = 0;

    if (!out_metadata) return false;
    *out_metadata = (Test_Daemon_Request_Metadata){0};
    if (!server) return true;

    now_usec = daemon_now_usec();
    if (server->watch.active) {
        out_metadata->kind = TEST_DAEMON_REQUEST_WATCH_SESSION;
        out_metadata->admission_policy = (uint32_t)server->current_policy;
        out_metadata->watch_mode = (uint32_t)server->watch.request.mode;
        out_metadata->client_attached = server->current_client_fd >= 0 ? 1u : 0u;
        out_metadata->pending_cancel = server->worker_cancel_requested ? 1u : 0u;
        out_metadata->pending_rerun =
            (server->watch.rerun_pending || server->watch.changed_paths.count > 0 || server->watch.rescan_required) ? 1u : 0u;
        out_metadata->pending_drain = (server->shutting_down || server->watch.closing) ? 1u : 0u;
        out_metadata->force_stop = server->force_stop_requested ? 1u : 0u;
        out_metadata->kill_escalation_armed = server->kill_timer_source ? 1u : 0u;
        out_metadata->kill_escalation_sent = server->kill_escalation_sent ? 1u : 0u;
        out_metadata->request_id = server->watch.request_id;
        if (server->worker_pid > 0 && server->worker_started_usec > 0 && now_usec > server->worker_started_usec) {
            out_metadata->active_duration_usec = now_usec - server->worker_started_usec;
        } else if (server->watch.started_usec > 0 && now_usec > server->watch.started_usec) {
            out_metadata->active_duration_usec = now_usec - server->watch.started_usec;
        }
        daemon_fill_request_names(server, out_metadata);
        return true;
    }

    if (server->worker_pid > 0 || (server->current_client_fd >= 0 && server->current_request_id != 0)) {
        out_metadata->kind = TEST_DAEMON_REQUEST_FOREGROUND_RUN;
        out_metadata->admission_policy = (uint32_t)server->current_policy;
        out_metadata->client_attached = server->current_client_fd >= 0 ? 1u : 0u;
        out_metadata->pending_cancel = server->worker_cancel_requested ? 1u : 0u;
        out_metadata->pending_drain = server->shutting_down ? 1u : 0u;
        out_metadata->force_stop = server->force_stop_requested ? 1u : 0u;
        out_metadata->kill_escalation_armed = server->kill_timer_source ? 1u : 0u;
        out_metadata->kill_escalation_sent = server->kill_escalation_sent ? 1u : 0u;
        out_metadata->request_id = server->current_request_id;
        if (server->worker_started_usec > 0 && now_usec > server->worker_started_usec) {
            out_metadata->active_duration_usec = now_usec - server->worker_started_usec;
        }
        daemon_fill_request_names(server, out_metadata);
        return true;
    }

    return true;
}

static Test_Daemon_Request_Telemetry daemon_effective_telemetry(const Test_Daemon_Server *server) {
    Test_Daemon_Request_Telemetry telemetry = {
        .launcher_kind = TEST_RUNNER_LAUNCHER_NONE,
        .launcher_cache_reason = TEST_DAEMON_LAUNCHER_CACHE_COLD,
        .preflight_reason = TEST_DAEMON_PREFLIGHT_REASON_COLD,
    };

    if (!server) return telemetry;
    if (server->watch.active || server->worker_pid > 0 || server->current_worker_request_valid) {
        return server->current_telemetry;
    }
    if (server->last_result_valid) return server->last_telemetry;
    return telemetry;
}

static bool daemon_watch_path_matches_root(const char *root, const char *path) {
    size_t root_len = 0;

    if (!root || !path) return false;
    if (strcmp(root, path) == 0) return true;
    root_len = strlen(root);
    return strncmp(path, root, root_len) == 0 && path[root_len] == '/';
}

static void daemon_file_paths_clear_owned(Nob_File_Paths *paths) {
    if (!paths) return;
    for (size_t i = 0; i < paths->count; ++i) {
        free((void *)paths->items[i]);
    }
    nob_da_free(*paths);
    paths->items = NULL;
    paths->count = 0;
    paths->capacity = 0;
}

static bool daemon_file_paths_append_unique(Nob_File_Paths *paths, const char *path) {
    char *copy = NULL;

    if (!paths || !path || path[0] == '\0') return false;
    for (size_t i = 0; i < paths->count; ++i) {
        if (strcmp(paths->items[i], path) == 0) return true;
    }

    copy = strdup(path);
    if (!copy) {
        nob_log(NOB_ERROR, "daemon failed to duplicate path %s", path);
        return false;
    }
    nob_da_append(paths, copy);
    return true;
}

static void daemon_watch_directories_clear(Test_Daemon_Watch_Directories *dirs) {
    if (!dirs) return;
    for (size_t i = 0; i < dirs->count; ++i) {
        free(dirs->items[i].path);
    }
    nob_da_free(*dirs);
    dirs->items = NULL;
    dirs->count = 0;
    dirs->capacity = 0;
}

static ssize_t daemon_watch_find_directory_index(const Test_Daemon_Watch_Directories *dirs,
                                                 int wd,
                                                 const char *path) {
    if (!dirs) return -1;
    for (size_t i = 0; i < dirs->count; ++i) {
        if (wd >= 0 && dirs->items[i].wd == wd) return (ssize_t)i;
        if (path && dirs->items[i].path && strcmp(dirs->items[i].path, path) == 0) return (ssize_t)i;
    }
    return -1;
}

static void daemon_watch_remove_directory_at(Test_Daemon_Watch_Directories *dirs, size_t index) {
    if (!dirs || index >= dirs->count) return;
    free(dirs->items[index].path);
    if (index + 1 < dirs->count) {
        memmove(&dirs->items[index],
                &dirs->items[index + 1],
                (dirs->count - index - 1) * sizeof(dirs->items[0]));
    }
    dirs->count--;
}

static int daemon_pidfd_open(pid_t pid) {
    return (int)syscall(SYS_pidfd_open, pid, 0);
}

static bool daemon_path_is_executable(const char *path) {
    return path && path[0] != '\0' && access(path, X_OK) == 0;
}

static bool daemon_find_executable_in_path(const char *name,
                                           char out_path[TEST_RUNNER_PATH_CAPACITY]) {
    const char *path_env = NULL;
    size_t temp_mark = 0;

    if (!name || !out_path) return false;
    out_path[0] = '\0';

    if (strchr(name, '/')) {
        if (!daemon_path_is_executable(name)) return false;
        return daemon_copy_string(out_path, TEST_RUNNER_PATH_CAPACITY, name);
    }

    path_env = getenv("PATH");
    if (!path_env || path_env[0] == '\0') return false;

    temp_mark = nob_temp_save();
    while (*path_env) {
        const char *sep = strchr(path_env, ':');
        size_t dir_len = sep ? (size_t)(sep - path_env) : strlen(path_env);
        const char *dir = dir_len == 0 ? "." : nob_temp_strndup(path_env, dir_len);
        const char *candidate = nob_temp_sprintf("%s/%s", dir, name);
        if (candidate && daemon_path_is_executable(candidate)) {
            bool ok = daemon_copy_string(out_path, TEST_RUNNER_PATH_CAPACITY, candidate);
            nob_temp_rewind(temp_mark);
            return ok;
        }
        if (!sep) break;
        path_env = sep + 1;
    }
    nob_temp_rewind(temp_mark);
    return false;
}

static bool daemon_prepare_runtime_root(void) {
    return nob_mkdir_if_not_exists(TEMP_TESTS_ROOT) &&
           nob_mkdir_if_not_exists(TEST_DAEMON_ROOT);
}

static bool daemon_write_pid_file(void) {
    char buffer[64] = {0};
    int n = snprintf(buffer, sizeof(buffer), "%ld\n", (long)getpid());
    if (n < 0 || (size_t)n >= sizeof(buffer)) return false;
    return nob_write_entire_file(TEST_DAEMON_PID_PATH, buffer, (size_t)n);
}

static void daemon_remove_state_files(Test_Daemon_Server *server, bool record_existing) {
    bool had_socket = nob_file_exists(TEST_DAEMON_SOCKET_PATH);
    bool had_pid = nob_file_exists(TEST_DAEMON_PID_PATH);

    (void)unlink(TEST_DAEMON_SOCKET_PATH);
    (void)unlink(TEST_DAEMON_PID_PATH);
    if (!record_existing || !server) return;

    server->startup_recovered_socket = had_socket;
    server->startup_recovered_pid = had_pid;
    if (had_socket && had_pid) {
        daemon_set_status_detail(server, "startup recovered stale socket and pid artifacts");
    } else if (had_socket) {
        daemon_set_status_detail(server, "startup recovered a stale socket artifact");
    } else if (had_pid) {
        daemon_set_status_detail(server, "startup recovered a stale pid artifact");
    }
}

static bool daemon_watch_send_text(Test_Daemon_Server *server, Test_Daemon_Message_Type type, const char *text) {
    uint64_t request_id = 0;

    if (!server || server->current_client_fd < 0 || !text) return false;
    request_id = server->watch.active ? server->watch.request_id : server->current_request_id;
    if (!test_daemon_send_message(server->current_client_fd,
                                  type,
                                  request_id,
                                  text,
                                  (uint32_t)strlen(text))) {
        daemon_handle_client_disconnect(server);
        return false;
    }
    return true;
}

static void daemon_handle_client_disconnect(Test_Daemon_Server *server) {
    if (!server) return;
    daemon_close_fd(&server->current_client_fd);
    if (server->watch.active) {
        if (server->worker_pid > 0) {
            (void)daemon_begin_worker_cancel(server, true, true);
        } else {
            daemon_watch_reset_session(server);
        }
    }
}

static bool daemon_watch_send_infof(Test_Daemon_Server *server, const char *fmt, ...) {
    char buffer[TEST_RUNNER_SUMMARY_CAPACITY * 2] = {0};
    va_list args;
    int n = 0;

    if (!fmt) return false;
    va_start(args, fmt);
    n = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (n < 0 || (size_t)n >= sizeof(buffer)) return false;
    return daemon_watch_send_text(server, TEST_DAEMON_MESSAGE_INFO, buffer);
}

static void daemon_watch_clear_batch(Test_Daemon_Server *server) {
    if (!server) return;
    server->watch.batch_count = 0;
    server->watch.batch_index = 0;
    server->watch.batch_is_baseline = false;
}

static bool daemon_watch_module_matches_changed_paths(const Test_Runner_Module_Def *module,
                                                      const Nob_File_Paths *changed_paths) {
    if (!module || !changed_paths) return false;
    for (size_t i = 0; i < changed_paths->count; ++i) {
        for (size_t j = 0; j < module->watch_root_count; ++j) {
            if (daemon_watch_path_matches_root(module->watch_roots[j], changed_paths->items[i])) {
                return true;
            }
        }
    }
    return false;
}

static bool daemon_watch_collect_auto_modules(const Nob_File_Paths *changed_paths,
                                              bool fallback_all,
                                              Test_Runner_Module_Id out_modules[TEST_RUNNER_MODULE_COUNT],
                                              size_t *out_count) {
    size_t count = 0;

    if (!out_modules || !out_count) return false;
    *out_count = 0;

    for (size_t i = 0; i < test_runner_module_count(); ++i) {
        const Test_Runner_Module_Def *module = test_runner_get_module_def((Test_Runner_Module_Id)i);
        if (!module || !module->watch_auto_eligible) continue;
        if (!fallback_all && !daemon_watch_module_matches_changed_paths(module, changed_paths)) continue;
        out_modules[count++] = module->id;
    }

    *out_count = count;
    return true;
}

static void daemon_watch_reset_session(Test_Daemon_Server *server) {
    if (!server) return;

    server->watch.inotify_source = sd_event_source_unref(server->watch.inotify_source);
    server->watch.client_source = sd_event_source_unref(server->watch.client_source);
    server->watch.debounce_source = sd_event_source_unref(server->watch.debounce_source);
    daemon_close_fd(&server->watch.inotify_fd);
    daemon_file_paths_clear_owned(&server->watch.roots);
    daemon_file_paths_clear_owned(&server->watch.changed_paths);
    daemon_watch_directories_clear(&server->watch.watched_dirs);
    daemon_clear_buffered_worker_output(server);
    server->watch = (Test_Daemon_Watch_Session){
        .inotify_fd = -1,
    };
    if (server->worker_pid <= 0) {
        daemon_close_fd(&server->current_client_fd);
        server->current_request_id = 0;
        server->state = TEST_DAEMON_STATE_IDLE;
        server->current_policy = TEST_DAEMON_POLICY_NONE;
    }
}

static bool daemon_begin_worker_cancel(Test_Daemon_Server *server,
                                       bool mark_watch_cancel,
                                       bool closing_session) {
    if (!server || server->worker_pid <= 0) return true;
    if (closing_session) server->watch.closing = true;
    if (mark_watch_cancel && !server->watch.cancel_requested) server->watch.cancel_requested = true;
    server->worker_cancel_requested = true;
    if (kill(server->worker_pid, SIGTERM) < 0 && errno != ESRCH) {
        nob_log(NOB_ERROR, "daemon failed to cancel worker %ld: %s", (long)server->worker_pid, strerror(errno));
        return false;
    }
    server->kill_timer_source = sd_event_source_unref(server->kill_timer_source);
    if (sd_event_add_time_relative(server->event,
                                   &server->kill_timer_source,
                                   CLOCK_MONOTONIC,
                                   TEST_DAEMON_KILL_GRACE_USEC,
                                   0,
                                   daemon_on_kill_timer,
                                   server) < 0) {
        kill(server->worker_pid, SIGKILL);
        server->kill_escalation_sent = true;
        daemon_set_status_detail(server, "kill escalation sent immediately after timer registration failure");
    }
    return true;
}

static void daemon_reset_worker_state(Test_Daemon_Server *server, bool close_client_fd) {
    if (!server) return;
    if (close_client_fd) daemon_close_fd(&server->current_client_fd);
    daemon_clear_buffered_worker_output(server);
    daemon_close_fd(&server->worker_stdout_fd);
    daemon_close_fd(&server->worker_stderr_fd);
    daemon_close_fd(&server->worker_pidfd);
    server->worker_pid = -1;
    server->worker_exit_code = 1;
    server->worker_exited = false;
    server->worker_cancel_requested = false;
    server->stdout_closed = true;
    server->stderr_closed = true;
    server->worker_started_usec = 0;
    server->kill_escalation_sent = false;
    server->force_stop_requested = false;
    server->current_worker_request = (Test_Runner_Request){0};
    server->current_worker_request_valid = false;
    if (close_client_fd && !server->watch.active) server->current_request_id = 0;
    server->worker_result_path[0] = '\0';
    server->state = server->watch.active ? TEST_DAEMON_STATE_WATCHING : TEST_DAEMON_STATE_IDLE;
    if (!server->watch.active) server->current_policy = TEST_DAEMON_POLICY_NONE;
    server->worker_stdout_source = sd_event_source_unref(server->worker_stdout_source);
    server->worker_stderr_source = sd_event_source_unref(server->worker_stderr_source);
    server->worker_pidfd_source = sd_event_source_unref(server->worker_pidfd_source);
    server->kill_timer_source = sd_event_source_unref(server->kill_timer_source);
    daemon_reset_request_telemetry(server);
}

static bool daemon_fill_status_payload(const Test_Daemon_Server *server,
                                       const char *summary,
                                       Test_Daemon_Result_Payload *out_payload) {
    Test_Daemon_Request_Metadata active_request = {0};
    Test_Daemon_Request_Telemetry telemetry = {0};

    if (!server || !out_payload) return false;
    *out_payload = (Test_Daemon_Result_Payload){0};
    out_payload->daemon_state = (uint32_t)daemon_effective_state(server);
    out_payload->runner_ok = 1u;
    out_payload->pid = (uint32_t)getpid();
    if (!daemon_fill_request_metadata(server, &active_request)) return false;
    out_payload->active_request = active_request;
    telemetry = daemon_effective_telemetry(server);
    out_payload->cache_launcher_kind = (uint32_t)telemetry.launcher_kind;
    out_payload->cache_launcher_reason = (uint32_t)telemetry.launcher_cache_reason;
    out_payload->cache_preflight_reason = (uint32_t)telemetry.preflight_reason;
    if (!daemon_copy_string(out_payload->socket_path, sizeof(out_payload->socket_path), TEST_DAEMON_SOCKET_PATH) ||
        !daemon_copy_string(out_payload->detail, sizeof(out_payload->detail), server->status_detail) ||
        !daemon_copy_string(out_payload->summary,
                            sizeof(out_payload->summary),
                            summary ? summary : test_daemon_state_name((Test_Daemon_State)out_payload->daemon_state))) {
        return false;
    }

    if (server->last_result_valid) {
        if (!daemon_copy_string(out_payload->preserved_workspace_path,
                                sizeof(out_payload->preserved_workspace_path),
                                server->last_result.preserved_workspace_path) ||
            !daemon_copy_string(out_payload->stdout_log_path,
                                sizeof(out_payload->stdout_log_path),
                                server->last_result.stdout_log_path) ||
            !daemon_copy_string(out_payload->stderr_log_path,
                                sizeof(out_payload->stderr_log_path),
                                server->last_result.stderr_log_path)) {
            return false;
        }
    }
    return true;
}

static bool daemon_send_status_result(int fd,
                                      uint64_t request_id,
                                      const Test_Daemon_Server *server,
                                      const char *summary) {
    Test_Daemon_Result_Payload payload = {0};
    if (!daemon_fill_status_payload(server, summary, &payload)) return false;
    return test_daemon_send_result(fd, request_id, &payload);
}

static bool daemon_send_structured_error(Test_Daemon_Server *server,
                                         int fd,
                                         uint64_t request_id,
                                         Test_Daemon_Error_Code code,
                                         const char *message) {
    Test_Daemon_Request_Metadata active_request = {0};

    (void)daemon_fill_request_metadata(server, &active_request);
    return test_daemon_send_error(fd,
                                  request_id,
                                  code,
                                  daemon_effective_state(server),
                                  &active_request,
                                  message);
}

static bool daemon_verify_peer_uid(int fd) {
    struct ucred cred = {0};
    socklen_t len = sizeof(cred);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0) {
        nob_log(NOB_ERROR, "daemon failed to read peer credentials: %s", strerror(errno));
        return false;
    }
    if (cred.uid != getuid()) {
        nob_log(NOB_ERROR, "daemon rejected peer uid %u", (unsigned)cred.uid);
        return false;
    }
    return true;
}

static void daemon_reset_request_telemetry(Test_Daemon_Server *server) {
    if (!server) return;
    server->current_telemetry = (Test_Daemon_Request_Telemetry){
        .launcher_kind = TEST_RUNNER_LAUNCHER_NONE,
        .launcher_cache_reason = TEST_DAEMON_LAUNCHER_CACHE_COLD,
        .preflight_reason = TEST_DAEMON_PREFLIGHT_REASON_COLD,
    };
}

static bool daemon_refresh_compiler_launcher(Test_Daemon_Server *server) {
    const char *override_env = getenv("NOB_TEST_COMPILER_LAUNCHER");
    const char *path_env = getenv("PATH");
    char ccache_path[TEST_RUNNER_PATH_CAPACITY] = {0};
    char sccache_path[TEST_RUNNER_PATH_CAPACITY] = {0};
    const char *candidate = NULL;
    char resolved[TEST_RUNNER_PATH_CAPACITY] = {0};
    Test_Daemon_Launcher_Cache_Reason reason = TEST_DAEMON_LAUNCHER_CACHE_COLD;
    bool same_cache = false;

    if (!server) return false;

    same_cache = server->launcher_cache.valid &&
                 strcmp(server->launcher_cache.override_env, override_env ? override_env : "") == 0 &&
                 strcmp(server->launcher_cache.path_env, path_env ? path_env : "") == 0;
    if (same_cache) {
        reason = TEST_DAEMON_LAUNCHER_CACHE_HIT;
        if (server->launcher_cache.resolved_launcher[0] != '\0') {
            setenv("NOB_TEST_COMPILER_LAUNCHER", server->launcher_cache.resolved_launcher, 1);
        } else {
            unsetenv("NOB_TEST_COMPILER_LAUNCHER");
        }
        server->current_telemetry.launcher_kind =
            test_runner_classify_launcher_kind(server->launcher_cache.resolved_launcher);
        server->current_telemetry.launcher_cache_reason = reason;
        return true;
    }

    if (server->launcher_cache.valid) {
        if (strcmp(server->launcher_cache.override_env, override_env ? override_env : "") != 0) {
            reason = TEST_DAEMON_LAUNCHER_CACHE_OVERRIDE;
        } else {
            reason = TEST_DAEMON_LAUNCHER_CACHE_PATH;
        }
    }

    candidate = test_runner_select_launcher_candidate(override_env,
                                                      daemon_find_executable_in_path("ccache", ccache_path),
                                                      daemon_find_executable_in_path("sccache", sccache_path));
    if (!candidate) {
        resolved[0] = '\0';
    } else if (override_env && override_env[0] != '\0') {
        if (!daemon_find_executable_in_path(candidate, resolved)) {
            nob_log(NOB_ERROR, "configured compiler launcher is not executable: %s", override_env);
            return false;
        }
    } else if (strcmp(candidate, "ccache") == 0) {
        if (!daemon_copy_string(resolved, sizeof(resolved), ccache_path)) return false;
    } else if (strcmp(candidate, "sccache") == 0) {
        if (!daemon_copy_string(resolved, sizeof(resolved), sccache_path)) return false;
    } else {
        nob_log(NOB_ERROR, "unexpected compiler launcher candidate: %s", candidate);
        return false;
    }

    memset(&server->launcher_cache, 0, sizeof(server->launcher_cache));
    server->launcher_cache.valid = true;
    if (!daemon_copy_string(server->launcher_cache.override_env,
                            sizeof(server->launcher_cache.override_env),
                            override_env ? override_env : "") ||
        !daemon_copy_string(server->launcher_cache.path_env,
                            sizeof(server->launcher_cache.path_env),
                            path_env ? path_env : "") ||
        !daemon_copy_string(server->launcher_cache.resolved_launcher,
                            sizeof(server->launcher_cache.resolved_launcher),
                            resolved)) {
        return false;
    }

    if (resolved[0] != '\0') setenv("NOB_TEST_COMPILER_LAUNCHER", resolved, 1);
    else unsetenv("NOB_TEST_COMPILER_LAUNCHER");
    server->current_telemetry.launcher_kind = test_runner_classify_launcher_kind(resolved);
    server->current_telemetry.launcher_cache_reason = reason;
    return true;
}

static bool daemon_ensure_preflight(Test_Daemon_Server *server, Test_Runner_Profile_Id profile_id) {
    Test_Daemon_Preflight_Cache_Entry *entry = NULL;
    uint64_t fingerprint = 0;
    const char *path_env = getenv("PATH");
    const char *llvm_cov_env = getenv("LLVM_COV");
    const char *llvm_profdata_env = getenv("LLVM_PROFDATA");
    const char *use_libcurl = getenv("NOBIFY_USE_LIBCURL");
    const char *use_libarchive = getenv("NOBIFY_USE_LIBARCHIVE");
    char resolved_cov[TEST_RUNNER_PATH_CAPACITY] = {0};
    char resolved_profdata[TEST_RUNNER_PATH_CAPACITY] = {0};
    Test_Daemon_Preflight_Reason reason = TEST_DAEMON_PREFLIGHT_REASON_COLD;
    bool needs_llvm_tools = profile_id == TEST_RUNNER_PROFILE_COVERAGE;

    if ((size_t)profile_id >= TEST_RUNNER_PROFILE_COUNT) return false;
    if (!test_runner_preflight_fingerprint(&fingerprint)) return false;
    if (!daemon_refresh_compiler_launcher(server)) return false;

    entry = &server->preflight_cache[profile_id];
    if (entry->valid) {
        if (entry->fingerprint != fingerprint) {
            reason = TEST_DAEMON_PREFLIGHT_REASON_FINGERPRINT;
        } else if (strcmp(entry->path_env, path_env ? path_env : "") != 0) {
            reason = TEST_DAEMON_PREFLIGHT_REASON_PATH;
        } else if (needs_llvm_tools &&
                   (strcmp(entry->llvm_cov_env, llvm_cov_env ? llvm_cov_env : "") != 0 ||
                    strcmp(entry->llvm_profdata_env, llvm_profdata_env ? llvm_profdata_env : "") != 0)) {
            reason = TEST_DAEMON_PREFLIGHT_REASON_LLVM_TOOLS;
        } else if (strcmp(entry->use_libcurl, use_libcurl ? use_libcurl : "") != 0 ||
                   strcmp(entry->use_libarchive, use_libarchive ? use_libarchive : "") != 0) {
            reason = TEST_DAEMON_PREFLIGHT_REASON_LIB_FLAGS;
        } else {
            reason = TEST_DAEMON_PREFLIGHT_REASON_HIT;
        }
    }

    if (reason == TEST_DAEMON_PREFLIGHT_REASON_HIT) {
        server->current_telemetry.preflight_reason = reason;
        return true;
    }

    if (profile_id == TEST_RUNNER_PROFILE_COVERAGE) {
        if (!test_runner_resolve_coverage_tools(resolved_cov, resolved_profdata)) return false;
        setenv("LLVM_COV", resolved_cov, 1);
        setenv("LLVM_PROFDATA", resolved_profdata, 1);
        llvm_cov_env = getenv("LLVM_COV");
        llvm_profdata_env = getenv("LLVM_PROFDATA");
    }

    if (!test_runner_run_preflight_for_profile(profile_id, false)) return false;

    memset(entry, 0, sizeof(*entry));
    entry->valid = true;
    entry->profile_id = profile_id;
    entry->fingerprint = fingerprint;
    server->current_telemetry.preflight_reason = reason;
    return daemon_copy_string(entry->path_env, sizeof(entry->path_env), path_env ? path_env : "") &&
           daemon_copy_string(entry->llvm_cov_env,
                              sizeof(entry->llvm_cov_env),
                              needs_llvm_tools ? (llvm_cov_env ? llvm_cov_env : "") : "") &&
           daemon_copy_string(entry->llvm_profdata_env,
                              sizeof(entry->llvm_profdata_env),
                              needs_llvm_tools ? (llvm_profdata_env ? llvm_profdata_env : "") : "") &&
           daemon_copy_string(entry->use_libcurl, sizeof(entry->use_libcurl), use_libcurl ? use_libcurl : "") &&
           daemon_copy_string(entry->use_libarchive, sizeof(entry->use_libarchive), use_libarchive ? use_libarchive : "");
}

static int daemon_compare_string_ptrs(const void *lhs, const void *rhs) {
    const char *const *a = lhs;
    const char *const *b = rhs;
    return strcmp(*a, *b);
}

static bool daemon_watch_parent_dir(const char *path, char out_dir[TEST_RUNNER_PATH_CAPACITY]) {
    const char *slash = NULL;
    size_t len = 0;

    if (!path || !out_dir) return false;
    slash = strrchr(path, '/');
    if (!slash) return daemon_copy_string(out_dir, TEST_RUNNER_PATH_CAPACITY, ".");
    len = (size_t)(slash - path);
    if (len == 0) return daemon_copy_string(out_dir, TEST_RUNNER_PATH_CAPACITY, ".");
    if (len >= TEST_RUNNER_PATH_CAPACITY) return false;
    memcpy(out_dir, path, len);
    out_dir[len] = '\0';
    return true;
}

static bool daemon_watch_add_directory(Test_Daemon_Server *server, const char *dir_path) {
    Test_Daemon_Watch_Directory entry = {0};
    int wd = -1;

    if (!server || !dir_path || server->watch.inotify_fd < 0) return false;
    if (daemon_watch_find_directory_index(&server->watch.watched_dirs, -1, dir_path) >= 0) return true;

    wd = inotify_add_watch(server->watch.inotify_fd, dir_path, TEST_DAEMON_WATCH_MASK);
    if (wd < 0) {
        if (errno == ENOENT || errno == ENOTDIR) {
            server->watch.rescan_required = true;
            return true;
        }
        nob_log(NOB_ERROR, "daemon failed to watch %s: %s", dir_path, strerror(errno));
        return false;
    }

    entry.wd = wd;
    entry.path = strdup(dir_path);
    if (!entry.path) {
        nob_log(NOB_ERROR, "daemon failed to duplicate watched directory %s", dir_path);
        return false;
    }

    nob_da_append(&server->watch.watched_dirs, entry);
    return true;
}

static bool daemon_watch_add_directory_recursive(Test_Daemon_Server *server, const char *dir_path) {
    DIR *dir = NULL;
    struct dirent *entry = NULL;
    struct stat st = {0};

    if (!server || !dir_path) return false;
    if (stat(dir_path, &st) < 0) {
        if (errno == ENOENT) {
            server->watch.rescan_required = true;
            return true;
        }
        nob_log(NOB_ERROR, "daemon failed to stat watch path %s: %s", dir_path, strerror(errno));
        return false;
    }
    if (!S_ISDIR(st.st_mode)) return daemon_watch_add_directory(server, dir_path);
    if (!daemon_watch_add_directory(server, dir_path)) return false;

    dir = opendir(dir_path);
    if (!dir) {
        nob_log(NOB_ERROR, "daemon failed to open watch directory %s: %s", dir_path, strerror(errno));
        return false;
    }

    while ((entry = readdir(dir)) != NULL) {
        char child_path[TEST_RUNNER_PATH_CAPACITY] = {0};

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        if (snprintf(child_path, sizeof(child_path), "%s/%s", dir_path, entry->d_name) >= (int)sizeof(child_path)) {
            closedir(dir);
            return false;
        }
        if (stat(child_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (!daemon_watch_add_directory_recursive(server, child_path)) {
                closedir(dir);
                return false;
            }
        }
    }

    closedir(dir);
    return true;
}

static bool daemon_watch_register_roots(Test_Daemon_Server *server) {
    server->watch.inotify_source = sd_event_source_unref(server->watch.inotify_source);
    daemon_close_fd(&server->watch.inotify_fd);
    daemon_watch_directories_clear(&server->watch.watched_dirs);

    server->watch.inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (server->watch.inotify_fd < 0) {
        nob_log(NOB_ERROR, "daemon failed to create inotify fd: %s", strerror(errno));
        return false;
    }
    if (sd_event_add_io(server->event,
                        &server->watch.inotify_source,
                        server->watch.inotify_fd,
                        EPOLLIN | EPOLLHUP | EPOLLERR,
                        daemon_on_watch_inotify,
                        server) < 0) {
        nob_log(NOB_ERROR, "daemon failed to watch inotify fd");
        return false;
    }

    for (size_t i = 0; i < server->watch.roots.count; ++i) {
        char parent_dir[TEST_RUNNER_PATH_CAPACITY] = {0};
        struct stat st = {0};
        const char *root = server->watch.roots.items[i];

        if (stat(root, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (!daemon_watch_add_directory_recursive(server, root)) return false;
            continue;
        }
        if (!daemon_watch_parent_dir(root, parent_dir)) return false;
        if (!daemon_watch_add_directory(server, parent_dir)) return false;
    }

    return true;
}

static bool daemon_watch_build_root_set(Test_Daemon_Server *server,
                                        const Test_Runner_Watch_Request *request) {
    if (!server || !request) return false;

    daemon_file_paths_clear_owned(&server->watch.roots);
    if (request->mode == TEST_RUNNER_WATCH_MODE_MODULE) {
        const Test_Runner_Module_Def *module = test_runner_get_module_def(request->module_id);
        if (!module) return false;
        for (size_t i = 0; i < module->watch_root_count; ++i) {
            if (!daemon_file_paths_append_unique(&server->watch.roots, module->watch_roots[i])) return false;
        }
        return true;
    }

    for (size_t i = 0; i < test_runner_module_count(); ++i) {
        const Test_Runner_Module_Def *module = test_runner_get_module_def((Test_Runner_Module_Id)i);
        if (!module || !module->watch_auto_eligible) continue;
        for (size_t j = 0; j < module->watch_root_count; ++j) {
            if (!daemon_file_paths_append_unique(&server->watch.roots, module->watch_roots[j])) return false;
        }
    }
    return true;
}

static Test_Runner_Profile_Id daemon_watch_profile_for_module(const Test_Daemon_Server *server,
                                                              Test_Runner_Module_Id module_id) {
    const Test_Runner_Module_Def *module = test_runner_get_module_def(module_id);
    if (!server || !module) return TEST_RUNNER_PROFILE_FAST;
    return server->watch.request.profile_explicit ? server->watch.request.profile_id : module->default_local_profile;
}

static bool daemon_watch_make_module_request(const Test_Daemon_Server *server,
                                             Test_Runner_Module_Id module_id,
                                             Test_Runner_Request *out_request) {
    if (!server || !out_request) return false;
    *out_request = (Test_Runner_Request){
        .action = TEST_RUNNER_ACTION_RUN_MODULE,
        .module_id = module_id,
        .profile_id = daemon_watch_profile_for_module(server, module_id),
        .verbose = server->watch.request.verbose,
        .skip_preflight = true,
    };
    return true;
}

static void daemon_watch_sort_changed_paths(Test_Daemon_Server *server) {
    if (!server || server->watch.changed_paths.count < 2) return;
    qsort(server->watch.changed_paths.items,
          server->watch.changed_paths.count,
          sizeof(server->watch.changed_paths.items[0]),
          daemon_compare_string_ptrs);
}

static size_t daemon_watch_auto_eligible_module_count(void) {
    size_t count = 0;
    for (size_t i = 0; i < test_runner_module_count(); ++i) {
        const Test_Runner_Module_Def *module = test_runner_get_module_def((Test_Runner_Module_Id)i);
        if (module && module->watch_auto_eligible) count++;
    }
    return count;
}

static void daemon_watch_append_path_summary(Nob_String_Builder *sb, const Nob_File_Paths *paths) {
    size_t limit = 8;

    if (!sb || !paths || paths->count == 0) {
        nob_sb_append_cstr(sb, "<none>");
        return;
    }
    if (paths->count < limit) limit = paths->count;
    for (size_t i = 0; i < limit; ++i) {
        nob_sb_append_cstr(sb, i == 0 ? "" : ", ");
        nob_sb_append_cstr(sb, paths->items[i]);
    }
    if (paths->count > limit) {
        nob_sb_append_cstr(sb, nob_temp_sprintf(" (+%zu more)", paths->count - limit));
    }
}

static void daemon_watch_append_module_summary(Nob_String_Builder *sb,
                                               const Test_Runner_Module_Id *modules,
                                               size_t module_count) {
    size_t limit = 8;

    if (!sb || !modules || module_count == 0) {
        nob_sb_append_cstr(sb, "<none>");
        return;
    }
    if (module_count < limit) limit = module_count;
    for (size_t i = 0; i < limit; ++i) {
        const Test_Runner_Module_Def *module = test_runner_get_module_def(modules[i]);
        nob_sb_append_cstr(sb, i == 0 ? "" : ", ");
        nob_sb_append_cstr(sb, module ? module->name : "<unknown>");
    }
    if (module_count > limit) {
        nob_sb_append_cstr(sb, nob_temp_sprintf(" (+%zu more)", module_count - limit));
    }
}

static bool daemon_watch_send_selected_modules(Test_Daemon_Server *server,
                                               const Test_Runner_Module_Id *modules,
                                               size_t module_count) {
    Nob_String_Builder sb = {0};
    bool ok = false;

    if (!server) return false;
    nob_sb_append_cstr(&sb, "[watch] modules:");
    for (size_t i = 0; i < module_count; ++i) {
        const Test_Runner_Module_Def *module = test_runner_get_module_def(modules[i]);
        nob_sb_append_cstr(&sb, i == 0 ? " " : ", ");
        nob_sb_append_cstr(&sb, module ? module->name : "<unknown>");
    }
    nob_da_append(&sb, '\n');
    nob_da_append(&sb, '\0');
    ok = daemon_watch_send_text(server, TEST_DAEMON_MESSAGE_INFO, sb.items ? sb.items : "");
    nob_sb_free(sb);
    return ok;
}

static Test_Daemon_Watch_Route_Kind daemon_watch_route_kind(const Test_Daemon_Server *server,
                                                            size_t module_count,
                                                            bool fallback_all) {
    size_t eligible = 0;

    if (!server || module_count == 0) return TEST_DAEMON_WATCH_ROUTE_NO_MATCH;
    if (fallback_all) return TEST_DAEMON_WATCH_ROUTE_RESCAN;
    if (server->watch.request.mode != TEST_RUNNER_WATCH_MODE_AUTO) return TEST_DAEMON_WATCH_ROUTE_MATCH;

    eligible = daemon_watch_auto_eligible_module_count();
    if (eligible > 0 && module_count * 2 >= eligible) return TEST_DAEMON_WATCH_ROUTE_BROAD;
    return TEST_DAEMON_WATCH_ROUTE_MATCH;
}

static bool daemon_watch_send_batch_overview(Test_Daemon_Server *server,
                                             const Test_Runner_Module_Id *modules,
                                             size_t module_count,
                                             bool fallback_all) {
    Nob_String_Builder sb = {0};
    Test_Daemon_Watch_Route_Kind route = daemon_watch_route_kind(server, module_count, fallback_all);
    size_t eligible = 0;
    bool ok = false;

    if (!server) return false;

    if (daemon_watch_is_verbose(server)) {
        if (fallback_all) {
            if (!daemon_watch_send_infof(server, "[watch] rescan fallback triggered\n")) return false;
        }
        if (server->watch.changed_paths.count > 0) {
            if (!daemon_watch_send_infof(server, "[watch] changed paths:\n")) return false;
            for (size_t i = 0; i < server->watch.changed_paths.count; ++i) {
                if (!daemon_watch_send_infof(server, "[watch]   %s\n", server->watch.changed_paths.items[i])) {
                    return false;
                }
            }
        }
        if (module_count > 0) return daemon_watch_send_selected_modules(server, modules, module_count);
        return daemon_watch_send_infof(server, "[watch] no routed modules for change set\n");
    }

    switch (route) {
        case TEST_DAEMON_WATCH_ROUTE_NO_MATCH:
            return daemon_watch_send_infof(server, "[watch] no routed modules for change set\n");

        case TEST_DAEMON_WATCH_ROUTE_RESCAN:
            return daemon_watch_send_infof(server, "[watch] rescan fallback: rerouting all eligible matches\n");

        case TEST_DAEMON_WATCH_ROUTE_BROAD:
            eligible = daemon_watch_auto_eligible_module_count();
            return daemon_watch_send_infof(server,
                                           "[watch] broad reroute: %zu/%zu modules\n",
                                           module_count,
                                           eligible);

        case TEST_DAEMON_WATCH_ROUTE_MATCH:
            nob_sb_append_cstr(&sb, "[watch] rerun: ");
            daemon_watch_append_path_summary(&sb, &server->watch.changed_paths);
            nob_sb_append_cstr(&sb, " -> ");
            daemon_watch_append_module_summary(&sb, modules, module_count);
            nob_sb_append_cstr(&sb, "\n");
            nob_da_append(&sb, '\0');
            ok = daemon_watch_send_text(server, TEST_DAEMON_MESSAGE_INFO, sb.items ? sb.items : "");
            nob_sb_free(sb);
            return ok;
    }

    return false;
}

static bool daemon_watch_start_next_module(Test_Daemon_Server *server);

static bool daemon_watch_handle_pending_batch(Test_Daemon_Server *server) {
    Test_Runner_Module_Id modules[TEST_RUNNER_MODULE_COUNT] = {0};
    size_t module_count = 0;
    bool fallback_all = false;

    if (!server || !server->watch.active) return false;

    daemon_watch_sort_changed_paths(server);
    fallback_all = server->watch.rescan_required;
    if (fallback_all && !daemon_watch_register_roots(server)) return false;

    if (server->watch.request.mode == TEST_RUNNER_WATCH_MODE_MODULE) {
        const Test_Runner_Module_Def *module = test_runner_get_module_def(server->watch.request.module_id);
        if (!module) return false;
        if (fallback_all || daemon_watch_module_matches_changed_paths(module, &server->watch.changed_paths)) {
            modules[module_count++] = module->id;
        }
    } else {
        if (!daemon_watch_collect_auto_modules(&server->watch.changed_paths,
                                               fallback_all,
                                               modules,
                                               &module_count)) {
            return false;
        }
    }

    server->watch.rerun_pending = false;
    if (!daemon_watch_send_batch_overview(server, modules, module_count, fallback_all)) return false;

    if (module_count == 0) {
        daemon_file_paths_clear_owned(&server->watch.changed_paths);
        server->watch.rescan_required = false;
        return true;
    }

    memcpy(server->watch.batch_modules, modules, module_count * sizeof(modules[0]));
    server->watch.batch_count = module_count;
    server->watch.batch_index = 0;
    server->watch.batch_is_baseline = false;

    daemon_file_paths_clear_owned(&server->watch.changed_paths);
    server->watch.rescan_required = false;
    return daemon_watch_start_next_module(server);
}

static bool daemon_watch_schedule_debounce(Test_Daemon_Server *server) {
    if (!server || !server->watch.active) return false;
    server->watch.debounce_source = sd_event_source_unref(server->watch.debounce_source);
    if (sd_event_add_time_relative(server->event,
                                   &server->watch.debounce_source,
                                   CLOCK_MONOTONIC,
                                   TEST_DAEMON_WATCH_DEBOUNCE_USEC,
                                   0,
                                   daemon_on_watch_debounce,
                                   server) < 0) {
        nob_log(NOB_ERROR, "daemon failed to schedule watch debounce");
        return false;
    }
    return true;
}

static bool daemon_watch_start_next_module(Test_Daemon_Server *server) {
    Test_Runner_Request request = {0};
    const Test_Runner_Module_Def *module = NULL;
    const Test_Runner_Profile_Def *profile = NULL;

    if (!server || !server->watch.active) return false;

    if (server->watch.batch_index >= server->watch.batch_count) {
        bool completed_rerun_batch = !server->watch.batch_is_baseline;
        daemon_watch_clear_batch(server);
        if (server->watch.exit_after_rerun && completed_rerun_batch) {
            (void)daemon_watch_send_infof(server, "[watch] exiting after rerun\n");
            daemon_watch_reset_session(server);
            return true;
        }
        if (server->watch.rerun_pending || server->watch.changed_paths.count > 0 || server->watch.rescan_required) {
            return daemon_watch_handle_pending_batch(server);
        }
        return true;
    }

    if (!daemon_watch_make_module_request(server,
                                          server->watch.batch_modules[server->watch.batch_index++],
                                          &request)) {
        return false;
    }
    module = test_runner_get_module_def(request.module_id);
    profile = test_runner_get_profile_def(request.profile_id);
    if (!module || !profile) return false;

    daemon_reset_request_telemetry(server);
    if (!daemon_ensure_preflight(server, request.profile_id)) {
        return daemon_send_structured_error(server,
                                            server->current_client_fd,
                                            server->watch.request_id,
                                            TEST_DAEMON_ERROR_PREFLIGHT_FAILED,
                                            "daemon preflight failed");
    }
    if (!daemon_watch_send_infof(server,
                                 "[watch] running %s (%s)\n",
                                 module->name,
                                 profile->name)) {
        return false;
    }
    if (!daemon_spawn_worker(server, &request, server->watch.request_id)) {
        return daemon_send_structured_error(server,
                                            server->current_client_fd,
                                            server->watch.request_id,
                                            TEST_DAEMON_ERROR_WORKER_START_FAILED,
                                            "watch failed to start worker");
    }
    return true;
}

static int daemon_on_watch_client(sd_event_source *source,
                                  int fd,
                                  uint32_t revents,
                                  void *userdata) {
    Test_Daemon_Server *server = userdata;

    (void)source;
    (void)fd;
    if (!(revents & (EPOLLHUP | EPOLLERR | EPOLLRDHUP))) return 0;
    if (!server || !server->watch.active) return 0;

    if (server->worker_pid > 0) {
        (void)daemon_begin_worker_cancel(server, true, true);
    } else {
        daemon_watch_reset_session(server);
        if (server->shutting_down) (void)sd_event_exit(server->event, 0);
    }
    return 0;
}

static int daemon_on_watch_debounce(sd_event_source *source,
                                    uint64_t usec,
                                    void *userdata) {
    Test_Daemon_Server *server = userdata;

    (void)source;
    (void)usec;
    if (!server || !server->watch.active) return 0;
    server->watch.debounce_source = sd_event_source_unref(server->watch.debounce_source);

    if (server->watch.changed_paths.count == 0 && !server->watch.rescan_required) return 0;

    if (server->worker_pid > 0) {
        server->watch.rerun_pending = true;
        if (!server->watch.cancel_requested) {
            (void)daemon_watch_send_infof(server, "[watch] canceling stale run after newer changes\n");
        }
        (void)daemon_begin_worker_cancel(server, true, false);
        return 0;
    }

    if (!daemon_watch_handle_pending_batch(server)) {
        daemon_watch_reset_session(server);
    }
    return 0;
}

static int daemon_on_watch_inotify(sd_event_source *source,
                                   int fd,
                                   uint32_t revents,
                                   void *userdata) {
    Test_Daemon_Server *server = userdata;
    unsigned char buffer[16384] = {0};
    bool changed = false;

    (void)source;
    if (!server || !server->watch.active) return 0;
    if (!(revents & EPOLLIN)) return 0;

    for (;;) {
        ssize_t nread = read(fd, buffer, sizeof(buffer));
        size_t offset = 0;

        if (nread < 0) {
            if (errno == EAGAIN || errno == EINTR) break;
            nob_log(NOB_ERROR, "daemon failed to read inotify events: %s", strerror(errno));
            server->watch.rescan_required = true;
            break;
        }
        if (nread == 0) break;

        while (offset + sizeof(struct inotify_event) <= (size_t)nread) {
            const struct inotify_event *event = (const struct inotify_event *)(buffer + offset);
            ssize_t dir_index = daemon_watch_find_directory_index(&server->watch.watched_dirs, event->wd, NULL);
            const char *base_dir = dir_index >= 0 ? server->watch.watched_dirs.items[dir_index].path : NULL;
            char full_path[TEST_RUNNER_PATH_CAPACITY] = {0};

            offset += sizeof(struct inotify_event) + event->len;
            if (!base_dir) {
                server->watch.rescan_required = true;
                continue;
            }
            if (event->mask & IN_Q_OVERFLOW) {
                server->watch.rescan_required = true;
                changed = true;
                continue;
            }

            if (event->len > 0 && event->name[0] != '\0') {
                if (snprintf(full_path, sizeof(full_path), "%s/%s", base_dir, event->name) >= (int)sizeof(full_path)) {
                    server->watch.rescan_required = true;
                    changed = true;
                    continue;
                }
            } else {
                if (!daemon_copy_string(full_path, sizeof(full_path), base_dir)) continue;
            }

            if ((event->mask & (IN_CREATE | IN_MOVED_TO)) && (event->mask & IN_ISDIR)) {
                if (!daemon_watch_add_directory_recursive(server, full_path)) {
                    server->watch.rescan_required = true;
                }
            }

            if (event->mask & (IN_MOVE_SELF | IN_DELETE_SELF | IN_IGNORED)) {
                server->watch.rescan_required = true;
                if (dir_index >= 0) daemon_watch_remove_directory_at(&server->watch.watched_dirs, (size_t)dir_index);
            }

            if (daemon_file_paths_append_unique(&server->watch.changed_paths, full_path)) changed = true;
        }
    }

    if (changed || server->watch.rescan_required) (void)daemon_watch_schedule_debounce(server);
    return 0;
}

static bool daemon_watch_start_session(Test_Daemon_Server *server,
                                       int fd,
                                       const Test_Daemon_Message *message) {
    Test_Runner_Watch_Request request = {0};
    char **argv = NULL;
    int argc = 0;
    bool ok = false;

    if (!server || !message) return false;
    if (!test_daemon_decode_watch_request(message, &argc, &argv)) {
        return daemon_send_structured_error(server,
                                            fd,
                                            message->header.request_id,
                                            TEST_DAEMON_ERROR_MALFORMED_REQUEST,
                                            "failed to decode watch request");
    }
    if (!test_runner_parse_watch_front_door("nob", argc, argv, &request)) {
        (void)daemon_send_structured_error(server,
                                           fd,
                                           message->header.request_id,
                                           TEST_DAEMON_ERROR_MALFORMED_REQUEST,
                                           "invalid watch request arguments");
        goto defer;
    }
    if (!daemon_watch_build_root_set(server, &request)) goto defer;

    server->watch.active = true;
    server->watch.request = request;
    server->watch.request_id = message->header.request_id;
    server->watch.started_usec = daemon_now_usec();
    server->watch.inotify_fd = -1;
    server->watch.exit_after_rerun = getenv("NOB_TEST_WATCH_EXIT_AFTER_RERUN") != NULL &&
                                     strcmp(getenv("NOB_TEST_WATCH_EXIT_AFTER_RERUN"), "1") == 0;
    server->current_client_fd = fd;
    server->current_request_id = message->header.request_id;
    server->current_worker_request = (Test_Runner_Request){0};
    server->current_worker_request_valid = false;
    server->current_policy = TEST_DAEMON_POLICY_WATCH_REPLACE_RUNNING;
    server->state = TEST_DAEMON_STATE_WATCHING;

    if (!daemon_watch_register_roots(server)) goto defer;
    if (sd_event_add_io(server->event,
                        &server->watch.client_source,
                        server->current_client_fd,
                        EPOLLHUP | EPOLLERR | EPOLLRDHUP,
                        daemon_on_watch_client,
                        server) < 0) {
        nob_log(NOB_ERROR, "daemon failed to watch active client connection");
        goto defer;
    }

    if (!test_daemon_send_message(fd, TEST_DAEMON_MESSAGE_ACK, message->header.request_id, NULL, 0)) goto defer;
    if (request.mode == TEST_RUNNER_WATCH_MODE_AUTO) {
        if (!daemon_watch_send_infof(server, "[watch] auto session started (%zu roots)\n", server->watch.roots.count)) {
            goto defer;
        }
    } else {
        const Test_Runner_Module_Def *module = test_runner_get_module_def(request.module_id);
        if (!module) goto defer;
        if (!daemon_watch_send_infof(server,
                                     "[watch] module session started for %s (%zu roots)\n",
                                     module->name,
                                     server->watch.roots.count)) {
            goto defer;
        }
        server->watch.batch_modules[0] = module->id;
        server->watch.batch_count = 1;
        server->watch.batch_index = 0;
        server->watch.batch_is_baseline = true;
        if (!daemon_watch_send_infof(server, "[watch] baseline run\n")) goto defer;
    }

    if (daemon_watch_is_verbose(server)) {
        if (!daemon_watch_send_infof(server, "[watch] roots:\n")) goto defer;
        for (size_t i = 0; i < server->watch.roots.count; ++i) {
            if (!daemon_watch_send_infof(server, "[watch]   %s\n", server->watch.roots.items[i])) goto defer;
        }
    }
    if (request.mode == TEST_RUNNER_WATCH_MODE_MODULE) {
        if (!daemon_watch_start_next_module(server)) goto defer;
    }

    ok = true;

defer:
    test_daemon_free_argv(argc, argv);
    if (!ok) daemon_watch_reset_session(server);
    return ok;
}

static bool daemon_write_worker_result_file(const char *path, const Test_Runner_Result *result) {
    if (!path || !result) return false;
    return nob_write_entire_file(path, result, sizeof(*result));
}

static bool daemon_read_worker_result_file(const char *path, Test_Runner_Result *out_result) {
    Nob_String_Builder file = {0};
    bool ok = false;

    if (!path || !out_result) return false;
    *out_result = (Test_Runner_Result){0};
    if (!nob_read_entire_file(path, &file)) goto defer;
    if (file.count != sizeof(*out_result)) {
        nob_log(NOB_ERROR, "daemon result file had unexpected size: %zu", file.count);
        goto defer;
    }
    memcpy(out_result, file.items, sizeof(*out_result));
    ok = true;

defer:
    nob_sb_free(file);
    return ok;
}

static bool daemon_should_emit_fast_path_summary(const Test_Daemon_Server *server,
                                                 const Test_Runner_Result *result,
                                                 bool watch_active) {
    if (!server || !result) return false;
    if (!watch_active) return true;
    if (daemon_watch_is_verbose(server)) return true;
    return server->current_telemetry.launcher_cache_reason != TEST_DAEMON_LAUNCHER_CACHE_HIT ||
           server->current_telemetry.preflight_reason != TEST_DAEMON_PREFLIGHT_REASON_HIT ||
           result->build_stats.objects_rebuilt > 0 ||
           result->build_stats.link_performed != 0 ||
           !result->ok;
}

static bool daemon_emit_fast_path_summary(Test_Daemon_Server *server,
                                          const Test_Runner_Result *result,
                                          bool watch_active) {
    char buffer[TEST_RUNNER_SUMMARY_CAPACITY * 2] = {0};
    int n = 0;

    if (!server || !result) return false;
    if (!daemon_should_emit_fast_path_summary(server, result, watch_active)) return true;

    n = snprintf(buffer,
                 sizeof(buffer),
                 "[daemon] fast-path: launcher=%s launcher_cache=%s preflight=%s jobs=%u rebuilt=%u reused=%u linked=%s\n",
                 test_runner_launcher_kind_name(server->current_telemetry.launcher_kind),
                 test_daemon_launcher_cache_reason_name(server->current_telemetry.launcher_cache_reason),
                 test_daemon_preflight_reason_name(server->current_telemetry.preflight_reason),
                 result->build_stats.compile_jobs,
                 result->build_stats.objects_rebuilt,
                 result->build_stats.objects_reused,
                 result->build_stats.link_performed ? "yes" : "no");
    if (n < 0 || (size_t)n >= sizeof(buffer)) return false;
    return daemon_watch_send_text(server, TEST_DAEMON_MESSAGE_INFO, buffer);
}

static bool daemon_finish_active_run(Test_Daemon_Server *server) {
    Test_Runner_Result runner_result = {0};
    Test_Daemon_Result_Payload payload = {0};
    bool have_result = false;
    bool ok = false;
    bool watch_active = false;
    bool close_client_fd = false;

    if (!server || server->current_client_fd < 0) return false;
    if (!server->worker_exited || !server->stdout_closed || !server->stderr_closed) return true;
    watch_active = server->watch.active;

    if (watch_active && server->watch.cancel_requested) {
        if (!daemon_watch_send_infof(server, "[watch] stale run aborted after newer changes\n")) {
            daemon_watch_reset_session(server);
            return false;
        }
        if (server->worker_result_path[0] != '\0') (void)unlink(server->worker_result_path);
        server->watch.cancel_requested = false;
        close_client_fd = server->watch.closing;
        daemon_reset_worker_state(server, close_client_fd);
        if (server->watch.closing) {
            daemon_watch_reset_session(server);
            if (server->shutting_down) (void)sd_event_exit(server->event, 0);
            return true;
        }
        if (server->watch.rerun_pending || server->watch.changed_paths.count > 0 || server->watch.rescan_required) {
            return daemon_watch_handle_pending_batch(server);
        }
        return true;
    }

    have_result = daemon_read_worker_result_file(server->worker_result_path, &runner_result);
    if (!have_result) {
        runner_result.ok = server->worker_exit_code == 0;
        runner_result.exit_code = server->worker_exit_code;
        (void)daemon_copy_string(runner_result.summary,
                                 sizeof(runner_result.summary),
                                 runner_result.ok ? "daemon worker completed successfully"
                                                  : "daemon worker failed");
    } else {
        runner_result.exit_code = server->worker_exit_code;
    }

    if (watch_active && daemon_watch_is_compact(server) && !runner_result.ok) {
        if (!daemon_flush_buffered_worker_output(server)) {
            daemon_watch_reset_session(server);
            return false;
        }
    } else {
        daemon_clear_buffered_worker_output(server);
    }

    if (!daemon_emit_fast_path_summary(server, &runner_result, watch_active)) {
        if (watch_active) daemon_watch_reset_session(server);
        return false;
    }

    server->last_result = runner_result;
    server->last_result_valid = true;
    server->last_telemetry = server->current_telemetry;

    if (!test_daemon_result_from_runner(daemon_effective_state(server),
                                        getpid(),
                                        &runner_result,
                                        &payload)) {
        ok = false;
    } else {
        (void)daemon_fill_request_metadata(server, &payload.active_request);
        payload.cache_launcher_kind = (uint32_t)server->current_telemetry.launcher_kind;
        payload.cache_launcher_reason = (uint32_t)server->current_telemetry.launcher_cache_reason;
        payload.cache_preflight_reason = (uint32_t)server->current_telemetry.preflight_reason;
        (void)daemon_copy_string(payload.detail, sizeof(payload.detail), server->status_detail);
        ok = test_daemon_send_result(server->current_client_fd, server->current_request_id, &payload);
    }

    if (server->worker_result_path[0] != '\0') (void)unlink(server->worker_result_path);
    close_client_fd = !watch_active;
    daemon_reset_worker_state(server, close_client_fd);
    if (watch_active) {
        if (!ok) {
            daemon_watch_reset_session(server);
        } else if (server->watch.closing) {
            daemon_watch_reset_session(server);
        } else if (server->watch.batch_index < server->watch.batch_count) {
            return daemon_watch_start_next_module(server);
        } else if (server->watch.rerun_pending || server->watch.changed_paths.count > 0 || server->watch.rescan_required) {
            return daemon_watch_handle_pending_batch(server);
        } else if (server->watch.exit_after_rerun && !server->watch.batch_is_baseline) {
            daemon_watch_reset_session(server);
        }
        if (server->shutting_down && !server->watch.active) (void)sd_event_exit(server->event, ok ? 0 : 1);
        return ok;
    }
    if (server->shutting_down) (void)sd_event_exit(server->event, ok ? 0 : 1);
    return ok;
}

static int daemon_on_worker_pipe(sd_event_source *source,
                                 int fd,
                                 uint32_t revents,
                                 void *userdata) {
    Test_Daemon_Server *server = userdata;
    unsigned char buffer[4096] = {0};
    ssize_t nread = 0;
    Test_Daemon_Message_Type type =
        (fd == server->worker_stdout_fd) ? TEST_DAEMON_MESSAGE_STDOUT : TEST_DAEMON_MESSAGE_STDERR;

    (void)source;
    if (!(revents & (EPOLLIN | EPOLLHUP | EPOLLERR))) return 0;

    nread = read(fd, buffer, sizeof(buffer));
    if (nread > 0) {
        if (daemon_watch_is_compact(server)) {
            (void)daemon_buffer_worker_output(server, type, buffer, (size_t)nread);
        } else if (server->current_client_fd >= 0) {
            if (!test_daemon_send_message(server->current_client_fd,
                                          type,
                                          server->current_request_id,
                                          buffer,
                                          (uint32_t)nread)) {
                daemon_handle_client_disconnect(server);
            }
        }
        return 0;
    }
    if (nread < 0 && (errno == EINTR || errno == EAGAIN)) return 0;

    if (fd == server->worker_stdout_fd) {
        server->worker_stdout_source = sd_event_source_unref(server->worker_stdout_source);
        daemon_close_fd(&server->worker_stdout_fd);
        server->stdout_closed = true;
    } else {
        server->worker_stderr_source = sd_event_source_unref(server->worker_stderr_source);
        daemon_close_fd(&server->worker_stderr_fd);
        server->stderr_closed = true;
    }
    if (!daemon_finish_active_run(server) && server->watch.active) {
        daemon_watch_reset_session(server);
    }
    return 0;
}

static int daemon_on_worker_pidfd(sd_event_source *source,
                                  int fd,
                                  uint32_t revents,
                                  void *userdata) {
    Test_Daemon_Server *server = userdata;
    int status = 0;

    (void)source;
    (void)fd;
    if (!(revents & (EPOLLIN | EPOLLHUP | EPOLLERR))) return 0;

    if (server->worker_pid > 0) {
        if (waitpid(server->worker_pid, &status, 0) >= 0) {
            if (WIFEXITED(status)) server->worker_exit_code = WEXITSTATUS(status);
            else if (WIFSIGNALED(status)) server->worker_exit_code = 128 + WTERMSIG(status);
            else server->worker_exit_code = 1;
        }
    }

    server->worker_exited = true;
    server->worker_pidfd_source = sd_event_source_unref(server->worker_pidfd_source);
    daemon_close_fd(&server->worker_pidfd);
    if (!daemon_finish_active_run(server) && server->watch.active) {
        daemon_watch_reset_session(server);
    }
    return 0;
}

static int daemon_on_kill_timer(sd_event_source *source,
                                uint64_t usec,
                                void *userdata) {
    Test_Daemon_Server *server = userdata;
    (void)source;
    (void)usec;
    if (server && server->worker_pid > 0) {
        kill(server->worker_pid, SIGKILL);
        server->kill_escalation_sent = true;
        daemon_set_status_detail(server,
                                 "kill escalation sent after %.1fs grace timeout",
                                 (double)TEST_DAEMON_KILL_GRACE_USEC / 1000000.0);
    }
    return 0;
}

static int daemon_on_signal(sd_event_source *source,
                            const struct signalfd_siginfo *siginfo,
                            void *userdata) {
    Test_Daemon_Server *server = userdata;

    (void)source;
    (void)siginfo;
    server->shutting_down = true;
    server->force_stop_requested = false;
    daemon_set_status_detail(server, "shutdown requested by signal");
    if (server->watch.active) server->watch.closing = true;
    if (server->worker_pid <= 0) {
        if (server->watch.active) daemon_watch_reset_session(server);
        (void)sd_event_exit(server->event, 0);
        return 0;
    }

    (void)daemon_begin_worker_cancel(server, server->watch.active, server->watch.active);
    return 0;
}

static bool daemon_spawn_worker(Test_Daemon_Server *server,
                                const Test_Runner_Request *request,
                                uint64_t request_id) {
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    pid_t pid = -1;
    int pidfd = -1;

    if (!server || !request) return false;

    if (!daemon_prepare_runtime_root()) return false;
    if (snprintf(server->worker_result_path,
                 sizeof(server->worker_result_path),
                 "%s/result-%llu.bin",
                 TEST_DAEMON_ROOT,
                 (unsigned long long)request_id) >= (int)sizeof(server->worker_result_path)) {
        nob_log(NOB_ERROR, "daemon worker result path too long");
        return false;
    }

    if (pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) {
        nob_log(NOB_ERROR, "daemon failed to create worker pipes: %s", strerror(errno));
        goto fail;
    }

    pid = fork();
    if (pid < 0) {
        nob_log(NOB_ERROR, "daemon failed to fork worker: %s", strerror(errno));
        goto fail;
    }

    if (pid == 0) {
        Test_Runner_Request child_request = *request;
        Test_Runner_Result result = {0};
        int exit_code = 1;

        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        child_request.skip_preflight = true;
        if (test_runner_execute(&child_request, &result)) exit_code = 0;
        result.exit_code = exit_code;
        (void)daemon_write_worker_result_file(server->worker_result_path, &result);
        _exit(exit_code);
    }

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    stdout_pipe[1] = -1;
    stderr_pipe[1] = -1;

    pidfd = daemon_pidfd_open(pid);
    if (pidfd < 0) {
        nob_log(NOB_ERROR, "daemon failed to open pidfd for worker %ld: %s", (long)pid, strerror(errno));
        goto fail;
    }

    server->worker_pid = pid;
    server->worker_pidfd = pidfd;
    server->worker_stdout_fd = stdout_pipe[0];
    server->worker_stderr_fd = stderr_pipe[0];
    server->worker_exited = false;
    server->stdout_closed = false;
    server->stderr_closed = false;
    server->worker_exit_code = 1;
    server->current_request_id = request_id;
    server->worker_started_usec = daemon_now_usec();
    server->current_worker_request = *request;
    server->current_worker_request_valid = true;
    server->state = TEST_DAEMON_STATE_BUSY;

    if (sd_event_add_io(server->event,
                        &server->worker_stdout_source,
                        server->worker_stdout_fd,
                        EPOLLIN | EPOLLHUP | EPOLLERR,
                        daemon_on_worker_pipe,
                        server) < 0 ||
        sd_event_add_io(server->event,
                        &server->worker_stderr_source,
                        server->worker_stderr_fd,
                        EPOLLIN | EPOLLHUP | EPOLLERR,
                        daemon_on_worker_pipe,
                        server) < 0 ||
        sd_event_add_io(server->event,
                        &server->worker_pidfd_source,
                        server->worker_pidfd,
                        EPOLLIN | EPOLLHUP | EPOLLERR,
                        daemon_on_worker_pidfd,
                        server) < 0) {
        nob_log(NOB_ERROR, "daemon failed to register worker watchers");
        goto fail;
    }

    return true;

fail:
    if (pid > 0) {
        kill(pid, SIGKILL);
        (void)waitpid(pid, NULL, 0);
    }
    daemon_close_fd(&stdout_pipe[0]);
    daemon_close_fd(&stdout_pipe[1]);
    daemon_close_fd(&stderr_pipe[0]);
    daemon_close_fd(&stderr_pipe[1]);
    daemon_close_fd(&pidfd);
    server->worker_stdout_source = sd_event_source_unref(server->worker_stdout_source);
    server->worker_stderr_source = sd_event_source_unref(server->worker_stderr_source);
    server->worker_pidfd_source = sd_event_source_unref(server->worker_pidfd_source);
    server->worker_pid = -1;
    server->worker_pidfd = -1;
    server->worker_stdout_fd = -1;
    server->worker_stderr_fd = -1;
    server->worker_started_usec = 0;
    server->current_worker_request = (Test_Runner_Request){0};
    server->current_worker_request_valid = false;
    server->state = server->watch.active ? TEST_DAEMON_STATE_WATCHING : TEST_DAEMON_STATE_IDLE;
    if (server->worker_result_path[0] != '\0') (void)unlink(server->worker_result_path);
    server->worker_result_path[0] = '\0';
    return false;
}

static bool daemon_send_admission_error(Test_Daemon_Server *server,
                                        int fd,
                                        uint64_t request_id) {
    Test_Daemon_State state = daemon_effective_state(server);

    if (!server) return false;
    if (state == TEST_DAEMON_STATE_DRAINING || state == TEST_DAEMON_STATE_STOPPING) {
        return daemon_send_structured_error(server,
                                            fd,
                                            request_id,
                                            TEST_DAEMON_ERROR_STOPPING,
                                            "daemon is draining or stopping");
    }
    if (server->watch.active) {
        return daemon_send_structured_error(server,
                                            fd,
                                            request_id,
                                            TEST_DAEMON_ERROR_BUSY_WATCH,
                                            "daemon is busy with an active watch session");
    }
    return daemon_send_structured_error(server,
                                        fd,
                                        request_id,
                                        TEST_DAEMON_ERROR_BUSY_FOREGROUND,
                                        "daemon is busy with an active foreground run");
}

static bool daemon_handle_control(Test_Daemon_Server *server,
                                  int fd,
                                  uint64_t request_id,
                                  const Test_Daemon_Control_Request_Payload *payload) {
    Test_Daemon_State state = daemon_effective_state(server);

    if (!server || !payload) return false;

    switch ((Test_Daemon_Control_Command)payload->command) {
        case TEST_DAEMON_CONTROL_PING:
            return daemon_send_status_result(fd, request_id, server, "pong");

        case TEST_DAEMON_CONTROL_STATUS:
            return daemon_send_status_result(fd,
                                             request_id,
                                             server,
                                             test_daemon_state_name(state));

        case TEST_DAEMON_CONTROL_STOP:
            if (server->shutting_down) {
                return daemon_send_status_result(fd, request_id, server, test_daemon_state_name(state));
            }
            server->shutting_down = true;
            server->force_stop_requested = false;
            if (server->watch.active) server->watch.closing = true;
            daemon_set_status_detail(server, "graceful drain requested");
            if (!daemon_send_status_result(fd,
                                           request_id,
                                           server,
                                           (server->worker_pid > 0 || server->watch.active) ? "draining" : "stopping")) {
                return false;
            }
            if (server->watch.active && server->worker_pid <= 0) daemon_watch_reset_session(server);
            if (server->worker_pid > 0) return true;
            return sd_event_exit(server->event, 0) >= 0;

        case TEST_DAEMON_CONTROL_STOP_FORCE:
            if (server->shutting_down && server->force_stop_requested) {
                return daemon_send_status_result(fd, request_id, server, test_daemon_state_name(state));
            }
            server->shutting_down = true;
            server->force_stop_requested = true;
            if (server->watch.active) server->watch.closing = true;
            daemon_set_status_detail(server, "force stop requested; SIGKILL after %.1fs if needed",
                                     (double)TEST_DAEMON_KILL_GRACE_USEC / 1000000.0);
            if (server->worker_pid > 0) {
                if (!daemon_begin_worker_cancel(server, server->watch.active, server->watch.active)) {
                    return daemon_send_structured_error(server,
                                                        fd,
                                                        request_id,
                                                        TEST_DAEMON_ERROR_INTERNAL,
                                                        "failed to cancel active daemon worker");
                }
                return daemon_send_status_result(fd, request_id, server, "force-stopping");
            }
            if (server->watch.active) daemon_watch_reset_session(server);
            if (!daemon_send_status_result(fd, request_id, server, "stopping")) {
                return false;
            }
            return sd_event_exit(server->event, 0) >= 0;
    }

    return daemon_send_structured_error(server,
                                        fd,
                                        request_id,
                                        TEST_DAEMON_ERROR_UNKNOWN_CONTROL,
                                        "unknown daemon control command");
}

static bool daemon_handle_run_request(Test_Daemon_Server *server,
                                      int fd,
                                      const Test_Daemon_Message *message) {
    char **argv = NULL;
    int argc = 0;
    Test_Runner_Request request = {0};
    bool ok = false;

    if (daemon_effective_state(server) != TEST_DAEMON_STATE_IDLE) {
        return daemon_send_admission_error(server, fd, message->header.request_id);
    }

    if (!test_daemon_decode_run_request(message, &argc, &argv)) {
        return daemon_send_structured_error(server,
                                            fd,
                                            message->header.request_id,
                                            TEST_DAEMON_ERROR_MALFORMED_REQUEST,
                                            "failed to decode run request");
    }
    if (!test_runner_parse_front_door("nob", argc, argv, &request)) {
        (void)daemon_send_structured_error(server,
                                           fd,
                                           message->header.request_id,
                                           TEST_DAEMON_ERROR_MALFORMED_REQUEST,
                                           "invalid run request arguments");
        goto defer;
    }
    server->current_policy = TEST_DAEMON_POLICY_FOREGROUND_EXCLUSIVE;
    daemon_reset_request_telemetry(server);
    if (!daemon_ensure_preflight(server, request.profile_id)) {
        (void)daemon_send_structured_error(server,
                                           fd,
                                           message->header.request_id,
                                           TEST_DAEMON_ERROR_PREFLIGHT_FAILED,
                                           "daemon preflight failed");
        goto defer;
    }

    server->current_client_fd = fd;
    if (!test_daemon_send_message(fd, TEST_DAEMON_MESSAGE_ACK, message->header.request_id, NULL, 0)) goto defer;
    if (!daemon_spawn_worker(server, &request, message->header.request_id)) {
        (void)daemon_send_structured_error(server,
                                           fd,
                                           message->header.request_id,
                                           TEST_DAEMON_ERROR_WORKER_START_FAILED,
                                           "daemon failed to start worker");
        daemon_close_fd(&server->current_client_fd);
        goto defer;
    }
    ok = true;

defer:
    test_daemon_free_argv(argc, argv);
    return ok;
}

static bool daemon_handle_watch_request(Test_Daemon_Server *server,
                                        int fd,
                                        const Test_Daemon_Message *message) {
    if (!server || !message) return false;
    if (daemon_effective_state(server) != TEST_DAEMON_STATE_IDLE) {
        return daemon_send_admission_error(server, fd, message->header.request_id);
    }
    return daemon_watch_start_session(server, fd, message);
}

static int daemon_on_listener(sd_event_source *source,
                              int fd,
                              uint32_t revents,
                              void *userdata) {
    Test_Daemon_Server *server = userdata;
    Test_Daemon_Message hello = {0};
    Test_Daemon_Message request = {0};
    int client_fd = -1;

    (void)source;
    if (!(revents & EPOLLIN)) return 0;

    client_fd = accept4(fd, NULL, NULL, SOCK_CLOEXEC);
    if (client_fd < 0) return 0;

    if (!daemon_verify_peer_uid(client_fd)) goto close_client;
    if (!test_daemon_recv_message(client_fd, &hello)) goto close_client;
    if (hello.header.type != TEST_DAEMON_MESSAGE_HELLO) {
        (void)test_daemon_send_error(client_fd,
                                     hello.header.request_id,
                                     TEST_DAEMON_ERROR_PROTOCOL,
                                     daemon_effective_state(server),
                                     NULL,
                                     "expected HELLO");
        goto close_client;
    }
    if (!test_daemon_send_message(client_fd, TEST_DAEMON_MESSAGE_ACK, hello.header.request_id, NULL, 0)) {
        goto close_client;
    }

    if (!test_daemon_recv_message(client_fd, &request)) goto close_client;
    switch ((Test_Daemon_Message_Type)request.header.type) {
        case TEST_DAEMON_MESSAGE_CONTROL_REQUEST:
            if (request.header.payload_len != sizeof(Test_Daemon_Control_Request_Payload)) {
                (void)daemon_send_structured_error(server,
                                                   client_fd,
                                                   request.header.request_id,
                                                   TEST_DAEMON_ERROR_MALFORMED_REQUEST,
                                                   "malformed control request");
                goto close_client;
            }
            (void)daemon_handle_control(server,
                                        client_fd,
                                        request.header.request_id,
                                        (const Test_Daemon_Control_Request_Payload *)request.payload);
            goto close_client;

        case TEST_DAEMON_MESSAGE_RUN_REQUEST:
            if (daemon_handle_run_request(server, client_fd, &request)) {
                client_fd = -1;
            }
            goto close_client;

        case TEST_DAEMON_MESSAGE_WATCH_REQUEST:
            if (daemon_handle_watch_request(server, client_fd, &request)) {
                client_fd = -1;
            }
            goto close_client;

        default:
            (void)daemon_send_structured_error(server,
                                               client_fd,
                                               request.header.request_id,
                                               TEST_DAEMON_ERROR_MALFORMED_REQUEST,
                                               "unsupported daemon message");
            goto close_client;
    }

close_client:
    if (client_fd >= 0) close(client_fd);
    test_daemon_message_free(&hello);
    test_daemon_message_free(&request);
    return 0;
}

static bool daemon_setup_listener(Test_Daemon_Server *server) {
    struct sockaddr_un addr = {0};

    if (!server) return false;
    if (!daemon_prepare_runtime_root()) return false;
    daemon_remove_state_files(server, true);

    server->listen_fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (server->listen_fd < 0) {
        nob_log(NOB_ERROR, "daemon failed to create socket: %s", strerror(errno));
        return false;
    }

    addr.sun_family = AF_UNIX;
    if (!daemon_copy_string(addr.sun_path, sizeof(addr.sun_path), TEST_DAEMON_SOCKET_PATH)) {
        nob_log(NOB_ERROR, "daemon socket path too long");
        return false;
    }

    if (bind(server->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        nob_log(NOB_ERROR, "daemon bind failed: %s", strerror(errno));
        return false;
    }
    if (listen(server->listen_fd, 16) < 0) {
        nob_log(NOB_ERROR, "daemon listen failed: %s", strerror(errno));
        return false;
    }
    if (!daemon_write_pid_file()) {
        nob_log(NOB_ERROR, "daemon failed to write pid file");
        return false;
    }
    return true;
}

static bool daemon_register_sources(Test_Daemon_Server *server) {
    sigset_t mask;

    if (!server) return false;
    if (sd_event_default(&server->event) < 0) {
        nob_log(NOB_ERROR, "daemon failed to create sd-event loop");
        return false;
    }
    if (sd_event_add_io(server->event,
                        &server->listen_source,
                        server->listen_fd,
                        EPOLLIN,
                        daemon_on_listener,
                        server) < 0) {
        nob_log(NOB_ERROR, "daemon failed to watch listener socket");
        return false;
    }

    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0) {
        nob_log(NOB_ERROR, "daemon failed to block signals: %s", strerror(errno));
        return false;
    }
    if (sd_event_add_signal(server->event,
                            &server->signal_int_source,
                            SIGINT,
                            daemon_on_signal,
                            server) < 0 ||
        sd_event_add_signal(server->event,
                            &server->signal_term_source,
                            SIGTERM,
                            daemon_on_signal,
                            server) < 0) {
        nob_log(NOB_ERROR, "daemon failed to register signal watchers");
        return false;
    }

    return true;
}

static void daemon_teardown(Test_Daemon_Server *server) {
    if (!server) return;
    if (server->worker_pid > 0 && !server->worker_exited) {
        kill(server->worker_pid, SIGKILL);
        (void)waitpid(server->worker_pid, NULL, 0);
    }
    daemon_reset_worker_state(server, true);
    daemon_watch_reset_session(server);
    server->listen_source = sd_event_source_unref(server->listen_source);
    server->signal_int_source = sd_event_source_unref(server->signal_int_source);
    server->signal_term_source = sd_event_source_unref(server->signal_term_source);
    server->kill_timer_source = sd_event_source_unref(server->kill_timer_source);
    daemon_close_fd(&server->listen_fd);
    server->event = sd_event_unref(server->event);
    daemon_remove_state_files(server, false);
}

int test_daemon_main(int argc, char **argv) {
    Test_Daemon_Server server = {
        .listen_fd = -1,
        .current_client_fd = -1,
        .worker_stdout_fd = -1,
        .worker_stderr_fd = -1,
        .worker_pidfd = -1,
        .worker_pid = -1,
        .watch = {
            .inotify_fd = -1,
        },
        .stdout_closed = true,
        .stderr_closed = true,
        .state = TEST_DAEMON_STATE_IDLE,
    };
    bool ok = false;

    if (argc > 1 && strcmp(argv[1], "serve") != 0) {
        nob_log(NOB_INFO, "Usage: %s serve", argv[0]);
        return 1;
    }

    daemon_reset_request_telemetry(&server);
    server.last_telemetry = server.current_telemetry;

    if (!daemon_setup_listener(&server)) goto defer;
    if (!daemon_register_sources(&server)) goto defer;

    nob_log(NOB_INFO, "[daemon] listening on %s", TEST_DAEMON_SOCKET_PATH);
    ok = sd_event_loop(server.event) >= 0;

defer:
    daemon_teardown(&server);
    return ok ? 0 : 1;
}
