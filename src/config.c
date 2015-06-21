#include "config.h"

#include "util.h"

struct config {
    const Logfile *first;
};

struct logfile {
    const Action *first;
    const Logfile *next;
};

struct action {
    const char *pattern;
    const char *command;
    const Action *next;
};

const Config *
config_Load(const char *configFile)
{
    Config *self = lladAlloc(sizeof(Config));
    self->first = NULL;
    return self;
}

