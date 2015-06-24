#ifndef LLAD_CONFIG_H
#define LLAD_CONFIG_H

#include <popt.h>

extern const struct poptOption config_opts[];
#define CONFIG_OPTS {NULL, '\0', POPT_ARG_INCLUDE_TABLE, (struct poptOption *)config_opts, 0, "Config options:", NULL},

struct config;
typedef struct config Config;

struct cfgLog;
typedef struct cfgLog CfgLog;

struct cfgLogItor;
typedef struct cfgLogItor CfgLogItor;

struct cfgAct;
typedef struct cfgAct CfgAct;

struct cfgActItor;
typedef struct cfgActItor CfgActItor;

void config_init(void);

const Config *config_instance(void);

CfgLogItor *config_cfgLogItor(const Config *self);
const CfgLog *cfgLogItor_current(const CfgLogItor *self);
int cfgLogItor_moveNext(CfgLogItor *self);
void cfgLogItor_free(CfgLogItor *self);

const char *cfgLog_name(const CfgLog *self);

CfgActItor *cfgLog_cfgActItor(const CfgLog *self);
const CfgAct *cfgActItor_current(const CfgActItor *self);
int cfgActItor_moveNext(CfgActItor *self);
void cfgActItor_free(CfgActItor *self);

const char *cfgAct_name(const CfgAct *self);
const char *cfgAct_pattern(const CfgAct *self);
const char *cfgAct_command(const CfgAct *self);

#endif
