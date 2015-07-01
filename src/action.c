#define _POSIX_C_SOURCE 200809L
#include "action.h"

#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <fcntl.h>
#include <errno.h>
#include <pcre.h>
#include <semaphore.h>

#include "common.h"
#include "daemon.h"
#include "util.h"

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
    const char *actname;
    const char *cmdname;
    char **cmd;
};

static char *cmdpath = NULL;
static int waitOutput = 120;
static int pipeWait = 2;
static int termWait = 10;
static int exitWait = 20;

const struct poptOption action_opts[] = {
    {"cmd", 'p', POPT_ARG_STRING, &cmdpath, 0,
	"Look for commands in <path> instead of the default " LLADCOMMANDS,
	"path"},
    {"wait", 'w', POPT_ARG_INT, &waitOutput, 0,
	"Wait for a maximum of <sec> seconds before closing the pipe when an "
	"action doesn't produce any output, defaults to 120.", "sec"},
    {"wpipe", 0, POPT_ARG_INT, &pipeWait, 0,
	"Wait <sec> seconds for a command to terminate after closing the "
	"output pipe before it is sent a TERM signal, defaults to 2.", "sec"},
    {"wterm", 0, POPT_ARG_INT, &termWait, 0,
	"Wait <sec> seconds for a command to terminate after sending it a "
	"TERM signal before it is forcibly stopped using a KILL signal, "
	"defaults to 10.", "sec"},
    {"wexit", 0, POPT_ARG_INT, &exitWait, 0,
	"Give actions <sec> seconds to complete after termination of llad was "
	"requested before asking them to terminate, defaults to 20.", "sec"},
    POPT_TABLEEND
};

static int classInitialized = 0;

static void
cleanup(void)
{
    free((void *)cmdpath);
}

static int numThreads = 0;
static pthread_mutex_t numThreadsLock = PTHREAD_MUTEX_INITIALIZER;
static sem_t threadsLock;
static sem_t forceExit;

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

    if (!classInitialized)
    {
	classInitialized = 1;
	atexit(&cleanup);
	sem_init(&threadsLock, 0, 1);
	sem_init(&forceExit, 0, 0);
    }

    return action_append(self, next);
}

struct actionExecArgs *
createExecArgs(const Action *self, const char *line, int numArgs)
{
    char *cmdName;
    char *arg;
    const char *path;
    int i;
    size_t captureLength;

    path = cmdpath;
    if (!path) path = LLADCOMMANDS;

    struct actionExecArgs *args = lladAlloc(sizeof(struct actionExecArgs));
    args->actname = cfgAct_name(self->cfgAct);
    args->cmdname = cfgAct_command(self->cfgAct);
    args->cmd = lladAlloc((numArgs + 2) * sizeof(char *));

    cmdName = lladAlloc(strlen(path) + strlen(args->cmdname) + 2);
    strcpy(cmdName, path);
    strcat(cmdName, "/");
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
    free(args->cmd);
    free(args);
}

static int
actionWaitEndLoop(pid_t pid, int *status, int maxwait)
{
    int rc;
    int counter = maxwait;

    while (counter--)
    {
	rc = waitpid(pid, status, WNOHANG);
	if (rc < 0)
	{
	    daemon_perror("waidpid()");
	    return -1;
	}
	if (rc == (int)pid && (WIFEXITED(*status) || WIFSIGNALED(*status)))
	{
	    return (int)pid;
	}
	sleep(1);
    }
    return 0;
}

static void
actionWaitEnd(struct actionExecArgs *args, pid_t pid)
{
    int rc;
    int status;
    int retcode;

    rc = actionWaitEndLoop(pid, &status, pipeWait);
    if (rc < 0) return;
    if (!rc)
    {
	daemon_printf_level(LEVEL_NOTICE,
		"[%s] %s still running, sending SIGTERM to %d...",
		args->actname, args->cmdname, pid);
	kill(pid, SIGTERM);
	rc = actionWaitEndLoop(pid, &status, termWait);
	if (rc < 0) return;
    }
    if (!rc)
    {
	daemon_printf_level(LEVEL_WARNING,
		"[%s] %s still running, sending SIGKILL to %d...",
		args->actname, args->cmdname, pid);
	kill(pid, SIGKILL);
	if (waitpid(pid, &status, 0) < 0) return;
    }

    if (WIFEXITED(status))
    {
	retcode = WEXITSTATUS(status);
	if (retcode)
	{
	    daemon_printf_level(LEVEL_NOTICE,
		    "[%s] %s (%d) failed with exit code %d.",
		    args->actname, args->cmdname, pid, retcode);
	}
	else
	{
	    daemon_printf("[%s] %s (%d) completed successfully.",
		    args->actname, args->cmdname, pid);
	}
    }
    else if (WIFSIGNALED(status))
    {
	retcode = WTERMSIG(status);
	daemon_printf_level(LEVEL_NOTICE,
		"[%s] %s (%d) was terminated by signal %s.",
		args->actname, args->cmdname, pid, strsignal(retcode));
    }
}

static int
readLine(FILE* file, char *buf, size_t bufsize, int timeout)
{
    int counter = timeout;
    sigset_t set, oldset;
    struct timeval tv;
    fd_set rfds;
    int fd;
    int rc;
    int rcout = 0;

    fd = fileno(file);
    if (fd < 0) return -1;

    sigfillset(&set);
    pthread_sigmask(SIG_SETMASK, &set, &oldset);

    while (counter--)
    {
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);

	if (!sem_trywait(&forceExit))
	{
	    sem_post(&forceExit);
	    rcout = -2;
	    break;
	}

	rc = select(fd+1, &rfds, NULL, NULL, &tv);
	if (rc < 0)
	{
	    rcout = -1;
	    break;
	}
	if (rc)
	{
	    if (fgets(buf, bufsize, file)) rcout = 1;
	    else rcout = -1;
	    break;
	}
    }
    pthread_sigmask(SIG_SETMASK, &oldset, NULL);
    return rcout;
}

static void *
actionExec(void *argsPtr)
{
    int fds[2];
    int devnull;
    int len;
    int rc;
    pid_t pid;
    char buf[4096];
    FILE *output;
    sigset_t sigset;
    struct actionExecArgs *args = argsPtr;

#ifdef DEBUG
    daemon_printf_level(LEVEL_DEBUG,
	    "[action.c] Thread for action `%s' started.",
	    args->actname);
#endif

    if (pipe(fds) < 0)
    {
	daemon_perror("pipe()");
	goto actionExec_done;
    }

    pid = fork();
    if (pid < 0)
    {
	daemon_perror("fork()");
	close(fds[0]);
	close(fds[1]);
	goto actionExec_done;
    }

    if (pid)
    {
	close(fds[1]);
	output = fdopen(fds[0], "r");
	setvbuf(output, NULL, _IONBF, 0);

	if (output)
	{
	    while ((rc = readLine(output, buf, 4096, waitOutput)) > 0)
	    {
		len = strlen(buf);
		if (buf[len-1] == '\n') buf[len-1] = '\0';
		if (buf[len-2] == '\r') buf[len-2] = '\0';
		daemon_printf("[%s] [%s:%d] %s",
			args->actname, args->cmdname, pid, buf);
	    }
	    if (!rc)
	    {
		daemon_printf_level(LEVEL_NOTICE,
			"[%s] %s (%d) created no output for %d seconds, "
			"closing pipe.",
			args->actname, args->cmdname, pid, waitOutput);
	    }
	    fclose(output);
	}
	else
	{
	    close(fds[0]);
	}
	actionWaitEnd(args, pid);
    }
    else
    {
	close(fds[0]);
	devnull = open("/dev/null", O_RDONLY);
	dup2(devnull, STDIN_FILENO);
	dup2(fds[1], STDOUT_FILENO);
	dup2(fds[1], STDERR_FILENO);
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGINT);
	sigprocmask(SIG_BLOCK, &sigset, NULL);
	execv(args->cmd[0], args->cmd);
	fprintf(stderr, "Cannot execute `%s': %s\n",
		args->cmd[0], strerror(errno));
	exit(EXIT_FAILURE);
    }

actionExec_done:
    freeExecArgs(args);
    pthread_mutex_lock(&numThreadsLock);
    if (--numThreads == 0) sem_post(&threadsLock);
    pthread_mutex_unlock(&numThreadsLock);
    return NULL;
}

void
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
		    self, line, rc);

	    pthread_mutex_lock(&numThreadsLock);
	    if (numThreads == 0)
	    {
		sem_trywait(&threadsLock);
	    }
	    ++numThreads;

	    if (pthread_attr_init(&attr) != 0
		    || pthread_attr_setdetachstate(&attr,
			PTHREAD_CREATE_DETACHED) != 0
		    || pthread_create(&thread, &attr, &actionExec, args) != 0)
	    {
		daemon_printf_level(LEVEL_WARNING,
			"[%s]: Unable to create thread for action `%s', "
			"giving up.", logname, cfgAct_name(self->cfgAct));
		freeExecArgs(args);
		--numThreads;
		if (numThreads == 0)
		{
		    sem_post(&threadsLock);
		}
	    }

	    pthread_mutex_unlock(&numThreadsLock);
	}
	self = self->next;
    }
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

void
Action_waitForPending(void)
{
    struct timespec ts;

    if (sem_trywait(&threadsLock) < 0)
    {
	if (clock_gettime(CLOCK_REALTIME, &ts) < 0)
	{
	    daemon_perror("clock_gettime()");
	    return;
	}
	ts.tv_sec += exitWait;

	daemon_print("Waiting for pending actions to finish ...");
	if (sem_timedwait(&threadsLock, &ts) < 0)
	{
	    daemon_printf_level(LEVEL_NOTICE,
		    "Pending actions after %d seconds, closing pipes.",
		    exitWait);
	    sem_post(&forceExit);
	    if (clock_gettime(CLOCK_REALTIME, &ts) < 0)
	    {
		daemon_perror("clock_gettime()");
		return;
	    }
	    ts.tv_sec += termWait + pipeWait + 2;

	    if (sem_timedwait(&threadsLock, &ts) < 0)
	    {
		daemon_print_level(LEVEL_ERR, "Still pending actions, giving up.");
		return;
	    }
	}
	daemon_print("All actions finished.");
    }
}

