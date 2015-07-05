#ifndef LLAD_DAEMON_H
#define LLAD_DAEMON_H

/** class Daemon.
 * @file
 */

/** Static class for managing a daemon.
 * This class provides static methods for running a daemon, with an optional
 * runtime argument for not forking into background. It contains methods for
 * daemon output that normally log to the "daemon" facility of syslog. For
 * running in foreground, stderr is used instead.
 * @class Daemon "daemon.h"
 */

#include <stdarg.h>
#include <popt.h>

#include "common.h"

/** Callback for the daemon main routine.
 * @memberof Daemon
 * @param data an optional argument passed to the daemon
 */
typedef int (*daemon_loop)(void *data);

extern const struct poptOption daemon_opts[];

/** libpopt option table for Daemon.
 */
#define DAEMON_OPTS {NULL, '\0', POPT_ARG_INCLUDE_TABLE, (struct poptOption *)daemon_opts, 0, "Daemon options:", NULL},

struct level;

/** Class representing a log level.
 * @class Level "daemon.h"
 */
typedef struct level Level;

/** debugging messages.
 * @memberof Level
 * @static
 */
extern const Level * const LEVEL_DEBUG;

/** informational messages.
 * @memberof Level
 * @static
 */
extern const Level * const LEVEL_INFO;

/** messages that should be noticed.
 * @memberof Level
 * @static
 */
extern const Level * const LEVEL_NOTICE;

/** warning messages.
 * @memberof Level
 * @static
 */
extern const Level * const LEVEL_WARNING;

/** error messages.
 * @memberof Level
 * @static
 */
extern const Level * const LEVEL_ERR;

/** critical error messages.
 * @memberof Level
 * @static
 */
extern const Level * const LEVEL_CRIT;

/** alerts, immediate action required.
 * @memberof Level
 * @static
 */
extern const Level * const LEVEL_ALERT;

/** fatal errors, cannot continue.
 * @memberof Level
 * @static
 */
extern const Level * const LEVEL_EMERG;

/** Get text description for log level.
 * assumes the default level for NULL
 * @memberof Level
 * @param l the log level
 * @returns string describing the log level
 */
const char * level_str(const Level *l);

/** Get numeric representation of log level.
 * assumes the default level for NULL
 * @memberof Level
 * @param l the log level
 * @returns number of the log level
 */
int level_int(const Level *l);

/** Initialize the daemon.
 * must be called before any other daemon method
 * @memberof Daemon
 * @static
 * @param name the name of the daemon (used e.g. for logging)
 */
void Daemon_init(const char *name);

/** Daemonize (fork into background).
 * This method does everything a daemon should do at startup like forking
 * into background, becoming a session leader, closing stdio streams,
 * initializing logging and handling the pidfile
 * @memberof Daemon
 * @static
 * @param daemon_main the code the daemon should execute
 * @param data optional argument for daemon_main
 */
int Daemon_daemonize(const daemon_loop daemon_main, void *data);

/** Print message with standard log level.
 * @memberof Daemon
 * @static
 * @param message the message to print
 */
void Daemon_print(const char *message);

/** Print message with given log level.
 * @memberof Daemon
 * @static
 * @param level the log level
 * @param message the message to print
 */
void Daemon_print_level(const Level *level, const char *message);

/** Print formatted message with given log level.
 * @memberof Daemon
 * @static
 * @param message_fmt printf()-compatible format string
 * @param ... values to be formatted
 */
void Daemon_printf(const char *message_fmt, ...)
    __attribute__((format(printf, 1, 2)));

/** Print formatted message with given log level.
 * @memberof Daemon
 * @static
 * @param level the log level
 * @param message_fmt printf()-compatible format string
 * @param ... values to be formatted
 */
void Daemon_printf_level(const Level *level, const char *message_fmt, ...)
    __attribute__((format(printf, 2, 3)));

/** Print formatted message with given log level, stdargs version.
 * @memberof Daemon
 * @static
 * @param level the log level
 * @param message_fmt printf()-compatible format string
 * @param ap values to be formatted
 */
void Daemon_vprintf_level(const Level *level, const char *message_fmt,
	va_list ap);

/** Print error message.
 * @memberof Daemon
 * @static
 * @param message the error message, a description of errno is appended
 */
void Daemon_perror(const char *message);

/** the name of the daemon.
 * @memberof Daemon
 * @static
 * @returns name of the daemon
 */
const char *Daemon_name(void);

/** Call this at exit for final cleanup.
 * @memberof Daemon
 * @static
 */
void Daemon_atexit(void);

#endif

