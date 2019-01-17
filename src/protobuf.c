/*****************************************************************************\
** protobuf.c                                                                **
** Protobuf parsing implementation                                           **
\*****************************************************************************/

#include "protobuf.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*****************************************************************************/

PROTOBUF_FILE *parse_protobuf_data(const uint8_t *data, uint32_t size)
{
    PROTOBUF_FILE *file;
    int i;

    file = calloc(1, sizeof(PROTOBUF_FILE));
    file->activity = activity__unpack(NULL, size, data);

    /* Calculate Summary Record */
    for (i = 0; i < file->activity->n_rootcontainer; i++) {
        RootContainer *container = file->activity->rootcontainer[i];

        if (container->datacontainer && container->datacontainer->subdatacontainer && container->datacontainer->subdatacontainer->summary) {
            SummaryRecord *s = container->datacontainer->subdatacontainer->summary;

            if (file->timestamp_utc == 0)
                file->timestamp_utc = s->time;
            file->totals.interval += s->interval;
            file->totals.steps += s->steps;
            file->totals.activity_time += s->activitytime;
            file->totals.distance += s->distance;
            file->totals.calories += s->calories;
            file->totals.base_calories += s->basecalories;
        }
    }

    return file;
}

/*****************************************************************************/

const char *create_protobuf_filename(PROTOBUF_FILE *protobuf, const char *ext)
{
    static char filename[32];
    struct tm *time = gmtime(&protobuf->timestamp_utc);
    const char *type = "Summary";

    sprintf(filename, "%s_%02d-%02d-%02d.%s", type, time->tm_year + 1900, time->tm_mon + 1, time->tm_mday, ext);

    return filename;
}

/*****************************************************************************/

void free_protobuf(PROTOBUF_FILE *protobuf)
{
    if (!protobuf)
        return;

    activity__free_unpacked(protobuf->activity, NULL);

    free(protobuf);
}
