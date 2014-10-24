/*****************************************************************************\
** messages.c                                                                **
** Message transmission/reception functions                                  **
\*****************************************************************************/

#include "messages.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

extern int show_packets;

void print_packet(uint8_t *packet, uint8_t size)
{
    int i;
    if (show_packets)
    {
        for (i = 0; i < size; ++i)
            write_log(0, "%02X ", packet[i]);
        write_log(0, "\n");
    }
}

int send_packet(libusb_device_handle *handle, uint8_t msg, uint8_t tx_length,
    const uint8_t *tx_data, uint8_t rx_length, uint8_t *rx_data)
{
    static uint8_t message_counter = 0;
    uint8_t packet[64] = {0};
    int count  = 0;
    int result = 0;

    /* create the tx packet */
    packet[0] = 0x09;
    packet[1] = tx_length + 2;
    packet[2] = message_counter++;
    packet[3] = msg;
    memcpy(packet + 4, tx_data, tx_length);

    print_packet(packet, tx_length + 4);

    /* send the packet */
    result = libusb_interrupt_transfer(handle, 0x05, packet, tx_length + 4, &count, 10000);
    if (result || (count != tx_length + 4))
        return 1;

    /* read the reply */
    result = libusb_interrupt_transfer(handle, 0x84, packet, 64, &count, 10000);
    if (result)
        return 2;

    print_packet(packet, packet[1] + 2);

    /* check that the reply is valid */
    if (packet[0] != 0x01)
        return 3;
    if ((rx_length <= 60) && (packet[1] != (rx_length + 2)))
        return 4;
    if (packet[2] != (uint8_t)(message_counter - 1))
        return 5;
    if (msg == MSG_READ_FILE_DATA_REQUEST)
    {
        if (packet[3] != MSG_READ_FILE_DATA_RESPONSE)
            return 6;
    }
    else
    {
        if (packet[3] != msg)
            return 7;
    }

    /* copy the back data to the caller */
    if (rx_data)
        memcpy(rx_data, packet + 4, packet[1] - 2);
    return 0;
}

int open_file(libusb_device_handle *device, uint32_t id, int write)
{
    TXFileOperationPacket request = { htobe32(id) };
    RXFileOperationPacket response = {0};
    if (send_packet(device, write ? MSG_OPEN_FILE_WRITE : MSG_OPEN_FILE_READ,
            sizeof(request), (uint8_t*)&request, sizeof(response), (uint8_t*)&response))
    {
        write_log(1, "Unable to open file: 0x%08x (%c)\n", id, write ? 'w' : 'r');
        return 1;
    }

    return (request.id != response.id) || (be32toh(response.error) != 0);
}

int close_file(libusb_device_handle *device, uint32_t id)
{
    TXFileOperationPacket request = { htobe32(id) };
    RXFileOperationPacket response = {0};
    if (send_packet(device, MSG_CLOSE_FILE, sizeof(request), (uint8_t*)&request, sizeof(response), (uint8_t*)&response))
    {
        write_log(1, "Unable to close file: 0x%08x\n", id);
        return 1;
    }

    return 0;
}

int delete_file(libusb_device_handle *device, uint32_t id)
{
    TXFileOperationPacket request = { htobe32(id) };
    RXFileOperationPacket response = {0};
    if (send_packet(device, MSG_DELETE_FILE, sizeof(request), (uint8_t*)&request, sizeof(response), (uint8_t*)&response))
    {
        write_log(1, "Unable to delete file: 0x%08x\n", id);
        return 1;
    }

    return 0;
}

uint32_t get_file_size(libusb_device_handle *device, uint32_t id)
{
    TXFileOperationPacket request = { htobe32(id) };
    RXGetFileSizePacket response = {0};
    if (send_packet(device, MSG_GET_FILE_SIZE, sizeof(request), (uint8_t*)&request, sizeof(response), (uint8_t*)&response))
    {
        write_log(1, "Unable to get file size: 0x%08x\n", id);
        return 0;
    }

    return be32toh(response.file_size);
}

int get_file_data(libusb_device_handle *device, uint32_t id, uint8_t *data, uint32_t length)
{
    TXReadFileDataPacket request = { htobe32(id), htobe32(length) };
    RXReadFileDataPacket response = {0};
    if (send_packet(device, MSG_READ_FILE_DATA_REQUEST, sizeof(request), (uint8_t*)&request, length + 8, (uint8_t*)&response))
    {
        write_log(1, "Unable to read file data: 0x%08x\n", id);
        return 1;
    }

    if (request.id != response.id)
        return 1;

    memcpy(data, response.data, length);
    return 0;
}

int write_file_data(libusb_device_handle *device, uint32_t id, uint8_t *data, uint32_t length)
{
    int result;
    TXWriteFileDataPacket request = { htobe32(id) };
    RXWriteFileDataPacket response = {0};
    memcpy(request.data, data, length);
    if (result = send_packet(device, MSG_WRITE_FILE_DATA, length + 4, (uint8_t*)&request, sizeof(response), (uint8_t*)&response))
    {
        write_log(1, "Unable to write file data: 0x%08x (%d)\n", id, result);
        return 1;
    }

    return (request.id != response.id);
}

int find_first_file(libusb_device_handle *device, RXFindFilePacket *response)
{
    TXFindFirstFilePacket request = {0};
    if (send_packet(device, MSG_FIND_FIRST_FILE, sizeof(request), (uint8_t*)&request, sizeof(*response), (uint8_t*)response))
    {
        write_log(1, "Unable to find first file\n");
        return 1;
    }

    response->id = be32toh(response->id);
    response->file_size = be32toh(response->file_size);
    response->end_of_list = be32toh(response->end_of_list);

    return 0;
}

int find_next_file(libusb_device_handle *device, RXFindFilePacket *response)
{
    if (send_packet(device, MSG_FIND_NEXT_FILE, 0, 0, sizeof(*response), (uint8_t*)response))
    {
        write_log(1, "Unable to find next file\n");
        return 1;
    }

    response->id = be32toh(response->id);
    response->file_size = be32toh(response->file_size);
    response->end_of_list = be32toh(response->end_of_list);

    return 0;
}

int end_gps_update(libusb_device_handle *device)
{
    RXRebootWatchPacket response = {0};
    if (send_packet(device, MSG_UNKNOWN_1D, 0, 0, 255, (uint8_t*)&response))
    {
        write_log(1, "Unable to reset watch\n");
        return 1;
    }

    return 0;
}

int reset_watch(libusb_device_handle *device)
{
    /* this message has no reply */
    send_packet(device, MSG_RESET_DEVICE, 0, 0, 0, 0);
    return 0;
}

int get_watch_time(libusb_device_handle *device, time_t *time)
{
    RXGetCurrentTimePacket response = {0};
    if (send_packet(device, MSG_GET_CURRENT_TIME, 0, 0, sizeof(response), (uint8_t*)&response))
    {
        write_log(1, "Unable to get watch time\n");
        return 1;
    }

    *time = be32toh(response.utc_time);
    return 0;
}

uint8_t *read_whole_file(libusb_device_handle *device, uint32_t id, uint32_t *length)
{
    uint8_t *data = 0, *ptr;
    uint32_t size = 0;
    if (open_file(device, id, 0))
        return 0;
    size = get_file_size(device, id);
    if (length)
        *length = size;
    if (size > 0)
    {
        ptr = data = malloc(size);
        while (size > 0)
        {
            int len = (size > 0x32) ? 0x32 : size;
            if (get_file_data(device, id, ptr, len))
            {
                free(data);
                data = 0;
                break;
            }
            ptr  += len;
            size -= len;
        }
    }
    close_file(device, id);
    return data;
}

int write_whole_file(libusb_device_handle *device, uint32_t id, uint8_t *data, uint32_t size)
{
    /* check to see if the file can be read */
    if (!open_file(device, id, 0))
    {
        close_file(device, id);
        /* the file exists, so delete it */
        if (delete_file(device, id))
            return 1;
    };
    /* open the file for writing */
    if (open_file(device, id, 1))
        return 1;
    while (size > 0)
    {
        int len = (size > 0x36) ? 0x36 : size;
        if (write_file_data(device, id, data, len))
            break;
        size -= len;
        data += len;
    }
    return close_file(device, id);
}

int write_verify_whole_file(libusb_device_handle *device, uint32_t id, uint8_t *data, uint32_t size)
{
    uint32_t read_size;
    uint8_t *read_data;
    if (write_whole_file(device, id, data, size))
        return 1;
    read_data = read_whole_file(device, id, &read_size);
    if (!read_data || (read_size != size))
        return 2;
    if (memcmp(data, read_data, size))
        return 3;
}

int get_watch_name(libusb_device_handle *device, char *name, size_t max_length)
{
    uint32_t size;
    uint8_t *data;
    char *start, *end;
    
    data = read_whole_file(device, FILE_PREFERENCES_XML, &size);
    if (!data)
        return 1;

    start = strstr(data, "<watchName>");
    if (!start)
    {
        free(data);
        return 2;
    }

    start += 11;
    end = strstr(start, "</watchName>");
    if (!end)
    {
        free(data);
        return 3;
    }

    if ((end - start) >= max_length)
    {
        free(data);
        return 4;
    }

    memcpy(name, start, end - start);
    name[end - start] = 0;

    free(data);
    return 0;
}

int get_serial_number(libusb_device_handle *device, char *serial, int length)
{
    struct libusb_device_descriptor desc;
    int count;

    /* get the device descriptor */
    libusb_get_device_descriptor(libusb_get_device(device), &desc);

    /* open the device so we can read the serial number */
    count = libusb_get_string_descriptor_ascii(device, desc.iSerialNumber, serial, length);
    if (count < length)
        serial[count] = 0;

    return 0;
}

uint32_t get_product_id(libusb_device_handle *device)
{
    RXGetProductIDPacket response = {0};
    if (send_packet(device, MSG_GET_PRODUCT_ID, 0, 0, sizeof(response), (uint8_t*)&response))
    {
        return 0;
    }

    return be32toh(response.product_id);
}

uint32_t get_firmware_version(libusb_device_handle *device)
{
    unsigned major, minor, build;

    RXGetFirmwareVersionPacket response = {0};
    if (send_packet(device, MSG_GET_FIRMWARE_VERSION, 0, 0, 64, (uint8_t*)&response))
        return 0;

    if (sscanf(response.version, "%u.%u.%u", &major, &minor, &build) != 3)
        return 0;

    return (major << 16) | (minor << 8) | build;
}

int send_message_group_1(libusb_device_handle *device)
{
    /* no idea what this group does, but it is sent at specific times */
    if (send_packet(device, MSG_UNKNOWN_1A, 0, 0, 4, 0))
        return 1;
    if (send_packet(device, MSG_UNKNOWN_0A, 0, 0, 0, 0))
        return 1;
    if (send_packet(device, MSG_UNKNOWN_23, 0, 0, 3, 0))
        return 1;
    if (send_packet(device, MSG_UNKNOWN_23, 0, 0, 3, 0))
        return 1;
}

int send_startup_message_group(libusb_device_handle *device)
{
    if (send_packet(device, MSG_UNKNOWN_0D, 0, 0, 20, 0))
        return 1;
    if (send_packet(device, MSG_UNKNOWN_0A, 0, 0, 0, 0))
        return 1;
    if (send_packet(device, MSG_UNKNOWN_22, 0, 0, 1, 0))
        return 1;
    if (send_packet(device, MSG_UNKNOWN_22, 0, 0, 1, 0))
        return 1;
    if (send_packet(device, MSG_GET_PRODUCT_ID, 0, 0, 4, 0))
        return 1;
    if (send_packet(device, MSG_UNKNOWN_0A, 0, 0, 0, 0))
        return 1;
    if (send_packet(device, MSG_GET_BLE_VERSION, 0, 0, 4, 0))
        return 1;
    if (send_packet(device, MSG_GET_BLE_VERSION, 0, 0, 4, 0))
        return 1;
    if (send_packet(device, MSG_UNKNOWN_1F, 0, 0, 4, 0))
        return 1;
}

uint32_t get_ble_version(libusb_device_handle *device)
{
    RXGetBLEVersionPacket response;
    if (send_packet(device, MSG_GET_BLE_VERSION, 0, 0, sizeof(response), (uint8_t*)&response))
        return 0;

    return be32toh(response.ble_version);
}

