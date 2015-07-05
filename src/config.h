#ifndef LLAD_CONFIG_H
#define LLAD_CONFIG_H

/** class Config
 * @file
 */

/** Static class for reading the configuration file.
 * This class reads the configuration file for llad. It is parsed and the
 * values are stored in an object tree consisting of CfgLog entries for
 * Logfile sections and CfgAct entries for Action blocks inside Logfile
 * sections. Methods for walking the tree are provided.
 * @class Config "config.h"
 */

#include <popt.h>

extern const struct poptOption config_opts[];

/** libpopt option table for Config.
 */
#define CONFIG_OPTS {NULL, '\0', POPT_ARG_INCLUDE_TABLE, (struct poptOption *)config_opts, 0, "Config options:", NULL},

struct cfgLog;

/** class holding a Logfile section of the configuration file.
 * @class CfgLog "config.h"
 */
typedef struct cfgLog CfgLog;

struct cfgLogItor;

/** class for iterating over a list of CfgLog entries.
 * @class CfgLogItor "config.h"
 */
typedef struct cfgLogItor CfgLogItor;

struct cfgAct;

/** class holding an Action block of the configuration file.
 * @class CfgAct "config.h"
 */
typedef struct cfgAct CfgAct;

struct cfgActItor;

/** class for iterating over a list of CfgAct entries.
 * @class CfgActItor "config.h"
 */
typedef struct cfgActItor CfgActItor;

/** static initialization of Config.
 * must be called before any other Config methods. Reserves resources and reads
 * and parses the config file.
 * @memberof Config
 * @static
 * @returns 1 on success, 0 on error
 */
int Config_init(void);

/** static destruction of Config.
 * should be called when the config file values are no longer needed, frees all
 * resources allocated by Config_init().
 * @memberof Config
 * @static
 */
void Config_done(void);

/** Create iterator for iterating over all Logfile sections.
 * @memberof Config
 * @static
 * @returns newly created iterator at invalid position.
 */
CfgLogItor *Config_cfgLogItor();

/** Get Logfile section at current iterator position.
 * @memberof CfgLogItor
 * @param self the iterator
 * @returns Logfile section at current position (NULL at invalid position).
 */
const CfgLog *cfgLogItor_current(const CfgLogItor *self);

/** Move iterator to next position.
 * At invalid position, moves to the first entry. At end of list, moves to
 * invalid position.
 * @memberof CfgLogItor
 * @param self the iterator
 * @returns 1 if reached position is valid, 0 otherwise
 */
int cfgLogItor_moveNext(CfgLogItor *self);

/** Destroy Iterator.
 * @memberof CfgLogItor
 * @param self the iterator
 */
void cfgLogItor_free(CfgLogItor *self);

/** Get name of Logfile section.
 * @memberof CfgLog
 * @param self the Logfile section
 * @returns name of the section
 */
const char *cfgLog_name(const CfgLog *self);

/** Create iterator for iterating over all Action blocks of a Logfile section.
 * @memberof CfgLog
 * @param self the Logfile section
 * @returns newly created iterator at invalid position.
 */
CfgActItor *cfgLog_cfgActItor(const CfgLog *self);

/** Get Action block at current iterator position.
 * @memberof CfgActItor
 * @param self the iterator
 * @returns Action block at current position (NULL at invalid position).
 */
const CfgAct *cfgActItor_current(const CfgActItor *self);

/** Move iterator to next position.
 * At invalid position, moves to the first entry. At end of list, moves to
 * invalid position.
 * @memberof CfgActItor
 * @param self the iterator
 * @returns 1 if reached position is valid, 0 otherwise
 */
int cfgActItor_moveNext(CfgActItor *self);

/** Destroy Iterator.
 * @memberof CfgActItor
 * @param self the iterator
 */
void cfgActItor_free(CfgActItor *self);

/** Get name of Action block.
 * @memberof CfgAct
 * @param self the Action block
 * @returns name of the block
 */
const char *cfgAct_name(const CfgAct *self);

/** Get pattern configured for Action.
 * @memberof CfgAct
 * @param self the Action block
 * @returns configured pattern
 */
const char *cfgAct_pattern(const CfgAct *self);

/** Get command configured for Action.
 * @memberof CfgAct
 * @param self the Action block
 * @returns configured command
 */
const char *cfgAct_command(const CfgAct *self);

/** Call this at exit for final cleanup.
 * @memberof Config
 * @static
 */
void Config_atexit(void);

#endif
