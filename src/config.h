#ifndef LLAD_CONFIG_H
#define LLAD_CONFIG_H

#include <popt.h>

extern const struct poptOption config_opts[];
#define CONFIG_OPTS {NULL, '\0', POPT_ARG_INCLUDE_TABLE, (struct poptOption *)config_opts, 0, "Config options:", NULL},

struct config;
typedef struct config Config;

struct logfile;
typedef struct logfile Logfile;

struct action;
typedef struct action Action;

const Config *config_Load(void);


#endif
