/******************************************************************************\
** set_time.c                                                                 **
** Implementation file for the time set routine                               **
\******************************************************************************/

#include "set_time.h"

#include <stdlib.h>

/*****************************************************************************/
void do_set_time(TTWATCH *watch)
{
    struct tm tm_local;
    time_t utc_time = time(NULL);

    localtime_r(&utc_time, &tm_local);
    int32_t offset = tm_local.tm_gmtoff;

    time_t watch_utc_time;
    if (ttwatch_get_watch_time(watch, &watch_utc_time) != TTWATCH_NoError)
    {
        write_log(1, "Unable to get watch time\n");
        return;
    }
    /* if the UTC time of the watch is severely skewed, adjust the
       offset so that the local time still shows up correctly, and
       warn the user */
    if (abs(watch_utc_time - utc_time) > 600)
    {
        write_log(1, "Watch has wrong UTC time. Use GPS to correct this.\n");
        offset += utc_time - watch_utc_time;
    }

    if ((ttwatch_set_manifest_entry(watch, TT_MANIFEST_ENTRY_UTC_OFFSET, (uint32_t*)&offset) != TTWATCH_NoError) ||
        (ttwatch_write_manifest(watch) != TTWATCH_NoError))
    {
        write_log(1, "Unable to set watch time\n");
        return;
    }

    if (ttwatch_get_watch_time(watch, &utc_time) != TTWATCH_NoError)
        write_log(1, "Unable to get watch time\n");
}

