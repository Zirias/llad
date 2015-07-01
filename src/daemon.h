#ifndef LLAD_DAEMON_H
#define LLAD_DAEMON_H

#include <popt.h>

typedef int (*daemon_loop)(void *data);

extern const struct poptOption daemon_opts[];
#define DAEMON_OPTS {NULL, '\0', POPT_ARG_INCLUDE_TABLE, (struct poptOption *)daemon_opts, 0, "Daemon options:", NULL},

struct level;
typedef struct level LEVEL;

extern const LEVEL * const LEVEL_DEBUG;
extern const LEVEL * const LEVEL_INFO;
extern const LEVEL * const LEVEL_NOTICE;
extern const LEVEL * const LEVEL_WARNING;
extern const LEVEL * const LEVEL_ERR;
extern const LEVEL * const LEVEL_CRIT;
extern const LEVEL * const LEVEL_ALERT;
extern const LEVEL * const LEVEL_EMERG;

void daemon_init(const char *name);
int daemon_daemonize(const daemon_loop daemon_main, void *data);
void daemon_print(const char *message);
void daemon_print_level(const LEVEL *level, const char *message);
void daemon_printf(const char *message_fmt, ...)
    __attribute__((format(printf, 1, 2)));
void daemon_printf_level(const LEVEL *level, const char *message_fmt, ...)
    __attribute__((format(printf, 2, 3)));
void daemon_perror(const char *message);
const char *daemon_name(void);

#endif

