#include "daemon.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <stdarg.h>
#include <unistd.h>

static int daemonize = 1;
static const char *pidfile = NULL;
static int loglevel = LEVEL_NOTICE;
static int logfacility = 0;

#define PIDFILE_DEFAULT "/var/run/%s.pid"

static const int loglvl[] = {
    LOG_DEBUG,
    LOG_INFO,
    LOG_NOTICE,
    LOG_WARNING,
    LOG_ERR,
    LOG_CRIT,
    LOG_ALERT,
    LOG_EMERG
};

static const char * const strlvl[] = {
    "DBG",
    "INF",
    "NOT",
    "WRN",
    "ERR",
    "CRT",
    "ALT",
    "EMG"
};

static void parseOpts(poptContext con,
	enum poptCallbackReason reason,
	const struct poptOption *opt,
	const char *arg, void *data)
{
    if (reason != POPT_CALLBACK_REASON_OPTION) return;
    if (opt->val == 1) daemonize = 0;
}

const struct poptOption daemon_opts[] = {
    {NULL, '\0', POPT_ARG_CALLBACK, &parseOpts, 0, NULL, NULL},
    {"no-detach", 'd', POPT_ARG_NONE, NULL, 1,
	"Do not detach from controlling tty, print to stderr instead of "
	"logging (for debugging)", NULL},
    {"pidfile", '\0', POPT_ARG_STRING, &pidfile, 0,
	"Write daemon pid to the file specified in <path>, defaults to "
	"/var/run/{daemon}.pid -- pass empty string to disable pidfile.",
	"path"},
    {"loglevel", 'l', POPT_ARG_INT, &loglevel, 0,
	"Control the amout of information printed and logged. The lower the "
	"number, the more information is given. The valid range for <level> "
	"reaches from 0 (print/log everyting including debugging info) to 7 "
	"(only print/log emergencies). The default is 2 (notices, warnings "
	"and everything more important).", "level"},
    POPT_TABLEEND
};

static void loginit(const char *daemon_name)
{
    logfacility = LOG_DAEMON;
    openlog(daemon_name, LOG_CONS | LOG_NOWAIT | LOG_PID, logfacility);
}

void daemon_perror(const char *message)
{
    if (loglevel > LEVEL_ERR) return;
    if (logfacility)
    {
	syslog(logfacility | LOG_ERR, "%s: %m", message);
    }
    else
    {
	fprintf(stderr, "[ERR] %s: %s\n", message, strerror(errno));
    }
}

void daemon_print_level(int level, const char *message)
{
    if (level < loglevel) return;
    if (logfacility)
    {
	syslog(logfacility | loglvl[level], "%s", message);
    }
    else
    {
	fprintf(stderr, "[%s] %s\n", strlvl[level], message);
    }
}


static void daemon_vprintf_level(int level, const char *message_fmt, va_list ap)
{
    if (level < loglevel) return;
    if (logfacility)
    {
	vsyslog(logfacility | loglvl[level], message_fmt, ap);
    }
    else
    {
	fprintf(stderr, "[%s] ", strlvl[level]);
	vfprintf(stderr, message_fmt, ap);
	fputs("\n", stderr);
    }
}

void daemon_printf_level(int level, const char *message_fmt, ...)
{
    va_list ap;
    va_start(ap, message_fmt);
    daemon_vprintf_level(level, message_fmt, ap);
    va_end(ap);
}

void daemon_print(const char *message)
{
    daemon_print_level(LEVEL_NOTICE, message);
}

void daemon_printf(const char *message_fmt, ...)
{
    va_list ap;
    va_start(ap, message_fmt);
    daemon_vprintf_level(LEVEL_NOTICE, message_fmt, ap);
    va_end(ap);
}

void daemon_daemonize(const char *daemon_name,
	const daemon_loop daemon_main, void *data)
{
    pid_t pid, sid;
    const char *pfn = NULL;
    char pfnbuf[1024];
    FILE *pf = NULL;
    int rc;

    if (!pidfile)
    {
	snprintf(pfnbuf, 1024, PIDFILE_DEFAULT, daemon_name);
	pfn = pfnbuf;
    }
    else if (strlen(pidfile) > 0)
    {
	pfn = pidfile;
    }

    if (daemonize)
    {
	if (pfn)
	{
	    pf = fopen(pfn, "r");
	    if (pf)
	    {
		rc = fscanf(pf, "%d", &pid);
		fclose(pf);

		if (rc < 1 || kill(pid, 0) < 0)
		{
		    daemon_printf_level(LEVEL_WARNING,
			    "Removing stale pidfile `%s'", pfn);
		    if (unlink(pfn) < 0)
		    {
			daemon_perror("Error removing pidfile");
			exit(EXIT_FAILURE);
		    }
		}
		else
		{
		    daemon_printf_level(LEVEL_ERR,
			    "%s seems to be running, check `%s'.",
			    daemon_name, pfn);
		    exit(EXIT_FAILURE);
		}
	    }

	    pf = fopen(pfn, "w");

	    if (!pf)
	    {
		daemon_printf_level(LEVEL_ERR,
			"Error opening pidfile `%s' for writing: %s",
			pfn, strerror(errno));
		exit(EXIT_FAILURE);
	    }
	}

	pid = fork();

	if (pid < 0)
	{
	    daemon_perror("fork()");
	    if (pf) fclose(pf);
	    exit(EXIT_FAILURE);
	}

	if (pid > 0)
	{
	    if (pf)
	    {
		fprintf(pf, "%d\n", pid);
		fclose(pf);
	    }

	    exit(EXIT_SUCCESS);
	}

	if (pf) fclose(pf);
	loginit(daemon_name);
	umask(0);
	sid = setsid();

	if (sid < 0)
	{
	    daemon_perror("setsid()");
	    exit(EXIT_FAILURE);
	}

	if (chdir("/") < 0)
	{
	    daemon_perror("chdir(\"/\")");
	    exit(EXIT_FAILURE);
	}

	daemon_print("Daemon started -- forked into background.");

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
    }
    else
    {
	daemon_print_level(LEVEL_DEBUG, "Not forking into background.");
    }

    rc = daemon_main(data);

    if (daemonize && pfn) unlink(pfn);

    exit(rc);
}


