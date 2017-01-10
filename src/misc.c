/******************************************************************************\
** misc.c                                                                     **
** Implementation file for the miscellaneous shared routines                  **
\******************************************************************************/

#include "download.h"
#include "log.h"
#include "json.h"
#include "misc.h"
#include "ttbin.h"

#include <memory.h>
#include <stdlib.h>
#include <ctype.h>

/*****************************************************************************/
char *replace(char *str, const char *old, const char *newstr)
{
    int old_len = strlen(old);
    int new_len = strlen(newstr);
    int correct = new_len - old_len;

    char *ptr = strstr(str, old);
    while (ptr)
    {
        size_t offset = ptr - str;
        size_t new_str_len = strlen(str) + correct + 1;
        char *new_str = (char*)calloc(1, new_str_len);

        strncpy(new_str, str, offset);
        strcat(new_str, newstr);
        strcat(new_str, ptr + old_len);

        free(str);
        str = new_str;
        ptr = strstr(str, old);
    }

    return str;
}

/*****************************************************************************/
char *get_config_string(TTWATCH *watch, const char *name)
{
    char *start, *end;
    char *url;
    size_t length;
    DOWNLOAD download = { 0, 0 };
    json_value *value = 0;
    unsigned i;

    /* make sure we've actually got a preferences file */
    if (!watch->preferences_file)
        return NULL;

    /* find the value of ConfigURL from the preferences file */
    start = strstr(watch->preferences_file, "<ConfigURL>");
    if (!start)
        return NULL;

    end = strstr(start, "</ConfigURL>");
    if (!end)
        return NULL;

    /* find the start and end of the url value, skipping whitespace */
    start += 11;
    end -= 1;
    while (isspace(*start))
        ++start;
    while (isspace(*end))
        --end;

    if (start >= end)
        return NULL;

    length = end - start + 1;
    url = (char*)malloc(length + 1);
    memcpy(url, start, length);
    url[length] = 0;

    /* download the config JSON file */
    if (download_file(url, &download))
    {
        free(url);
        return NULL;
    }
    free(url);
    url = NULL;

    /* attempt to parse the config JSON file */
    value = json_parse((json_char*)download.data, download.length);
    if (!value || (value->type != json_object))
        goto cleanup;

    /* loop through all the object entries to find the firmware entry */
    for (i = 0; i < value->u.object.length; ++i)
    {
        if (strcasecmp(value->u.object.values[i].name, name) != 0)
            continue;

        if (value->u.object.values[i].value->type == json_string)
            url = strdup(value->u.object.values[i].value->u.string.ptr);

        break;
    }

cleanup:
    json_value_free(value);
    free(download.data);
    return url;
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

/*****************************************************************************/
uint32_t get_configured_formats(TTWATCH *watch)
{
    uint32_t formats = 0;
    if (ttwatch_enumerate_offline_formats(watch, get_configured_formats_callback, &formats) != TTWATCH_NoError)
        write_log(1, "Unable to read configured formats\n");
    return formats;
}

