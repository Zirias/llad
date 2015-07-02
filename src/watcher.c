#define _POSIX_C_SOURCE 200809L
#include "watcher.h"

#include <stdio.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#include "logfile.h"
#include "daemon.h"
#include "util.h"

#define EVENT_BUFSIZE 4096

struct watcherDir;
typedef struct watcherDir WatcherDir;

struct watcherFile;
typedef struct watcherFile WatcherFile;

struct watcherDirEntry;
typedef struct watcherDirEntry WatcherDirEntry;

struct watcherFile
{
    Logfile *logfile;
    WatcherFile *next;
    int inwd;
};

struct watcherDirEntry
{
    Logfile *logfile;
    WatcherDirEntry *next;
};

struct watcherDir
{
    WatcherDirEntry entry;
    WatcherDir *next;
    int inwd;
};

static int Watcher_init(void);
static void Watcher_done(void);

static int infd = -1;
static sig_atomic_t running = 0;
static int lastSigNum = 0;
static WatcherDir *firstDir = NULL;
static WatcherFile *firstFile = NULL;
static char evbuf[EVENT_BUFSIZE]
    __attribute__ ((aligned(__alignof__(struct inotify_event))));

static void
registerFile(Logfile *log)
{
    WatcherFile *current = firstFile;
    WatcherFile *next = lladAlloc(sizeof(WatcherFile));
    next->next = NULL;
    next->logfile = log;
    next->inwd = inotify_add_watch(infd, logfile_name(log), IN_MODIFY);
    if (current)
    {
	while (current->next) current = current->next;
	current->next = next;
    }
    else
    {
	firstFile = next;
    }
    if (next->inwd > 0)
    {
	daemon_printf("Watching file `%s'", logfile_name(log));
    }
    else
    {
	daemon_printf_level(LEVEL_NOTICE,
		"Waiting to watch non-accessible or non-existent file `%s'",
		logfile_name(log));
    }
}

static void
registerDir(Logfile *log)
{
    WatcherDir *current = firstDir;
    WatcherDirEntry *currentEntry, *nextEntry;
    WatcherDir *nextDir;

    while (current)
    {
	if (!strcmp(logfile_dirName(current->entry.logfile),
		    logfile_dirName(log)))
	{
	    nextEntry = lladAlloc(sizeof(WatcherDirEntry));
	    nextEntry->next = NULL;
	    nextEntry->logfile = log;
	    if (current->entry.next)
	    {
		currentEntry = current->entry.next;
		while (currentEntry->next) currentEntry = currentEntry->next;
		currentEntry->next = nextEntry;
	    }
	    else current->entry.next = nextEntry;
	    return;
	}
	current = current->next;
    }

    current = firstDir;
    nextDir = lladAlloc(sizeof(WatcherDir));
    nextDir->next = NULL;
    nextDir->entry.next = NULL;
    nextDir->entry.logfile = log;
    nextDir->inwd = inotify_add_watch(infd, logfile_dirName(log),
	    IN_CREATE | IN_ATTRIB | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO
	    | IN_EXCL_UNLINK | IN_ONLYDIR);
    if (current)
    {
	while (current->next) current = current->next;
	current->next = nextDir;
    }
    else
    {
	firstDir = nextDir;
    }
    if (nextDir->inwd > 0)
    {
	daemon_printf("Watching dir `%s'", logfile_dirName(log));
    }
}

static void
sighdl(int signum)
{
    lastSigNum = signum;
    running = 0;
}

static void
initSignals(void)
{
    struct sigaction handler;
    memset(&handler, 0, sizeof(handler));
    handler.sa_handler = &sighdl;
    sigemptyset(&(handler.sa_mask));
    sigaddset(&(handler.sa_mask), SIGTERM);
    sigaddset(&(handler.sa_mask), SIGINT);
    sigaddset(&(handler.sa_mask), SIGHUP);
    sigaddset(&(handler.sa_mask), SIGUSR1);
    sigaddset(&(handler.sa_mask), SIGQUIT);
    sigaddset(&(handler.sa_mask), SIGSTOP);
    sigaction(SIGTERM, &handler, NULL);
    sigaction(SIGINT, &handler, NULL);
    sigaction(SIGHUP, &handler, NULL);
    sigaction(SIGUSR1, &handler, NULL);
}

static void
doneSignals(void)
{
    struct sigaction handler;
    memset(&handler, 0, sizeof(handler));
    handler.sa_handler = SIG_IGN;
    sigemptyset(&(handler.sa_mask));
    sigaction(SIGTERM, &handler, NULL);
    sigaction(SIGINT, &handler, NULL);
    sigaction(SIGHUP, &handler, NULL);
    sigaction(SIGUSR1, &handler, NULL);
}

static int
Watcher_init(void)
{
    LogfileItor *i;
    Logfile *log;

    infd = inotify_init();
    if (infd < 0)
    {
	daemon_perror("inotify_init()");
	return 0;
    }

    i = LogfileList_itor();
    while (logfileItor_moveNext(i))
    {
	log = logfileItor_current(i);
	registerDir(log);
	registerFile(log);
    }
    logfileItor_free(i);

    if (!firstFile && !firstDir)
    {
	daemon_print_level(LEVEL_ERR,
		"Nothing to watch, check configuration.");
	close(infd);
	return 0;
    }

    initSignals();
    running = 1;
    return 1;
}

static void
Watcher_done(void)
{
    WatcherFile *fcurr, *flast;
    WatcherDir *dcurr, *dlast;
    WatcherDirEntry *ecurr, *elast;

    running = 0;
    doneSignals();
    fcurr = firstFile;
    while (fcurr)
    {
	flast = fcurr;
	fcurr = flast->next;
	free (flast);
    }

    dcurr = firstDir;
    while (dcurr)
    {
	dlast = dcurr;
	dcurr = dlast->next;
	ecurr = dlast->entry.next;
	while (ecurr)
	{
	    elast = ecurr;
	    ecurr = elast->next;
	    free(elast);
	}
	free(dlast);
    }
    close(infd);
    infd = -1;
}

static WatcherFile *
findFile(int wd)
{
    WatcherFile *curr;

    curr = firstFile;
    while (curr)
    {
	if (curr->inwd == wd) break;
	curr = curr->next;
    }
    return curr;
}

static WatcherDir *
findDir(int wd)
{
    WatcherDir *curr;

    curr = firstDir;
    while (curr)
    {
	if (curr->inwd == wd) break;
	curr = curr->next;
    }
    return curr;
}

static void
fileModified(int inwd)
{
    WatcherFile *wf = findFile(inwd);
    if (wf) logfile_scan(wf->logfile, 0);
}

static void
fileDeleted(int inwd, const char *name)
{
    WatcherDirEntry *entry;
    WatcherFile *wf;
    WatcherDir *wd = findDir(inwd);
    if (wd)
    {
	entry = &(wd->entry);
	while (entry)
	{
	    if (!strcmp(logfile_baseName(entry->logfile), name))
	    {
		wf = firstFile;
		while (wf)
		{
		    if (wf->logfile == entry->logfile) break;
		    wf = wf->next;
		}
		if (wf)
		{
		    inotify_rm_watch(infd, wf->inwd);
		    daemon_printf_level(LEVEL_NOTICE,
			    "File `%s' disappeared, waiting to watch it again.",
			    logfile_name(wf->logfile));
		    wf->inwd = -1;
		    logfile_close(wf->logfile);
		}
		break;
	    }
	    entry = entry->next;
	}
    }
}

static void
fileCreated(int inwd, const char *name)
{
    WatcherDirEntry *entry;
    WatcherFile *wf;
    WatcherDir *wd = findDir(inwd);
    if (wd)
    {
	entry = &(wd->entry);
	while (entry)
	{
	    if (!strcmp(logfile_baseName(entry->logfile), name))
	    {		
		wf = firstFile;
		while (wf)
		{
		    if (wf->logfile == entry->logfile) break;
		    wf = wf->next;
		}
		if (wf && wf->inwd < 0)
		{
		    wf->inwd = inotify_add_watch(infd,
			    logfile_name(wf->logfile),
			    IN_MODIFY | IN_MOVE_SELF | IN_DELETE_SELF);
		    if (wf->inwd > 0)
		    {
			daemon_printf("Watching file `%s'",
				logfile_name(wf->logfile));
			logfile_scan(wf->logfile, 1);
		    }
		    else
		    {
			daemon_printf_level(LEVEL_NOTICE,
				"Waiting to watch non-accessible newly "
				"created file `%s'",
				logfile_name(wf->logfile));
		    }
		}
		break;
	    }
	    entry = entry->next;
	}
    }
}

static void
watchloop(void)
{
    int chunk, pos;
    const struct inotify_event *ev;
    const char *sig;

    while (running)
    {
	/* EVENT_BUFSIZE should be smaller than MAX int value */
	while ((chunk = (int) read(infd, &evbuf, EVENT_BUFSIZE)) > 0)
	{
	    pos = 0;
	    while (pos < chunk)
	    {
		ev = (void *)(&evbuf[pos]);
		if (ev->len)
		{
		    if (ev->mask & (IN_MOVED_FROM | IN_DELETE))
		    {
			fileDeleted(ev->wd, ev->name);
		    }
		    else if (ev->mask & (IN_MOVED_TO | IN_ATTRIB | IN_CREATE))
		    {
			fileCreated(ev->wd, ev->name);
		    }
		}
		else if (ev->mask & IN_MODIFY)
		{
		    fileModified(ev->wd);
		}
		pos += (int) sizeof(struct inotify_event) + (int) ev->len;
	    }
	}
	if (errno != EAGAIN && errno != EINTR)
	{
	    daemon_perror("inotify read()");
	    break;
	}
	else
	{
	    if (lastSigNum)
	    {
		sig = strsignal(lastSigNum);
		lastSigNum = 0;
		if (running)
		{
		    daemon_printf("Ignoring signal %s", sig);
		}
		else
		{
		    daemon_printf_level(LEVEL_NOTICE,
			    "Received signal %s: stopping daemon.", sig);
		}
	    }
	}
    }
}

int
Watcher_watchlogs(void)
{
    if(!Watcher_init()) return 0;
    watchloop();
    Watcher_done();
    return 1;
}

