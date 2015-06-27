#ifndef LLAD_LOGFILE_H
#define LLAD_LOGFILE_H

struct logfile;
typedef struct logfile Logfile;

struct logfileItor;
typedef struct logfileItor LogfileItor;

void LogfileList_init(void);
void LogfileList_done(void);

LogfileItor *LogfileList_itor(void);

Logfile *logfileItor_current(const LogfileItor *self);
int logfileItor_moveNext(LogfileItor *self);
void logfileItor_free(LogfileItor *self);

const char *logfile_name(const Logfile *self);
const char *logfile_dirName(const Logfile *self);
void logfile_scan(Logfile *self);
void logfile_reset(Logfile *self);

#endif
