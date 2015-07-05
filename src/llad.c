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

/* libpopt table including options from all modules */
static const struct poptOption opts[] = {
    ACTION_OPTS
    CONFIG_OPTS
    LOGFILE_OPTS
    DAEMON_OPTS
    POPT_AUTOHELP
    POPT_TABLEEND
};

/* main routine of the daemon */
static int
svcmain(void *data)
{
    int rc;

    (void)(data); /* unused */

    LogfileList_init();

    if ((rc = Watcher_watchlogs()))
    {
	/* only wait if Watcher ran successfully, otherwise there can be no
	 * actions launched. */
	rc = Action_waitForPending();
    }

    LogfileList_done();

    Daemon_print("Daemon stopped");

    return rc ? EXIT_SUCCESS : EXIT_FAILURE;
}

int
main(int argc, const char **argv)
{
    int rc, prc;
    poptContext ctx;
    char *cmd = lladCloneString(argv[0]);

    /* set daemon name from command invoked, normally `llad' */
    Daemon_init(basename(cmd));

    /* handle command line arguments using libpopt */
    ctx = poptGetContext(cmd, argc, argv, opts, 0);
    prc = poptGetNextOpt(ctx);
    if (prc < -1)
    {
	Daemon_printf_level(LEVEL_ERR, "Option `%s': %s",
		poptBadOption(ctx, POPT_BADOPTION_NOALIAS),
		poptStrerror(prc));
	poptFreeContext(ctx);
	free(cmd);
	return EXIT_FAILURE;
    }
    else
    {
	/* hack around popGetNextOpt() allocating memory even when assignment
	 * is made from popt table */
	free(poptGetOptArg(ctx));
    }

    poptFreeContext(ctx);

    /* load configuration file before launching daemon, so it doesn't even
     * start when there are errors. */
    if (Config_init())
    {
	rc = Daemon_daemonize(&svcmain, NULL);
	Config_done();
    }
    else
    {
	rc = EXIT_FAILURE;
    }

    /* call final cleanup routines */
    Action_atexit();
    Config_atexit();
    Daemon_atexit();

    free(cmd);
    return rc;
}

