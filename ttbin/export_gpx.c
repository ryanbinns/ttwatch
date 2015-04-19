/*****************************************************************************\
** export_gpx.c                                                              **
** GPX export code                                                           **
\*****************************************************************************/

#include "ttbin.h"

#include <math.h>

void export_gpx(TTBIN_FILE *ttbin, FILE *file)
{
    char timestr[32];
    TTBIN_RECORD *record;
    int heart_rate;

    if (!ttbin->gps_records.count)
        return;

    fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
          "<gpx xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
          " xsi:schemaLocation=\"http://www.topografix.com/GPX/1/1"
          " http://www.topografix.com/GPX/1/1/gpx.xsd"
          " http://www.garmin.com/xmlschemas/GpxExtensions/v3"
          " http://www.garmin.com/xmlschemas/GpxExtensionsv3.xsd"
          " http://www.garmin.com/xmlschemas/TrackPointExtension/v1"
          " http://www.garmin.com/xmlschemas/TrackPointExtensionv1.xsd\""
          " xmlns:gpxx=\"http://www.garmin.com/xmlschemas/GpxExtensions/v3\""
          " xmlns:gpxtpx=\"http://www.garmin.com/xmlschemas/TrackPointExtension/v1\""
          " version=\"1.1\" creator=\"TomTom\" xmlns=\"http://www.topografix.com/GPX/1/1\">\r\n"
          "    <metadata>\r\n        <name>", file);
    fputs(create_filename(ttbin, "gpx"), file);
    fputs("</name>\r\n    </metadata>\r\n"
          "    <trk>\r\n        <name>", file);
    switch(ttbin->activity)
    {
    case ACTIVITY_RUNNING:   fputs("RUNNING", file);   break;
    case ACTIVITY_CYCLING:   fputs("CYCLING", file);   break;
    case ACTIVITY_SWIMMING:  fputs("POOL SWIM", file); break;
    case ACTIVITY_TREADMILL: fputs("TREADMILL", file); break;
    case ACTIVITY_FREESTYLE: fputs("FREESTYLE", file); break;
    default:                 fputs("UNKNOWN", file);   break;
    }
    fputs("</name>\r\n        <trkseg>\r\n", file);

    heart_rate = 0;
    for (record = ttbin->first; record; record = record->next)
    {
        switch (record->tag)
        {
        case TAG_GPS:
            /* this will happen if the GPS signal is lost or the activity is paused */
            if ((record->gps.timestamp == 0) || ((record->gps.latitude == 0) && (record->gps.longitude == 0)))
                continue;
            strftime(timestr, sizeof(timestr), "%FT%X.000Z", gmtime(&record->gps.timestamp));
            fprintf(file, "            <trkpt lon=\"%.6f\" lat=\"%.6f\">\r\n",
                record->gps.longitude, record->gps.latitude);
            if (!isnan(record->gps.elevation))
                fprintf(file, "                <ele>%d</ele>\r\n", (int)record->gps.elevation);
            fputs(        "                <time>", file);
            fputs(timestr, file);
            fputs("</time>\r\n", file);
            if (heart_rate > 0)
            {
                fputs("                <extensions>\r\n"
                      "                    <gpxtpx:TrackPointExtension>\r\n", file);
                fprintf(file, "                        <gpxtpx:hr>%d</gpxtpx:hr>\r\n", heart_rate);
                fputs("                    </gpxtpx:TrackPointExtension>\r\n"
                      "                </extensions>\r\n", file);
            }
            fputs(        "            </trkpt>\r\n", file);
            break;
        case TAG_HEART_RATE:
            heart_rate = record->heart_rate.heart_rate;
            break;
        }
    }

    fputs("        </trkseg>\r\n    </trk>\r\n</gpx>\r\n", file);
}

