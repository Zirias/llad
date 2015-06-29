#include "action.h"

#include <pcre.h>

#include "daemon.h"
#include "util.h"

struct action
{
    const CfgAct *cfgAct;
    pcre *re;
    Action *next;
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
    const char *error;
    int erroffset;

    re = pcre_compile(cfgAct_pattern(cfgAct), 0, &error, &erroffset, NULL);
    if (!re)
    {
	daemon_printf_level(LEVEL_WARNING,
		"Action `%s' error in pattern: %s", cfgAct_name(cfgAct), error);
	return NULL;
    }

    next = lladAlloc(sizeof(Action));
    next->next = NULL;
    next->cfgAct = cfgAct;
    next->re = re;

    return action_append(self, next);
}

int
action_matchAndExecChain(const Action *self, const char *line)
{
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
	pcre_free(last->re);
	free(last);
    }
}

