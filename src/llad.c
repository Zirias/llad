#include <stdio.h>
#include <string.h>
#include <popt.h>
#include <unistd.h>
#include <libgen.h>

#include "action.h"
#include "config.h"
#include "daemon.h"
#include "logfile.h"
#include "watcher.h"
#include "util.h"

static const struct poptOption opts[] = {
    ACTION_OPTS
    CONFIG_OPTS
    DAEMON_OPTS
    POPT_AUTOHELP
    POPT_TABLEEND
};

static int
svcmain(void *data)
{
    LogfileList_init();
    Watcher_watchlogs();
    LogfileList_done();

    daemon_print("Daemon stopped");

    return EXIT_SUCCESS;
}

int
main(int argc, const char **argv)
{
    int rc, prc;
    poptContext ctx;
    char *cmd = lladCloneString(argv[0]);

    daemon_init(basename(cmd));

    ctx = poptGetContext(cmd, argc, argv, opts, 0);
    prc = poptGetNextOpt(ctx);
    if (prc < -1)
    {
	daemon_printf_level(LEVEL_ERR, "Option `%s': %s",
		poptBadOption(ctx, POPT_BADOPTION_NOALIAS),
		poptStrerror(prc));
	free(cmd);
	return EXIT_FAILURE;
    }
    else
    {
	free(poptGetOptArg(ctx));
    }

    Config_init();
    rc = daemon_daemonize(&svcmain, NULL);
    Config_done();

    poptFreeContext(ctx);

    free(cmd);
    return rc;
}

