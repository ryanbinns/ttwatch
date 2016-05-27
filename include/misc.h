/******************************************************************************\
** misc.h                                                                     **
** Interface file for the miscellaneous shared routines                       **
\******************************************************************************/

#ifndef __MISC_H__
#define __MISC_H__

#include "libttwatch.h"

#include <inttypes.h>

/*************************************************************************************************/

char *replace(char *str, const char *old, const char *newstr);

char *get_config_string(TTWATCH *watch, const char *name);

uint32_t get_configured_formats(TTWATCH *watch);

#endif  /* __MISC_H__ */
