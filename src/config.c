#include "config.h"

#include <string.h>

#include "util.h"
#include "daemon.h"

#define DEFAULT_CFG "/etc/llad/llad.conf"
const char *configFile = NULL;

const struct poptOption config_opts[] = {
    {"config", 'c', POPT_ARG_STRING, &configFile, 1,
	"Load config from <path> instead of the default " DEFAULT_CFG, "path"},
    POPT_TABLEEND
};

struct cfgLog {
    char *name;
    CfgAct *first;
    CfgLog *next;
};

struct cfgLogItor {
    CfgLog *current;
};

struct cfgAct {
    char *name;
    char *pattern;
    char *command;
    CfgAct *next;
};

struct cfgActItor {
    const CfgLog *container;
    CfgAct *current;
};

static CfgLog *firstCfgLog = NULL;

static char *
nextLine(char *buf, FILE *cfg, int fullLine)
{
    char *ptr;
    
    while (fgets(buf, 1024, cfg))
    {
	ptr = buf;
	if (fullLine) return ptr;
	while (*ptr == ' ' || *ptr == '\t')
	{
	    ++ptr;
	}
	if (*ptr == '\r' || *ptr == '\n' || *ptr == '\0'
		|| *ptr == ';' || *ptr == '#')
	{
	    continue;
	}
	return ptr;
    }
    return NULL;
}

static char *
parseWord(char **pos)
{
    enum qst
    {
	Q_NORMAL,
	Q_QUOTE,
	Q_DBLQUOTE
    };

    struct state
    {
	enum qst qst;
	int esc;
	char buf[1024];
	char *bufptr;
    };

    char *word = NULL;
    static int initialized = 0;
    static struct state st;

    if (!initialized)
    {
	initialized = 1;
	memset(&st, 0, sizeof(struct state));
	st.bufptr = st.buf;
    }

    while (**pos && st.bufptr < st.buf+1023)
    {
	if (st.esc)
	{
	    *st.bufptr++ = **pos;
	    ++*pos;
	    st.esc = 0;
	}
	else if (st.qst)
	{
	    if (**pos == '\\' &&
		    ((st.qst == Q_QUOTE && *(*pos+1) == '\'') ||
		     (st.qst == Q_DBLQUOTE && *(*pos+1) == '"')))
	    {
		++*pos;
		st.esc = 1;
	    }
	    else if ((st.qst == Q_QUOTE && **pos == '\'') ||
		(st.qst == Q_DBLQUOTE && **pos == '"'))
	    {
		++*pos;
		st.qst = Q_NORMAL;
	    }
	    else
	    {
		*st.bufptr++ = **pos;
		++*pos;
	    }
	}
	else if (**pos == '\\')
	{
	    ++*pos;
	    st.esc = 1;
	}
	else if (**pos == '\'')
	{
	    ++*pos;
	    st.qst = Q_QUOTE;
	}
	else if (**pos == '"')
	{
	    ++*pos;
	    st.qst = Q_DBLQUOTE;
	}
	else if (**pos == ' ' || **pos == '\t' || **pos == '=' || **pos == '{'
		|| **pos == '}' || **pos == '\r' || **pos == '\n')
	{
	    if (st.bufptr == st.buf)
	    {
		++*pos;
	    }
	    else
	    {
		word = lladCloneString(st.buf);
		daemon_printf_level(LEVEL_DEBUG,
			"[config.c] parseWord(): found `%s'", word);
		memset(&st, 0, sizeof(struct state));
		st.bufptr = st.buf;
		return word;
	    }
	}
	else
	{
	    *st.bufptr++ = **pos;
	    ++*pos;
	}
    }
    if (**pos)
    {
	daemon_print_level(LEVEL_ERR, "Buffer full reading configuration.");
	exit(EXIT_FAILURE);
    }

    daemon_print_level(LEVEL_DEBUG, "[config.c] parseWord(): incomplete");
    return NULL;
}

static void
skipWithWhitespace(char **pos)
{
    ++*pos;
    while (**pos == ' ' || **pos == '\t' || **pos == '\r' || **pos == '\n')
    {
	++*pos;
    }
}

static int
parseActions(CfgLog *log, char *line)
{
    enum step
    {
	ST_START,
	ST_NAME,
	ST_NAME_EQUALS,
	ST_BLOCK,
	ST_BLOCK_NAME,
	ST_BLOCK_VALUE
    };

    struct state
    {
	CfgLog *lastLog;
	char *name;
	char *pattern;
	char *command;
	char *blockname;
	char **blockval;
	CfgAct *currentAction;
	enum step step;
    };

    static int initialized = 0;
    static struct state st;
    char *ptr;
    CfgAct *nextAction;

    if (!initialized)
    {
	initialized = 1;
	memset(&st, 0, sizeof(struct state));
	st.lastLog = log;
    }
    else if (log != st.lastLog)
    {
	free(st.name);
	free(st.pattern);
	free(st.command);
	memset(&st, 0, sizeof(struct state));
	st.lastLog = log;
    }

    ptr = line;

    while (*ptr)
    {
	switch (st.step)
	{
	    case ST_START:
		if ((st.name = parseWord(&ptr)))
		{
		    daemon_printf_level(LEVEL_INFO,
			    "[config.c] Found action: %s", st.name);
		    st.step = ST_NAME;
		}
		else return 1;
		break;
	    
	    case ST_NAME:
		if (*ptr == '=')
		{
		    st.step = ST_NAME_EQUALS;
		}
		skipWithWhitespace(&ptr);
		break;

	    case ST_NAME_EQUALS:
		if (*ptr == '{')
		{
		    st.step = ST_BLOCK;
		}
		skipWithWhitespace(&ptr);
		break;

	    case ST_BLOCK:
		if (*ptr == '}')
		{
		    st.step = ST_START;
		    if (st.pattern && st.command)
		    {
			nextAction = lladAlloc(sizeof(CfgAct));
			if (st.currentAction)
			{
			    st.currentAction->next = nextAction;
			}
			else
			{
			    log->first = nextAction;
			}
			nextAction->name = st.name;
			nextAction->pattern = st.pattern;
			nextAction->command = st.command;
			nextAction->next = NULL;
			daemon_printf_level(LEVEL_INFO,
				"[config.c] pattern: `%s' command: `%s'",
				st.pattern, st.command);
		    }
		    else
		    {
			nextAction = st.currentAction;
			daemon_printf_level(LEVEL_WARNING,
				"[config.c] Ignoring incomplete action `%s'",
				st.name);
		    }
		    memset(&st, 0, sizeof(struct state));
		    st.lastLog = log;
		    st.currentAction = nextAction;
		    skipWithWhitespace(&ptr);
		}
		else if ((st.blockname = parseWord(&ptr)))
		{
		    if (!strncmp(st.blockname, "pattern", 7))
		    {
			st.step = ST_BLOCK_NAME;
			st.blockval = &(st.pattern);
		    }
		    else if (!strncmp(st.blockname, "command", 7))
		    {
			st.step = ST_BLOCK_NAME;
			st.blockval = &(st.command);
		    }
		    else
		    {
			st.blockval = NULL;
			daemon_printf_level(LEVEL_WARNING,
				"[config.c] Unknown config value: %s",
				st.blockname);
		    }
		    free(st.blockname);
		}
		else return 1;
		break;

	    case ST_BLOCK_NAME:
		if (*ptr == '=')
		{
		    st.step = ST_BLOCK_VALUE;
		}
		skipWithWhitespace(&ptr);
		break;

	    case ST_BLOCK_VALUE:
		if ((*(st.blockval) = parseWord(&ptr)))
		{
		    st.step = ST_BLOCK;
		}
		else return 1;
		break;
	}
    }
    return 0;
}

static void
loadConfigEntries(FILE *cfg)
{
    CfgLog *currentLog = NULL;
    int needFullLine = 0;
    char buf[1024];
    char *ptr;
    char *ptr2;

    while ((ptr = nextLine(buf, cfg, needFullLine)))
    {
	if (*ptr == '[')
	{
	    ++ptr;
	    for (ptr2 = ptr; *ptr2; ++ptr2)
	    {
		if (*ptr2 == ']')
		{
		    *ptr2 = '\0';
		    daemon_printf_level(LEVEL_INFO,
			    "[config.c] Found logfile section: %s", ptr);
		    if (currentLog)
		    {
			currentLog->next = lladAlloc(sizeof(CfgLog));
			currentLog = currentLog->next;
		    }
		    else
		    {
			currentLog = lladAlloc(sizeof(CfgLog));
			firstCfgLog = currentLog;
		    }
		    currentLog->name = lladCloneString(ptr);
		    currentLog->first = NULL;
		    currentLog->next = NULL;
		}
	    }
	}
	else if (currentLog)
	{
	    needFullLine = parseActions(currentLog, ptr);
	}
    }
}

void
Config_done(void)
{
    CfgLog *logc, *logl;
    CfgAct *actc, *actl;

    logc = firstCfgLog;
    while (logc)
    {
	logl = logc;
	logc = logl->next;

	actc = logl->first;
	while (actc)
	{
	    actl = actc;
	    actc = actl->next;

	    free(actl->name);
	    free(actl->pattern);
	    free(actl->command);
	    free(actl);
	}

	free(logl->name);
	free(logl);
    }

    firstCfgLog = NULL;
}

void
Config_init(void)
{
    FILE *cfg;
    const char *cfgFile;

    if (firstCfgLog) Config_done();

    if (configFile)
    {
	cfgFile = configFile;
    }
    else
    {
	cfgFile = DEFAULT_CFG;
    }

    if ((cfg = fopen(cfgFile, "r")))
    {
	loadConfigEntries(cfg);
	fclose(cfg);
    }
}

CfgLogItor *
Config_cfgLogItor()
{
    CfgLogItor *i = lladAlloc(sizeof(CfgLogItor));
    i->current = NULL;
    return i;
}

const CfgLog *
cfgLogItor_current(const CfgLogItor *self)
{
    return self->current;
}

int
cfgLogItor_moveNext(CfgLogItor *self)
{
    if (self->current) self->current = self->current->next;
    else self->current = firstCfgLog;
    return (self->current != NULL);
}

void
cfgLogItor_free(CfgLogItor *self)
{
    free(self);
}

const char *
cfgLog_name(const CfgLog *self)
{
    return self->name;
}

CfgActItor *
cfgLog_cfgActItor(const CfgLog *self)
{
    CfgActItor *i = lladAlloc(sizeof(CfgActItor));
    i->container = self;
    i->current = NULL;
    return i;
}

const CfgAct *
cfgActItor_current(const CfgActItor *self)
{
    return self->current;
}

int
cfgActItor_moveNext(CfgActItor *self)
{
    if (self->current) self->current = self->current->next;
    else self->current = self->container->first;
    return (self->current != NULL);
}

void
cfgActItor_free(CfgActItor *self)
{
    free(self);
}

const char *
cfgAct_name(const CfgAct *self)
{
    return self->name;
}

const char *
cfgAct_pattern(const CfgAct *self)
{
    return self->pattern;
}

const char *
cfgAct_command(const CfgAct *self)
{
    return self->command;
}

