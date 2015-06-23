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
    const Config *config;
    const Logfile *logfile;
    const Action *action;
    LogfileIterator *li;
    ActionIterator *ai;

    daemon_print("Daemon started");

    config = config_instance();
    for (li = config_logfileIterator(config);
	    logfileIterator_moveNext(li),
	    logfile = logfileIterator_current(li);)
    {
	daemon_printf("Logfile: %s", logfile_name(logfile));
	for (ai = logfile_actionIterator(logfile);
		actionIterator_moveNext(ai),
		action = actionIterator_current(ai);)
	{
	    daemon_printf("  Action: %s", action_name(action));
	    daemon_printf("    Pattern: %s", action_pattern(action));
	    daemon_printf("    Command: %s", action_command(action));
	}
	actionIterator_free(ai);
    }
    logfileIterator_free(li);

    daemon_print("Daemon stopped");

    return EXIT_SUCCESS;
}

int main(int argc, const char **argv)
{
    int rc;
    poptContext ctx;
    char *cmd = lladCloneString(argv[0]);

    daemon_init(basename(cmd));

    ctx = poptGetContext(cmd, argc, argv, opts, 0);
    if (poptGetNextOpt(ctx) > 0)
    {
	free(poptGetOptArg(ctx));
    }
    poptFreeContext(ctx);

    config_init();

    rc = daemon_daemonize(&svcmain, NULL);
    
    free(cmd);
    return rc;
}

