/*****************************************************************************\
** export_tcx.c                                                              **
** TCX export code                                                           **
\*****************************************************************************/

#include "ttbin.h"

#include <math.h>

void export_tcx(TTBIN_FILE *ttbin, FILE *file)
{
    uint32_t i;
    char timestr[32];
    TTBIN_RECORD *record;
    float max_speed = 0.0f;
    float total_speed = 0.0f;
    uint32_t total_heart_rate = 0;
    uint32_t max_heart_rate = 0;
    uint32_t heart_rate_count = 0;
    uint32_t gps_count = 0;
    unsigned heart_rate;
    int lap_state;
    int insert_pause;
    float lap_avg_speed;
    unsigned lap_time, lap_start_time = 0;
    float lap_distance, lap_start_distance = 0.0f;
    float lap_max_speed;
    unsigned lap_calories, lap_start_calories = 0;
    unsigned lap_heart_rate_count;
    unsigned lap_avg_heart_rate;
    unsigned lap_max_heart_rate;
    const char *trigger_method = "Manual";

    if (!ttbin->gps_records.count)
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
    case ACTIVITY_FREESTYLE: fputs("Freestyle", file); break;
    default:                 fputs("Unknown", file);   break;
    }
    fputs("\">\r\n"
          "            <Id>", file);
    strftime(timestr, sizeof(timestr), "%FT%X.000Z", gmtime(&ttbin->timestamp_utc));
    fputs(timestr, file);
    fputs("</Id>\r\n", file);

    fprintf(file, "            <Lap StartTime=\"%s\">\r\n", timestr);
    fputs(        "                <Intensity>Active</Intensity>\r\n"
                  "                <Track>\r\n", file);

    heart_rate = 0;
    lap_state = 0;
    insert_pause = 0;
    for (record = ttbin->first; record; record = record->next)
    {
        switch (record->tag)
        {
        case TAG_TRAINING_SETUP:
            switch (record->training_setup.type)
            {
            case 7: trigger_method="Time";     break;
            case 8: trigger_method="Distance"; break;
            }
            break;

        case TAG_STATUS:
            if ((record->status.status == 2) && (lap_state == 0))
                insert_pause = 1;
            break;

        case TAG_GPS:
            /* this will happen if the activity is paused and then resumed, or if the GPS signal is lost  */
            if ((record->gps.timestamp == 0) || ((record->gps.latitude == 0) && (record->gps.longitude == 0)))
                break;

            if (record->gps.instant_speed > max_speed)
                max_speed = record->gps.instant_speed;
            total_speed += record->gps.instant_speed;
            ++gps_count;

            if (lap_state == 0 && insert_pause)
            {
                /* Garmin's tools use multiple tracks within a lap to signal a pause */
                insert_pause = 0;
                fputs("                </Track>\r\n"
                      "                <Track>\r\n", file);
            }

            if (lap_state == 1)
            {
                fprintf(file, "            <Lap StartTime=\"%s\">\r\n", timestr);
                fputs(        "                <Intensity>Active</Intensity>\r\n"
                              "                <Track>\r\n", file);
                lap_state = 0;
            }

            strftime(timestr, sizeof(timestr), "%FT%X.000Z", gmtime(&record->gps.timestamp));
            fputs(        "                    <Trackpoint>\r\n", file);
            fprintf(file, "                        <Time>%s</Time>\r\n", timestr);
            fputs(        "                        <Position>\r\n", file);
            fprintf(file, "                            <LatitudeDegrees>%.7f</LatitudeDegrees>\r\n", record->gps.latitude);
            fprintf(file, "                            <LongitudeDegrees>%.7f</LongitudeDegrees>\r\n", record->gps.longitude);
            fputs(        "                        </Position>\r\n", file);
            if (!isnan(record->gps.elevation))
                fprintf(file, "                        <AltitudeMeters>%.0f</AltitudeMeters>\r\n", record->gps.elevation);
            fprintf(file, "                        <DistanceMeters>%.5f</DistanceMeters>\r\n", record->gps.cum_distance);
            if (heart_rate > 0)
            {
                fputs(        "                        <HeartRateBpm>\r\n", file);
                fprintf(file, "                            <Value>%d</Value>\r\n", heart_rate);
                fputs(        "                        </HeartRateBpm>\r\n", file);
            }
            fputs(        "                        <Extensions>\r\n"
                          "                            <TPX xmlns=\"http://www.garmin.com/xmlschemas/ActivityExtension/v2\">\r\n", file);
            fprintf(file, "                                <Speed>%.2f</Speed>\r\n", record->gps.instant_speed);
            fputs(        "                            </TPX>\r\n"
                          "                        </Extensions>\r\n"
                          "                    </Trackpoint>\r\n", file);
            if (lap_state == 2)
            {
                fputs(        "                    <Extensions>\r\n"
                              "                       <LX xmlns=\"http://www.garmin.com/xmlschemas/ActivityExtension/v2\">\r\n", file);
                fprintf(file, "                           <AvgSpeed>%.5f</AvgSpeed>\r\n", lap_avg_speed);
                fputs(        "                       </LX>\r\n"
                              "                    </Extensions>\r\n", file);
                fputs(        "                </Track>\r\n", file);
                fprintf(file, "                <TriggerMethod>%s</TriggerMethod>\r\n", trigger_method);
                fprintf(file, "                <TotalTimeSeconds>%d</TotalTimeSeconds>\r\n", lap_time);
                fprintf(file, "                <DistanceMeters>%.2f</DistanceMeters>\r\n", lap_distance);
                fprintf(file, "                <MaximumSpeed>%.2f</MaximumSpeed>\r\n", lap_max_speed);
                fprintf(file, "                <Calories>%d</Calories>\r\n", lap_calories);
                if (heart_rate_count)
                {
                    fputs(        "                <AverageHeartRateBpm>\r\n", file);
                    fprintf(file, "                    <Value>%d</Value>\r\n", lap_avg_heart_rate);
                    fputs(        "                </AverageHeartRateBpm>\r\n", file);
                    fputs(        "                <MaximumHeartRateBpm>\r\n", file);
                    fprintf(file, "                    <Value>%d</Value>\r\n", lap_max_heart_rate);
                    fputs(        "                </MaximumHeartRateBpm>\r\n", file);
                }
                fputs(        "            </Lap>\r\n", file);
                lap_state = 1;
            }
            heart_rate = 0;
            break;
        case TAG_HEART_RATE:
            if (record->heart_rate.heart_rate > max_heart_rate)
                max_heart_rate = record->heart_rate.heart_rate;
            total_heart_rate += record->heart_rate.heart_rate;
            ++heart_rate_count;

            heart_rate = record->heart_rate.heart_rate;
            break;
        case TAG_LAP:
            lap_avg_speed = total_speed / gps_count;
            lap_time = record->lap.total_time - lap_start_time;
            lap_distance = record->lap.total_distance - lap_start_distance;
            lap_max_speed = max_speed;
            lap_calories = record->lap.total_calories - lap_start_calories;
            lap_heart_rate_count = heart_rate_count;
            if (heart_rate_count > 0)
                lap_avg_heart_rate = (total_heart_rate + (heart_rate_count >> 1)) / heart_rate_count;
            lap_max_heart_rate = max_heart_rate;
            gps_count = 0;
            heart_rate_count = 0;
            total_speed = 0;
            max_speed = 0;
            max_heart_rate = 0;
            total_heart_rate = 0;
            lap_state = 2;
            lap_start_time = record->lap.total_time;
            lap_start_distance = record->lap.total_distance;
            lap_start_calories = record->lap.total_calories;
            break;
        }
    }

    if (lap_state != 1)
    {
        if (!lap_state)
        {
            lap_avg_speed = total_speed / gps_count;
            lap_time = ttbin->duration - lap_start_time;
            lap_distance = ttbin->total_distance - lap_start_distance;
            lap_max_speed = max_speed;
            lap_calories = ttbin->total_calories - lap_start_calories;
            lap_heart_rate_count = heart_rate_count;
            if (heart_rate_count > 0)
                lap_avg_heart_rate = (total_heart_rate + (heart_rate_count >> 1)) / heart_rate_count;
            lap_max_heart_rate = max_heart_rate;
        }

        fputs(        "                    <Extensions>\r\n"
                      "                       <LX xmlns=\"http://www.garmin.com/xmlschemas/ActivityExtension/v2\">\r\n", file);
        fprintf(file, "                           <AvgSpeed>%.5f</AvgSpeed>\r\n", lap_avg_speed);
        fputs(        "                       </LX>\r\n"
                      "                    </Extensions>\r\n", file);
        fputs(        "                </Track>\r\n", file);
        fprintf(file, "                <TriggerMethod>%s</TriggerMethod>\r\n", trigger_method);
        fprintf(file, "                <TotalTimeSeconds>%d</TotalTimeSeconds>\r\n", lap_time);
        fprintf(file, "                <DistanceMeters>%.2f</DistanceMeters>\r\n", lap_distance);
        fprintf(file, "                <MaximumSpeed>%.2f</MaximumSpeed>\r\n", lap_max_speed);
        fprintf(file, "                <Calories>%d</Calories>\r\n", lap_calories);
        if (heart_rate_count)
        {
            fputs(        "                <AverageHeartRateBpm>\r\n", file);
            fprintf(file, "                    <Value>%d</Value>\r\n", lap_avg_heart_rate);
            fputs(        "                </AverageHeartRateBpm>\r\n", file);
            fputs(        "                <MaximumHeartRateBpm>\r\n", file);
            fprintf(file, "                    <Value>%d</Value>\r\n", lap_max_heart_rate);
            fputs(        "                </MaximumHeartRateBpm>\r\n", file);
        }
        fputs(        "            </Lap>\r\n", file);
    }

    fputs(        "            <Creator xsi:type=\"Device_t\">\r\n"
                  "                <Name>TomTom GPS Sport Watch</Name>\r\n"
                  "                <UnitId>0</UnitId>\r\n"
                  "                <ProductID>0</ProductID>\r\n"
                  "                <Version>\r\n", file);
    fprintf(file, "                    <VersionMajor>%d</VersionMajor>\r\n", ttbin->firmware_version[0]);
    fprintf(file, "                    <VersionMinor>%d</VersionMinor>\r\n", ttbin->firmware_version[1]);
    fprintf(file, "                    <BuildMajor>%d</BuildMajor>\r\n", ttbin->firmware_version[2]);
    fputs(        "                    <BuildMinor>0</BuildMinor>\r\n"
                  "                </Version>\r\n"
                  "            </Creator>\r\n"
                  "        </Activity>\r\n"
                  "    </Activities>\r\n"
                  "</TrainingCenterDatabase>\r\n", file);
}

