#include "logfile.h"

#include <stdio.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "config.h"
#include "daemon.h"
#include "util.h"

struct logfile
{
    char *name;
    char *dirName;
    FILE *file;
    off_t readpos;
    Logfile *next;
};

struct logfileItor
{
    Logfile *current;
};

static Logfile *firstLog = NULL;

static Logfile *
logfile_new(const CfgLog *cl)
{
    struct stat st;
    Logfile *self = NULL;
    char *tmp = lladCloneString(cfgLog_name(cl));
    char *dirName = dirname(tmp);

    if (stat(dirName, &st) < 0)
    {
	daemon_printf_level(LEVEL_WARNING,
		"Could not stat `%s': %s", dirName, strerror(errno));
	free(tmp);
	return NULL;
    }

    if (!S_ISDIR(st.st_mode))
    {
	daemon_printf_level(LEVEL_WARNING,
		"%s: Not a directory", dirName);
	free(tmp);
	return NULL;
    }

    self = lladAlloc(sizeof(Logfile));
    self->name = lladCloneString(cfgLog_name(cl));
    self->dirName = lladCloneString(dirName);
    free(tmp);
    self->file = NULL;

    if (stat(self->name, &st) < 0)
    {
	self->readpos = 0L;
    }
    else
    {
	self->readpos = st.st_size;
    }

    self->next = NULL;
    return self;
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
	free(last->dirName);
	free(last->name);
	free(last);
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

void logfile_scan(Logfile *self)
{
}

void logfile_reset(Logfile *self)
{
    self->readpos = 0L;
}

