/*****************************************************************************\
** export_tcx.c                                                              **
** TCX export code                                                           **
\*****************************************************************************/

#include "ttbin.h"

void export_tcx(TTBIN_FILE *ttbin, FILE *file)
{
    uint32_t i, j, lap;
    char timestr[32];

    if (!ttbin->gps_records)
        return;

    fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
          "<TrainingCenterDatabase xsi:schemaLocation=\"http://www.garmin.com/xmlschemas/TrainingCenterDatabase/v2"
          " http://www.garmin.com/xmlschemas/TrainingCenterDatabasev2.xsd\""
          " xmlns=\"http://www.garmin.com/xmlschemas/TrainingCenterDatabase/v2\""
          " xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
          " xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\""
          " xmlns:ns2=\"http://www.garmin.com/xmlschemas/ActivityExtension/v2\">\r\n"
          "    <Activities>\r\n"
          "        <Activity Sport=\"", file);
    switch(ttbin->activity)
    {
    case ACTIVITY_RUNNING:   fputs("Running", file);   break;
    case ACTIVITY_CYCLING:   fputs("Cycling", file);   break;
    case ACTIVITY_SWIMMING:  fputs("Pool Swim", file); break;
    case ACTIVITY_TREADMILL: fputs("Treadmill", file); break;
    case ACTIVITY_FREESTYLE: fputs("Fresstyle", file); break;
    default:                 fputs("Unknown", file);   break;
    }
    fputs("\">\r\n"
          "            <Id>", file);
    strftime(timestr, sizeof(timestr), "%FT%X.000Z", gmtime(&ttbin->timestamp_utc));
    fputs(timestr, file);
    fputs("</Id>\r\n", file);

    j = 0;
    for (lap = 0; lap <= ttbin->lap_record_count; ++lap)
    {
        float max_speed = 0.0f;
        float total_speed = 0.0f;
        uint32_t total_heart_rate = 0;
        uint32_t max_heart_rate = 0;
        uint32_t heart_rate_count = 0;
        uint32_t old_j = j;

        if (lap != 0)
        {
            time_t timestamp = ttbin->timestamp_utc + ttbin->lap_records[lap - 1].total_time;
            strftime(timestr, sizeof(timestr), "%FT%X.000Z", gmtime(&timestamp));
        }
        fprintf(file, "            <Lap StartTime=\"%s\">\r\n", timestr);

        while ((j < ttbin->gps_record_count) &&
            ((lap >= ttbin->lap_record_count) ||
            ((ttbin->gps_records[j].timestamp - ttbin->timestamp_utc) <= ttbin->lap_records[lap].total_time)))
        {
            /* this will happen if the activity is paused and then resumed, or if the GPS signal is lost  */
            if ((ttbin->gps_records[j].timestamp == 0) || ((ttbin->gps_records[j].latitude == 0) && (ttbin->gps_records[j].longitude == 0)))
            {
                ++j;
                continue;
            }

            if (ttbin->gps_records[j].speed > max_speed)
                max_speed = ttbin->gps_records[j].speed;
            total_speed += ttbin->gps_records[j].speed;
            if ((j < ttbin->heart_rate_record_count) && (ttbin->heart_rate_records[j].heart_rate > 0))
            {
                if (ttbin->heart_rate_records[j].heart_rate > max_heart_rate)
                    max_heart_rate = ttbin->heart_rate_records[j].heart_rate;
                total_heart_rate += ttbin->heart_rate_records[j].heart_rate;
                ++heart_rate_count;
            }
            ++j;
        }
        total_speed /= j - old_j;

        fprintf(file, "                <TotalTimeSeconds>%d</TotalTimeSeconds>\r\n",
            (lap < ttbin->lap_record_count) ? ttbin->lap_records[lap].total_time : ttbin->duration);
        fprintf(file, "                <DistanceMeters>%.2f</DistanceMeters>\r\n",
            (lap < ttbin->lap_record_count) ? ttbin->lap_records[lap].total_distance : ttbin->total_distance);
        fprintf(file, "                <MaximumSpeed>%.2f</MaximumSpeed>\r\n", max_speed);
        fprintf(file, "                <Calories>%d</Calories>\r\n",
            (lap < ttbin->lap_record_count) ? ttbin->lap_records[lap].total_calories : ttbin->total_calories);

        if (heart_rate_count)
        {
            fputs(        "                <AverageHeartRateBpm>\r\n", file);
            fprintf(file, "                    <Value>%d</Value>\r\n",
                (total_heart_rate + (heart_rate_count >> 1)) / heart_rate_count);
            fputs(        "                </AverageHeartRateBpm>\r\n", file);
            fputs(        "                <MaximumHeartRateBpm>\r\n", file);
            fprintf(file, "                    <Value>%d</Value>\r\n", max_heart_rate);
            fputs(        "                </MaximumHeartRateBpm>\r\n", file);
        }

        fputs(        "                <Intensity>Active</Intensity>\r\n"
                      "                <TriggerMethod>Manual</TriggerMethod>\r\n"
                      "                <Track>\r\n", file);

        j = old_j;
        while ((j < ttbin->gps_record_count) &&
            ((lap >= ttbin->lap_record_count) ||
            ((ttbin->gps_records[j].timestamp - ttbin->timestamp_utc) <= ttbin->lap_records[lap].total_time)))
        {
            /* this will happen if the activity is paused and then resumed, or if the GPS signal is lost  */
            if ((ttbin->gps_records[j].timestamp == 0) || ((ttbin->gps_records[j].latitude == 0) && (ttbin->gps_records[j].longitude == 0)))
            {
                ++j;
                continue;
            }

            strftime(timestr, sizeof(timestr), "%FT%X.000Z", gmtime(&ttbin->gps_records[j].timestamp));

            fputs(        "                    <Trackpoint>\r\n", file);
            fprintf(file, "                        <Time>%s</Time>\r\n", timestr);
            fputs(        "                        <Position>\r\n", file);
            fprintf(file, "                            <LatitudeDegrees>%.7f</LatitudeDegrees>\r\n", ttbin->gps_records[j].latitude);
            fprintf(file, "                            <LongitudeDegrees>%.7f</LongitudeDegrees>\r\n", ttbin->gps_records[j].longitude);
            fputs(        "                        </Position>\r\n", file);
            fprintf(file, "                        <AltitudeMeters>%.0f</AltitudeMeters>\r\n", ttbin->gps_records[j].elevation);
            fprintf(file, "                        <DistanceMeters>%.5f</DistanceMeters>\r\n", ttbin->gps_records[j].cum_distance);
            if ((j < ttbin->heart_rate_record_count) && (ttbin->heart_rate_records[j].heart_rate > 0))
            {
                fputs(        "                        <HeartRateBpm>\r\n", file);
                fprintf(file, "                            <Value>%d</Value>\r\n", ttbin->heart_rate_records[j].heart_rate);
                fputs(        "                        </HeartRateBpm>\r\n", file);
            }
            fputs(        "                        <Extensions>\r\n"
                          "                            <TPX xmlns=\"http://www.garmin.com/xmlschemas/ActivityExtension/v2\">\r\n", file);
            fprintf(file, "                                <Speed>%.2f</Speed>\r\n", ttbin->gps_records[j].speed);
            fputs(        "                            </TPX>\r\n"
                          "                        </Extensions>\r\n"
                          "                    </Trackpoint>\r\n", file);
            ++j;
        }
        fputs(        "                    <Extensions>\r\n"
                      "                       <LX xmlns=\"http://www.garmin.com/xmlschemas/ActivityExtension/vs\">\r\n", file);
        fprintf(file, "                           <AvgSpeed>%.5f</AvgSpeed>\r\n", ttbin->total_distance / ttbin->duration);
        fputs(        "                       </LX>\r\n"
                      "                    </Extensions>\r\n", file);

        fputs(        "                </Track>\r\n"
                      "            </Lap>\r\n", file);
    }

    fputs(        "            <Creator xsi:type=\"Device_t\">\r\n"
                  "                <Name>TomTom GPS Sport Watch</Name>\r\n"
                  "                <UnitId>0</UnitId>\r\n"
                  "                <ProductID>0</ProductID>\r\n"
                  "                <Version>\r\n", file);
    fprintf(file, "                    <VersionMajor>%d</VersionMajor>\r\n", ttbin->firmware_version[1]);
    fprintf(file, "                    <VersionMinor>%d</VersionMinor>\r\n", ttbin->firmware_version[2]);
    fprintf(file, "                    <BuildMajor>%d</BuildMajor>\r\n", ttbin->firmware_version[3]);
    fputs(        "                    <BuildMinor>0</BuildMinor>\r\n"
                  "                </Version>\r\n"
                  "            </Creator>\r\n"
                  "        </Activity>\r\n"
                  "    </Activities>\r\n"
                  "</TrainingCenterDatabase>\r\n", file);
}

