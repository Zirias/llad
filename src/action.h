#ifndef LLAD_ACTION_H
#define LLAD_ACTION_H

#include "config.h"

#include <popt.h>

extern const struct poptOption action_opts[];
#define ACTION_OPTS {NULL, '\0', POPT_ARG_INCLUDE_TABLE, (struct poptOption *)action_opts, 0, "Action options:", NULL},

struct action;
typedef struct action Action;

Action *action_append(Action *self, Action *act);
Action *action_appendNew(Action *self, const CfgAct *cfgAct);

void action_matchAndExecChain(Action *self,
	const char *logname, const char *line);

void action_free(Action *self);

#endif
