#include "action.h"

#include "util.h"

struct action
{
    const CfgAct *cfgAct;
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
    Action *next = lladAlloc(sizeof(Action));

    next->next = NULL;
    next->cfgAct = cfgAct;

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
	free(last);
    }
}

