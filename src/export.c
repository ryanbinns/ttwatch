/*****************************************************************************\
** export.c                                                                  **
** Export definitions and common export helper functions                     **
\*****************************************************************************/

#include "export.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*****************************************************************************/

const OFFLINE_FORMAT OFFLINE_FORMATS[OFFLINE_FORMAT_COUNT] = {
    { OFFLINE_FORMAT_CSV, "csv", 1, 1, 1, 1, export_csv, export_protobuf_csv },
    { OFFLINE_FORMAT_FIT, "fit", 1, 0, 0, 0, 0,          0 },
    { OFFLINE_FORMAT_GPX, "gpx", 1, 0, 0, 0, export_gpx, 0 },
    { OFFLINE_FORMAT_KML, "kml", 1, 0, 0, 0, export_kml, 0 },
    { OFFLINE_FORMAT_PWX, "pwx", 1, 0, 0, 0, 0,          0 },
    { OFFLINE_FORMAT_TCX, "tcx", 1, 1, 1, 1, export_tcx, 0 },
};

/*****************************************************************************/

uint32_t parse_format_list(const char *formats)
{
    uint32_t fmts = 0;
    int i;
    char *str, *ptr;

    str = strdup(formats);
    ptr = strtok(str, " ,");
    while (ptr)
    {
        for (i = 0; i < OFFLINE_FORMAT_COUNT; ++i)
        {
            if (!strcasecmp(ptr, OFFLINE_FORMATS[i].name))
            {
                fmts |= OFFLINE_FORMATS[i].mask;
                break;
            }
        }
        /* ignore any unknown formats... */
        ptr = strtok(NULL, " ,");
    }
    free(str);
    return fmts;
}

/*****************************************************************************/

uint32_t export_formats(TTBIN_FILE *ttbin, uint32_t formats)
{
    unsigned i;
    FILE *f;

    for (i = 0; i < OFFLINE_FORMAT_COUNT; ++i)
    {
        if ((formats & OFFLINE_FORMATS[i].mask) && OFFLINE_FORMATS[i].producer)
        {
            if ((OFFLINE_FORMATS[i].gps_ok && ttbin->gps_records.count)
                || (OFFLINE_FORMATS[i].treadmill_ok && (ttbin->activity == ACTIVITY_TREADMILL))
                || (OFFLINE_FORMATS[i].pool_swim_ok && (ttbin->activity == ACTIVITY_SWIMMING)))
            {
                f = fopen(create_filename(ttbin, OFFLINE_FORMATS[i].name), "w");
                if (f)
                {
                    (*OFFLINE_FORMATS[i].producer)(ttbin, f);
                    fclose(f);
                }
                else
                    formats &= ~OFFLINE_FORMATS[i].mask;
            }
            else
                formats &= ~OFFLINE_FORMATS[i].mask;
        }
        else
            formats &= ~OFFLINE_FORMATS[i].mask;
    }

    return formats;
}
/*****************************************************************************/

uint32_t export_protobuf_formats(PROTOBUF_FILE *protobuf, uint32_t formats)
{
    unsigned i;
    FILE *f;

    for (i = 0; i < OFFLINE_FORMAT_COUNT; ++i)
    {
        if ((formats & OFFLINE_FORMATS[i].mask) && OFFLINE_FORMATS[i].protobuf_producer)
        {
            f = fopen(create_protobuf_filename(protobuf, OFFLINE_FORMATS[i].name), "w");
            if (f)
            {
                (*OFFLINE_FORMATS[i].protobuf_producer)(protobuf, f);
                fclose(f);
            }
            else
                formats &= ~OFFLINE_FORMATS[i].mask;
        }
    }

    return formats;
}
