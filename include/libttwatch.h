/*******************************************************************************
** libttwatch.h
**
** main header file for the ttwatch library
*******************************************************************************/
  
#ifndef __LIBTTWATCH_H__
#define __LIBTTWATCH_H__

#include <stdint.h>
#include <libusb.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LIBTTWATCH_VERSION  (0x000106)  /* version 0.1.6 */

#define TOMTOM_VENDOR_ID                (0x1390)
#define TOMTOM_MULTISPORT_PRODUCT_ID    (0x7474)
#define TOMTOM_SPARK_MUSIC_PRODUCT_ID   (0x7475)
#define TOMTOM_SPARK_CARDIO_PRODUCT_ID  (0x7477)
#define TOMTOM_TOUCH_PRODUCT_ID         (0x7480)

#define IS_SPARK(id)                            \
    (((id) == TOMTOM_SPARK_MUSIC_PRODUCT_ID) || \
     ((id) == TOMTOM_SPARK_CARDIO_PRODUCT_ID) || \
     ((id) == TOMTOM_TOUCH_PRODUCT_ID))  \


/*****************************************************************************/
typedef struct
{
    libusb_device_handle *device;
    int         attach_kernel_driver;

    uint32_t    product_id;
    uint32_t    firmware_version;
    uint32_t    ble_version;
    const char  serial_number[64];

    uint16_t    usb_product_id;

    uint32_t    current_file;

    char       *preferences_file;
    size_t      preferences_file_length;
    uint8_t    *manifest_file;
    size_t      manifest_file_length;

    int         preferences_changed;
    int         manifest_changed;
} TTWATCH;

/*****************************************************************************/
typedef struct
{
    TTWATCH *watch;
    uint32_t file_id;
} TTWATCH_FILE;

/*****************************************************************************/
typedef enum
{
    TTWATCH_NoError,
    TTWATCH_UnableToSendPacket,
    TTWATCH_UnableToReceivePacket,
    TTWATCH_InvalidResponse,
    TTWATCH_IncorrectResponseLength,
    TTWATCH_OutOfSyncResponse,
    TTWATCH_UnexpectedResponse,
    TTWATCH_NoMatchingWatch,
    TTWATCH_NotAWatch,
    TTWATCH_UnableToOpenDevice,
    TTWATCH_FileOpen,
    TTWATCH_FileNotOpen,
    TTWATCH_NoMoreFiles,
    TTWATCH_VerifyError,
    TTWATCH_ParseError,
    TTWATCH_NoData,
    TTWATCH_InvalidParameter,
} TTWATCH_ERROR;

/*****************************************************************************/
typedef enum
{    
    TTWATCH_Running = 0,    
    TTWATCH_Cycling = 1,    
    TTWATCH_Swimming = 2,    
    TTWATCH_Treadmill = 7,    
    TTWATCH_Freestyle = 8,    
    TTWATCH_Gym = 9,    
    TTWATCH_Hiking = 10,           // Adventurer
    TTWATCH_IndoorCycling = 11,    // Adventurer
    TTWATCH_TrailRunning = 14,     // Adventurer
    TTWATCH_Skiing = 15,           // Adventurer
    TTWATCH_Snowboarding = 16      // Adventurer
} TTWATCH_ACTIVITY;

/*****************************************************************************/
typedef struct __attribute__((packed))
{
    uint16_t file_type;
    uint16_t entry_count;
    struct __attribute__((packed))
    {
        uint16_t index;
        uint32_t value;
    } entries[1];
} TTWATCH_MANIFEST_FILE;

/*****************************************************************************/
typedef struct __attribute__((packed))
{
    char     name[16];
    uint32_t _unk1[5];
    uint8_t  _unk2;
    uint8_t  _unk3;
    uint32_t checkpoints;
    uint32_t time;
    uint32_t distance;
    uint8_t  distances[1];
} TTWATCH_RACE_FILE;

/*****************************************************************************/
typedef struct __attribute__((packed))
{
    uint32_t index;
    uint8_t  activity;
    uint32_t _unk1[2];
    uint32_t year;
    uint32_t month;
    uint32_t day;
    uint32_t _unk2[2];
    uint32_t hour;
    uint32_t minute;
    uint32_t second;
    union
    {
        struct  __attribute__((packed))
        {
            uint32_t _unk3[2];
            uint32_t duration;
            float    distance;
            uint32_t calories;
            uint32_t file_id;           /* does not contain valid data for swim entries */
            uint32_t swolf;             /* only exists for swim entries */
            uint32_t strokes_per_lap;   /* only exists for swim entries */
        } multisport;
        struct __attribute__((packed))
        {
            uint8_t _unk3[11];
            uint32_t duration;
            float    distance;
            uint32_t calories;
            uint32_t file_id;           /* does not contain valid data for swim entries */
            uint32_t swolf;             /* only exists for swim entries */
            uint32_t strokes_per_lap;   /* only exists for swim entries */
        } spark;
    };
} TTWATCH_HISTORY_ENTRY;

/*****************************************************************************/
typedef struct __attribute__((packed))
{
    uint32_t _unk1;
    uint16_t activity;
    uint16_t entry_length;
    uint16_t entry_count;
    uint16_t _unk2;
    uint8_t  data[1];   /* cannot be TT_HISTORY_ENTRY's because they are variable size... */
} TTWATCH_HISTORY_FILE;

/*****************************************************************************/
typedef struct __attribute__((packed))
{
    uint32_t file_type;     /* always 0x00720000 */
    uint32_t _unk;          /* always 0x0001e240 (123456dec) */
    uint8_t  split_time;    /* seconds */
    uint16_t split_distances[1]; /* centimeters (!) */
    /* the file length specifies how many entries there are... */
} TTWATCH_RACE_HISTORY_DATA_FILE;

/*****************************************************************************/
typedef struct __attribute__((packed))
{
    uint8_t  _unk;
    uint32_t entry_count;
    struct __attribute__((packed))
    {
        uint8_t tag;
        union
        {
            uint32_t int_val;
            float    float_val;
        };
    } entries[1];   /* entries[entry_count] */
} TTWATCH_HISTORY_DATA_FILE;

typedef enum
{
    TTWATCH_HISTORY_ENTRY_TAG_Duration     = 0x01,  /* int_val   = seconds       */
    TTWATCH_HISTORY_ENTRY_TAG_Distance     = 0x02,  /* float_val = metres        */
    TTWATCH_HISTORY_ENTRY_TAG_AveragePace  = 0x05,  /* float_val = metres/second */
    TTWATCH_HISTORY_ENTRY_TAG_AverageSpeed = 0x07,  /* float_val = metres/second */
    TTWATCH_HISTORY_ENTRY_TAG_Lengths      = 0x08,  /* int_val                   */
    TTWATCH_HISTORY_ENTRY_TAG_Laps         = 0x09,  /* int_val                   */
    TTWATCH_HISTORY_ENTRY_TAG_Swolf        = 0x0f,  /* int_val                   */
    TTWATCH_HISTORY_ENTRY_TAG_Strokes      = 0x13,  /* int_val                   */
    TTWATCH_HISTORY_ENTRY_TAG_Calories     = 0x14,  /* int_val                   */
    TTWATCH_HISTORY_ENTRY_TAG_RacePosition = 0x25,  /* int_val                   */
    TTWATCH_HISTORY_ENTRY_TAG_RaceSpeed    = 0x26,  /* float_val = metres/second */
    TTWATCH_HISTORY_ENTRY_TAG_RaceTime     = 0x27   /* int_val   = seconds       */
} TTWATCH_HISTORY_ENTRY_TAG;

/******************************************************************************
* File IDs                                                                    *
******************************************************************************/
#define TTWATCH_FILE_BLE_FIRMWARE           (0x00000012)
#define TTWATCH_FILE_SYSTEM_FIRMWARE        (0x000000f0)
#define TTWATCH_FILE_GPSQUICKFIX_DATA       (0x00010100)
#define TTWATCH_FILE_GPS_FIRMWARE           (0x00010200)
#define TTWATCH_FILE_BLE_UPDATE_LOG         (0x00013001)
#define TTWATCH_FILE_BLE_PEER_NAME          (0x00020002)
#define TTWATCH_FILE_BLE_DEVICE_DESCRIPTION (0x00020005)
#define TTWATCH_FILE_BLE_PAIRING_CODES      (0x0002000f)
#define TTWATCH_FILE_MANIFEST1              (0x00850000)
#define TTWATCH_FILE_MANIFEST2              (0x00850001)
#define TTWATCH_FILE_PREFERENCES_XML        (0x00f20000)

#define TTWATCH_FILE_TYPE_MASK              (0xffff0000)
#define TTWATCH_FILE_RACE_DATA              (0x00710000)
#define TTWATCH_FILE_RACE_HISTORY_DATA      (0x00720000)
#define TTWATCH_FILE_HISTORY_DATA           (0x00730000)
#define TTWATCH_FILE_HISTORY_SUMMARY        (0x00830000)
#define TTWATCH_FILE_TTBIN_DATA             (0x00910000)
#define TTWATCH_FILE_ACTIVITY_SUMMARY       (0x00b30000)

/******************************************************************************
* Callback function for ttwatch_enumerate_devices. The watch is open before   *
* this function is called. If the callback returns a non-zero function, the   *
* watch is closed and enumeration continues. If the callback function returns *
* 0, enumeration is cancelled and the watch is closed.                        *
******************************************************************************/
typedef int (*TTWATCH_DEVICE_ENUMERATOR)(TTWATCH *watch, void *data);

/******************************************************************************
* Callback function for ttwatch_enumerate_files.                              *
******************************************************************************/
typedef void (*TTWATCH_FILE_ENUMERATOR)(uint32_t id, uint32_t size, void *data);

/******************************************************************************
* Callback function for ttwatch_enumerate_offline_formats.                    *
******************************************************************************/
typedef void (*TTWATCH_FORMAT_ENUMERATOR)(const char *name, int auto_open, void *data);

/******************************************************************************
* Callback function for ttwatch_enumerate_races. 'index' is 0-based           *
******************************************************************************/
typedef void (*TTWATCH_RACE_ENUMERATOR)(TTWATCH_ACTIVITY activity, int index, const TTWATCH_RACE_FILE *file, void *data);

/******************************************************************************
* Callback function for ttwatch_enumerate_history_entries. 'index' is 0-based *
******************************************************************************/
typedef void (*TTWATCH_HISTORY_ENUMERATOR)(TTWATCH_ACTIVITY activity, int index, const TTWATCH_HISTORY_ENTRY *entry, void *data);

/******************************************************************************
* Device functions                                                            *
******************************************************************************/

/******************************************************************************
* Calls the callback function for each watch found attached to the system.    *
* 'data' is passed directly to the callback function                          *
******************************************************************************/
void ttwatch_enumerate_devices(TTWATCH_DEVICE_ENUMERATOR enumerator, void *data);

/******************************************************************************
* Opens a watch that matches the supplied serial number or name. If           *
* serial_or_name is null, matches the first watch found. If the function is   *
* successful, *watch contains a pointer to a watch structure.                 *
******************************************************************************/
int ttwatch_open(const char *serial_or_name, TTWATCH **watch);

/******************************************************************************
* Opens a watch specified by the given libusb device. The watch must still    *
* match the supplied serial number or name. If serial_or_name is 0, the watch *
* is opened regardless of its serial number or name. Note that the watch may  *
* still not be opened if an error occurs.                                     *
******************************************************************************/
int ttwatch_open_device(libusb_device *device, const char *serial_or_name, TTWATCH **watch);

/******************************************************************************
* Closes the watch and frees memory associated with the watch structure.      *
* The watch structure is freed and cannot be accessed after this function is  *
* called. Also writes the preferences and manifest files if they have been    *
* modified but not written. Set preferences_changed and manifest_changed in   *
* the watch structure to 0 to prevent this.                                   *
******************************************************************************/
int ttwatch_close(TTWATCH *watch);

/******************************************************************************
* File functions                                                              *
******************************************************************************/

/******************************************************************************
* Opens the specified file and returns a pointer to the file structure.       *
* 'read' is non-zero to open the file for reading, 0 to open for writing.     *
* Only one file can be open at a time, and once a file is opened,             *
* non-file-related commands will fail until the file is closed again.         *
******************************************************************************/
int ttwatch_open_file(TTWATCH *watch, uint32_t id, int read, TTWATCH_FILE **file);

/******************************************************************************
* Closes the specified file and frees memory used by the file structure.      *
* The file structure is freed and cannot be accessed after this function is   *
* called.                                                                     *
******************************************************************************/
int ttwatch_close_file(TTWATCH_FILE *file);

/******************************************************************************
* Deletes the specified file. A file cannot be opened when this function is   *
* called.                                                                     *
******************************************************************************/
int ttwatch_delete_file(TTWATCH *watch, uint32_t id);

/******************************************************************************
* Returns the size of the specified file.                                     *
******************************************************************************/
int ttwatch_get_file_size(TTWATCH_FILE *file, uint32_t *size);

/******************************************************************************
* Reads data from the specified file. The data buffer must be at least        *
* 'length' bytes long, otherwise a buffer overflow will occur. 'length' must  *
* be less than or equal to 50.                                                *
******************************************************************************/
int ttwatch_read_file_data(TTWATCH_FILE *file, void *data, uint32_t length);

/******************************************************************************
* Writes data to the specified file. 'length' must be less than or equal to   *
* 54 (yes, this is different to the maximum size for reading data; a          *
* limitation of the watch protocol). There is no error correction or error    *
* detection in the watch protocol, so verification is recommended.            *
******************************************************************************/
int ttwatch_write_file_data(TTWATCH_FILE *file, const void *data, uint32_t length);

/******************************************************************************
* Returns information about the first file on the watch and starts an         *
* enumeration. 'length' can be null if the file size is not required.         *
******************************************************************************/
int ttwatch_find_first_file(TTWATCH *watch, uint32_t *file_id, uint32_t *length);

/******************************************************************************
* Returns information about the next file on the watch.                       *
* ttwatch_find_first_file must have been called first. Returns                *
* TTWATCH_NoMoreFiles if the end of the file list has been reached. 'length'  *
* can be null if the file size is not required.                               *
******************************************************************************/
int ttwatch_find_next_file(TTWATCH *watch, uint32_t *file_id, uint32_t *length);

/******************************************************************************
* Finishes the file enumeration. This function MUST be called after a         *
* ttwatch_find_first_file/ttwatch_find_next_file enumeration, whether it      *
* fails or is successful, otherwise subsequent file enumerations will fail.   *
******************************************************************************/
int ttwatch_find_close(TTWATCH *watch);

/******************************************************************************
* Provides a simpler way to enumerate files using a callback function.        *
* The callback function is called for every file on the device whose file ID  *
* matches the 'type' parameter (or for every file if 'type' is 0). The files  *
* are not enumerated in any particular order. 'data' is passed directly to    *
* the callback function. This function guarantees that the enumeration        *
* functions are called correctly.                                             *
******************************************************************************/
int ttwatch_enumerate_files(TTWATCH *watch, uint32_t type, TTWATCH_FILE_ENUMERATOR enumerator, void *data);

/******************************************************************************
* Reads a whole file from the watch into memory.  Memory is allocated by the  *
* function and a pointer is returned in 'data'. 'length' is optional and can  *
* be 0 if the file size is not required. The memory allocated must be freed   *
* using 'free' when finished with. File sizes are variable. For reference, a  *
* 90-minute freestyle activity without cardio is roughly 200kB in size        *
******************************************************************************/
int ttwatch_read_whole_file(TTWATCH *watch, uint32_t id, void **data, uint32_t *length);

/******************************************************************************
* Writes a whole file from memory into the watch. Writes 'length' bytes from  *
* 'data' to the specified file. If the file exists on the watch already, it   *
* is deleted. There is no error correction or error detection in the watch    *
* protocol, so verification is recommended.                                   *
******************************************************************************/
int ttwatch_write_whole_file(TTWATCH *watch, uint32_t id, const void *data, uint32_t length);

/******************************************************************************
* Write a whole file from memory into the watch and verifies it was written   *
* correctly. Internally uses ttwatch_write_whole_file and                     *
* ttwatch_read_whole_file so see those methods for parameter information.     *
******************************************************************************/
int ttwatch_write_verify_whole_file(TTWATCH *watch, uint32_t id, const void *data, uint32_t length);

/******************************************************************************
* General functions                                                           *
******************************************************************************/

/******************************************************************************
* Resets the gps processor. Call after updating the GPSQuickFix ddata file    *
******************************************************************************/
int ttwatch_reset_gps_processor(TTWATCH *watch);

/******************************************************************************
* Resets the watch (including gps processor). Call after updating the watch   *
* firmware files. Calling this makes the watch disconnect from the USB for    *
* up to 90 seconds. This function returns immediately, so the delay must be   *
* accounted for in user code.                                                 *
******************************************************************************/
int ttwatch_reset_watch(TTWATCH *watch);

/******************************************************************************
* Retrieves the current time on the watch. This is UTC time, NOT local time.  *
******************************************************************************/
int ttwatch_get_watch_time(TTWATCH *watch, time_t *time);

/******************************************************************************
* Retrieves the watch serial number. No actual watch communications are       *
* performed as the serial number is taken from the system USB database. The   *
* serial number is stored in the watch structure when the watch is opened.    *
******************************************************************************/
int ttwatch_get_serial_number(TTWATCH *watch, char *serial, size_t max_length);

/******************************************************************************
* Retrieves the product ID from the watch. The product ID is stored in the    *
* watch structure when the watch is opened.                                   *
******************************************************************************/
int ttwatch_get_product_id(TTWATCH *watch, uint32_t *product_id);

/******************************************************************************
* Retrieves the firmware version number from the watch. The firmware version  *
* is stored in the watch structure when the watch is opened.                  *
******************************************************************************/
int ttwatch_get_firmware_version(TTWATCH *watch, uint32_t *firmware_version);

/******************************************************************************
* Retrieves the BLE (Bluetooth Low Energy) firmware version number from the   *
* watch. The BLE version is stored in the watch structure when the watch is   *
* opened.                                                                     *
******************************************************************************/
int ttwatch_get_ble_version(TTWATCH *watch, uint32_t *ble_version);

/******************************************************************************
* Sends a group of messages. This message group must be sent after:           *
* - the file list is read (done automatically by ttwatch_find_close)          *
* - the main firmware file is written                                         *
* - the GPS firmware file is written                                          *
******************************************************************************/
int ttwatch_send_message_group_1(TTWATCH *watch);

/******************************************************************************
* Sends a group of messages that must be sent at startup to initialise the    *
* watch. This group of messages is automatically sent by the functions that   *
* open the watch, so user code should never have to call it.                  *
******************************************************************************/
int ttwatch_send_startup_message_group(TTWATCH *watch);

/******************************************************************************
* Clears all activity data from the watch, including activity files, race     *
* history files (but not race definitions) and history entries. Does NOT      *
* clear user preferences or settings.                                         *
******************************************************************************/
int ttwatch_clear_data(TTWATCH *watch);

/******************************************************************************
* Formats the watch as part of a factory reset. Removes ALL user data and the *
* preferences file, manifest file, history files, races files and language    *
* files. The watch will be unusable until the firmware is reprogrammed, and   *
* the manifest and preferences files are rewritten. Races will not be         *
* available until the race files are rewritten                                *
******************************************************************************/
int ttwatch_format(TTWATCH *watch);

/******************************************************************************
* Preferences file functions                                                  *
******************************************************************************/

/******************************************************************************
* Creates a new blank user preferences file. Sets the preferences_changed     *
* flag so the file is automatically written when the watch is closed.         *
******************************************************************************/
int ttwatch_create_default_preferences_file(TTWATCH *watch);

/******************************************************************************
* Reloads the user preferences file from the watch. This file is XML and is   *
* stored in the watch structure. Clears the preferences_changed flag so the   *
* file is not automatically written when the watch is closed.                 *
******************************************************************************/
int ttwatch_reload_preferences(TTWATCH *watch);

/******************************************************************************
* Writes the preferences file to the watch. Clears the preferences_changed    *
* flag if the write was successul so the file is not automatically written    *
* when the watch is closed.                                                   *
******************************************************************************/
int ttwatch_write_preferences(TTWATCH *watch);

/******************************************************************************
* Updates the modification time in the preferences XML file. This is probably *
* not really necessary, but should be called whenever a change is made to the *
* preferences data. Sets the preferences_changed flag so the preferences file *
* is automatically written when the watch is closed.                          *
******************************************************************************/
int ttwatch_update_preferences_modified_time(TTWATCH *watch);

/******************************************************************************
* Retrieves the user-specified watch name. Stores at most 'max_length' bytes  *
* into 'name'. Reloads the preferences file if it hasn't been loaded yet.     *
* 'name' is always null-terminated.                                           *
******************************************************************************/
int ttwatch_get_watch_name(TTWATCH *watch, char *name, size_t max_length);

/******************************************************************************
* Updates the preferences file with the specified watch name. Sets the        *
* preferences_changed flag so the preferences file is automatically written   *
* when the watch is closed.                                                   *
******************************************************************************/
int ttwatch_set_watch_name(TTWATCH *watch, const char *name);

/******************************************************************************
* Calls the specified callback function for every offline file format that is *
* specified in the preferences file. 'data' is passed directly to the         *
* callback function. Reloads the preferences file if it hasn't been loaded.   *
******************************************************************************/
int ttwatch_enumerate_offline_formats(TTWATCH *watch, TTWATCH_FORMAT_ENUMERATOR enumerator, void *data);

/******************************************************************************
* Adds the specified offline file format to the preferences file. 'auto_open' *
* specifies whether the file is automatically opened when it is created,      *
* although this is ignored by the ttwatch command-line software. If the file  *
* format already exists in the file, the auto_open flag is updated to match   *
* the parameter given. No check is made to make sure the given format is a    *
* recognised file format.                                                     *
******************************************************************************/
int ttwatch_add_offline_format(TTWATCH *watch, const char *format, int auto_open);

/******************************************************************************
* Removes the specified offline file format from the preferences file. No     *
* error is returned if the file format does not exist in the file.            *
******************************************************************************/
int ttwatch_remove_offline_format(TTWATCH *watch, const char *format);

/******************************************************************************
* Manifest file functions                                                     *
******************************************************************************/

/******************************************************************************
* Reloads the watch manifest file containing user watch settings. This is     *
* stored inside the watch structure. Also clears the manifest_changed flag so *
* that the manifest is not written automatically when the watch is closed.    *
******************************************************************************/
int ttwatch_reload_manifest(TTWATCH *watch);

/******************************************************************************
* Writes the watch manifest file to the watch. Clears the manifest_changed    *
* if successfull so that the manifest is not written automatically when the   *
* watch is closed.                                                            *
******************************************************************************/
int ttwatch_write_manifest(TTWATCH *watch);

/******************************************************************************
* Retrieves the number of entries in the watch manifest file.                 *
******************************************************************************/
int ttwatch_get_manifest_entry_count(TTWATCH *watch, uint16_t *entry_count);

/******************************************************************************
* Retrieves a single entry from the watch manifest file. All entries are      *
* 32-bits in size and are either an integer, a float, or an enumeration. For  *
* integer or enumeration entries, 'data' should point to a 32-bit integer.    *
* For float entries, 'data' should point to a float. 'entry' is an index into *
* the manifest file and should be between 0 (inclusive) and the value         *
* returned by ttwatch_get_manifest_entry_count (exclusive).                   *
******************************************************************************/
int ttwatch_get_manifest_entry(TTWATCH *watch, uint16_t entry, uint32_t *data);

/******************************************************************************
* Sets a single entry in the watch manifest file. The parameters are as per   *
* ttwatch_get_manifest_entry.                                                 *
******************************************************************************/
int ttwatch_set_manifest_entry(TTWATCH *watch, uint16_t entry, const uint32_t *data);

/******************************************************************************
* Race functions                                                              *
******************************************************************************/

/******************************************************************************
* Calls the callback function once for each race specification found on the   *
* watch. 'data' is passed directly to the callback function. The races are    *
* not enumerated in any particular order.                                     *
******************************************************************************/
int ttwatch_enumerate_races(TTWATCH *watch, TTWATCH_RACE_ENUMERATOR enumerator, void *data);

/******************************************************************************
* Updates the specified race specification on the watch. 'index' is 0-based   *
* and must be betwen 0 and 4 (inclusive). 'index' is per-activity, so there   *
* are up to 5 races per activity type. 'distance' is in metres and 'duration' *
* is in seconds. 'name' can be up to 16 characters, but only the first 13 are *
* visible on the watch screen. 'checkpoints' is the number of checkpoints to  *
* use for determining how far in front/behind the user is during the race.    *
******************************************************************************/
int ttwatch_update_race(TTWATCH *watch, TTWATCH_ACTIVITY activity, int index,
    const char *name, uint32_t distance, uint32_t duration, int checkpoints);

/******************************************************************************
* Similar to ttwatch_update_race, but creates the race file from scratch,     *
* using the specified data for the unknown fields of the race file. The data  *
* array must be at least 22 bytes long, only the first 22 bytes are used.     *
******************************************************************************/
int ttwatch_create_race(TTWATCH *watch, TTWATCH_ACTIVITY activity, int index,
    const char *name, uint32_t distance, uint32_t duration, int checkpoints,
    const uint8_t *unknown_data);

/******************************************************************************
* History functions                                                           *
******************************************************************************/

/******************************************************************************
* Calls the callback function once for each history entry. 'data' is passed   *
* directly to the callback functions. All history entries for a particular    *
* activity type should be passed consecutively in oldest-to-newest order, but *
* activity types are not in any particular order.                             *
******************************************************************************/
int ttwatch_enumerate_history_entries(TTWATCH *watch, TTWATCH_HISTORY_ENUMERATOR enumerator, void *data);

/******************************************************************************
* Deletes one history entry from the watch. 'index' is 0-based. An error is   *
* returned if 'index' is out of range.                                        *
******************************************************************************/
int ttwatch_delete_history_entry(TTWATCH *watch, TTWATCH_ACTIVITY activity, int index);

/******************************************************************************
* Debugging functions                                                         *
******************************************************************************/

/******************************************************************************
* Turns on packet printing so that all communications to/from the watch can   *
* be logged and checked. Useful only for debugging. Prints the packet data to *
* standard output, one packet per line in hex format.                         *
******************************************************************************/
void ttwatch_show_packets(int show);

/******************************************************************************
* Retrieves the library version.                                              *
******************************************************************************/
int ttwatch_get_library_version();

#ifdef __cplusplus
}


#endif

#endif  // __LIBTTWATCH_H__
