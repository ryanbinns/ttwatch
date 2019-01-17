/******************************************************************************\
** get_activities.h                                                           **
** Interface file for the activity download routines                          **
\******************************************************************************/

#ifndef __GET_ACTIVITIES_H__
#define __GET_ACTIVITIES_H__

#include "libttwatch.h"
#include "options.h"

#include <inttypes.h>

/*************************************************************************************************/
void do_get_activities(TTWATCH *watch, OPTIONS *options, uint32_t formats);
void do_get_activity_summaries(TTWATCH *watch, OPTIONS *options, uint32_t formats);

#endif  /* __GET_ACTIVITIES_H__ */
