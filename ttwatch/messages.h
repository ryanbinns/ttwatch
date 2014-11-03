/*****************************************************************************\
** messages.h                                                                **
** message structure definitions                                             **
\*****************************************************************************/

#include <stdint.h>

#include <libusb-1.0/libusb.h>

/* message IDs */
#define MSG_OPEN_FILE_WRITE         (0x02)
#define MSG_DELETE_FILE             (0x03)
#define MSG_WRITE_FILE_DATA         (0x04)
#define MSG_GET_FILE_SIZE           (0x05)
#define MSG_OPEN_FILE_READ          (0x06)
#define MSG_READ_FILE_DATA_REQUEST  (0x07)
#define MSG_READ_FILE_DATA_RESPONSE (0x09)
#define MSG_UNKNOWN_0A              (0x0a)  /* sent to initialise and after
                                               retrieving the file list */
#define MSG_CLOSE_FILE              (0x0c)
#define MSG_UNKNOWN_0D              (0x0d)
#define MSG_RESET_DEVICE            (0x10)  /* issued after updating firmware,
                                               causes USB disconnect and reconnect
                                               after approximately 90 seconds */
#define MSG_FIND_FIRST_FILE         (0x11)
#define MSG_FIND_NEXT_FILE          (0x12)
#define MSG_GET_CURRENT_TIME        (0x14)
#define MSG_UNKNOWN_1A              (0x1a)
#define MSG_UNKNOWN_1D              (0x1d)  /* called after the GPSQuickFix
                                               data changes, unless ttbin
                                               data is also downloaded, after
                                               that instead */
#define MSG_UNKNOWN_1F              (0x1f)
#define MSG_GET_PRODUCT_ID          (0x20)
#define MSG_GET_FIRMWARE_VERSION    (0x21)
#define MSG_UNKNOWN_22              (0x22)
#define MSG_UNKNOWN_23              (0x23)
#define MSG_GET_BLE_VERSION         (0x28)

/* interesting information:
   1. messages 0x1a, 0x0a, 0x23, 0x23 are always sent after:
      - file list is read
      - main firmware is written
      - gps firmware is written
   2. message group 0x0d, 0x0a, 0x22, 0x22, 0x20, 0x0a, 0x28, 0x28, 0x1f, 0x21
      is always sent at startup
*/

/* file IDs */
#define FILE_SYSTEM_FIRMWARE        (0x000000f0)
#define FILE_GPSQUICKFIX_DATA       (0x00010100)
#define FILE_GPS_FIRMWARE           (0x00010200)
#define FILE_FIRMWARE_UPDATE_LOG    (0x00013001)
#define FILE_MANIFEST1              (0x00850000)
#define FILE_MANIFEST2              (0x00850001)
#define FILE_PREFERENCES_XML        (0x00f20000)

#define FILE_TYPE_MASK              (0xffff0000)
#define FILE_RACE_DATA              (0x00710000)
#define FILE_RACE_HISTORY_DATA      (0x00720000)
#define FILE_HISTORY_DATA           (0x00730000)
#define FILE_HISTORY_SUMMARY        (0x00830000)
#define FILE_TTBIN_DATA             (0x00910000)

typedef struct
{
    uint8_t direction;  /* 0x09 = transmit, 0x01 = receive */
    uint8_t length;     /* of remaining packet (excluding direction and length) */
    uint8_t counter;
    uint8_t message_id;
} PacketHeader;

/* TX packets */
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

/* RX packets */
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

int send_packet(libusb_device_handle *handle, uint8_t msg, uint8_t tx_length,
    const uint8_t *tx_data, uint8_t rx_length, uint8_t *rx_data);

int open_file(libusb_device_handle *device, uint32_t id, int write);

int close_file(libusb_device_handle *device, uint32_t id);

int delete_file(libusb_device_handle *device, uint32_t id);

uint32_t get_file_size(libusb_device_handle *device, uint32_t id);

int get_file_data(libusb_device_handle *device, uint32_t id, uint8_t *data, uint32_t length);

int write_file_data(libusb_device_handle *device, uint32_t id, uint8_t *data, uint32_t length);

int find_first_file(libusb_device_handle *device, RXFindFilePacket *response);

int find_next_file(libusb_device_handle *device, RXFindFilePacket *response);

int end_gps_update(libusb_device_handle *device);

int reset_watch(libusb_device_handle *device);

int get_watch_time(libusb_device_handle *device, time_t *time);

uint8_t *read_whole_file(libusb_device_handle *device, uint32_t id, uint32_t *length);

int write_whole_file(libusb_device_handle *device, uint32_t id, uint8_t *data, uint32_t size);

int write_verify_whole_file(libusb_device_handle *device, uint32_t id, uint8_t *data, uint32_t size);

int get_watch_name(libusb_device_handle *device, char *name, size_t max_length);

int get_serial_number(libusb_device_handle *device, char *serial, int length);

uint32_t get_product_id(libusb_device_handle *device);

uint32_t get_firmware_version(libusb_device_handle *device);

int send_message_group_1(libusb_device_handle *device);

int send_startup_message_group(libusb_device_handle *device);

uint32_t get_ble_version(libusb_device_handle *device);

