/******************************************************************************\
** download.h                                                                 **
** Interface file for file download routine                                   **
\******************************************************************************/

#ifndef __DOWNLOAD_H__
#define __DOWNLOAD_H__

#include <inttypes.h>

/*************************************************************************************************/

typedef struct
{
    uint8_t *data;
    uint32_t length;
} DOWNLOAD;

int download_file(const char *url, DOWNLOAD *download);

#endif  /* __DOWNLOAD_H__ */
