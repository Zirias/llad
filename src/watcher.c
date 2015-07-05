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

/* buffer size for reading events from inotify */
#define EVENT_BUFSIZE 4096

/* information about a watched directory */
struct watcherDir;
typedef struct watcherDir WatcherDir;

/* information about a watched file */
struct watcherFile;
typedef struct watcherFile WatcherFile;

/* information about Logfiles expected in a watched directory */
struct watcherDirEntry;
typedef struct watcherDirEntry WatcherDirEntry;

struct watcherFile
{
    Logfile *logfile;	/* the Logfile */
    WatcherFile *next;	/* next entry for watched file */
    int inwd;		/* inotify watch descriptor */
};

struct watcherDirEntry
{
    Logfile *logfile;	    /* the Logfile */
    WatcherDirEntry *next;  /* next Logfile entry for this directory */
};

struct watcherDir
{
    WatcherDirEntry entry;  /* first Logfile entry for this directory */
    WatcherDir *next;	    /* next entry for watched directory */
    int inwd;		    /* inotify watch descriptor */
};

static int Watcher_init(void);	/* initialize Watcher */
static void Watcher_done(void);	/* destroy Watcher */

static int infd = -1;			/* inotify file descriptor */
static sig_atomic_t running = 0;	/* flag indicating running watcher */
static int lastSigNum = 0;		/* number of the signal last received */
static WatcherDir *firstDir = NULL;	/* first watched directory entry */
static WatcherFile *firstFile = NULL;	/* first watched file entry */
static char evbuf[EVENT_BUFSIZE]	/* inotify events buffer */
    __attribute__ ((aligned(__alignof__(struct inotify_event))));

static void
registerFile(Logfile *log)
{
    WatcherFile *current = firstFile;

    /* create new file watch entry */
    WatcherFile *next = lladAlloc(sizeof(WatcherFile));
    next->next = NULL;
    next->logfile = log;

    /* add inotify watch for that file */
    next->inwd = inotify_add_watch(infd, logfile_name(log), IN_MODIFY);

    /* append it to the list */
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
	/* watching now */
	daemon_printf("Watching file `%s'", logfile_name(log));
    }
    else
    {
	/* can't watch at the moment */
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

    /* check whether this directory is already watched */
    while (current)
    {
	if (!strcmp(logfile_dirName(current->entry.logfile),
		    logfile_dirName(log)))
	{
	    /* if yes, just append new entry for this logfile to the
	     * WatcherDir */
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

    /* otherwise create new directory watch entry */
    nextDir = lladAlloc(sizeof(WatcherDir));
    nextDir->next = NULL;
    nextDir->entry.next = NULL;
    nextDir->entry.logfile = log;

    /* add inotify watch for the directory */
    nextDir->inwd = inotify_add_watch(infd, logfile_dirName(log),
	    IN_CREATE | IN_ATTRIB | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO
	    | IN_EXCL_UNLINK | IN_ONLYDIR);

    if (nextDir->inwd > 0)
    {
	/* watching now */
	daemon_printf("Watching directory `%s'", logfile_dirName(log));
    }
    else
    {
	/* "impossible case" ... Logfile guarantees an accessible directory */
	daemon_printf_level(LEVEL_ALERT,
		"Cannot watch directory `%s'. This should never happen!",
		logfile_dirName(log));
	free(nextDir);
	return;
    }

    /* append it to the list */
    if (current)
    {
	while (current->next) current = current->next;
	current->next = nextDir;
    }
    else
    {
	firstDir = nextDir;
    }
}

static void
sighdl(int signum)
{
    /* record number of signal received */
    lastSigNum = signum;

    /* on TERM and INT, set flag to "not running" */
    if (signum == SIGTERM || signum == SIGINT)
    {
	running = 0;
    }
}

static void
initSignals(void)
{
    /* handle TERM, INT, HUP and USR1,
     * block all standard signals while handling */
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
    /* set TERM, INT, HUP and USR1 back to being ignored */
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

    /* initialize inotify */
    infd = inotify_init();
    if (infd < 0)
    {
	daemon_perror("inotify_init()");
	return 0;
    }

    /* iterate over Logfiles, add watchers for the files and directories */
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
	/* nothing to watch means nothing to do at all -> misconfiguration */
	daemon_print_level(LEVEL_ERR,
		"Nothing to watch, check configuration.");
	close(infd);
	return 0;
    }

    /* handle signals and set flag to "running" */
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

/* find file watcher entry by inotify watch descriptor */
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

/* find directory watcher entry by inotify watch descriptor */
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

/* handle modified file: scan it for new lines */
static void
fileModified(int inwd)
{
    WatcherFile *wf = findFile(inwd);
    if (wf) logfile_scan(wf->logfile, 0);
}

/* handle deleted file */
static void
fileDeleted(int inwd, const char *name)
{
    WatcherDirEntry *entry;
    WatcherFile *wf;
    WatcherDir *wd = findDir(inwd);
    if (wd)
    {
	/* search entries of watched directory for filename */
	entry = &(wd->entry);
	while (entry)
	{
	    if (!strcmp(logfile_baseName(entry->logfile), name))
	    {
		/* found, search list of watched files for logfile */
		wf = firstFile;
		while (wf)
		{
		    if (wf->logfile == entry->logfile) break;
		    wf = wf->next;
		}
		if (wf)
		{
		    /* found, remove inotify watch for this file */
		    inotify_rm_watch(infd, wf->inwd);
		    daemon_printf_level(LEVEL_NOTICE,
			    "File `%s' disappeared, waiting to watch it again.",
			    logfile_name(wf->logfile));
		    wf->inwd = -1;

		    /* and close it */
		    logfile_close(wf->logfile);
		}
		break;
	    }
	    entry = entry->next;
	}
    }
}

/* handle new file in watched directory */
static void
fileCreated(int inwd, const char *name)
{
    WatcherDirEntry *entry;
    WatcherFile *wf;
    WatcherDir *wd = findDir(inwd);
    if (wd)
    {
	/* search entries of watched directory for filename */
	entry = &(wd->entry);
	while (entry)
	{
	    if (!strcmp(logfile_baseName(entry->logfile), name))
	    {
		/* found, search list of watched files for logfile */
		wf = firstFile;
		while (wf)
		{
		    if (wf->logfile == entry->logfile) break;
		    wf = wf->next;
		}
		if (wf && wf->inwd < 0)
		{
		    /* found if not currently watched, then add watch */
		    wf->inwd = inotify_add_watch(infd,
			    logfile_name(wf->logfile), IN_MODIFY);
		    if (wf->inwd > 0)
		    {
			/* on success, directly scan the newly created file */
			daemon_printf("Watching file `%s'",
				logfile_name(wf->logfile));
			logfile_scan(wf->logfile, 1);
		    }
		    else
		    {
			/* otherwise wait for changes making it accessible
			 * to us */
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

/* main event loop of watcher */
static void
watchloop(void)
{
    int chunk, pos;
    const struct inotify_event *ev;
    const char *sig;

    /* only loop as long as the flag is set to "running" */
    while (running)
    {
	/* read events */
	/* EVENT_BUFSIZE should be smaller than MAX int value */
	while ((chunk = (int) read(infd, &evbuf, EVENT_BUFSIZE)) > 0)
	{
	    /* iterate over events read */
	    pos = 0;
	    while (pos < chunk)
	    {
		ev = (void *)(&evbuf[pos]);
		if (ev->len)
		{
		    /* ev->len means an event from a directory, containing a
		     * file name in ev->name */
		    if (ev->mask & (IN_MOVED_FROM | IN_DELETE))
		    {
			/* moved away or deleted is the same for us, handle
			 * disappeared file */
			fileDeleted(ev->wd, ev->name);
		    }
		    else if (ev->mask & (IN_MOVED_TO | IN_ATTRIB | IN_CREATE))
		    {
			/* moved here and created is the same for us, also
			 * do the same on attribute changes because it COULD
			 * have become readable */
			fileCreated(ev->wd, ev->name);
		    }
		}
		else if (ev->mask & IN_MODIFY)
		{
		    /* event from file itself, scan it when modified */
		    fileModified(ev->wd);
		}
		pos += (int) sizeof(struct inotify_event) + (int) ev->len;
	    }
	}
	if (errno != EAGAIN && errno != EINTR)
	{
	    /* if not interrupted by a signal or temporary error, log the
	     * error */
	    daemon_perror("inotify read()");
	    break;
	}
	else
	{
	    /* otherwise check signal */
	    if (lastSigNum)
	    {
		sig = strsignal(lastSigNum);
		lastSigNum = 0;
		if (running)
		{
		    /* still running -> log ignored signal */
		    daemon_printf("Ignoring signal %s", sig);
		}
		else
		{
		    /* not running any more -> log signal that stopped us */
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
    /* don't try to watch if initialization fails */
    if (!Watcher_init()) return 0;

    /* watch and clean up when done */
    watchloop();
    Watcher_done();
    return 1;
}

