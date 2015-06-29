#include "action.h"

#include <string.h>
#include <pcre.h>

#include "daemon.h"
#include "util.h"

struct action
{
    const CfgAct *cfgAct;
    Action *next;
    pcre *re;
    pcre_extra *extra;
    int ovecsize;
    int ovec[];
};

Action *
action_append(Action *self, Action *act)
{
    Action *curr;

    if (self)
    {
	curr = self;
	while (curr->next) curr = curr->next;
	curr->next = act;
    }

    return act;
}

Action *
action_appendNew(Action *self, const CfgAct *cfgAct)
{
    Action *next;
    pcre *re;
    pcre_extra *extra;
    int ovecsize;
    const char *error;
    int erroffset;

    re = pcre_compile(cfgAct_pattern(cfgAct), 0, &error, &erroffset, NULL);
    if (!re)
    {
	daemon_printf_level(LEVEL_WARNING,
		"Action `%s' error in pattern: %s",
		cfgAct_name(cfgAct), error);
	return NULL;
    }
    extra = pcre_study(re, PCRE_STUDY_JIT_COMPILE, &error);
    ovecsize = 0;
    pcre_fullinfo(re, extra, PCRE_INFO_CAPTURECOUNT, &ovecsize);
    ++ovecsize;
    ovecsize *= 3;

    next = lladAlloc(sizeof(Action) + ovecsize * sizeof(int));
    next->next = NULL;
    next->cfgAct = cfgAct;
    next->re = re;
    next->extra = extra;
    next->ovecsize = ovecsize;

    return action_append(self, next);
}

int
action_matchAndExecChain(Action *self, const char *logname, const char *line)
{
    int rc;

    while (self)
    {
	rc = pcre_exec(self->re, self->extra, line, strlen(line), 0, 0,
		self->ovec, self->ovecsize);
	if (rc > 0)
	{
	    daemon_printf("[%s]: Action `%s' matched, executing `%s'.",
		    logname, cfgAct_name(self->cfgAct),
		    cfgAct_command(self->cfgAct));
	}
	self = self->next;
    }
    return 1;
}

void
action_free(Action *self)
{
    Action *curr, *last;

    curr = self;

    while (curr)
    {
	last = curr;
	curr = last->next;
	pcre_free_study(last->extra);
	pcre_free(last->re);
	free(last);
    }
}

