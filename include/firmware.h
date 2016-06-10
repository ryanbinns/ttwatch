/******************************************************************************\
** firmware.h                                                                 **
** Interface file for firmware update routines                                **
\******************************************************************************/

#ifndef __FIRMWARE_H__
#define __FIRMWARE_H__

#include "libttwatch.h"

#include <inttypes.h>

/*************************************************************************************************/
/* return 1 if the firmware was updated, 0 if not */
int do_update_firmware(TTWATCH *watch, int force);

#endif  /* __FIRMWARE_H__ */
