//------------------------------------------------------------------------------
// libttwatch.cpp
// implementation file for the TomTom watch methods
//------------------------------------------------------------------------------

#include "libttwatch.h"

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"

#include <string>
#include <vector>

//------------------------------------------------------------------------------
// macros

#define TOMTOM_VENDOR_ID    (0x1390)
#define TOMTOM_PRODUCT_ID   (0x7474)

#define RETURN_ERROR(err)               \
    do                                  \
    {                                   \
        int result = (err);             \
        if (result != TTWATCH_NoError)  \
            return result;              \
    } while (0)                         \

#define foreach(var, container) for (__decltype(container)::iterator var = container.begin(); var != container.end(); ++var)

// message IDs
#define MSG_OPEN_FILE_WRITE         (0x02)
#define MSG_DELETE_FILE             (0x03)
#define MSG_WRITE_FILE_DATA         (0x04)
#define MSG_GET_FILE_SIZE           (0x05)
#define MSG_OPEN_FILE_READ          (0x06)
#define MSG_READ_FILE_DATA_REQUEST  (0x07)
#define MSG_READ_FILE_DATA_RESPONSE (0x09)
#define MSG_FIND_CLOSE              (0x0a)
#define MSG_CLOSE_FILE              (0x0c)
#define MSG_UNKNOWN_0D              (0x0d)
#define MSG_RESET_DEVICE            (0x10)  /* issued after updating firmware,
                                               causes USB disconnect and reconnect
                                               after approximately 90 seconds */
#define MSG_FIND_FIRST_FILE         (0x11)
#define MSG_FIND_NEXT_FILE          (0x12)
#define MSG_GET_CURRENT_TIME        (0x14)
#define MSG_UNKNOWN_1A              (0x1a)
#define MSG_RESET_GPS_PROCESSOR     (0x1d)
#define MSG_UNKNOWN_1F              (0x1f)
#define MSG_GET_PRODUCT_ID          (0x20)
#define MSG_GET_FIRMWARE_VERSION    (0x21)
#define MSG_UNKNOWN_22              (0x22)
#define MSG_UNKNOWN_23              (0x23)
#define MSG_GET_BLE_VERSION         (0x28)

// interesting information:
// 1. messages 0x1a, 0x0a, 0x23, 0x23 are always sent after:
//    - file list is read
//    - main firmware is written
//    - gps firmware is written
// 2. message group 0x0d, 0x0a, 0x22, 0x22, 0x20, 0x0a, 0x28, 0x28, 0x1f, 0x21
//    is always sent at startup

typedef struct
{
    uint8_t direction;  // 0x09 = transmit, 0x01 = receive
    uint8_t length;     // of remaining packet (excluding direction and length)
    uint8_t counter;
    uint8_t message_id;
} PacketHeader;

// TX packets
typedef struct
{
    uint32_t _unk[2];
} TXFindFirstFilePacket;

typedef struct
{
    uint32_t id;
} TXFileOperationPacket;

typedef struct
{
    uint32_t id;
    uint32_t length;
} TXReadFileDataPacket;

typedef struct
{
    uint32_t id;
    uint8_t data[54];
} TXWriteFileDataPacket;

// RX packets
typedef struct
{
    uint32_t _unk1;
    uint32_t id;
    uint32_t _unk2;
    uint32_t file_size;
    uint32_t end_of_list;
} RXFindFilePacket;

typedef struct
{
    uint32_t _unk1;
} RXFindClosePacket;

typedef struct
{
    uint32_t _unk1;
    uint32_t id;
    uint32_t _unk2[2];
    uint32_t error;
} RXFileOperationPacket;

typedef struct
{
    uint32_t _unk1;
    uint32_t id;
    uint32_t _unk2;
    uint32_t file_size;
    uint32_t _unk3;
} RXGetFileSizePacket;

typedef struct
{
    uint32_t id;
    uint32_t data_length;
    uint8_t  data[50];
} RXReadFileDataPacket;

typedef struct
{
    uint32_t _unk1;
    uint32_t id;
    uint32_t _unk2[3];
} RXWriteFileDataPacket;

typedef struct
{
    uint32_t utc_time;
    uint32_t _unk[4];
} RXGetCurrentTimePacket;

typedef struct
{
    char version[60];
} RXGetFirmwareVersionPacket;

typedef struct
{
    uint32_t product_id;
} RXGetProductIDPacket;

typedef struct
{
    uint32_t ble_version;
} RXGetBLEVersionPacket;

typedef struct
{
    char message[60];
} RXRebootWatchPacket;

typedef std::vector<std::pair<uint32_t,uint32_t> >  FileList;

//------------------------------------------------------------------------------
// variables

static int s_show_packets;

//------------------------------------------------------------------------------
void print_packet(uint8_t *packet, uint8_t size)
{
    int i;
    if (s_show_packets)
    {
        for (i = 0; i < size; ++i)
            printf("%02X ", packet[i]);
        printf("\n");
    }
}

//------------------------------------------------------------------------------
int send_packet(TTWATCH *watch, uint8_t msg, uint8_t tx_length,
    const uint8_t *tx_data, uint8_t rx_length, uint8_t *rx_data)
{
    static uint8_t message_counter = 0;
    uint8_t packet[64] = {0};
    int count  = 0;
    int result = 0;

    // create the tx packet
    packet[0] = 0x09;
    packet[1] = tx_length + 2;
    packet[2] = message_counter++;
    packet[3] = msg;
    memcpy(packet + 4, tx_data, tx_length);

    print_packet(packet, tx_length + 4);

    // send the packet
    result = libusb_interrupt_transfer(watch->device, 0x05, packet, tx_length + 4, &count, 10000);
    if (result || (count != tx_length + 4))
        return TTWATCH_UnableToSendPacket;

    // read the reply
    result = libusb_interrupt_transfer(watch->device, 0x84, packet, 64, &count, 10000);
    if (result)
        return TTWATCH_UnableToReceivePacket;

    print_packet(packet, packet[1] + 2);

    // check that the reply is valid
    if (packet[0] != 0x01)
        return TTWATCH_InvalidResponse;
    if ((rx_length < 60) && (packet[1] != (rx_length + 2)))
        return TTWATCH_IncorrectResponseLength;
    if (packet[2] != (uint8_t)(message_counter - 1))
        return TTWATCH_OutOfSyncResponse;
    if (msg == MSG_READ_FILE_DATA_REQUEST)
    {
        if (packet[3] != MSG_READ_FILE_DATA_RESPONSE)
            return TTWATCH_UnexpectedResponse;
    }
    else
    {
        if (packet[3] != msg)
            return TTWATCH_UnexpectedResponse;
    }

    // copy the back data to the caller
    if (rx_data)
        memcpy(rx_data, packet + 4, packet[1] - 2);
    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int enum_files(TTWATCH *watch, uint32_t type, FileList &files)
{
    uint32_t id, length;
    int result;
    RETURN_ERROR(ttwatch_find_first_file(watch, &id, &length));
    do
    {
        if ((type == 0) || ((id & TTWATCH_FILE_TYPE_MASK) == type))
            files.push_back(std::pair<uint32_t,uint32_t>(id, length));
    }
    while ((result = ttwatch_find_next_file(watch, &id, &length)) == TTWATCH_NoError);
    RETURN_ERROR(ttwatch_find_close(watch));
    return (result == TTWATCH_NoMoreFiles) ? TTWATCH_NoError : result;
}

extern "C"
{

//------------------------------------------------------------------------------
// device functions
void ttwatch_enumerate_devices(TTWATCH_DEVICE_ENUMERATOR enumerator, void *data)
{
    libusb_device **list = 0;

    ssize_t count = libusb_get_device_list(NULL, &list);
    for (ssize_t i = 0; i < count; ++i)
    {
        TTWATCH *watch;
        if (ttwatch_open_device(list[i], 0, &watch) == TTWATCH_NoError)
        {
            if (enumerator(watch, data))
                ttwatch_close(watch);
            else
                break;
        }
    }

    libusb_free_device_list(list, 1);
}

//------------------------------------------------------------------------------
int ttwatch_open(const char *serial_or_name, TTWATCH **watch)
{
    libusb_device **list = 0;

    ssize_t count = libusb_get_device_list(NULL, &list);
    for (ssize_t i = 0; i < count; ++i)
    {
        if (ttwatch_open_device(list[i], serial_or_name, watch) == TTWATCH_NoError)
            break;
    }

    libusb_free_device_list(list, 1);
    return *watch ? TTWATCH_NoError : TTWATCH_NoMatchingWatch;
}

//------------------------------------------------------------------------------
int ttwatch_open_device(libusb_device *device, const char *serial_or_name, TTWATCH **watch)
{
    struct libusb_device_descriptor desc;
    int result;
    libusb_device_handle *handle;
    char serial[64];
    char name[64];
    int count;
    int attempts = 0;

    // get the device descriptor
    libusb_get_device_descriptor(device, &desc);

    // ignore any non-TomTom devices
    // PID 0x7474 is Multisport and Multisport Cardio
    if ((desc.idVendor  != TOMTOM_VENDOR_ID) ||
        (desc.idProduct != TOMTOM_PRODUCT_ID))
    {
        *watch = 0;
        return TTWATCH_NotAWatch;
    }

    // open the device so we can read the serial number
    if (libusb_open(device, &handle))
    {
        *watch = 0;
        return TTWATCH_UnableToOpenDevice;
    }

    *watch = (TTWATCH*)malloc(sizeof(TTWATCH));
    memset(*watch, 0, sizeof(TTWATCH));
    (*watch)->device = handle;

    // Claim the device interface. If the device is busy (such as opened
    // by a daemon), wait up to 60 seconds for it to become available
    while (attempts++ < 60)
    {
        // detach the kernel HID driver, otherwise we can't do anything
        if (libusb_kernel_driver_active(handle, 0))
        {
            if (!libusb_detach_kernel_driver(handle, 0))
                (*watch)->attach_kernel_driver = 1;
        }
        if ((result = libusb_claim_interface(handle, 0)) != 0)
        {
            if (result == LIBUSB_ERROR_BUSY)
                sleep(1);
            else
            {
                ttwatch_close(*watch);
                *watch = 0;
                return TTWATCH_UnableToOpenDevice;
            }
        }
        else
            break;
    }
    // if we have finished the attempts and it's still busy, abort
    if (result)
    {
        ttwatch_close(*watch);
        *watch = 0;
        return TTWATCH_UnableToOpenDevice;
    }

    // get the watch serial number
    count = libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber,
        (unsigned char*)(*watch)->serial_number, sizeof((*watch)->serial_number));
    if (count > 0)
        ((char*)(*watch)->serial_number)[count] = 0;

    RETURN_ERROR(ttwatch_send_startup_message_group(*watch));

    // get the watch name
    if (ttwatch_get_watch_name(*watch, name, sizeof(name)) != TTWATCH_NoError)
        name[0] = 0;

    ttwatch_get_product_id(*watch, &(*watch)->product_id);
    ttwatch_get_firmware_version(*watch, &(*watch)->firmware_version);
    ttwatch_get_ble_version(*watch, &(*watch)->ble_version);

    // see if we can match the device serial number, name or index
    if (!serial_or_name ||
        (strcasecmp(serial_or_name, serial) == 0) ||
        (strcasecmp(serial_or_name, name) == 0))
    {
        return TTWATCH_NoError;
    }
    else
    {
        ttwatch_close(*watch);
        *watch = 0;
        return TTWATCH_NoMatchingWatch;
    }
}

//------------------------------------------------------------------------------
int ttwatch_close(TTWATCH *watch)
{
    if (!watch)
        return TTWATCH_InvalidParameter;

    if (watch->preferences_changed)
        RETURN_ERROR(ttwatch_write_preferences(watch));
    if (watch->manifest_changed)
        RETURN_ERROR(ttwatch_write_manifest(watch));

    libusb_release_interface(watch->device, 0);

    if (watch->attach_kernel_driver)
        libusb_attach_kernel_driver(watch->device, 0);
    libusb_close(watch->device);

    if (watch->preferences_file)
        free(watch->preferences_file);
    if (watch->manifest_file)
        free(watch->manifest_file);

    free(watch);
    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
// file functions
int ttwatch_open_file(TTWATCH *watch, uint32_t id, int read, TTWATCH_FILE **file)
{
    if (!watch)
        return TTWATCH_InvalidParameter;
    if (watch->current_file)
        return TTWATCH_FileOpen;

    TXFileOperationPacket request = { htobe32(id) };
    RXFileOperationPacket response = { 0, 0, 0, 0, 0 };
    RETURN_ERROR(send_packet(watch, read ? MSG_OPEN_FILE_READ : MSG_OPEN_FILE_WRITE,
            sizeof(request), (uint8_t*)&request, sizeof(response), (uint8_t*)&response));

    if ((request.id != response.id) || (be32toh(response.error) != 0))
        return TTWATCH_InvalidResponse;

    *file = (TTWATCH_FILE*)malloc(sizeof(TTWATCH_FILE));
    (*file)->watch = watch;
    (*file)->file_id = id;

    watch->current_file = id;
    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int ttwatch_close_file(TTWATCH_FILE *file)
{
    if (!file->watch)
        return TTWATCH_InvalidParameter;
    if (!file->watch->current_file)
        return TTWATCH_FileNotOpen;

    TXFileOperationPacket request = { htobe32(file->file_id) };
    RXFileOperationPacket response = { 0, 0, 0, 0, 0 };
    RETURN_ERROR(send_packet(file->watch, MSG_CLOSE_FILE, sizeof(request),
        (uint8_t*)&request, sizeof(response), (uint8_t*)&response));

    file->watch->current_file = 0;
    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int ttwatch_delete_file(TTWATCH *watch, uint32_t id)
{
    if (!watch)
        return TTWATCH_InvalidParameter;
    if (watch->current_file)
        return TTWATCH_FileOpen;

    TXFileOperationPacket request = { htobe32(id) };
    RXFileOperationPacket response = { 0, 0, 0, 0, 0 };
    return send_packet(watch, MSG_DELETE_FILE, sizeof(request),
        (uint8_t*)&request, sizeof(response), (uint8_t*)&response);
}

//------------------------------------------------------------------------------
int ttwatch_get_file_size(TTWATCH_FILE *file, uint32_t *size)
{
    if (!file)
        return TTWATCH_InvalidParameter;
    if (!file->watch->current_file)
        return TTWATCH_FileNotOpen;

    TXFileOperationPacket request = { htobe32(file->file_id) };
    RXGetFileSizePacket response = { 0, 0, 0, 0, 0 };
    RETURN_ERROR(send_packet(file->watch, MSG_GET_FILE_SIZE, sizeof(request),
        (uint8_t*)&request, sizeof(response), (uint8_t*)&response));

    *size = be32toh(response.file_size);
    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int ttwatch_read_file_data(TTWATCH_FILE *file, void *data, uint32_t length)
{
    if (!file)
        return TTWATCH_InvalidParameter;
    if (!file->watch->current_file)
        return TTWATCH_FileNotOpen;

    TXReadFileDataPacket request = { htobe32(file->file_id), htobe32(length) };
    RXReadFileDataPacket response = { 0, 0, 0 };
    RETURN_ERROR(send_packet(file->watch, MSG_READ_FILE_DATA_REQUEST, sizeof(request),
        (uint8_t*)&request, length + 8, (uint8_t*)&response));

    if (request.id != response.id)
        return TTWATCH_InvalidResponse;

    memcpy(data, response.data, length);
    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int ttwatch_write_file_data(TTWATCH_FILE *file, const void *data, uint32_t length)
{
    if (!file)
        return TTWATCH_InvalidParameter;
    if (!file->watch->current_file)
        return TTWATCH_FileNotOpen;

    TXWriteFileDataPacket request = { htobe32(file->file_id), 0 };
    RXWriteFileDataPacket response = { 0, 0, 0 };
    memcpy(request.data, data, length);
    RETURN_ERROR(send_packet(file->watch, MSG_WRITE_FILE_DATA, length + 4,
        (uint8_t*)&request, sizeof(response), (uint8_t*)&response));

    return (request.id != response.id) ? TTWATCH_InvalidResponse : TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int ttwatch_find_first_file(TTWATCH *watch, uint32_t *file_id, uint32_t *length)
{
    if (!watch)
        return TTWATCH_InvalidParameter;
    if (watch->current_file)
        return TTWATCH_FileOpen;

    TXFindFirstFilePacket request = {0};
    RXFindFilePacket response = { 0, 0, 0, 0, 0 };
    RETURN_ERROR(send_packet(watch, MSG_FIND_FIRST_FILE, sizeof(request),
        (uint8_t*)&request, sizeof(response), (uint8_t*)&response));

    if (*file_id) *file_id = be32toh(response.id);
    if (*length) *length   = be32toh(response.file_size);

    return be32toh(response.end_of_list) ? TTWATCH_NoMoreFiles : TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int ttwatch_find_next_file(TTWATCH *watch, uint32_t *file_id, uint32_t *length)
{
    if (!watch)
        return TTWATCH_InvalidParameter;
    if (watch->current_file)
        return TTWATCH_FileOpen;

    RXFindFilePacket response =  { 0, 0, 0, 0, 0 };
    RETURN_ERROR(send_packet(watch, MSG_FIND_NEXT_FILE, 0, 0,
        sizeof(response), (uint8_t*)&response));

    if (*file_id) *file_id = be32toh(response.id);
    if (*length) *length   = be32toh(response.file_size);

    return be32toh(response.end_of_list) ? TTWATCH_NoMoreFiles : TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int ttwatch_find_close(TTWATCH *watch)
{
    if (!watch)
        return TTWATCH_InvalidParameter;
    if (watch->current_file)
        return TTWATCH_FileOpen;

    return ttwatch_send_message_group_1(watch);
}

//------------------------------------------------------------------------------
int ttwatch_enumerate_files(TTWATCH *watch, uint32_t type, TTWATCH_FILE_ENUMERATOR enumerator, void *data)
{
    FileList files;
    RETURN_ERROR(enum_files(watch, type, files));

    foreach (it, files)
        enumerator(it->first, it->second, data);
    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int ttwatch_read_whole_file(TTWATCH *watch, uint32_t id, void **data, uint32_t *length)
{
    uint8_t *ptr;
    uint32_t size;
    TTWATCH_FILE *file;
    int result = TTWATCH_NoData;

    RETURN_ERROR(ttwatch_open_file(watch, id, true, &file));
    RETURN_ERROR(ttwatch_get_file_size(file, &size));

    if (length)
        *length = size;
    if (size > 0)
    {
        *data = malloc(size);
        ptr = (uint8_t*)*data;
        while (size > 0)
        {
            int len = (size > 50) ? 50 : size;
            if ((result = ttwatch_read_file_data(file, ptr, len)) != TTWATCH_NoError)
            {
                free(*data);
                *data = 0;
                break;
            }
            ptr  += len;
            size -= len;
        }
    }
    RETURN_ERROR(ttwatch_close_file(file));
    return result;
}

//------------------------------------------------------------------------------
int ttwatch_write_whole_file(TTWATCH *watch, uint32_t id, const void *data, uint32_t length)
{
    TTWATCH_FILE *file;
    if (ttwatch_open_file(watch, id, true, &file) == TTWATCH_NoError)
    {
        RETURN_ERROR(ttwatch_close_file(file));
        RETURN_ERROR(ttwatch_delete_file(watch, id));
    }

    RETURN_ERROR(ttwatch_open_file(watch, id, false, &file));

    uint8_t *ptr = (uint8_t*)data;
    int result = TTWATCH_NoError;
    while (length > 0)
    {
        int len = (length > 54) ? 54 : length;
        if ((result = ttwatch_write_file_data(file, ptr, len)) != TTWATCH_NoError)
            break;
        length -= len;
        ptr += len;
    }
    RETURN_ERROR(ttwatch_close_file(file));
    return result;
}

//------------------------------------------------------------------------------
int ttwatch_write_verify_whole_file(TTWATCH *watch, uint32_t id, const void *data, uint32_t length)
{
    uint32_t read_size;
    void *read_data;
    RETURN_ERROR(ttwatch_write_whole_file(watch, id, data, length));
    RETURN_ERROR(ttwatch_read_whole_file(watch, id, &read_data, &read_size));
    if ((read_size != length) || memcmp(data, read_data, length))
        return TTWATCH_VerifyError;
    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
// general functions
int ttwatch_reset_gps_processor(TTWATCH *watch)
{
    if (!watch)
        return TTWATCH_InvalidParameter;
    if (watch->current_file)
        return TTWATCH_FileOpen;

    RXRebootWatchPacket response = {0};
    return send_packet(watch, MSG_RESET_GPS_PROCESSOR, 0, 0, 60, (uint8_t*)&response);
}

//------------------------------------------------------------------------------
int ttwatch_reset_watch(TTWATCH *watch)
{
    if (!watch)
        return TTWATCH_InvalidParameter;
    if (watch->current_file)
        return TTWATCH_FileOpen;

    // this message has no reply
    send_packet(watch, MSG_RESET_DEVICE, 0, 0, 0, 0);
    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int ttwatch_get_watch_time(TTWATCH *watch, time_t *time)
{
    if (!watch)
        return TTWATCH_InvalidParameter;
    if (watch->current_file)
        return TTWATCH_FileOpen;

    RXGetCurrentTimePacket response = { 0, 0 };
    RETURN_ERROR(send_packet(watch, MSG_GET_CURRENT_TIME, 0, 0, sizeof(response), (uint8_t*)&response));

    *time = be32toh(response.utc_time);
    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int ttwatch_get_serial_number(TTWATCH *watch, char *serial, size_t max_length)
{
    if (!watch)
        return TTWATCH_InvalidParameter;
    if (watch->current_file)
        return TTWATCH_FileOpen;

    libusb_device_descriptor desc;

    libusb_get_device_descriptor(libusb_get_device(watch->device), &desc);

    size_t count = libusb_get_string_descriptor_ascii(watch->device, desc.iSerialNumber, (unsigned char*)serial, max_length);
    if (count < max_length)
        serial[count] = 0;

    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int ttwatch_get_product_id(TTWATCH *watch, uint32_t *product_id)
{
    if (!watch)
        return TTWATCH_InvalidParameter;
    if (watch->current_file)
        return TTWATCH_FileOpen;

    RXGetProductIDPacket response = {0};
    RETURN_ERROR(send_packet(watch, MSG_GET_PRODUCT_ID, 0, 0, sizeof(response), (uint8_t*)&response));

    *product_id = be32toh(response.product_id);
    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int ttwatch_get_firmware_version(TTWATCH *watch, uint32_t *firmware_version)
{
    if (!watch)
        return TTWATCH_InvalidParameter;
    if (watch->current_file)
        return TTWATCH_FileOpen;

    RXGetFirmwareVersionPacket response = {0};
    RETURN_ERROR(send_packet(watch, MSG_GET_FIRMWARE_VERSION, 0, 0, 64, (uint8_t*)&response));

    unsigned major, minor, build;
    if (sscanf(response.version, "%u.%u.%u", &major, &minor, &build) != 3)
        return TTWATCH_ParseError;

    if (firmware_version)
        *firmware_version = (major << 16) | (minor << 8) | build;
    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int ttwatch_get_ble_version(TTWATCH *watch, uint32_t *ble_version)
{
    if (!watch)
        return TTWATCH_InvalidParameter;
    if (watch->current_file)
        return TTWATCH_FileOpen;

    RXGetBLEVersionPacket response;
    RETURN_ERROR(send_packet(watch, MSG_GET_BLE_VERSION, 0, 0, sizeof(response), (uint8_t*)&response));

    if (ble_version)
        *ble_version = be32toh(response.ble_version);
    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int ttwatch_send_message_group_1(TTWATCH *watch)
{
    if (!watch)
        return TTWATCH_InvalidParameter;
    if (watch->current_file)
        return TTWATCH_FileOpen;

    RETURN_ERROR(send_packet(watch, MSG_UNKNOWN_1A, 0, 0, 4, 0));
    RETURN_ERROR(send_packet(watch, MSG_FIND_CLOSE, 0, 0, 0, 0));
    RETURN_ERROR(send_packet(watch, MSG_UNKNOWN_23, 0, 0, 3, 0));
    RETURN_ERROR(send_packet(watch, MSG_UNKNOWN_23, 0, 0, 3, 0));
    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int ttwatch_send_startup_message_group(TTWATCH *watch)
{
    if (!watch)
        return TTWATCH_InvalidParameter;
    if (watch->current_file)
        return TTWATCH_FileOpen;

    RETURN_ERROR(send_packet(watch, MSG_UNKNOWN_0D, 0, 0, 20, 0));
    RETURN_ERROR(send_packet(watch, MSG_FIND_CLOSE, 0, 0, 0, 0));
    RETURN_ERROR(send_packet(watch, MSG_UNKNOWN_22, 0, 0, 1, 0));
    RETURN_ERROR(send_packet(watch, MSG_UNKNOWN_22, 0, 0, 1, 0));
    RETURN_ERROR(send_packet(watch, MSG_GET_PRODUCT_ID, 0, 0, 4, 0));
    RETURN_ERROR(send_packet(watch, MSG_FIND_CLOSE, 0, 0, 0, 0));
    RETURN_ERROR(send_packet(watch, MSG_GET_BLE_VERSION, 0, 0, 4, 0));
    RETURN_ERROR(send_packet(watch, MSG_GET_BLE_VERSION, 0, 0, 4, 0));
    RETURN_ERROR(send_packet(watch, MSG_UNKNOWN_1F, 0, 0, 4, 0));
    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int ttwatch_clear_data(TTWATCH *watch)
{
    if (!watch)
        return TTWATCH_InvalidParameter;
    if (watch->current_file)
        return TTWATCH_FileOpen;

    FileList files;
    enum_files(watch, 0, files);

    foreach (it, files)
    {
        uint32_t length;
        TTWATCH_HISTORY_FILE *history;

        switch (it->first & TTWATCH_FILE_TYPE_MASK)
        {
        case TTWATCH_FILE_TTBIN_DATA:
        case TTWATCH_FILE_RACE_HISTORY_DATA:
        case TTWATCH_FILE_HISTORY_DATA:
            RETURN_ERROR(ttwatch_delete_file(watch, it->first));
            break;

        case TTWATCH_FILE_HISTORY_SUMMARY:
            RETURN_ERROR(ttwatch_read_whole_file(watch, it->first, (void**)&history, &length));
            {
                history->entry_count = 0;
                length = sizeof(TTWATCH_HISTORY_FILE) - 1;
                int res = ttwatch_write_verify_whole_file(watch, it->first, history, length);
                free(history);
                RETURN_ERROR(res);
            }
            break;
        }
    }
    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
// preferences file functions
int ttwatch_reload_preferences(TTWATCH *watch)
{
    void *data;
    uint32_t length;
    RETURN_ERROR(ttwatch_read_whole_file(watch, TTWATCH_FILE_PREFERENCES_XML, &data, &length));

    if (watch->preferences_file)
        free(watch->preferences_file);

    watch->preferences_file         = (char*)realloc(data, length + 1);
    watch->preferences_file[length] = 0;
    watch->preferences_changed      = false;

    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int ttwatch_write_preferences(TTWATCH *watch)
{
    RETURN_ERROR(ttwatch_write_whole_file(watch, TTWATCH_FILE_PREFERENCES_XML,
        (uint8_t*)watch->preferences_file, strlen(watch->preferences_file)));
    watch->preferences_changed = false;
    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int ttwatch_update_preferences_modified_time(TTWATCH *watch)
{
    static const char *const DAYNAMES[]   = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *const MONTHNAMES[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    char timestr[64];
    time_t rawtime;
    struct tm *timeinfo;

    if (!watch)
        return TTWATCH_InvalidParameter;

    std::string file = watch->preferences_file;

    size_t start = file.find("modified=\"");
    if (start == std::string::npos)
        return TTWATCH_ParseError;
    start += 10;

    size_t end = file.find('\"', start);
    if (end == std::string::npos)
        return TTWATCH_ParseError;

    time(&rawtime);
    timeinfo = localtime(&rawtime);
    sprintf(timestr, "%s %d. %s %02d:%02d:%02d %04d",
         DAYNAMES[timeinfo->tm_wday], timeinfo->tm_mday,
         MONTHNAMES[timeinfo->tm_mon], timeinfo->tm_hour,
         timeinfo->tm_min, timeinfo->tm_sec, timeinfo->tm_year + 1900);

    file.replace(start, end - start, timestr);

    free(watch->preferences_file);
    watch->preferences_file = strdup(file.c_str());
    watch->preferences_changed = true;

    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int ttwatch_get_watch_name(TTWATCH *watch, char *name, size_t max_length)
{
    if (!watch)
        return TTWATCH_InvalidParameter;

    if (!watch->preferences_file)
        RETURN_ERROR(ttwatch_reload_preferences(watch));

    std::string file = watch->preferences_file;

    size_t start = file.find("<watchName>");
    if (start == std::string::npos)
    {
        name[0] = 0;
        return TTWATCH_NoError;
    }
    start += 11;

    size_t end = file.find("</watchName>", start);
    if (end == std::string::npos)
        return TTWATCH_ParseError;

    std::string str = file.substr(start, end - start);

    strncpy(name, str.c_str(), max_length);
    name[max_length - 1] = 0;

    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int ttwatch_set_watch_name(TTWATCH *watch, const char *name)
{
    if (!watch)
        return TTWATCH_InvalidParameter;

    if (!watch->preferences_file)
        RETURN_ERROR(ttwatch_reload_preferences(watch));

    std::string file = watch->preferences_file;

    size_t start = file.find("<watchName>");
    if (start == std::string::npos)
    {
        start = file.find("<preferences");
        if (start == std::string::npos)
            return TTWATCH_ParseError;
        start = file.find('>', start);
        if (start == std::string::npos)
            return TTWATCH_ParseError;
        file.insert(start + 1, "<watchName>" + std::string(name) + "</watchName>");
    }
    else
    {
        start += 11;

        size_t end = file.find("</watchName>", start);
        if (end == std::string::npos)
            return TTWATCH_ParseError;

        file.replace(start, end - start, name);
    }

    free(watch->preferences_file);
    watch->preferences_file = strdup(file.c_str());
    watch->preferences_changed = true;

    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int ttwatch_enumerate_offline_formats(TTWATCH *watch, TTWATCH_FORMAT_ENUMERATOR enumerator, void *data)
{
    if (!watch || !enumerator)
        return TTWATCH_InvalidParameter;

    if (!watch->preferences_file)
        RETURN_ERROR(ttwatch_reload_preferences(watch));

    std::string file = watch->preferences_file;

    size_t start = file.find("<exporters>");
    if (start == std::string::npos)
        return TTWATCH_NoError;
    start += 11;
    size_t end = file.find("</exporters>");
    if (end == std::string::npos)
        return TTWATCH_ParseError;

    start = file.find("<offline>", start);
    if (start == std::string::npos)
        return TTWATCH_NoError;
    start += 9;
    end = file.find("</offline>", start);
    if (end == std::string::npos)
        return TTWATCH_ParseError;

    file = file.substr(start, end - start);
    start = end = 0;

    do
    {
        start = file.find("<export id=\"", end);
        if (start == std::string::npos)
            break;
        start += 12;
        end = file.find('\"', start);
        if (end == std::string::npos)
            return TTWATCH_ParseError;

        std::string id = file.substr(start, end - start);

        start = file.find("autoOpen=\"", end);
        if (start == std::string::npos)
            return TTWATCH_ParseError;
        end = file.find('\"', start);
        if (end == std::string::npos)
            return TTWATCH_ParseError;

        bool auto_open = file.substr(start, end - start) == "1";

        enumerator(id.c_str(), auto_open ? 1 : 0, data);
    }
    while (true);

    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int ttwatch_add_offline_format(TTWATCH *watch, const char *format, int auto_open)
{
    if (!watch)
        return TTWATCH_InvalidParameter;

    if (!watch->preferences_file)
        RETURN_ERROR(ttwatch_reload_preferences(watch));

    std::string file = watch->preferences_file;

    size_t start = file.find("<exporters>");
    size_t end;
    if (start == std::string::npos)
    {
        start = file.find("</preferences>");
        if (start == std::string::npos)
            return TTWATCH_ParseError;
        file.insert(start, "<exporters></exporters>");
        start = end = start + 11;
    }
    else
    {
        start += 11;
        end = file.find("</exporters>");
        if (end == std::string::npos)
            return TTWATCH_ParseError;
    }

    start = file.find("<offline>", start);
    if (start == std::string::npos)
    {
        file.insert(end, "<offline></offline>");
        start = end = end + 9;
    }
    else
    {
        start += 9;
        end = file.find("</offline>", start);
        if (end == std::string::npos)
            return TTWATCH_ParseError;
    }

    std::string str = "<export id=\"" + std::string(format) + "\"";

    start = file.find(str, start);
    if (start == std::string::npos)
    {
        str += std::string(" autoOpen=\"") + (auto_open ? "1\"/>" : "0\"/>");
        file.insert(end, str);
    }
    else
    {
        start = file.find("autoOpen=\"", start);
        if (start == std::string::npos)
            return TTWATCH_ParseError;
        start += 10;
        end = file.find('\"', start);
        if (start == std::string::npos)
            return TTWATCH_ParseError;

        file.replace(start, end - start, auto_open ? "1" : "0");
    }

    free(watch->preferences_file);
    watch->preferences_file = strdup(file.c_str());
    watch->preferences_changed = true;

    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int ttwatch_remove_offline_format(TTWATCH *watch, const char *format)
{
    if (!watch)
        return TTWATCH_InvalidParameter;

    if (!watch->preferences_file)
        RETURN_ERROR(ttwatch_reload_preferences(watch));

    std::string file = watch->preferences_file;

    size_t start = file.find("<exporters>");
    size_t end;
    if (start == std::string::npos)
    {
        start = file.find("</preferences>");
        if (start == std::string::npos)
            return TTWATCH_ParseError;
        file.insert(start, "<exporters></exporters>");
        start = end = start + 11;
    }
    else
    {
        start += 11;
        end = file.find("</exporters>");
        if (end == std::string::npos)
            return TTWATCH_ParseError;
    }

    start = file.find("<offline>", start);
    if (start == std::string::npos)
    {
        file.insert(end, "<offline></offline>");
        start = end = end + 9;
    }
    else
    {
        start += 9;
        end = file.find("</offline>", start);
        if (end == std::string::npos)
            return TTWATCH_ParseError;
    }

    std::string str = "<export id=\"" + std::string(format) + "\"";

    start = file.find(str, start);
    if (start == std::string::npos)
        return TTWATCH_NoError;
    end = file.find("/>", start);
    if (end == std::string::npos)
        return TTWATCH_ParseError;
    end += 2;

    file.erase(start, end - start);

    free(watch->preferences_file);
    watch->preferences_file = strdup(file.c_str());
    watch->preferences_changed = true;

    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
// manifest file functions
int ttwatch_reload_manifest(TTWATCH *watch)
{
    if (!watch)
        return TTWATCH_InvalidParameter;

    void *data;
    uint32_t length;
    RETURN_ERROR(ttwatch_read_whole_file(watch, TTWATCH_FILE_MANIFEST1, &data, &length));

    if (watch->manifest_file)
        free(watch->manifest_file);

    watch->manifest_file = (uint8_t*)data;
    watch->manifest_changed = false;

    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int ttwatch_write_manifest(TTWATCH *watch)
{
    if (!watch)
        return TTWATCH_InvalidParameter;
    if (!watch->manifest_file)
        return TTWATCH_NoData;

    TTWATCH_MANIFEST_FILE *manifest = (TTWATCH_MANIFEST_FILE*)watch->manifest_file;

    uint32_t length = 8 + (6 * manifest->entry_count);
    RETURN_ERROR(ttwatch_write_whole_file(watch, TTWATCH_FILE_MANIFEST1, watch->manifest_file, length));
    watch->manifest_changed = false;

    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int ttwatch_get_manifest_entry_count(TTWATCH *watch, uint16_t *entry_count)
{
    if (!watch)
        return TTWATCH_InvalidParameter;

    if (!watch->manifest_file)
        RETURN_ERROR(ttwatch_reload_manifest(watch));

    TTWATCH_MANIFEST_FILE *manifest = (TTWATCH_MANIFEST_FILE*)watch->manifest_file;

    if (entry_count)
        *entry_count = manifest->entry_count;

    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int ttwatch_get_manifest_entry(TTWATCH *watch, uint16_t entry, uint32_t *data)
{
    if (!watch)
        return TTWATCH_InvalidParameter;

    if (!watch->manifest_file)
        RETURN_ERROR(ttwatch_reload_manifest(watch));

    TTWATCH_MANIFEST_FILE *manifest = (TTWATCH_MANIFEST_FILE*)watch->manifest_file;

    if (entry >= manifest->entry_count)
        return TTWATCH_InvalidParameter;

    if (data)
        *data = manifest->entries[entry].value;

    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int ttwatch_set_manifest_entry(TTWATCH *watch, uint16_t entry, const uint32_t *data)
{
    if (!watch || !data)
        return TTWATCH_InvalidParameter;

    if (!watch->manifest_file)
        RETURN_ERROR(ttwatch_reload_manifest(watch));

    TTWATCH_MANIFEST_FILE *manifest = (TTWATCH_MANIFEST_FILE*)watch->manifest_file;

    if (entry >= manifest->entry_count)
        return TTWATCH_InvalidParameter;

    manifest->entries[entry].value = *data;
    watch->manifest_changed = true;

    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
// race functions
int ttwatch_enumerate_races(TTWATCH *watch, TTWATCH_RACE_ENUMERATOR enumerator, void *data)
{
    if (!watch || !enumerator)
        return TTWATCH_InvalidParameter;

    FileList files;
    RETURN_ERROR(enum_files(watch, TTWATCH_FILE_RACE_DATA, files));

    foreach (it, files)
    {
        TTWATCH_RACE_FILE *file;
        RETURN_ERROR(ttwatch_read_whole_file(watch, it->first, (void**)&file, 0));

        enumerator((TTWATCH_ACTIVITY)((it->first >> 8) & 0xff), it->first & 0xff, file, data);

        free(file);
    }

    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int ttwatch_update_race(TTWATCH *watch, TTWATCH_ACTIVITY activity, int index,
    const char *name, uint32_t distance, uint32_t duration, int checkpoints)
{
    if (!watch)
        return TTWATCH_InvalidParameter;

    float checkpoint_distance = (float)distance / checkpoints;
    int required_length = (int)((checkpoint_distance + 254) / 255) * checkpoints;

    uint32_t id = TTWATCH_FILE_RACE_DATA | (activity << 8) | index;

    TTWATCH_RACE_FILE *race_file;
    uint32_t length;
    RETURN_ERROR(ttwatch_read_whole_file(watch, id, (void**)&race_file, &length));

    length = sizeof(TTWATCH_RACE_FILE) - 1 + required_length;
    race_file = (TTWATCH_RACE_FILE*)realloc(race_file, length);

    strncpy(race_file->name, name, sizeof(race_file->name));
    race_file->distance    = distance;
    race_file->time        = duration;
    race_file->checkpoints = checkpoints;

    uint32_t current_distance = 0;
    int j = 0;
    for (int i = 1; i <= checkpoints; ++i)
    {
        uint32_t end_distance = (distance * i + checkpoints / 2) / checkpoints;
        while (current_distance < end_distance)
        {
            if ((end_distance - current_distance) >= 255)
                race_file->distances[j] = 255;
            else
                race_file->distances[j] = end_distance - current_distance;
            current_distance += race_file->distances[j++];
        }
    }

    RETURN_ERROR(ttwatch_write_verify_whole_file(watch, id, (uint8_t*)race_file, length));

    free(race_file);
    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
// history functions
int ttwatch_enumerate_history_entries(TTWATCH *watch, TTWATCH_HISTORY_ENUMERATOR enumerator, void *data)
{
    if (!watch || !enumerator)
        return TTWATCH_InvalidParameter;

    FileList files;
    RETURN_ERROR(enum_files(watch, TTWATCH_FILE_HISTORY_SUMMARY, files));

    foreach (it, files)
    {
        uint32_t i;
        uint8_t *ptr;

        TTWATCH_HISTORY_FILE *history;
        RETURN_ERROR(ttwatch_read_whole_file(watch, it->first, (void**)&history, 0));

        ptr = history->data;
        for (i = 0; i < history->entry_count; ++i)
        {
            TTWATCH_HISTORY_ENTRY *entry = (TTWATCH_HISTORY_ENTRY*)ptr;

            enumerator((TTWATCH_ACTIVITY)entry->activity, i, entry, data);

            ptr += history->entry_length;
        }

        free(history);
    }

    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
int ttwatch_delete_history_entry(TTWATCH *watch, TTWATCH_ACTIVITY activity, int index)
{
    if (!watch)
        return TTWATCH_InvalidParameter;

    FileList files;
    RETURN_ERROR(enum_files(watch, TTWATCH_FILE_HISTORY_SUMMARY, files));

    foreach (it, files)
    {
        TTWATCH_HISTORY_FILE *history;
        uint32_t length;
        RETURN_ERROR(ttwatch_read_whole_file(watch, it->first, (void**)&history, &length));

        if (history->activity != activity)
        {
            free(history);
            continue;
        }

        if (index >= history->entry_count)
        {
            free(history);
            return TTWATCH_InvalidParameter;
        }

        if (activity != TTWATCH_Swimming)
        {
            TTWATCH_HISTORY_ENTRY *entry = (TTWATCH_HISTORY_ENTRY*)(history->data + (index * history->entry_length));
            ttwatch_delete_file(watch, TTWATCH_FILE_HISTORY_DATA | entry->file_id);
            ttwatch_delete_file(watch, TTWATCH_FILE_RACE_HISTORY_DATA | entry->file_id);
        }

        if (index != (history->entry_count - 1))
        {
            memmove(history->data + (index * history->entry_length),
                    history->data + ((index + 1) * history->entry_length),
                    (history->entry_count - index - 1) * history->entry_length);
        }

        --history->entry_count;
        length -= history->entry_length;
        int result = ttwatch_write_verify_whole_file(watch, it->first, (uint8_t*)history, length);

        free(history);
        return result;
    }

    return TTWATCH_NoError;
}

//------------------------------------------------------------------------------
// debugging functions
void ttwatch_show_packets(int show)
{
    s_show_packets = show;
}

//------------------------------------------------------------------------------
int ttwatch_get_library_version()
{
    return LIBTTWATCH_VERSION;
}

}

