/******************************************************************************\
** update_gps.h                                                               **
** Interface file for the GPS update routine                                  **
\******************************************************************************/

#ifndef __UPDATE_GPS_H__
#define __UPDATE_GPS_H__

#include "libttwatch.h"

/******************************************************************************/
void do_update_gps(TTWATCH *watch, int eph_7_days);

#endif  /* __UPDATE_GPS_H__ */
