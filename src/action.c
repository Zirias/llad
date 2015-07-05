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
    const CfgAct *cfgAct;	/* config section */
    Action *next;		/* pointer to next Action in list */
    pcre *re;			/* Compiled regular expression from pattern */
    pcre_extra *extra;		/* Study data for regular expression */
    unsigned int ovecsize;	/* size of vector used for matching */
    int ovec[];			/* variable-sized vector */
};

/* arguments to pass to a thread controlling an action */
struct actionExecArgs
{
    const char *actname;	/* name of the action */
    const char *cmdname;	/* command to execute */
    char **cmd;			/* command line for execv, NULL-terminated */
};

static char *cmdpath = NULL;	/* configurable path for commands */
static int waitOutput = 120;	/* max time waiting for command output (sec) */
static int pipeWait = 2;	/* max waiting time after pipe closed (sec) */
static int termWait = 10;	/* max waiting time after SIGTERM */
static int exitWait = 20;	/* max waiting time on daemon shutdown */

const struct poptOption action_opts[] = {
    {"cmd", 'p', POPT_ARG_STRING, &cmdpath, 0,
	"Look for commands in <path> instead of the default " LLADCOMMANDS,
	"path"},
    {"wait", 'w', POPT_ARG_INT, &waitOutput, 0,
	"Wait for a maximum of <sec> seconds before closing the pipe when an "
	"action doesn't produce any output, defaults to 120.", "sec"},
    {"wpipe", '\0', POPT_ARG_INT, &pipeWait, 0,
	"Wait <sec> seconds for a command to terminate after closing the "
	"output pipe before it is sent a TERM signal, defaults to 2.", "sec"},
    {"wterm", '\0', POPT_ARG_INT, &termWait, 0,
	"Wait <sec> seconds for a command to terminate after sending it a "
	"TERM signal before it is forcibly stopped using a KILL signal, "
	"defaults to 10.", "sec"},
    {"wexit", '\0', POPT_ARG_INT, &exitWait, 0,
	"Give actions <sec> seconds to complete after termination of llad was "
	"requested before asking them to terminate, defaults to 20.", "sec"},
    POPT_TABLEEND
};

static int classInitialized = 0; /* flag for static initialization */

/* free popt arguments at exit */
static void
cleanup(void)
{
    free(cmdpath);
}

static int numThreads = 0;		/* current number of running threads */
static pthread_mutex_t numThreadsLock	/* mutex for numThreads */
	= PTHREAD_MUTEX_INITIALIZER;
static sem_t threadsLock;		/* locked indicates running threads */
static sem_t forceExit;			/* unlocked indicates daemon shutdown */

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
    unsigned int ovecsize;
    const char *error;
    int erroffset;

    /* compile pattern to PCRE regular expression */
    re = pcre_compile(cfgAct_pattern(cfgAct), 0, &error, &erroffset, NULL);
    if (!re)
    {
	daemon_printf_level(LEVEL_WARNING,
		"Action `%s' error in pattern: %s",
		cfgAct_name(cfgAct), error);
	return NULL;
    }

    /* study regular expression, if possible use JIT compilation */
    extra = pcre_study(re, PCRE_STUDY_JIT_COMPILE, &error);

    /* calculate needed vector size for matching:
     * pcre_exec() needs 2 elements for the whole match, 2 elements for each
     * capturing group (to pass begin and end positions in the subject string)
     * plus one additional element per pair for internal use, so use the
     * formula (num_capturing_groups + 1) * 3
     */
    ovecsize = 0;
    pcre_fullinfo(re, extra, PCRE_INFO_CAPTURECOUNT, &ovecsize);
    ++ovecsize;
    ovecsize *= 3;

    /* create and initialize new Action object */
    next = lladAlloc(sizeof(Action) + ovecsize * sizeof(int));
    next->next = NULL;
    next->cfgAct = cfgAct;
    next->re = re;
    next->extra = extra;
    next->ovecsize = ovecsize;

    /* do static initialization if not done before */
    if (!classInitialized)
    {
	classInitialized = 1;
	atexit(&cleanup);
	sem_init(&threadsLock, 0, 1); /* unlocked -> no threads running */
	sem_init(&forceExit, 0, 0);   /* locked -> not shutting down */
    }

    return action_append(self, next);
}

/* create arguments to pass to controlling thread */
struct actionExecArgs *
createExecArgs(const Action *self, const char *line, int numArgs)
{
    char *cmdName;
    char *arg;
    const char *path;
    int i;
    size_t captureLength;
    struct actionExecArgs *args;

    /* determine path of commands, option overrides compile time config */
    path = cmdpath;
    if (!path) path = LLADCOMMANDS;

    /* allocate and initialize structure */
    args = lladAlloc(sizeof(struct actionExecArgs));
    args->actname = cfgAct_name(self->cfgAct);
    args->cmdname = cfgAct_command(self->cfgAct);
    args->cmd = lladAlloc((size_t)(numArgs + 2) * sizeof(char *));

    /* determine full path for executed command */
    cmdName = lladAlloc(strlen(path) + strlen(args->cmdname) + 2);
    strcpy(cmdName, path);
    strcat(cmdName, "/");
    strcat(cmdName, args->cmdname);
    args->cmd[0] = cmdName;

    /* pass matches as arguments to executed command */
    for (i = 0; i < numArgs; ++i)
    {
	/* safe conversion if we trust libpcre */
	captureLength = (size_t)(self->ovec[2*i+1] - self->ovec[2*i]);
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

/* wait a given time for child process to exit */
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
	    /* error waiting */
	    daemon_perror("waidpid()");
	    return -1;
	}
	if (rc == (int)pid && (WIFEXITED(*status) || WIFSIGNALED(*status)))
	{
	    /* child exited */
	    return (int)pid;
	}
	sleep(1);
    }

    /* child still running */
    return 0;
}

/* wait for child process to exit, send signals as necessary */
static void
actionWaitEnd(struct actionExecArgs *args, pid_t pid)
{
    int rc;
    int status;
    int retcode;

    /* wait for some time */
    rc = actionWaitEndLoop(pid, &status, pipeWait);
    if (rc < 0) return;
    if (!rc)
    {
	/* not yet exited, send SIGTERM and wait again */
	daemon_printf_level(LEVEL_NOTICE,
		"[%s] %s still running, sending SIGTERM to %d...",
		args->actname, args->cmdname, pid);
	kill(pid, SIGTERM);
	rc = actionWaitEndLoop(pid, &status, termWait);
	if (rc < 0) return;
    }
    if (!rc)
    {
	/* not yet exited, send SIGKILL and wait until child died */
	daemon_printf_level(LEVEL_WARNING,
		"[%s] %s still running, sending SIGKILL to %d...",
		args->actname, args->cmdname, pid);
	kill(pid, SIGKILL);
	if (waitpid(pid, &status, 0) < 0) return;
    }

    /* determine how child exited and log */
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

/* read line from pipe to child with timeout */
static int
readLine(FILE* file, char *buf, int bufsize, int timeout)
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

    /* don't allow interruptions by signals */
    sigfillset(&set);
    pthread_sigmask(SIG_SETMASK, &set, &oldset);

    while (counter--)
    {
	/* 1 second for each select() attempt */
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);

	/* check whether daemon shutdown was requested */
	if (!sem_trywait(&forceExit))
	{
	    /* pass on to next thread */
	    sem_post(&forceExit);
	    rcout = -2;
	    break;
	}

	rc = select(fd+1, &rfds, NULL, NULL, &tv);
	if (rc < 0)
	{
	    /* abort if select() fails */
	    rcout = -1;
	    break;
	}
	if (rc)
	{
	    /* data available -> read line and return */
	    if (fgets(buf, bufsize, file)) rcout = 1;
	    else rcout = -1;
	    break;
	}
    }

    /* restore signals */
    pthread_sigmask(SIG_SETMASK, &oldset, NULL);
    return rcout;
}

/* main routine for controlling thread, open pipe and execute command */
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

	/* open stream for pipe to read one line at a time */
	output = fdopen(fds[0], "r");

	if (output)
	{
	    /* first disable buffering, because otherwise some lines might
	     * never be read, thus missing from the log */
	    setvbuf(output, NULL, _IONBF, 0);

	    /* read from pipe and log command output */
	    while ((rc = readLine(output, buf, 4096, waitOutput)) > 0)
	    {
		/* strip newline first */
		len = (int)strlen(buf);
		if (buf[len-1] == '\n') buf[len-1] = '\0';
		if (buf[len-2] == '\r') buf[len-2] = '\0';
		daemon_printf("[%s] [%s:%d] %s",
			args->actname, args->cmdname, pid, buf);
	    }
	    if (!rc)
	    {
		/* readLine() == 0 means timeout occured */
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

	/* wait for child process to exit */
	actionWaitEnd(args, pid);
    }
    else
    {
	/* in child process, arrange stdio file descriptors */
	close(fds[0]);
	devnull = open("/dev/null", O_RDONLY);
	dup2(devnull, STDIN_FILENO);
	dup2(fds[1], STDOUT_FILENO);
	dup2(fds[1], STDERR_FILENO);

	/* block SIGINT, because this propagates when running interactively,
	 * so children wouldn't get a chance to terminate normally when the
	 * user hits Ctrl-C */
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGINT);
	sigprocmask(SIG_BLOCK, &sigset, NULL);

	/* execute the command */
	execv(args->cmd[0], args->cmd);

	/* if execv returns, execution failed -> log (through pipe) and exit */
	fprintf(stderr, "Cannot execute `%s': %s\n",
		args->cmd[0], strerror(errno));
	exit(EXIT_FAILURE);
    }

actionExec_done:
    /* common cleanup code */

    /* free arguments structure */
    freeExecArgs(args);

    /* count down number of running threads, unlock threadsLock
     * when reaching 0
     */
    pthread_mutex_lock(&numThreadsLock);
    if (--numThreads == 0) sem_post(&threadsLock);
    pthread_mutex_unlock(&numThreadsLock);

    /* exit thread */
    return NULL;
}

void
action_matchAndExecChain(Action *self, const char *logname, const char *line)
{
    int rc;
    pthread_attr_t attr;
    pthread_t thread;
    struct actionExecArgs *args;

    while (self)
    {
	/* try to match the line */
	rc = pcre_exec(self->re, self->extra, line, (int)strlen(line), 0, 0,
		self->ovec, (int)self->ovecsize);
	if (rc > 0)
	{
	    daemon_printf("[%s]: Action `%s' matched, executing `%s'.",
		    logname, cfgAct_name(self->cfgAct),
		    cfgAct_command(self->cfgAct));

	    /* line matches, the number of matched groups is in the return
	     * code of pcre_exec() */
	    args = createExecArgs(self, line, rc);

	    pthread_mutex_lock(&numThreadsLock);
	    if (numThreads == 0)
	    {
		/* if this is the first thread, lock threadsLock indicating
		 * "running threads" */
		sem_trywait(&threadsLock);
	    }

	    /* increase number of threads */
	    ++numThreads;

	    /* try creating the thread in detached state */
	    if (pthread_attr_init(&attr) != 0
		    || pthread_attr_setdetachstate(&attr,
			PTHREAD_CREATE_DETACHED) != 0
		    || pthread_create(&thread, &attr, &actionExec, args) != 0)
	    {
		daemon_printf_level(LEVEL_WARNING,
			"[%s]: Unable to create thread for action `%s', "
			"giving up.", logname, cfgAct_name(self->cfgAct));

		/* clean up when thread can't be created */
		freeExecArgs(args);
		--numThreads;
		if (numThreads == 0)
		{
		    /* no threads running -> release threadsLock */
		    sem_post(&threadsLock);
		}
	    }

	    pthread_mutex_unlock(&numThreadsLock);
	}

	/* iterate through the whole chain */
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

int
Action_waitForPending(void)
{
    struct timespec ts;

    if (sem_trywait(&threadsLock) < 0)
    {
	/* threads are running, calculate absolute timeout */
	if (clock_gettime(CLOCK_REALTIME, &ts) < 0)
	{
	    daemon_perror("clock_gettime()");
	    return 0;
	}
	ts.tv_sec += exitWait;

	daemon_print("Waiting for pending actions to finish ...");
	if (sem_timedwait(&threadsLock, &ts) < 0)
	{
	    daemon_printf_level(LEVEL_NOTICE,
		    "Pending actions after %d seconds, closing pipes.",
		    exitWait);

	    /* threads are running after timeout, signal them to close their
	     * pipes */
	    sem_post(&forceExit);

	    /* and calculate absolute timeout until all should have finished */
	    if (clock_gettime(CLOCK_REALTIME, &ts) < 0)
	    {
		daemon_perror("clock_gettime()");
		return 0;
	    }
	    ts.tv_sec += termWait + pipeWait + 2;

	    if (sem_timedwait(&threadsLock, &ts) < 0)
	    {
		daemon_print_level(LEVEL_ERR,
			"Still pending actions, giving up.");

		/* if threads are STILL running, this is an error condition */
		return 0;
	    }
	}
	daemon_print("All actions finished.");
    }

    /* all fine, return TRUE */
    return 1;
}

