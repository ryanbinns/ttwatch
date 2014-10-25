/*****************************************************************************\
** export_gpx.c                                                              **
** GPX export code                                                           **
\*****************************************************************************/

#include "ttbin.h"

void export_gpx(TTBIN_FILE *ttbin, FILE *file)
{
    uint32_t i;
    char timestr[32];

    if (!ttbin->gps_records)
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

    for (i = 0; i < ttbin->gps_record_count; ++i)
    {
        if ((ttbin->gps_records[i].timestamp != 0) &&
            !((ttbin->gps_records[i].latitude == 0) && (ttbin->gps_records[i].longitude == 0)))
        {
            strftime(timestr, sizeof(timestr), "%FT%X.000Z", gmtime(&ttbin->gps_records[i].timestamp));
            fprintf(file, "            <trkpt lon=\"%.6f\" lat=\"%.6f\">\r\n",
                ttbin->gps_records[i].longitude, ttbin->gps_records[i].latitude);
            fprintf(file, "                <ele>%d</ele>\r\n", (int)ttbin->gps_records[i].elevation);
            fputs(        "                <time>", file);
            fputs(timestr, file);
            fputs("</time>\r\n"
                          "            </trkpt>\r\n", file);
        }
    }

    fputs(        "        </trkseg>\r\n    </trk>\r\n</gpx>\r\n", file);
}

