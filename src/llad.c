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

static int
svcmain(void *data)
{
    const Config *config;
    const CfgLog *cfgLog;
    const CfgAct *cfgAct;
    CfgLogItor *li;
    CfgActItor *ai;

    daemon_print("Daemon started");

    config = config_instance();
    for (li = config_cfgLogItor(config);
	    cfgLogItor_moveNext(li), cfgLog = cfgLogItor_current(li);)
    {
	daemon_printf("Logfile: %s", cfgLog_name(cfgLog));
	for (ai = cfgLog_cfgActItor(cfgLog);
		cfgActItor_moveNext(ai), cfgAct = cfgActItor_current(ai);)
	{
	    daemon_printf("  Action: %s", cfgAct_name(cfgAct));
	    daemon_printf("    Pattern: %s", cfgAct_pattern(cfgAct));
	    daemon_printf("    Command: %s", cfgAct_command(cfgAct));
	}
	cfgActItor_free(ai);
    }
    cfgLogItor_free(li);

    daemon_print("Daemon stopped");

    return EXIT_SUCCESS;
}

int
main(int argc, const char **argv)
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

