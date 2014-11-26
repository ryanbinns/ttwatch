/*****************************************************************************\
** export_csv.c                                                              **
** CSV export code                                                           **
\*****************************************************************************/

#include "ttbin.h"

void export_csv(TTBIN_FILE *ttbin, FILE *file)
{
    uint32_t i, steps_prev = 0;
    uint32_t current_lap = 1;
    char timestr[32];
    TTBIN_RECORD *record;
    unsigned heart_rate;

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
                if ((record->gps->timestamp == 0) || ((record->gps->latitude == 0) && (record->gps->longitude == 0)))
                    continue;

                strftime(timestr, sizeof(timestr), "%FT%X", localtime(&record->gps->timestamp));

                fprintf(file, "%u,%d,%d,%.5f,%.2f,%d,%.7f,%.7f,%.2f,",
                    (unsigned)(record->gps->timestamp - ttbin->timestamp_utc), ttbin->activity, current_lap,
                    record->gps->cum_distance, record->gps->speed, record->gps->calories,
                    record->gps->latitude, record->gps->longitude, record->gps->elevation);
                if (heart_rate > 0)
                    fprintf(file, "%d", heart_rate);
                fprintf(file, ",%d,%s\r\n", record->gps->cycles, timestr);
                heart_rate = 0;
                break;
            case TAG_HEART_RATE:
                heart_rate = record->heart_rate->heart_rate;
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
        for (record = ttbin->first; record; record = record->next)
        {
            switch (record->tag)
            {
            case TAG_TREADMILL:
                /* this will happen if the activity is paused and then resumed */
                if (record->treadmill->timestamp == 0)
                    continue;

                strftime(timestr, sizeof(timestr), "%FT%X", localtime(&record->treadmill->timestamp));

                fprintf(file, "%u,7,%u,%.2f,,%d,,,,", i, current_lap, record->treadmill->distance,
                    record->treadmill->calories);
                if (heart_rate > 0)
                    fprintf(file, "%d", heart_rate);
                fprintf(file, ",%d,%s\r\n", record->treadmill->steps - steps_prev, timestr);
                steps_prev = record->treadmill->steps;
                heart_rate = 0;
                break;
            case TAG_HEART_RATE:
                heart_rate = record->heart_rate->heart_rate;
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
            if (record->swim->timestamp == 0)
                continue;

            strftime(timestr, sizeof(timestr), "%FT%X", localtime(&record->swim->timestamp));

            fprintf(file, "%u,2,%d,%.2f,,%d,,,,,%d,%s\r\n",
                i, record->swim->completed_laps + 1, record->swim->total_distance,
                record->swim->total_calories, record->swim->strokes * 60, timestr);
        }
        break;
    }
}

