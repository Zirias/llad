#ifndef PITHDR_DAEMON_H
#define PITHDR_DAEMON_H

#include <popt.h>

typedef int (*daemon_loop)(void *data);

extern const struct poptOption daemon_opts[];
#define DAEMON_OPTS {NULL, '\0', POPT_ARG_INCLUDE_TABLE, daemon_opts, 0, "Daemon options:", NULL},

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

extern void daemon_daemonize(const char *daemon_name,
	const daemon_loop daemon_main, void *data);
extern void daemon_print(const char *message);
extern void daemon_print_level(int level, const char *message);
extern void daemon_printf(const char *message_fmt, ...);
extern void daemon_printf_level(int level, const char *message_fmt, ...);
extern void daemon_perror(const char *message);

#endif

