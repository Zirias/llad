#ifndef LLAD_LOGFILE_H
#define LLAD_LOGFILE_H

/** class Logfile
 * @file
 */

#include <popt.h>

extern const struct poptOption logfile_opts[];

/** libpopt option table for Logfile
 */
#define LOGFILE_OPTS {NULL, '\0', POPT_ARG_INCLUDE_TABLE, (struct poptOption *)logfile_opts, 0, "Logfile options:", NULL},

struct logfile;

/** Class representing a Logfile.
 * This class holds data about the Logfile (name, list of Actions) and includes
 * code for reading new Lines from a Logfile and passing it to the Actions.
 * @class Logfile "logfile.h"
 */
typedef struct logfile Logfile;

/** Static class for managing the list of logfiles that should be scanned.
 * @class LogfileList "logfile.h"
 */

struct logfileItor;

/** Class for iterating over the list of Logfiles.
 * @class LogfileItor "logfile.h"
 */
typedef struct logfileItor LogfileItor;

/** Initialize the list of Logfiles.
 * This automatically creates the Logfile list, including Actions, from Config.
 * @memberof LogfileList
 * @static
 */
void LogfileList_init(void);

/** Destroy the list of Logfiles.
 * This frees all resources allocated during initialization.
 * @memberof LogfileList
 * @static
 */
void LogfileList_done(void);

/** Create iterator for iterating over all Logfiles.
 * @memberof LogfileList
 * @static
 * @returns newly created iterator at invalid position
 */
LogfileItor *LogfileList_itor(void);

/** Get logfile at current iterator position.
 * @memberof LogfileItor
 * @param self the iterator
 * @returns Logfile at current position (NULL at invalid position)
 */
Logfile *logfileItor_current(const LogfileItor *self);

/** Move iterator to next position.
 * At invalid position, moves to the first entry. At end of list, moves to
 * invalid position.
 * @memberof LogfileItor
 * @param self the iterator
 * @returns 1 if reached position is valid, 0 otherwise
 */
int logfileItor_moveNext(LogfileItor *self);

/** Destroy iterator.
 * @memberof LogfileItor
 * @param self the iterator
 */
void logfileItor_free(LogfileItor *self);

/** Get full name of Logfile.
 * @memberof Logfile
 * @param self the Logfile
 * @returns the full canonic filename
 */
const char *logfile_name(const Logfile *self);

/** Get directory name of Logfile.
 * @memberof Logfile
 * @param self the Logfile
 * @returns the canonic directory name
 */
const char *logfile_dirName(const Logfile *self);

/** Get base name of Logfile.
 * @memberof Logfile
 * @param self the Logfile
 * @returns the base filename
 */
const char *logfile_baseName(const Logfile *self);

/** Scan logfile for new lines.
 * This method scans the logfile for new lines, reading them one by one and
 * feeding them to the list of Actions for pattern matching. If the file is
 * not opened, the method tries to open it and reads it from the beginning if
 * it is smaller than 8k -- otherwise the file pointer is put at the end and
 * nothing happens.
 *
 * If reopen is given, the file is first closed and reopened and the above
 * logic applies. Only do this if you know the file has been re-created for
 * example by log rotation.
 *
 * Otherwise, if the file is smaller than at the last invocation, the file
 * pointer is just put to the end of the file and a warning message is logged
 * that the file has been truncated.
 *
 * @memberof Logfile
 * @param self the Logfile
 * @param reopen close and reopen the file if this is not 0
 */
void logfile_scan(Logfile *self, int reopen);

/** Close the logfile.
 * If the file is currently opened, this method closes it. This could be used
 * if deletion of the file was detected.
 * @memberof Logfile
 * @param self the Logfile
 */
void logfile_close(Logfile *self);

#endif
