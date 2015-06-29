#define _POSIX_C_SOURCE 200809L
#include "action.h"

#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pcre.h>

#include "daemon.h"
#include "util.h"

#define CMDPATH "/etc/llad/command/"

struct action
{
    const CfgAct *cfgAct;
    Action *next;
    pcre *re;
    pcre_extra *extra;
    int ovecsize;
    int ovec[];
};

struct actionExecArgs
{
    const char *logname;
    const char *actname;
    const char *cmdname;
    char **cmd;
};

static char *cmdpath = CMDPATH;

static int siginitialized = 0;
static void
initsig(void)
{
    struct sigaction handler;
    memset(&handler, 0, sizeof(handler));
    handler.sa_handler = SIG_IGN;
    sigemptyset(&(handler.sa_mask));
    sigaction(SIGCHLD, &handler, NULL);
    siginitialized = 1;
}

Action *
action_append(Action *self, Action *act)
{
    Action *curr;

    if (self)
    {
	curr = self;
	while (curr->next) curr = curr->next;
	curr->next = act;
    }

    return act;
}

Action *
action_appendNew(Action *self, const CfgAct *cfgAct)
{
    Action *next;
    pcre *re;
    pcre_extra *extra;
    int ovecsize;
    const char *error;
    int erroffset;

    re = pcre_compile(cfgAct_pattern(cfgAct), 0, &error, &erroffset, NULL);
    if (!re)
    {
	daemon_printf_level(LEVEL_WARNING,
		"Action `%s' error in pattern: %s",
		cfgAct_name(cfgAct), error);
	return NULL;
    }
    extra = pcre_study(re, PCRE_STUDY_JIT_COMPILE, &error);
    ovecsize = 0;
    pcre_fullinfo(re, extra, PCRE_INFO_CAPTURECOUNT, &ovecsize);
    ++ovecsize;
    ovecsize *= 3;

    next = lladAlloc(sizeof(Action) + ovecsize * sizeof(int));
    next->next = NULL;
    next->cfgAct = cfgAct;
    next->re = re;
    next->extra = extra;
    next->ovecsize = ovecsize;

    return action_append(self, next);
}

struct actionExecArgs *
createExecArgs(const Action *self, const char *logname,
	const char *line, int numArgs)
{
    char *cmdName;
    char *arg;
    int i;
    size_t captureLength;

    struct actionExecArgs *args = lladAlloc(sizeof(struct actionExecArgs));
    args->logname = logname;
    args->actname = cfgAct_name(self->cfgAct);
    args->cmdname = cfgAct_command(self->cfgAct);
    args->cmd = lladAlloc((numArgs + 2) * sizeof(char *));

    cmdName = lladAlloc(strlen(cmdpath) + strlen(args->cmdname) + 1);
    strcpy(cmdName, cmdpath);
    strcat(cmdName, args->cmdname);
    args->cmd[0] = cmdName;

    for (i = 0; i < numArgs; ++i)
    {
	captureLength = self->ovec[2*i+1] - self->ovec[2*i];
	arg = lladAlloc(captureLength + 1);
	arg[captureLength] = '\0';
	strncpy(arg, line + self->ovec[2*i], captureLength);
	args->cmd[i+1] = arg;
    }
    args->cmd[numArgs+1] = NULL;

    return args;
}

void
freeExecArgs(struct actionExecArgs *args)
{
    char **argptr = args->cmd;
    while (*argptr)
    {
	free (*argptr);
	++argptr;
    }
    free(args);
}

void *
actionExec(void *argsPtr)
{
    int fds[2];
    int devnull;
    int len;
    pid_t pid;
    char buf[1024];
    FILE *output;
    struct actionExecArgs *args = argsPtr;

    daemon_printf_level(LEVEL_DEBUG,
	    "[%s]: Thread for action `%s' started.",
	    args->logname, args->actname);

    if (pipe(fds) < 0)
    {
	daemon_perror("pipe()");
	goto actionExec_done;
    }

    pid = fork();
    if (pid < 0)
    {
	daemon_perror("fork()");
	goto actionExec_done;
    }

    if (pid)
    {
	close(fds[1]);
	output = fdopen(fds[0], "r");

	if (output)
	{
	    while (fgets(buf, 1024, output))
	    {
		len = strlen(buf);
		if (buf[len-1] == '\n') buf[len-1] = '\0';
		if (buf[len-2] == '\n') buf[len-2] = '\0';
		daemon_printf("[%s %s] [%s %d] %s",
			args->logname, args->actname,
			args->cmdname, pid, buf);
	    }
	    fclose(output);
	}
	else
	{
	    close(fds[0]);
	}
    }
    else
    {
	close(fds[0]);
	devnull = open("/dev/null", O_RDONLY);
	dup2(devnull, STDIN_FILENO);
	dup2(fds[1], STDOUT_FILENO);
	dup2(fds[1], STDERR_FILENO);
	execv(args->cmd[0], args->cmd);
	fprintf(stderr, "Cannot execute `%s': %s\n",
		args->cmd[0], strerror(errno));
	exit(EXIT_FAILURE);
    }

actionExec_done:
    freeExecArgs(args);
    return NULL;
}

int
action_matchAndExecChain(Action *self, const char *logname, const char *line)
{
    int rc;
    pthread_attr_t attr;
    pthread_t thread;

    while (self)
    {
	rc = pcre_exec(self->re, self->extra, line, strlen(line), 0, 0,
		self->ovec, self->ovecsize);
	if (rc > 0)
	{
	    daemon_printf("[%s]: Action `%s' matched, executing `%s'.",
		    logname, cfgAct_name(self->cfgAct),
		    cfgAct_command(self->cfgAct));

	    struct actionExecArgs *args = createExecArgs(
		    self, logname, line, rc);

	    if (!siginitialized) initsig();

	    if (pthread_attr_init(&attr) != 0
		    || pthread_attr_setdetachstate(&attr,
			PTHREAD_CREATE_DETACHED) != 0
		    || pthread_create(&thread, &attr, &actionExec, args) != 0)
	    {
		daemon_printf_level(LEVEL_WARNING,
			"[%s]: Unable to create thread for action `%s', "
			"giving up.", logname, cfgAct_name(self->cfgAct));
		freeExecArgs(args);
	    }
	}
	self = self->next;
    }
    return 1;
}

void
action_free(Action *self)
{
    Action *curr, *last;

    curr = self;

    while (curr)
    {
	last = curr;
	curr = last->next;
	pcre_free_study(last->extra);
	pcre_free(last->re);
	free(last);
    }
}

