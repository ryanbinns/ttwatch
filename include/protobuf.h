/*****************************************************************************\
** protobuf.h                                                                **
** Protobuf parsing implementation                                           **
\*****************************************************************************/

#ifndef __PROTOBUF_H__
#define __PROTOBUF_H__

#include "activity_tracking.pb-c.h"

#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define PROTOBUF_TYPE_SUMMARY (0x20)

typedef struct _SUMMARY_RECORD
{
    uint32_t interval;
    uint32_t steps;
    uint32_t activity_time;
    uint32_t distance;
    uint16_t calories;
    uint16_t base_calories;
} SUMMARY_RECORD;

typedef struct
{
    time_t   timestamp_utc;
    
    SUMMARY_RECORD totals;

    Activity *activity;
} PROTOBUF_FILE;

/*****************************************************************************/

PROTOBUF_FILE *read_protobuf_file(FILE *file);

PROTOBUF_FILE *parse_protobuf_data(const uint8_t *data, uint32_t size);

const char *create_protobuf_filename(PROTOBUF_FILE *file, const char *ext);

void free_protobuf(PROTOBUF_FILE *protobuf);

#endif  /* __PROTOBUF__ */

