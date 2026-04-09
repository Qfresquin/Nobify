#ifndef TEST_DAEMON_SD_EVENT_COMPAT_H_
#define TEST_DAEMON_SD_EVENT_COMPAT_H_

#include <signal.h>
#include <stdint.h>
#include <sys/signalfd.h>
#include <time.h>

typedef struct sd_event sd_event;
typedef struct sd_event_source sd_event_source;

typedef int (*sd_event_io_handler_t)(sd_event_source *source,
                                     int fd,
                                     uint32_t revents,
                                     void *userdata);
typedef int (*sd_event_signal_handler_t)(sd_event_source *source,
                                         const struct signalfd_siginfo *siginfo,
                                         void *userdata);
typedef int (*sd_event_time_handler_t)(sd_event_source *source,
                                       uint64_t usec,
                                       void *userdata);

enum {
    SD_EVENT_OFF = 0,
    SD_EVENT_ON = 1,
    SD_EVENT_ONESHOT = 2,
};

int sd_event_default(sd_event **event);
sd_event *sd_event_unref(sd_event *event);
int sd_event_loop(sd_event *event);
int sd_event_exit(sd_event *event, int code);
int sd_event_now(sd_event *event, clockid_t clock, uint64_t *ret);
int sd_event_add_io(sd_event *event,
                    sd_event_source **source,
                    int fd,
                    uint32_t events,
                    sd_event_io_handler_t callback,
                    void *userdata);
int sd_event_add_signal(sd_event *event,
                        sd_event_source **source,
                        int sig,
                        sd_event_signal_handler_t callback,
                        void *userdata);
int sd_event_add_time_relative(sd_event *event,
                               sd_event_source **source,
                               clockid_t clock,
                               uint64_t usec,
                               uint64_t accuracy,
                               sd_event_time_handler_t callback,
                               void *userdata);
int sd_event_source_set_enabled(sd_event_source *source, int enabled);
sd_event_source *sd_event_source_unref(sd_event_source *source);

#endif // TEST_DAEMON_SD_EVENT_COMPAT_H_
