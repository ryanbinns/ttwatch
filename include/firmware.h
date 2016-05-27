/******************************************************************************\
** firmware.h                                                                 **
** Interface file for firmware update routines                                **
\******************************************************************************/

#ifndef __FIRMWARE_H__
#define __FIRMWARE_H__

#include "libttwatch.h"

#include <inttypes.h>

/*************************************************************************************************/
void do_update_firmware(TTWATCH *watch, int force);

#endif  /* __FIRMWARE_H__ */
