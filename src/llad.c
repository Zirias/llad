#include <stdio.h>
#include <string.h>
#include <popt.h>
#include <unistd.h>
#include <libgen.h>

#include "config.h"
#include "daemon.h"
#include "logfile.h"
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
    const CfgLog *cfgLog;
    const CfgAct *cfgAct;
    CfgLogItor *li;
    CfgActItor *ai;
    LogfileItor *i;

    daemon_print("Daemon started");

    for (li = Config_cfgLogItor();
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

    LogfileList_init();

    while (!sleep(1))
    {
	i = LogfileList_itor();
	while (logfileItor_moveNext(i))
	{
	    logfile_scan(logfileItor_current(i), 0);
	}
	logfileItor_free(i);
    }

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

