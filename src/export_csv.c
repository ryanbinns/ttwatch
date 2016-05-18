/*****************************************************************************\
** export_csv.c                                                              **
** CSV export code                                                           **
\*****************************************************************************/

#include "ttbin.h"

#include <math.h>

void export_csv(TTBIN_FILE *ttbin, FILE *file)
{
    uint32_t steps_prev = 0;
    uint32_t current_lap = 1;
    char timestr[32];
    TTBIN_RECORD *record;
    unsigned heart_rate;
    double distance_factor = 1;
    unsigned time;

    fputs("time,activityType,lapNumber,distance,speed,calories,lat,long,elevation,heartRate,cycles,localtime\r\n", file);

    switch (ttbin->activity)
    {
    case ACTIVITY_RUNNING:
    case ACTIVITY_CYCLING:
    case ACTIVITY_FREESTYLE:
        heart_rate = 0;
        current_lap = 1;
        for (record = ttbin->first; record; record = record->next)
        {
            switch (record->tag)
            {
            case TAG_GPS:
                /* this will happen if the activity is paused and then resumed, or if the GPS signal is lost  */
                if ((record->gps.timestamp == 0) || ((record->gps.latitude == 0) && (record->gps.longitude == 0)))
                    continue;

                strftime(timestr, sizeof(timestr), "%FT%X", localtime(&record->gps.timestamp));

                time = (unsigned)(record->gps.timestamp - ttbin->timestamp_utc);
                fprintf(file, "%u,%d,%d,%.5f,%.2f,%d,%.7f,%.7f,",
                    time, ttbin->activity, current_lap, record->gps.cum_distance, record->gps.instant_speed,
                    record->gps.calories, record->gps.latitude, record->gps.longitude);
                if (!isnan(record->gps.elevation))
                    fprintf(file, "%.2f", record->gps.elevation);
                fputs(",", file);
                if (heart_rate > 0)
                    fprintf(file, "%d", heart_rate);
                fprintf(file, ",%d,%s", record->gps.cycles, timestr);
                if (time >= 3600)
                    fprintf(file, ",%d:%02d:%02d\r\n", time / 3600, (time % 3600) / 60, time % 60);
                else
                    fprintf(file, ",%d:%02d\r\n", time / 60, time % 60);
                heart_rate = 0;
                break;
            case TAG_HEART_RATE:
                heart_rate = record->heart_rate.heart_rate;
                break;
            case TAG_LAP:
                ++current_lap;
                break;
            }
        }
        break;

    case ACTIVITY_TREADMILL:
        heart_rate = 0;
        current_lap = 1;
        for (record = ttbin->last; record; record = record->prev)
        {
            if ((record->tag == TAG_TREADMILL) && record->treadmill.distance)
            {
                distance_factor = ttbin->total_distance / record->treadmill.distance;
                break;
            }
        }
        for (record = ttbin->first; record; record = record->next)
        {
            switch (record->tag)
            {
            case TAG_TREADMILL:
                /* this will happen if the activity is paused and then resumed */
                if (record->treadmill.timestamp == 0)
                    continue;

                strftime(timestr, sizeof(timestr), "%FT%X", localtime(&record->treadmill.timestamp));

                time = (unsigned)(record->treadmill.timestamp - ttbin->timestamp_utc);
                fprintf(file, "%u,7,%u,%.2f,,%d,,,,", time, current_lap,
                    record->treadmill.distance * distance_factor, record->treadmill.calories);
                if (heart_rate > 0)
                    fprintf(file, "%d", heart_rate);
                fprintf(file, ",%d,%s", record->treadmill.steps - steps_prev, timestr);
                if (time >= 3600)
                    fprintf(file, ",%d:%02d:%02d\r\n", time / 3600, (time % 3600) / 60, time % 60);
                else
                    fprintf(file, ",%d:%02d\r\n", time / 60, time % 60);
                steps_prev = record->treadmill.steps;
                heart_rate = 0;
                break;
            case TAG_HEART_RATE:
                heart_rate = record->heart_rate.heart_rate;
                break;
            case TAG_LAP:
                ++current_lap;
                break;
            }
        }
        break;

    case ACTIVITY_SWIMMING:
        for (record = ttbin->first; record; record = record->next)
        {
            if (record->tag != TAG_SWIM)
                continue;

            /* this will happen if the activity is paused and then resumed */
            if (record->swim.timestamp == 0)
                continue;

            strftime(timestr, sizeof(timestr), "%FT%X", localtime(&record->swim.timestamp));

            time = (unsigned)(record->swim.timestamp - ttbin->timestamp_utc);
            fprintf(file, "%u,2,%d,%.2f,,%d,,,,,%d,%s",
                time, record->swim.completed_laps + 1, record->swim.total_distance,
                record->swim.total_calories, record->swim.strokes * 60, timestr);
            if (time >= 3600)
                fprintf(file, ",%d:%02d:%02d\r\n", time / 3600, (time % 3600) / 60, time % 60);
            else
                fprintf(file, ",%d:%02d\r\n", time / 60, time % 60);
        }
        break;
    }
}

