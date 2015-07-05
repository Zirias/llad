#include "config.h"

#include <string.h>
#include <errno.h>

#include "util.h"
#include "daemon.h"
#include "common.h"

static char *configFile = NULL;	    /* config file location from options */
static const char *cfgFile;	    /* real config file location */

const struct poptOption config_opts[] = {
    {"config", 'c', POPT_ARG_STRING, &configFile, 0,
	"Load config from <path> instead of the default " LLADCONF, "path"},
    POPT_TABLEEND
};

struct cfgLog {
    char *name;			/* logfile section name */
    CfgAct *first;		/* first action block in section */
    CfgLog *next;		/* next logfile section */
};

struct cfgLogItor {
    CfgLog *current;		/* current logfile section */
};

struct cfgAct {
    char *name;			/* action block name */
    char *pattern;		/* pattern for action */
    char *command;		/* command for action */
    CfgAct *next;		/* next action block */
};

struct cfgActItor {
    const CfgLog *container;	/* logfile section containing action blocks */
    CfgAct *current;		/* current action block */
};

static CfgLog *firstCfgLog = NULL;  /* first logfile section in config */
static int lineNumber = 0;	    /* current line number while parsing */
static int actionInProgress = 0;    /* if 1, action still parsing */

static int cleanupInstalled = 0;    /* flag for static initialization */

/* free resources at exit */
static void
cleanup(void)
{
    Config_done();
    free(configFile);
}

/* put next "meaningful" line in buf */
static char *
nextLine(char *buf, FILE *cfg, int fullLine)
{
    char *ptr;
   
    while (fgets(buf, 1024, cfg))
    {
	++lineNumber;
	ptr = buf;

	/* if fullLine requested, just give the next full line */
	if (fullLine) return ptr;

	/* otherwise skip whitespace */
	while (*ptr == ' ' || *ptr == '\t')
	{
	    ++ptr;
	}

	/* if line is empty or only contains a comment, try next */
	if (*ptr == '\r' || *ptr == '\n' || *ptr == '\0'
		|| *ptr == ';' || *ptr == '#')
	{
	    continue;
	}

	return ptr;
    }

    /* no more lines */
    return NULL;
}

/* skip any whitespace character */
static void
skipWhitespace(char **pos)
{
    while (**pos == ' ' || **pos == '\t' || **pos == '\r' || **pos == '\n')
    {
	++*pos;
    }
}

/* parse and return a word at a given start position, honouring quotes
 * and escapes.
 * This can be called multiple times for words spanning across multiple lines.
 */
static char *
parseWord(char **pos)
{
    /* Quote state */
    enum qst
    {
	Q_NORMAL,	/* outside of quotes */
	Q_QUOTE,	/* inside single quotes (') */
	Q_DBLQUOTE	/* inside double quotes (") */
    };

    /* Parser state */
    struct state
    {
	enum qst qst;	    /* Quote state */
	int esc;	    /* flag for escape */
	char buf[1024];	    /* parsing buffer */
	char *bufptr;	    /* current position in the buffer */
    };

    char *word = NULL;		    /* return value */
    static int initialized = 0;	    /* flag for static initialization */
    static struct state st;	    /* current state of the parser */

    if (!initialized)
    {
	/* initialize state */
	initialized = 1;
	memset(&st, 0, sizeof(struct state));
	st.bufptr = st.buf;
    }

    while (**pos && st.bufptr < st.buf+1023)
    {
	if (st.esc)
	{
	    /* in escape mode, copy any next character and end escape mode */
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
		/* in quote mode, only the quote character can be escaped */
		++*pos;
		st.esc = 1;
	    }
	    else if ((st.qst == Q_QUOTE && **pos == '\'') ||
		(st.qst == Q_DBLQUOTE && **pos == '"'))
	    {
		/* found matching quote character -> end quote mode */
		++*pos;
		st.qst = Q_NORMAL;
	    }
	    else
	    {
		/* just copy any other character */
		*st.bufptr++ = **pos;
		++*pos;
	    }
	}
	else if (**pos == '\\')
	{
	    /* enter escape mode */
	    ++*pos;
	    st.esc = 1;
	}
	else if (**pos == '\'')
	{
	    /* enter single quote mode */
	    ++*pos;
	    st.qst = Q_QUOTE;
	}
	else if (**pos == '"')
	{
	    /* enter double quote mode */
	    ++*pos;
	    st.qst = Q_DBLQUOTE;
	}
	else if (**pos == ' ' || **pos == '\t' || **pos == '=' || **pos == '{'
		|| **pos == '}' || **pos == '\r' || **pos == '\n')
	{
	    /* these characters end a word if not escaped or inside quotes
	     * skip any trailing whitespace */
	    skipWhitespace(pos);

	    /* clone read word */
	    word = lladCloneString(st.buf);
#ifdef DEBUG
	    daemon_printf_level(LEVEL_DEBUG,
		    "[config.c] parseWord(): found `%s'", word);
#endif
	    /* reinitialize state */
	    memset(&st, 0, sizeof(struct state));
	    st.bufptr = st.buf;

	    /* and return */
	    return word;
	}
	else
	{
	    /* normal character -> copy */
	    *st.bufptr++ = **pos;
	    ++*pos;
	}
    }
    if (**pos)
    {
	/* end of parsing buffer reached -> error */
	daemon_print_level(LEVEL_ERR, "Buffer full reading configuration.");
	exit(EXIT_FAILURE);
    }

#ifdef DEBUG
    daemon_print_level(LEVEL_DEBUG, "[config.c] parseWord(): incomplete");
#endif

    /* no complete word read before end of input string -> return nothing
     * and leave state, so this function can be called with more data */
    return NULL;
}

/* parse Action blocks, append complete blocks to given Logfile section.
 * return 1 if line ends inside of a word, 0 otherwise */
static int
parseActions(CfgLog *log, char *line)
{
    /* parsing steps (state machine) */
    enum step
    {
	ST_START,	/* initial state, expect action name */
	ST_NAME,	/* name read, expect equals sign */
	ST_NAME_EQUALS,	/* equals sign read, expect beginning of block */
	ST_BLOCK,	/* beginning of block read, expect property name
			 * or end of block */
	ST_BLOCK_NAME,	/* valid property name read, expect equals sign */
	ST_BLOCK_VALUE	/* equals sign read, expect property value */
    };

    /* Parser state */
    struct state
    {
	CfgLog *lastLog;	/* Logfile section from last invocation */
	char *name;		/* name of new Action block */
	char *pattern;		/* pattern for new Action block */
	char *command;		/* command for new Action block */
	char *blockname;	/* property name inside block */
	char **blockval;	/* property value, ptr to pattern or command */
	CfgAct *currentAction;	/* currently completed Action block */
	enum step step;		/* parser step */
    };

    static int initialized = 0;	/* flag for static initialization */
    static struct state st;	/* parser state */
    char *ptr;			/* working pointer, position in line */
    CfgAct *nextAction;		/* newly parsed Action block */

    if (!initialized)
    {
	/* initialize parser state */
	initialized = 1;
	memset(&st, 0, sizeof(struct state));
	st.lastLog = log;
    }
    else if (log != st.lastLog)
    {
	/* reinitialize parser state if called for different Logfile section */
	free(st.name);
	free(st.pattern);
	free(st.command);
	memset(&st, 0, sizeof(struct state));
	st.lastLog = log;
    }

    ptr = line; /* point to beginning of line */

    while (*ptr)
    {
	/* state machine */
	switch (st.step)
	{
	    case ST_START:
		/* need a word as name of Action */
		if ((st.name = parseWord(&ptr)))
		{
		    if (!strlen(st.name))
		    {
			/* empty name -> error */
			free(st.name);
			daemon_printf_level(LEVEL_ERR,
				"Error in `%s': Expected action name in line "
				"%d, got `%c'", cfgFile, lineNumber, *ptr);
			return -1;
		    }
		    daemon_printf_level(LEVEL_DEBUG,
			    "[config.c] Found action: %s", st.name);
		    /* name found -> transition to ST_NAME */
		    st.step = ST_NAME;

		    /* set flag that we are not done yet */
		    actionInProgress = 1;
		}

		/* no word complete -> need whole next line */
		else return 1;

		break;
	    
	    case ST_NAME:
		if (*ptr == '=')
		{
		    /* found -> transition to ST_NAME_EQUALS */
		    st.step = ST_NAME_EQUALS;
		    ++ptr;
		    skipWhitespace(&ptr);
		}
		else
		{
		    /* error */
		    free(st.name);
		    daemon_printf_level(LEVEL_ERR,
			    "Error in `%s': Unexpected `%c' in line %d, "
			    "expected `='",
			    cfgFile, *ptr, lineNumber);
		    return -1;
		}
		break;

	    case ST_NAME_EQUALS:
		if (*ptr == '{')
		{
		    /* found -> transition to ST_BLOCK */
		    st.step = ST_BLOCK;
		    ++ptr;
		    skipWhitespace(&ptr);
		}
		else
		{
		    /* error */
		    free(st.name);
		    daemon_printf_level(LEVEL_ERR,
			    "Error in `%s': Unexpected `%c' in line %d, "
			    "expected `{'",
			    cfgFile, *ptr, lineNumber);
		    return -1;
		}
		break;

	    case ST_BLOCK:
		/* inside block */
		if (*ptr == '}')
		{
		    /* end of block found, transition to ST_START state */
		    st.step = ST_START;

		    /* block is only complete with pattern and command */
		    if (st.pattern && st.command)
		    {
			/* have both -> create new Action block object */
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
			daemon_printf_level(LEVEL_DEBUG,
				"[config.c] pattern: `%s' command: `%s'",
				st.pattern, st.command);

			/* done with this action: */
			actionInProgress = 0;
		    }
		    else
		    {
			/* error -> incomplete block */
			daemon_printf_level(LEVEL_ERR,
				"Error in `%s': Incomplete action `%s' "
				"at line %d.",
				cfgFile, st.name, lineNumber);
			free(st.name);
			free(st.pattern);
			free(st.command);
			return -1;
		    }

		    /* reinitialize state */
		    memset(&st, 0, sizeof(struct state));
		    st.lastLog = log;
		    st.currentAction = nextAction;

		    /* skip '}' and any following whitespace */
		    ++ptr;
		    skipWhitespace(&ptr);
		}
		else if ((st.blockname = parseWord(&ptr)))
		{
		    if (!strlen(st.blockname))
		    {
			/* empty name -> error */
			free(st.name);
			free(st.blockname);
			free(st.command);
			free(st.pattern);
			daemon_printf_level(LEVEL_ERR,
				"Error in `%s': Expected config value in line "
				"%d, got `%c'.", cfgFile, lineNumber, *ptr);
			return -1;
		    }

		    /* word inside a block is property name */
		    if (!strncmp(st.blockname, "pattern", 7))
		    {
			if (st.pattern)
			{
			    /* already got pattern for this action -> error */
			    daemon_printf_level(LEVEL_ERR,
				    "Error in `%s': Found second pattern for "
				    "action `%s' in line %d",
				    cfgFile, st.name, lineNumber);
			    free(st.name);
			    free(st.blockname);
			    free(st.command);
			    free(st.pattern);
			    return -1;
			}

			/* found "pattern" -> transition to ST_BLOCK_NAME */
			st.step = ST_BLOCK_NAME;

			/* and set value pointer to pattern */
			st.blockval = &(st.pattern);
		    }
		    else if (!strncmp(st.blockname, "command", 7))
		    {
			if (st.command)
			{
			    /* already got pattern for this action -> error */
			    daemon_printf_level(LEVEL_ERR,
				    "Error in `%s': Found second command for "
				    "action `%s' in line %d",
				    cfgFile, st.name, lineNumber);
			    free(st.name);
			    free(st.blockname);
			    free(st.command);
			    free(st.pattern);
			    return -1;
			}

			/* found "command" -> transition to ST_BLOCK_NAME */
			st.step = ST_BLOCK_NAME;

			/* and set value pointer to command */
			st.blockval = &(st.command);
		    }
		    else
		    {
			/* unknown property name -> error */
			free(st.command);
			free(st.pattern);
			free(st.name);
			daemon_printf_level(LEVEL_ERR,
				"Error in `%s': Unknown config value `%s' in "
				"line %d", cfgFile, st.blockname, lineNumber);
			free(st.blockname);
			return -1;
		    }
		    free(st.blockname);
		}

		/* still in block and no word complete
		 * -> need whole next line */
		else return 1;

		break;

	    case ST_BLOCK_NAME:
		/* need equals sign */
		if (*ptr == '=')
		{
		    /* found -> transition to ST_BLOCK_VALUE */
		    st.step = ST_BLOCK_VALUE;
		    ++ptr;
		    skipWhitespace(&ptr);
		}
		else
		{
		    /* error */
		    free(st.name);
		    daemon_printf_level(LEVEL_ERR,
			    "Error in `%s': Unexpected `%c' in line %d, "
			    "expected `='",
			    cfgFile, *ptr, lineNumber);
		    return -1;
		}
		break;

	    case ST_BLOCK_VALUE:
		/* need word for property value */
		if ((*(st.blockval) = parseWord(&ptr)))
		{
		    if (!strlen(st.name))
		    {
			/* empty name -> error */
			free(st.name);
			if (st.blockval == &(st.command))
			{
			    daemon_printf_level(LEVEL_ERR,
				    "Error in `%s': Expected command in line "
				    "%d, got `%c'", cfgFile, lineNumber, *ptr);
			}
			else
			{
			    daemon_printf_level(LEVEL_ERR,
				    "Error in `%s': Expected pattern in line "
				    "%d, got `%c'", cfgFile, lineNumber, *ptr);
			}
			return -1;
		    }
		    /* found -> transition to ST_BLOCK */
		    st.step = ST_BLOCK;
		}

		/* no word complete -> need whole next line */
		else return 1;

		break;
	}
    }

    /* parsing step complete */
    return 0;
}

/* parse config file */
static int
loadConfigEntries(FILE *cfg)
{
    CfgLog *currentLog = NULL;
    int needFullLine = 0;
    char buf[1024];
    char *ptr;
    char *ptr2;

    lineNumber = 0;

    /* read line for line, ignoring trainling whitespace and comments */
    while ((ptr = nextLine(buf, cfg, needFullLine)))
    {
	if (!needFullLine && *ptr == '[')
	{
	    /* first character is '[' -> new logfile section found */
	    ++ptr;

	    if (actionInProgress)
	    {
		/* new section while action is incomplete is an error */
		daemon_printf_level(LEVEL_ERR,
			"Error in `%s': Found '[' before action block was "
			"completed in line %d.", cfgFile, lineNumber);
		return 0;
	    }

	    /* search for matching ']' */
	    for (ptr2 = ptr; *ptr2; ++ptr2)
	    {
		if (*ptr2 == ']')
		{
		    /* found -> create new Logfile section object */
		    *ptr2 = '\0';
		    daemon_printf_level(LEVEL_DEBUG,
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
		    goto loadConfigNext;
		}
	    }

	    /* missing ']' -> error */
	    daemon_printf_level(LEVEL_ERR,
		    "Error in `%s': '[' without matching ']' in line %d.",
		    cfgFile, lineNumber);
	    return 0;
	}
	else if (currentLog)
	{
	    /* no section start -> try to parse actions */
	    needFullLine = parseActions(currentLog, ptr);
	    if (needFullLine < 0) return 0;
	}
loadConfigNext:;
    }

    if (actionInProgress)
    {
	/* end of file while action is incompete -> error */
	daemon_printf_level(LEVEL_ERR,
		"Error in `%s': Unexpected end of file before action block "
		"was completed.", cfgFile);
	return 0;
    }

    /* parsed successfully */
    return 1;
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

int
Config_init(void)
{
    FILE *cfg;

    if (firstCfgLog) Config_done();

    if (!cleanupInstalled)
    {
	cleanupInstalled = 1;
	atexit(&cleanup);
    }

    /* prefer option over compile-time configuration
     * for config file location */
    if (configFile)
    {
	cfgFile = configFile;
    }
    else
    {
	cfgFile = LLADCONF;
    }

    /* try reading configuration file */
    if ((cfg = fopen(cfgFile, "r")))
    {
	if (!loadConfigEntries(cfg))
	{
	    Config_done();
	    return 0;
	}
	fclose(cfg);
    }
    else
    {
	daemon_printf_level(LEVEL_ERR,
		"Could not read `%s': %s", cfgFile, strerror(errno));
	return 0;
    }

    return 1;
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

