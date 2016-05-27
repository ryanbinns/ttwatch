/******************************************************************************\
** download.c                                                                 **
** Implementation file for file download routine                              **
\******************************************************************************/

#include "download.h"
#include "log.h"

#include <memory.h>
#include <stdlib.h>

#include <curl/curl.h>

/*****************************************************************************/
static size_t write_download(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    DOWNLOAD *download = (DOWNLOAD*)userdata;
    /* reallocate storage for the data to fit the extra size */
    size_t length = size * nmemb;
    download->data = (uint8_t*)realloc(download->data, download->length + length);

    /* store the new data and update the data length */
    memcpy(download->data + download->length, ptr, length);
    download->length += length;
    return length;
}

/*****************************************************************************/
int download_file(const char *url, DOWNLOAD *download)
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

