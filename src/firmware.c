/******************************************************************************\
** firmware.c                                                                 **
** Implementation file for firmware updating routines                         **
\******************************************************************************/

#include "download.h"
#include "firmware.h"
#include "log.h"
#include "misc.h"

#include <memory.h>
#include <stdlib.h>
#include <stdio.h>

/*****************************************************************************/

typedef struct
{
    uint32_t id;
    DOWNLOAD download;
} FIRMWARE_FILE;

/*****************************************************************************/
/* find the firmware version from the downloaded firmware version XML file */
static uint32_t decode_latest_firmware_version(char *data)
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
static char *find_firmware_url(char **data)
{
    char *str;
    char *ptr = strstr(*data, "<URL>");
    if (!ptr)
        return 0;
    *data = ptr + 5;

    ptr = strstr(*data, "</URL>");
    if (!ptr)
        return 0;

    str = (char*)malloc(ptr - *data + 1);
    memcpy(str, *data, ptr - *data);
    str[ptr - *data] = 0;

    *data = ptr + 6;

    return str;
}

/*****************************************************************************/
/* updates either one single file (if id is not zero), or all remaining files (if id is zero) */
static int update_firmware_file(TTWATCH *watch, FIRMWARE_FILE *files, int file_count, uint32_t id)
{
    int i;
    for (i = 0; i < file_count; ++i)
    {
        if (id && (files[i].id != id))
            continue;
        else if (!id && ((files[i].id == TTWATCH_FILE_SYSTEM_FIRMWARE) || (files[i].id == TTWATCH_FILE_GPS_FIRMWARE) ||
                         (files[i].id == TTWATCH_FILE_MANIFEST1)       || (files[i].id == TTWATCH_FILE_MANIFEST2) ||
                         (files[i].id == TTWATCH_FILE_BLE_FIRMWARE)))
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
int do_update_firmware(TTWATCH *watch, int force)
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
    char *ptr, *fw_url;
    char *serial = 0;
    char *fw_config_url = 0;
    char *fw_base_url = 0;
    int ret = 0;

    product_id = watch->product_id;
    current_version = watch->firmware_version;
    current_ble_version = watch->ble_version;
    serial = strdup(watch->serial_number);

    /* find the firmware config URL */
    fw_config_url = get_config_string(watch, "service:firmware");
    if (!fw_config_url)
    {
        write_log(1, "Unable to determine firmware verion information\n");
        return 0;
    }
    fw_config_url = replace(fw_config_url, "{PRODUCT_ID}", "%08X");

    /* find the firmware base URL */
    fw_base_url = strdup(fw_config_url);
    ptr = strrchr(fw_base_url, '/');
    if (!ptr || ((strlen(fw_base_url) - (ptr - fw_base_url)) < 3))
    {
        write_log(1, "Invalid firmware config URL\n");
        goto cleanup;
    }
    *++ptr = '%';
    *++ptr = 's';
    *++ptr = 0;

    /* download the firmware information file */
    sprintf(url, fw_config_url, product_id);
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

    /* find the latest BLE version for the Multisport version*/
    if (!IS_SPARK(watch->usb_product_id))
    {
        ptr = strstr((char*)download.data, "<BLE version=\"");
        if (!ptr)
        {
            write_log(1, "Unable to determine latest BLE version\n");
            goto cleanup;
        }
        latest_ble_version = strtoul(ptr + 14, NULL, 0);

        /* check to see if we need to do anything */
        if (!force && (latest_ble_version <= current_ble_version))
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

            firmware_files = (FIRMWARE_FILE*)realloc(firmware_files, ++file_count * sizeof(FIRMWARE_FILE));

            firmware_files[file_count - 1].id = strtoul(ptr + 1, NULL, 0);
            firmware_files[file_count - 1].download.data   = 0;
            firmware_files[file_count - 1].download.length = 0;

            /* create the full URL and download the file */
            sprintf(url, fw_base_url, product_id, fw_url);
            write_log(0, "Download %s ... ", url);
            if (download_file(url, &firmware_files[file_count - 1].download))
            {
                write_log(0, "Failed\n");
                goto cleanup;
            }
            else
                write_log(0, "Done\n");
        }
    }

    /* check to see if we need to update the firmware */
    if (!force && (latest_version <= current_version))
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
            sprintf(url, fw_base_url, product_id, fw_url);
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
    }

    if (file_count > 0)
    {
        /* update files in this order: 0x000000f0, 0x00010200, 0x00850000, 0x00850001, others */
        update_firmware_file(watch, firmware_files, file_count, TTWATCH_FILE_BLE_FIRMWARE);
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
        write_log(0, "Firmware updated\n");
        ret = 1;
    }

cleanup:
    /* free all the allocated data */
    free(fw_config_url);
    free(fw_base_url);
    free(serial);
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
    return ret;
}
