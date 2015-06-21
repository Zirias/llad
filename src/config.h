#ifndef LLAD_CONFIG_H
#define LLAD_CONFIG_H

struct config;
typedef struct config Config;

struct logfile;
typedef struct logfile Logfile;

struct action;
typedef struct action Action;

const Config *config_Load(const char *configFile);


#endif
