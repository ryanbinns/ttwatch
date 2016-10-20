/*****************************************************************************\
** ttbin.c                                                                   **
** TTBIN parsing implementation                                              **
\*****************************************************************************/

#include "ttbin.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <curl/curl.h>

#define max(a, b)   ((a) > (b) ? (a) : (b))

/*****************************************************************************/

const OFFLINE_FORMAT OFFLINE_FORMATS[OFFLINE_FORMAT_COUNT] = {
    { OFFLINE_FORMAT_CSV, "csv", 1, 1, 1, export_csv },
    { OFFLINE_FORMAT_FIT, "fit", 1, 0, 0, 0          },
    { OFFLINE_FORMAT_GPX, "gpx", 1, 0, 0, export_gpx },
    { OFFLINE_FORMAT_KML, "kml", 1, 0, 0, export_kml },
    { OFFLINE_FORMAT_PWX, "pwx", 1, 0, 0, 0          },
    { OFFLINE_FORMAT_TCX, "tcx", 1, 1, 0, export_tcx },
};

/*****************************************************************************/

typedef struct __attribute__((packed))
{
    uint8_t tag;
    uint16_t length;
} RECORD_LENGTH;

typedef struct __attribute__((packed))
{
    uint16_t file_version;
    uint8_t  firmware_version[3];
    uint16_t product_id;
    uint32_t start_time;    /* local time */
    uint8_t  software_version[16];
    uint8_t  gps_firmware_version[80];
    uint32_t watch_time;    /* local time */
    int32_t local_time_offset;  /* seconds from UTC */
    uint8_t  _reserved;
    uint8_t  length_count;  /* number of RECORD_LENGTH objects to follow */
    RECORD_LENGTH lengths[1];
} FILE_HEADER;

typedef struct __attribute__((packed))
{
    uint8_t  activity;
    float    distance;
    uint32_t duration;      /* seconds, after adding 1 */
    uint16_t calories;
} FILE_SUMMARY_RECORD;

typedef struct __attribute__((packed))
{
    int32_t  latitude;      /* degrees * 1e7 */
    int32_t  longitude;     /* degrees * 1e7 */
    uint16_t heading;       /* degrees * 100, N = 0, E = 9000 */
    uint16_t gps_speed;     /* cm/s */
    uint32_t timestamp;     /* gps time (utc) */
    uint16_t calories;
    float    instant_speed; /* m/s */
    float    cum_distance;  /* metres */
    uint8_t  cycles;        /* running = steps/sec, cycling = crank rpm */
} FILE_GPS_RECORD;

typedef struct __attribute__((packed))
{
    uint8_t  heart_rate;    /* bpm */
    uint8_t  _reserved;
    uint32_t timestamp;     /* local time */
} FILE_HEART_RATE_RECORD;

typedef struct __attribute__((packed))
{
    uint8_t  status;        /* 0 = ready, 1 = active, 2 = paused, 3 = stopped */
    uint8_t  activity;      /* 0 = running, 1 = cycling, 2 = swimming, 7 = treadmill, 8 = freestyle */
    uint32_t timestamp;     /* local time */
} FILE_STATUS_RECORD;

typedef struct __attribute__((packed))
{
    uint32_t timestamp;     /* local time */
    float    distance;      /* metres */
    uint16_t calories;
    uint32_t steps;
    uint16_t step_length;   /* cm, not implemented yet */
} FILE_TREADMILL_RECORD;

typedef struct __attribute__((packed))
{
    uint32_t timestamp;         /* local time */
    float    total_distance;    /* metres */
    uint8_t  frequency;
    uint8_t  stroke_type;
    uint32_t strokes;           /* since the last report */
    uint32_t completed_laps;
    uint16_t total_calories;
} FILE_SWIM_RECORD;

typedef struct __attribute__((packed))
{
    uint32_t timestamp;
    uint16_t total_calories;
    uint32_t total_cycles;
} FILE_GYM_RECORD;

typedef struct __attribute__((packed))
{
    uint32_t total_time;        /* seconds since activity start */
    float    total_distance;    /* metres */
    uint16_t total_calories;
} FILE_LAP_RECORD;

typedef struct __attribute__((packed))
{
    uint8_t  race_id[16];   /* only used for a web services race, 0 otherwise */
    float    distance;  /* metres */
    uint32_t duration;  /* seconds */
    char     name[16];  /* unused characters are zero */
} FILE_RACE_SETUP_RECORD;

typedef struct __attribute__((packed))
{
    uint32_t duration;  /* seconds */
    float    distance;  /* metres */
    uint16_t calories;
} FILE_RACE_RESULT_RECORD;

typedef struct __attribute__((packed))
{
    uint8_t type;   /* 0 = goal distance, 1 = goal time, 2 = goal calories,
                       3 = zones pace, 4 = zones heart, 5 = zones cadence,
                       6 = race, 7 = laps time, 8 = laps distance, 9 = laps manual,
                       10 = stroke rate, 11 = zones speed, 12 = intervals */
    float   min;    /* metres, seconds, calories, secs/km, km/h, bpm */
    float   max;    /* secs/km, km/h, bpm (only used for zones) */
} FILE_TRAINING_SETUP_RECORD;

typedef struct __attribute__((packed))
{
    uint8_t  percent;   /* 0 - 100 */
    uint32_t value;     /* metres, seconds, calories */
} FILE_GOAL_PROGRESS_RECORD;

typedef struct __attribute__((packed))
{
    uint8_t  warm_type; /* 0 = distance, 1 = time */
    uint32_t warm;      /* metres, seconds */
    uint8_t  work_type; /* 0 = distance, 1 = time */
    uint32_t work;      /* metres, seconds */
    uint8_t  rest_type; /* 0 = distance, 1 = time */
    uint32_t rest;      /* metres, seconds */
    uint8_t  cool_type; /* 0 = distance, 1 = time */
    uint32_t cool;      /* metres, seconds */
    uint8_t  sets;
} FILE_INTERVAL_SETUP_RECORD;

typedef struct __attribute__((packed))
{
    uint8_t type;   /* 1 = warmup, 2 = work, 3 = rest, 4 = cooldown, 5 = finished */
} FILE_INTERVAL_START_RECORD;

typedef struct __attribute__((packed))
{
    uint8_t  type;              /* 1 = warmup, 2 = work, 3 = rest, 4 = cooldown */
    uint32_t total_time;        /* seconds */
    float    total_distance;    /* metres */
    uint16_t total_calories;
} FILE_INTERVAL_FINISH_RECORD;

typedef struct __attribute__((packed))
{
    uint32_t status;        /* 3 = good, 4 = excellent */
    uint32_t heart_rate;    /* bpm */
} FILE_HEART_RATE_RECOVERY_RECORD;

typedef struct __attribute__((packed))
{
    int16_t rel_altitude;   /* altitude change from workout start */
    float   total_climb;    /* metres, descents are ignored */
    uint8_t qualifier;      /* not defined yet */
} FILE_ALTITUDE_RECORD;

typedef struct __attribute__((packed))
{
    int32_t pool_size;      /* centimeters */
} FILE_POOL_SIZE_RECORD;

typedef struct __attribute__((packed))
{
    uint32_t wheel_size;    /* millimetres */
} FILE_WHEEL_SIZE_RECORD;

typedef struct __attribute__((packed))
{
    uint32_t wheel_revolutions;
    uint16_t wheel_revolutions_time;
    uint16_t crank_revolutions;
    uint16_t crank_revolutions_time;
} FILE_CYCLING_CADENCE_RECORD;

/*****************************************************************************/

TTBIN_FILE *read_ttbin_file(FILE *file)
{
    uint32_t size = 0;
    uint8_t *data = 0;
    TTBIN_FILE *ttbin;

    while (!feof(file))
    {
        data = realloc(data, size + 1024);
        size += fread(data + size, 1, 1024, file);
    }

    ttbin = parse_ttbin_data(data, size);

    free(data);
    return ttbin;
}

/*****************************************************************************/

static TTBIN_RECORD *append_record(TTBIN_FILE *ttbin, uint8_t tag, uint16_t length)
{
    size_t size = offsetof(TTBIN_RECORD, data) + length - 1;
    TTBIN_RECORD *record = (TTBIN_RECORD*)malloc(max(sizeof(TTBIN_RECORD), size));
    if (ttbin->last)
    {
        record->prev = ttbin->last;
        ttbin->last->next = record;
    }
    else
    {
        record->prev = 0;
        ttbin->first = record;
    }
    ttbin->last = record;
    record->next = 0;
    record->length = length;
    record->tag = tag;

    return record;
}

/*****************************************************************************/

static void append_array(RECORD_ARRAY* array, TTBIN_RECORD *ptr)
{
    array->records = realloc(array->records, (array->count + 1) * sizeof(TTBIN_RECORD*));
    array->records[array->count++] = ptr;
}

/*****************************************************************************/

static void remove_array(RECORD_ARRAY *array, TTBIN_RECORD *record)
{
    unsigned i;
    for (i = 0; i < array->count; ++i)
    {
        if (array->records[i] == record)
        {
            --array->count;
            if (i != array->count)
                memmove(array->records + i, array->records + i + 1, sizeof(TTBIN_RECORD*));
            break;
        }
    }
}


TTBIN_FILE *parse_ttbin_data(uint8_t *data, uint32_t size)
{
    const uint8_t *const end = data + size;
    TTBIN_FILE *file;
    unsigned length;

    FILE_HEADER               *file_header = 0;
    union
    {
        uint8_t *data;
        struct
        {
            uint8_t tag;
            union
            {
                FILE_SUMMARY_RECORD             summary;
                FILE_GPS_RECORD                 gps;
                FILE_HEART_RATE_RECORD          heart_rate;
                FILE_STATUS_RECORD              status;
                FILE_TREADMILL_RECORD           treadmill;
                FILE_SWIM_RECORD                swim;
                FILE_GYM_RECORD                 gym;
                FILE_LAP_RECORD                 lap;
                FILE_RACE_SETUP_RECORD          race_setup;
                FILE_RACE_RESULT_RECORD         race_result;
                FILE_TRAINING_SETUP_RECORD      training_setup;
                FILE_GOAL_PROGRESS_RECORD       goal_progress;
                FILE_INTERVAL_SETUP_RECORD      interval_setup;
                FILE_INTERVAL_START_RECORD      interval_start;
                FILE_INTERVAL_FINISH_RECORD     interval_finish;
                FILE_ALTITUDE_RECORD            altitude;
                FILE_POOL_SIZE_RECORD           pool_size;
                FILE_WHEEL_SIZE_RECORD          wheel_size;
                FILE_HEART_RATE_RECOVERY_RECORD heart_rate_recovery;
                FILE_CYCLING_CADENCE_RECORD     cycling_cadence;
            };
        } *record;
    } p;

    TTBIN_RECORD *record;

    /* check that the file is long enough */
    if (size < (sizeof(FILE_HEADER) - sizeof(RECORD_LENGTH)))
        return 0;

    if (*data++ != TAG_FILE_HEADER)
        return 0;

    file = malloc(sizeof(TTBIN_FILE));
    memset(file, 0, sizeof(TTBIN_FILE));

    file_header = (FILE_HEADER*)data;
    data += sizeof(FILE_HEADER) + (file_header->length_count - 1) * sizeof(RECORD_LENGTH);
    file->file_version    = file_header->file_version;
    memcpy(file->firmware_version, file_header->firmware_version, sizeof(file->firmware_version));
    file->product_id      = file_header->product_id;
    file->timestamp_local = file_header->start_time;
    file->timestamp_utc   = file_header->start_time - file_header->local_time_offset;
    file->utc_offset      = file_header->local_time_offset;

    for (p.data = data; p.data < end; p.data += length)
    {
        unsigned index = 0;

        /* find the length of this tag */
        while ((index < file_header->length_count) && (file_header->lengths[index].tag != p.record->tag))
            ++index;
        if ((index < file_header->length_count) && (file_header->lengths[index].tag == p.record->tag))
            length = file_header->lengths[index].length;
        else
        {
            free_ttbin(file);
            return 0;
        }

        switch (p.record->tag)
        {
        case TAG_SUMMARY:
            file->activity       = p.record->summary.activity;
            file->total_distance = p.record->summary.distance;
            file->duration       = p.record->summary.duration;
            file->total_calories = p.record->summary.calories;
            break;
        case TAG_STATUS:
            p.record->status.timestamp -= file->utc_offset;

            record = append_record(file, p.record->tag, length);
            record->status.status = p.record->status.status;
            record->status.activity = p.record->status.activity;
            record->status.timestamp = p.record->status.timestamp;
            append_array(&file->status_records, record);
            break;
        case TAG_GPS:
            /* if the GPS signal is lost, 0xffffffff is stored in the file */
            if (p.record->gps.timestamp == 0xffffffff)
                break;

            record = append_record(file, p.record->tag, length);
            record->gps.latitude      = p.record->gps.latitude / 1e7;
            record->gps.longitude     = p.record->gps.longitude / 1e7;
            record->gps.elevation     = NAN; /* was 0.0f */
            record->gps.heading       = p.record->gps.heading / 100.0f;
            record->gps.gps_speed     = p.record->gps.gps_speed;
            record->gps.timestamp     = p.record->gps.timestamp;
            record->gps.calories      = p.record->gps.calories;
            record->gps.instant_speed = p.record->gps.instant_speed;
            record->gps.cum_distance  = p.record->gps.cum_distance;
            record->gps.cycles        = p.record->gps.cycles;
            append_array(&file->gps_records, record);
            break;
        case TAG_HEART_RATE:
            p.record->heart_rate.timestamp -= file->utc_offset;

            record = append_record(file, p.record->tag, length);
            record->heart_rate.timestamp  = p.record->heart_rate.timestamp;
            record->heart_rate.heart_rate = p.record->heart_rate.heart_rate;
            append_array(&file->heart_rate_records, record);
            break;
        case TAG_LAP:
            record = append_record(file, p.record->tag, length);
            record->lap.total_time     = p.record->lap.total_time;
            record->lap.total_distance = p.record->lap.total_distance;
            record->lap.total_calories = p.record->lap.total_calories;
            append_array(&file->lap_records, record);
            break;
        case TAG_CYCLING_CADENCE:
            record = append_record(file, p.record->tag, length);
            record->cycling_cadence.wheel_revolutions      = p.record->cycling_cadence.wheel_revolutions;
            record->cycling_cadence.wheel_revolutions_time = p.record->cycling_cadence.wheel_revolutions_time;
            record->cycling_cadence.crank_revolutions      = p.record->cycling_cadence.crank_revolutions;
            record->cycling_cadence.crank_revolutions_time = p.record->cycling_cadence.crank_revolutions_time;
            append_array(&file->cycling_cadence_records, record);
            break;
        case TAG_TREADMILL:
            p.record->treadmill.timestamp -= file->utc_offset;

            record = append_record(file, p.record->tag, length);
            record->treadmill.timestamp = p.record->treadmill.timestamp;
            record->treadmill.distance  = p.record->treadmill.distance;
            record->treadmill.calories  = p.record->treadmill.calories;
            record->treadmill.steps     = p.record->treadmill.steps;
            append_array(&file->treadmill_records, record);
            break;
        case TAG_SWIM:
            p.record->swim.timestamp -= file->utc_offset;

            record = append_record(file, p.record->tag, length);
            record->swim.timestamp      = p.record->swim.timestamp;
            record->swim.total_distance = p.record->swim.total_distance;
            record->swim.strokes        = p.record->swim.strokes;
            record->swim.completed_laps = p.record->swim.completed_laps;
            record->swim.total_calories = p.record->swim.total_calories;
            append_array(&file->swim_records, record);
            break;
        case TAG_RACE_SETUP:
            record = append_record(file, p.record->tag, length);
            record->race_setup.distance = p.record->race_setup.distance;
            record->race_setup.duration = p.record->race_setup.duration;
            memcpy(record->race_setup.name, p.record->race_setup.name, sizeof(p.record->race_setup.name));
            file->race_setup = record;
            break;
        case TAG_RACE_RESULT:
            if (!file->race_setup)
            {
                free_ttbin(file);
                return 0;
            }
            record = append_record(file, p.record->tag, length);
            record->race_result.distance = p.record->race_result.distance;
            record->race_result.duration = p.record->race_result.duration;
            record->race_result.calories = p.record->race_result.calories;
            file->race_result = record;
            break;
        case TAG_TRAINING_SETUP:
            record = append_record(file, p.record->tag, length);
            record->training_setup.type      = p.record->training_setup.type;
            record->training_setup.value_min = p.record->training_setup.min;
            record->training_setup.max       = p.record->training_setup.max;
            file->training_setup = record;
            break;
        case TAG_GOAL_PROGRESS:
            record = append_record(file, p.record->tag, length);
            record->goal_progress.percent = p.record->goal_progress.percent;
            record->goal_progress.value   = p.record->goal_progress.value;
            append_array(&file->goal_progress_records, record);
            break;
        case TAG_INTERVAL_SETUP:
            record = append_record(file, p.record->tag, length);
            record->interval_setup.warm_type = p.record->interval_setup.warm_type;
            record->interval_setup.warm      = p.record->interval_setup.warm;
            record->interval_setup.work_type = p.record->interval_setup.work_type;
            record->interval_setup.work      = p.record->interval_setup.work;
            record->interval_setup.rest_type = p.record->interval_setup.rest_type;
            record->interval_setup.rest      = p.record->interval_setup.rest;
            record->interval_setup.cool_type = p.record->interval_setup.cool_type;
            record->interval_setup.cool      = p.record->interval_setup.cool;
            record->interval_setup.sets      = p.record->interval_setup.sets;
            file->interval_setup = record;
            break;
        case TAG_INTERVAL_START:
            record = append_record(file, p.record->tag, length);
            record->interval_start.type = p.record->interval_start.type;
            append_array(&file->interval_start_records, record);
            break;
        case TAG_INTERVAL_FINISH:
            record = append_record(file, p.record->tag, length);
            record->interval_finish.type           = p.record->interval_finish.type;
            record->interval_finish.total_time     = p.record->interval_finish.total_time;
            record->interval_finish.total_distance = p.record->interval_finish.total_distance;
            record->interval_finish.total_calories = p.record->interval_finish.total_calories;
            append_array(&file->interval_finish_records, record);
            break;
        case TAG_ALTITUDE_UPDATE:
            record = append_record(file, p.record->tag, length);
            record->altitude.rel_altitude = p.record->altitude.rel_altitude;
            record->altitude.total_climb  = p.record->altitude.total_climb;
            record->altitude.qualifier    = p.record->altitude.qualifier;
            append_array(&file->altitude_records, record);
            break;
        case TAG_POOL_SIZE:
            record = append_record(file, p.record->tag, length);
            record->pool_size.pool_size = p.record->pool_size.pool_size;
            file->pool_size = record;
            break;
        case TAG_WHEEL_SIZE:
            record = append_record(file, p.record->tag, length);
            record->wheel_size.wheel_size = p.record->wheel_size.wheel_size;
            file->wheel_size = record;
            break;
        case TAG_HEART_RATE_RECOVERY:
            record = append_record(file, p.record->tag, length);
            record->heart_rate_recovery.status     = p.record->heart_rate_recovery.status;
            record->heart_rate_recovery.heart_rate = p.record->heart_rate_recovery.heart_rate;
            file->heart_rate_recovery = record;
            break;
        case TAG_GYM:
            record = append_record(file, p.record->tag, length);
            record->gym.timestamp = p.record->gym.timestamp;
            record->gym.total_calories  = p.record->gym.total_calories;
            record->gym.total_cycles    = p.record->gym.total_cycles;
            append_array(&file->gym_records, record);
            break;
        default:
            record = append_record(file, p.record->tag, length);
            memcpy(record->data, p.data + 1, length - 1);
            break;
        }
    }

    return file;
}

/*****************************************************************************/

void insert_length_record(FILE_HEADER *header, uint8_t tag, uint16_t length)
{
    unsigned i = 0;
    /* look for the position to put the new tag (numerical order) */
    while ((tag > header->lengths[i].tag) && (header->lengths[i].tag != 0))
        ++i;

    /* make sure we don't insert duplicates */
    if (header->lengths[i].tag != tag)
    {
        memmove(header->lengths + i + 1, header->lengths + i, (29 - i) * sizeof(RECORD_LENGTH));
        header->lengths[i].tag = tag;
        header->lengths[i].length = length;
        ++header->length_count;
    }
}

int write_ttbin_file(const TTBIN_FILE *ttbin, FILE *file)
{
    TTBIN_RECORD *record;
    uint8_t tag = TAG_FILE_HEADER;
    unsigned size;
    FILE_HEADER *header;
    FILE_SUMMARY_RECORD summary;

    /* create and write the file header */
    size = sizeof(FILE_HEADER) + 29 * sizeof(RECORD_LENGTH);
    header = (FILE_HEADER*)malloc(size);
    memset(header, 0, size);
    header->file_version = ttbin->file_version;
    memcpy(header->firmware_version, ttbin->firmware_version, sizeof(header->firmware_version));
    header->product_id = ttbin->product_id;
    header->start_time = ttbin->timestamp_local;
    header->watch_time = ttbin->timestamp_local;
    header->local_time_offset = ttbin->utc_offset;
    insert_length_record(header, TAG_FILE_HEADER, sizeof(FILE_HEADER) - sizeof(RECORD_LENGTH));
    insert_length_record(header, TAG_SUMMARY, sizeof(FILE_SUMMARY_RECORD) + 1);
    for (record = ttbin->first; record; record = record->next)
        insert_length_record(header, record->tag, record->length);
    fwrite(&tag, 1, 1, file);
    fwrite(header, 1, sizeof(FILE_HEADER) + (header->length_count - 1) * sizeof(RECORD_LENGTH), file);

    for (record = ttbin->first; record; record = record->next)
    {
        fwrite(&record->tag, 1, 1, file);
        switch (record->tag)
        {
        case TAG_STATUS: {
            FILE_STATUS_RECORD r = {
                record->status.status,
                record->status.activity,
                record->status.timestamp + ttbin->utc_offset
            };
            fwrite(&r, 1, sizeof(FILE_STATUS_RECORD), file);
            break;
        }
        case TAG_GPS: {
            FILE_GPS_RECORD r = {
                (int32_t)(record->gps.latitude * 1e7),
                (int32_t)(record->gps.longitude * 1e7),
                (uint16_t)(record->gps.heading * 100.0f + 0.5f),
                record->gps.gps_speed,
                record->gps.timestamp,
                record->gps.calories,
                record->gps.instant_speed,
                record->gps.cum_distance,
                record->gps.cycles
            };
            fwrite(&r, 1, sizeof(FILE_GPS_RECORD), file);
            break;
        }
        case TAG_HEART_RATE: {
            FILE_HEART_RATE_RECORD r = {
                record->heart_rate.heart_rate,
                0,  /* reserved */
                record->heart_rate.timestamp + ttbin->utc_offset
            };
            fwrite(&r, 1, sizeof(FILE_HEART_RATE_RECORD), file);
            break;
        }
        case TAG_LAP: {
            FILE_LAP_RECORD r = {
                record->lap.total_time,
                record->lap.total_distance,
                record->lap.total_calories
            };
            fwrite(&r, 1, sizeof(FILE_LAP_RECORD), file);
            break;
        }
        case TAG_CYCLING_CADENCE: {
            FILE_CYCLING_CADENCE_RECORD r = {
                record->cycling_cadence.wheel_revolutions,
                record->cycling_cadence.wheel_revolutions_time,
                record->cycling_cadence.crank_revolutions,
                record->cycling_cadence.crank_revolutions_time
            };
            fwrite(&r, 1, sizeof(FILE_CYCLING_CADENCE_RECORD), file);
            break;
        }
        case TAG_TREADMILL: {
            FILE_TREADMILL_RECORD r = {
                record->treadmill.timestamp + ttbin->utc_offset,
                record->treadmill.distance,
                record->treadmill.calories,
                record->treadmill.steps,
                record->treadmill.step_length
            };
            fwrite(&r, 1, sizeof(FILE_TREADMILL_RECORD), file);
            break;
        }
        case TAG_SWIM: {
            FILE_SWIM_RECORD r = {
                record->swim.timestamp + ttbin->utc_offset,
                record->swim.total_distance,
                record->swim.frequency,
                record->swim.stroke_type,
                record->swim.strokes,
                record->swim.completed_laps,
                record->swim.total_calories
            };
            fwrite(&r, 1, sizeof(FILE_SWIM_RECORD), file);
            break;
        }
        case TAG_RACE_SETUP: {
            FILE_RACE_SETUP_RECORD r = {
                {0},
                record->race_setup.distance,
                record->race_setup.duration,
                {0},
            };
            memcpy(r.race_id, record->race_setup.race_id, sizeof(r.race_id));
            memcpy(r.name, record->race_setup.name, sizeof(r.name));
            fwrite(&r, 1, sizeof(FILE_RACE_SETUP_RECORD), file);
            break;
        }
        case TAG_RACE_RESULT: {
            FILE_RACE_RESULT_RECORD r = {
                record->race_result.duration,
                record->race_result.distance,
                record->race_result.calories
            };
            fwrite(&r, 1, sizeof(FILE_RACE_RESULT_RECORD), file);
            break;
        }
        case TAG_TRAINING_SETUP: {
            FILE_TRAINING_SETUP_RECORD r = {
                record->training_setup.type,
                record->training_setup.value_min,
                record->training_setup.max
            };
            fwrite(&r, 1, sizeof(FILE_TRAINING_SETUP_RECORD), file);
            break;
        }
        case TAG_GOAL_PROGRESS: {
            FILE_GOAL_PROGRESS_RECORD r = {
                record->goal_progress.percent,
                record->goal_progress.value
            };
            fwrite(&r, 1, sizeof(FILE_GOAL_PROGRESS_RECORD), file);
            break;
        }
        case TAG_INTERVAL_SETUP: {
            FILE_INTERVAL_SETUP_RECORD r = {
                record->interval_setup.warm_type,
                record->interval_setup.warm,
                record->interval_setup.work_type,
                record->interval_setup.work,
                record->interval_setup.rest_type,
                record->interval_setup.rest,
                record->interval_setup.cool_type,
                record->interval_setup.cool,
                record->interval_setup.sets
            };
            fwrite(&r, 1, sizeof(FILE_INTERVAL_SETUP_RECORD), file);
            break;
        }
        case TAG_INTERVAL_START: {
            FILE_INTERVAL_START_RECORD r = {
                record->interval_start.type
            };
            fwrite(&r, 1, sizeof(FILE_INTERVAL_START_RECORD), file);
            break;
        }
        case TAG_INTERVAL_FINISH: {
            FILE_INTERVAL_FINISH_RECORD r = {
                record->interval_finish.type,
                record->interval_finish.total_time,
                record->interval_finish.total_distance,
                record->interval_finish.total_calories
            };
            fwrite(&r, 1, sizeof(FILE_INTERVAL_FINISH_RECORD), file);
            break;
        }
        case TAG_ALTITUDE_UPDATE: {
            FILE_ALTITUDE_RECORD r = {
                record->altitude.rel_altitude,
                record->altitude.total_climb,
                record->altitude.qualifier
            };
            fwrite(&r, 1, sizeof(FILE_ALTITUDE_RECORD), file);
            break;
        }
        case TAG_POOL_SIZE: {
            FILE_POOL_SIZE_RECORD r = {
                record->pool_size.pool_size
            };
            fwrite(&r, 1, sizeof(FILE_POOL_SIZE_RECORD), file);
            break;
        }
        case TAG_WHEEL_SIZE: {
            FILE_WHEEL_SIZE_RECORD r = {
                record->wheel_size.wheel_size
            };
            fwrite(&r, 1, sizeof(FILE_WHEEL_SIZE_RECORD), file);
            break;
        }
        case TAG_HEART_RATE_RECOVERY: {
            FILE_HEART_RATE_RECOVERY_RECORD r = {
                record->heart_rate_recovery.status,
                record->heart_rate_recovery.heart_rate
            };
            fwrite(&r, 1, sizeof(FILE_HEART_RATE_RECOVERY_RECORD), file);
            break;
        }
        case TAG_GYM: {
            FILE_GYM_RECORD r = {
                record->gym.timestamp,
                record->gym.total_calories,
                record->gym.total_cycles
            };
            fwrite(&r, 1, sizeof(FILE_GYM_RECORD), file);
        }
        default: {
            fwrite(record->data, 1, record->length - 1, file);
            break;
        }
        }
    }

    /* write the summary record */
    summary.activity = ttbin->activity;
    summary.distance = ttbin->total_distance;
    summary.duration = ttbin->duration;
    summary.calories = ttbin->total_calories;
    tag = TAG_SUMMARY;
    fwrite(&tag, 1, 1, file);
    fwrite(&summary, 1, sizeof(FILE_SUMMARY_RECORD), file);
    return 0;
}

/*****************************************************************************/

TTBIN_RECORD *insert_before(TTBIN_FILE *ttbin, TTBIN_RECORD *record)
{
    TTBIN_RECORD *r = (TTBIN_RECORD*)malloc(sizeof(TTBIN_RECORD));
    if (record == ttbin->first)
    {
        r->next = ttbin->first;
        r->prev = 0;
        r->next->prev = r;
        ttbin->first = r;
    }
    else
    {
        r->next = record;
        r->prev = record->prev;
        r->next->prev = r;
        r->prev->next = r;
    }
    return r;
}

/*****************************************************************************/

TTBIN_RECORD *insert_after(TTBIN_FILE *ttbin, TTBIN_RECORD *record)
{
   TTBIN_RECORD *r = (TTBIN_RECORD*)malloc(sizeof(TTBIN_RECORD));
   if (record == ttbin->last)
   {
       r->next = 0;
       r->prev = ttbin->last;
       r->prev->next = r;
       ttbin->last = r;
   }
   else
   {
       r->next = record->next;
       r->prev = record;
       r->next->prev = r;
       r->prev->next = r;
   }
   return r;
}

/*****************************************************************************/

void delete_record(TTBIN_FILE *ttbin, TTBIN_RECORD *record)
{
    switch (record->tag)
    {
    case TAG_GPS: remove_array(&ttbin->gps_records, record); break;
    case TAG_LAP: remove_array(&ttbin->lap_records, record); break;
    case TAG_HEART_RATE: remove_array(&ttbin->heart_rate_records, record); break;
    case TAG_TREADMILL: remove_array(&ttbin->treadmill_records, record); break;
    case TAG_SWIM: remove_array(&ttbin->swim_records, record); break;
    case TAG_STATUS: remove_array(&ttbin->status_records, record); break;
    case TAG_GOAL_PROGRESS: remove_array(&ttbin->goal_progress_records, record); break;
    case TAG_INTERVAL_START: remove_array(&ttbin->interval_start_records, record); break;
    case TAG_INTERVAL_FINISH: remove_array(&ttbin->interval_finish_records, record); break;
    case TAG_ALTITUDE_UPDATE: remove_array(&ttbin->altitude_records, record); break;
    case TAG_GYM: remove_array(&ttbin->gym_records, record); break;
    case TAG_CYCLING_CADENCE: remove_array(&ttbin->cycling_cadence_records, record); break;
    }

    if (record != ttbin->first)
        record->prev->next = record->next;
    else
        ttbin->first = record->next;
    if (record != ttbin->last)
        record->next->prev = record->prev;
    else
        ttbin->last = record->prev;
    free(record);
}

/*****************************************************************************/

const char *create_filename(TTBIN_FILE *ttbin, const char *ext)
{
    static char filename[32];
    struct tm *time = gmtime(&ttbin->timestamp_local);
    const char *type = "Unknown";

    switch (ttbin->activity)
    {
    case ACTIVITY_RUNNING:   type = "Running"; break;
    case ACTIVITY_CYCLING:   type = "Cycling"; break;
    case ACTIVITY_SWIMMING:  type = "Pool_swim"; break;
    case ACTIVITY_TREADMILL: type = "Treadmill"; break;
    case ACTIVITY_FREESTYLE: type = "Freestyle"; break;
    case ACTIVITY_GYM:       type = "Gym"; break;
    }
    sprintf(filename, "%s_%02d-%02d-%02d.%s", type, time->tm_hour, time->tm_min, time->tm_sec, ext);

    return filename;
}

/*****************************************************************************/

typedef struct
{
    TTBIN_RECORD **data;
    uint32_t max_count;
    uint32_t current_count;

    float elev;
    float mult;
} ELEV_DATA_INFO;

static size_t curl_write_data(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    ELEV_DATA_INFO *info = (ELEV_DATA_INFO*)userdata;
    char *s1;

    size_t length = size * nmemb;

    /* this is a simple float-parser that maintains state between
       invocations incase we get a single number split between
       multiple buffers */
    for (s1 = ptr; s1 < (ptr + length); ++s1)
    {
        if (isdigit(*s1))
        {
            if (info->mult > 0.5f)
                info->elev = (info->elev * 10.0f) + (*s1 - '0');
            else
            {
                info->elev += info->mult * (*s1 - '0');
                info->mult /= 10.0f;
            }
        }
        else if (*s1 == '.')
            info->mult = 0.1f;
        else if ((*s1 == ',') || (*s1 == ']'))
        {
            if (info->current_count < info->max_count)
            {
                (*info->data)->gps.elevation = info->elev;
                ++info->current_count;
                ++info->data;
            }
            info->elev = 0.0f;
            info->mult = 1.0f;
        }
    }

    return length;
}

void download_elevation_data(TTBIN_FILE *ttbin)
{
    CURL *curl;
    struct curl_slist *headers;
    char *post_data;
    char *str;
    uint32_t i;
    ELEV_DATA_INFO info = {0};
    int result;

    /* only download elevation data if we have GPS records */
    if (!ttbin || !ttbin->gps_records.count || !ttbin->gps_records.records)
        return;

    curl = curl_easy_init();
    if (!curl)
    {
        fprintf(stderr, "Unable to initialise libcurl\n");
        return;
    }

    /* create the post string to send to the server */
    post_data = malloc(ttbin->gps_records.count * 52 + 10);
    str = post_data;
    str += sprintf(str, "[\n");
    for (i = 0; i < ttbin->gps_records.count; ++i)
    {
        if (i != (ttbin->gps_records.count - 1))
        {
            str += sprintf(str, "   [ %f, %f ],\n",
                ttbin->gps_records.records[i]->gps.latitude,
                ttbin->gps_records.records[i]->gps.longitude);
        }
        else
        {
            str += sprintf(str, "   [ %f, %f ]\n",
                ttbin->gps_records.records[i]->gps.latitude,
                ttbin->gps_records.records[i]->gps.longitude);
        }
    }
    str += sprintf(str, "]\n");

    headers = curl_slist_append(NULL, "Content-Type:text/plain");

    /* setup the callback function data structure */
    info.mult = 1.0;
    info.elev = 0.0;
    info.data = ttbin->gps_records.records;
    info.max_count = ttbin->gps_records.count;
    info.current_count = 0;

    /* setup the transaction */
    curl_easy_setopt(curl, CURLOPT_URL, "https://mysports.tomtom.com/tyne/dem/fixmodel");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, str - post_data);
    curl_easy_setopt(curl, CURLOPT_POST, 1);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "TomTom");
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 50);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &info);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_data);

    /* perform the transaction */
    result = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (result != CURLE_OK)
        fprintf(stderr, "Unable to download elevation data: %d\n", result);
}

/*****************************************************************************/

uint32_t export_formats(TTBIN_FILE *ttbin, uint32_t formats)
{
    unsigned i;
    FILE *f;

    for (i = 0; i < OFFLINE_FORMAT_COUNT; ++i)
    {
        if ((formats & OFFLINE_FORMATS[i].mask) && OFFLINE_FORMATS[i].producer)
        {
            if ((OFFLINE_FORMATS[i].gps_ok && ttbin->gps_records.count)
                || (OFFLINE_FORMATS[i].treadmill_ok && (ttbin->activity == ACTIVITY_TREADMILL))
                || (OFFLINE_FORMATS[i].pool_swim_ok && (ttbin->activity == ACTIVITY_SWIMMING)))
            {
                f = fopen(create_filename(ttbin, OFFLINE_FORMATS[i].name), "w");
                if (f)
                {
                    (*OFFLINE_FORMATS[i].producer)(ttbin, f);
                    fclose(f);
                }
                else
                    formats &= ~OFFLINE_FORMATS[i].mask;
            }
            else
                formats &= ~OFFLINE_FORMATS[i].mask;
        }
        else
            formats &= ~OFFLINE_FORMATS[i].mask;
    }

    return formats;
}

/*****************************************************************************/

void free_ttbin(TTBIN_FILE *ttbin)
{
    TTBIN_RECORD *record;

    if (!ttbin)
        return;
        
    for (record = ttbin->first; record; record = record->next)
    {
        if (record->prev)
            free(record->prev);
    }
    if (ttbin->last)                            free(ttbin->last);
    if (ttbin->gps_records.records)             free(ttbin->gps_records.records);
    if (ttbin->status_records.records)          free(ttbin->status_records.records);
    if (ttbin->treadmill_records.records)       free(ttbin->treadmill_records.records);
    if (ttbin->swim_records.records)            free(ttbin->swim_records.records);
    if (ttbin->lap_records.records)             free(ttbin->lap_records.records);
    if (ttbin->heart_rate_records.records)      free(ttbin->heart_rate_records.records);
    if (ttbin->goal_progress_records.records)   free(ttbin->goal_progress_records.records);
    if (ttbin->interval_start_records.records)  free(ttbin->interval_start_records.records);
    if (ttbin->interval_finish_records.records) free(ttbin->interval_finish_records.records);
    if (ttbin->altitude_records.records)        free(ttbin->altitude_records.records);
    if (ttbin->gym_records.records)             free(ttbin->gym_records.records);
    if (ttbin->cycling_cadence_records.records) free(ttbin->cycling_cadence_records.records);
    free(ttbin);
}

/*****************************************************************************/

void replace_lap_list(TTBIN_FILE *ttbin, float *distances, unsigned count)
{
    float end_of_lap = 0;
    float last_distance = 0;
    uint32_t i;
    unsigned d = 0;

    /* remove the current lap records */
    if (ttbin->lap_records.count)
    {
        for (i = 0; i < ttbin->lap_records.count; ++i)
            delete_record(ttbin, ttbin->lap_records.records[i]);
        free(ttbin->lap_records.records);
        ttbin->lap_records.records = 0;
        ttbin->lap_records.count   = 0;
    }

    /* do the check here, so that we can just remove all the laps if we want to */
    if (!distances || (count == 0))
        return;

    end_of_lap = distances[d];
    for (i = 0; i < ttbin->gps_records.count; ++i)
    {
        TTBIN_RECORD *lap_record;
        /* skip records until we reach the desired lap distance */
        if (ttbin->gps_records.records[i]->gps.cum_distance < end_of_lap)
            continue;

        /* right, so we need to add a lap marker here */
        lap_record = insert_before(ttbin, ttbin->gps_records.records[i]);
        lap_record->tag = TAG_LAP;
        lap_record->length = 10;
        lap_record->lap.total_time = i;
        lap_record->lap.total_distance = ttbin->gps_records.records[i]->gps.cum_distance;
        lap_record->lap.total_calories = ttbin->gps_records.records[i]->gps.calories;
        append_array(&ttbin->lap_records, lap_record);

        /* get the next lap distance */
        if (++d >= count)
        {
            d = 0;
            last_distance = end_of_lap;
        }

        end_of_lap = last_distance + distances[d];
    }
}

/*****************************************************************************/

static void update_summary_information(TTBIN_FILE *ttbin)
{
    unsigned i;
    TTBIN_RECORD *record;
    /* update the summary information from the last GPS record */
    switch (ttbin->activity)
    {
    case ACTIVITY_RUNNING:
    case ACTIVITY_CYCLING:
    case ACTIVITY_FREESTYLE:
        i = ttbin->gps_records.count;
        while (i > 0)
        {
            record = ttbin->gps_records.records[--i];
            if ((record->gps.timestamp == 0) || ((record->gps.latitude == 0) && (record->gps.longitude == 0)))
                continue;

            ttbin->total_distance = record->gps.cum_distance;
            ttbin->total_calories = record->gps.calories;
            ttbin->duration       = record->gps.timestamp - ttbin->timestamp_utc;
            break;
        }
        break;
    case ACTIVITY_SWIMMING:
        record = ttbin->swim_records.records[ttbin->swim_records.count - 1];
        ttbin->total_distance = record->swim.total_distance;
        ttbin->total_calories = record->swim.total_calories;
        ttbin->duration       = record->swim.timestamp - ttbin->timestamp_utc;
        break;
    case ACTIVITY_TREADMILL:
        record = ttbin->treadmill_records.records[ttbin->treadmill_records.count - 1];
        ttbin->total_distance = record->treadmill.distance;
        ttbin->total_calories = record->treadmill.calories;
        ttbin->duration       = record->treadmill.timestamp - ttbin->timestamp_utc;
        break;
    case ACTIVITY_GYM:
        record = ttbin->gym_records.records[ttbin->gym_records.count - 1];
        ttbin->total_distance = 0.0f;
        ttbin->total_calories = record->gym.total_calories;
        ttbin->duration       = record->gym.timestamp - ttbin->timestamp_utc;
        break;
    }
}

/*****************************************************************************/

int truncate_laps(TTBIN_FILE *ttbin)
{
    TTBIN_RECORD *record, *end;
    /* if we have no laps, we can't truncate the file */
    if (!ttbin->lap_records.count)
        return 0;

    /* find the position record AFTER the final lap record */
    end = ttbin->lap_records.records[ttbin->lap_records.count - 1];
    while (end->next)
    {
        end = end->next;
        if ((end->tag == TAG_GPS) || (end->tag == TAG_SWIM) || (end->tag == TAG_TREADMILL) || (end->tag == TAG_GYM))
            break;
    }

    /* delete everything after this point */
    record = ttbin->last;
    while (record != end)
    {
        TTBIN_RECORD *r = record->prev;
        delete_record(ttbin, record);
        record = r;
    }

    update_summary_information(ttbin);

    return 1;
}

/*****************************************************************************/

int truncate_race(TTBIN_FILE *ttbin)
{
    TTBIN_RECORD *record, *end;
    /* if we have no race, we can't truncate the file */
    if (!ttbin->race_result)
        return 0;

    /* find the position record AFTER the race result record */
    end = ttbin->race_result;
    while (end->next)
    {
        end = end->next;
        if ((end->tag == TAG_GPS) || (end->tag == TAG_SWIM) || (end->tag == TAG_TREADMILL) || (end->tag == TAG_GYM))
            break;
    }

    /* delete everything after this point */
    record = ttbin->last;
    while (record != end)
    {
        TTBIN_RECORD *r = record->prev;
        delete_record(ttbin, record);
        record = r;
    }

    update_summary_information(ttbin);

    return 1;
}

/*****************************************************************************/

int truncate_goal(TTBIN_FILE *ttbin)
{
    TTBIN_RECORD *record, *end;
    int i;
    /* if we have no goal, we can't truncate the file */
    if (!ttbin->goal_progress_records.count)
        return 0;

    /* don't truncate anything if we didn't reach the goal */
    i = ttbin->goal_progress_records.count - 1;
    end = ttbin->goal_progress_records.records[i];
    if (end->goal_progress.percent < 100)
        return 0;

    /* find the 100% record (there may be others after it) */
    while (ttbin->goal_progress_records.records[i]->goal_progress.percent > 100)
        i--;
    end = ttbin->goal_progress_records.records[i];

    /* find the position record AFTER the 100% goal record */
    while (end->next)
    {
        end = end->next;
        if ((end->tag == TAG_GPS) || (end->tag == TAG_SWIM) || (end->tag == TAG_TREADMILL) || (end->tag == TAG_GYM))
            break;
    }

    /* delete everything after this point */
    record = ttbin->last;
    while (record != end)
    {
        TTBIN_RECORD *r = record->prev;
        delete_record(ttbin, record);
        record = r;
    }

    update_summary_information(ttbin);

    return 1;
}

/*****************************************************************************/

int truncate_intervals(TTBIN_FILE *ttbin)
{
    TTBIN_RECORD *record, *end;
    /* if we have no intervals, we can't truncate the file */
    if (!ttbin->interval_finish_records.count)
        return 0;

    /* find the position record AFTER the final interval finish record */
    end = ttbin->interval_finish_records.records[ttbin->interval_finish_records.count - 1];
    while (end->next)
    {
        end = end->next;
        if ((end->tag == TAG_GPS) || (end->tag == TAG_SWIM) || (end->tag = TAG_TREADMILL) || (end->tag == TAG_GYM))
            break;
    }

    /* delete everything after this point */
    record = ttbin->last;
    while (record != end)
    {
        TTBIN_RECORD *r = record->prev;
        delete_record(ttbin, record);
        record = r;
    }

    update_summary_information(ttbin);

    return 1;
}

/*****************************************************************************/
uint32_t parse_format_list(const char *formats)
{
    uint32_t fmts = 0;
    int i;
    char *str, *ptr;

    str = strdup(formats);
    ptr = strtok(str, " ,");
    while (ptr)
    {
        for (i = 0; i < OFFLINE_FORMAT_COUNT; ++i)
        {
            if (!strcasecmp(ptr, OFFLINE_FORMATS[i].name))
            {
                fmts |= OFFLINE_FORMATS[i].mask;
                break;
            }
        }
        /* ignore any unknown formats... */
        ptr = strtok(NULL, " ,");
    }
    free(str);
    return fmts;
}

