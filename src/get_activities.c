/******************************************************************************\
** get_activities.c                                                           **
** Implementation file for the activity download routines                     **
\******************************************************************************/

#include "download.h"
#include "export.h"
#include "get_activities.h"
#include "log.h"
#include "ttbin.h"
#include "protobuf.h"

#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

/*****************************************************************************/
typedef struct
{
    TTWATCH *watch;
    OPTIONS *options;
    uint32_t formats;

} DGACallback;

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
/* create the directory name from store, watch name, and date */
static void create_directory_name(DGACallback *c, struct tm timestamp)
{
    char dir_name[256] = {0};

    strcpy(dir_name, c->options->activity_store);
    strcat(dir_name, "/");
    if (ttwatch_get_watch_name(c->watch, dir_name + strlen(dir_name), sizeof(dir_name) - strlen(dir_name)) == TTWATCH_NoError)
        strcat(dir_name, "/");
    sprintf(dir_name + strlen(dir_name), "%04d-%02d-%02d",
        timestamp.tm_year + 1900, timestamp.tm_mon + 1, timestamp.tm_mday);
    _mkdir(dir_name);
    chdir(dir_name);
}

/*****************************************************************************/
static void do_get_activities_callback(uint32_t id, uint32_t length, void *cbdata)
{
    DGACallback *c = (DGACallback*)cbdata;
    char filename[256] = {0};
    uint8_t *data;
    TTBIN_FILE *ttbin;
    FILE *f;
    struct tm timestamp;
    uint32_t fmt1;
    int i;
    char cwd[PATH_MAX];

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

    /* get the current directory so we can restore it later */
    getcwd(cwd, sizeof(cwd));

    /* create the directory name: [store]/[watch name]/[date] */
    create_directory_name(c, timestamp);

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
        data1 = (uint8_t*)malloc(length);
        if ((fread(data1, 1, length, f) != length) ||
            (memcmp(data, data1, length) != 0))
        {
            write_log(1, "TTBIN file did not verify correctly\n");
            if (ttbin)
                free_ttbin(ttbin);
            free(data);
            free(data1);
            chdir(cwd);
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
        chdir(cwd);
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
    chdir(cwd);
}

/*****************************************************************************/
void do_get_activities(TTWATCH *watch, OPTIONS *options, uint32_t formats)
{
    DGACallback dgacallback = { watch, options, formats };
    if (ttwatch_enumerate_files(watch, TTWATCH_FILE_TTBIN_DATA, do_get_activities_callback, &dgacallback) != TTWATCH_NoError)
        write_log(1, "Unable to enumerate files\n");
}


/*****************************************************************************/
static void do_get_activity_summaries_callback(uint32_t id, uint32_t length, void *cbdata)
{
    DGACallback *c = (DGACallback*)cbdata;
    char filename[256] = {0};
    uint8_t *data;
    PROTOBUF_FILE *protobuf = NULL;
    FILE *f;
    struct tm timestamp;
    uint32_t fmt1;
    int i;
    char cwd[PATH_MAX];

    /* We need to skip IDs where the high bit of the least significant nibble isn't set */
    if (! (id & 0x08)) {
        return;
    }

    if (ttwatch_read_whole_file(c->watch, id, (void**)&data, 0) != TTWATCH_NoError)
    {
        write_log(1, "Unable to read activity file\n");
        return;
    }

    /* parse the activity summary file */
    protobuf = parse_protobuf_data(data, length);

    if (protobuf)
        gmtime_r(&protobuf->timestamp_utc, &timestamp);
    else
    {
        time_t t = time(NULL);
        gmtime_r(&t, &timestamp);
    }

    /* get the current directory so we can restore it later */
    getcwd(cwd, sizeof(cwd));

    /* create the directory name: [store]/[watch name]/[date] */
    create_directory_name(c, timestamp);

    /* create the file name */
    if (protobuf)
        sprintf(filename, "%s", create_protobuf_filename(protobuf, "protobuf"));
    else
        sprintf(filename, "Summary_%08X-%02d-%02d-%02d.protobuf", id, timestamp.tm_year + 1900, timestamp.tm_mon + 1, timestamp.tm_mday);

    /* write the protobuf file */
    f = fopen(filename, "w+");
    if (f)
    {
        uint8_t *data1;
        fwrite(data, 1, length, f);

        /* verify that the file was written correctly */
        fseek(f, 0, SEEK_SET);
        data1 = (uint8_t*)malloc(length);
        if ((fread(data1, 1, length, f) != length) ||
            (memcmp(data, data1, length) != 0))
        {
            write_log(1, "Protobuffer file did not verify correctly\n");
            if (protobuf)
                free_protobuf(protobuf);
            free(data);
            free(data1);
            chdir(cwd);
            return;
        }
        free(data1);

        fclose(f);
    }
    else
        write_log(1, "Unable to write file: %s\n", filename);

    if (!protobuf)
    {
        write_log(1, "Warning: Corrupt or unrecognisable activity file: %s\n", filename);
        free(data);
        chdir(cwd);
        return;
    }

    /* export_formats returns the formats parameter with bits corresponding to failed exports cleared */
    fmt1 = c->formats ^ export_protobuf_formats(protobuf, c->formats);
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

    free_protobuf(protobuf);
    free(data);
    chdir(cwd);
}

/*****************************************************************************/
void do_get_activity_summaries(TTWATCH *watch, OPTIONS *options, uint32_t formats)
{
    DGACallback dgacallback = { watch, options, formats };
    if (ttwatch_enumerate_files(watch, TTWATCH_FILE_ACTIVITY_SUMMARY, do_get_activity_summaries_callback, &dgacallback) != TTWATCH_NoError)
        write_log(1, "Unable to enumerate files\n");
}

