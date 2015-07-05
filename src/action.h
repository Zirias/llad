#ifndef LLAD_ACTION_H
#define LLAD_ACTION_H

/** class Action.
 * @file
 */

#include "config.h"

#include <popt.h>

extern const struct poptOption action_opts[];

/** libpopt option table for Action.
 */
#define ACTION_OPTS {NULL, '\0', POPT_ARG_INCLUDE_TABLE, (struct poptOption *)action_opts, 0, "Action options:", NULL},

struct action;

/** Class representing an action to be executed.
 * This class holds information about actions to be executed (name, pattern and
 * command) and code to actually check matches and execute the command.
 * 
 * Internally, POSIX threads are used for executing Actions. For each command
 * to be executed, a thread is launched that opens a pipe, captures the output
 * of the command (for logging) and tries to terminate the command if it seems
 * stuck (by default: if it does not produce any output for 2 minutes).
 *
 * After the pipe is closed, commands are given 2 seconds before sending them
 * a SIGTERM and another 10 seconds before forcing them to stop using SIGKILL.
 *
 * All timeout values are configurable on the command line through libpopt
 * options.
 *
 * @class Action "action.h"
 */
typedef struct action Action;

/** Append an Action to a given chain of actions.
 * @memberof Action
 * @param self chain of Actions that act should be appended to, if self is NULL
 *             nothing is done.
 * @param act the Action to append to self.
 * @returns the Action given in act.
 */
Action *action_append(Action *self, Action *act);

/** Create a new Action from config file entry and append it to chain.
 * This works as a constructor.
 * @memberof Action
 * @param self chain of Actions the new Action should be appended to, may be
 *             NULL to just create and return a new Action.
 * @param cfgAct config file entry to create the new Action from.
 * @returns the newly created Action.
 */
Action *action_appendNew(Action *self, const CfgAct *cfgAct);


/** Execute Actions matching a given log line.
 * This method walks through the chain of Actions, checking for each whether
 * the pattern matches and if so, executing the command in background.
 * @memberof Action
 * @param self chain of Actions to check for matches
 * @param logname the name of the Logfile the line came from
 * @param line the log line that should be checked for matches
 */
void action_matchAndExecChain(Action *self,
	const char *logname, const char *line);

/** Destructor for Actions.
 * This optionally destructs a whole chain of Actions.
 * @memberof Action
 * @param self chain of Actions to destroy.
 */
void action_free(Action *self);

/** Check for pending Actions and wait for them.
 * This method checks whether there are Actions executing in the background
 * and if so, waits for their completion. After a given timeout (configurable
 * through a libpopt option, default is 20 seconds), the Actions will close
 * their pipes.
 * @memberof Action
 * @static
 */
void Action_waitForPending(void);

#endif
