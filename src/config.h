#ifndef LLAD_CONFIG_H
#define LLAD_CONFIG_H

#include <popt.h>

extern const struct poptOption config_opts[];
#define CONFIG_OPTS {NULL, '\0', POPT_ARG_INCLUDE_TABLE, (struct poptOption *)config_opts, 0, "Config options:", NULL},

struct config;
typedef struct config Config;

struct logfile;
typedef struct logfile Logfile;

struct logfileIterator;
typedef struct logfileIterator LogfileIterator;

struct action;
typedef struct action Action;

struct actionIterator;
typedef struct actionIterator ActionIterator;

void config_init(void);

const Config *config_instance(void);

LogfileIterator *config_logfileIterator(const Config *self);
const Logfile *logfileIterator_current(const LogfileIterator *self);
int logfileIterator_moveNext(LogfileIterator *self);
void logfileIterator_free(LogfileIterator *self);

const char *logfile_name(const Logfile *self);

ActionIterator *logfile_actionIterator(const Logfile *self);
const Action *actionIterator_current(const ActionIterator *self);
int actionIterator_moveNext(ActionIterator *self);
void actionIterator_free(ActionIterator *self);

const char *action_name(const Action *self);
const char *action_pattern(const Action *self);
const char *action_command(const Action *self);

#endif
