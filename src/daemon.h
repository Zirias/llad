#ifndef LLAD_DAEMON_H
#define LLAD_DAEMON_H

#include <stdarg.h>
#include <popt.h>

typedef int (*daemon_loop)(void *data);

extern const struct poptOption daemon_opts[];
#define DAEMON_OPTS {NULL, '\0', POPT_ARG_INCLUDE_TABLE, (struct poptOption *)daemon_opts, 0, "Daemon options:", NULL},

struct level;
typedef struct level Level;

extern const Level * const LEVEL_DEBUG;
extern const Level * const LEVEL_INFO;
extern const Level * const LEVEL_NOTICE;
extern const Level * const LEVEL_WARNING;
extern const Level * const LEVEL_ERR;
extern const Level * const LEVEL_CRIT;
extern const Level * const LEVEL_ALERT;
extern const Level * const LEVEL_EMERG;

const char * level_str(const Level *l);
int level_int(const Level *l);

void daemon_init(const char *name);
int daemon_daemonize(const daemon_loop daemon_main, void *data);
void daemon_print(const char *message);
void daemon_print_level(const Level *level, const char *message);
void daemon_printf(const char *message_fmt, ...)
    __attribute__((format(printf, 1, 2)));
void daemon_printf_level(const Level *level, const char *message_fmt, ...)
    __attribute__((format(printf, 2, 3)));
void daemon_vprintf_level(const Level *level, const char *message_fmt,
	va_list ap);
void daemon_perror(const char *message);
const char *daemon_name(void);

#endif

