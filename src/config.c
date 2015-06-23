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

struct config {
    Logfile *first;
};

struct logfile {
    char *name;
    Action *first;
    Logfile *next;
};

struct logfileIterator {
    const Config *container;
    Logfile *current;
};

struct action {
    char *name;
    char *pattern;
    char *command;
    Action *next;
};

struct actionIterator {
    const Logfile *container;
    Action *current;
};

static Config *configInstance = NULL;

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
parseActions(Logfile *log, char *line)
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
	Logfile *lastLog;
	char *name;
	char *pattern;
	char *command;
	char *blockname;
	char **blockval;
	Action *currentAction;
	enum step step;
    };

    static int initialized = 0;
    static struct state st;
    char *ptr;
    Action *nextAction;

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
			nextAction = lladAlloc(sizeof(Action));
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

static Logfile *
loadConfigEntries(FILE *cfg)
{
    Logfile *firstLog = NULL;
    Logfile *currentLog = NULL;
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
			currentLog->next = lladAlloc(sizeof(Logfile));
			currentLog = currentLog->next;
		    }
		    else
		    {
			currentLog = lladAlloc(sizeof(Logfile));
			firstLog = currentLog;
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

    return firstLog;
}

void
config_init(void)
{
    FILE *cfg;
    const char *cfgFile;

    if (configInstance) return;

    configInstance = lladAlloc(sizeof(Config));
    configInstance->first = NULL;

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
	configInstance->first = loadConfigEntries(cfg);
	fclose(cfg);
    }
}

const Config *
config_instance(void)
{
    return configInstance;
}

LogfileIterator *
config_logfileIterator(const Config *self)
{
    LogfileIterator *i = lladAlloc(sizeof(LogfileIterator));
    i->container = self;
    i->current = NULL;
    return i;
}

const Logfile *
logfileIterator_current(const LogfileIterator *self)
{
    return self->current;
}

int
logfileIterator_moveNext(LogfileIterator *self)
{
    if (self->current) self->current = self->current->next;
    else self->current = self->container->first;
    return (self->current != NULL);
}

void
logfileIterator_free(LogfileIterator *self)
{
    free(self);
}

const char *
logfile_name(const Logfile *self)
{
    return self->name;
}

ActionIterator *
logfile_actionIterator(const Logfile *self)
{
    ActionIterator *i = lladAlloc(sizeof(ActionIterator));
    i->container = self;
    i->current = NULL;
    return i;
}

const Action *
actionIterator_current(const ActionIterator *self)
{
    return self->current;
}

int
actionIterator_moveNext(ActionIterator *self)
{
    if (self->current) self->current = self->current->next;
    else self->current = self->container->first;
    return (self->current != NULL);
}

void
actionIterator_free(ActionIterator *self)
{
    free(self);
}

const char *
action_name(const Action *self)
{
    return self->name;
}

const char *
action_pattern(const Action *self)
{
    return self->pattern;
}

const char *
action_command(const Action *self)
{
    return self->command;
}

