#include <stdio.h>
#include <string.h>
#include <popt.h>
#include <unistd.h>
#include <libgen.h>

#include "config.h"
#include "daemon.h"
#include "util.h"

static const struct poptOption opts[] = {
    CONFIG_OPTS
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
    int rc;
    char *cmd = lladCloneString(argv[0]);

    daemon_init(basename(cmd));
    poptContext ctx = poptGetContext(cmd, argc, argv, opts, 0);
    poptGetNextOpt(ctx);
    rc = daemon_daemonize(&svcmain, NULL);
    free(cmd);
    return rc;
}

