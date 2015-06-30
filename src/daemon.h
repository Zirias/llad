#ifndef LLAD_DAEMON_H
#define LLAD_DAEMON_H

#include <popt.h>

typedef int (*daemon_loop)(void *data);

const struct poptOption daemon_opts[];
#define DAEMON_OPTS {NULL, '\0', POPT_ARG_INCLUDE_TABLE, (struct poptOption *)daemon_opts, 0, "Daemon options:", NULL},

enum daemon_levels
{
    LEVEL_DEBUG,
    LEVEL_INFO,
    LEVEL_NOTICE,
    LEVEL_WARNING,
    LEVEL_ERR,
    LEVEL_CRIT,
    LEVEL_ALERT,
    LEVEL_EMERG
};

void daemon_init(const char *name);
int daemon_daemonize(const daemon_loop daemon_main, void *data);
void daemon_print(const char *message);
void daemon_print_level(int level, const char *message);
void daemon_printf(const char *message_fmt, ...)
    __attribute__((format(printf, 1, 2)));
void daemon_printf_level(int level, const char *message_fmt, ...)
    __attribute__((format(printf, 2, 3)));
void daemon_perror(const char *message);

#endif

