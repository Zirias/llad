#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200112L
#define _XOPEN_SOURCE 500
#include "logfile.h"

#include <stdio.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>

#include "action.h"
#include "config.h"
#include "daemon.h"
#include "util.h"

#define MAX_SCAN_COMPLETE_FILE 8192
#define SCAN_BUFSIZE 4096

struct logfile
{
    char *name;
    char *dirName;
    FILE *file;
    Action *first;
    Logfile *next;
};

struct logfileItor
{
    Logfile *current;
};

static Logfile *firstLog = NULL;

static Action *
createActions(const CfgLog *cl)
{
    Action *first = NULL;
    Action *next;
    CfgActItor *i;
    const CfgAct *ca;

    i = cfgLog_cfgActItor(cl);
    while (cfgActItor_moveNext(i))
    {
	ca = cfgActItor_current(i);
	next = action_appendNew(first, ca);
	if (!first) first = next;
    }
    cfgActItor_free(i);

    return first;
}

static Logfile *
logfile_new(const CfgLog *cl)
{
    struct stat st;
    int fd;
    char *realName;
    Logfile *curr;
    char *tmp, *baseName, *dirName;
    Logfile *self = NULL;
    Action *action = createActions(cl);

    if (!action)
    {
	daemon_printf_level(LEVEL_WARNING,
		"Ignoring `%s' without actions.", cfgLog_name(cl));
	return NULL;
    }

    tmp = lladCloneString(cfgLog_name(cl));
    baseName = basename(tmp);
    dirName = realpath(dirname(tmp), NULL);

    if (!dirName)
    {
	daemon_printf_level(LEVEL_WARNING,
		"Can't get real directory of `%s': %s", tmp, strerror(errno));
	free(tmp);
	action_free(action);
	return NULL;
    }

    if (stat(dirName, &st) < 0)
    {
	daemon_printf_level(LEVEL_WARNING,
		"Could not stat `%s': %s", dirName, strerror(errno));
	free(dirName);
	free(tmp);
	action_free(action);
	return NULL;
    }

    if (!S_ISDIR(st.st_mode))
    {
	daemon_printf_level(LEVEL_WARNING,
		"%s: Not a directory", dirName);
	free(dirName);
	free(tmp);
	action_free(action);
	return NULL;
    }

    realName = lladAlloc(strlen(dirName) + strlen(baseName) + 2);
    strcpy(realName, dirName);
    strcat(realName, "/");
    strcat(realName, baseName);

    curr = firstLog;
    while (curr)
    {
	if (!strcmp(curr->name, realName))
	{
	    free(realName);
	    free(dirName);
	    free(tmp);
	    if (curr->first)
	    {
		action_append(curr->first, action);
	    }
	    else
	    {
		curr->first = action;
	    }
	    return NULL;
	}
	curr = curr->next;
    }

    self = lladAlloc(sizeof(Logfile));
    self->name = realName;
    self->dirName = dirName;
    free(tmp);
    self->file = NULL;

    if ((self->file = fopen(self->name, "r")))
    {
	fd = fileno(self->file);
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
	fseeko(self->file, 0L, SEEK_END);
    }
    else
    {
	daemon_printf_level(LEVEL_NOTICE,
		"Could not open `%s': %s", self->name, strerror(errno));
    }

    self->next = NULL;
    self->first = action;
    return self;
}

static void
logfile_free(Logfile *self)
{
    if (self->file) fclose(self->file);
    action_free(self->first);
    free(self->dirName);
    free(self->name);
    free(self);
}

void
LogfileList_done(void)
{
    Logfile *curr, *last;

    curr = firstLog;
    while (curr)
    {
	last = curr;
	curr = last->next;
	logfile_free(last);
    }

    firstLog = NULL;
}

void
LogfileList_init(void)
{
    Logfile *curr, *next;
    CfgLogItor *li;
    const CfgLog *cl;

    if (firstLog) LogfileList_done();

    curr = NULL;

    li = Config_cfgLogItor();
    while (cfgLogItor_moveNext(li))
    {
	cl = cfgLogItor_current(li);
	next = logfile_new(cl);
	if (next)
	{
	    if (curr)
	    {
		curr->next = next;
		curr = next;
	    }
	    else
	    {
		curr = next;
		firstLog = next;
	    }
	}
    }
    cfgLogItor_free(li);
}

LogfileItor *
LogfileList_itor(void)
{
    LogfileItor *self = lladAlloc(sizeof(LogfileItor));
    self->current = NULL;
    return self;
}

Logfile *
logfileItor_current(const LogfileItor *self)
{
    return self->current;
}

int
logfileItor_moveNext(LogfileItor *self)
{
    if (self->current) self->current = self->current->next;
    else self->current = firstLog;
    return (self->current != NULL);
}

void
logfileItor_free(LogfileItor *self)
{
    free(self);
}

const char *
logfile_name(const Logfile *self)
{
    return self->name;
}

const char *
logfile_dirName(const Logfile *self)
{
    return self->dirName;
}

void logfile_scan(Logfile *self, int reopen)
{
    char buf[SCAN_BUFSIZE];
    struct stat st;
    int fd;

    if (reopen && self->file)
    {
	daemon_printf_level(LEVEL_NOTICE, "Reopening %s", self->name);
	fclose(self->file);
	self->file = NULL;
    }

    if (!self->file)
    {
	self->file = fopen(self->name, "r");
	if (!self->file)
	{
	    daemon_printf_level(LEVEL_WARNING,
		    "Could not open `%s': %s", self->name, strerror(errno));
	    return;
	}
	fd = fileno(self->file);
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
	fseeko(self->file, 0L, SEEK_END);
	if (ftello(self->file) <= MAX_SCAN_COMPLETE_FILE)
	{
	    rewind(self->file);
	}
	else return;
    }
    else
    {
	fstat(fileno(self->file), &st);
	if (st.st_size < ftello(self->file))
	{
	    daemon_printf_level(LEVEL_NOTICE,
		    "%s: truncation detected", self->name);
	    fclose(self->file);
	    self->file = fopen(self->name, "r");
	    if (!self->file)
	    {
		daemon_printf_level(LEVEL_WARNING,
			"Could not open `%s': %s", self->name, strerror(errno));
		return;
	    }
	    fd = fileno(self->file);
	    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
	    fseeko(self->file, 0L, SEEK_END);
	    return;
	}
    }
    while (fgets(buf, SCAN_BUFSIZE, self->file))
    {

	daemon_printf_level(LEVEL_DEBUG,
		"[logfile.c] [%s] got line: %s", self->name, buf);
	action_matchAndExecChain(self->first, self->name, buf);
    }

    if (errno != EWOULDBLOCK && errno != EAGAIN && errno != ENOENT)
    {
	daemon_printf_level(LEVEL_WARNING,
		"Can't read from `%s': %s", self->name, strerror(errno));
    }
}

