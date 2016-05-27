/******************************************************************************\
** get_activities.c                                                           **
** Implementation file for the activity download routines                     **
\******************************************************************************/

#include "download.h"
#include "get_activities.h"
#include "log.h"
#include "ttbin.h"

#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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
        data1 = (uint8_t*)malloc(length);
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

/*****************************************************************************/
void do_get_activities(TTWATCH *watch, OPTIONS *options, uint32_t formats)
{
    DGACallback dgacallback = { watch, options, formats };
    if (ttwatch_enumerate_files(watch, TTWATCH_FILE_TTBIN_DATA, do_get_activities_callback, &dgacallback) != TTWATCH_NoError)
        write_log(1, "Unable to enumerate files\n");
}

