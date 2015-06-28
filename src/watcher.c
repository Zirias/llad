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
    int inwd;
    Logfile *logfile;
    WatcherFile *next;
};

struct watcherDirEntry
{
    Logfile *logfile;
    WatcherDirEntry *next;
};

struct watcherDir
{
    int inwd;
    WatcherDirEntry entry;
    WatcherDir *next;
};

static int Watcher_init(void);
static void Watcher_done(void);

static int infd = -1;
static int running = 0;
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
    next->inwd = inotify_add_watch(infd, logfile_name(log),
	    IN_MODIFY | IN_MOVE_SELF | IN_DELETE_SELF);
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
	daemon_printf_level(LEVEL_INFO,
		"Watching file `%s'", logfile_name(log));
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
    }

    current = firstDir;
    nextDir = lladAlloc(sizeof(WatcherDir));
    nextDir->next = NULL;
    nextDir->entry.next = NULL;
    nextDir->entry.logfile = log;
    nextDir->inwd = inotify_add_watch(infd, logfile_dirName(log), IN_CREATE);
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
	daemon_printf_level(LEVEL_INFO,
		"Watching dir `%s'", logfile_dirName(log));
    }
}

static void
sighdl(int signum)
{
    if (signum == SIGTERM || signum == SIGINT)
    {
	daemon_printf("Received signal %s: stopping daemon.",
		strsignal(signum));
	running = 0;
    }
    else
    {
	daemon_printf("Ignoring signal %s", strsignal(signum));
    }
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
    handler.sa_handler = SIG_DFL;
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
fileDeleted(int inwd)
{
    WatcherFile *wf = findFile(inwd);
    if (wf)
    {
	inotify_rm_watch(infd, wf->inwd);
	wf->inwd = -1;
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
	    if (!strcmp(logfile_name(entry->logfile), name))
	    {		
		wf = firstFile;
		while (wf)
		{
		    if (wf->logfile == entry->logfile) break;
		    wf = wf->next;
		}
		if (wf)
		{
		    wf->inwd = inotify_add_watch(infd, name,
			    IN_MODIFY | IN_MOVE_SELF | IN_DELETE_SELF);
		    logfile_scan(wf->logfile, 1);
		    break;
		}
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

    while (running)
    {
	while ((chunk = read(infd, &evbuf, EVENT_BUFSIZE)) > 0)
	{
	    pos = 0;
	    while (pos < chunk)
	    {
		ev = (void *)(&evbuf[pos]);
		if (ev->mask & IN_MODIFY)
		{
		    fileModified(ev->wd);
		}
		if (ev->mask & (IN_MOVE_SELF | IN_DELETE_SELF))
		{
		    fileDeleted(ev->wd);
		}
		if (ev->mask & IN_CREATE)
		{
		    fileCreated(ev->wd, ev->name);
		}
		pos += sizeof(struct inotify_event) + ev->len;
	    }
	}
	daemon_printf_level(LEVEL_DEBUG,
		"inotify read() failed: %s", strerror(errno));
	if (errno != EAGAIN && errno != EINTR) break;
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

