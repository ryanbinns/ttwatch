/*****************************************************************************\
** export_csv.c                                                              **
** CSV export code                                                           **
\*****************************************************************************/

#include "ttbin.h"

void export_csv(TTBIN_FILE *ttbin, FILE *file)
{
    uint32_t i;
    time_t initial_time = 0;
    uint32_t current_lap = 1;

    fputs("time,activityType,lapNumber,distance,speed,calories,lat,long,elevation,heartRate,cycles\r\n", file);

    if (ttbin->activity != ACTIVITY_SWIMMING)
    switch (ttbin->activity)
    {
    case ACTIVITY_RUNNING:
    case ACTIVITY_CYCLING:
    case ACTIVITY_FREESTYLE:
        for (i = 0; i < ttbin->gps_record_count; ++i)
        {
            GPS_RECORD *record = &ttbin->gps_records[i];
            if (i == 0)
                initial_time = record->timestamp;

            /* this will happen if the activity is paused and then resumed, or if the GPS signal is lost  */
            if ((record->timestamp == 0) || ((record->latitude == 0) && (record->longitude == 0)))
                continue;

            if (current_lap <= ttbin->lap_record_count)
            {
                if (record->cum_distance >= ttbin->lap_records[current_lap - 1].total_distance)
                    ++current_lap;
            }

            fprintf(file, "%ld,%d,%d,%.2f,%.2f,%d,%.6f,%.6f,%.2f,",
                record->timestamp - initial_time, ttbin->activity, current_lap,
                record->cum_distance, record->speed, record->calories, record->latitude,
                record->longitude, record->elevation);
            if (ttbin->gps_records[i].heart_rate > 0)
                fprintf(file, "%d", record->heart_rate);
            fprintf(file, ",%d\r\n", record->cycles);
        }
        break;

    case ACTIVITY_TREADMILL:
        for (i = 0; i < ttbin->treadmill_record_count; ++i)
        {
            TREADMILL_RECORD *record = &ttbin->treadmill_records[i];
            if (i == 0)
                initial_time = record->timestamp;

            /* this will happen if the activity is paused and then resumed */
            if (record->timestamp == 0)
                continue;

            fprintf(file, "%ld,7,1,%.2f,,%d,,,,",
                record->timestamp - initial_time, record->distance, record->calories);
            if (ttbin->treadmill_records[i].heart_rate > 0)
                fprintf(file, "%d", record->heart_rate);
            fprintf(file, ",%d\r\n", record->steps);
        }
        break;

    case ACTIVITY_SWIMMING:
        for (i = 0; i < ttbin->swim_record_count; ++i)
        {
            SWIM_RECORD *record = &ttbin->swim_records[i];
            if (i == 0)
                initial_time = record->timestamp;

            /* this will happen if the activity is paused and then resumed */
            if (record->timestamp == 0)
                continue;

            fprintf(file, "%ld,2,%d,%.2f,,%d,,,,,%d\r\n",
                record->timestamp - initial_time,
                record->completed_laps + 1,
                record->total_distance,
                record->total_calories,
                record->strokes * 60);
        }
        break;
    }
}

