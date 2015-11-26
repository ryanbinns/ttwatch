/*****************************************************************************\
** ttbin.h                                                                   **
** TTBIN parsing implementation                                              **
\*****************************************************************************/

#ifndef __TTBIN_H__
#define __TTBIN_H__

#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define TAG_FILE_HEADER         (0x20)
#define TAG_STATUS              (0x21)
#define TAG_GPS                 (0x22)
#define TAG_HEART_RATE          (0x25)
#define TAG_SUMMARY             (0x27)
#define TAG_POOL_SIZE           (0x2a)
#define TAG_WHEEL_SIZE          (0x2b)
#define TAG_TRAINING_SETUP      (0x2d)
#define TAG_LAP                 (0x2f)
#define TAG_CYCLING_CADENCE     (0x31)
#define TAG_TREADMILL           (0x32)
#define TAG_SWIM                (0x34)
#define TAG_GOAL_PROGRESS       (0x35)
#define TAG_INTERVAL_SETUP      (0x39)
#define TAG_INTERVAL_START      (0x3a)
#define TAG_INTERVAL_FINISH     (0x3b)
#define TAG_RACE_SETUP          (0x3c)
#define TAG_RACE_RESULT         (0x3d)
#define TAG_ALTITUDE_UPDATE     (0x3e)
#define TAG_HEART_RATE_RECOVERY (0x3f)
#define TAG_GYM                 (0x41)

#define ACTIVITY_RUNNING    (0)
#define ACTIVITY_CYCLING    (1)
#define ACTIVITY_SWIMMING   (2)
#define ACTIVITY_STOPWATCH  (6) /* doesn't actually log any data */
#define ACTIVITY_TREADMILL  (7)
#define ACTIVITY_FREESTYLE  (8)
#define ACTIVITY_GYM        (9)

typedef struct
{
    double   latitude;      /* degrees */
    double   longitude;     /* degrees */
    float    elevation;     /* metres, initialised to 0 */
    float    heading;       /* degrees, N = 0, E = 90.00 */
    uint16_t gps_speed;         /* m/s */
    time_t   timestamp;     /* gps time (utc) */
    uint16_t calories;
    float    instant_speed; /* m/s */
    float    cum_distance;  /* metres */
    uint8_t  cycles;        /* steps/strokes/cycles etc. */
} GPS_RECORD;

typedef struct
{
    uint8_t  status;        /* 0 = ready, 1 = active, 2 = paused, 3 = stopped */
    uint8_t  activity;      /* 0 = running, 1 = cycling, 2 = swimming, 7 = treadmill, 8 = freestyle */
    time_t   timestamp;     /* utc time */
} STATUS_RECORD;

#define TTBIN_STATUS_READY      (0)
#define TTBIN_STATUS_ACTIVE     (1)
#define TTBIN_STATUS_PAUSED     (2)
#define TTBIN_STATUS_STOPPED    (3)

typedef struct
{
    time_t   timestamp;     /* utc time */
    float    distance;      /* metres */
    uint16_t calories;
    uint32_t steps;
    uint16_t step_length;   /* centimetres */
} TREADMILL_RECORD;

typedef struct
{
    time_t   timestamp;         /* utc time */
    float    total_distance;    /* metres */
    uint8_t  frequency;
    uint8_t  stroke_type;
    uint32_t strokes;           /* since the last report */
    uint32_t completed_laps;
    uint16_t total_calories;
} SWIM_RECORD;

typedef struct
{
    time_t   timestamp;
    uint16_t total_calories;
    uint32_t total_cycles;
} GYM_RECORD;

typedef struct
{
    uint32_t total_time;        /* seconds since activity start */
    float    total_distance;    /* metres */
    uint16_t total_calories;
} LAP_RECORD;

typedef struct
{
    time_t  timestamp;          /* utc time */
    uint8_t heart_rate;         /* bpm */
} HEART_RATE_RECORD;

typedef struct
{
    uint8_t  race_id[16];   /* only used for a web services race, 0 otherwise */
    float    distance;  /* metres */
    uint32_t duration;  /* seconds */
    char     name[16];  /* always null-terminated */
} RACE_SETUP_RECORD;

typedef struct
{
    float    distance;  /* metres */
    uint32_t duration;  /* seconds */
    uint16_t calories;
} RACE_RESULT_RECORD;

typedef struct
{
    uint8_t type;   /* 0 = goal distance, 1 = goal time, 2 = goal calories,
                       3 = zones pace, 4 = zones heart, 5 = zones cadence,
                       6 = race, 7 = laps time, 8 = laps distance, 9 = laps manual,
                       10 = stroke rate, 11 = zones speed, 12 = intervals */
    float   value_min;  /* metres, seconds, calories, secs/km, km/h, bpm (min for zones) */
    float   max;        /* secs/km, km/h, bpm (only used for zones) */
} TRAINING_SETUP_RECORD;

#define TRAINING_GOAL_DISTANCE  (0)
#define TRAINING_GOAL_TIME      (1)
#define TRAINING_GOAL_CALORIES  (2)
#define TRAINING_ZONES_PACE     (3)
#define TRAINING_ZONES_HEART    (4)
#define TRAINING_ZONES_CADENCE  (5)
#define TRAINING_RACE           (6)
#define TRAINING_LAPS_TIME      (7)
#define TRAINING_LAPS_DISTANCE  (8)
#define TRAINING_LAPS_MANUAL    (9)
#define TRAINING_STROKE_RATE    (10)
#define TRAINING_ZONES_SPEED    (11)
#define TRAINING_INTERVALS      (12)

typedef struct
{
    uint8_t  percent;
    uint32_t value;
} GOAL_PROGRESS_RECORD;

typedef struct
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
} INTERVAL_SETUP_RECORD;

#define TTBIN_INTERVAL_TYPE_DISTANCE    (0)
#define TTBIN_INTERVAL_TYPE_TIME        (1)

typedef struct
{
    uint8_t type;   /* 1 = warmup, 2 = work, 3 = rest, 4 = cooldown, 5 = finished */
} INTERVAL_START_RECORD;

#define TTBIN_INTERVAL_TYPE_WARMUP      (1)
#define TTBIN_INTERVAL_TYPE_WORK        (2)
#define TTBIN_INTERVAL_TYPE_REST        (3)
#define TTBIN_INTERVAL_TYPE_COOLDOWN    (4)
#define TTBIN_INTERVAL_TYPE_FINISHED    (5)

typedef struct
{
    uint8_t  type;              /* 1 = warmup, 2 = work, 3 = rest, 4 = cooldown */
    uint32_t total_time;        /* seconds */
    float    total_distance;    /* metres */
    uint16_t total_calories;
} INTERVAL_FINISH_RECORD;

typedef struct
{
    uint32_t status;            /* 1 = poor, 2 = decent, 3 = good, 4 = excellent */
    uint32_t heart_rate;        /* heart rate recovery in bpm */
} HEART_RATE_RECOVERY_RECORD;

typedef struct
{
    int16_t rel_altitude;   /* altitude change from workout start */
    float   total_climb;    /* metres, descents are ignored */
    uint8_t qualifier;      /* not defined yet */
} ALTITUDE_RECORD;

typedef struct
{
    int32_t pool_size;      /* centimeters */
} POOL_SIZE_RECORD;

typedef struct
{
    uint32_t wheel_size;    /* millimetres */
} WHEEL_SIZE_RECORD;

typedef struct
{
    uint32_t wheel_revolutions;
    uint16_t wheel_revolutions_time;
    uint16_t crank_revolutions;
    uint16_t crank_revolutions_time;
} CYCLING_CADENCE_RECORD;

typedef struct
{
    time_t   timestamp;     /* utc time */
    uint16_t length;
    uint8_t  *data;
} UNKNOWN_RECORD;

typedef struct _TTBIN_RECORD
{
    struct _TTBIN_RECORD *prev;
    struct _TTBIN_RECORD *next;
    uint16_t length;
    uint8_t  tag;
    union
    {
        uint8_t                    data[1];
        GPS_RECORD                 gps;
        STATUS_RECORD              status;
        TREADMILL_RECORD           treadmill;
        SWIM_RECORD                swim;
        GYM_RECORD                 gym;
        LAP_RECORD                 lap;
        HEART_RATE_RECORD          heart_rate;
        RACE_SETUP_RECORD          race_setup;
        RACE_RESULT_RECORD         race_result;
        TRAINING_SETUP_RECORD      training_setup;
        GOAL_PROGRESS_RECORD       goal_progress;
        INTERVAL_SETUP_RECORD      interval_setup;
        INTERVAL_START_RECORD      interval_start;
        INTERVAL_FINISH_RECORD     interval_finish;
        HEART_RATE_RECOVERY_RECORD heart_rate_recovery;
        ALTITUDE_RECORD            altitude;
        POOL_SIZE_RECORD           pool_size;
        WHEEL_SIZE_RECORD          wheel_size;
        CYCLING_CADENCE_RECORD     cycling_cadence;
    };
} TTBIN_RECORD;

typedef struct
{
    unsigned count;
    TTBIN_RECORD **records;
} RECORD_ARRAY;

typedef struct
{
    uint8_t  file_version;
    uint8_t  firmware_version[3];
    uint16_t product_id;
    time_t   timestamp_local;
    time_t   timestamp_utc;
    unsigned utc_offset;

    uint8_t  activity;
    float    total_distance;
    uint32_t duration;          /* seconds */
    uint16_t total_calories;

    TTBIN_RECORD *race_setup;
    TTBIN_RECORD *race_result;
    TTBIN_RECORD *training_setup;
    TTBIN_RECORD *interval_setup;
    TTBIN_RECORD *pool_size;
    TTBIN_RECORD *wheel_size;
    TTBIN_RECORD *heart_rate_recovery;

    RECORD_ARRAY gps_records;
    RECORD_ARRAY status_records;
    RECORD_ARRAY treadmill_records;
    RECORD_ARRAY swim_records;
    RECORD_ARRAY gym_records;
    RECORD_ARRAY lap_records;
    RECORD_ARRAY heart_rate_records;
    RECORD_ARRAY goal_progress_records;
    RECORD_ARRAY interval_start_records;
    RECORD_ARRAY interval_finish_records;
    RECORD_ARRAY altitude_records;
    RECORD_ARRAY cycling_cadence_records;

    TTBIN_RECORD *first;
    TTBIN_RECORD *last;
} TTBIN_FILE;

/*****************************************************************************/

TTBIN_FILE *read_ttbin_file(FILE *file);

TTBIN_FILE *parse_ttbin_data(uint8_t *data, uint32_t size);

int write_ttbin_file(const TTBIN_FILE *ttbin, FILE *file);

TTBIN_RECORD *insert_before(TTBIN_FILE *ttbin, TTBIN_RECORD *record);

TTBIN_RECORD *insert_after(TTBIN_FILE *ttbin, TTBIN_RECORD *record);

void delete_record(TTBIN_FILE *ttbin, TTBIN_RECORD *record);

const char *create_filename(TTBIN_FILE *file, const char *ext);

void download_elevation_data(TTBIN_FILE *ttbin);

void export_csv(TTBIN_FILE *ttbin, FILE *file);

void export_gpx(TTBIN_FILE *ttbin, FILE *file);

void export_kml(TTBIN_FILE *ttbin, FILE *file);

void export_tcx(TTBIN_FILE *ttbin, FILE *file);

uint32_t export_formats(TTBIN_FILE *ttbin, uint32_t formats);

void free_ttbin(TTBIN_FILE *ttbin);

void replace_lap_list(TTBIN_FILE *ttbin, float *distances, unsigned count);

int truncate_laps(TTBIN_FILE *ttbin);

int truncate_race(TTBIN_FILE *ttbin);

int truncate_goal(TTBIN_FILE *ttbin);

int truncate_intervals(TTBIN_FILE *ttbin);

/*****************************************************************************/

#define OFFLINE_FORMAT_CSV  (0x00000001)
#define OFFLINE_FORMAT_FIT  (0x00000002)
#define OFFLINE_FORMAT_GPX  (0x00000004)
#define OFFLINE_FORMAT_KML  (0x00000008)
#define OFFLINE_FORMAT_PWX  (0x00000010)
#define OFFLINE_FORMAT_TCX  (0x00000020)

typedef struct
{
    uint32_t mask;
    const char *name;
    int gps_ok;
    int treadmill_ok;
    int pool_swim_ok;
    void (*producer)(TTBIN_FILE* ttbin, FILE *file);
} OFFLINE_FORMAT;

#define OFFLINE_FORMAT_COUNT    (6)
extern const OFFLINE_FORMAT OFFLINE_FORMATS[OFFLINE_FORMAT_COUNT];

uint32_t parse_format_list(const char *formats);

#endif  /* __TTBIN_H__ */

