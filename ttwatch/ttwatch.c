/******************************************************************************\
** ttwatch.c                                                                  **
** Main implementation file for the TomTom watch linux driver                 **
\******************************************************************************/

#include "ttbin.h"
#include "log.h"
#include "options.h"

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

#include <libusb-1.0/libusb.h>

#include <curl/curl.h>

/*************************************************************************************************/

#define TOMTOM_VENDOR_ID    (0x1390)
#define TOMTOM_PRODUCT_ID   (0x7474)

#define TT_MANIFEST_ENTRY_UTC_OFFSET    (169)

#include "manifest_definitions.h"

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
};

#define MANIFEST_DEFINITION_COUNT (sizeof(MANIFEST_DEFINITIONS) / sizeof(MANIFEST_DEFINITIONS[0]))

/*************************************************************************************************/

typedef struct
{
    uint8_t *data;
    uint32_t length;
} DOWNLOAD;

typedef struct
{
    uint32_t id;
    DOWNLOAD download;
} FIRMWARE_FILE;


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
static size_t write_download(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    DOWNLOAD *download = (DOWNLOAD*)userdata;
    /* reallocate storage for the data to fit the extra size */
    size_t length = size * nmemb;
    download->data = realloc(download->data, download->length + length);

    /* store the new data and update the data length */
    memcpy(download->data + download->length, ptr, length);
    download->length += length;
    return length;
}

/*****************************************************************************/
static int download_file(const char *url, DOWNLOAD *download)
{
    int result;
    CURL *curl = curl_easy_init();
    if (!curl)
    {
        write_log(1, "Unable to initialise libcurl\n");
        return 1;
    }

    download->data   = 0;
    download->length = 0;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "curl/7.35.0");
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 50);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, download);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_download);

    result = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (result != CURLE_OK)
    {
        write_log(1, "Unable to download file: %s (%d)\n", url, result);
        return 1;
    }
    return 0;
}

/*****************************************************************************/
void do_update_gps(TTWATCH *watch)
{
    DOWNLOAD download = {0};

    /* download the data file */
    write_log(0, "Downloading GPSQuickFix data file...\n");
    if (download_file("http://gpsquickfix.services.tomtom.com/fitness/sifgps.f2p3enc.ee", &download))
    {
        free(download.data);
        return;
    }

    write_log(0, "Writing file to watch...\n");
    if (ttwatch_write_verify_whole_file(watch, TTWATCH_FILE_GPSQUICKFIX_DATA, download.data, download.length) == TTWATCH_NoError)
    {
        write_log(0, "GPSQuickFix data updated\n");
        ttwatch_reset_gps_processor(watch);
    }
    else
        write_log(1, "GPSQuickFix update failed\n");

    free(download.data);
}

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
void do_set_time(TTWATCH *watch)
{
    struct tm tm_local;
    time_t utc_time = time(NULL);

    localtime_r(&utc_time, &tm_local);

    int32_t offset = tm_local.tm_gmtoff;
    if ((ttwatch_set_manifest_entry(watch, TT_MANIFEST_ENTRY_UTC_OFFSET, (uint32_t*)&offset) != TTWATCH_NoError) ||
        (ttwatch_write_manifest(watch) != TTWATCH_NoError))
    {
        write_log(1, "Unable to set watch time\n");
        return;
    }

    if (ttwatch_get_watch_time(watch, &utc_time) != TTWATCH_NoError)
        write_log(1, "Unable to get watch time\n");
}

/*****************************************************************************/
/* performs a 'mkdir -p', i.e. creates an entire directory tree */
static void _mkdir(const char *dir)
{
    char tmp[256];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", dir);
    len = strlen(tmp);
    if(tmp[len - 1] == '/')
        tmp[len - 1] = 0;
    for(p = tmp + 1; *p; p++)
    {
        if(*p == '/')
        {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

/*****************************************************************************/
typedef struct
{
    TTWATCH *watch;
    OPTIONS *options;
    uint32_t formats;

} DGACallback;
void do_get_activities_callback(uint32_t id, uint32_t length, void *cbdata)
{
    DGACallback *c = (DGACallback*)cbdata;
    char filename[256] = {0};
    uint8_t *data;
    TTBIN_FILE *ttbin;
    FILE *f;
    struct tm timestamp;
    uint32_t fmt1;
    int i;

    if (ttwatch_read_whole_file(c->watch, id, (void**)&data, 0) != TTWATCH_NoError)
    {
        write_log(1, "Unable to read activity file\n");
        return;
    }

    /* parse the activity file */
    ttbin = parse_ttbin_data(data, length);

    if (ttbin)
        gmtime_r(&ttbin->timestamp_local, &timestamp);
    else
    {
        time_t t = time(NULL);
        gmtime_r(&t, &timestamp);
    }

    /* create the directory name: [store]/[watch name]/[date] */
    strcpy(filename, c->options->activity_store);
    strcat(filename, "/");
    if (ttwatch_get_watch_name(c->watch, filename + strlen(filename), sizeof(filename) - strlen(filename)) == TTWATCH_NoError)
        strcat(filename, "/");
    sprintf(filename + strlen(filename), "%04d-%02d-%02d",
        timestamp.tm_year + 1900, timestamp.tm_mon + 1, timestamp.tm_mday);
    _mkdir(filename);
    chdir(filename);

    /* create the file name */
    if (ttbin)
        sprintf(filename, "%s", create_filename(ttbin, "ttbin"));
    else
        sprintf(filename, "Unknown_%d-%d-%d_%d.ttbin", timestamp.tm_hour, timestamp.tm_min, timestamp.tm_sec, length);

    /* write the ttbin file */
    f = fopen(filename, "w+");
    if (f)
    {
        uint8_t *data1;
        fwrite(data, 1, length, f);

        /* verify that the file was written correctly */
        fseek(f, 0, SEEK_SET);
        data1 = malloc(length);
        if ((fread(data1, 1, length, f) != length) ||
            (memcmp(data, data1, length) != 0))
        {
            write_log(1, "TTBIN file did not verify correctly\n");
            if (ttbin)
                free_ttbin(ttbin);
            free(data);
            free(data1);
            return;
        }
        else
        {
            /* delete the file from the watch only if verification passed */
            ttwatch_delete_file(c->watch, id);
        }
        free(data1);

        fclose(f);
    }
    else
        write_log(1, "Unable to write file: %s\n", filename);

    if (!ttbin)
    {
        write_log(1, "Warning: Corrupt or unrecognisable activity file: %s\n", filename);
        free(data);
        return;
    }

    /* download the elevation data */
    if (c->formats && ttbin->gps_records.count && !c->options->skip_elevation)
    {
        write_log(0, "Downloading elevation data\n");
        download_elevation_data(ttbin);
    }

    /* export_formats returns the formats parameter with bits corresponding to failed exports cleared */
    fmt1 = c->formats ^ export_formats(ttbin, c->formats);
    if (fmt1)
    {
        write_log(1, "Unable to write file formats: ");
        for (i = 0; i < OFFLINE_FORMAT_COUNT; ++i)
        {
            if (fmt1 & OFFLINE_FORMATS[i].mask)
                write_log(1, "%s ", OFFLINE_FORMATS[i].name);
        }
        write_log(1, "\n");
    }

    /* don't run the post-processor as root */
    if (c->options->post_processor && (getuid() != 0))
    {
        if (fork() == 0)
        {
            /* execute the post-processor */
            execl(c->options->post_processor, c->options->post_processor, filename, (char*)0);
        }
    }

    free_ttbin(ttbin);
    free(data);
}
void do_get_activities(TTWATCH *watch, OPTIONS *options, uint32_t formats)
{
    DGACallback dgacallback = { watch, options, formats };
    if (ttwatch_enumerate_files(watch, TTWATCH_FILE_TTBIN_DATA, do_get_activities_callback, &dgacallback) != TTWATCH_NoError)
        write_log(1, "Unable to enumerate files\n");
}

/*****************************************************************************/
/* find the firmware version from the downloaded firmware version XML file */
uint32_t decode_latest_firmware_version(char *data)
{
    uint32_t latest_version = 0;
    char *ptr = strstr(data, "<latestVersion>");
    if (!ptr)
        return 0;
    ptr = strstr(ptr + 15, "<Major>");
    if (!ptr)
        return 0;
    latest_version |= atoi(ptr + 7) << 16;
    ptr = strstr(ptr, "<Minor>");
    if (!ptr)
        return 0;
    latest_version |= atoi(ptr + 7) << 8;
    ptr = strstr(ptr, "<Build>");
    if (!ptr)
        return 0;
    latest_version |= atoi(ptr + 7);
    return latest_version;
}

/*****************************************************************************/
/* find a firmware URL from the downloaded firmware version XML file. Updates the
   provided pointer so that this function can be called repeatedly until it fails,
   to find the complete list of URLs */
char *find_firmware_url(char **data)
{
    char *str;
    char *ptr = strstr(*data, "<URL>");
    if (!ptr)
        return 0;
    *data = ptr + 5;

    ptr = strstr(*data, "</URL>");
    if (!ptr)
        return 0;

    str = malloc(ptr - *data + 1);
    memcpy(str, *data, ptr - *data);
    str[ptr - *data] = 0;

    *data = ptr + 6;

    return str;
}

/*****************************************************************************/
/* updates either one single file (if id is not zero), or all remaining files (if id is zero) */
int update_firmware_file(TTWATCH *watch, FIRMWARE_FILE *files, int file_count, uint32_t id)
{
    int i;
    for (i = 0; i < file_count; ++i)
    {
        if (id && (files[i].id != id))
            continue;
        else if (!id && ((files[i].id == TTWATCH_FILE_SYSTEM_FIRMWARE) || (files[i].id == TTWATCH_FILE_GPS_FIRMWARE) ||
                         (files[i].id == TTWATCH_FILE_MANIFEST1)       || (files[i].id == TTWATCH_FILE_MANIFEST2)))
        {
            continue;
        }

        /* update the file; even if it fails, still update the next file */
        write_log(0, "Updating firmware file: %08x ... ", files[i].id);
        fflush(stdout);
        if (ttwatch_write_verify_whole_file(watch, files[i].id, files[i].download.data, files[i].download.length) != TTWATCH_NoError)
            write_log(0, "Failed\n");
        else
            write_log(0, "Done\n");
    }

    return 0;
}

/*****************************************************************************/
void do_update_firmware(TTWATCH *watch)
{
    uint32_t product_id;
    char url[128];
    DOWNLOAD download;
    uint32_t current_version;
    uint32_t current_ble_version;
    uint32_t latest_version;
    uint32_t latest_ble_version;
    FIRMWARE_FILE *firmware_files = 0;
    int file_count = 0;
    int i;
    char *ptr, *fw_url;
    unsigned long id;

    product_id = watch->product_id;
    current_version = watch->firmware_version;
    current_ble_version = watch->ble_version;

    /* download the firmware information file */
    sprintf(url, "http://download.tomtom.com/sweet/fitness/Firmware/%08X/FirmwareVersionConfigV2.xml?timestamp=%d",
        product_id, (int)time(NULL));

    if (download_file(url, &download))
        goto cleanup;
    /* null-terminate the data (yes, we lose the last byte of the file,
       but since it's XML it's only the last closing bracket) */
    download.data[download.length - 1] = 0;

    /* get the latest firmware version */
    latest_version = decode_latest_firmware_version((char*)download.data);
    if (!latest_version)
    {
        write_log(1, "Unable to determine latest firmware version\n");
        goto cleanup;
    }

    /* find the latest BLE version */
    ptr = strstr((char*)download.data, "<BLE version=\"");
    if (!ptr)
    {
        write_log(1, "Unable to determine latest BLE version\n");
        goto cleanup;
    }
    latest_ble_version = strtoul(ptr + 14, NULL, 0);

    /* check to see if we need to do anything */
    if (latest_ble_version <= current_ble_version)
        write_log(1, "Current BLE firmware is already at latest version\n");
    else
    {
        write_log(0, "Current BLE Firmware Version: %u\n", current_ble_version);
        write_log(0, "Latest BLE Firmware Version : %u\n", latest_ble_version);

        /* find the download URL of the BLE firmware */
        ptr = strstr(ptr, "URL=\"");
        if (!ptr)
        {
            write_log(1, "Unable to determine BLE firmware download URL\n");
            goto cleanup;
        }
        fw_url = ptr + 5;
        ptr = strstr(fw_url, "\"");
        if (!ptr)
        {
            write_log(1, "Unable to determine BLE firmware download URL\n");
            goto cleanup;
        }
        *ptr = 0;

        /* find the BLE firmware file ID */
        ptr = strrchr(fw_url, '/');
        if (!ptr)
        {
            write_log(1, "Unable to determine BLE file ID\n");
            goto cleanup;
        }
        id = strtoul(ptr + 1, NULL, 0);
        if (!id)
        {
            write_log(1, "Unable to determine BLE file ID\n");
            goto cleanup;
        }

        /* create the full URL and download the file */
        sprintf(url, "http://download.tomtom.com/sweet/fitness/Firmware/%08X/%s", product_id, fw_url);

        free(download.data);

        write_log(0, "Download %s ... ", url);
        fflush(stdout);
        if (download_file(url, &download))
        {
            write_log(0, "Failed\n");
            goto cleanup;
        }
        else
            write_log(0, "Done\n");

        /* update the file; even if it fails, still update the next file */
        write_log(0, "Updating firmware file: %08x ... ", TTWATCH_FILE_BLE_FIRMWARE);
        fflush(stdout);
        if (ttwatch_write_verify_whole_file(watch, TTWATCH_FILE_BLE_FIRMWARE, download.data, download.length) != TTWATCH_NoError)
            write_log(0, "Failed\n");
        else
            write_log(0, "Done\n");

        write_log(0, "BLE firmware updated\n");
    }

    /* check to see if we need to update the firmware */
    if (latest_version <= current_version)
        write_log(1, "Current firmware is already at latest version\n");
    else
    {
        write_log(0, "Current Firmware Version: 0x%08x\n", current_version);
        write_log(0, "Latest Firmware Version : 0x%08x\n", latest_version);

        /* download all the firmware files from the server */
        ptr = (char*)download.data;
        while ((fw_url = find_firmware_url(&ptr)) != 0)
        {
            /* find the file ID within the file URL */
            char *start = strrchr(fw_url, '/');
            if (!start)
            {
                free(fw_url);
                continue;
            }

            /* allocate space for the new file */
            firmware_files = (FIRMWARE_FILE*)realloc(firmware_files, ++file_count * sizeof(FIRMWARE_FILE));

            firmware_files[file_count - 1].id = strtoul(start + 1, NULL, 0);
            firmware_files[file_count - 1].download.data   = 0;
            firmware_files[file_count - 1].download.length = 0;

            /* create the full URL and download the file */
            sprintf(url, "http://download.tomtom.com/sweet/fitness/Firmware/%08X/%s", product_id, fw_url);
            free(fw_url);
            write_log(0, "Download %s ... ", url);
            if (download_file(url, &firmware_files[file_count - 1].download))
            {
                write_log(0, "Failed\n");
                goto cleanup;
            }
            else
                write_log(0, "Done\n");
        }

        /* update files in this order: 0x000000f0, 0x00010200, 0x00850000, 0x00850001, others */
        update_firmware_file(watch, firmware_files, file_count, TTWATCH_FILE_SYSTEM_FIRMWARE);
        ttwatch_send_message_group_1(watch);   /* not sure why this is needed */
        update_firmware_file(watch, firmware_files, file_count, TTWATCH_FILE_GPS_FIRMWARE);
        ttwatch_send_message_group_1(watch);   /* not sure why this is needed */
        update_firmware_file(watch, firmware_files, file_count, TTWATCH_FILE_MANIFEST1);
        update_firmware_file(watch, firmware_files, file_count, TTWATCH_FILE_MANIFEST2);
        update_firmware_file(watch, firmware_files, file_count, 0);

        /* resetting the watch causes the watch to disconnect and
           reconnect. This can take almost 90 seconds to complete */
        ttwatch_reset_watch(watch);
        write_log(0, "Waiting for watch to restart");
        for (i = 0; i < 45; ++i)
        {
            sleep(2);
            write_log(0, ".");
        }
        write_log(0, "\n");

        write_log(0, "Firmware updated\n");
    }

cleanup:
    /* free all the allocated data */
    if (download.data)
        free(download.data);
    if (firmware_files)
    {
        while (file_count--)
        {
            if (firmware_files[file_count].download.data)
                free(firmware_files[file_count].download.data);
        }
        free(firmware_files);
    }
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
static void get_configured_formats_callback(const char *id, int auto_open, void *data)
{
    unsigned i;
    for (i = 0; i < OFFLINE_FORMAT_COUNT; ++i)
    {
        if (strcasecmp(id, OFFLINE_FORMATS[i].name) == 0)
            *(uint32_t*)data |= OFFLINE_FORMATS[i].mask;
    }
}
static uint32_t get_configured_formats(TTWATCH *watch)
{
    uint32_t formats = 0;
    if (ttwatch_enumerate_offline_formats(watch, get_configured_formats_callback, &formats) != TTWATCH_NoError)
        write_log(1, "Unable to read configured formats\n");
    return formats;
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
void do_update_race(TTWATCH *watch, char *race)
{
    int activity;
    int index;
    const char *name;
    uint32_t duration;
    uint32_t distance;
    uint32_t checkpoints;
    int num;
    int i;

    /* parse the race data */
    switch (race[0])
    {
    case 'r': activity = ACTIVITY_RUNNING;   break;
    case 'c': activity = ACTIVITY_CYCLING;   break;
    case 's': activity = ACTIVITY_SWIMMING;  break;
    case 't': activity = ACTIVITY_TREADMILL; break;
    case 'f': activity = ACTIVITY_FREESTYLE; break;
    default:
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
    for (i = 0; i < 3; ++i)
    {
        if (sscanf(race, "%d", &num) < 1)
        {
            write_log(1, "Invalid race data specified\n");
            return;
        }
        duration += num;
        while (isdigit(*race) && *race)
            race++;
        if (!*race)
        {
            write_log(1, "Insufficient race data specified\n");
            return;
        }
        if (*race == ':')
        {
            if (i == 2)
            {
                write_log(1, "Invalid race data specified\n");
                return;
            }
            ++race;
            duration *= 60;
        }
        else if (*race == ',')
        {
            ++race;
            break;
        }
        else
        {
            write_log(1, "Invalid race data specified\n");
            return;
        }
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
static void do_list_history_callback(TTWATCH_ACTIVITY activity, int index, const TTWATCH_HISTORY_ENTRY *entry, void *data)
{
    TTWATCH_ACTIVITY act = *(TTWATCH_ACTIVITY*)data;
    if (act != activity)
    {
        switch (activity)
        {
        case TTWATCH_Running:   write_log(0, "Running:\n");  break;
        case TTWATCH_Cycling:   write_log(0, "Cycling:\n");   break;
        case TTWATCH_Swimming:  write_log(0, "Swimming:\n");  break;
        case TTWATCH_Treadmill: write_log(0, "Treadmill:\n"); break;
        case TTWATCH_Freestyle: write_log(0, "Freestyle:\n"); break;
        }
        *(int*)data = activity;
    }
    write_log(0, "%d: %04d/%02d/%02d %02d:%02d:%02d, %4ds, %8.2fm, %4d calories", index + 1,
        entry->year, entry->month, entry->day, entry->hour, entry->minute, entry->second,
        entry->duration, entry->distance, entry->calories);
    if (entry->activity == TTWATCH_Swimming)
        write_log(0, ", %d swolf, %d spl", entry->swolf, entry->strokes_per_lap);
    write_log(0, "\n");
}
void do_list_history(TTWATCH *watch)
{
    unsigned act = -1;
    if (ttwatch_enumerate_history_entries(watch, do_list_history_callback, &act) != TTWATCH_NoError)
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
    default:
        write_log(1, "Invalid activity type specified, must be one of r, c, s, t or f\n");
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
            write_log(0, "%.2f %s", *(float*)&value / float_defn->scaling_factor, float_defn->units);
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
            write_log(0, "%.2f %s", *(float*)&value / float_defn->scaling_factor, float_defn->units);
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
int list_devices_callback(TTWATCH *watch, void *data)
{
    char name[64] = {0};
    ttwatch_get_watch_name(watch, name, sizeof(name));
    write_log(0, "%s: %s\n", watch->serial_number, name);
    return 1;
}

/*****************************************************************************/
void daemon_watch_operations(TTWATCH *watch, OPTIONS *options)
{
    char name[32];
    OPTIONS *new_options = copy_options(options);

    /* make a copy of the options, and load any overriding
       ones from the watch-specific conf file */
    if (ttwatch_get_watch_name(watch, name, sizeof(name)) != TTWATCH_NoError)
    {
        char *filename = malloc(strlen(new_options->activity_store) + 1 + strlen(name) + 1 + 12 + 1);
        sprintf(filename, "%s/%s/ttwatch.conf", new_options->activity_store, name);
        load_conf_file(filename, new_options, LoadDaemonOperations);
        free(filename);
    }

    /* perform the activities the user has requested */
    if (new_options->get_activities)
    {
        uint32_t formats = get_configured_formats(watch);
        if (!new_options->set_formats)
            formats |= new_options->formats;
        do_get_activities(watch, new_options, formats);
    }

    if (new_options->update_gps)
        do_update_gps(watch);

    if (new_options->update_firmware)
        do_update_firmware(watch);

    if (new_options->set_time)
        do_set_time(watch);

    free_options(new_options);
}

/*****************************************************************************/
int hotplug_attach_callback(struct libusb_context *ctx, struct libusb_device *dev,
    libusb_hotplug_event event, void *user_data)
{
    OPTIONS *options = (OPTIONS*)user_data;
    TTWATCH *watch = 0;

    write_log(0, "Watch connected...\n");

    if (ttwatch_open_device(dev, options->select_device ? options->device : 0, &watch) == TTWATCH_NoError)
    {
        daemon_watch_operations(watch, options);

        write_log(0, "Finished watch operations\n");

        ttwatch_close(watch);
    }
    else
        write_log(0, "Watch not processed - does not match user selection\n");
    return 0;
}

/*****************************************************************************/
void daemonise(const char *user)
{
    pid_t pid, sid;
    struct passwd *pwd;
    struct group *grp;
    FILE *file;
    char *group;

    /* we must be run as root */
    if (getuid() != 0)
    {
        write_log(1, "This daemon must be run as root. The --runas=[USER[:GROUP]] parameter can be\n");
        write_log(1, "used to run as an unprivileged user after daemon initialisation\n");
        exit(1);
    }

    /* find the group in the user name */
    group = strchr(user, ':');
    if (group)
        *(group++) = 0;

    /* perform the first fork */
    pid = fork();
    if (pid < 0)
        exit(1);
    if (pid > 0)
        exit(0);

    /* create a new session with no controlling terminal */
    sid = setsid();
    if (sid < 0)
        _exit(1);

    /* perform the second fork */
    pid = fork();
    if (pid)
        _exit(0);

    /* chdir to the root directory to prevent issues with removing inuse directories */
    if (chdir("/") < 0)
        _exit(1);

    /* set umask to 0 since we don't know what it was before */
    umask(0);

    /* create the pid file */
    file = fopen("/var/run/ttwatch.pid", "w");
    if (file)
    {
        fprintf(file, "%d\n", getpid());
        fclose(file);
    }

    /* retrieve information about the user we want to run as */
    if (user)
    {
        pwd = getpwnam(user);
        if (!pwd)
        {
            perror("getpwnam");
            _exit(1);
        }
    }
    if (group)
    {
        grp = getgrnam(group);
        if (!grp)
        {
            perror("getgrnam");
            _exit(1);
        }
        /* rewrite the ':' so when we do a ps, we see the original command line... */
        *(group - 1) = ':';
    }

    /* make the log directory... */
    mkdir("/var/log/ttwatch", 0755);
    /* and make our user the owner */
    if (user)
        chown("/var/log/ttwatch", pwd->pw_uid, pwd->pw_gid);

    /* drop privileges to that of the specified user */
    if ((getuid() == 0) && user)
    {
        if (group)
            setgid(grp->gr_gid);
        else
            setgid(pwd->pw_gid);
        setuid(pwd->pw_uid);
    }
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
    write_log(0, "      --daemon               Run the program in daemon mode\n");
#ifdef UNSAFE
    write_log(0, "      --delete               Deletes a single file from the device\n");
#endif
    write_log(0, "      --delete-history=[ENTRY] Deletes a single history entry from the watch\n");
    write_log(0, "  -d, --device=STRING        Specify which device to use (see below)\n");
    write_log(0, "      --devices              List detected USB devices that can be selected.\n");
    write_log(0, "      --get-activities       Downloads and deletes any activity records\n");
    write_log(0, "                               currently stored on the watch\n");
    write_log(0, "      --get-formats          Displays the list of file formats that are\n");
    write_log(0, "                               saved when the watch is automatically processed\n");
    write_log(0, "      --get-name             Displays the current watch name\n");
    write_log(0, "      --get-time             Returns the current GPS time on the watch\n");
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
    write_log(0, "      --runas=USER[:GROUP]   Run the daemon as the specified user, and\n");
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
    write_log(0, "The program can be run as a daemon, which will automatically perform\n");
    write_log(0, "the operations specified on the command line whenever a watch is\n");
    write_log(0, "connected. The program must be run as root initially, but the --runas\n");
    write_log(0, "parameter can be used to specify a user (and optionally a group) that the\n");
    write_log(0, "program will drop privileges to after it has finished initialising.\n");
    write_log(0, "Without this parameter specified, the program will continue to run as\n");
    write_log(0, "root, which is not recommended for security reasons. Note that this\n");
    write_log(0, "unprivileged user must have access to the USB devices, and write access\n");
    write_log(0, "to the activity store location.\n");
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
        { "daemon",         no_argument,       &options->daemon_mode,     1 },
        { "list-races",     no_argument,       &options->list_races,      1 },
        { "list-history",   no_argument,       &options->list_history,    1 },
        { "clear-data",     no_argument,       &options->clear_data,      1 },
        { "all-settings",   no_argument,       &options->display_settings,1 },
        { "settings",       no_argument,       &options->list_settings,   1 },
        { "auto",           no_argument,       0, 'a' },
        { "help",           no_argument,       0, 'h' },
        { "version",        no_argument,       0, 'v' },
        { "device",         required_argument, 0, 'd' },
        { "activity-store", required_argument, 0, 's' },
        { "set-name",       required_argument, 0, 1   },
        { "set-formats",    required_argument, 0, 2   },
        { "runas",          required_argument, 0, 3   },
        { "delete-history", required_argument, 0, 4   },
        { "update-race",    required_argument, 0, 5   },
        { "setting",        required_argument, 0, 6   },
#ifdef UNSAFE
        { "list",           no_argument,       0, 'l' },
        { "read",           required_argument, 0, 'r' },
        { "write",          required_argument, 0, 'w' },
        { "delete",         required_argument, 0, 7   },
#endif
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
        case 3:     /* set daemon user */
            options->run_as = 1;
            if (options->run_as_user)
                free(options->run_as_user);
            options->run_as_user = strdup(optarg);
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
        write_log(0, "Invalid parameter specified: %s", argv[optind]);
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

    /* if daemon mode is requested ...*/
    if (options->daemon_mode)
    {
        /* if the user has selected a device, make sure it's by name or serial number */
        if (options->select_device)
        {
            if (isdigit(options->device[0]))
            {
                write_log(1, "Device selection in daemon mode must be by name or serial number\n");
                free_options(options);
                return 1;
            }
        }

        /* reload the daemon operations from the config file. We don't load a per-user file */
        load_conf_file("/etc/ttwatch", options, LoadDaemonOperations);

        /* we have to include some useful functions, otherwise there's no point... */
        if (!options->update_firmware && !options->update_gps && !options->get_activities && !options->set_time)
        {
            write_log(1, "To run as a daemon, you must include one or more of:\n");
            write_log(1, "    --update-fw\n");
            write_log(1, "    --update-gps\n");
            write_log(1, "    --get-activities\n");
            write_log(1, "    --set-time\n");
            write_log(1, "    --auto (OR -a)\n");
            free_options(options);
            return 1;
        }

        /* become a daemon */
        daemonise(options->run_as ? options->run_as_user : NULL);

        /* we're a daemon, so open the log file and report that we have started */
        set_log_location(LOG_VAR_LOG);
        write_log(0, "Starting daemon.\n");

        libusb_init(NULL);

        /* setup hot-plug detection so we know when a watch is plugged in */
        if (libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG))
        {
            int result;
            if ((result = libusb_hotplug_register_callback(NULL, LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED,
                LIBUSB_HOTPLUG_ENUMERATE, TOMTOM_VENDOR_ID, TOMTOM_PRODUCT_ID,
                LIBUSB_HOTPLUG_MATCH_ANY, hotplug_attach_callback, options, NULL)) != 0)
            {
                write_log(1, "Unable to register hotplug callback: %d\n", result);
                _exit(1);
            }

            /* infinite loop - handle events every 10 seconds */
            while (1)
            {
                libusb_handle_events_completed(NULL, NULL);
                usleep(10000);
            }
        }
        else
            write_log(0, "System does not support hotplug notification\n");

        free_options(options);
        _exit(0);
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
        !options->list_settings)
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
        do_update_firmware(watch);

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

