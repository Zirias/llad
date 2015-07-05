#ifndef LLAD_WATCHER_H
#define LLAD_WATCHER_H

/** class Watcher
 * @file
 */

/** Static class for watching a set of Logfiles.
 * This implementation uses the Linux inotify API for watching the files. At
 * least Linux 2.6.36 is needed.
 * @class Watcher "watcher.h"
 */

/** Watch logfiles in a loop.
 * This method expects the LogfileList to be initialized. Everything else is
 * handled inside. It installs some signal handling and returns upon receipt
 * of a SIGTERM or SIGINT.
 * @memberof Watcher
 * @static
 * @return 1 on success and normal termination, 0 if initialization failed
 */
int Watcher_watchlogs(void);

#endif
