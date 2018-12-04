/*****************************************************************************\
** log.h                                                                     **
** Logging functions                                                         **
\*****************************************************************************/

#ifndef __LOG_H__
#define __LOG_H__

#define LOG_CONSOLE     (0)
#define LOG_VAR_LOG     (1)

#ifdef __cplusplus
extern "C" {
#endif

void write_log(int error, const char * fmt, ...);

void set_log_location(int location);

#ifdef __cplusplus
}
#endif

#endif  // __LOG_H__
