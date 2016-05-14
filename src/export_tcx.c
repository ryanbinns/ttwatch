/*****************************************************************************\
** export_tcx.c                                                              **
** TCX export code                                                           **
\*****************************************************************************/

#include "ttbin.h"

#include <math.h>

enum LapState
{
    LapState_None,      /* the next GPS/treadmill record is processed normally */
    LapState_Start,     /* the next GPS/treadmill record starts a lap */
    LapState_Finish     /* the next GPS/treadmill record finishes a lap */
};

struct LapData
{
    const char *intensity;
    float avg_speed;
    unsigned step_count;
    unsigned time;
    const char *trigger_method;
    float distance;
    float max_speed;
    unsigned calories;
    unsigned avg_heart_rate;
    unsigned max_heart_rate;
};

static void write_lap_finish(FILE *file, const struct LapData *lap)
{
    fputs(        "                    <Extensions>\r\n"
                  "                       <LX xmlns=\"http://www.garmin.com/xmlschemas/ActivityExtension/v2\">\r\n", file);
    fprintf(file, "                           <AvgSpeed>%.5f</AvgSpeed>\r\n", lap->avg_speed);
    if (lap->step_count)
        fprintf(file, "                           <Steps>%d</Steps>\r\n"
                  "                           <AvgRunCadence>%d</AvgRunCadence>\r\n", lap->step_count, 30*lap->step_count/lap->time);
    fputs(        "                       </LX>\r\n"
                  "                    </Extensions>\r\n", file);
    fputs(        "                </Track>\r\n", file);
    fprintf(file, "                <Intensity>%s</Intensity>\r\n", lap->intensity);
    fprintf(file, "                <TriggerMethod>%s</TriggerMethod>\r\n", lap->trigger_method);
    fprintf(file, "                <TotalTimeSeconds>%d</TotalTimeSeconds>\r\n", lap->time);
    fprintf(file, "                <DistanceMeters>%.2f</DistanceMeters>\r\n", lap->distance);
    if (lap->max_speed > 0.0f)
        fprintf(file, "                <MaximumSpeed>%.2f</MaximumSpeed>\r\n", lap->max_speed);
    fprintf(file, "                <Calories>%d</Calories>\r\n", lap->calories);
    if (lap->avg_heart_rate > 0)
    {
        fputs(        "                <AverageHeartRateBpm>\r\n", file);
        fprintf(file, "                    <Value>%d</Value>\r\n", lap->avg_heart_rate);
        fputs(        "                </AverageHeartRateBpm>\r\n", file);
        fputs(        "                <MaximumHeartRateBpm>\r\n", file);
        fprintf(file, "                    <Value>%d</Value>\r\n", lap->max_heart_rate);
        fputs(        "                </MaximumHeartRateBpm>\r\n", file);
    }
    fputs(        "            </Lap>\r\n", file);
}

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
    enum LapState lap_state;
    int insert_pause;
    unsigned lap_start_time = 0;
    float lap_start_distance = 0.0f;
    unsigned lap_start_calories = 0;
    float cadence_avg = 0.0f;
    double distance_factor = 1;
    uint32_t steps, steps_prev = 0;
    time_t timestamp;
    float distance;

    struct LapData lap = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    lap.trigger_method = "Manual";
    lap.intensity = "Active";

    if (ttbin->activity == ACTIVITY_TREADMILL)
    {
        for (record = ttbin->last; record; record = record->prev)
        {
            if ((record->tag == TAG_TREADMILL) && record->treadmill.distance)
            {
                distance_factor = ttbin->total_distance / record->treadmill.distance;
                break;
            }
        }
    }
    else if (!ttbin->gps_records.count)
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

    heart_rate = 0;
    lap_state = LapState_Start; /* the first GPS/treadmill record should start a lap */
    insert_pause = 0;
    for (record = ttbin->first; record; record = record->next)
    {
        switch (record->tag)
        {
        case TAG_TRAINING_SETUP:
            switch (record->training_setup.type)
            {
            case TRAINING_LAPS_TIME:     lap.trigger_method = "Time";     break;
            case TRAINING_LAPS_DISTANCE: lap.trigger_method = "Distance"; break;
            }
            break;

        case TAG_STATUS:
            if ((record->status.status == TTBIN_STATUS_PAUSED) && (lap_state == LapState_None))
                insert_pause = 1;
            break;

        case TAG_TREADMILL:
        case TAG_GPS:
            if (record->tag == TAG_TREADMILL)
            {
                /* this will happen if the activity is paused and then resumed */
                if (record->treadmill.timestamp == 0)
                    break;

                steps = record->treadmill.steps - steps_prev;
                steps_prev = record->treadmill.steps;

                total_step_count += steps;
                timestamp = record->treadmill.timestamp;
                distance = record->treadmill.distance * distance_factor;
            }
            else /* TAG_GPS only */
            {
                /* this will happen if the activity is paused and then resumed, or if the GPS signal is lost */
                if ((record->gps.timestamp == 0) || ((record->gps.latitude == 0) && (record->gps.longitude == 0)))
                    break;

                if (record->gps.instant_speed > max_speed)
                    max_speed = record->gps.instant_speed;
                total_speed += record->gps.instant_speed;

                if (ttbin->activity == ACTIVITY_RUNNING)
                    total_step_count += record->gps.cycles;
                timestamp = record->gps.timestamp;
                distance = record->gps.cum_distance;
            }

            /* code common to both TAG_GPS and TAG_TREADMILL */
            ++move_count;

            if ((lap_state == LapState_None) && insert_pause)
            {
                /* Garmin's tools use multiple tracks within a lap to signal a pause */
                insert_pause = 0;
                fputs("                </Track>\r\n"
                      "                <Track>\r\n", file);
            }

            if (lap_state == LapState_Start)
            {
                fprintf(file, "            <Lap StartTime=\"%s\">\r\n", timestr);
                fputs(        "                <Track>\r\n", file);
                lap_state = LapState_None;
            }

            strftime(timestr, sizeof(timestr), "%FT%X.000Z", gmtime(&timestamp));

            fputs(        "                    <Trackpoint>\r\n", file);
            fprintf(file, "                        <Time>%s</Time>\r\n", timestr);
            if (record->tag == TAG_GPS)
            {
                fputs(        "                        <Position>\r\n", file);
                fprintf(file, "                            <LatitudeDegrees>%.7f</LatitudeDegrees>\r\n", record->gps.latitude);
                fprintf(file, "                            <LongitudeDegrees>%.7f</LongitudeDegrees>\r\n", record->gps.longitude);
                fputs(        "                        </Position>\r\n", file);
                if (!isnan(record->gps.elevation))
                    fprintf(file, "                        <AltitudeMeters>%.0f</AltitudeMeters>\r\n", record->gps.elevation);
            }
            fprintf(file, "                        <DistanceMeters>%.5f</DistanceMeters>\r\n", distance);

            if (heart_rate > 0)
            {
                fputs(        "                        <HeartRateBpm>\r\n", file);
                fprintf(file, "                            <Value>%d</Value>\r\n", heart_rate);
                fputs(        "                        </HeartRateBpm>\r\n", file);
            }

            fputs(        "                        <Extensions>\r\n"
                          "                            <TPX xmlns=\"http://www.garmin.com/xmlschemas/ActivityExtension/v2\">\r\n", file);
            if (record->tag == TAG_GPS)
                fprintf(file, "                                <Speed>%.2f</Speed>\r\n", record->gps.instant_speed);
            if (ttbin->activity == ACTIVITY_RUNNING)
            {
                /* use an exponential moving average to smooth cadence data */
                if ((int)record->gps.cycles <= 4) // max 4 * 60 = 240 spm
                    cadence_avg = (0.05 * 30 * (int)record->gps.cycles) + (1.0 - 0.05) * cadence_avg;
                fprintf(file, "                                <RunCadence>%d</RunCadence>\r\n", (int)cadence_avg);
            }
            else if (ttbin->activity == ACTIVITY_TREADMILL)
            {
                /* use an exponential moving average to smooth cadence data */
                if ((int)record->treadmill.steps <= 4) // max 4 * 60 = 240 spm
                    cadence_avg = (0.05 * 30 * (int)record->treadmill.steps) + (1.0 - 0.05) * cadence_avg;
                fprintf(file, "                                <RunCadence>%d</RunCadence>\r\n", (int)cadence_avg);
            }
            fputs(        "                            </TPX>\r\n"
                          "                        </Extensions>\r\n"
                          "                    </Trackpoint>\r\n", file);

            if (lap_state == LapState_Finish)
            {
                write_lap_finish(file, &lap);
                lap_state = LapState_Start;
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
        case TAG_INTERVAL_SETUP:
            break;
        case TAG_INTERVAL_START:
            break;
        case TAG_INTERVAL_FINISH:
            if (record->interval_start.type == TTBIN_INTERVAL_TYPE_WORK)
            {
                lap.intensity = "Active";
            }
            else
            {
                lap.intensity = "Resting";
            }
            lap.time = record->interval_finish.total_time - lap_start_time;
            if (ttbin->activity == ACTIVITY_TREADMILL)
            {
                lap.distance = distance_factor * (double)(record->interval_finish.total_distance - lap_start_distance);
                lap.avg_speed = lap.distance / move_count;
            }
            else
            {
                lap.distance = record->interval_finish.total_distance - lap_start_distance;
                lap.avg_speed = total_speed / move_count;
            }
            lap.max_speed = max_speed;
            lap.calories = record->interval_finish.total_calories - lap_start_calories;
            if (heart_rate_count > 0)
                lap.avg_heart_rate = (total_heart_rate + (heart_rate_count >> 1)) / heart_rate_count;
            else
                lap.avg_heart_rate = 0;
            lap.max_heart_rate = max_heart_rate;
            lap.step_count = total_step_count;
            move_count = 0;
            heart_rate_count = 0;
            total_speed = 0;
            max_speed = 0;
            max_heart_rate = 0;
            total_heart_rate = 0;
            total_step_count = 0;
            lap_state = LapState_Finish;
            lap_start_time = record->interval_finish.total_time;
            lap_start_distance = record->interval_finish.total_distance;
            lap_start_calories = record->interval_finish.total_calories;
            break;
        case TAG_LAP:
            lap.time = record->lap.total_time - lap_start_time;
            if (ttbin->activity == ACTIVITY_TREADMILL)
            {
                lap.distance = distance_factor * (double)(record->lap.total_distance - lap_start_distance);
                lap.avg_speed = lap.distance / move_count;
            }
            else
            {
                lap.distance = record->lap.total_distance - lap_start_distance;
                lap.avg_speed = total_speed / move_count;
            }
            lap.max_speed = max_speed;
            lap.calories = record->lap.total_calories - lap_start_calories;
            if (heart_rate_count > 0)
                lap.avg_heart_rate = (total_heart_rate + (heart_rate_count >> 1)) / heart_rate_count;
            else
                lap.avg_heart_rate = 0;
            lap.max_heart_rate = max_heart_rate;
            lap.step_count = total_step_count;
            move_count = 0;
            heart_rate_count = 0;
            total_speed = 0;
            max_speed = 0;
            max_heart_rate = 0;
            total_heart_rate = 0;
            total_step_count = 0;
            lap_state = LapState_Finish;
            lap_start_time = record->lap.total_time;
            lap_start_distance = record->lap.total_distance;
            lap_start_calories = record->lap.total_calories;
            break;
        }
    }

    if (lap_state != LapState_Start)
    {
        if (lap_state == LapState_None)
        {
            lap.time = ttbin->duration - lap_start_time;
            lap.distance = ttbin->total_distance - lap_start_distance;
            if (ttbin->activity == ACTIVITY_TREADMILL)
                lap.avg_speed = lap.distance / move_count;
            else
                lap.avg_speed = total_speed / move_count;
            lap.max_speed = max_speed;
            lap.calories = ttbin->total_calories - lap_start_calories;
            if (heart_rate_count > 0)
                lap.avg_heart_rate = (total_heart_rate + (heart_rate_count >> 1)) / heart_rate_count;
            else
                lap.avg_heart_rate = 0;
            lap.max_heart_rate = max_heart_rate;
            lap.step_count = total_step_count;
        }

        write_lap_finish(file, &lap);
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

