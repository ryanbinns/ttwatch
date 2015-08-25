/*****************************************************************************\
** export_tcx.c                                                              **
** TCX export code                                                           **
\*****************************************************************************/

#include "ttbin.h"

#include <math.h>

void export_tcx(TTBIN_FILE *ttbin, FILE *file)
{
    char timestr[32];
    TTBIN_RECORD *record;
    float max_speed = 0.0f;
    float total_speed = 0.0f;
    uint32_t total_heart_rate = 0;
    uint32_t max_heart_rate = 0;
    uint32_t heart_rate_count = 0;
    uint32_t move_count = 0;
    uint32_t total_step_count = 0;
    unsigned heart_rate;
    int lap_state;
    int insert_pause;
    float lap_avg_speed;
    unsigned lap_time, lap_start_time = 0;
    float lap_distance, lap_start_distance = 0.0f;
    float lap_max_speed;
    unsigned lap_calories, lap_start_calories = 0;
    unsigned lap_heart_rate_count;
    unsigned lap_start_heart_rate_count;
    unsigned lap_avg_heart_rate;
    unsigned lap_max_heart_rate;
    unsigned lap_step_count;
    float cadence_avg = 0.0f;
    const char *trigger_method = "Manual";
    double distance_factor = 1;
    uint32_t steps, steps_prev = 0;

    if (ttbin->activity == ACTIVITY_TREADMILL) {
        for (record = ttbin->last; record; record = record->prev)
        {
            if ((record->tag == TAG_TREADMILL) && record->treadmill.distance)
            {
                distance_factor = ttbin->total_distance / record->treadmill.distance;
                break;
            }
        }
    } else if (!ttbin->gps_records.count)
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
    case ACTIVITY_TREADMILL: fputs("Running", file);   break; /* per Garmin spec, not "Treadmill" */
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

        case TAG_TREADMILL:
        case TAG_GPS:
            if (record->tag==TAG_TREADMILL) {
                /* this will happen if the activity is paused and then resumed */
                if (record->treadmill.timestamp == 0)
                    break;

                steps = record->treadmill.steps - steps_prev;
                steps_prev = record->treadmill.steps;

                total_step_count += steps;
            } else { /* TAG_GPS only */
                /* this will happen if the activity is paused and then resumed, or if the GPS signal is lost  */
                if ((record->gps.timestamp == 0) || ((record->gps.latitude == 0) && (record->gps.longitude == 0)))
                    break;

                if (record->gps.instant_speed > max_speed)
                    max_speed = record->gps.instant_speed;
                total_speed += record->gps.instant_speed;

                if (ttbin->activity==ACTIVITY_RUNNING)
                    total_step_count += record->gps.cycles;
            }

            /* code common to both TAG_GPS and TAG_TREADMILL */
            ++move_count;

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

            if (record->tag==TAG_GPS)
                strftime(timestr, sizeof(timestr), "%FT%X.000Z", gmtime(&record->gps.timestamp));
            else if (record->tag==TAG_TREADMILL)
                strftime(timestr, sizeof(timestr), "%FT%X.000Z", gmtime(&record->treadmill.timestamp));

            fputs(        "                    <Trackpoint>\r\n", file);
            fprintf(file, "                        <Time>%s</Time>\r\n", timestr);
            if (record->tag==TAG_GPS) {
                fputs(        "                        <Position>\r\n", file);
                fprintf(file, "                            <LatitudeDegrees>%.7f</LatitudeDegrees>\r\n", record->gps.latitude);
                fprintf(file, "                            <LongitudeDegrees>%.7f</LongitudeDegrees>\r\n", record->gps.longitude);
                fputs(        "                        </Position>\r\n", file);
                if (!isnan(record->gps.elevation))
                    fprintf(file, "                        <AltitudeMeters>%.0f</AltitudeMeters>\r\n", record->gps.elevation);
            }

            if (record->tag==TAG_GPS)
                fprintf(file, "                        <DistanceMeters>%.5f</DistanceMeters>\r\n", record->gps.cum_distance);
            else if (record->tag==TAG_TREADMILL)
                fprintf(file, "                        <DistanceMeters>%.5f</DistanceMeters>\r\n", record->treadmill.distance * distance_factor);

            if (heart_rate > 0)
            {
                fputs(        "                        <HeartRateBpm>\r\n", file);
                fprintf(file, "                            <Value>%d</Value>\r\n", heart_rate);
                fputs(        "                        </HeartRateBpm>\r\n", file);
            }

            fputs(        "                        <Extensions>\r\n"
                          "                            <TPX xmlns=\"http://www.garmin.com/xmlschemas/ActivityExtension/v2\">\r\n", file);
            if (record->tag==TAG_GPS)
                fprintf(file, "                                <Speed>%.2f</Speed>\r\n", record->gps.instant_speed);
            if (ttbin->activity==ACTIVITY_RUNNING || ttbin->activity==ACTIVITY_TREADMILL)
                /* use an exponential moving average to smooth cadence data */
                if ((int)record->gps.cycles <= 4) // max 4 * 60 = 240 spm
                    cadence_avg = (0.05 * 30 * (int)record->gps.cycles) + (1.0 - 0.05) * cadence_avg;
                fprintf(file, "                                <RunCadence>%d</RunCadence>\r\n", (int)cadence_avg);
            fputs(        "                            </TPX>\r\n"
                          "                        </Extensions>\r\n"
                          "                    </Trackpoint>\r\n", file);
            if (lap_state == 2)
            {
                fputs(        "                    <Extensions>\r\n"
                              "                       <LX xmlns=\"http://www.garmin.com/xmlschemas/ActivityExtension/v2\">\r\n", file);
                fprintf(file, "                           <AvgSpeed>%.5f</AvgSpeed>\r\n", lap_avg_speed);
                if (lap_step_count)
                    fprintf(file, "                           <Steps>%d</Steps>\r\n"
                              "                           <AvgRunCadence>%d</AvgRunCadence>\r\n", lap_step_count, 30*lap_step_count/lap_time);
                fputs(        "                       </LX>\r\n"
                              "                    </Extensions>\r\n", file);
                fputs(        "                </Track>\r\n", file);
                fprintf(file, "                <TriggerMethod>%s</TriggerMethod>\r\n", trigger_method);
                fprintf(file, "                <TotalTimeSeconds>%d</TotalTimeSeconds>\r\n", lap_time);
                fprintf(file, "                <DistanceMeters>%.2f</DistanceMeters>\r\n", lap_distance);
                if (record->tag==TAG_GPS)
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
            lap_time = record->lap.total_time - lap_start_time;
            if (ttbin->activity == ACTIVITY_TREADMILL) {
                lap_distance = distance_factor * (double)(record->lap.total_distance - lap_start_distance);
                lap_avg_speed = lap_distance / move_count;
            } else {
                lap_distance = record->lap.total_distance - lap_start_distance;
                lap_avg_speed = total_speed / move_count;
            }
            lap_max_speed = max_speed;
            lap_calories = record->lap.total_calories - lap_start_calories;
            lap_heart_rate_count = heart_rate_count - lap_start_heart_rate_count;;
            if (lap_heart_rate_count > 0)
                lap_avg_heart_rate = (total_heart_rate + (lap_heart_rate_count >> 1)) / lap_heart_rate_count;
            lap_max_heart_rate = max_heart_rate;
            lap_step_count = total_step_count;
            move_count = 0;
            heart_rate_count = 0;
            total_speed = 0;
            max_speed = 0;
            max_heart_rate = 0;
            total_heart_rate = 0;
            total_step_count = 0;
            lap_state = 2;
            lap_start_time = record->lap.total_time;
            lap_start_distance = record->lap.total_distance;
            lap_start_calories = record->lap.total_calories;
            lap_start_heart_rate_count = heart_rate_count;
            break;
        }
    }

    if (lap_state != 1)
    {
        if (!lap_state)
        {
            lap_time = ttbin->duration - lap_start_time;
            lap_distance = ttbin->total_distance - lap_start_distance;
            if (ttbin->activity == ACTIVITY_TREADMILL)
                lap_avg_speed = lap_distance / move_count;
            else
                lap_avg_speed = total_speed / move_count;
            lap_max_speed = max_speed;
            lap_calories = ttbin->total_calories - lap_start_calories;
            lap_heart_rate_count = heart_rate_count - lap_start_heart_rate_count;;
            if (lap_heart_rate_count > 0)
                lap_avg_heart_rate = (total_heart_rate + (lap_heart_rate_count >> 1)) / lap_heart_rate_count;
            lap_max_heart_rate = max_heart_rate;
            lap_step_count = total_step_count;
        }

        fputs(        "                    <Extensions>\r\n"
                      "                       <LX xmlns=\"http://www.garmin.com/xmlschemas/ActivityExtension/v2\">\r\n", file);
        fprintf(file, "                           <AvgSpeed>%.5f</AvgSpeed>\r\n", lap_avg_speed);
        if (lap_step_count)
            fprintf(file, "                           <Steps>%d</Steps>\r\n"
                      "                           <AvgRunCadence>%d</AvgRunCadence>\r\n", lap_step_count, 30*lap_step_count/lap_time);
        fputs(        "                       </LX>\r\n"
                      "                    </Extensions>\r\n", file);
        fputs(        "                </Track>\r\n", file);
        fprintf(file, "                <TriggerMethod>%s</TriggerMethod>\r\n", trigger_method);
        fprintf(file, "                <TotalTimeSeconds>%d</TotalTimeSeconds>\r\n", lap_time);
        fprintf(file, "                <DistanceMeters>%.2f</DistanceMeters>\r\n", lap_distance);
        if (ttbin->gps_records.count)
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

