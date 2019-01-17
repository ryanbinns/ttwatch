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
void do_update_gps(TTWATCH *watch, int eph_7_days)
{
    DOWNLOAD download = {0};
    char *url = 0;

    url = get_config_string(watch, "service:ephemeris");
    if (!url)
    {
        write_log(1, "Unable to get GPSQuickFix data URL\n");
        return;
    }
    /* get days ephemeris */
    if (eph_7_days)
        url = replace(url, "{DAYS}", "7");
    else
        url = replace(url, "{DAYS}", "3");

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

