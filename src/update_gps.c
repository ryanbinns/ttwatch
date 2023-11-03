/******************************************************************************\
** update_gps.c                                                               **
** Implementation file for the GPS update routine                             **
\******************************************************************************/

#include "download.h"
#include "misc.h"
#include "update_gps.h"
#include "log.h"

#include <stdlib.h>

/*****************************************************************************/
void do_update_gps(TTWATCH *watch, int eph_7_days, const char *url)
{
    DOWNLOAD download = {0};
    char *original_url;

    if (!url)
    {
        write_log(1, "GPSQuickFix data URL option EphemerisURL not configured\n");
        return;
    }

    original_url = strdup(url);
    /* get days ephemeris */
    if (eph_7_days)
        url = replace(original_url, "{DAYS}", "7");
    else
        url = replace(original_url, "{DAYS}", "3");

    /* download the data file */
    write_log(0, "Downloading GPSQuickFix data file...\n");
    if (download_file(url, &download))
    {
        free(download.data);
        return;
    }

    write_log(0, "Writing file to watch...\n");
    if (ttwatch_write_verify_whole_file(watch, TTWATCH_FILE_GPSQUICKFIX_DATA, download.data, download.length) == TTWATCH_NoError)
    {
        write_log(0, "GPSQuickFix data updated\n");
        ttwatch_reset_gps_processor(watch);
    }
    else
        write_log(1, "GPSQuickFix update failed\n");

    free(download.data);
}

