#define _BSD_SOURCE
#define _POSIX_SOURCE
#include "daemon.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>

static const char *daemonName = NULL;
static int nodetach = 0;
static char *pidfile = NULL;
static int loglevel = LOG_INFO;
static int logfacility = 0;

#define PIDFILE_DEFAULT RUNSTATEDIR "/%s.pid"

struct level
{
    const int val;
};

static const Level LVDEBUG = { LOG_DEBUG };
static const Level LVINFO = { LOG_INFO };
static const Level LVNOTICE = { LOG_NOTICE };
static const Level LVWARNING = { LOG_WARNING };
static const Level LVERR = { LOG_ERR };
static const Level LVCRIT = { LOG_CRIT };
static const Level LVALERT = { LOG_ALERT };
static const Level LVEMERG = { LOG_EMERG };

const Level * const LEVEL_DEBUG = &LVDEBUG;
const Level * const LEVEL_INFO = &LVINFO;
const Level * const LEVEL_NOTICE = &LVNOTICE;
const Level * const LEVEL_WARNING = &LVWARNING;
const Level * const LEVEL_ERR = &LVERR;
const Level * const LEVEL_CRIT = &LVCRIT;
const Level * const LEVEL_ALERT = &LVALERT;
const Level * const LEVEL_EMERG = &LVEMERG;

static const char * const strlvl[] = {
    "EMG",
    "ALT",
    "CRT",
    "ERR",
    "WRN",
    "NOT",
    "INF",
    "DBG"
};

static char pidfileHelp[1024];
#define PID_HLP_PATTERN "Write daemon pid to the file specified in <path>, " \
    "defaults to " RUNSTATEDIR \
    "/%s.pid -- pass empty string to disable pidfile."

const struct poptOption daemon_opts[] = {
    {"no-detach", 'd', POPT_ARG_NONE, &nodetach, 0,
	"Do not detach from controlling tty, print to stderr instead of "
	"logging (for debugging)", NULL},
    {"pidfile", '\0', POPT_ARG_STRING, &pidfile, 0, pidfileHelp,
	"path"},
    {"loglevel", 'l', POPT_ARG_INT, &loglevel, 0,
	"Control the amout of information printed and logged. The higher the "
	"number, the more information is given. The valid range for <level> "
	"reaches from 0 (only print/log emergencies) to 7 (print/log "
	"everyting including debugging info) to 7 . The default is 6 "
	"(infos, notices, warnings and everything more important). ", "level"},
    POPT_TABLEEND
};

static int cleanupInstalled = 0;
static void
cleanup(void)
{
    free(pidfile);
}

static void
loginit(void)
{
    logfacility = LOG_DAEMON;
    openlog(daemonName, LOG_CONS | LOG_NOWAIT | LOG_PID, logfacility);
}

const char *
level_str(const Level *l)
{
    if (!l) l = LEVEL_INFO;
    return strlvl[l->val];
}

int
level_int(const Level *l)
{
    if (!l) l = LEVEL_INFO;
    return l->val;
}

void
daemon_perror(const char *message)
{
    if (loglevel < LOG_ERR) return;
    if (logfacility)
    {
	syslog(logfacility | LOG_ERR, "%s: %s", message, strerror(errno));
    }
    else
    {
	fprintf(stderr, "[ERR] %s: %s\n", message, strerror(errno));
    }
}

void
daemon_print_level(const Level *level, const char *message)
{
    if (level_int(level) > loglevel) return;
    if (logfacility)
    {
	syslog(logfacility | level_int(level), "%s", message);
    }
    else
    {
	fprintf(stderr, "[%s] %s\n", level_str(level), message);
    }
}


void
daemon_vprintf_level(const Level *level, const char *message_fmt, va_list ap)
{
    if (level_int(level) > loglevel) return;
    if (logfacility)
    {
	vsyslog(logfacility | level_int(level), message_fmt, ap);
    }
    else
    {
	fprintf(stderr, "[%s] ", level_str(level));
	vfprintf(stderr, message_fmt, ap);
	fputs("\n", stderr);
    }
}

void
daemon_printf_level(const Level *level, const char *message_fmt, ...)
{
    va_list ap;
    va_start(ap, message_fmt);
    daemon_vprintf_level(level, message_fmt, ap);
    va_end(ap);
}

void
daemon_print(const char *message)
{
    daemon_print_level(LEVEL_INFO, message);
}

void
daemon_printf(const char *message_fmt, ...)
{
    va_list ap;
    va_start(ap, message_fmt);
    daemon_vprintf_level(LEVEL_INFO, message_fmt, ap);
    va_end(ap);
}

void
daemon_init(const char *name)
{
    if (!cleanupInstalled)
    {
	cleanupInstalled = 1;
	atexit(&cleanup);
    }

    if (!name)
    {
	daemon_print_level(LEVEL_ERR, "Daemon initialization failed, "
		"no daemon name given!");
	exit(EXIT_FAILURE);
    }

    daemonName = name;
    snprintf(pidfileHelp, 1024, PID_HLP_PATTERN, name);
}

int
daemon_daemonize(const daemon_loop daemon_main, void *data)
{
    pid_t pid, sid;
    struct sigaction handler;
    const char *pfn = NULL;
    char pfnbuf[1024];
    FILE *pf = NULL;
    int rc;

    if (!daemonName)
    {
	daemon_print_level(LEVEL_CRIT, "Can't daemonize, daemon.c was not "
		"initialized! Call daemon_init() before daemon_daemonize()!");
	exit(EXIT_FAILURE);
    }

    if (!pidfile)
    {
	snprintf(pfnbuf, 1024, PIDFILE_DEFAULT, daemonName);
	pfn = pfnbuf;
    }
    else if (strlen(pidfile) > 0)
    {
	pfn = pidfile;
    }

    if (!nodetach)
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
			daemon_printf_level(LEVEL_CRIT,
				"Error removing pidfile: %s", strerror(errno));
			exit(EXIT_FAILURE);
		    }
		}
		else
		{
		    daemon_printf_level(LEVEL_ERR,
			    "%s seems to be running, check `%s'.",
			    daemonName, pfn);
		    exit(EXIT_FAILURE);
		}
	    }

	    pf = fopen(pfn, "w");

	    if (!pf)
	    {
		daemon_printf_level(LEVEL_CRIT,
			"Error opening pidfile `%s' for writing: %s",
			pfn, strerror(errno));
		exit(EXIT_FAILURE);
	    }
	}

	pid = fork();

	if (pid < 0)
	{
	    daemon_printf_level(LEVEL_CRIT,
		    "fork() failed: %s", strerror(errno));
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

	    return EXIT_SUCCESS;
	}

	if (pf) fclose(pf);
	loginit();
	umask(0);
	sid = setsid();

	if (sid < 0)
	{
	    daemon_printf_level(LEVEL_CRIT,
		    "setsid() failed: %s", strerror(errno));
	    exit(EXIT_FAILURE);
	}

	if (chdir("/") < 0)
	{
	    daemon_printf_level(LEVEL_CRIT, 
		    "chdir(\"/\") failed: %s", strerror(errno));
	    exit(EXIT_FAILURE);
	}

	memset(&handler, 0, sizeof(handler));
	handler.sa_handler = SIG_IGN;
	sigemptyset(&(handler.sa_mask));
	sigaction(SIGQUIT, &handler, NULL);
	sigaction(SIGTERM, &handler, NULL);
	sigaction(SIGINT, &handler, NULL);
	sigaction(SIGHUP, &handler, NULL);
	sigaction(SIGUSR1, &handler, NULL);
#ifndef DEBUG
	sigaction(SIGSTOP, &handler, NULL);
#endif

	daemon_print("Daemon started -- forked into background.");

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
    }
    else
    {
	daemon_print_level(LEVEL_DEBUG, "Not forking into background.");

	if (setpgid(0,0) < 0)
	{
	    daemon_print_level(LEVEL_WARNING,
		    "Unable to start process group.");
	}
    }

    rc = daemon_main(data);

    if (!nodetach && pfn) unlink(pfn);

    return rc;
}

const char *
daemon_name(void)
{
    return daemonName;
}

