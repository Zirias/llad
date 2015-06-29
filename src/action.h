#ifndef LLAD_ACTION_H
#define LLAD_ACTION_H

#include "config.h"

struct action;
typedef struct action Action;

Action *action_appendNew(Action *self, const CfgAct *cfgAct);

int action_matchAndExecChain(const Action *self, const char *line);

void action_free(Action *self);

#endif
