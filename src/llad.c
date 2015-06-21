#include <stdio.h>
#include <string.h>
#include <popt.h>
#include <unistd.h>

#include "daemon.h"

static const struct poptOption opts[] = {
    DAEMON_OPTS
    POPT_AUTOHELP
    POPT_TABLEEND
};

static int svcmain(void *data)
{
    daemon_print("Daemon started");
    sleep(30);
    daemon_print("Daemon stopped");
}

int main(int argc, const char **argv)
{
    poptContext ctx = poptGetContext("llad", argc, argv, opts, 0);
    poptGetNextOpt(ctx);
    daemon_daemonize("llad", &svcmain, NULL);
}

