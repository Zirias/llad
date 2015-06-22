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

struct action {
    char *name;
    char *pattern;
    char *command;
    Action *next;
};

static char *
nextLine(char *buf, FILE *cfg)
{
    char *ptr;
    
    while (fgets(buf, 1024, cfg))
    {
	ptr = buf;
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

static void
parseActions(Logfile *log, char *line)
{
    enum step
    {
	ST_START,
	ST_NAME,
	ST_NAME_EQUALS,
	ST_BLOCK,
	ST_BLOCK_VALUE
    };

    enum qst
    {
	Q_NORMAL,
	Q_QUOTE,
	Q_DBLQUOTE
    };

    struct state
    {
	Logfile *lastLog;
	char *name;
	char *pattern;
	char *command;
	Action currentAction;
	enum step step;
	enum qst qst;
    };

    static int initialized = 0;
    static struct state st;

    if (!initialized)
    {
	initialized = 1;
	memset(&st, 0, sizeof(struct state));
    }

    if (log != st.lastLog)
    {
	free(st.name);
	free(st.pattern);
	free(st.command);
	memset(&st, 0, sizeof(struct state));
	st.lastLog = log;
    }
}

static Logfile *
loadConfigEntries(FILE *cfg)
{
    Logfile *firstLog = NULL;
    Logfile *currentLog = NULL;
    char buf[1024];
    char *ptr;
    char *ptr2;

    while (ptr = nextLine(buf, cfg))
    {
	if (*ptr == '[')
	{
	    ++ptr;
	    for (ptr2 = ptr; *ptr2; ++ptr2)
	    {
		if (*ptr2 == ']')
		{
		    *ptr2 = '\0';
		    daemon_printf_level(LEVEL_DEBUG,
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
		    currentLog->name = strdup(ptr);
		    currentLog->first = NULL;
		    currentLog->next = NULL;
		}
	    }
	}
	else if (currentLog)
	{
	    parseActions(currentLog, ptr);
	}
    }

    return firstLog;
}

const Config *
config_Load(void)
{
    FILE *cfg;
    const char *cfgFile;

    Config *self = lladAlloc(sizeof(Config));
    self->first = NULL;

    if (configFile)
    {
	cfgFile = configFile;
    }
    else
    {
	cfgFile = DEFAULT_CFG;
    }

    if (cfg = fopen(cfgFile, "r"))
    {
	self->first = loadConfigEntries(cfg);
	fclose(cfg);
    }

    return self;
}

