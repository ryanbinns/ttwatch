/******************************************************************************\
** ttwatch.c                                                                  **
** Main implementation file for the TomTom watch linux driver                 **
\******************************************************************************/

#include "ttbin.h"
#include "json.h"
#include "log.h"
#include "options.h"
#include "misc.h"
#include "firmware.h"
#include "update_gps.h"
#include "set_time.h"

#include "libttwatch.h"

#include <ctype.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <curl/curl.h>

/*************************************************************************************************/

#define MANIFEST_TYPE_ENUM  (0)
#define MANIFEST_TYPE_INT   (1)
#define MANIFEST_TYPE_FLOAT (2)

struct MANIFEST_DEFINITION
{
    const char *name;
    int writable;
    int type;
};

struct MANIFEST_INT_DEFINITION
{
    const char *name;
    int writable;
    int type;
    const char *units;
    uint32_t min;
    uint32_t max;
};

struct MANIFEST_FLOAT_DEFINITION
{
    const char *name;
    int writable;
    int type;
    const char *units;
    float scaling_factor;
    float min;
    float max;
};

struct MANIFEST_ENUM_VALUE
{
    uint32_t value;
    const char *name;
};

struct MANIFEST_ENUM_DEFINITION
{
    const char *name;
    int writable;
    int type;
    int value_count;
    struct MANIFEST_ENUM_VALUE values[];
};

#include "manifest_definitions.h"
#include "manifest_definitions_0001082e.h"
#include "manifest_definitions_00010113.h"

struct
{
    uint32_t version;
    uint32_t count;
    struct MANIFEST_DEFINITION **definitions;
} MANIFEST_DEFINITIONS[] =
{
    { 0x00010819, MANIFEST_DEFINITION_00010819_COUNT, MANIFEST_DEFINITIONS_00010819 },
    { 0x00010822, MANIFEST_DEFINITION_00010819_COUNT, MANIFEST_DEFINITIONS_00010819 },
    { 0x00010823, MANIFEST_DEFINITION_00010819_COUNT, MANIFEST_DEFINITIONS_00010819 },
    { 0x0001082a, MANIFEST_DEFINITION_00010819_COUNT, MANIFEST_DEFINITIONS_00010819 },
    { 0x0001082e, MANIFEST_DEFINITION_0001082e_COUNT, MANIFEST_DEFINITIONS_0001082e },
    { 0x00010113, MANIFEST_DEFINITION_00010113_COUNT, MANIFEST_DEFINITIONS_00010113 },
};

#define MANIFEST_DEFINITION_COUNT (sizeof(MANIFEST_DEFINITIONS) / sizeof(MANIFEST_DEFINITIONS[0]))

/*************************************************************************************************/

void show_device_versions(TTWATCH *watch)
{
    char name[64];

    write_log(0, "Product ID:       0x%08x\n", watch->product_id);
    write_log(0, "BLE Version:      %u\n", watch->ble_version);
    write_log(0, "Firmware Version: %d.%d.%d\n", (watch->firmware_version >> 16) & 0xff,
        (watch->firmware_version >> 8) & 0xff, watch->firmware_version & 0xff);

    if (ttwatch_get_watch_name(watch, name, sizeof(name)) != TTWATCH_NoError)
    {
        write_log(1, "Unable to read watch name\n");
        return;
    }
    write_log(0, "Watch Name:       %s\n", name);
    write_log(0, "Serial Number:    %s\n", watch->serial_number);
}

#ifdef UNSAFE
/*****************************************************************************/
void show_file_list_callback(uint32_t id, uint32_t length, void *data)
{
    write_log(0, "0x%08x: %u\n", id, length);
}
void show_file_list(TTWATCH *watch)
{
    if (ttwatch_enumerate_files(watch, 0, show_file_list_callback, 0) != TTWATCH_NoError)
        write_log(1, "Unable to display file list\n");
}

/*****************************************************************************/
void do_read_file(TTWATCH *watch, uint32_t id, FILE *file)
{
    uint32_t length;
    void *data;

    if (ttwatch_read_whole_file(watch, id, &data, &length) != TTWATCH_NoError)
    {
        write_log(1, "Unable to read file\n");
        return;
    }

    fwrite(data, 1, length, file);
    free(data);
}

/*****************************************************************************/
void do_write_file(TTWATCH *watch, uint32_t id, FILE *file)
{
    uint32_t size = 0;
    uint8_t *data = 0;

    /* read in the whole file */
    while (!feof(file))
    {
        data = (uint8_t*)realloc(data, size + 256);
        size += fread(data + size, 1, 256, file);
    }

    /* write the file to the device */
    ttwatch_write_verify_whole_file(watch, id, data, size);

    free(data);
}

/*****************************************************************************/
void do_delete_file(TTWATCH *watch, uint32_t id)
{
    if (ttwatch_delete_file(watch, id) != TTWATCH_NoError)
        write_log(1, "Unable to delete file\n");
}

/*****************************************************************************/
void read_all_files_callback(uint32_t id, uint32_t length, void *data)
{
    char filename[16];
    FILE *f;
    sprintf(filename, "%08x.bin", id);
    if ((f = fopen(filename, "w")) != 0)
    {
        do_read_file((TTWATCH*)data, id, f);
        fclose(f);
    }
}
void read_all_files(TTWATCH *watch)
{
    if (ttwatch_enumerate_files(watch, 0, read_all_files_callback, watch) != TTWATCH_NoError)
        write_log(1, "Unable to enumerate files\n");
}
#endif  /* UNSAFE */

/*****************************************************************************/
void do_get_time(TTWATCH *watch)
{
    char timestr[64];
    time_t time;
    if (ttwatch_get_watch_time(watch, &time) != TTWATCH_NoError)
        return;

    strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", gmtime(&time));

    write_log(0, "UTC time:   %s\n", timestr);

    int32_t offset;
    if (ttwatch_get_manifest_entry(watch, TT_MANIFEST_ENTRY_UTC_OFFSET, (uint32_t*)&offset) != TTWATCH_NoError)
    {
        write_log(1, "Unable to get UTC offset\n");
        return;
    }

    time += offset;
    strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", gmtime(&time));
    if (offset % 3600)
        write_log(0, "Local time: %s (UTC%+.1f)\n", timestr, offset / 3600.0);
    else
        write_log(0, "Local time: %s (UTC%+d)\n", timestr, offset / 3600);
}

/*****************************************************************************/
void do_get_watch_name(TTWATCH *watch)
{
    char name[64];
    if (ttwatch_get_watch_name(watch, name, sizeof(name)) != TTWATCH_NoError)
        write_log(1, "Unable to get watch name\n");
    else
        write_log(0, "%s\n", name);
}

/*****************************************************************************/
void do_set_watch_name(TTWATCH *watch, const char *name)
{
    if (ttwatch_set_watch_name(watch, name) != TTWATCH_NoError)
        write_log(1, "Unable to write new watch name\n");
    if (ttwatch_update_preferences_modified_time(watch) != TTWATCH_NoError)
        write_log(1, "Unable to write new watch name\n");
}
/*****************************************************************************/
void do_list_formats(TTWATCH *watch)
{
    unsigned i;
    uint32_t formats = get_configured_formats(watch);
    for (i = 0; i < OFFLINE_FORMAT_COUNT; ++i)
    {
        if (formats & OFFLINE_FORMATS[i].mask)
            write_log(0, "%s ", OFFLINE_FORMATS[i].name);
    }

    write_log(0, "\n");
}

/*****************************************************************************/
static void do_set_formats_callback(const char *id, int auto_open, void *data)
{
    ttwatch_remove_offline_format((TTWATCH*)data, id);
}
void do_set_formats(TTWATCH *watch, uint32_t formats)
{
    unsigned i;
    /* make sure we've got some formats... */
    if (!formats)
    {
        write_log(1, "No valid file formats found\n");
        return;
    }

    if (ttwatch_enumerate_offline_formats(watch, do_set_formats_callback, watch) != TTWATCH_NoError)
    {
        write_log(1, "Unable to clear format list\n");
        return;
    }

    for (i = 0; i < OFFLINE_FORMAT_COUNT; ++i)
    {
        if (formats & (1 << i))
        {
            if (ttwatch_add_offline_format(watch, OFFLINE_FORMATS[i].name, 0) != TTWATCH_NoError)
            {
                write_log(1, "Unable to add offline format\n");
                return;
            }
        }
    }

    if (ttwatch_update_preferences_modified_time(watch) != TTWATCH_NoError)
        write_log(1, "Unable to write preferences file\n");

    if (ttwatch_write_preferences(watch) != TTWATCH_NoError)
        write_log(1, "Unable to write preferences file\n");
}

/*****************************************************************************/
static void do_list_races_callback(TTWATCH_ACTIVITY activity, int index, const TTWATCH_RACE_FILE *race, void *data)
{
    static const char ACTIVITY_CHARS[] = "rcs    tf";
    uint32_t i;

    printf("%c%d, \"%s\", %ds, %dm, %d checkpoints = { ", ACTIVITY_CHARS[activity],
        index + 1, race->name, race->time, race->distance, race->checkpoints);
    index = 0;
    for (i = 0; i < race->checkpoints; ++i)
    {
        uint32_t distance = 0;
        do
        {
            distance += race->distances[index];
        }
        while (race->distances[index++] == 0xff);
        printf("%d", distance);
        if (i < (race->checkpoints - 1))
            printf(", ");
        else
            printf(" }\n");
    }
}
void do_list_races(TTWATCH *watch)
{
    if (ttwatch_enumerate_races(watch, do_list_races_callback, 0) != TTWATCH_NoError)
        write_log(1, "Unable to enumerate races\n");
}

/*****************************************************************************/
static int decode_activity_character(char ch, int *activity)
{
    switch (ch)
    {
    case 'r': *activity = ACTIVITY_RUNNING;   break;
    case 'c': *activity = ACTIVITY_CYCLING;   break;
    case 's': *activity = ACTIVITY_SWIMMING;  break;
    case 't': *activity = ACTIVITY_TREADMILL; break;
    case 'f': *activity = ACTIVITY_FREESTYLE; break;
    default:
        return 0;
    }
    return 1;
}

/*****************************************************************************/
static int extract_time_specification(const char *str, uint32_t *time)
{
    const char *ptr = str;
    int i;

    *time = 0;
    for (i = 0; i < 3; ++i)
    {
        int num, count;
        if (sscanf(ptr, "%d%n", &num, &count) < 1)
            return 0;
        *time += num;
        ptr += count;
        if (!*ptr)
            break;
        if (*ptr == ':')
        {
            if (i == 2)
                return 0;
            ++ptr;
            *time *= 60;
        }
        else if (*ptr == ',')
            break;
        else
            return 0;
    }

    return ptr - str;
}

/*****************************************************************************/
void do_update_race(TTWATCH *watch, char *race)
{
    int activity;
    int index;
    const char *name;
    uint32_t duration;
    uint32_t distance;
    uint32_t checkpoints;
    int num;

    /* parse the race data */
    if (!decode_activity_character(race[0], &activity))
    {
        write_log(1, "Invalid activity type specified, must be one of r, c, s, t or f\n");
        return;
    }

    if ((sscanf(race + 1, "%d", &index) < 1) || (index < 1) || (index > 5))
    {
        write_log(1, "Invalid index specified, must be a integer between 1 and 5 inclusive\n");
        return;
    }
    --index;    /* we really want a 0-based index */

    name = strchr(race, ',');
    if (!name)
    {
        write_log(1, "Insufficient race data specified\n");
        return;
    }
    ++name;
    race = strchr(name, ',');
    if (!race)
    {
        write_log(1, "Invalid race data specified\n");
        return;
    }
    if ((race - name) > 16)
    {
        write_log(1, "Race name can be a maximum of 16 characters\n");
        return;
    }
    *race++ = 0;    /* null-terminate the name */

    /* find the duration (just seconds, minutes:seconds or hours:minutes:seconds */
    duration = 0;
    num = extract_time_specification(race, &duration);
    if (!num)
    {
        write_log(1, "Invalid race data specified\n");
        return;
    }
    race += num;
    if (!*race++)
    {
        return;
        write_log(1, "Invalid race data specified\n");
        return;
    }

    /* find the distance and number of checkpoints */
    if ((sscanf(race, "%d,%d", &distance, &checkpoints) < 2) || (distance <= 0) || (checkpoints <= 0))
    {
        write_log(1, "Invalid race data specified\n");
        return;
    }

    if (ttwatch_update_race(watch, (TTWATCH_ACTIVITY)activity,
        index, name, distance, duration, checkpoints) != TTWATCH_NoError)
    {
        write_log(1, "Unable to update race\n");
    }
}

/*****************************************************************************/
typedef struct
{
    TTWATCH *watch;
    int activity;
    float    distance;
    uint32_t duration;
    uint32_t race_data_file_length;
    uint32_t history_data_file_length;
    TTWATCH_RACE_HISTORY_DATA_FILE *race_data_file;
    TTWATCH_HISTORY_DATA_FILE *history_data_file;
} DCCRCBData;
static void do_create_continuous_race_file_callback(uint32_t id, uint32_t length, void *cbdata)
{
    DCCRCBData *data = (DCCRCBData*)cbdata;
    TTWATCH_HISTORY_FILE *file;
    TTWATCH_HISTORY_ENTRY *entry;
    time_t t;
    struct tm timestamp;
    uint32_t race_file_mask    = 0x00720000;
    uint32_t history_file_mask = 0x00730000;
    uint32_t index;
    uint32_t i;

    if (ttwatch_read_whole_file(data->watch, id, (void*)&file, &length) != TTWATCH_NoError)
        return;

    if (file->activity != data->activity)
    {
        free(file);
        return;
    }

    /* find the highest index */
    index = 0xffffffff;
    for (i = 0; i < file->entry_count; ++i)
    {
        entry = (TTWATCH_HISTORY_ENTRY*)(file->data + i * file->entry_length);
        if ((index == 0xffffffff) || (entry->index > index))
            index = entry->index;
    }
    ++index;
    race_file_mask    = TTWATCH_FILE_RACE_HISTORY_DATA | (data->activity << 8) | (index & 0xff);
    history_file_mask = TTWATCH_FILE_HISTORY_DATA      | (data->activity << 8) | (index & 0xff);

    /* write the race data file */
    if (ttwatch_write_whole_file(data->watch, race_file_mask,
                                 data->race_data_file, data->race_data_file_length) != TTWATCH_NoError)
    {
        write_log(1, "Unable to write race history data file\n");
        return;
    }

    /* write the history data file */
    if (ttwatch_write_whole_file(data->watch, history_file_mask,
                                 data->history_data_file, data->history_data_file_length) != TTWATCH_NoError)
    {
        write_log(1, "Unable to write history data file\n");
        ttwatch_delete_file(data->watch, race_file_mask);    /* don't leave orphans */
        return;
    }

    t = time(NULL);
    localtime_r(&t, &timestamp);

    length += file->entry_length;

    file = (TTWATCH_HISTORY_FILE*)realloc(file, length);
    entry = (TTWATCH_HISTORY_ENTRY*)(file->data + file->entry_count * file->entry_length);
    memset(entry, 0, file->entry_length);
    entry->index    = file->entry_count++;
    entry->activity = data->activity;
    entry->year     = timestamp.tm_year + 1900;
    entry->month    = timestamp.tm_mon + 1;
    entry->day      = timestamp.tm_mday;
    entry->hour     = timestamp.tm_hour;
    entry->minute   = timestamp.tm_min;
    entry->second   = timestamp.tm_sec;
    if (!IS_SPARK(data->watch->usb_product_id))
    {
        entry->multisport.duration = data->duration;
        entry->multisport.distance = data->distance;
        entry->multisport.file_id  = index;
    }
    else
    {
        entry->spark.duration = data->duration;
        entry->spark.distance = data->distance;
        entry->spark.file_id  = index;
    }

    if (ttwatch_write_whole_file(data->watch, id, file, length) != TTWATCH_NoError)
    {
        ttwatch_delete_file(data->watch, race_file_mask);    /* don't leave orphans */
        ttwatch_delete_file(data->watch, history_file_mask);
    }
    free(file);
}
void do_create_continuous_race(TTWATCH *watch, char *race)
{
    int activity;
    float total_distance;
    uint32_t total_time;
    DCCRCBData cbdata;

    /* the first character should be race type */
    if (!decode_activity_character(*race++, &activity))
    {
        write_log(1, "Invalid activity type specified, must be one of r, c, s, t or f\n");
        return;
    }

    /* allocate data for the race file */
    cbdata.race_data_file_length = 9;
    cbdata.race_data_file = (TTWATCH_RACE_HISTORY_DATA_FILE*)malloc(cbdata.race_data_file_length);
    cbdata.race_data_file->file_type = TTWATCH_FILE_RACE_HISTORY_DATA;
    cbdata.race_data_file->_unk = 123456;
    cbdata.race_data_file->split_time = 1;

    /* loop through and create the race file */
    total_distance = 0.0f;
    total_time = 0;
    while (*race)
    {
        float distance;
        int count;
        char *ptr;
        char type;
        uint32_t time;
        int imperial;
        uint32_t cum_distance;
        uint16_t *entry_ptr;
        uint32_t i;

        /* skip the comma */
        if (*race++ != ',')
            goto error;

        /* extract the distance from the specification string */
        if ((sscanf(race, "%f%n", &distance, &count) < 1) || (distance < 1.0f))
            goto error;
        race += count;

        /* extract the speed type (time or pace) */
        ptr = race;
        while (*ptr && isalpha(*ptr))
            ++ptr;
        if (!*ptr)
            goto error;
        type = *ptr;
        *ptr = 0;
        if ((type != '/') && (type != '@'))
            goto error;

        /* convert from requested units (must be specified) to centimeters */
        imperial = 0;
        if (strcmp(race, "km") == 0)
            distance *= 100000.0f;
        else if (strcmp(race, "mi") == 0)
        {
            distance *= 160934.4f;
            imperial = 1;
        }
        else if (strcmp(race, "m") == 0)
            distance *= 100.0f;
        else if (strcmp(race, "yd") == 0)
        {
            distance *= 91.44;
            imperial = 1;
        }
        else
            goto error;
        race = ptr + 1;

        /* extract the time specification from the string */
        count = extract_time_specification(race, &time);
        if (!count || (time == 0))
            goto error;
        race += count;

        /* convert from pace to total time if required */
        if (type == '@')
        {
            if (imperial)
                time = (uint32_t)(time * (distance / 160934.4f) + 0.5f);    /* time per mile */
            else
                time = (uint32_t)(time * (distance / 100000.0f) + 0.5f);    /* time per km */
        }

        /* update the length of the file */
        i = cbdata.race_data_file_length;
        cbdata.race_data_file_length += time * sizeof(uint16_t);
        cbdata.race_data_file = (TTWATCH_RACE_HISTORY_DATA_FILE*)realloc(cbdata.race_data_file, cbdata.race_data_file_length);
        entry_ptr = (uint16_t*)((char*)cbdata.race_data_file + i);

        /* write out the file data */
        cum_distance = 0;
        for (i = 1; i <= time; ++i)
        {
            *entry_ptr = (uint16_t)((distance * i / time) - cum_distance + 0.5f);
            cum_distance += *entry_ptr++;
        }
        total_distance += distance;
        total_time += time;
    }
    total_distance /= 100.0f;

    if ((total_distance < 1.0f) || (total_time == 0))
        goto error;

    /* make the history data file */
    cbdata.history_data_file_length = 25;
    cbdata.history_data_file = (TTWATCH_HISTORY_DATA_FILE*)malloc(cbdata.history_data_file_length);
    cbdata.history_data_file->_unk = 1;
    cbdata.history_data_file->entry_count = 4;
    cbdata.history_data_file->entries[0].tag = TTWATCH_HISTORY_ENTRY_TAG_Duration;
    cbdata.history_data_file->entries[0].int_val = total_time;
    cbdata.history_data_file->entries[1].tag = TTWATCH_HISTORY_ENTRY_TAG_Distance;
    cbdata.history_data_file->entries[1].float_val = total_distance;
    cbdata.history_data_file->entries[2].tag = TTWATCH_HISTORY_ENTRY_TAG_AveragePace;
    cbdata.history_data_file->entries[2].float_val = total_distance / total_time;
    cbdata.history_data_file->entries[3].tag = TTWATCH_HISTORY_ENTRY_TAG_AverageSpeed;
    cbdata.history_data_file->entries[3].float_val = total_distance / total_time;

    cbdata.watch    = watch;
    cbdata.activity = activity;
    cbdata.distance = total_distance;
    cbdata.duration = total_time;
    ttwatch_enumerate_files(watch, TTWATCH_FILE_HISTORY_SUMMARY, do_create_continuous_race_file_callback, &cbdata);

    free(cbdata.race_data_file);
    free(cbdata.history_data_file);
    return;

error:
    write_log(1, "Invalid race data specified\n");
    if (cbdata.race_data_file)
        free(cbdata.race_data_file);
    if (cbdata.history_data_file)
        free(cbdata.history_data_file);
}

/*****************************************************************************/
typedef struct
{
    TTWATCH_ACTIVITY activity;
    TTWATCH *watch;
} ListHistoryCallbackData;
static void do_list_history_callback(TTWATCH_ACTIVITY activity, int index, const TTWATCH_HISTORY_ENTRY *entry, void *data)
{
    ListHistoryCallbackData *d = (ListHistoryCallbackData*)data;
    if (d->activity != activity)
    {
        switch (activity)
        {
        case TTWATCH_Running:   write_log(0, "Running:\n");  break;
        case TTWATCH_Cycling:   write_log(0, "Cycling:\n");   break;
        case TTWATCH_Swimming:  write_log(0, "Swimming:\n");  break;
        case TTWATCH_Treadmill: write_log(0, "Treadmill:\n"); break;
        case TTWATCH_Freestyle: write_log(0, "Freestyle:\n"); break;
        case TTWATCH_Gym:       write_log(0, "Gym:\n"); break;
        }
        d->activity = activity;
    }
    write_log(0, "%d: %04d/%02d/%02d %02d:%02d:%02d", index + 1,
        entry->year, entry->month, entry->day, entry->hour, entry->minute, entry->second);
    if (!IS_SPARK(d->watch->usb_product_id))
    {
        write_log(0, ", %4ds, %8.2fm, %4d calories",
            entry->multisport.duration, entry->multisport.distance, entry->multisport.calories);
        if (entry->activity == TTWATCH_Swimming)
            write_log(0, ", %d swolf, %d spl", entry->multisport.swolf, entry->multisport.strokes_per_lap);
    }
    else
    {
        write_log(0, ", %4ds, %8.2fm, %4d calories",
            entry->spark.duration, entry->spark.distance, entry->spark.calories);
        if (entry->activity == TTWATCH_Swimming)
            write_log(0, ", %d swolf, %d spl", entry->spark.swolf, entry->spark.strokes_per_lap);
    }
    write_log(0, "\n");
}
void do_list_history(TTWATCH *watch)
{
    ListHistoryCallbackData cbdata = { -1, watch };
    if (ttwatch_enumerate_history_entries(watch, do_list_history_callback, &cbdata) != TTWATCH_NoError)
        write_log(1, "Unable to enumerate history entries\n");
}

/*****************************************************************************/
void do_delete_history_item(TTWATCH *watch, const char *item)
{
    TTWATCH_ACTIVITY activity;
    int index;
    /* decode the input string */
    switch (item[0])
    {
    case 'r': activity = TTWATCH_Running;   break;
    case 'c': activity = TTWATCH_Cycling;   break;
    case 's': activity = TTWATCH_Swimming;  break;
    case 't': activity = TTWATCH_Treadmill; break;
    case 'f': activity = TTWATCH_Freestyle; break;
    case 'g': activity = TTWATCH_Gym;       break;
    default:
        write_log(1, "Invalid activity type specified, must be one of r, c, s, t, f or g\n");
        return;
    }

    if ((sscanf(item + 1, "%d", &index) < 1) || (index < 1))
    {
        write_log(1, "Invalid index specified, must be a positive integer (1, 2, 3 etc...)\n");
        return;
    }
    --index;    /* we really want a 0-based index */

    if (ttwatch_delete_history_entry(watch, activity, index) != TTWATCH_NoError)
        write_log(1, "Unable to delete history entry\n");
}

/*****************************************************************************/
void do_clear_data(TTWATCH *watch)
{
    if (ttwatch_clear_data(watch) != TTWATCH_NoError)
        write_log(1, "Unable to clear watch data\n");
}

/*****************************************************************************/
void do_display_settings(TTWATCH *watch)
{
    unsigned i;
    int j;
    struct MANIFEST_ENUM_DEFINITION *enum_defn;
    struct MANIFEST_INT_DEFINITION *int_defn;
    struct MANIFEST_FLOAT_DEFINITION *float_defn;
    struct MANIFEST_DEFINITION** definitions = 0;
    uint32_t defn_count = 0;

    /* check to make sure we support this firmware version */
    for (i = 0; i < MANIFEST_DEFINITION_COUNT; ++i)
    {
        if (MANIFEST_DEFINITIONS[i].version == watch->firmware_version)
        {
            definitions = MANIFEST_DEFINITIONS[i].definitions;
            defn_count  = MANIFEST_DEFINITIONS[i].count;
            break;
        }
    }
    if (!definitions)
    {
        write_log(1, "Firmware version not supported\n");
        return;
    }

    for (i = 0; i < defn_count; ++i)
    {
        uint32_t value;

        if (!definitions[i])
            continue;

        if (ttwatch_get_manifest_entry(watch, i, &value) != TTWATCH_NoError)
        {
            write_log(1, "Unable to read manifest entry\n");
            return;
        }

        write_log(0, "%s = ", definitions[i]->name);
        switch (definitions[i]->type)
        {
        case MANIFEST_TYPE_ENUM:
            enum_defn = (struct MANIFEST_ENUM_DEFINITION*)definitions[i];
            for (j = 0; j < enum_defn->value_count; ++j)
            {
                if (value == enum_defn->values[j].value)
                {
                    write_log(0, "%s", enum_defn->values[j].name);
                    break;
                }
            }
            if (j >= enum_defn->value_count)
                write_log(0, "unknown (%u)", value);
            break;
        case MANIFEST_TYPE_INT:
            int_defn = (struct MANIFEST_INT_DEFINITION*)definitions[i];
            write_log(0, "%d %s", (int)value, int_defn->units);
            break;
        case MANIFEST_TYPE_FLOAT:
            float_defn = (struct MANIFEST_FLOAT_DEFINITION*)definitions[i];
            write_log(0, "%.2f %s", (float)value / float_defn->scaling_factor, float_defn->units);
            break;
        }
        write_log(0, "\n");
        
    }
}

/*****************************************************************************/
void do_set_setting(TTWATCH *watch, const char *setting, const char *value)
{
    uint32_t i;
    int j;
    struct MANIFEST_ENUM_DEFINITION *enum_defn;
    struct MANIFEST_INT_DEFINITION *int_defn;
    struct MANIFEST_FLOAT_DEFINITION *float_defn;
    uint32_t int_val;
    float float_val;
    struct MANIFEST_DEFINITION** definitions = 0;
    uint32_t defn_count = 0;

    /* check to make sure we support this firmware version */
    for (i = 0; i < MANIFEST_DEFINITION_COUNT; ++i)
    {
        if (MANIFEST_DEFINITIONS[i].version == watch->firmware_version)
        {
            definitions = MANIFEST_DEFINITIONS[i].definitions;
            defn_count  = MANIFEST_DEFINITIONS[i].count;
            break;
        }
    }
    if (!definitions)
    {
        write_log(1, "Firmware version not supported\n");
        return;
    }

    /* check to see if the setting exists */
    for (i = 0; i < defn_count; ++i)
    {
        if (!definitions[i])
            continue;

        if (strcasecmp(definitions[i]->name, setting))
            continue;

        if (!definitions[i]->writable)
        {
            write_log(1, "Setting is not writable: %s\n", setting);
            return;
        }

        switch (definitions[i]->type)
        {
        case MANIFEST_TYPE_ENUM:
            enum_defn = (struct MANIFEST_ENUM_DEFINITION*)definitions[i];
            for (j = 0; j < enum_defn->value_count; ++j)
            {
                if (!strcasecmp(value, enum_defn->values[j].name))
                {
                    int_val = enum_defn->values[j].value;
                    break;
                }
            }
            if (j >= enum_defn->value_count)
            {
                write_log(0, "Unknown value: %s\n", value);
                return;
            }
            break;
        case MANIFEST_TYPE_INT:
            int_defn = (struct MANIFEST_INT_DEFINITION*)definitions[i];
            if (sscanf(value, "%u", &int_val) != 1)
            {
                write_log(1, "Invalid value specified: %s\n", value);
                return;
            }
            if ((int_val < int_defn->min) || (int_val > int_defn->max))
            {
                write_log(1, "Valid out of range: %u (%u <= value <= %u)\n", int_val,
                    int_defn->min, int_defn->max);
                return;
            }
            break;
        case MANIFEST_TYPE_FLOAT:
            float_defn = (struct MANIFEST_FLOAT_DEFINITION*)definitions[i];
            if (sscanf(value, "%f", &float_val) != 1)
            {
                write_log(1, "Invalid value specified: %s\n", value);
                return;
            }
            if ((float_val < float_defn->min) || (float_val > float_defn->max))
            {
                write_log(1, "Valid out of range: %.3f (%.3f <= value <= %.3f)\n", float_val,
                    float_defn->min, float_defn->max);
                return;
            }
            int_val = (uint32_t)(float_val * float_defn->scaling_factor);
            break;
        }
        if (ttwatch_set_manifest_entry(watch, i, &int_val) != TTWATCH_NoError)
        {
            write_log(1, "Unable to set manifest entry value\n");
            return;
        }
        return;
    }
    write_log(1, "Unknown setting: %s\n", setting);
}

/*****************************************************************************/
void do_get_setting(TTWATCH *watch, const char *setting)
{
    uint32_t i;
    int j;
    struct MANIFEST_ENUM_DEFINITION *enum_defn;
    struct MANIFEST_INT_DEFINITION *int_defn;
    struct MANIFEST_FLOAT_DEFINITION *float_defn;
    struct MANIFEST_DEFINITION** definitions = 0;
    uint32_t defn_count = 0;

    /* check to make sure we support this firmware version */
    for (i = 0; i < MANIFEST_DEFINITION_COUNT; ++i)
    {
        if (MANIFEST_DEFINITIONS[i].version == watch->firmware_version)
        {
            definitions = MANIFEST_DEFINITIONS[i].definitions;
            defn_count  = MANIFEST_DEFINITIONS[i].count;
            break;
        }
    }
    if (!definitions)
    {
        write_log(1, "Firmware version not supported\n");
        return;
    }

    /* check to see if the setting exists */
    for (i = 0; i < defn_count; ++i)
    {
        uint32_t value;
        if (!definitions[i])
            continue;

        if (strcasecmp(definitions[i]->name, setting))
            continue;

        if (ttwatch_get_manifest_entry(watch, i, &value) != TTWATCH_NoError)
        {
            write_log(1, "Unable to read manifest entry\n");
            return;
        }

        write_log(0, "%s = ", definitions[i]->name);
        switch (definitions[i]->type)
        {
        case MANIFEST_TYPE_ENUM:
            enum_defn = (struct MANIFEST_ENUM_DEFINITION*)definitions[i];
            for (j = 0; j < enum_defn->value_count; ++j)
            {
                if (value == enum_defn->values[j].value)
                {
                    write_log(0, "%s", enum_defn->values[j].name);
                    break;
                }
            }
            if (j >= enum_defn->value_count)
                write_log(0, "unknown (%u)", value);
            break;
        case MANIFEST_TYPE_INT:
            int_defn = (struct MANIFEST_INT_DEFINITION*)definitions[i];
            write_log(0, "%d %s", (int)value, int_defn->units);
            break;
        case MANIFEST_TYPE_FLOAT:
            float_defn = (struct MANIFEST_FLOAT_DEFINITION*)definitions[i];
            write_log(0, "%.2f %s", (float)value / float_defn->scaling_factor, float_defn->units);
            break;
        }
        write_log(0, "\n");

        return;
    }
    write_log(1, "Unknown setting: %s\n", setting);
}

/*****************************************************************************/
void do_list_settings(TTWATCH *watch)
{
    uint32_t i;
    int j;
    struct MANIFEST_ENUM_DEFINITION *enum_defn;
    struct MANIFEST_INT_DEFINITION *int_defn;
    struct MANIFEST_FLOAT_DEFINITION *float_defn;
    struct MANIFEST_DEFINITION** definitions = 0;
    uint32_t defn_count = 0;

    /* check to make sure we support this firmware version */
    for (i = 0; i < MANIFEST_DEFINITION_COUNT; ++i)
    {
        if (MANIFEST_DEFINITIONS[i].version == watch->firmware_version)
        {
            definitions = MANIFEST_DEFINITIONS[i].definitions;
            defn_count  = MANIFEST_DEFINITIONS[i].count;
            break;
        }
    }
    if (!definitions)
    {
        write_log(1, "Firmware version not supported\n");
        return;
    }

    for (i = 0; i < defn_count; ++i)
    {
        if (!definitions[i])
            continue;

        write_log(0, "%s = ", definitions[i]->name);
        switch (definitions[i]->type)
        {
        case MANIFEST_TYPE_ENUM:
            enum_defn = (struct MANIFEST_ENUM_DEFINITION*)definitions[i];
            write_log(0, "( ");
            for (j = 0; j < enum_defn->value_count; ++j)
            {
                write_log(0, "%s", enum_defn->values[j].name);
                if (j < enum_defn->value_count - 1)
                    write_log(0, ", ");
            }
            write_log(0, " )");
            break;
        case MANIFEST_TYPE_INT:
            int_defn = (struct MANIFEST_INT_DEFINITION*)definitions[i];
            write_log(0, "integer");
            if ((int_defn->min > 0) || (int_defn->max < 4294967295ul))
            {
                write_log(0, " (");
                if (int_defn->min > 0)
                    write_log(0, "%u <= ", int_defn->min);
                write_log(0, "value");
                if (int_defn->max < 4294967295ul)
                    write_log(0, " <= %u", int_defn->max);
                write_log(0, ")");
            }
            if (int_defn->units[0])
                write_log(0, ", units = %s", int_defn->units);
            break;
        case MANIFEST_TYPE_FLOAT:
            float_defn = (struct MANIFEST_FLOAT_DEFINITION*)definitions[i];
            write_log(0, "float");
            if ((float_defn->min > 0) || (float_defn->max < 4294967.295f))
            {
                write_log(0, " (");
                if (float_defn->min > 0)
                    write_log(0, "%.3f <= ", float_defn->min);
                write_log(0, "value");
                if (float_defn->max < 4294967.295f)
                    write_log(0, " <= %.3f", float_defn->max);
                write_log(0, ")");
            }
            if (float_defn->units[0])
                write_log(0, ", units = %s", float_defn->units);
            break;
        }
        if (!definitions[i]->writable)
            write_log(0, " READ-ONLY");
        write_log(0, "\n");
    }
}

/*****************************************************************************/
void do_factory_reset(TTWATCH *watch)
{
    write_log(0, "Formatting watch ... ");
    fflush(stdout);
    if (ttwatch_format(watch) != TTWATCH_NoError)
    {
        write_log(0, "Failed\n");
        return;
    }
    else
        write_log(0, "Done\n");

    /* force the firmware update */
    do_update_firmware(watch, 1);
}

/*****************************************************************************/
void do_initial_setup(TTWATCH *watch)
{
    static const uint8_t FILE_0x00710000[22] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x02, 0xFA };
    static const uint8_t FILE_0x00710001[22] = {
        0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x02, 0xC3 };
    static const uint8_t FILE_0x00710002[22] = {
        0xFD, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x02, 0xFA };
    static const uint8_t FILE_0x00710003[22] = {
        0xFC, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x02, 0xFA };
    static const uint8_t FILE_0x00710004[22] = {
        0xFB, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x02, 0xF0 };
    static const uint8_t FILE_0x00710100[22] = {
        0xF5, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x02, 0xD2 };
    static const uint8_t FILE_0x00710101[22] = {
        0xF4, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x02, 0xE1 };
    static const uint8_t FILE_0x00710102[22] = {
        0xF3, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x02, 0xE1 };
    static const uint8_t FILE_0x00710103[22] = {
        0xF2, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x02, 0xE1 };
    static const uint8_t FILE_0x00710104[22] = {
        0xF1, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x02, 0xFC };
    static const uint8_t FILE_0x00710700[22] = {
        0xFA, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x02, 0xFA };
    static const uint8_t FILE_0x00710701[22] = {
        0xF9, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x02, 0xC3 };
    static const uint8_t FILE_0x00710702[22] = {
        0xF8, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x02, 0xFA };
    static const uint8_t FILE_0x00710703[22] = {
        0xF7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x02, 0xFA };
    static const uint8_t FILE_0x00710704[22] = {
        0xF6, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x02, 0xF0 };
    static const uint8_t FILE_0x00710800[22] = {
        0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x02, 0xC8 };
    static const uint8_t FILE_0x00710801[22] = {
        0xEF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x02, 0xF8 };
    static const uint8_t FILE_0x00710802[22] = {
        0xEE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x02, 0xFA };
    static const uint8_t FILE_0x00710803[22] = {
        0xED, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x02, 0xE1 };
    static const uint8_t FILE_0x00710804[22] = {
        0xEC, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x02, 0xF0 };

    ttwatch_create_race(watch, TTWATCH_Running,   0, "3MI 25M",       4828.0f,  1500,  6, FILE_0x00710000);
    ttwatch_create_race(watch, TTWATCH_Running,   1, "5KM 26M",       5000.0f,  1560,  8, FILE_0x00710001);
    ttwatch_create_race(watch, TTWATCH_Running,   2, "6MI 50M",       9656.1f,  3000, 12, FILE_0x00710002);
    ttwatch_create_race(watch, TTWATCH_Running,   3, "10KM 50M",     10000.0f,  3000, 12, FILE_0x00710003);
    ttwatch_create_race(watch, TTWATCH_Running,   4, "13.1MI 2HR",   21097.5f,  7200, 30, FILE_0x00710004);
    ttwatch_create_race(watch, TTWATCH_Cycling,   0, "10MI 35M",     16093.4f,  2100, 10, FILE_0x00710100);
    ttwatch_create_race(watch, TTWATCH_Cycling,   1, "30KM 60M",     30000.0f,  3600, 16, FILE_0x00710101);
    ttwatch_create_race(watch, TTWATCH_Cycling,   2, "25MI 1HR",     40233.6f,  3600, 16, FILE_0x00710102);
    ttwatch_create_race(watch, TTWATCH_Cycling,   3, "60KM 135M",    60000.0f,  8100, 36, FILE_0x00710103);
    ttwatch_create_race(watch, TTWATCH_Cycling,   4, "100KM 3.5HR", 100000.0f, 12600, 50, FILE_0x00710104);
    ttwatch_create_race(watch, TTWATCH_Treadmill, 0, "3MI 25M",       4828.0f,  1500,  6, FILE_0x00710700);
    ttwatch_create_race(watch, TTWATCH_Treadmill, 1, "5KM 26M",       5000.0f,  1560,  8, FILE_0x00710701);
    ttwatch_create_race(watch, TTWATCH_Treadmill, 2, "6MI 50M",       9656.1f,  3000, 12, FILE_0x00710702);
    ttwatch_create_race(watch, TTWATCH_Treadmill, 3, "10KM 50M",     10000.0f,  3000, 12, FILE_0x00710703);
    ttwatch_create_race(watch, TTWATCH_Treadmill, 4, "13.1MI 2HR",   21097.5f,  7200, 30, FILE_0x00710704);
    ttwatch_create_race(watch, TTWATCH_Freestyle, 0, "3KM 10M",       3000.0f,   600,  3, FILE_0x00710800);
    ttwatch_create_race(watch, TTWATCH_Freestyle, 1, "5MI 1HR",       8046.7f,  3600, 16, FILE_0x00710801);
    ttwatch_create_race(watch, TTWATCH_Freestyle, 2, "10KM 2.5HR",   10000.0f,  9000, 36, FILE_0x00710802);
    ttwatch_create_race(watch, TTWATCH_Freestyle, 3, "25KM 1HR",     25000.0f,  3600, 16, FILE_0x00710803);
    ttwatch_create_race(watch, TTWATCH_Freestyle, 4, "30MI 2HR",     48280.3f,  7200, 30, FILE_0x00710804);

    ttwatch_create_default_preferences_file(watch);
}

/*****************************************************************************/
int list_devices_callback(TTWATCH *watch, void *data)
{
    char name[64] = {0};
    ttwatch_get_watch_name(watch, name, sizeof(name));
    write_log(0, "%s: %s\n", watch->serial_number, name);
    return 1;
}

/*****************************************************************************/
void help(char *argv[])
{
    int i;
#ifdef UNSAFE
    write_log(0, "Usage: %s [OPTION]... [FILE]\n", argv[0]);
#else
    write_log(0, "Usage: %s [OPTION]...\n", argv[0]);
#endif
    write_log(0, "Perform various operations with an attached TomTom GPS watch.\n");
    write_log(0, "\n");
    write_log(0, "Mandatory arguments to long options are mandatory for short options too.\n");
    write_log(0, "  -h, --help                 Print this help\n");
    write_log(0, "  -s, --activity-store=PATH Specify an alternate place for storing\n");
    write_log(0, "                               downloaded ttbin activity files\n");
    write_log(0, "  -a, --auto                 Same as \"--update-fw --update-gps --get-activities --set-time\"\n");
    write_log(0, "      --all-settings         List all the current settings on the watch\n");
    write_log(0, "      --clear-data           Delete all activities and history data from the\n");
    write_log(0, "                               watch. Does NOT save the data before deleting it\n");
    write_log(0, "      --create-continuous-race [RACE] Create a continuously monitored race and\n");
    write_log(0, "                               uploads it to the watch (see below)\n");
#ifdef UNSAFE
    write_log(0, "      --delete               Deletes a single file from the device\n");
#endif
    write_log(0, "      --delete-history=[ENTRY] Deletes a single history entry from the watch\n");
    write_log(0, "  -d, --device=STRING        Specify which device to use (see below)\n");
    write_log(0, "      --devices              List detected USB devices that can be selected.\n");
    write_log(0, "      --factory-reset        Performs a factory reset on the watch. This option\n");
    write_log(0, "                               must be specified twice for safety.\n");
    write_log(0, "      --get-activities       Downloads and deletes any activity records\n");
    write_log(0, "                               currently stored on the watch\n");
    write_log(0, "      --get-formats          Displays the list of file formats that are\n");
    write_log(0, "                               saved when the watch is automatically processed\n");
    write_log(0, "      --get-name             Displays the current watch name\n");
    write_log(0, "      --get-time             Returns the current GPS time on the watch\n");
    write_log(0, "      --initial-setup        Performs an initial setup for the watch, adding a\n");
    write_log(0, "                               default preferences file and default race files\n");
#ifdef UNSAFE
    write_log(0, "  -l, --list                 List files currently available on the device\n");
#endif
    write_log(0, "      --list-history         Lists the activity history as reported by the watch\n");
    write_log(0, "      --list-races           List the available races on the watch\n");
    write_log(0, "      --packets              Displays the packets being sent/received\n");
    write_log(0, "                               to/from the watch. Only used for debugging\n");
#ifdef UNSAFE
    write_log(0, "  -r, --read=NUMBER          Reads a single file from the device\n");
#endif
    write_log(0, "                               optionally as the specified group\n");
    write_log(0, "      --set-formats=LIST     Sets the list of file formats that are saved\n");
    write_log(0, "                               when processing activity files\n");
    write_log(0, "      --set-name=STRING      Sets a new watch name\n");
    write_log(0, "      --set-time             Updates the time on the watch\n");
    write_log(0, "      --settings             Lists all available settings and their valid\n");
    write_log(0, "                               values and physical units\n");
    write_log(0, "      --setting [SETTING[=VALUE]] Gets or sets a setting on the watch. To get\n");
    write_log(0, "                               the current value of a setting, simply leave off\n");
    write_log(0, "                               the \"=VALUE\" part\n");
    write_log(0, "      --update-fw            Checks for available firmware updates from\n");
    write_log(0, "                               Tomtom's website and updates the watch if\n");
    write_log(0, "                               newer firmware is found\n");
    write_log(0, "      --update-gps           Updates the GPSQuickFix data on the watch\n");
    write_log(0, "      --update-race=[RACE]   Update a race\n");
    write_log(0, "  -v, --version              Shows firmware version and device identifiers\n");
#ifdef UNSAFE
    write_log(0, "  -w, --write=NUMBER         Writes the specified file on the device\n");
#endif
    write_log(0, "\n");
    write_log(0, "ENTRY is a single-character activity type followed by a positive index\n");
    write_log(0, "starting from 1. The indices are listed by the --list-history option. The\n");
    write_log(0, "activity type is specified as 'r' for running, 'c' for cycling, 's' for\n");
    write_log(0, "swimming, 't' for treadmill or 'f' for freestyle. For example:\n");
    write_log(0, "\"--delete-history t3\" would delete the third treadmill entry.\n");
    write_log(0, "\n");
    write_log(0, "NUMBER is an integer specified in decimal, octal, or hexadecimal form.\n");
    write_log(0, "\n");
    write_log(0, "The --device (-d) option takes a string. The string can match either the\n");
    write_log(0, "serial number or the name of the watch. Both are also printed out by the\n");
    write_log(0, "--devices option.\n");
#ifdef UNSAFE
    write_log(0, "\n");
    write_log(0, "Read and Write commands require the file ID to be specified. Available\n");
    write_log(0, "IDs can be found using the List command. If a file ID of 0 is specified,\n");
    write_log(0, "the read command will read all available files and store them in files in\n");
    write_log(0, "the current directory. The optional file argument cannot be used in this\n");
    write_log(0, "case. The write command will not accept a file ID of 0.\n");
    write_log(0, "\n");
    write_log(0, "WARNING: DO NOT WRITE ANY FILES UNLESS YOU REALLY KNOW WHAT YOU ARE DOING!\n");
    write_log(0, "\n");
    write_log(0, "The read and write commands have an optional file argument that either\n");
    write_log(0, "contains (write) or receives (read) the file data. If this argument is\n");
    write_log(0, "not specified, stdin (write) or stdout (read) are used instead.\n");
#endif
    write_log(0, "\n");
    write_log(0, "If --activity-store is not specified, \"~/ttwatch\" is used for storing\n");
    write_log(0, "downloaded activity data, with subdirectories of the watch name and\n");
    write_log(0, "activity date, as per the official TomTom software.\n");
    write_log(0, "\n");
    write_log(0, "LIST is a comma-separated list of file formats. Valid formats are:\n");
    write_log(0, "    ");
    for (i = 0; i < OFFLINE_FORMAT_COUNT; ++i)
    {
        write_log(0, OFFLINE_FORMATS[i].name);
        if (i < (OFFLINE_FORMAT_COUNT - 1))
            write_log(0, ", ");
        else
            write_log(0, ".\n");
    }
    write_log(0, "Case is not important, but there must be no spaces or other characters\n");
    write_log(0, "in the list.\n");
    write_log(0, "\n");
    write_log(0, "RACE is a race specification consisting of 5 comma-separated parts:\n");
    write_log(0, "  <entry>,<name>,<duration>,<distance>,<laps>\n");
    write_log(0, "Where: <entry>    is a single entry as per the entry for deleting a\n");
    write_log(0, "                  history item. index must be between 1 and 5 inclusive.\n");
    write_log(0, "       <name>     is the name of the race. Maximum of 16 characters,\n");
    write_log(0, "                  although only 9 are visible on the watch screen.\n");
    write_log(0, "       <duration> is the duration of the race, specified as seconds,\n");
    write_log(0, "                  minutes:seconds or hours:minutes:seconds.\n");
    write_log(0, "       <distance> is the race distance in metres, must be an integer.\n");
    write_log(0, "       <laps>     is the number of laps to record, evenly spaced.\n");
    write_log(0, "For example: --update-race \"r1,3KM 14:30MIN,14:30,3000,3\"\n");
    write_log(0, "    specifies a race for running 3km in 14:30 minutes with 3 laps stored\n");
    write_log(0, "    (every 1000m - automatically calculated).\n");
    write_log(0, "If the name has spaces in it, the entire race specification must be\n");
    write_log(0, "surrounded in quotes, or the space can be escaped with a '\\'.\n");
    write_log(0, "\n");
    write_log(0, "--create-continuous-race creates a different type of race that shows\n");
    write_log(0, "progress on the watch every second. It appears as a recent activity race\n");
    write_log(0, "rather than a MySports race. It is listed under the date and time that\n");
    write_log(0, "the race is created. It has a different specification string to the\n");
    write_log(0, "--update-race command, and is as follows:\n");
    write_log(0, "  <activity>,<distance/time>[,<distance/time>]...\n");
    write_log(0, "Where: <activity>      is a single-character activity type (r, c, s, f or t)\n");
    write_log(0, "       <distance/time> is a distance and time specification given as\n");
    write_log(0, "                       \"<distance>/<time>\" to race a set distance in a\n");
    write_log(0, "                       set time, or \"<distance>@<pace>\" to race a set\n");
    write_log(0, "                       distance at a given pace.\n");
    write_log(0, "The distance specification consists of a number followed by a unit suffix,\n");
    write_log(0, "either \"km\", \"mi\", \"m\" or \"yd\". Fractional numbers are allowed.\n");
    write_log(0, "The time/pace specification is in seconds, minutes:seconds or\n");
    write_log(0, "hours:minutes:seconds and represents the time per km (if a \"km\" or \"m\"\n");
    write_log(0, "distance is specified) or time per mile (if a \"mi\" or \"yd\" distance is\n");
    write_log(0, "specified. Multiple specifications can be given, separated by commas, which\n");
    write_log(0, "are concatenated in the race.\n");
    write_log(0, "For example:  --create-continuous-race \"r,400m/1:00,10km@4:00\"\n");
    write_log(0, "    specifies a race for running 400m in 1 minute, followed by 10km at 4min/km\n");
    write_log(0, "Note that the watch cannot differentiate between multiple segments, so it\n");
    write_log(0, "will not notify you when you have finished a segment.\n");
}

/*****************************************************************************/
int main(int argc, char *argv[])
{
    int opt;
    int option_index = 0;

    TTWATCH *watch = 0;

    OPTIONS *options = alloc_options();

    /* load the system-wide options */
    load_conf_file("/etc/ttwatch.conf", options, LoadSettingsOnly);
    /* load the user-specific options */
    if (getuid() != 0)
    {
        /* find the user's home directory, either from $HOME or from
           looking at the system password database */
        char *home = getenv("HOME");
        if (!home)
        {
            struct passwd *pwd = getpwuid(getuid());
            home = pwd->pw_dir;
        }
        if (home)
        {
            char *filename = malloc(strlen(home) + 10);
            sprintf(filename, "%s/.ttwatch", home);
            load_conf_file(filename, options, LoadSettingsOnly);
            free(filename);
        }
    }

    struct option long_options[] = {
        { "update-fw",      no_argument,       &options->update_firmware, 1 },
        { "update-gps",     no_argument,       &options->update_gps,      1 },
        { "get-time",       no_argument,       &options->get_time,        1 },
        { "set-time",       no_argument,       &options->set_time,        1 },
        { "get-activities", no_argument,       &options->get_activities,  1 },
        { "packets",        no_argument,       &options->show_packets,    1 },
        { "devices",        no_argument,       &options->list_devices,    1 },
        { "get-formats",    no_argument,       &options->list_formats,    1 },
        { "get-name",       no_argument,       &options->get_name,        1 },
        { "list-races",     no_argument,       &options->list_races,      1 },
        { "list-history",   no_argument,       &options->list_history,    1 },
        { "clear-data",     no_argument,       &options->clear_data,      1 },
        { "all-settings",   no_argument,       &options->display_settings,1 },
        { "settings",       no_argument,       &options->list_settings,   1 },
        { "initial-setup",  no_argument,       &options->initial_setup,   1 },
        { "auto",           no_argument,       0, 'a' },
        { "help",           no_argument,       0, 'h' },
        { "version",        no_argument,       0, 'v' },
        { "device",         required_argument, 0, 'd' },
        { "activity-store", required_argument, 0, 's' },
        { "set-name",       required_argument, 0, 1   },
        { "set-formats",    required_argument, 0, 2   },
        { "delete-history", required_argument, 0, 4   },
        { "update-race",    required_argument, 0, 5   },
        { "setting",        required_argument, 0, 6   },
        { "create-continuous-race", required_argument, 0, 9 },
#ifdef UNSAFE
        { "list",           no_argument,       0, 'l' },
        { "read",           required_argument, 0, 'r' },
        { "write",          required_argument, 0, 'w' },
        { "delete",         required_argument, 0, 7   },
#endif
        { "factory-reset",  no_argument,       0, 8   },
        {0}
    };

    /* check the command-line options */
    while ((opt = getopt_long(argc, argv,
#ifdef UNSAFE
        "lr:w:"
#endif
        "ahd:s:v", long_options, &option_index)) != -1)
    {
        switch (opt)
        {
        case 1:     /* list formats */
            options->set_name = 1;
            if (options->watch_name)
                free(options->watch_name);
            options->watch_name = strdup(optarg);
            break;
        case 2:     /* set formats */
            options->set_formats = 1;
            options->formats = parse_format_list(optarg);
            break;
        case 4:     /* delete history entry */
            options->delete_history = 1;
            if (options->history_entry)
                free(options->history_entry);
            options->history_entry = strdup(optarg);
            break;
        case 5:     /* redefine a race mysports entry */
            options->update_race = 1;
            if (options->race)
                free(options->race);
            options->race = strdup(optarg);
            break;
        case 6:     /* get or set a setting on the watch */
            options->setting = 1;
            if (options->setting_spec)
                free(options->setting_spec);
            options->setting_spec = strdup(optarg);
            break;
        case 8:     /* factory reset */
            ++options->factory_reset;
            break;
        case 9:     /* create continuous race */
            options->create_continuous_race = 1;
            if (options->race)
                free(options->race);
            options->race = strdup(optarg);
            break;

        case 'a':   /* auto mode */
            options->update_firmware = 1;
            options->update_gps      = 1;
            options->get_activities  = 1;
            options->set_time        = 1;
            break;
#ifdef UNSAFE
        case 7:
            options->delete_file = 1;
            if (optarg)
                options->file_id = strtoul(optarg, NULL, 0);
            break;
        case 'l':   /* list files */
            options->list_files = 1;
            break;
        case 'r':   /* read file */
            options->read_file = 1;
            if (optarg)
                options->file_id = strtoul(optarg, NULL, 0);
            break;
        case 'w':   /* write file */
            options->write_file = 1;
            if (optarg)
                options->file_id = strtoul(optarg, NULL, 0);
            break;
#endif
        case 'd':   /* select device */
            options->select_device = 1;
            if (optarg)
            {
                if (options->device)
                    free(options->device);
                options->device = strdup(optarg);
            }
            break;
        case 'v':   /* report version information */
            options->show_versions = 1;
            break;
        case 's':   /* activity store */
            if (optarg)
            {
                if (options->activity_store)
                    free(options->activity_store);
                options->activity_store = strdup(optarg);
            }
            break;
        case 'h': /* help */
            help(argv);
            free_options(options);
            return 0;
        }
    }

#ifdef UNSAFE
    /* keep track of the file argument if one was provided */
    if (optind < argc)
        options->file = strdup(argv[optind]);

    /* make sure we've got compatible command-line options */
    if ((options->read_file + options->write_file + options->delete_file) > 1)
    {
        write_log(1, "Read, Write and Delete files are mutually exclusive\n");
        free_options(options);
        return 1;
    }
    if (options->file && !(options->read_file || options->write_file || options->delete_file))
    {
        write_log(1, "File argument is only used to read/write files\n");
        free_options(options);
        return 1;
    }
#else
    if (optind < argc)
    {
        write_log(0, "Invalid parameter specified: %s\n", argv[optind]);
        free_options(options);
        return 1;
    }
#endif

    if (!options->activity_store)
    {
        /* find the user's home directory, either from $HOME or from
           looking at the system password database */
        char *home = getenv("HOME");
        if (!home)
        {
            struct passwd *pwd = getpwuid(getuid());
            home = pwd->pw_dir;
        }
        options->activity_store = (char*)malloc(strlen(home) + 9);
        if (options->activity_store)
            sprintf(options->activity_store, "%s/ttwatch", home);
    }

    /* we need to do something, otherwise just show the help */
    if (
#ifdef UNSAFE
        !options->read_file && !options->write_file && !options->delete_file && !options->list_files &&
#endif
        !options->update_firmware && !options->update_gps && !options->show_versions &&
        !options->get_activities && !options->get_time && !options->set_time &&
        !options->list_devices && !options->get_name && !options->set_name &&
        !options->list_formats && !options->set_formats && !options->list_races &&
        !options->list_history && !options->delete_history && !options->update_race &&
        !options->clear_data && !options->display_settings && !options->setting &&
        !options->list_settings && !options->factory_reset && !options->initial_setup &&
        !options->create_continuous_race)
    {
        help(argv);
        free_options(options);
        return 0;
    }

    libusb_init(NULL);

    if (options->show_packets)
        ttwatch_show_packets(1);

    if (options->list_devices)
    {
        ttwatch_enumerate_devices(list_devices_callback, 0);
        return 0;
    }

    if (ttwatch_open(options->select_device ? options->device : 0, &watch) != TTWATCH_NoError)
    {
        write_log(1, "Unable to open watch\n");
        free_options(options);
        return 1;
    }

    if (options->show_versions)
        show_device_versions(watch);

#ifdef UNSAFE
    if (options->list_files)
        show_file_list(watch);

    if (options->read_file)
    {
        if (options->file_id == 0)
            read_all_files(watch);
        else
        {
            FILE *f;
            if (!options->file)
                f = stdout;
            else
                f = fopen(options->file, "w");
            if (!f)
                write_log(1, "Unable to open file: %s\n", options->file);
            else
            {
                do_read_file(watch, options->file_id, f);
                if (f != stdout)
                    fclose(f);
            }
        }
    }

    if (options->write_file)
    {
        if (options->file_id == 0)
            write_log(1, "File ID must be non-zero when writing a file\n");
        else
        {
            FILE *f;
            if (!options->file)
                f = stdin;
            else
                f = fopen(options->file, "r");
            if (!f)
                write_log(1, "Unable to open file: %s\n", options->file);
            else
            {
                do_write_file(watch, options->file_id, f);
                if (f != stdin)
                    fclose(f);
            }
        }
    }

    if (options->delete_file)
    {
        if (options->file_id == 0)
            write_log(1, "File ID must be non-zero when writing a file\n");
        else
            do_delete_file(watch, options->file_id);
    }
#endif

    if (options->factory_reset)
    {
        /* this option must be specified at least twice */
        if (options->factory_reset < 2)
            write_log(1, "--factory-reset must be specified twice, otherwise it is ignored\n");
        else
            do_factory_reset(watch);
    }

    if (options->initial_setup)
        do_initial_setup(watch);

    if (options->get_time)
        do_get_time(watch);

    if (options->set_time)
        do_set_time(watch);

    if (options->get_activities)
    {
        char name[32];
        if (!ttwatch_get_watch_name(watch, name, sizeof(name)) == TTWATCH_NoError)
        {
            char *filename = malloc(strlen(options->activity_store) + 1 + strlen(name) + 1 + 12 + 1);
            sprintf(filename, "%s/%s/ttwatch.conf", options->activity_store, name);
            load_conf_file(filename, options, LoadDaemonOperations);
            free(filename);
        }
        do_get_activities(watch, options, get_configured_formats(watch));
    }

    if (options->update_gps)
        do_update_gps(watch);

    if (options->update_firmware)
        do_update_firmware(watch, 0);

    if (options->get_name)
        do_get_watch_name(watch);

    if (options->set_name)
        do_set_watch_name(watch, options->watch_name);

    if (options->list_formats)
        do_list_formats(watch);

    if (options->set_formats)
        do_set_formats(watch, options->formats);

    if (options->list_races)
        do_list_races(watch);

    if (options->update_race)
        do_update_race(watch, options->race);

    if (options->create_continuous_race)
        do_create_continuous_race(watch, options->race);

    if (options->list_history)
        do_list_history(watch);

    if (options->delete_history)
        do_delete_history_item(watch, options->history_entry);

    if (options->clear_data)
        do_clear_data(watch);

    if (options->display_settings)
        do_display_settings(watch);

    if (options->setting)
    {
        char *str = strchr(options->setting_spec, '=');
        if (str)
        {
            *str = 0;
            do_set_setting(watch, options->setting_spec, ++str);
        }
        else
            do_get_setting(watch, options->setting_spec);
    }

    if (options->list_settings)
        do_list_settings(watch);

    ttwatch_close(watch);

    libusb_exit(NULL);

    free_options(options);
    return 0;
}

