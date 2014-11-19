/*****************************************************************************\
** ttbin.c                                                                   **
** TTBIN parsing implementation                                              **
\*****************************************************************************/

#include "ttbin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

/*****************************************************************************/

const OFFLINE_FORMAT OFFLINE_FORMATS[OFFLINE_FORMAT_COUNT] = {
    { OFFLINE_FORMAT_CSV, "csv", 0, export_csv },
    { OFFLINE_FORMAT_FIT, "fit", 1, 0          },
    { OFFLINE_FORMAT_GPX, "gpx", 1, export_gpx },
    { OFFLINE_FORMAT_KML, "kml", 1, export_kml },
    { OFFLINE_FORMAT_PWX, "pwx", 1, 0          },
    { OFFLINE_FORMAT_TCX, "tcx", 1, export_tcx },
};

/*****************************************************************************/

#define TAG_FILE_HEADER     (0x20)
#define TAG_STATUS          (0x21)
#define TAG_GPS             (0x22)
#define TAG_HEART_RATE      (0x25)
#define TAG_SUMMARY         (0x27)
#define TAG_LAP             (0x2f)
#define TAG_TREADMILL       (0x32)
#define TAG_SWIM            (0x34)
#define TAG_RACE_SETUP      (0x3c)
#define TAG_RACE_RESULT     (0x3d)

typedef struct __attribute__((packed))
{
    uint8_t tag;
    uint16_t length;
} RECORD_LENGTH;

typedef struct __attribute__((packed))
{
    uint8_t  file_version;
    uint8_t  firmware_version[4];
    uint16_t product_id;
    uint32_t timestamp;     /* local time */
    uint8_t  _unk[96];
    uint32_t timestamp2;    /* local time, duplicate */
    int32_t local_time_offset;  /* seconds from UTC */
    uint8_t  _unk2;
    uint8_t  length_count;  /* number of RECORD_LENGTH objects to follow */
    RECORD_LENGTH lengths[0];
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
    uint16_t speed;         /* m/s * 100 */
    uint32_t timestamp;     /* gps time (utc) */
    uint16_t calories;
    float    inc_distance;  /* metres */
    float    cum_distance;  /* metres */
    uint8_t  cycles;        /* steps/strokes/cycles etc. */
} FILE_GPS_RECORD;

typedef struct __attribute__((packed))
{
    uint8_t  heart_rate;    /* bpm */
    uint8_t  _unk;
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
    uint16_t _unk;
} FILE_TREADMILL_RECORD;

typedef struct __attribute__((packed))
{
    uint32_t timestamp;         /* local time */
    float    total_distance;    /* metres */
    uint8_t  _unk1;             /* always 0xff */
    uint8_t  _unk2;
    uint32_t strokes;           /* since the last report */
    uint32_t completed_laps;
    uint16_t total_calories;
} FILE_SWIM_RECORD;

typedef struct __attribute__((packed))
{
    uint32_t total_time;        /* seconds since activity start */
    float    total_distance;    /* metres */
    uint16_t total_calories;
} FILE_LAP_RECORD;

typedef struct __attribute__((packed))
{
    uint32_t _unk[4];
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

#define realloc_array(ptr, count, index, size)                  \
    do {                                                        \
        if (index >= count)                                     \
        {                                                       \
            ptr = realloc(ptr, (index + 1) * size);             \
            memset(ptr + count, 0, (index + 1 - count) * size); \
            count = index + 1;                                  \
        }                                                       \
    } while (0)

/*****************************************************************************/

TTBIN_FILE *parse_ttbin_data(uint8_t *data, uint32_t size)
{
    uint8_t *end;
    TTBIN_FILE *file;
    int index;

    FILE_HEADER             *file_header = 0;
    FILE_SUMMARY_RECORD     *summary_record;
    FILE_GPS_RECORD         *gps_record;
    FILE_HEART_RATE_RECORD  *heart_rate_record;
    FILE_STATUS_RECORD      *status_record;
    FILE_TREADMILL_RECORD   *treadmill_record;
    FILE_SWIM_RECORD        *swim_record;
    FILE_LAP_RECORD         *lap_record;
    FILE_RACE_SETUP_RECORD  *race_setup_record;
    FILE_RACE_RESULT_RECORD *race_result_record;

    file = malloc(sizeof(TTBIN_FILE));
    memset(file, 0, sizeof(TTBIN_FILE));

    end = data + size;

    while (data < end)
    {
        uint8_t tag = *data++;
        switch (tag)
        {
        case TAG_FILE_HEADER:
            file_header = (FILE_HEADER*)data;
            data += sizeof(FILE_HEADER) + file_header->length_count * sizeof(RECORD_LENGTH);

            file->file_version    = file_header->file_version;
            memcpy(file->firmware_version, file_header->firmware_version, sizeof(file->firmware_version));
            file->product_id      = file_header->product_id;
            file->timestamp_local = file_header->timestamp;
            file->timestamp_utc   = file_header->timestamp - file_header->local_time_offset;
            break;
        case TAG_SUMMARY:
            summary_record = (FILE_SUMMARY_RECORD*)data;

            file->activity       = summary_record->activity;
            file->total_distance = summary_record->distance;
            file->duration       = summary_record->duration;
            file->total_calories = summary_record->calories;
            break;
        case TAG_STATUS:
            status_record = (FILE_STATUS_RECORD*)data;
            file->status_records = realloc(file->status_records, (file->status_record_count + 1) * sizeof(STATUS_RECORD));

            file->status_records[file->status_record_count].status    = status_record->status;
            file->status_records[file->status_record_count].activity  = status_record->activity;
            file->status_records[file->status_record_count].timestamp = status_record->timestamp;
            ++file->status_record_count;
            break;
        case TAG_GPS:
            gps_record = (FILE_GPS_RECORD*)data;

            /* if the GPS signal is lost, 0xffffffff is stored in the file */
            if (gps_record->timestamp == 0xffffffff)
                break;

            index = gps_record->timestamp - file->timestamp_utc;
            if (index < 0)
            {
                file->timestamp_utc   += index;
                file->timestamp_local += index;
                index = 0;
            }

            /* expand the array if necessary */
            realloc_array(file->gps_records, file->gps_record_count, index, sizeof(GPS_RECORD));

            file->gps_records[index].latitude     = gps_record->latitude * 1e-7f;
            file->gps_records[index].longitude    = gps_record->longitude * 1e-7f;
            file->gps_records[index].elevation    = 0.0f;
            file->gps_records[index].heading      = gps_record->heading / 100.0f;
            file->gps_records[index].speed        = gps_record->speed / 100.0f;
            file->gps_records[index].timestamp    = gps_record->timestamp;
            file->gps_records[index].calories     = gps_record->calories;
            file->gps_records[index].inc_distance = gps_record->inc_distance;
            file->gps_records[index].cum_distance = gps_record->cum_distance;
            file->gps_records[index].cycles       = gps_record->cycles;
            break;
        case TAG_HEART_RATE:
            heart_rate_record = (FILE_HEART_RATE_RECORD*)data;

            index = heart_rate_record->timestamp - file->timestamp_local;

            realloc_array(file->heart_rate_records, file->heart_rate_record_count, index, sizeof(HEART_RATE_RECORD));

            file->heart_rate_records[index].timestamp  = heart_rate_record->timestamp;
            file->heart_rate_records[index].heart_rate = heart_rate_record->heart_rate;
            break;
        case TAG_LAP:
            lap_record = (FILE_LAP_RECORD*)data;
            file->lap_records = realloc(file->lap_records, (file->lap_record_count + 1) * sizeof(LAP_RECORD));

            file->lap_records[file->lap_record_count].total_time     = lap_record->total_time;
            file->lap_records[file->lap_record_count].total_distance = lap_record->total_distance;
            file->lap_records[file->lap_record_count].total_calories = lap_record->total_calories;
            ++file->lap_record_count;
            break;
        case TAG_TREADMILL:
            treadmill_record = (FILE_TREADMILL_RECORD*)data;

            index = treadmill_record->timestamp - file->timestamp_local;

            /* expand the array if necessary */
            realloc_array(file->treadmill_records, file->treadmill_record_count, index, sizeof(TREADMILL_RECORD));

            file->treadmill_records[index].timestamp = treadmill_record->timestamp;
            file->treadmill_records[index].distance  = treadmill_record->distance;
            file->treadmill_records[index].calories  = treadmill_record->calories;
            file->treadmill_records[index].steps     = treadmill_record->steps;
            break;
        case TAG_SWIM:
            swim_record = (FILE_SWIM_RECORD*)data;

            index = swim_record->timestamp - file->timestamp_local;

            /* expand the array if necessary */
            realloc_array(file->swim_records, file->swim_record_count, index, sizeof(SWIM_RECORD));

            file->swim_records[index].timestamp      = swim_record->timestamp;
            file->swim_records[index].total_distance = swim_record->total_distance;
            file->swim_records[index].strokes        = swim_record->strokes;
            file->swim_records[index].completed_laps = swim_record->completed_laps;
            file->swim_records[index].total_calories = swim_record->total_calories;
            break;
        case TAG_RACE_SETUP:
            race_setup_record = (FILE_RACE_SETUP_RECORD*)data;

            if (!file->race)
            {
                file->race = (RACE_RECORD*)malloc(sizeof(RACE_RECORD));
                memset(file->race, 0, sizeof(RACE_RECORD));
            }

            file->race->setup.distance = race_setup_record->distance;
            file->race->setup.duration = race_setup_record->duration;
            memcpy(file->race->setup.name, race_setup_record->name, sizeof(race_setup_record->name));
            break;
        case TAG_RACE_RESULT:
            race_result_record = (FILE_RACE_RESULT_RECORD*)data;

            if (!file->race)
            {
                file->race = (RACE_RECORD*)malloc(sizeof(RACE_RECORD));
                memset(file->race, 0, sizeof(RACE_RECORD));
            }

            file->race->result.distance = race_result_record->distance;
            file->race->result.duration = race_result_record->duration;
            file->race->result.calories = race_result_record->calories;
            break;
        default:
            break;
        }

        /* we should have got a file header first... */
        if (!file_header)
        {
            free(file);
            return 0;
        }

        /* increment the data by the correct amount */
        if (tag != TAG_FILE_HEADER)
        {
            index = 0;
            while ((index < file_header->length_count) && (file_header->lengths[index].tag < tag))
                ++index;
            if ((index < file_header->length_count) && (file_header->lengths[index].tag == tag))
                data += file_header->lengths[index].length - 1;
            else
            {
                free(file);
                return 0;
            }
        }
    }

    return file;
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
    }
    sprintf(filename, "%s_%02d-%02d-%02d.%s", type, time->tm_hour, time->tm_min, time->tm_sec, ext);

    return filename;
}

/*****************************************************************************/

typedef struct
{
    GPS_RECORD *data;
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
                info->data->elevation = info->elev;
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
    char *response_data;
    char *str;
    uint32_t i;
    ELEV_DATA_INFO info = {0};
    int result;

    /* only download elevation data if we have GPS records */
    if (!ttbin || !ttbin->gps_record_count || !ttbin->gps_records)
        return;

    curl = curl_easy_init();
    if (!curl)
    {
        fprintf(stderr, "Unable to initialise libcurl\n");
        return;
    }

    /* create the post string to send to the server */
    post_data = malloc(ttbin->gps_record_count * 52 + 10);
    str = post_data;
    str += sprintf(str, "[\n");
    for (i = 0; i < ttbin->gps_record_count; ++i)
    {
        if (i != (ttbin->gps_record_count - 1))
        {
            str += sprintf(str, "   [ %f, %f ],\n",
                ttbin->gps_records[i].latitude,
                ttbin->gps_records[i].longitude);
        }
        else
        {
            str += sprintf(str, "   [ %f, %f ]\n",
                ttbin->gps_records[i].latitude,
                ttbin->gps_records[i].longitude);
        }
    }
    str += sprintf(str, "]\n");

    headers = curl_slist_append(NULL, "Content-Type:text/plain");

    /* setup the callback function data structure */
    info.mult = 1.0;
    info.elev = 0.0;
    info.data = ttbin->gps_records;
    info.max_count = ttbin->gps_record_count;
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
            if (!OFFLINE_FORMATS[i].requires_gps || ttbin->gps_record_count)
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
    if (ttbin)
    {
        if (ttbin->race)
            free(ttbin->race);
        if (ttbin->gps_records)
            free(ttbin->gps_records);
        if (ttbin->status_records)
            free(ttbin->status_records);
        if (ttbin->treadmill_records)
            free(ttbin->treadmill_records);
        if (ttbin->swim_records)
            free(ttbin->swim_records);
        if (ttbin->lap_records)
            free(ttbin->lap_records);
        if (ttbin->heart_rate_records)
            free(ttbin->heart_rate_records);
        free(ttbin);
    }
}
