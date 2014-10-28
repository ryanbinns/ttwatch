/*****************************************************************************\
** ttbin.h                                                                   **
** TTBIN parsing implementation                                              **
\*****************************************************************************/

#ifndef __TTBIN_H__
#define __TTBIN_H__

#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define ACTIVITY_RUNNING    (0)
#define ACTIVITY_CYCLING    (1)
#define ACTIVITY_SWIMMING   (2)
#define ACTIVITY_TREADMILL  (7)
#define ACTIVITY_FREESTYLE  (8)

typedef struct
{
    float    latitude;      /* degrees */
    float    longitude;     /* degrees */
    float    elevation;     /* metres, initialised to 0 */
    float    heading;       /* degrees, N = 0, E = 90.00 */
    float    speed;         /* m/s */
    time_t   timestamp;     /* gps time (utc) */
    uint16_t calories;
    float    inc_distance;  /* metres */
    float    cum_distance;  /* metres */
    uint8_t  cycles;        /* steps/strokes/cycles etc. */
    uint8_t  heart_rate;    /* bpm, initialised to 0 if no heart-rate information exists */
} GPS_RECORD;

typedef struct
{
    uint8_t  status;        /* 0 = ready, 1 = active, 2 = paused, 3 = stopped */
    uint8_t  activity;      /* 0 = running, 1 = cycling, 2 = swimming, 7 = treadmill, 8 = freestyle */
    time_t   timestamp;     /* local time */
} STATUS_RECORD;

typedef struct
{
    time_t   timestamp;     /* local time */
    float    distance;      /* metres */
    uint16_t calories;
    uint32_t steps;
    uint8_t  heart_rate;    /* bpm, initialised to 0 if no heart-rate information exists */
} TREADMILL_RECORD;

typedef struct
{
    time_t   timestamp;         /* local time */
    float    total_distance;    /* metres */
    uint32_t strokes;           /* since the last report */
    uint32_t completed_laps;
    uint16_t total_calories;
} SWIM_RECORD;

typedef struct
{
    uint32_t total_time;        /* seconds since activity start */
    float    total_distance;    /* metres */
    uint16_t total_calories;
} LAP_RECORD;

typedef struct
{
    uint8_t  file_version;
    uint8_t  firmware_version[4];
    uint16_t product_id;
    time_t   timestamp;

    uint8_t  activity;
    float    total_distance;
    uint32_t duration;          /* seconds, after adding 1 */
    uint16_t total_calories;

    uint8_t  has_heart_rate;    /* 1 if heart rate records are found, 0 otherwise */

    uint32_t gps_record_count;
    GPS_RECORD *gps_records;

    uint32_t status_record_count;
    STATUS_RECORD *status_records;

    uint32_t treadmill_record_count;
    TREADMILL_RECORD *treadmill_records;

    uint32_t swim_record_count;
    SWIM_RECORD *swim_records;

    uint32_t lap_record_count;
    LAP_RECORD *lap_records;
} TTBIN_FILE;

/*****************************************************************************/

TTBIN_FILE *read_ttbin_file(FILE *file);

TTBIN_FILE *parse_ttbin_data(uint8_t *data, uint32_t size);

const char *create_filename(TTBIN_FILE *file, const char *ext);

void download_elevation_data(TTBIN_FILE *ttbin);

void export_csv(TTBIN_FILE *ttbin, FILE *file);

void export_gpx(TTBIN_FILE *ttbin, FILE *file);

void export_kml(TTBIN_FILE *ttbin, FILE *file);

#endif  /* __TTBIN_H__ */

