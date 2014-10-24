/*****************************************************************************\
** export_gpx.c                                                              **
** GPX export code                                                           **
\*****************************************************************************/

#include "ttbin.h"

void export_gpx(TTBIN_FILE *ttbin, FILE *file)
{
    uint32_t i;

    if (!ttbin->gps_records)
        return;

    fprintf(file, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n");
    fprintf(file, "<gpx xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
        " xsi:schemaLocation=\"http://www.topografix.com/GPX/1/1"
        " http://www.topografix.com/GPX/1/1/gpx.xsd"
        " http://www.garmin.com/xmlschemas/GpxExtensions/v3"
        " http://www.garmin.com/xmlschemas/GpxExtensionsv3.xsd"
        " http://www.garmin.com/xmlschemas/TrackPointExtension/v1"
        " http://www.garmin.com/xmlschemas/TrackPointExtensionv1.xsd\""
        " xmlns:gpxx=\"http://www.garmin.com/xmlschemas/GpxExtensions/v3\""
        " xmlns:gpxtpx=\"http://www.garmin.com/xmlschemas/TrackPointExtension/v1\""
        " version=\"1.1\" creator=\"TomTom\" xmlns=\"http://www.topografix.com/GPX/1/1\">\r\n");
    fprintf(file, "    <metadata>\r\n        <name>%s</name>\r\n    </metadata>\r\n",
        create_filename(ttbin, "gpx"));
    fprintf(file, "    <trk>\r\n        <name>");
    switch(ttbin->activity)
    {
    case ACTIVITY_RUNNING:   fprintf(file, "RUNNING");   break;
    case ACTIVITY_CYCLING:   fprintf(file, "CYCLING");   break;
    case ACTIVITY_SWIMMING:  fprintf(file, "POOL SWIM"); break;
    case ACTIVITY_TREADMILL: fprintf(file, "TREADMILL"); break;
    case ACTIVITY_FREESTYLE: fprintf(file, "FREESTYLE"); break;
    default:                 fprintf(file, "UNKNOWN");   break;
    }
    fprintf(file, "</name>\r\n        <trkseg>\r\n");

    for (i = 0; i < ttbin->gps_record_count; ++i)
    {
        char timestr[32];
        strftime(timestr, sizeof(timestr), "%FT%X.000Z", gmtime(&ttbin->gps_records[i].timestamp));
        fprintf(file, "            <trkpt lon=\"%.6f\" lat=\"%.6f\">\r\n",
            ttbin->gps_records[i].longitude, ttbin->gps_records[i].latitude);
        fprintf(file, "                <ele>%d</ele>\r\n", (int)ttbin->gps_records[i].elevation);
        fprintf(file, "                <time>%s</time>\r\n", timestr);
        fprintf(file, "            </trkpt>\r\n");
    }

    fprintf(file, "        </trkseg>\r\n    </trk>\r\n</gpx>\r\n");
}

